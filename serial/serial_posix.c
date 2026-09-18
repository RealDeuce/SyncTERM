/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "serial.h"

#include "io_error.h"
#include "serial_config.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

struct serial_port {
	int descriptor;
};

struct serial_baud {
	uint32_t rate;
	speed_t value;
};

#define SERIAL_BAUD(rate_) { (rate_), B ## rate_ }
static const struct serial_baud serial_baud_rates[] = {
	SERIAL_BAUD(50),
	SERIAL_BAUD(75),
	SERIAL_BAUD(110),
	SERIAL_BAUD(134),
	SERIAL_BAUD(150),
	SERIAL_BAUD(200),
	SERIAL_BAUD(300),
	SERIAL_BAUD(600),
	SERIAL_BAUD(1200),
	SERIAL_BAUD(1800),
	SERIAL_BAUD(2400),
	SERIAL_BAUD(4800),
	SERIAL_BAUD(9600),
	SERIAL_BAUD(19200),
	SERIAL_BAUD(38400),
#if ST_SERIAL_HAVE_B57600
	SERIAL_BAUD(57600),
#endif
#if ST_SERIAL_HAVE_B115200
	SERIAL_BAUD(115200),
#endif
#if ST_SERIAL_HAVE_B230400
	SERIAL_BAUD(230400),
#endif
#if ST_SERIAL_HAVE_B460800
	SERIAL_BAUD(460800),
#endif
#if ST_SERIAL_HAVE_B500000
	SERIAL_BAUD(500000),
#endif
#if ST_SERIAL_HAVE_B576000
	SERIAL_BAUD(576000),
#endif
#if ST_SERIAL_HAVE_B921600
	SERIAL_BAUD(921600),
#endif
#if ST_SERIAL_HAVE_B1000000
	SERIAL_BAUD(1000000),
#endif
#if ST_SERIAL_HAVE_B1152000
	SERIAL_BAUD(1152000),
#endif
#if ST_SERIAL_HAVE_B1500000
	SERIAL_BAUD(1500000),
#endif
#if ST_SERIAL_HAVE_B2000000
	SERIAL_BAUD(2000000),
#endif
#if ST_SERIAL_HAVE_B2500000
	SERIAL_BAUD(2500000),
#endif
#if ST_SERIAL_HAVE_B3000000
	SERIAL_BAUD(3000000),
#endif
#if ST_SERIAL_HAVE_B3500000
	SERIAL_BAUD(3500000),
#endif
#if ST_SERIAL_HAVE_B4000000
	SERIAL_BAUD(4000000),
#endif
};
#undef SERIAL_BAUD

static int
serial_posix_error(int error)
{
	if (error == ENXIO || error == ENODEV)
		return IO_ERROR_NOT_FOUND;
	return io_error_from_errno(error);
}

int
serial_port_open(serial_port_t **result, const char *device)
{
	serial_port_t *port;
	struct termios attributes;
	int descriptor;
	int flags;

	if (result == NULL || device == NULL || *device == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	descriptor = open(device, O_RDWR | O_NONBLOCK | O_NOCTTY);
	if (descriptor < 0)
		return serial_posix_error(errno);
	if (tcgetattr(descriptor, &attributes) != 0) {
		int error = serial_posix_error(errno);
		(void)close(descriptor);
		return error;
	}
	flags = fcntl(descriptor, F_GETFD);
	if (flags < 0 || fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) != 0) {
		int error = serial_posix_error(errno);
		(void)close(descriptor);
		return error;
	}
	port = malloc(sizeof(*port));
	if (port == NULL) {
		(void)close(descriptor);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	port->descriptor = descriptor;
	*result = port;
	return 0;
}

int
serial_port_close(serial_port_t *port)
{
	int result;

	if (port == NULL)
		return 0;
	(void)tcflush(port->descriptor, TCOFLUSH);
	result = close(port->descriptor) == 0 ? 0 : serial_posix_error(errno);
	free(port);
	return result;
}

static bool
serial_baud_value(uint32_t rate, speed_t *value)
{
	size_t i;

	for (i = 0; i < sizeof(serial_baud_rates) /
	    sizeof(serial_baud_rates[0]); i++) {
		if (serial_baud_rates[i].rate == rate) {
			*value = serial_baud_rates[i].value;
			return true;
		}
	}
	return false;
}

static int
serial_config_valid(const struct serial_config *config)
{
	const unsigned flow_mask = SERIAL_FLOW_CONTROL_RTS_CTS |
	    SERIAL_FLOW_CONTROL_DTR_DSR | SERIAL_FLOW_CONTROL_XON_XOFF;

	if (config == NULL || config->data_bits < 5 || config->data_bits > 8 ||
	    (config->stop_bits != 1 && config->stop_bits != 2) ||
	    (config->parity != SERIAL_PARITY_NONE &&
	    config->parity != SERIAL_PARITY_EVEN &&
	    config->parity != SERIAL_PARITY_ODD) ||
	    (config->flow_control & ~flow_mask) != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if ((config->flow_control & SERIAL_FLOW_CONTROL_DTR_DSR) != 0)
		return IO_ERROR_NOT_SUPPORTED;
#if !ST_SERIAL_HAVE_CRTSCTS && !ST_SERIAL_HAVE_CCTS_OFLOW_CRTS_IFLOW
	if ((config->flow_control & SERIAL_FLOW_CONTROL_RTS_CTS) != 0)
		return IO_ERROR_NOT_SUPPORTED;
#endif
	return 0;
}

int
serial_port_configure(serial_port_t *port,
	const struct serial_config *config)
{
	struct termios attributes;
	speed_t input_rate;
	speed_t output_rate;
	speed_t requested_rate = 0;
	int result;

	if (port == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = serial_config_valid(config);
	if (result != 0)
		return result;
	if (config->baud_rate != 0 &&
	    !serial_baud_value(config->baud_rate, &requested_rate))
		return IO_ERROR_NOT_SUPPORTED;
	if (tcgetattr(port->descriptor, &attributes) != 0)
		return serial_posix_error(errno);
	input_rate = cfgetispeed(&attributes);
	output_rate = cfgetospeed(&attributes);
	attributes.c_iflag = IGNBRK | IGNPAR;
	attributes.c_oflag = 0;
	attributes.c_cflag = CREAD | CLOCAL | HUPCL;
	attributes.c_lflag = 0;
	attributes.c_cc[VMIN] = 0;
	attributes.c_cc[VTIME] = 0;
	switch (config->data_bits) {
		case 5:
			attributes.c_cflag |= CS5;
			break;
		case 6:
			attributes.c_cflag |= CS6;
			break;
		case 7:
			attributes.c_cflag |= CS7;
			break;
		case 8:
			attributes.c_cflag |= CS8;
			break;
	}
	if (config->stop_bits == 2)
		attributes.c_cflag |= CSTOPB;
	if (config->parity != SERIAL_PARITY_NONE) {
		attributes.c_cflag |= PARENB;
		if (config->parity == SERIAL_PARITY_ODD)
			attributes.c_cflag |= PARODD;
	}
	if ((config->flow_control & SERIAL_FLOW_CONTROL_XON_XOFF) != 0)
		attributes.c_iflag |= IXON | IXOFF;
#if ST_SERIAL_HAVE_CRTSCTS
	if ((config->flow_control & SERIAL_FLOW_CONTROL_RTS_CTS) != 0)
		attributes.c_cflag |= CRTSCTS;
#elif ST_SERIAL_HAVE_CCTS_OFLOW_CRTS_IFLOW
	if ((config->flow_control & SERIAL_FLOW_CONTROL_RTS_CTS) != 0)
		attributes.c_cflag |= CCTS_OFLOW | CRTS_IFLOW;
#endif
	if (config->baud_rate != 0)
		input_rate = output_rate = requested_rate;
	if (cfsetispeed(&attributes, input_rate) != 0 ||
	    cfsetospeed(&attributes, output_rate) != 0)
		return errno == EINVAL ? IO_ERROR_NOT_SUPPORTED :
		    serial_posix_error(errno);
	if (tcsetattr(port->descriptor, TCSANOW, &attributes) != 0)
		return errno == EINVAL ? IO_ERROR_NOT_SUPPORTED :
		    serial_posix_error(errno);
	return 0;
}

static int
serial_wait(serial_port_t *port, short events, int64_t timeout_ms)
{
	struct pollfd item;
	int64_t remaining = timeout_ms;
	int timeout;
	int result;

	if (timeout_ms < -1)
		return IO_ERROR_INVALID_ARGUMENT;
	item.fd = port->descriptor;
	item.events = events;
	for (;;) {
		timeout = remaining > INT_MAX ? INT_MAX : (int)remaining;
		item.revents = 0;
		result = poll(&item, 1, timeout);
		if (result < 0)
			return serial_posix_error(errno);
		if (result != 0)
			break;
		if (remaining <= INT_MAX)
			return IO_ERROR_TIMED_OUT;
		remaining -= INT_MAX;
	}
	if ((item.revents & POLLNVAL) != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if ((item.revents & POLLERR) != 0)
		return IO_ERROR_IO;
	return 0;
}

ssize_t
serial_port_read(serial_port_t *port, void *buffer, size_t size,
	int64_t timeout_ms)
{
	ssize_t result;
	int wait_result;

	if (port == NULL || (buffer == NULL && size != 0) || timeout_ms < -1)
		return IO_ERROR_INVALID_ARGUMENT;
	if (size == 0)
		return 0;
	if (size > (size_t)PTRDIFF_MAX)
		size = (size_t)PTRDIFF_MAX;
	wait_result = serial_wait(port, POLLIN, timeout_ms);
	if (wait_result != 0)
		return wait_result;
	result = read(port->descriptor, buffer, size);
	if (result >= 0)
		return result;
	return serial_posix_error(errno);
}

ssize_t
serial_port_write(serial_port_t *port, const void *buffer, size_t size,
	int64_t timeout_ms)
{
	ssize_t result;
	int wait_result;

	if (port == NULL || (buffer == NULL && size != 0) || timeout_ms < -1)
		return IO_ERROR_INVALID_ARGUMENT;
	if (size == 0)
		return 0;
	if (size > (size_t)PTRDIFF_MAX)
		size = (size_t)PTRDIFF_MAX;
	wait_result = serial_wait(port, POLLOUT, timeout_ms);
	if (wait_result != 0)
		return wait_result;
	result = write(port->descriptor, buffer, size);
	if (result >= 0)
		return result;
	return serial_posix_error(errno);
}

int
serial_port_get_modem_lines(serial_port_t *port, unsigned *lines)
{
#if ST_SERIAL_HAVE_MODEM_STATUS
	int status;
	unsigned result = 0;

	if (port == NULL || lines == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (ioctl(port->descriptor, TIOCMGET, &status) != 0)
		return serial_posix_error(errno);
	if ((status & TIOCM_CTS) != 0)
		result |= SERIAL_MODEM_LINE_CTS;
	if ((status & TIOCM_DSR) != 0)
		result |= SERIAL_MODEM_LINE_DSR;
#if ST_SERIAL_HAVE_TIOCM_CAR
	if ((status & TIOCM_CAR) != 0)
		result |= SERIAL_MODEM_LINE_DCD;
#elif ST_SERIAL_HAVE_TIOCM_CD
	if ((status & TIOCM_CD) != 0)
		result |= SERIAL_MODEM_LINE_DCD;
#endif
#if ST_SERIAL_HAVE_TIOCM_RNG
	if ((status & TIOCM_RNG) != 0)
		result |= SERIAL_MODEM_LINE_RING;
#elif ST_SERIAL_HAVE_TIOCM_RI
	if ((status & TIOCM_RI) != 0)
		result |= SERIAL_MODEM_LINE_RING;
#endif
	*lines = result;
	return 0;
#else
	(void)port;
	(void)lines;
	return port == NULL || lines == NULL ? IO_ERROR_INVALID_ARGUMENT :
	    IO_ERROR_NOT_SUPPORTED;
#endif
}

static int
serial_set_modem_line(serial_port_t *port, int line, bool enabled)
{
#if ST_SERIAL_HAVE_MODEM_CONTROL
	if (port == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if ((enabled && ioctl(port->descriptor, TIOCMBIS, &line) == 0) ||
	    (!enabled && ioctl(port->descriptor, TIOCMBIC, &line) == 0))
		return 0;
	return serial_posix_error(errno);
#else
	(void)line;
	(void)enabled;
	return port == NULL ? IO_ERROR_INVALID_ARGUMENT : IO_ERROR_NOT_SUPPORTED;
#endif
}

int
serial_port_set_dtr(serial_port_t *port, bool enabled)
{
#if ST_SERIAL_HAVE_MODEM_CONTROL
	return serial_set_modem_line(port, TIOCM_DTR, enabled);
#else
	return serial_set_modem_line(port, 0, enabled);
#endif
}

int
serial_port_set_rts(serial_port_t *port, bool enabled)
{
#if ST_SERIAL_HAVE_MODEM_CONTROL
	return serial_set_modem_line(port, TIOCM_RTS, enabled);
#else
	return serial_set_modem_line(port, 0, enabled);
#endif
}

int
serial_port_get_baud_rates(serial_port_t *port, uint32_t *rates,
	size_t capacity, size_t *count)
{
	size_t available = sizeof(serial_baud_rates) /
	    sizeof(serial_baud_rates[0]);
	size_t copied;
	size_t i;

	if (port == NULL || count == NULL || (rates == NULL && capacity != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	copied = capacity < available ? capacity : available;
	for (i = 0; i < copied; i++)
		rates[i] = serial_baud_rates[i].rate;
	*count = available;
	return 0;
}
