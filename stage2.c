#include "pci21.h"
#include "print.h"

static void on_pci_dev_found(const uint8_t bus, const uint8_t device,
                             const uint8_t func, struct pci_header *dev_header,
                             void *userdata) {
	(void)userdata; // unused

	char buf[8];
	print_string(itoa(dev->bus, buf, 10));
	print_string(":");
	print_string(itoa(dev->device, buf, 10));
	print_string(".");
	print_string(itoa(dev->func, buf, 10));

	print_string(" ");
	print_string(itoa(dev->header.vendor_id, buf, 16));
	print_string(":");
	print_string(itoa(dev->header.device_id, buf, 16));

	print_string(" rev ");
	print_string(itoa(dev->header.rev_id, buf, 16));

	print_string(" [");
	print_string(itoa(dev->header.class_code, buf, 16));
	print_string(":");
	print_string(itoa(dev->header.subclass, buf, 16));
	print_string("]");

	print_string(" if ");
	print_string(itoa(dev->header.prog_if, buf, 16));
	print_string("\n");
}

void stage2_main(void) {
	init_output();

	print_string("Hello from C!\n");

	pci_enumerate_devices(on_pci_dev_found, 0);

	while (1)
		;
}
