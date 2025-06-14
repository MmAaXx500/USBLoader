#include "pci21.h"
#include "pit.h"
#include "print.h"
#include "uhci.h"

static void on_pci_dev_found(struct pci_dev *dev, void *userdata) {
	(void)userdata; // unused

	print_pci_dev(dev);
	print_string("\n");
}

void stage2_main(void) {
	init_output();

	print_string("Hello from C!\n");

	pci_enumerate_devices(on_pci_dev_found, 0);

	while (1)
		;
}
