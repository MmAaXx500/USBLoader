#include <stdint.h>

#include "gdbstub.h"
#include "pci21.h"
#include "print.h"
#include "mem.h"
#include "serial.h"

void stage2_main(void) {
	init_output();

	if(!serial_init_port(COM1, 115200))
		print_string("COM1 fail");

	print_string("Hello from C!\n");

	set_debug_traps();
	breakpoint();

	init_memory();

	pci_init();

	while (1)
		;
}
