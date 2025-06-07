#include "pci21.h"
#include "io.h"

// PCI Configuration Space Access Mechanism #1 IO locations
#define PCI_CONFIG_ADDRESS 0xcf8
#define PCI_CONFIG_DATA    0xcfc

static uint32_t pci_read_dword(const struct pci_dev *dev,
                               const uint8_t offset) {
	uint8_t reg_num = offset >> 2;
	uint32_t addr = 0;

	addr |= (uint32_t)1 << 31;                    // bit 31 enable bit
	                                              // bits 24-30 reserved (0)
	addr |= (uint32_t)dev->bus << 16;             // bits 16-23
	addr |= (uint32_t)(dev->device & 0x1f) << 11; // bits 11-15
	addr |= (uint32_t)(dev->func & 0x07) << 8;    // bits 8-10
	addr |= (uint32_t)reg_num << 2;               // bits 2-7
	// bits 0-1 is 0 for 32bit aligned access

	outl(PCI_CONFIG_ADDRESS, addr);

	return inl(PCI_CONFIG_DATA);
}

static uint16_t pci_read_word(const struct pci_dev *dev, const uint8_t offset) {
	uint32_t tmp = pci_read_dword(dev, offset);
	return (tmp >> ((offset & 0x02) * 8)) & 0xffff;
}

static uint8_t pci_read_byte(const struct pci_dev *dev, const uint8_t offset) {
	uint32_t tmp = pci_read_dword(dev, offset);
	return (tmp >> ((offset & 0x03) * 8)) & 0xff;
}

static void pci_read_device(const uint8_t bus, const uint8_t device,
                            struct pci_dev *dev, pci_device_cb_t cb,
                            void *userdata) {
	uint8_t max_funcs = 1;

	dev->bus = bus;
	dev->device = device;
	dev->func = 0;

	dev->header.vendor_id = pci_read_word(dev, 0);
	if (dev->header.vendor_id == 0xffff)
		return;

	dev->header.header_type = pci_read_byte(dev, 0xe);
	if ((dev->header.header_type & PCI_HEADER_TYPE_MULTIFUNC_MASK) != 0)
		max_funcs = 8;

	for (uint8_t func = 0; func < max_funcs; func++) {
		dev->func = func;

		dev->header.device_id = pci_read_word(dev, 0x2);
		dev->header.command = pci_read_word(dev, 0x4);
		dev->header.status = pci_read_word(dev, 0x6);
		dev->header.rev_id = pci_read_byte(dev, 0x8);
		dev->header.prog_if = pci_read_byte(dev, 0x9);
		dev->header.subclass = pci_read_byte(dev, 0xa);
		dev->header.class_code = pci_read_byte(dev, 0xb);
		dev->header.cache_line_size = pci_read_byte(dev, 0xc);
		dev->header.latency_timer = pci_read_byte(dev, 0xd);
		dev->header.bist = pci_read_byte(dev, 0xf);

		// filter non-existing functions
		// PCI Specification 6.2.1. Device Identification
		// In "Header Type" and "Class Code": All other/unspecified encodings
		// are reserved.
		if (dev->header.header_type == 0xff || dev->header.class_code == 0xff)
			continue;

		if ((dev->header.header_type & PCI_HEADER_TYPE_TYPE_MASK) == 0) {
			for (uint8_t bar_off = 0; bar_off < PCI_HEADER_TYPE00_BAR_ADDRS;
			     bar_off++)
				dev->header.u.type00.bar[bar_off] =
				    pci_read_dword(dev, 0x10 + bar_off);

			dev->header.u.type00.cardbus_cis = pci_read_dword(dev, 0x28);
			dev->header.u.type00.subsystem_vid = pci_read_word(dev, 0x2c);
			dev->header.u.type00.subsystem_id = pci_read_word(dev, 0x2e);
			dev->header.u.type00.rom_base_addr = pci_read_word(dev, 0x30);
			dev->header.u.type00.interrupt_line = pci_read_byte(dev, 0x3c);
			dev->header.u.type00.interrupt_pin = pci_read_byte(dev, 0x3d);
			dev->header.u.type00.min_grant = pci_read_byte(dev, 0x3e);
			dev->header.u.type00.max_latency = pci_read_byte(dev, 0x3f);
		}

		cb(dev, userdata);
	}
}

void pci_enumerate_devices(pci_device_cb_t cb, void *userdata) {
	struct pci_dev dev = {0};

	for (uint16_t bus = 0; bus < 256; bus++) {
		for (uint8_t device = 0; device < 32; device++) {
			pci_read_device((uint8_t)bus, device, &dev, cb, userdata);
		}
	}
}
