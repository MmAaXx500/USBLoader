#include <stdint.h>

#include "arch/pit.h"
#include "drivers/display/print.h"
#include "drivers/pci/pci21.h"
#include "drivers/serial/serial.h"
#include "drivers/usb/uhci.h"
#include "mem/mem.h"
#include "utils/gdbstub.h"

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
