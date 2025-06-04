%include "io.asm"

[BITS 16]
enter_protected:
    cli
    lgdt [gdtr]
    mov eax, cr0
    or al, 1
    mov cr0, eax

    jmp 0x8:protected_init

%define GDT_LIMIT(seg)  (((seg)  & 0x0000ffff) | \
                       ((((seg)  & 0x000f0000) >> 16) << 48))
%define GDT_BASE(base) ((((base) & 0x00ffffff)        << 16) | \
                       ((((base) & 0xff000000) >> 24) << 56))
%define GDT_TYPE(type)       (((type)    & 0x1f) << 40)
%define GDT_PRIV_LVL(lvl)    (((lvl)     & 0x3)  << 45)
%define GDT_PRESENT(present) (((present) & 0x1)  << 47)
%define GDT_ATTS(atts)       (((atts)    & 0x7)  << 52)
%define GDT_GRAN(gran)   (((gran)  & 0x1)  << 55)

gdtr:
    dw gdt_end - gdt - 1
    dd gdt

gdt:
    ;ALIGN 16
    dq 0x0   ; null entry

    ; Code segment
    ; Limit: 4GiB (gran = 1)
    ; Base: start at 0
    ; Type: system, executable, grows up, conforming, rw, not accessed
    ; Privilege level: ring 0
    ; Present:  must be 1
    ; Attributes: 32-bit protected mode
    ; Granularity: 4KiB blocks
    dq GDT_LIMIT(0x0fffff) | GDT_BASE(0x0) | GDT_TYPE(0x1a) | GDT_PRIV_LVL(0x0) | GDT_PRESENT(0x1) | GDT_ATTS(0x4) | GDT_GRAN(0x1)

    ; Data segment
    ; Limit: 4GiB (gran = 1)
    ; Base: start at 0
    ; Type: system, not executable, grows up, conforming, rw, not accessed
    ; Privilege level: ring 0
    ; Present:  must be 1
    ; Attributes: 32-bit protected mode
    ; Granularity: 4KiB blocks
    dq GDT_LIMIT(0x0fffff) | GDT_BASE(0x0) | GDT_TYPE(0x12) | GDT_PRIV_LVL(0x0) | GDT_PRESENT(0x1) | GDT_ATTS(0x4) | GDT_GRAN(0x1)
gdt_end:

[BITS 32]

PIC1_COMMAND equ 0x20
PIC1_DATA equ PIC1_COMMAND + 1
PIC2_COMMAND equ 0xa0
PIC2_DATA equ PIC2_COMMAND + 1

PIC_EOI equ 0x20

ICW1_ICW4 equ 0x01
ICW1_INIT equ 0x10

; Cascade, slave on IRQ2
ICW3_M equ 0x4
; slave identification code
ICW3_S equ 0x2

; Intel/Microprocessor mode
ICW4_INTEL equ 0x1

extern stage2_main

protected_init:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov esp, 0x7bfe  ; stack is before the bootloader (16 bit align)

    ; A20 check
    mov edi, 0x111111   ; A20 = 1
    mov esi, 0x011111   ; A20 = 0
    mov [edi], edi
    mov [esi], esi
    cmpsd   ; [esi] == [edi] ?
    jne a20_ok

a20_ena:
    ; Assumming Fast A20 Gate support
    inb 0x92
    or al, 2
    outb 0x92, al

a20_ok:
    mov ah, 32  ; master PIC offset
    mov al, 40  ; slave PIC offset
    call PicRemap

    xor ax, ax
    mov di, ax  ; INT num
    lea eax, idt
    lea esi, interrupt_map

idt_loop:
    movzx dx, [esi]
    cmp di, dx
    jne .use_dummy

    movzx bx, [esi + 1] ; int/trap flag
    mov ecx, [esi + 2]  ; handler

    add esi, 6  ; next INT map entry
    jmp .idt_set

.use_dummy:
    ; Normal interrupts
    cmp di, 8   ; #DF
    je .use_dummy_ec
    cmp di, 17  ; #AC
    je .use_dummy_ec
    cmp di, 21  ; #CP
    je .use_dummy_ec

    ; PIC interrupts
    cmp di, 47  ; last slave PIC INT
    ja .use_dummy_nrm
    cmp di, 40  ; first slave PIC INT
    jae .use_dummy_pic_slave
    cmp di, 32  ; first master PIC INT
    jae .use_dummy_pic_master

    ; Normal interrupts again
    cmp di, 14
    ja .use_dummy_nrm
    cmp di, 10  ; #TS, #NP, #SS, #GP, #PF
    jae .use_dummy_ec
    cmp di, 2   ; nonmaskable
    jne .use_dummy_nrm

.use_dummy_int:
    mov bx, 0
    lea ecx, isr_dummy
    jmp .idt_set

.use_dummy_ec:
    mov bx, 1
    lea ecx, isr_dummy_ec
    jmp .idt_set

.use_dummy_pic_slave:
    mov bx, 1
    lea ecx, isr_dummy_pic_slave
    jmp .idt_set

.use_dummy_pic_master:
    mov bx, 1
    lea ecx, isr_dummy_pic_master
    jmp .idt_set

.use_dummy_nrm:
    mov bx, 1
    lea ecx, isr_dummy

.idt_set:
    call SetIdtEntry

    add eax, 8
    inc di

    cmp eax, idt_end
    jne idt_loop

    lidt [idtr]

    sti

    call stage2_main

; ========== Interrupt handlers ==========

%macro pic_send_eoi 1
%if %1 >= 8
    outb PIC2_COMMAND, PIC_EOI
%endif
    outb PIC1_COMMAND, PIC_EOI
%endmacro

isr_dummy:
    iret

isr_dummy_ec:
    add esp, 4  ; pop error code
    iret

isr_doublefault:
    hlt

isr_dummy_pic_slave:
    pusha
    ; real PIC INT num doesn't matter for EOI
    pic_send_eoi 8
    popa
    iret

isr_dummy_pic_master:
    pusha
    ; real PIC INT num doesn't matter for EOI
    pic_send_eoi 0
    popa
    iret

; ========== Interrupt handlers end ==========

; Set an enty in the Interrupt Description Table
; Params: EAX: pointer to the entry
;          BX: 0: interrupt, 1: trap
;         ECX: pointer to the handler
; Clobber: EBX, EDX
SetIdtEntry:
    mov edx, 0x8 << 16  ; segment selector
    mov dx, cx          ; low 16 bits of the handler
    mov [eax], edx

    mov edx, ecx                ; high 16 bits of the handler
    mov dx, 1000111000000000b   ; present, DPL ring 0, Type interrupt/trap
    shl bx, 8
    or  dx, bx                  ; set trap optionally
    mov [eax + 4], edx
    ret

; Params: AH: master PIC offset, bits 2:0 must be 0 (Interrupt Request Level)
;         AL: slave PIC offset, bits 2:0 must be 0 (Interrupt Request Level)
; Clobber: AX
PicRemap:
    push eax

    ; init in cascade mode
    ; ICW2, ICW3, ICW4 expected
    outb PIC1_COMMAND, ICW1_INIT | ICW1_ICW4
    outb PIC2_COMMAND, ICW1_INIT | ICW1_ICW4

    ; ICW2
    outb PIC1_DATA, [esp + 1]
    outb PIC2_DATA, [esp]

    pop eax

    ; ICW3 Master
    outb PIC1_DATA, ICW3_M

    ; ICW3 Slave
    outb PIC2_DATA, ICW3_S

    ; ICW4
    outb PIC1_DATA, ICW4_INTEL
    outb PIC2_DATA, ICW4_INTEL

    ; OCW1 Interrupt mask
    ; unmask interrupts
    outb PIC1_DATA, 0
    outb PIC2_DATA, 0

    ret


; Map interrupts to handlers
; format: interrupt no (byte), int=0/fault=1 (byte), handler pointer (32bit ptr)
interrupt_map:
    db 8, 1
    dd isr_doublefault

    db 0, 0
    dd 0

idtr:
    dw idt_end - idt - 1
    dd idt

[SECTION .bss]
idt:
    times 256 dq ?
idt_end:
