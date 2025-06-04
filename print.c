#include "print.h"
#include "io.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

#define VGA_COLOR_MEM 0xB8000

#define VGA_LIGHT_GRAY 0x07

#define CRTC_ADDRESS 0x3d4
#define CRTC_DATA 0x3d5
#define CRTC_CURS_LOC_HI 0xe
#define CRTC_CURS_LOC_LO 0xf

static uint8_t curs_row = 0;
static uint8_t curs_col = 0;

static void move_vga_cursor(uint8_t column, uint8_t row) {
	uint16_t pos = (uint16_t)(row * VGA_WIDTH + column);

	outb(CRTC_ADDRESS, CRTC_CURS_LOC_LO);
	outb(CRTC_DATA, (uint8_t)(pos & 0xFF));
	outb(CRTC_ADDRESS, CRTC_CURS_LOC_HI);
	outb(CRTC_DATA, (uint8_t)((pos >> 8) & 0xFF));
}

static void get_vga_cursor(uint8_t *column, uint8_t *row) {
	uint16_t pos = 0;
	outb(CRTC_ADDRESS, CRTC_CURS_LOC_LO);
	pos |= inb(CRTC_DATA);
	outb(CRTC_ADDRESS, CRTC_CURS_LOC_HI);
	pos |= ((uint16_t)inb(CRTC_DATA)) << 8;

	*row = (uint8_t)(pos / VGA_WIDTH);
	*column = (uint8_t)(pos % VGA_WIDTH);
}

static void screen_scroll(void) {
	volatile char *video_dst = (volatile char *)VGA_COLOR_MEM;
	volatile char *video_src = video_dst + VGA_WIDTH * 2;
	volatile char *video_end =
	    (volatile char *)VGA_COLOR_MEM + VGA_WIDTH * 2 * VGA_HEIGHT * 2;

	while (video_src < video_end)
		*video_dst++ = *video_src++;

	for (uint8_t i = 0; i < VGA_WIDTH; i++) {
		*video_dst++ = ' ';
		*video_dst++ = VGA_LIGHT_GRAY;
	}
}

static void advance_cursor_newline(void) {
	if (curs_row + 1 == VGA_HEIGHT)
		screen_scroll();
	else
		curs_row += 1;

	curs_col = 0;
}

static void advance_cursor(void) {
	if (curs_col + 1 == VGA_WIDTH)
		advance_cursor_newline();
	else
		curs_col += 1;
}

void print_string(const char *string) {
	while (*string != 0) {
		volatile char *video = (volatile char *)VGA_COLOR_MEM
		                       + curs_row * VGA_WIDTH * 2 + curs_col * 2;
		if (*string == '\n') {
			advance_cursor_newline();
			string++;
			continue;
		}

		*video++ = *string++;
		*video++ = VGA_LIGHT_GRAY;

		advance_cursor();
	}

	move_vga_cursor(curs_col, curs_row);
}

void init_output(void) {
	get_vga_cursor(&curs_col, &curs_row);
	print_string("\n");
}

// itoa OSDev Wiki implementation
char *itoa(int value, char *str, int base) {
	char *rc;
	char *ptr;
	char *low;

	if (base < 2 || base > 36) {
		*str = '\0';
		return str;
	}
	rc = ptr = str;
	// Set '-' for negative decimals.
	if (value < 0 && base == 10) {
		*ptr++ = '-';
	}
	// Remember where the numbers start.
	low = ptr;
	// The actual conversion.
	do {
		// Modulo is negative for negative value. This trick makes abs()
		// unnecessary.
		*ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnop"
		         "qrstuvwxyz"[35 + value % base];
		value /= base;
	} while (value);
	// Terminating the string.
	*ptr-- = '\0';
	// Invert the numbers.
	while (low < ptr) {
		char tmp = *low;
		*low++ = *ptr;
		*ptr-- = tmp;
	}
	return rc;
}
