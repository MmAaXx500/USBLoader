#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "idt.h"
#include "io.h"
#include "pit.h"

// ========================================================
// PIT IO registers

#define PIT_COUNTER_0 0x40
#define PIT_COUNTER_1 0x41
#define PIT_COUNTER_2 0x42
#define PIT_TCW       0x43

// ========================================================
// TCW — Timer Control Word Register

// Counter Select
#define TCW_COUNTER_0 0
#define TCW_COUNTER_1 (1 << 6)
#define TCW_COUNTER_2 (2 << 6)
// Read Back Command
#define TCW_READ_BACK (3 << 6)

// Read/Write Select
// Counter Latch Command
#define TCW_RW_LATCH   0
// Read/Write Least Significant Byte
#define TCW_RW_LSB     (1 << 4)
// Read/Write Most Significant Byte
#define TCW_RW_MSB     (2 << 4)
// Read/Write LSB then MSB
#define TCW_RW_LSB_MSB (3 << 4)

// Counter Mode Selection
// Out signal on end of count
#define TCW_MODE_INT_ON_0    0
// Hardware retriggerable one-shot
#define TCW_MODE_HW_ONE_SHOT (1 << 1)
// Rate generator
#define TCW_MODE_RATE_GEN    (2 << 1)
// Square wave output
#define TCW_MODE_SQUARE_WAVE (3 << 1)
// Software triggered strobe
#define TCW_MODE_SW_STROBE   (4 << 1)
// Hardware triggered strobe
#define TCW_MODE_HW_STROBE   (5 << 1)

// Binary/BCD Countdown Select
#define TCW_BINARY 0
#define TCW_BCD    1

// ========================================================

#define PIT_HZ     1193182
#define PIT_1000HZ 1193

static volatile uint32_t timer_ms_left = 0;

bool pit_timer_isr(uint8_t, void *);
static struct idt_int_handler int_h = {&pit_timer_isr, NULL, NULL};

void sleep(uint32_t ms) {
	timer_ms_left = ms;
	while (timer_ms_left != 0)
		__asm__("hlt");
}

void pit_init(void) {
	uint16_t divisor = (uint16_t)PIT_1000HZ;
	idt_reg_handler(32, &int_h);

	outb(PIT_TCW,
	     TCW_COUNTER_0 | TCW_RW_LSB_MSB | TCW_MODE_RATE_GEN | TCW_BINARY);

	outb(PIT_COUNTER_0, (uint8_t)(divisor & 0xff)); // LSB
	outb(PIT_COUNTER_0, (uint8_t)(divisor >> 8));   // MSB
}

bool pit_timer_isr(uint8_t int_n, void *userdata) {
	(void)int_n, (void)userdata;
	if (timer_ms_left > 0) {
		timer_ms_left--;
	}
	return false;
}
