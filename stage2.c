#include <stdint.h>

#include "gdbstub.h"
#include "mem.h"
#include "pci21.h"
#include "pit.h"
#include "print.h"
#include "serial.h"
#include "uhci.h"

void stage2_main(void) {
	init_output();
	pit_init();

	if (!serial_init_port(COM1, 115200))
		print_string("COM1 fail");

	print_string("Hello from C!\n");

	// set_debug_traps();
	// breakpoint();

	init_memory();

	uhci_init();
	pci_init();

	while (1)
		;
}
