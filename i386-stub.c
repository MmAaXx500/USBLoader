/****************************************************************************

        THIS SOFTWARE IS NOT COPYRIGHTED

   HP offers the following for use in the public domain.  HP makes no
   warranty with regard to the software or it's performance and the
   user accepts the software "AS IS" with all faults.

   HP DISCLAIMS ANY WARRANTIES, EXPRESS OR IMPLIED, WITH REGARD
   TO THIS SOFTWARE INCLUDING BUT NOT LIMITED TO THE WARRANTIES
   OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.

****************************************************************************/

/****************************************************************************
 *  Header: remcom.c,v 1.34 91/03/09 12:29:49 glenne Exp $
 *
 *  Module name: remcom.c $
 *  Revision: 1.34 $
 *  Date: 91/03/09 12:29:49 $
 *  Contributor:     Lake Stevens Instrument Division$
 *
 *  Description:     low level support for gdb debugger. $
 *
 *  Considerations:  only works on target hardware $
 *
 *  Written by:      Glenn Engel $
 *  ModuleState:     Experimental $
 *
 *  NOTES:           See Below $
 *
 *  Modified for 386 by Jim Kingdon, Cygnus Support.
 *
 *  To enable debugger support, two things need to happen.  One, a
 *  call to set_debug_traps() is necessary in order to allow any breakpoints
 *  or error conditions to be properly intercepted and reported to gdb.
 *  Two, a breakpoint needs to be generated to begin communication.  This
 *  is most easily accomplished by a call to breakpoint().  Breakpoint()
 *  simulates a breakpoint by executing a trap #1.
 *
 *  The external function exceptionHandler() is
 *  used to attach a specific handler to a specific 386 vector number.
 *  It should use the same privilege level it runs at.  It should
 *  install it as an interrupt gate so that interrupts are masked
 *  while the handler runs.
 *
 *  Because gdb will sometimes write to the stack area to execute function
 *  calls, this program cannot rely on using the supervisor stack so it
 *  uses its own stack area reserved in the int array remcomStack.
 *
 *************
 *
 *    The following gdb commands are supported:
 *
 * command          function                               Return value
 *
 *    g             return the value of the CPU registers  hex data or ENN
 *    G             set the value of the CPU registers     OK or ENN
 *
 *    mAA..AA,LLLL  Read LLLL bytes at address AA..AA      hex data or ENN
 *    MAA..AA,LLLL: Write LLLL bytes at address AA.AA      OK or ENN
 *
 *    c             Resume at current address              SNN   ( signal NN)
 *    cAA..AA       Continue at address AA..AA             SNN
 *
 *    s             Step one instruction                   SNN
 *    sAA..AA       Step one instruction from AA..AA       SNN
 *
 *    k             kill
 *
 *    ?             What was the last sigval ?             SNN   (signal NN)
 *
 * All commands and responses are sent with a packet which includes a
 * checksum.  A packet consists of
 *
 * $<packet info>#<checksum>.
 *
 * where
 * <packet info> :: <characters representing the command or response>
 * <checksum>    :: < two hex digits computed as modulo 256 sum of <packetinfo>>
 *
 * When a packet is received, it is first acknowledged with either '+' or '-'.
 * '+' indicates a successful transfer.  '-' indicates a failed transfer.
 *
 * Example:
 *
 * Host:                  Reply:
 * $m0,10#2a               +$00010203040506070809101112131415#42
 *
 ****************************************************************************/

#include <stddef.h>

#include "gdbstub.h"
#include "mem.h"
#include "print.h"

/************************************************************************
 *
 * external low-level support routines
 */

extern void putDebugChar(char ch); /* write a single character      */
extern int getDebugChar(void);         /* read and return a single char */
extern void
exceptionHandler(int exception_number,
                 void *exception_address); /* assign an exception handler   */

/************************************************************************/
/* BUFMAX defines the maximum number of characters in inbound/outbound buffers*/
/* at least NUMREGBYTES*2 are needed for register packets */
#define BUFMAX 400

static char initialized; /* boolean flag. != 0 means we've been initialized */

int remote_debug;
/*  debug >  0 prints ill-formed commands in valid packets & checksum errors */

static const char hexchars[] = "0123456789abcdef";

/* Number of registers.  */
#define NUMREGS 16

/* Number of bytes of registers.  */
#define NUMREGBYTES (NUMREGS * 4)

enum regnames {
	EAX,
	ECX,
	EDX,
	EBX,
	ESP,
	EBP,
	ESI,
	EDI,
	PC /* also known as eip */,
	PS /* also known as eflags */,
	CS,
	SS,
	DS,
	ES,
	FS,
	GS
};

/*
 * these should not be static cuz they can be used outside this module
 */
int registers[NUMREGS];

#define STACKSIZE 10000
int remcomStack[STACKSIZE / sizeof(int)];
__attribute__((used)) static int *stackPtr = &remcomStack[STACKSIZE / sizeof(int) - 1];

/***************************  ASSEMBLY CODE MACROS *************************/
/* 									   */

extern void return_to_prog();

/* Restore the program's registers (including the stack pointer, which
   means we get the right stack and don't have to worry about popping our
   return address and any stack frames and so on) and return.  */
__asm__(".text");
__asm__(".globl return_to_prog");
__asm__("return_to_prog:");
__asm__("        movw registers+44, %ss");
__asm__("        movl registers+16, %esp");
__asm__("        movl registers+4, %ecx");
__asm__("        movl registers+8, %edx");
__asm__("        movl registers+12, %ebx");
__asm__("        movl registers+20, %ebp");
__asm__("        movl registers+24, %esi");
__asm__("        movl registers+28, %edi");
__asm__("        movw registers+48, %ds");
__asm__("        movw registers+52, %es");
__asm__("        movw registers+56, %fs");
__asm__("        movw registers+60, %gs");
__asm__("        movl registers+36, %eax");
__asm__("        pushl %eax"); /* saved eflags */
__asm__("        movl registers+40, %eax");
__asm__("        pushl %eax"); /* saved cs */
__asm__("        movl registers+32, %eax");
__asm__("        pushl %eax"); /* saved eip */
__asm__("        movl registers, %eax");
/* use iret to restore pc and flags together so
   that trace flag works right.  */
__asm__("        iret");

#define BREAKPOINT() __asm__("   int $3");

/* Put the error code here just in case the user cares.  */
int gdb_i386errcode;
/* Likewise, the vector number here (since GDB only gets the signal
   number through the usual means, and that's not very specific).  */
int gdb_i386vector = -1;

/* GDB stores segment registers in 32-bit words (that's just the way
   m-i386v.h is written).  So zero the appropriate areas in registers.  */
#define SAVE_REGISTERS1()                                                      \
	__asm__("movl %eax, registers");                                           \
	__asm__("movl %ecx, registers+4");                                         \
	__asm__("movl %edx, registers+8");                                         \
	__asm__("movl %ebx, registers+12");                                        \
	__asm__("movl %ebp, registers+20");                                        \
	__asm__("movl %esi, registers+24");                                        \
	__asm__("movl %edi, registers+28");                                        \
	__asm__("movw $0, %ax");                                                   \
	__asm__("movw %ds, registers+48");                                         \
	__asm__("movw %ax, registers+50");                                         \
	__asm__("movw %es, registers+52");                                         \
	__asm__("movw %ax, registers+54");                                         \
	__asm__("movw %fs, registers+56");                                         \
	__asm__("movw %ax, registers+58");                                         \
	__asm__("movw %gs, registers+60");                                         \
	__asm__("movw %ax, registers+62");
#define SAVE_ERRCODE()                                                         \
	__asm__("popl %ebx");                                                      \
	__asm__("movl %ebx, gdb_i386errcode");
#define SAVE_REGISTERS2()                                                      \
	__asm__("popl %ebx"); /* old eip */                                        \
	__asm__("movl %ebx, registers+32");                                        \
	__asm__("popl %ebx"); /* old cs */                                         \
	__asm__("movl %ebx, registers+40");                                        \
	__asm__("movw %ax, registers+42");                                         \
	__asm__("popl %ebx"); /* old eflags */                                     \
	__asm__("movl %ebx, registers+36");                                        \
	/* Now that we've done the pops, we can save the stack pointer.");  */     \
	__asm__("movw %ss, registers+44");                                         \
	__asm__("movw %ax, registers+46");                                         \
	__asm__("movl %esp, registers+16");

/* See if mem_fault_routine is set, if so just IRET to that address.  */
#define CHECK_FAULT()                                                          \
	__asm__("cmpl $0, mem_fault_routine");                                     \
	__asm__("jne mem_fault");

__asm__(".text");
__asm__("mem_fault:");
/* OK to clobber temp registers; we're just going to end up in set_mem_err.  */
/* Pop error code from the stack and save it.  */
__asm__("     popl %eax");
__asm__("     movl %eax, gdb_i386errcode");

__asm__("     popl %eax"); /* eip */
/* We don't want to return there, we want to return to the function
   pointed to by mem_fault_routine instead.  */
__asm__("     movl mem_fault_routine, %eax");
__asm__("     popl %ecx"); /* cs (low 16 bits; junk in hi 16 bits).  */
__asm__("     popl %edx"); /* eflags */

/* Remove this stack frame; when we do the iret, we will be going to
   the start of a function, so we want the stack to look just like it
   would after a "call" instruction.  */
__asm__("     leave");

/* Push the stuff that iret wants.  */
__asm__("     pushl %edx"); /* eflags */
__asm__("     pushl %ecx"); /* cs */
__asm__("     pushl %eax"); /* eip */

/* Zero mem_fault_routine.  */
__asm__("     movl $0, %eax");
__asm__("     movl %eax, mem_fault_routine");

__asm__("iret");

#define CALL_HOOK() __asm__("call remcomHandler");

/* This function is called when a i386 exception occurs.  It saves
 * all the cpu regs in the registers array, munges the stack a bit,
 * and invokes an exception handler (remcom_handler).
 *
 * stack on entry:                       stack on exit:
 *   old eflags                          vector number
 *   old cs (zero-filled to 32 bits)
 *   old eip
 *
 */
extern void catchException3();
__asm__(".text");
__asm__(".globl catchException3");
__asm__("catchException3:");
SAVE_REGISTERS1();
SAVE_REGISTERS2();
__asm__("pushl $3");
CALL_HOOK();

/* Same thing for exception 1.  */
extern void catchException1();
__asm__(".text");
__asm__(".globl catchException1");
__asm__("catchException1:");
SAVE_REGISTERS1();
SAVE_REGISTERS2();
__asm__("pushl $1");
CALL_HOOK();

/* Same thing for exception 0.  */
extern void catchException0();
__asm__(".text");
__asm__(".globl catchException0");
__asm__("catchException0:");
SAVE_REGISTERS1();
SAVE_REGISTERS2();
__asm__("pushl $0");
CALL_HOOK();

/* Same thing for exception 4.  */
extern void catchException4();
__asm__(".text");
__asm__(".globl catchException4");
__asm__("catchException4:");
SAVE_REGISTERS1();
SAVE_REGISTERS2();
__asm__("pushl $4");
CALL_HOOK();

/* Same thing for exception 5.  */
extern void catchException5();
__asm__(".text");
__asm__(".globl catchException5");
__asm__("catchException5:");
SAVE_REGISTERS1();
SAVE_REGISTERS2();
__asm__("pushl $5");
CALL_HOOK();

/* Same thing for exception 6.  */
extern void catchException6();
__asm__(".text");
__asm__(".globl catchException6");
__asm__("catchException6:");
SAVE_REGISTERS1();
SAVE_REGISTERS2();
__asm__("pushl $6");
CALL_HOOK();

/* Same thing for exception 7.  */
extern void catchException7();
__asm__(".text");
__asm__(".globl catchException7");
__asm__("catchException7:");
SAVE_REGISTERS1();
SAVE_REGISTERS2();
__asm__("pushl $7");
CALL_HOOK();

/* Same thing for exception 8.  */
extern void catchException8();
__asm__(".text");
__asm__(".globl catchException8");
__asm__("catchException8:");
SAVE_REGISTERS1();
SAVE_ERRCODE();
SAVE_REGISTERS2();
__asm__("pushl $8");
CALL_HOOK();

/* Same thing for exception 9.  */
extern void catchException9();
__asm__(".text");
__asm__(".globl catchException9");
__asm__("catchException9:");
SAVE_REGISTERS1();
SAVE_REGISTERS2();
__asm__("pushl $9");
CALL_HOOK();

/* Same thing for exception 10.  */
extern void catchException10();
__asm__(".text");
__asm__(".globl catchException10");
__asm__("catchException10:");
SAVE_REGISTERS1();
SAVE_ERRCODE();
SAVE_REGISTERS2();
__asm__("pushl $10");
CALL_HOOK();

/* Same thing for exception 12.  */
extern void catchException12();
__asm__(".text");
__asm__(".globl catchException12");
__asm__("catchException12:");
SAVE_REGISTERS1();
SAVE_ERRCODE();
SAVE_REGISTERS2();
__asm__("pushl $12");
CALL_HOOK();

/* Same thing for exception 16.  */
extern void catchException16();
__asm__(".text");
__asm__(".globl catchException16");
__asm__("catchException16:");
SAVE_REGISTERS1();
SAVE_REGISTERS2();
__asm__("pushl $16");
CALL_HOOK();

/* For 13, 11, and 14 we have to deal with the CHECK_FAULT stuff.  */

/* Same thing for exception 13.  */
extern void catchException13();
__asm__(".text");
__asm__(".globl catchException13");
__asm__("catchException13:");
CHECK_FAULT();
SAVE_REGISTERS1();
SAVE_ERRCODE();
SAVE_REGISTERS2();
__asm__("pushl $13");
CALL_HOOK();

/* Same thing for exception 11.  */
extern void catchException11();
__asm__(".text");
__asm__(".globl catchException11");
__asm__("catchException11:");
CHECK_FAULT();
SAVE_REGISTERS1();
SAVE_ERRCODE();
SAVE_REGISTERS2();
__asm__("pushl $11");
CALL_HOOK();

/* Same thing for exception 14.  */
extern void catchException14();
__asm__(".text");
__asm__(".globl catchException14");
__asm__("catchException14:");
CHECK_FAULT();
SAVE_REGISTERS1();
SAVE_ERRCODE();
SAVE_REGISTERS2();
__asm__("pushl $14");
CALL_HOOK();

/*
 * remcomHandler is a front end for handle_exception.  It moves the
 * stack pointer into an area reserved for debugger use.
 */
__asm__("remcomHandler:");
__asm__("           popl %eax");          /* pop off return address     */
__asm__("           popl %eax");          /* get the exception number   */
__asm__("		movl stackPtr, %esp");    /* move to remcom stack area  */
__asm__("		pushl %eax");             /* push exception onto stack  */
__asm__("		call  handle_exception"); /* this never returns */

void _returnFromException() { return_to_prog(); }

int hex(char ch) {
	if ((ch >= 'a') && (ch <= 'f'))
		return (ch - 'a' + 10);
	if ((ch >= '0') && (ch <= '9'))
		return (ch - '0');
	if ((ch >= 'A') && (ch <= 'F'))
		return (ch - 'A' + 10);
	return (-1);
}

static char remcomInBuffer[BUFMAX];
static char remcomOutBuffer[BUFMAX];

/* scan for the sequence $<data>#<checksum>     */

unsigned char *getpacket(void) {
	unsigned char *buffer = (unsigned char *)&remcomInBuffer[0];
	unsigned char checksum;
	unsigned char xmitcsum;
	int count;
	char ch;
	char print_buf[16];

	while (1) {
		/* wait around for the start character, ignore all other characters */
		while ((ch = (char)getDebugChar()) != '$')
			;

	retry:
		checksum = 0;
		xmitcsum = (unsigned char)-1;
		count = 0;

		/* now, read until a # or end of buffer is found */
		while (count < BUFMAX - 1) {
			ch = (char)getDebugChar();
			if (ch == '$')
				goto retry;
			if (ch == '#')
				break;
			checksum = (unsigned char)(checksum + ch);
			buffer[count] = (unsigned char)ch;
			count = count + 1;
		}
		buffer[count] = 0;

		if (ch == '#') {
			ch = (char)getDebugChar();
			xmitcsum = (unsigned char)hex(ch) << 4;
			ch = (char)getDebugChar();
			xmitcsum += (unsigned char)hex(ch);

			if (checksum != xmitcsum) {
				if (remote_debug) {
					print_string("bad checksum.  My count = 0x");
					print_string(itoa(checksum, print_buf, 16));
					print_string(", sent=0x");
					print_string(itoa(xmitcsum, print_buf, 16));
					print_string(". buf=");
					print_string((char *)buffer);
				}
				putDebugChar('-'); /* failed checksum */
			} else {
				putDebugChar('+'); /* successful transfer */

				/* if a sequence char is present, reply the sequence ID */
				if (buffer[2] == ':') {
					putDebugChar((char)buffer[0]);
					putDebugChar((char)buffer[1]);

					return &buffer[3];
				}

				return &buffer[0];
			}
		}
	}
}

/* send the packet in buffer.  */

void putpacket(unsigned char *buffer) {
	unsigned char checksum;
	int count;
	char ch;

	/*  $<packet info>#<checksum>.  */
	do {
		putDebugChar('$');
		checksum = 0;
		count = 0;

		while ((ch = (char)buffer[count])) {
			putDebugChar(ch);
			checksum += (unsigned char)ch;
			count += 1;
		}

		putDebugChar('#');
		putDebugChar(hexchars[checksum >> 4]);
		putDebugChar(hexchars[checksum % 16]);

	} while (getDebugChar() != '+');
}

/* Address of a routine to RTE to if we get a memory fault.  */
static void (*volatile mem_fault_routine)() = NULL;

/* Indicate to caller of mem2hex or hex2mem that there has been an
   error.  */
static volatile int mem_err = 0;

void set_mem_err(void) { mem_err = 1; }

/* These are separate functions so that they are so short and sweet
   that the compiler won't save any registers (if there is a fault
   to mem_fault, they won't get restored, so there better not be any
   saved).  */
char get_char(char *addr) { return *addr; }

void set_char(char *addr, char val) { *addr = val; }

/* convert the memory pointed to by mem into hex, placing result in buf */
/* return a pointer to the last char put in buf (null) */
/* If MAY_FAULT is non-zero, then we should set mem_err in response to
   a fault; if zero treat a fault like any other fault in the stub.  */
char *mem2hex(char *mem, char *buf, int count, int may_fault) {
	int i;
	unsigned char ch;

	if (may_fault)
		mem_fault_routine = set_mem_err;
	for (i = 0; i < count; i++) {
		ch = (unsigned char)get_char(mem++);
		if (may_fault && mem_err)
			return (buf);
		*buf++ = hexchars[ch >> 4];
		*buf++ = hexchars[ch % 16];
	}
	*buf = 0;
	if (may_fault)
		mem_fault_routine = NULL;
	return (buf);
}

/* convert the hex array pointed to by buf into binary to be placed in mem */
/* return a pointer to the character AFTER the last byte written */
char *hex2mem(char *buf, char *mem, int count, int may_fault) {
	int i;
	unsigned char ch;

	if (may_fault)
		mem_fault_routine = set_mem_err;
	for (i = 0; i < count; i++) {
		ch = (unsigned char)hex(*buf++) << 4;
		ch = ch + (unsigned char)hex(*buf++);
		set_char(mem++, (char)ch);
		if (may_fault && mem_err)
			return (mem);
	}
	if (may_fault)
		mem_fault_routine = NULL;
	return (mem);
}

/* this function takes the 386 exception vector and attempts to
   translate this number into a unix compatible signal value */
int computeSignal(int exceptionVector) {
	int sigval;
	switch (exceptionVector) {
	case 0:
		sigval = 8;
		break; /* divide by zero */
	case 1:
		sigval = 5;
		break; /* debug exception */
	case 3:
		sigval = 5;
		break; /* breakpoint */
	case 4:
		sigval = 16;
		break; /* into instruction (overflow) */
	case 5:
		sigval = 16;
		break; /* bound instruction */
	case 6:
		sigval = 4;
		break; /* Invalid opcode */
	case 7:
		sigval = 8;
		break; /* coprocessor not available */
	case 8:
		sigval = 7;
		break; /* double fault */
	case 9:
		sigval = 11;
		break; /* coprocessor segment overrun */
	case 10:
		sigval = 11;
		break; /* Invalid TSS */
	case 11:
		sigval = 11;
		break; /* Segment not present */
	case 12:
		sigval = 11;
		break; /* stack exception */
	case 13:
		sigval = 11;
		break; /* general protection */
	case 14:
		sigval = 11;
		break; /* page fault */
	case 16:
		sigval = 7;
		break; /* coprocessor error */
	default:
		sigval = 7; /* "software generated" */
	}
	return (sigval);
}

/**********************************************/
/* WHILE WE FIND NICE HEX CHARS, BUILD AN INT */
/* RETURN NUMBER OF CHARS PROCESSED           */
/**********************************************/
int hexToInt(char **ptr, int *intValue) {
	int numChars = 0;
	int hexValue;

	*intValue = 0;

	while (**ptr) {
		hexValue = hex(**ptr);
		if (hexValue >= 0) {
			*intValue = (*intValue << 4) | hexValue;
			numChars++;
		} else
			break;

		(*ptr)++;
	}

	return (numChars);
}

/*
 * This function does all command procesing for interfacing to gdb.
 */
void handle_exception(int exceptionVector) {
	int sigval, stepping;
	int addr, length;
	char *ptr;
	char print_buf[16];

	gdb_i386vector = exceptionVector;

	if (remote_debug) {
		print_string("vector=");
		print_string(itoa(exceptionVector, print_buf, 10));
		print_string(", sr=0x");
		print_string(itoa(registers[PS], print_buf, 16));
		print_string(", pc=0x");
		print_string(itoa(registers[PC], print_buf, 16));
		print_string("\n");
	}

	/* reply to host that an exception has occurred */
	sigval = computeSignal(exceptionVector);

	ptr = remcomOutBuffer;

	*ptr++ = 'T'; /* notify gdb with signo, PC, FP and SP */
	*ptr++ = hexchars[sigval >> 4];
	*ptr++ = hexchars[sigval & 0xf];

	*ptr++ = hexchars[ESP];
	*ptr++ = ':';
	ptr = mem2hex((char *)&registers[ESP], ptr, 4, 0); /* SP */
	*ptr++ = ';';

	*ptr++ = hexchars[EBP];
	*ptr++ = ':';
	ptr = mem2hex((char *)&registers[EBP], ptr, 4, 0); /* FP */
	*ptr++ = ';';

	*ptr++ = hexchars[PC];
	*ptr++ = ':';
	ptr = mem2hex((char *)&registers[PC], ptr, 4, 0); /* PC */
	*ptr++ = ';';

	*ptr = '\0';

	putpacket((unsigned char *)remcomOutBuffer);

	stepping = 0;

	while (1 == 1) {
		remcomOutBuffer[0] = 0;
		ptr = (char *)getpacket();

		switch (*ptr++) {
		case '?':
			remcomOutBuffer[0] = 'S';
			remcomOutBuffer[1] = hexchars[sigval >> 4];
			remcomOutBuffer[2] = hexchars[sigval % 16];
			remcomOutBuffer[3] = 0;
			break;
		case 'd':
			remote_debug = !(remote_debug); /* toggle debug flag */
			break;
		case 'g': /* return the value of the CPU registers */
			mem2hex((char *)registers, remcomOutBuffer, NUMREGBYTES, 0);
			break;
		case 'G': /* set the value of the CPU registers - return OK */
			hex2mem(ptr, (char *)registers, NUMREGBYTES, 0);
			scopy(remcomOutBuffer, "OK");
			break;
		case 'P': /* set the value of a single CPU register - return OK */
		{
			int regno;

			if (hexToInt(&ptr, &regno) && *ptr++ == '=')
				if (regno >= 0) {
					if(regno < NUMREGS)
						hex2mem(ptr, (char *)&registers[regno], 4, 0);

					scopy(remcomOutBuffer, "OK");
					break;
				}

			scopy(remcomOutBuffer, "E01");
			break;
		}

			/* mAA..AA,LLLL  Read LLLL bytes at address AA..AA */
		case 'm':
			/* TRY TO READ %x,%x.  IF SUCCEED, SET PTR = 0 */
			if (hexToInt(&ptr, &addr))
				if (*(ptr++) == ',')
					if (hexToInt(&ptr, &length)) {
						ptr = 0;
						mem_err = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
						// Assuming that 32bit pointers fit into an int
						mem2hex((char *)addr, remcomOutBuffer, length, 1);
#pragma GCC diagnostic pop

						if (mem_err) {
							scopy(remcomOutBuffer, "E03");
							print_string("memory fault");
						}
					}

			if (ptr) {
				scopy(remcomOutBuffer, "E01");
			}
			break;

			/* MAA..AA,LLLL: Write LLLL bytes at address AA.AA return OK */
		case 'M':
			/* TRY TO READ '%x,%x:'.  IF SUCCEED, SET PTR = 0 */
			if (hexToInt(&ptr, &addr))
				if (*(ptr++) == ',')
					if (hexToInt(&ptr, &length))
						if (*(ptr++) == ':') {
							mem_err = 0;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
							// Assuming that 32bit pointers fit into an int
							hex2mem(ptr, (char *)addr, length, 1);
#pragma GCC diagnostic pop

							if (mem_err) {
								scopy(remcomOutBuffer, "E03");
								print_string("memory fault");
							} else {
								scopy(remcomOutBuffer, "OK");
							}

							ptr = 0;
						}
			if (ptr) {
				scopy(remcomOutBuffer, "E02");
			}
			break;

			/* cAA..AA    Continue at address AA..AA(optional) */
			/* sAA..AA   Step one instruction from AA..AA(optional) */
		case 's':
			stepping = 1;
			__attribute__((fallthrough));
		case 'c':
			/* try to read optional parameter, pc unchanged if no parm */
			if (hexToInt(&ptr, &addr))
				registers[PC] = addr;

			/* clear the trace bit */
			registers[PS] &= (int)0xfffffeff;

			/* set the trace bit if we're stepping */
			if (stepping)
				registers[PS] |= 0x100;

			_returnFromException(); /* this is a jump */
			break;

			/* kill the program */
		case 'k': /* do nothing */
#if 0
	  /* Huh? This doesn't look like "nothing".
	     m68k-stub.c and sparc-stub.c don't have it.  */
	  BREAKPOINT ();
#endif
			break;
		} /* switch */

		/* reply to the request */
		putpacket((unsigned char *)remcomOutBuffer);
	}
}

/* this function is used to set up exception handlers for tracing and
   breakpoints */
void set_debug_traps(void) {
	stackPtr = &remcomStack[STACKSIZE / sizeof(int) - 1];

	exceptionHandler(0, catchException0);
	exceptionHandler(1, catchException1);
	exceptionHandler(3, catchException3);
	exceptionHandler(4, catchException4);
	exceptionHandler(5, catchException5);
	exceptionHandler(6, catchException6);
	exceptionHandler(7, catchException7);
	exceptionHandler(8, catchException8);
	exceptionHandler(9, catchException9);
	exceptionHandler(10, catchException10);
	exceptionHandler(11, catchException11);
	exceptionHandler(12, catchException12);
	exceptionHandler(13, catchException13);
	exceptionHandler(14, catchException14);
	exceptionHandler(16, catchException16);

	initialized = 1;
}

/* This function will generate a breakpoint exception.  It is used at the
   beginning of a program to sync up with a debugger and can be used
   otherwise as a quick means to stop program execution and "break" into
   the debugger.  */

void breakpoint(void) {
	if (initialized)
		BREAKPOINT();
}
