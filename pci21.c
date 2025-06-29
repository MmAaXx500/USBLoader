#include "pci21.h"
#include "io.h"
#include "mem.h"

// PCI Configuration Space Access Mechanism #1 IO locations
#define PCI_CONFIG_ADDRESS 0xcf8
#define PCI_CONFIG_DATA    0xcfc

#define MAX_REG_DRIVERS 3

static struct pci_dev_driver drivers[MAX_REG_DRIVERS];
static uint8_t registered_drivers = 0;

static uint32_t get_pci_dev_addr(const struct pci_dev *dev, const uint8_t reg) {
	uint32_t addr = 0;

	addr |= (uint32_t)1 << 31;                    // bit 31 enable bit
	                                              // bits 24-30 reserved (0)
	addr |= (uint32_t)dev->bus << 16;             // bits 16-23
	addr |= (uint32_t)(dev->device & 0x1f) << 11; // bits 11-15
	addr |= (uint32_t)(dev->func & 0x07) << 8;    // bits 8-10
	addr |= (uint32_t)reg & 0xfc;                 // bits 0-7

	return addr;
}

uint32_t pci_read_32(const struct pci_dev *dev, const uint8_t offset) {
	outl(PCI_CONFIG_ADDRESS, get_pci_dev_addr(dev, offset & 0xfc));
	return inl(PCI_CONFIG_DATA); // no offset needed, reading 32 bits
}

uint16_t pci_read_16(const struct pci_dev *dev, const uint8_t offset) {
	outl(PCI_CONFIG_ADDRESS, get_pci_dev_addr(dev, offset & 0xfc));
	return inw(PCI_CONFIG_DATA + (offset & 0x3));
}

uint8_t pci_read_8(const struct pci_dev *dev, const uint8_t offset) {
	outl(PCI_CONFIG_ADDRESS, get_pci_dev_addr(dev, offset & 0xfc));
	return inb(PCI_CONFIG_DATA + (offset & 0x3));
}

void pci_write_32(const struct pci_dev *dev, const uint8_t offset,
                  const uint32_t data) {
	outl(PCI_CONFIG_ADDRESS, get_pci_dev_addr(dev, offset & 0xfc));
	outl(PCI_CONFIG_DATA, data);
}

void pci_write_16(const struct pci_dev *dev, const uint8_t offset,
                  const uint16_t data) {
	outl(PCI_CONFIG_ADDRESS, get_pci_dev_addr(dev, offset & 0xfc));
	outw(PCI_CONFIG_DATA + (offset & 0x3), data);
}

void pci_write_8(const struct pci_dev *dev, const uint8_t offset,
                 const uint8_t data) {
	outl(PCI_CONFIG_ADDRESS, get_pci_dev_addr(dev, offset & 0xfc));
	outb(PCI_CONFIG_DATA + (offset & 0x3), data);
}

static void pci_read_device(const uint8_t bus, const uint8_t device) {
	uint8_t max_funcs = 1;
	struct pci_dev tmp_dev = {
		.bus = bus,
		.device = device,
		.func = 0
	};

	const uint16_t vid = pci_read_16(&tmp_dev, 0);
	if (vid == 0xffff)
		return;

	const uint16_t header_type = pci_read_8(&tmp_dev, 0xe);
	if ((header_type & PCI_HEADER_TYPE_MULTIFUNC_MASK) != 0)
		max_funcs = 8;

	for (uint8_t func = 0; func < max_funcs; func++) {
		struct pci_dev *dev = memalloc(sizeof(struct pci_dev));

		dev->bus = bus;
		dev->device = device;
		dev->func = func;

		dev->header.vendor_id = pci_read_16(dev, 0x0);
		dev->header.device_id = pci_read_16(dev, 0x2);
		dev->header.command = pci_read_16(dev, 0x4);
		dev->header.status = pci_read_16(dev, 0x6);
		dev->header.rev_id = pci_read_8(dev, 0x8);
		dev->header.prog_if = pci_read_8(dev, 0x9);
		dev->header.subclass = pci_read_8(dev, 0xa);
		dev->header.class_code = pci_read_8(dev, 0xb);
		dev->header.cache_line_size = pci_read_8(dev, 0xc);
		dev->header.latency_timer = pci_read_8(dev, 0xd);
		dev->header.header_type = pci_read_8(dev, 0xe);
		dev->header.bist = pci_read_8(dev, 0xf);

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
				    pci_read_32(dev, 0x10 + (uint8_t)(bar_off * 4));

			dev->header.u.type00.cardbus_cis = pci_read_32(dev, 0x28);
			dev->header.u.type00.subsystem_vid = pci_read_16(dev, 0x2c);
			dev->header.u.type00.subsystem_id = pci_read_16(dev, 0x2e);
			dev->header.u.type00.rom_base_addr = pci_read_16(dev, 0x30);
			dev->header.u.type00.interrupt_line = pci_read_8(dev, 0x3c);
			dev->header.u.type00.interrupt_pin = pci_read_8(dev, 0x3d);
			dev->header.u.type00.min_grant = pci_read_8(dev, 0x3e);
			dev->header.u.type00.max_latency = pci_read_8(dev, 0x3f);
		}

		bool init_done = false;
		for (uint8_t i = 0; i < registered_drivers; i++) {
			if (drivers[i].init(dev)) {
				init_done = true;
				break;
			}
		}

		if (!init_done)
			pci_destroy_device(dev);
	}
}

void pci_enumerate_devices() {
	for (uint16_t bus = 0; bus < 256; bus++) {
		for (uint8_t device = 0; device < 32; device++) {
			pci_read_device((uint8_t)bus, device);
		}
	}
}

void pci_destroy_device(struct pci_dev *dev) { memfree(dev); }

void pci_register_driver(const struct pci_dev_driver *drv) {
	if (registered_drivers >= MAX_REG_DRIVERS)
		return;

	drivers[registered_drivers++] = *drv;
}

void pci_init() { pci_enumerate_devices(); }
