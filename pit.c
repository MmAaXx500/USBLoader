#include <stdbool.h>
#include <stdint.h>

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
#define TCW_RW_LATCH 0
// Read/Write Least Significant Byte
#define TCW_RW_LSB (1 << 4)
// Read/Write Most Significant Byte
#define TCW_RW_MSB (2 << 4)
// Read/Write LSB then MSB
#define TCW_RW_LSB_MSB (3 << 4)

// Counter Mode Selection
// Out signal on end of count
#define TCW_MODE_INT_ON_0 0
// Hardware retriggerable one-shot
#define TCW_MODE_HW_ONE_SHOT (1 << 1)
// Rate generator
#define TCW_MODE_RATE_GEN (2 << 1)
// Square wave output
#define TCW_MODE_SQUARE_WAVE (3 << 1)
// Software triggered strobe
#define TCW_MODE_SW_STROBE (4 << 1)
// Hardware triggered strobe
#define TCW_MODE_HW_STROBE (5 << 1)

// Binary/BCD Countdown Select
#define TCW_BINARY 0
#define TCW_BCD    1

// ========================================================

#define PIT_HZ 1193182
// largest whole ms
#define PIT_MAX_MS (1000 * 65536 / PIT_HZ)

static bool timer_int_fired = false;

void sleep(uint32_t ms) {
	uint32_t pit_ms = ms;
	uint16_t divisor;

	while (ms > 0) {
		if (ms > PIT_MAX_MS) {
			pit_ms = PIT_MAX_MS;
			ms -= PIT_MAX_MS;
		} else {
			pit_ms = ms;
			ms = 0;
		}

		divisor = (uint16_t)(PIT_HZ * pit_ms / 1000);

		outb(PIT_TCW,
		     TCW_COUNTER_0 | TCW_RW_LSB_MSB | TCW_MODE_INT_ON_0 | TCW_BINARY);

		outb(PIT_COUNTER_0, (uint8_t)(divisor & 0xff)); // LSB
		outb(PIT_COUNTER_0, (uint8_t)(divisor >> 8));   // MSB

		timer_int_fired = false;
		while (!timer_int_fired)
			__asm__("hlt");
	}
}

// called from assembly
// INT0 timer interrupt ISR
void pit_isr_timer(void) { timer_int_fired = true; }
