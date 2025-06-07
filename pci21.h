#pragma once

#include <stdint.h>

#define PCI_HEADER_TYPE00_BAR_ADDRS 6

#define PCI_HEADER_TYPE_MULTIFUNC_MASK 0x80
#define PCI_HEADER_TYPE_TYPE_MASK      0x7f

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
	union {
		struct {
			uint32_t bar[PCI_HEADER_TYPE00_BAR_ADDRS];
			uint32_t cardbus_cis;
			uint16_t subsystem_vid;
			uint16_t subsystem_id;
			uint16_t rom_base_addr;
			uint32_t reserved[2];
			uint8_t interrupt_line;
			uint8_t interrupt_pin;
			uint8_t min_grant;
			uint8_t max_latency;
		} type00;
		// TODO: type01: PCI-to-PCI bridge
	} u;
};

typedef void (*pci_device_cb_t)(const uint8_t bus, const uint8_t device,
                                const uint8_t func,
                                struct pci_header *dev_header, void *userdata);

void pci_enumerate_devices(pci_device_cb_t cb, void *userdata);
