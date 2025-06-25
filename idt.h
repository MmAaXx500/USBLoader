#pragma once

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
#pragma pack(push, 1)
struct idtr {
	uint16_t len;
	uint32_t offset;
};

// Interrupt Descriptor Table Gate
struct idt_gate {
	uint16_t offset_low;
	uint16_t segment;
	uint16_t flags;
	uint16_t offset_high;
};
#pragma pack(pop)

/*
 * Load a new IDT
 *
 * @param idtr Pointer to idtr to use
 */
static void idt_set(struct idtr *idtr) {
	__asm__ volatile("lidt    %0"
	                 :            /* Outputs  */
	                 : "m"(*idtr) /* Inputs   */
	                 :            /* Clobbers */
	);
}

/*
 * Get current IDT
 *
 * @param idtr Currently loaded idtr is returned here
 */
static void idt_get(struct idtr *idtr) {
	__asm__ volatile("sidt    %0"
	                 : "=m"(*idtr) /* Outputs  */
	                 :             /* Inputs   */
	                 :             /* Clobbers */
	);
}
