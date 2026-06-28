#pragma once

#include <stdint.h>
#include <stdbool.h>

// ========================================================
// PCI Device Command Register Masks

#define PCI_CMD_IO_SPACE_MASK       (1 << 0)
#define PCI_CMD_MEMORY_SPACE_MASK   (1 << 1)
#define PCI_CMD_BUSMASTER_MASK      (1 << 2)
#define PCI_CMD_SPECIAL_CYCLES_MASK (1 << 3)
#define PCI_CMD_MEM_WR_INV_MASK     (1 << 4)
#define PCI_CMD_VGA_SNOOP_MASK      (1 << 5)
#define PCI_CMD_PARITY_ERR_RSP_MASK (1 << 6)
#define PCI_CMD_WAIT_CYCLE_MASK     (1 << 7)
#define PCI_CMD_SERR_MASK           (1 << 8)
#define PCI_CMD_FBB_MASK            (1 << 9)

// ========================================================
// PCI Device Status Register Masks

#define PCI_STAT_66MHZ_MASK            (1 << 5)
#define PCI_STAT_UDF_MASK              (1 << 6)
#define PCI_STAT_FBB_MASK              (1 << 7)
#define PCI_STAT_DATA_PARITY_ERR_MASK  (1 << 8)
#define PCI_STAT_DEVSEL_TIMING_MASK    (3 << 9)
#define PCI_STAT_SIG_TARGET_ABRT_MASK  (1 << 11)
#define PCI_STAT_RECV_TARGET_ABRT_MASK (1 << 12)
#define PCI_STAT_RECV_MASTER_ABRT_MASK (1 << 13)
#define PCI_STAT_SIG_SYS_ERR_MASK      (1 << 14)
#define PCI_STAT_PARITY_ERR_MASK       (1 << 15)

// ========================================================
// PCI header BAR sizes

#define PCI_HEADER_TYPE00_BAR_ADDRS 6

// ========================================================
// PCI Header Header Type Masks

#define PCI_HEADER_TYPE_MULTIFUNC_MASK 0x80
#define PCI_HEADER_TYPE_TYPE_MASK      0x7f

// ========================================================
// PCI Header Offsets

// Common

#define PCI_HOFF_VENDOR_ID       0x0
#define PCI_HOFF_DEVICE_ID       0x2
#define PCI_HOFF_COMMAND         0x4
#define PCI_HOFF_STATUS          0x6
#define PCI_HOFF_REV_ID          0x8
#define PCI_HOFF_PROG_IF         0x9
#define PCI_HOFF_SUBCLASS        0xa
#define PCI_HOFF_CLASS_CODE      0xb
#define PCI_HOFF_CACHE_LINE_SIZE 0xc
#define PCI_HOFF_LATENCY_TIMER   0xd
#define PCI_HOFF_HEADER_TYPE     0xe
#define PCI_HOFF_BIST            0xf

// TYPE 00

#define PCI_HOFF_T00_BAR            0x10
#define PCI_HOFF_T00_CARDBUS_CIS    0x28
#define PCI_HOFF_T00_SUBSYSTEM_VID  0x2c
#define PCI_HOFF_T00_SUBSYSTEM_ID   0x2e
#define PCI_HOFF_T00_ROM_BASE_ADDR  0x30
#define PCI_HOFF_T00_INTERRUPT_LINE 0x3c
#define PCI_HOFF_T00_INTERRUPT_PIN  0x3d
#define PCI_HOFF_T00_MIN_GRANT      0x3e
#define PCI_HOFF_T00_MAX_LATENCY    0x3f

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

struct pci_dev {
	uint8_t bus;
	uint8_t device;
	uint8_t func;
	struct pci_header header;
};

uint32_t pci_read_32(const struct pci_dev *dev, const uint8_t offset);
uint16_t pci_read_16(const struct pci_dev *dev, const uint8_t offset);
uint8_t pci_read_8(const struct pci_dev *dev, const uint8_t offset);

void pci_write_32(const struct pci_dev *dev, const uint8_t offset,
                  const uint32_t data);
void pci_write_16(const struct pci_dev *dev, const uint8_t offset,
                  const uint16_t data);
void pci_write_8(const struct pci_dev *dev, const uint8_t offset,
                 const uint8_t data);

typedef void (*pci_device_cb_t)(struct pci_dev *dev, void *userdata);

void pci_enumerate_devices();

void pci_destroy_device(struct pci_dev *dev);

struct pci_dev_driver {
	bool (*init)(struct pci_dev *dev);
};

void pci_register_driver(const struct pci_dev_driver* drv);

void pci_init();
