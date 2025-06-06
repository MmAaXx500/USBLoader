#pragma once

#include <stdint.h>

struct pci_header {
	uint16_t vendor_id;
	uint16_t device_id;
	uint16_t command;
	uint16_t status;
	uint8_t rev_id;
	uint8_t prog_if;
	uint8_t subclass;
	uint8_t class_code;
	uint8_t cache_line_size;
	uint8_t latency_timer;
	uint8_t header_type;
	uint8_t bist;
};

typedef void (*pci_device_cb_t)(const uint8_t bus, const uint8_t device,
                                const uint8_t func,
                                const struct pci_header *dev_header,
                                void *userdata);

void pci_enumerate_devices(pci_device_cb_t cb, void *userdata);
