#include "pci21.h"
#include "print.h"

static void on_pci_dev_found(const uint8_t bus, const uint8_t device,
                      const uint8_t func, const struct pci_header *dev_header,
                      void *userdata) {
	(void)userdata; // unused

	char buf[8];
	print_string(itoa(bus, buf, 10));
	print_string(":");
	print_string(itoa(device, buf, 10));
	print_string(".");
	print_string(itoa(func, buf, 10));

	print_string(" ");
	print_string(itoa(dev_header->vendor_id, buf, 16));
	print_string(":");
	print_string(itoa(dev_header->device_id, buf, 16));

	print_string(" rev ");
	print_string(itoa(dev_header->rev_id, buf, 16));

	print_string(" [");
	print_string(itoa(dev_header->class_code, buf, 16));
	print_string(":");
	print_string(itoa(dev_header->subclass, buf, 16));
	print_string("]\n");
}

void stage2_main(void) {
	init_output();

	print_string("Hello from C!\n");

	pci_enumerate_devices(on_pci_dev_found, 0);

	while (1)
		;
}
