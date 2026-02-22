#include <stddef.h>

#include "idt.h"
#include "print.h"

#define MAX_ISR_CNT 48 // CPU exceptions + remaped PIC

static struct idt_int_handler *int_handlers[MAX_ISR_CNT] = {0};

void idt_reg_handler(uint8_t int_n, struct idt_int_handler *h) {
	if (int_n >= MAX_ISR_CNT) {
		return;
	}

	h->_next = NULL;
	if (!int_handlers[int_n]) {
		int_handlers[int_n] = h;
		return;
	}

	struct idt_int_handler *elem = int_handlers[int_n];
	while (elem->_next) {
		elem = elem->_next;
	}
	elem->_next = h;
}

void idt_common_isr(uint8_t int_n) {
	if (!int_handlers[int_n]) {
		// no registered handler
		print_string("No handler for INT ");
		print_string(itoa_once(int_n, 10));
		print_string("\n");
		return;
	}

	struct idt_int_handler *elem = int_handlers[int_n];

	do {
		if (elem->handler(int_n, elem->userdata)) {
			return;
		}
		elem = elem->_next;
	} while (elem);
	print_string("INT ");
	print_string(itoa_once(int_n, 10));
	print_string(" not handled\n");
}

void idt_set(struct idtr *idtr) {
	__asm__ volatile("lidt    %0"
	                 :            /* Outputs  */
	                 : "m"(*idtr) /* Inputs   */
	                 :            /* Clobbers */
	);
}

void idt_get(struct idtr *idtr) {
	__asm__ volatile("sidt    %0"
	                 : "=m"(*idtr) /* Outputs  */
	                 :             /* Inputs   */
	                 :             /* Clobbers */
	);
}
