#pragma once

#include <stdbool.h>
#include <stdint.h>

// ========================================================
// IDT Gate Present bit

#define IDT_GATE_PRESENT (1 << 15)

// ========================================================
// IDT Gate DLP

#define IDT_GATE_DPL_RING0 0
#define IDT_GATE_DPL_RING1 (0x1 << 13)
#define IDT_GATE_DPL_RING2 (0x2 << 13)
#define IDT_GATE_DPL_RING3 (0x3 << 13)

// ========================================================
// IDT Gate Types

#define IDT_GATE_TASK   (0x5 << 8)
#define IDT_GATE_16INT  (0x6 << 8)
#define IDT_GATE_16TRAP (0x7 << 8)
#define IDT_GATE_32INT  (0xE << 8)
#define IDT_GATE_32TRAP (0xF << 8)

// Interrupt Descriptor Table
struct __attribute__((__packed__)) idtr {
	uint16_t len;
	uint32_t offset;
};

// Interrupt Descriptor Table Gate
struct __attribute__((__packed__)) idt_gate {
	uint16_t offset_low;
	uint16_t segment;
	uint16_t flags;
	uint16_t offset_high;
};

struct idt_int_handler {
	/*
	 * Handler function that is called when the interrupt is triggered.
	 *
	 * @param int_n interrupt number
	 * @param userdata the userdata passed at the registration
	 *
	 * @return true if the interrupt is handled; false if the interrupt is not
	 * handled (ex: the INT may come from an other device in case of shared
	 * interrupt lines).
	 */
	bool (*handler)(uint8_t int_n, void *userdata);
	/*
	 * Arbitrary user data. Passed to the handler as a parameter.
	 */
	void *userdata;
	// internal
	struct idt_int_handler *_next; // included in this struct to push the
	                               // allocation responsibility to the caller
};

/*
 * Register a new handler for the specified interrupt.
 * The interrupt handers are called in the order they registered.
 * The handler is not called if a previous handler is already handled the
 * interrupt.
 *
 * @param int_n interrupt number
 * @param h handler
 */
void idt_reg_handler(uint8_t int_n, struct idt_int_handler *h);

/*
 * Load a new IDT
 *
 * @param idtr Pointer to idtr to use
 */
void idt_set(struct idtr *idtr);

/*
 * Get current IDT
 *
 * @param idtr Currently loaded idtr is returned here
 */
void idt_get(struct idtr *idtr);
