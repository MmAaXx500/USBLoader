#include "serial.h"
#include "io.h"
#include "pit.h"

#define RECV_BUF(com)         ((uint16_t)(com + 0))
#define SEND_BUF(com)         ((uint16_t)(com + 0))
#define INT_ENA(com)          ((uint16_t)(com + 1))
#define DIV_LO_DLAB(com)      ((uint16_t)(com + 0))
#define DIV_HI_DLAB(com)      ((uint16_t)(com + 1))
#define INT_ID(com)           ((uint16_t)(com + 2))
#define FIFO_CTRL(com)        ((uint16_t)(com + 2))
#define LINE_CTRL_REG(com)    ((uint16_t)(com + 3))
#define MODEM_CTRL_REG(com)   ((uint16_t)(com + 4))
#define LINE_STATUS_REG(com)  ((uint16_t)(com + 5))
#define MODEM_STATUS_REG(com) ((uint16_t)(com + 6))
#define SCRATCH_REG(com)      ((uint16_t)(com + 7))

static bool fifo_empty(serial_port port) {
	return (inb(LINE_STATUS_REG(port)) & 0x20) != 0;
}

bool serial_init_port(serial_port port, uint32_t bps) {
	// xtal frequency in Hz / 16 / desired rate = divisor
	uint16_t divisor = (uint16_t)(1843200 / 16 / bps);

	// disable interrupts
	outb(INT_ENA(port), 0);

	// set DLAB to enable access to the divisor regs
	outb(LINE_CTRL_REG(port), 0x80);
	// set divisor
	outb(DIV_LO_DLAB(port), (uint8_t)divisor);
	outb(DIV_HI_DLAB(port), (uint8_t)(divisor >> 8));

	// clear DLAB, parity no, stop bits 1, databits 8
	outb(LINE_CTRL_REG(port), 0x03);
	// 14 byte trigger, clear tx & rx FIFO, enable FIFO
	outb(FIFO_CTRL(port), 0xc7);
	// enable loopback, ena OUT2, ena OUT1, RTS set, DTR set
	outb(MODEM_CTRL_REG(port), 0x1f);

	// set testbyte in loopback mode to test the port
	outb(SEND_BUF(port), 0xae);

	uint8_t tries = 10; // 10 * 10ms (100ms)
	// check line status register data ready bit
	while (tries > 0 && ((inb(LINE_STATUS_REG(port)) & 0x1) == 0)) {
		sleep(10);
		tries--;
	}

	// normal operation, ena OUT2, ena OUT1, RTS set, DTR set
	outb(MODEM_CTRL_REG(port), 0x0f);

	return tries > 0;
}

void serial_write(serial_port port, uint8_t ch) {
	while (!fifo_empty(port))
		;

	outb(SEND_BUF(port), ch);
}

bool serial_data_ready(serial_port port) {
	return (inb(LINE_STATUS_REG(port)) & 0x01) != 0;
}

uint8_t serial_read(serial_port port) {
	if (!serial_data_ready(port))
		return 0;

	return inb(RECV_BUF(port));
}
