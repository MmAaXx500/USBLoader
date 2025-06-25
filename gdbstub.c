#include "idt.h"
#include "serial.h"

static const serial_port gdb_port = COM1;

void putDebugChar(char ch) { serial_write(gdb_port, (uint8_t)ch); }

int getDebugChar(void) {
	while (!serial_data_ready(gdb_port))
		;

	return serial_read(gdb_port);
}

void exceptionHandler(int exception_number, void *exception_address) {
	struct idtr idtr;
	uint16_t gate_type = IDT_GATE_32TRAP;

	idt_get(&idtr);

	if (exception_number == 2 || exception_number >= 32)
		gate_type = IDT_GATE_32INT;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
	struct idt_gate *gate = (struct idt_gate *)idtr.offset;
#pragma GCC diagnostic pop

	gate[exception_number].offset_high =
	    (uint16_t)((uintptr_t)exception_address >> 16);
	gate[exception_number].offset_low = (uintptr_t)exception_address & 0xffff;
	gate[exception_number].flags =
	    IDT_GATE_PRESENT | IDT_GATE_DPL_RING0 | gate_type;
	// TODO: Segement selector not set, may contain invalid value

	idt_set(&idtr);
}
