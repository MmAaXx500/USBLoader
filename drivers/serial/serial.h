#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	COM1 = 0x3f8,
	COM2 = 0x2f8,
} serial_port;

/**
 * Initialize a serial port or change it's parameters. This must be called at
 * least once per port to behave predictably
 *
 * @param port serial port to initialize
 * @param bps desized speed in bits per second
 * @return true if the port is initialized and working correctly; false if the
 * port faulty / nonexistent
 */
bool serial_init_port(serial_port port, uint32_t bps);

/**
 * Send one byte on a port
 *
 * @param port port to send on
 * @param ch byte to send
 */
void serial_write(serial_port port, uint8_t ch);

/**
 * Check if there is any data received and waiting to be read
 *
 * @param port port to check
 * @return true if data is received
 */
bool serial_data_ready(serial_port port);

/**
 * Receiving a byte on a port. It is recommended to use `serial_data_ready` to
 * check if there is data available
 *
 * @param port port to receive on
 * @return the byte read or 0 if there is no
 */
uint8_t serial_read(serial_port port);
