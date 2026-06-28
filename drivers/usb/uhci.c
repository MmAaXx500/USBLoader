#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <uchar.h>
#include <wchar.h>

#include "arch/idt.h"
#include "arch/pit.h"
#include "drivers/display/print.h"
#include "drivers/io/io.h"
#include "drivers/pci/pci21.h"
#include "drivers/usb/uhci.h"
#include "mem/mem.h"
#include "utils/utils.h"

// LEGACY SUPPORT REGISTER 16bit
#define PCI_HOFF_UHCI_LEGSUP 0xc0

// IO Space Base Address index in the PCI header BAR field
#define USBBASE_BAR_IDX 4

#define UHCI_FRAME_LIST_SIZE    1024
#define UHCI_FRBASEADD_PTR(ptr) (((uint32_t)(ptr)) & 0xfffff000)

/*
 * Frame List Pointer / Frame List Entry / Queue Head Link Pointer
 *
 * Must be aligned to 4KB
 *
 *    3                   2                   1                   0
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                Frame List Pointer                     |0|0|Q|T|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Frame List Pointer (FLP): Pointer to the next Queue Head or Transfer
 *                           Descriptor
 * Q: 1 = FLP points to a QH; 0 = FLP points to a TD
 * T: 1 = Last QH / Empty frame / FLP is invalid
 */

#define UHCI_FLP_PTR(ptr) (((uint32_t)(ptr)) & 0xfffffff0)
#define UHCI_FLP_QH       (1 << 1)
#define UHCI_FLP_TERM     1

// Frame list must be 4KB aligned
#define UHCI_FRAME_LIST_ALIGN 4096

struct frame_list_pointer {
	uint32_t pointer;
};

/*
 * Queue Head
 *
 * Must be aligned to 16 bytes
 *
 *    3                   2                   1                   0
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |       Queue Head / Horizontal Link Pointer            |0|0|Q|T|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |       Queue Element / Vertical Link Pointer           |0|0|Q|T|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Structure is same as the Frame List Pointer
 * Queue Head / Horizontal Link Pointer (QHLP): Pointer to the next QH
 * Queue Element / Vertical Link Pointer (QELP): Pointer to the first queue
 *                                               element
 *
 * QELP is Host Controller RW
 */

struct queue_head {
	struct frame_list_pointer qhlp;
	volatile struct frame_list_pointer qelp;
};

/*
 * Transfer descriptor
 *
 * Must be aligned to 16 bytes
 *
 *    3                   2                   1                   0
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                    Link Pointer                       |0|V|Q|T|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |   |D|Err|L|I|C|     Status    |         |       ActLen        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |    Maximum Length   | |D| EndPt | Dev Address |      PID      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                         Buffer Pointer                        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * |                 128 bit usable by software                    |
 * |                                                               |
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * TD Link Pointer:
 *   Link Pointer (LP): Points to a QH or TD
 *   Depth / Breadth Select (Vf, V): 1 = Depth First; 0 = Breadth First
 *   Q: 1 = FLP points to a QH; 0 = FLP points to a TD
 *   T: 1 = Last QH / Empty frame / FLP is invalid
 *
 * TD Control and Status (RW by HC):
 *   Short Packet Detect (SPD, D): 1 = Enable; 0 = Disable
 *   Error Counter (C_ERR; ERR): Write: Max allowed errors. If 0 reached TD
 *                               marked as inactive. Writing 0 disables this.
 *   Low Speed Device (LS, L): 1 = Low Speed Device; 0 = Full Speed Device
 *   Isochronous Select (IOS, I): 1 = Isochronous TD; 0 = Non-isochronous TD
 *   Interrupt on Complete (IOC, C): 1 = issue IOC
 *   Status: By software: Active
 *           By Host Controler: Stalled, Data Buffer Error, Babble Detected,
 *                              NAK Received, CRC/Time out, Bitstuff Error
 *   Actual Length (ActLen): Written by HC, actual number of bytes.
 *                           Encoded as N-1
 *
 * TD Token:
 *   Maximum Length (Max Len): Max number of bytes for the transfer
 *                             0x0 = 1 ... 0x3FE = 1023 ... 0x4FF = 1280;
 *                             0x7FF = Null Data Packet
 *   Data Toggle (D): 0 = DATA0; 1 = DATA1 USB Specification 8.6
 *   Endpoint (EndPt): Device internal endpoints
 *   Device Address: Address of the device
 *   Packet Identification (PID): IN (0x69), OUT (0xE1), SETUP (0x2D)
 *
 * TD Bufer Pointer
 *   Buffer Pointer: Byte aligned buffer address.
 *                   Must be at least as long as Maximum Length
 */

#define UHCI_TD_LPTR_PTR(ptr) ((uint32_t)ptr & 0xfffffff0)
#define UHCI_TD_LPTR_DEPTH    (1 << 2)
#define UHCI_TD_LPTR_QH       (1 << 1)
#define UHCI_TD_LPTR_TERM     1

#define UHCI_TD_SPD                 (1 << 29)
#define UHCI_TD_ERR_CNT(val)        ((val & 0x3) << 27)
#define UHCI_TD_LOW_SPEED           (1 << 26)
#define UHCI_TD_IOS                 (1 << 25)
#define UHCI_TD_IOC                 (1 << 24)
#define UHCI_TD_STATUS_ACTIVE       (1 << 23)
#define UHCI_TD_STATUS_STALLED      (1 << 22)
#define UHCI_TD_STATUS_DATA_BUF_ERR (1 << 21)
#define UHCI_TD_STATUS_BABBLE       (1 << 20)
#define UHCI_TD_STATUS_NAK          (1 << 19)
#define UHCI_TD_STATUS_CRC_TO       (1 << 18)
#define UHCI_TD_STATUS_BITSTUFF_ERR (1 << 17)
#define UHCI_TD_STATUS_MASK         (0x7f << 17)
#define UHCI_TD_ACT_LEN_MASK        0x3ff

#define UHCI_TD_MAX_LEN(len)   ((uint32_t)(((len) & 0x3f) << 21))
#define UHCI_TD_DATA_TOGGLE    (1 << 19)
#define UHCI_TD_ENDPOINT_MASK  (0x0f << 15)
#define UHCI_TD_DEV_ADDR(addr) ((uint32_t)(((addr) & 0x7f) << 8))
#define UHCI_TD_DEV_ADDR_MASK  (0x7f << 8)
#define UHCI_TD_PID_MASK       0x7f

#define UHCI_TD_PID_IN    0x69
#define UHCI_TD_PID_OUT   0xe1
#define UHCI_TD_PID_SETUP 0x2d

#define LINK_PTR_TO_TD(lptr)                                                   \
	((struct transfer_descriptor *)UHCI_TD_LPTR_PTR(lptr))

struct __attribute__((__packed__)) transfer_descriptor {
	uint32_t link_ptr;
	volatile uint32_t ctrl_status;
	uint32_t token;
	uint32_t buffer_ptr;
	uint32_t swdata[4];
};

/*
 * USB Device Requests
 *
 *    3                   2                   1                   0
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |           Value               |   Request     | Request Type  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |           Length              |             Index             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Request Type:
 *   bit 7 Data xfer direction:
 *     0 = Host to device
 *     1 = Device to host
 *   bit 6-5 Type:
 *     0 = Standard
 *     1 = Class
 *     2 = Vendor
 *     3 = Reserved
 *   bit 4-0 Recipient:
 *     0 = Device
 *     1 = Interface
 *     2 = Endpoint
 *     3 = Other
 *     4-31 = Reserved
 *
 * Request, Value, Index:
 *   Universal Serial Bus Specification Revision 1.0
 *   Table 9-2
 *
 * Length:
 *   Number of bytes to transfer if there is a data phase
 */

#define UHCI_DR_RT_XDIR_HOST_DEV 0
#define UHCI_DR_RT_XDIR_DEV_HOST (1 << 7)

#define UHCI_DR_RT_TYPE_STANDARD (0 << 5)
#define UHCI_DR_RT_TYPE_CLASS    (1 << 5)
#define UHCI_DR_RT_TYPE_VENDOR   (2 << 5)

#define UHCI_DR_RT_RECIPIENT_DEVICE    0
#define UHCI_DR_RT_RECIPIENT_INTERFACE 1
#define UHCI_DR_RT_RECIPIENT_ENDPOINT  2
#define UHCI_DR_RT_RECIPIENT_OTHER     3

/*
 * Universal Serial Bus Specification Revision 1.0
 * Table 9-3. Standard Request Codes
 */
#define UHCI_DR_REQ_GET_STATUS        0
#define UHCI_DR_REQ_CLEAR_FEATURE     1
// 2 = reserved
#define UHCI_DR_REQ_SET_FEATURE       3
// 4 = reserved
#define UHCI_DR_REQ_SET_ADDRESS       5
#define UHCI_DR_REQ_GET_DESCRIPTOR    6
#define UHCI_DR_REQ_SET_DESCRIPTOR    7
#define UHCI_DR_REQ_GET_CONFIGURATION 8
#define UHCI_DR_REQ_SET_CONFIGURATION 9
#define UHCI_DR_REQ_GET_INTERFACE     10
#define UHCI_DR_REQ_SET_INTERFACE     11
#define UHCI_DR_REQ_SYNCH_FRAME       12

/*
 * Universal Serial Bus Specification Revision 1.0
 * Table 9-4. Descriptor Types
 */
#define UHCI_DR_VAL_DESC_DEVICE        (1 << 8)
#define UHCI_DR_VAL_DESC_CONFIGURATION (2 << 8)
#define UHCI_DR_VAL_DESC_STRING        (3 << 8)
#define UHCI_DR_VAL_DESC_INTERFACE     (4 << 8)
#define UHCI_DR_VAL_DESC_ENDPOINT      (5 << 8)

struct device_request {
	uint8_t request_type;
	uint8_t request;
	uint16_t value;
	uint16_t index;
	uint16_t length;
};

struct descriptor {
	uint8_t length;
	uint8_t desc_type;
};

struct device_descriptor {
	uint8_t length;
	uint8_t desc_type;
	uint16_t bcdUSB;
	uint8_t device_class;
	uint8_t device_sub_class;
	uint8_t device_protocol;
	uint8_t max_packet_size;
	uint16_t vendor_id;
	uint16_t product_id;
	uint16_t bcdDevice;
	uint8_t manufacturer_idx;
	uint8_t product_idx;
	uint8_t serial_number_idx;
	uint8_t configurations_num;
};

struct __attribute__((__packed__)) configuration_descriptor {
	uint8_t length;
	uint8_t desc_type;
	uint16_t total_length;
	uint8_t interfaces_num;
	uint8_t configuration_value;
	uint8_t configuration_idx;
	uint8_t attribute;
	uint8_t max_power; // unit: 2mA
};

struct string_descriptor {
	uint8_t length;
	uint8_t desc_type;
	char16_t string[];
};

// ========================================================
// UHCI registers

typedef uint16_t uhci_reg;

#define UHCI_USBCMD    ((uhci_reg)0x00)
#define UHCI_USBSTS    ((uhci_reg)0x02)
#define UHCI_USBINTR   ((uhci_reg)0x04)
#define UHCI_FRNUM     ((uhci_reg)0x06)
#define UHCI_FRBASEADD ((uhci_reg)0x08)
#define UHCI_SOFMOD    ((uhci_reg)0x0c)
#define UHCI_PORTSC1   ((uhci_reg)0x10)
#define UHCI_PORTSC2   ((uhci_reg)0x12)

// ========================================================
// UHCI Command register

#define UHCI_USBCMD_MAX_PKT_SIZE   (1 << 7)
#define UHCI_USBCMD_CONFIGURE      (1 << 6)
#define UHCI_USBCMD_SOFT_DBG       (1 << 5)
#define UHCI_USBCMD_GLOBAL_RESUME  (1 << 4)
#define UHCI_USBCMD_GLOBAL_SUSPEND (1 << 3)
#define UHCI_USBCMD_GLOBAL_RESET   (1 << 2)
#define UHCI_USBCMD_HC_RESET       (1 << 1)
#define UHCI_USBCMD_RUN            (1 << 0)

// ========================================================
// UHCI Interrupt Enable register

#define UHCI_USBINTR_SHORT_PKT_INT   (1 << 3)
#define UHCI_USBINTR_INT_ON_COMPLETE (1 << 2)
#define UHCI_USBINTR_RESUME_INT      (1 << 1)
#define UHCI_USBINTR_CRC_TIMEOUT_INT (1 << 0)

// ========================================================
// UHCI Port register

#define UHCI_PORTSC_SUSPEND            (1 << 12)               // R/W
#define UHCI_PORTSC_RESET              (1 << 9)                // R/W
#define UHCI_PORTSC_LOW_SPEED(reg)     ((reg & (1 << 8)) >> 8) // RO
#define UHCI_PORTSC_PORT_DETECT        (1 << 7)                // RO
#define UHCI_PORTSC_RESUME_DETECT      (1 << 6)                // R/W
#define UHCI_PORTSC_LINE_STATUS_MASK   (0x3 << 4)              // RO
#define UHCI_PORTSC_PORT_ENABLE_CHG    (1 << 3)                // R/WC
#define UHCI_PORTSC_PORT_ENABLE        (1 << 2)                // R/W
#define UHCI_PORTSC_CONNECT_STATUS_CHG (1 << 1)                // R/WC
#define UHCI_PORTSC_CONNECT_STATUS     (1 << 0)                // RO

struct uhci_dev {
	uint16_t iobase;
	struct frame_list_pointer *frame_list_base;
	uint8_t portnum;
	struct pci_dev *pci_dev;
	struct queue_head *qh1ms;
};

struct usb_device {
	bool low_speed;
	uint8_t addr;
	struct device_descriptor dev_desc;
	struct configuration_descriptor *conf_desc;
};

struct transfer_entry {
	struct transfer_descriptor *last;
	struct transfer_descriptor *first;
	void (*handler)(struct transfer_entry *te);
	void *userdata;
	volatile struct transfer_entry *next;
};

const uhci_reg ports[2] = {UHCI_PORTSC1, UHCI_PORTSC2};

static volatile struct transfer_entry *pending_queue = NULL;

uint32_t uhci_read_32(const struct uhci_dev *dev, const uhci_reg reg) {
	return inl(dev->iobase + (uint16_t)reg);
}
uint16_t uhci_read_16(const struct uhci_dev *dev, const uhci_reg reg) {
	return inw(dev->iobase + (uint16_t)reg);
}
uint8_t uhci_read_8(const struct uhci_dev *dev, const uhci_reg reg) {
	return inb(dev->iobase + (uint16_t)reg);
}

void uhci_write_32(const struct uhci_dev *dev, const uhci_reg reg,
                   const uint32_t data) {
	outl(dev->iobase + (uint16_t)reg, data);
}
void uhci_write_16(const struct uhci_dev *dev, const uhci_reg reg,
                   const uint16_t data) {
	outw(dev->iobase + (uint16_t)reg, data);
}
void uhci_write_8(const struct uhci_dev *dev, const uhci_reg reg,
                  const uint8_t data) {
	outb(dev->iobase + (uint16_t)reg, data);
}

bool uhci_isr(uint8_t int_n, void *userdata) {
	(void)int_n;
	struct uhci_dev *uhci_dev = (struct uhci_dev *)userdata;
	volatile struct transfer_entry *prev_entry = NULL;
	volatile struct transfer_entry *entry = pending_queue;
	if (uhci_read_16(uhci_dev, UHCI_USBSTS) == 0) {
		// Not our INT
		return false;
	}

	while (entry != NULL) {
		if (entry->last != NULL
		    && (entry->last->ctrl_status & UHCI_TD_STATUS_ACTIVE) == 0) {
			if (prev_entry != NULL) {
				prev_entry->next = entry->next;
			} else {
				pending_queue = NULL;
			}
			volatile struct transfer_entry *next_entry = entry->next;
			entry->handler((struct transfer_entry *)entry);
			entry = next_entry;
		} else {
			prev_entry = entry;
			entry = entry->next;
		}
	}

	uhci_write_16(uhci_dev, UHCI_USBSTS, 0x1f);
	return true;
}

static bool is_UHCI_device(const struct pci_header *dev_header) {
	return dev_header->class_code == 0x0c && dev_header->subclass == 0x03
	       && dev_header->prog_if == 0;
}

static uint8_t uhci_reset(struct uhci_dev *dev) {
	uhci_write_16(dev, UHCI_USBCMD, UHCI_USBCMD_GLOBAL_RESET);
	sleep(20); // UHCI spec 2.1.1 "This bit is reset by the software after a
	           // minimum of 10 ms has elapsed"
	uhci_write_16(dev, UHCI_USBCMD, 0);

	uhci_write_16(dev, UHCI_USBCMD, UHCI_USBCMD_HC_RESET);

	// wait for host controller bit to reset
	uint8_t timeout = 100; // 100 * 10 ms (1s)
	while (timeout > 0
	       && (uhci_read_16(dev, UHCI_USBCMD) & UHCI_USBCMD_HC_RESET) != 0) {
		sleep(10);
		timeout--;
	}

	return (timeout > 0) ? 0 : 1;
}

static uint8_t uhci_find_ports(const struct uhci_dev *dev) {
	uint8_t port_num = 0;

	for (uint8_t i = 0; i < ARRSIZE(ports); i++) {
		if ((uhci_read_16(dev, ports[i]) & UHCI_PORTSC_PORT_DETECT) != 0)
			port_num++;
	}

	return port_num;
}

static void uhci_init_frame_list(struct uhci_dev *dev) {
	struct frame_list_pointer *flist = memalloc_aligned(
	    sizeof(struct frame_list_pointer) * UHCI_FRAME_LIST_SIZE,
	    UHCI_FRAME_LIST_ALIGN);

	// more queues can be chanined for other timings
	struct queue_head *qh1ms = memalloc_aligned(sizeof(struct queue_head), 16);
	qh1ms->qhlp.pointer = UHCI_FLP_TERM;
	qh1ms->qelp.pointer = UHCI_FLP_TERM;
	dev->qh1ms = qh1ms;

	for (uint16_t i = 0; i < UHCI_FRAME_LIST_SIZE; ++i) {
		flist[i].pointer = UHCI_FLP_PTR(qh1ms) | UHCI_FLP_QH;
	}

	dev->frame_list_base->pointer = UHCI_FRBASEADD_PTR(flist);

	// set frame list base address
	uhci_write_32(dev, UHCI_FRBASEADD, UHCI_FRBASEADD_PTR(flist));
}

static bool uhci_enable_device_on_port(const struct uhci_dev *dev,
                                       const uhci_reg port) {
	uint16_t timeout = 10;
	uint16_t regval = uhci_read_16(dev, port);
	// do not clear the Status Change bit yet
	regval &= (uint16_t)~(UHCI_PORTSC_CONNECT_STATUS_CHG);

	uhci_write_16(dev, port, regval | UHCI_PORTSC_RESET);
	sleep(100);

	uhci_write_16(dev, port, regval & (uint16_t)~(UHCI_PORTSC_RESET));
	sleep(50);

	uhci_write_16(dev, port, regval | UHCI_PORTSC_PORT_ENABLE);
	while (timeout != 0) {
		sleep(10);
		if ((uhci_read_16(dev, port) & UHCI_PORTSC_PORT_ENABLE) != 0)
			break;

		--timeout;
	}

	if (timeout == 0) {
		print_string("UHCI device bringup timed out on port ");
		print_string(itoa_once(port, 16));
		print_string("\n");
		print_pci_dev(dev->pci_dev);
		return false;
	}

	// clear Status Change bit
	regval = uhci_read_16(dev, port);
	uhci_write_16(dev, port, regval);
	return true;
}

static void uhci_schedule_queue(struct queue_head *queue,
                                struct transfer_entry *transfer_entry) {
	uint32_t endptr = queue->qelp.pointer;
	volatile struct transfer_entry *te = pending_queue;

	if (te) {
		while (te->next != NULL) {
			te = te->next;
		}
		te->next = transfer_entry;
	} else {
		pending_queue = transfer_entry;
	}

	if (endptr & UHCI_TD_LPTR_TERM) {
		queue->qelp.pointer = UHCI_FLP_PTR(transfer_entry->first);
		return;
	}

	while (!(LINK_PTR_TO_TD(endptr)->link_ptr & UHCI_TD_LPTR_TERM)) {
		endptr = LINK_PTR_TO_TD(endptr)->link_ptr;
	}

	LINK_PTR_TO_TD(endptr)->link_ptr = UHCI_FLP_PTR(transfer_entry->first);
}

static bool uhci_print_td_status(const struct transfer_descriptor *td) {
	bool is_ok = true;

	if ((td->ctrl_status & UHCI_TD_STATUS_ACTIVE) != 0)
		print_string("TD active\n");
	if ((td->ctrl_status & UHCI_TD_STATUS_STALLED) != 0) {
		is_ok = false;
		print_string("TD stalled\n");
	}
	if ((td->ctrl_status & UHCI_TD_STATUS_DATA_BUF_ERR) != 0) {
		is_ok = false;
		print_string("TD data buffer error\n");
	}
	if ((td->ctrl_status & UHCI_TD_STATUS_BABBLE) != 0) {
		is_ok = false;
		print_string("TD babble\n");
	}
	if ((td->ctrl_status & UHCI_TD_STATUS_NAK) != 0) {
		is_ok = false;
		print_string("TD nak\n");
	}
	if ((td->ctrl_status & UHCI_TD_STATUS_CRC_TO) != 0) {
		is_ok = false;
		print_string("TD crc error or timeout\n");
	}
	if ((td->ctrl_status & UHCI_TD_STATUS_BITSTUFF_ERR) != 0) {
		is_ok = false;
		print_string("TD bitstuff error\n");
	}

	if (!is_ok) {
		print_string("ERR CNT: ");
		print_string(itoa_once((td->ctrl_status & (0x3 << 27)) >> 27, 10));
		print_string("\n");
		print_string("SWDATA ID: ");
		print_string(itoa_once((int)td->swdata[0], 10));
		print_string("\n");
	}

	return is_ok;
}

static bool uhci_wait_entry_complete(struct transfer_entry *te) {
	while (*((bool *)te->userdata) == false) {
		__asm__("hlt");
	}

	return true;
}

static uint16_t uhci_create_td_control(struct transfer_descriptor **out_td,
                                       const struct usb_device *dev,
                                       uint8_t request_type, uint8_t request,
                                       uint16_t value, uint16_t index,
                                       uint16_t length, uint8_t data_stage,
                                       uint8_t status_stage, void *buf) {
	bool toggle = true;
	uint16_t td_cnt = 2;
	uint16_t max_pkt_size = dev->dev_desc.max_packet_size;
	struct device_request *dr = memalloc(sizeof(struct device_request));

	dr->request_type = request_type;
	dr->request = request;
	dr->value = value;
	dr->index = index;
	dr->length = length;

	if (max_pkt_size < 8)
		max_pkt_size = 8;

	td_cnt += (uint16_t)DIV_CEIL(length, max_pkt_size);

	*out_td = memalloc_aligned(sizeof(struct transfer_descriptor) * td_cnt, 16);

	(*out_td)[0].link_ptr =
	    UHCI_TD_LPTR_PTR(&(*out_td)[1]) | UHCI_TD_LPTR_DEPTH;
	(*out_td)[0].ctrl_status = UHCI_TD_ERR_CNT(3)
	                           | (dev->low_speed ? UHCI_TD_LOW_SPEED : 0)
	                           | UHCI_TD_STATUS_ACTIVE;
	// according to bochs the MAX_LEN should be max_pkt_size, but real hardware
	// stalls with that
	(*out_td)[0].token = UHCI_TD_MAX_LEN(sizeof(struct device_request) - 1)
	                     | UHCI_TD_DEV_ADDR(dev->addr) | UHCI_TD_PID_SETUP;
	(*out_td)[0].buffer_ptr = (uint32_t)dr;
	(*out_td)[0].swdata[0] = 1;

	for (uint16_t i = 0; i < td_cnt - 2; ++i) {
		(*out_td)[i + 1].link_ptr =
		    UHCI_TD_LPTR_PTR(&(*out_td)[i + 2]) | UHCI_TD_LPTR_DEPTH;
		(*out_td)[i + 1].ctrl_status =
		    UHCI_TD_ERR_CNT(3) | (dev->low_speed ? UHCI_TD_LOW_SPEED : 0)
		    | UHCI_TD_STATUS_ACTIVE;
		(*out_td)[i + 1].token = UHCI_TD_MAX_LEN(max_pkt_size - 1)
		                         | UHCI_TD_DEV_ADDR(dev->addr) | data_stage;
		(*out_td)[i + 1].buffer_ptr = (uint32_t)buf;
		(*out_td)[i + 1].swdata[0] = i + 1;

		if (toggle)
			(*out_td)[i + 1].token |= UHCI_TD_DATA_TOGGLE;

		toggle = !toggle;
		buf = (uint8_t *)(buf) + max_pkt_size;
	}

	(*out_td)[td_cnt - 1].link_ptr = UHCI_TD_LPTR_TERM;
	(*out_td)[td_cnt - 1].ctrl_status =
	    UHCI_TD_ERR_CNT(3) | (dev->low_speed ? UHCI_TD_LOW_SPEED : 0)
	    | UHCI_TD_IOC | UHCI_TD_STATUS_ACTIVE;
	(*out_td)[td_cnt - 1].token = UHCI_TD_MAX_LEN(0x7ff) | UHCI_TD_DATA_TOGGLE
	                              | UHCI_TD_DEV_ADDR(dev->addr) | status_stage;
	(*out_td)[td_cnt - 1].buffer_ptr = 0;
	(*out_td)[td_cnt - 1].swdata[0] = td_cnt;

	return td_cnt;
}

static uint16_t uhci_create_td_control_in(struct transfer_descriptor **out_td,
                                          const struct usb_device *dev,
                                          uint8_t request, uint16_t value,
                                          uint16_t index, uint16_t length,
                                          void *buf) {
	uint8_t request_type = UHCI_DR_RT_XDIR_DEV_HOST | UHCI_DR_RT_TYPE_STANDARD
	                       | UHCI_DR_RT_RECIPIENT_DEVICE;
	return uhci_create_td_control(out_td, dev, request_type, request, value,
	                              index, length, UHCI_TD_PID_IN,
	                              UHCI_TD_PID_OUT, buf);
}

static uint16_t uhci_create_td_control_out(struct transfer_descriptor **out_td,
                                           const struct usb_device *dev,
                                           uint8_t request, uint16_t value,
                                           uint16_t index, uint16_t length,
                                           void *buf) {
	uint8_t request_type = UHCI_DR_RT_XDIR_HOST_DEV | UHCI_DR_RT_TYPE_STANDARD
	                       | UHCI_DR_RT_RECIPIENT_DEVICE;
	return uhci_create_td_control(out_td, dev, request_type, request, value,
	                              index, length, UHCI_TD_PID_OUT,
	                              UHCI_TD_PID_IN, buf);
}

static void uhci_delete_td_control(struct transfer_descriptor **td) {
	memfree((void *)(*td)[0].buffer_ptr); // device_request
	memfree(*td);
	*td = NULL;
}

static void uhci_callback_trans_end(struct transfer_entry *te) {
	struct transfer_descriptor *td = te->first;
	while (td != te->last) {
		if (td->ctrl_status & UHCI_TD_STATUS_MASK) {
			uhci_print_td_status(td);
		}
		td = LINK_PTR_TO_TD(td->link_ptr);
	}
	*((bool *)te->userdata) = true;
}

static bool uhci_read_dev_desc(struct uhci_dev *dev, struct usb_device *udev,
                               struct device_descriptor *dev_desc) {
	struct transfer_descriptor *td = NULL;
	bool result = true;

	uint16_t ntd =
	    uhci_create_td_control_in(&td, udev, UHCI_DR_REQ_GET_DESCRIPTOR,
	                              UHCI_DR_VAL_DESC_DEVICE, 0, 18, dev_desc);

	bool done = false;
	struct transfer_entry *entry = memalloc(sizeof(struct transfer_entry));
	entry->first = td;
	entry->last = &td[ntd - 1];
	entry->handler = &uhci_callback_trans_end;
	entry->userdata = &done;
	entry->next = NULL;
	uhci_schedule_queue(dev->qh1ms, entry);

	result = uhci_wait_entry_complete(entry);
	memfree(entry);

	uhci_delete_td_control(&td);

	return result;
}

static bool uhci_read_dev_desc_maxpkg(struct uhci_dev *dev,
                                      struct usb_device *udev,
                                      struct device_descriptor *dev_desc) {
	struct transfer_descriptor *td = NULL;
	bool result = true;

	// Length is 8 to read up to the MaxPacketSize field
	uint16_t ntd = uhci_create_td_control_in(
	    &td, udev, UHCI_DR_REQ_GET_DESCRIPTOR, UHCI_DR_VAL_DESC_DEVICE, 0,
	    udev->low_speed ? 8 : 8, dev_desc);

	bool done = false;
	struct transfer_entry *entry = memalloc(sizeof(struct transfer_entry));
	entry->first = td;
	entry->last = &td[ntd - 1];
	entry->handler = &uhci_callback_trans_end;
	entry->userdata = &done;
	entry->next = NULL;
	uhci_schedule_queue(dev->qh1ms, entry);

	result = uhci_wait_entry_complete(entry);
	memfree(entry);

	uhci_delete_td_control(&td);

	return result;
}

static bool uhci_set_device_address(struct uhci_dev *dev,
                                    struct usb_device *udev, uint8_t addr) {
	struct transfer_descriptor *td = NULL;
	bool result = true;

	uint16_t ntd = uhci_create_td_control_out(
	    &td, udev, UHCI_DR_REQ_SET_ADDRESS, addr, 0, 0, 0);
	bool done = false;
	struct transfer_entry *entry = memalloc(sizeof(struct transfer_entry));
	entry->first = td;
	entry->last = &td[ntd - 1];
	entry->handler = &uhci_callback_trans_end;
	entry->userdata = &done;
	entry->next = NULL;
	uhci_schedule_queue(dev->qh1ms, entry);

	result = uhci_wait_entry_complete(entry);
	memfree(entry);
	if (result) {
		udev->addr = addr;
	}

	uhci_delete_td_control(&td);

	return result;
}

bool uhci_read_string_desc(struct uhci_dev *dev, struct usb_device *udev,
                           uint8_t index, struct string_descriptor **sdesc) {
	uint8_t desc_len = 0;
	struct transfer_descriptor *td = NULL;
	bool result = true;

	uint16_t ntd = uhci_create_td_control_in(
	    &td, udev, UHCI_DR_REQ_GET_DESCRIPTOR, UHCI_DR_VAL_DESC_STRING | index,
	    0, 1, &desc_len);

	bool done = false;
	struct transfer_entry *entry = memalloc(sizeof(struct transfer_entry));
	entry->first = td;
	entry->last = &td[ntd - 1];
	entry->handler = &uhci_callback_trans_end;
	entry->userdata = &done;
	entry->next = NULL;
	uhci_schedule_queue(dev->qh1ms, entry);
	result = uhci_wait_entry_complete(entry);
	memfree(entry);
	if (!result) {
		goto exit_error;
	}

	uhci_delete_td_control(&td);

	*sdesc = memalloc(desc_len);
	memfill(*sdesc, 0, desc_len);

	ntd = uhci_create_td_control_in(&td, udev, UHCI_DR_REQ_GET_DESCRIPTOR,
	                                UHCI_DR_VAL_DESC_STRING | index, 0,
	                                desc_len, *sdesc);

	done = false;
	entry = memalloc(sizeof(struct transfer_entry));
	entry->first = td;
	entry->last = &td[ntd - 1];
	entry->handler = &uhci_callback_trans_end;
	entry->userdata = &done;
	entry->next = NULL;
	uhci_schedule_queue(dev->qh1ms, entry);
	result = uhci_wait_entry_complete(entry);
	memfree(entry);
	if (!result) {
		memfree(*sdesc);
		*sdesc = NULL;
		goto exit_error;
	}

	goto exit;

exit_error:
	result = false;

exit:
	uhci_delete_td_control(&td);

	return result;
}

static bool pci_dev_init_cb(struct pci_dev *dev) {
	struct uhci_dev *uhci_dev = NULL;

	if (!is_UHCI_device(&dev->header)) {
		return false;
	}

	uhci_dev = memalloc(sizeof(struct uhci_dev));
	uhci_dev->pci_dev = dev;

	print_string("UHCI found\n");

	// Disable Legacy keyboard / mouse support
	pci_write_16(dev, PCI_HOFF_UHCI_LEGSUP, 0x2000);

	if ((dev->header.command & PCI_CMD_BUSMASTER_MASK) == 0) {
		dev->header.command |= PCI_CMD_BUSMASTER_MASK;
		pci_write_16(dev, PCI_HOFF_COMMAND, dev->header.command);
	}

	uhci_dev->iobase = dev->header.u.type00.bar[USBBASE_BAR_IDX] & 0xfffe;

	if (uhci_reset(uhci_dev) != 0) {
		print_string("UHCI reset timed out\n");
		goto fail;
	}

	uhci_init_frame_list(uhci_dev);

	uhci_dev->portnum = uhci_find_ports(uhci_dev);

	// enable interrupts
	uhci_write_16(uhci_dev, UHCI_USBINTR,
	              UHCI_USBINTR_SHORT_PKT_INT | UHCI_USBINTR_INT_ON_COMPLETE
	                  | UHCI_USBINTR_RESUME_INT | UHCI_USBINTR_CRC_TIMEOUT_INT);

	// set 64 byte packet size and start the controller
	uhci_write_16(uhci_dev, UHCI_USBCMD,
	              UHCI_USBCMD_MAX_PKT_SIZE | UHCI_USBCMD_RUN);

	print_string("UHCI init OK\n");

	print_string("UHCI INT line: ");
	print_string(
	    itoa_once(uhci_dev->pci_dev->header.u.type00.interrupt_line, 10));
	print_string("\n");

	struct idt_int_handler *h = memalloc(sizeof(struct idt_int_handler));
	h->handler = &uhci_isr;
	h->userdata = uhci_dev;

	idt_reg_handler(uhci_dev->pci_dev->header.u.type00.interrupt_line + 32, h);

	print_string("UHCI portnum: ");
	print_string(itoa_once(uhci_dev->portnum, 10));
	print_string("\n");

	for (uint8_t i = 0; i < uhci_dev->portnum; ++i) {
		// TODO: Use better check for presence (UHCI_PORTSC_CONNECT_STATUS)
		if ((uhci_read_16(uhci_dev, ports[i]) & UHCI_PORTSC_CONNECT_STATUS_CHG)
		    != 0) {
			struct usb_device *usb_dev = NULL;
			struct string_descriptor *sdesc = NULL;
			char *buf = NULL;
			uint8_t buflen = 0;

			print_string("CONNECT STATUS CHANGE detected on port ");
			print_string(itoa_once(i, 10));
			print_string("\n");

			print_string("LO: ");
			print_string(itoa_once(
			    UHCI_PORTSC_LOW_SPEED(uhci_read_16(uhci_dev, ports[i])), 16));
			print_string("\n");

			if (!uhci_enable_device_on_port(uhci_dev, ports[i])) {
				print_string("Device enablement failed");
				continue;
			}

			usb_dev = memalloc(sizeof(struct usb_device));
			memfill(usb_dev, 0, sizeof(struct usb_device));
			usb_dev->low_speed =
			    UHCI_PORTSC_LOW_SPEED(uhci_read_16(uhci_dev, ports[i]));
			if (!uhci_read_dev_desc_maxpkg(uhci_dev, usb_dev,
			                               &usb_dev->dev_desc)) {
				print_string("Failed to retrive initial device descriptor");
				memfree(usb_dev);
				continue;
			}

			if (!uhci_enable_device_on_port(uhci_dev, ports[i])) {
				print_string("Device enablement failed 2");
				continue;
			}

			if (!uhci_set_device_address(uhci_dev, usb_dev, i + 1)) {
				print_string("Failed to set device address");
				memfree(usb_dev);
				continue;
			}

			if (!uhci_read_dev_desc(uhci_dev, usb_dev, &usb_dev->dev_desc)) {
				print_string("Failed to retrive device descriptor");
				memfree(usb_dev);
				continue;
			}

			if (!uhci_read_string_desc(uhci_dev, usb_dev,
			                           usb_dev->dev_desc.manufacturer_idx,
			                           &sdesc)) {
				print_string("uhci_read_string_desc mfg FAIL");
			}

			buflen = (uint8_t)((sdesc->length - 2u) / 2u + 1u);
			buf = memalloc(buflen);
			memfill(buf, 0, buflen);

			wstr_to_str(sdesc->string, sdesc->length - 2, buf, buflen);
			print_string(buf);
			print_string(": ");

			memfree(buf);
			buf = NULL;

			memfree(sdesc);
			sdesc = NULL;

			if (!uhci_read_string_desc(uhci_dev, usb_dev,
			                           usb_dev->dev_desc.product_idx, &sdesc)) {
				print_string("uhci_read_string_desc prod FAIL");
			}

			buflen = (uint8_t)((sdesc->length - 2u) / 2u + 1u);
			buf = memalloc(buflen);
			memfill(buf, 0, buflen);

			wstr_to_str(sdesc->string, sdesc->length - 2, buf, buflen);
			print_string(buf);
			print_string("\n");

			memfree(buf);
			buf = NULL;

			memfree(sdesc);
			sdesc = NULL;
		} else {
			print_string("Inactive port: ");
			print_string(itoa_once(i, 10));
			print_string("\n");
		}
	}

	return true;

fail:
	print_pci_dev(dev);
	memfree(uhci_dev);
	return false;
}

void uhci_init() {
	struct pci_dev_driver drv = {.init = pci_dev_init_cb};
	pci_register_driver(&drv);
}
