/* Serial-port configuration and byte I/O. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_SERIAL_H_
#define ST_SERIAL_H_

#include "io_error.h"
#include "syncterm_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct serial_port serial_port_t;

enum serial_parity {
	SERIAL_PARITY_NONE,
	SERIAL_PARITY_EVEN,
	SERIAL_PARITY_ODD,
};

enum serial_flow_control {
	SERIAL_FLOW_CONTROL_NONE = 0,
	SERIAL_FLOW_CONTROL_RTS_CTS = 1U << 0,
	SERIAL_FLOW_CONTROL_DTR_DSR = 1U << 1,
	SERIAL_FLOW_CONTROL_XON_XOFF = 1U << 2,
};

enum serial_modem_line {
	SERIAL_MODEM_LINE_CTS = 1U << 0,
	SERIAL_MODEM_LINE_DSR = 1U << 1,
	SERIAL_MODEM_LINE_RING = 1U << 2,
	SERIAL_MODEM_LINE_DCD = 1U << 3,
};

struct serial_config {
	/* A zero rate preserves the rate already configured on the device. */
	uint32_t baud_rate;
	unsigned data_bits;
	unsigned stop_bits;
	enum serial_parity parity;
	unsigned flow_control;
};

/*
 * Device is a path on POSIX and a UTF-8 COM-device name on Windows. The
 * device configuration is not changed by open. Result is unchanged on
 * failure. Closing NULL succeeds.
 */
int serial_port_open(serial_port_t **result, const char *device);
int serial_port_close(serial_port_t *port);

/*
 * Applies the complete configuration in one native operation. Data bits must
 * be 5 through 8 and stop bits must be 1 or 2. Unsupported baud rates and
 * flow-control modes are rejected rather than approximated.
 */
int serial_port_configure(serial_port_t *port,
	const struct serial_config *config);

/*
 * Returns a byte count (which may be short) or IO_ERROR_*. A zero timeout
 * polls without waiting and -1 waits indefinitely. No other negative timeout
 * is valid. A successful zero-sized operation returns zero immediately.
 */
ssize_t serial_port_read(serial_port_t *port, void *buffer, size_t size,
	int64_t timeout_ms);
ssize_t serial_port_write(serial_port_t *port, const void *buffer, size_t size,
	int64_t timeout_ms);

int serial_port_get_modem_lines(serial_port_t *port, unsigned *lines);
int serial_port_set_dtr(serial_port_t *port, bool enabled);
int serial_port_set_rts(serial_port_t *port, bool enabled);

/*
 * Reports the complete number of enumerable rates in count and copies at
 * most capacity entries to rates. The POSIX list reports rates expressible by
 * the termios programming environment; a particular device may reject one.
 * Drivers which accept arbitrary rates may still report only their
 * enumerable standard rates.
 */
int serial_port_get_baud_rates(serial_port_t *port, uint32_t *rates,
	size_t capacity, size_t *count);

/*
 * One reader and one writer may use a port concurrently. Configuration and
 * modem-line operations must not race I/O. The owner must stop all users
 * before closing the port.
 */

#endif
