/* Serial and dial-up modem transports. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "connection_internal.h"

#include "io_error.h"
#include "serial.h"
#include "st_settings.h"
#include "st_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct serial_transport {
	serial_port_t *port;
	bool seven_bits;
	bool modem;
	bool monitor_line;
	unsigned monitored_line;
};

static uint64_t
serial_now_ms(void)
{
	uint64_t now = 0;

	(void)st_time_monotonic_ms(&now);
	return now;
}

static ssize_t
serial_read_transport(void *state, void *data, size_t size,
	int64_t timeout_ms)
{
	struct serial_transport *transport = state;
	ssize_t result = serial_port_read(transport->port, data, size,
	    timeout_ms);

	if (result > 0 && transport->seven_bits) {
		uint8_t *bytes = data;
		ssize_t i;

		for (i = 0; i < result; ++i)
			bytes[i] &= 0x7f;
	}
	if ((result == IO_ERROR_TIMED_OUT || result == IO_ERROR_WOULD_BLOCK)
	    && transport->monitor_line) {
		unsigned lines;

		if (serial_port_get_modem_lines(transport->port, &lines) == 0
		    && (lines & transport->monitored_line) == 0)
			return 0;
	}
	return result;
}

static ssize_t
serial_write_transport(void *state, const void *data, size_t size,
	int64_t timeout_ms)
{
	struct serial_transport *transport = state;
	uint8_t masked[4096];
	size_t count;
	size_t i;

	if (!transport->seven_bits)
		return serial_port_write(transport->port, data, size, timeout_ms);
	count = size > sizeof(masked) ? sizeof(masked) : size;
	for (i = 0; i < count; ++i)
		masked[i] = ((const uint8_t *)data)[i] & 0x7f;
	return serial_port_write(transport->port, masked, count, timeout_ms);
}

static void
serial_interrupt(void *state)
{
	struct serial_transport *transport = state;

	if (!transport->modem)
		(void)serial_port_set_dtr(transport->port, false);
}

static int
serial_update_terminal_size(void *state,
	const struct st_screen_geometry *geometry)
{
	(void)state;
	(void)geometry;
	return 0;
}

static void
serial_destroy(void *state)
{
	struct serial_transport *transport = state;

	if (transport->modem) {
		unsigned lines = 0;
		uint64_t deadline;
		struct timespec pause = {0, 100000000L};

		(void)st_thread_sleep(&(struct timespec){0, 500000000L}, NULL);
		(void)serial_port_set_dtr(transport->port, false);
		deadline = serial_now_ms() + 10000U;
		while (serial_now_ms() < deadline
		    && serial_port_get_modem_lines(transport->port, &lines) == 0
		    && (lines & SERIAL_MODEM_LINE_DCD) != 0)
			(void)st_thread_sleep(&pause, NULL);
	}
	(void)serial_port_close(transport->port);
	free(transport);
}

static const struct st_connection_transport_ops serial_operations = {
	.bind = NULL,
	.read = serial_read_transport,
	.write = serial_write_transport,
	.interrupt = serial_interrupt,
	.update_terminal_size = serial_update_terminal_size,
	.destroy = serial_destroy,
};

static enum serial_parity
profile_parity(int parity)
{
	if (parity == ST_PROFILE_PARITY_EVEN)
		return SERIAL_PARITY_EVEN;
	if (parity == ST_PROFILE_PARITY_ODD)
		return SERIAL_PARITY_ODD;
	return SERIAL_PARITY_NONE;
}

static int
write_all(serial_port_t *port, const void *data, size_t size,
	const struct st_connection_hooks *hooks,
	enum st_connection_setup_phase phase)
{
	const uint8_t *bytes = data;
	size_t written = 0;

	while (written < size) {
		ssize_t result;

		if (st_connection_cancelled(hooks, phase))
			return IO_ERROR_CANCELLED;
		result = serial_port_write(port, bytes + written, size - written,
		    100);
		if (result == IO_ERROR_TIMED_OUT || result == IO_ERROR_WOULD_BLOCK)
			continue;
		if (result <= 0)
			return result == 0 ? IO_ERROR_IO : (int)result;
		written += (size_t)result;
	}
	return 0;
}

static int
modem_line(serial_port_t *port, char *line, size_t capacity,
	int64_t timeout_ms, const struct st_connection_hooks *hooks,
	enum st_connection_setup_phase phase)
{
	size_t length = 0;
	uint64_t deadline = serial_now_ms() + (uint64_t)timeout_ms;

	while (serial_now_ms() < deadline) {
		uint8_t byte;
		ssize_t result;

		if (st_connection_cancelled(hooks, phase))
			return IO_ERROR_CANCELLED;
		result = serial_port_read(port, &byte, 1, 100);
		if (result == IO_ERROR_TIMED_OUT || result == IO_ERROR_WOULD_BLOCK)
			continue;
		if (result <= 0)
			return result == 0 ? IO_ERROR_IO : (int)result;
		if (byte == '\r') {
			if (length == 0)
				continue;
			line[length] = '\0';
			return 0;
		}
		if (byte < ' ' && length == 0)
			continue;
		if (length + 1 >= capacity)
			return IO_ERROR_MESSAGE_TOO_LARGE;
		line[length++] = (char)byte;
	}
	return IO_ERROR_TIMED_OUT;
}

static int
wait_for_modem(serial_port_t *port, const char *command,
	const char *wanted, int64_t timeout_ms,
	const struct st_connection_hooks *hooks,
	enum st_connection_setup_phase phase)
{
	char line[1024];
	uint64_t deadline = serial_now_ms() + (uint64_t)timeout_ms;

	while (serial_now_ms() < deadline) {
		uint64_t remaining = deadline - serial_now_ms();
		int result = modem_line(port, line, sizeof(line),
		    (int64_t)remaining, hooks, phase);

		if (result != 0)
			return result;
		if (strstr(line, command) != NULL)
			continue;
		return strstr(line, wanted) != NULL ? 0 : IO_ERROR_PROTOCOL;
	}
	return IO_ERROR_TIMED_OUT;
}

static int
configure_port(serial_port_t *port, const struct st_profile *profile,
	uint32_t baud_rate, bool three_wire)
{
	struct serial_config config;

	config.baud_rate = baud_rate;
	config.data_bits = profile->data_bits != 0 ? profile->data_bits : 8;
	config.stop_bits = profile->stop_bits != 0 ? profile->stop_bits : 1;
	config.parity = profile_parity(profile->parity);
	config.flow_control = (unsigned)profile->flow_control;
	if (three_wire)
		config.flow_control &= ~SERIAL_FLOW_CONTROL_RTS_CTS;
	return serial_port_configure(port, &config);
}

static int
connect_serial_common(st_connection_t **result,
	const struct st_profile *profile, const struct st_connection_hooks *hooks,
	struct st_connection_error *error, bool modem)
{
	struct serial_transport *transport;
	serial_port_t *port;
	const char *device;
	bool three_wire;
	int status;

	if (result == NULL || profile == NULL)
		return st_connection_fail(error, IO_ERROR_INVALID_ARGUMENT,
		    ST_CONNECTION_SETUP_NONE, "invalid serial profile");
	three_wire = profile->connection_type == ST_CONNECTION_SERIAL_3WIRE;
	device = modem ? st_settings.modem_device : profile->address;
	if (device == NULL || device[0] == '\0')
		return st_connection_fail(error, IO_ERROR_INVALID_ARGUMENT,
		    ST_CONNECTION_SETUP_OPENING_DEVICE,
		    "no serial device is configured");
	if (st_connection_cancelled(hooks,
	    ST_CONNECTION_SETUP_OPENING_DEVICE))
		return st_connection_fail(error, IO_ERROR_CANCELLED,
		    ST_CONNECTION_SETUP_OPENING_DEVICE, "connection cancelled");
	status = serial_port_open(&port, device);
	if (status != 0)
		return st_connection_fail(error, status,
		    ST_CONNECTION_SETUP_OPENING_DEVICE,
		    "unable to open serial device");
	status = configure_port(port, profile,
	    modem ? st_settings.modem_rate : profile->bps_rate, three_wire);
	if (status == 0 && three_wire)
		status = serial_port_set_rts(port, false);
	if (status == 0)
		status = serial_port_set_dtr(port, true);
	if (status != 0) {
		(void)serial_port_close(port);
		return st_connection_fail(error, status,
		    ST_CONNECTION_SETUP_OPENING_DEVICE,
		    "unable to configure serial device");
	}
	if (modem) {
		uint8_t discarded[256];
		char dial[ST_SETTINGS_VALUE_MAX + ST_PROFILE_ADDRESS_MAX + 2];

		while (serial_port_read(port, discarded, sizeof(discarded), 0) > 0)
			;
		if (st_connection_cancelled(hooks,
		    ST_CONNECTION_SETUP_INITIALIZING_MODEM)) {
			(void)serial_port_set_dtr(port, false);
			(void)serial_port_close(port);
			return st_connection_fail(error, IO_ERROR_CANCELLED,
			    ST_CONNECTION_SETUP_INITIALIZING_MODEM,
			    "connection cancelled");
		}
		status = write_all(port, st_settings.modem_init_string,
		    strlen(st_settings.modem_init_string), hooks,
		    ST_CONNECTION_SETUP_INITIALIZING_MODEM);
		if (status == 0)
			status = write_all(port, "\r", 1, hooks,
			    ST_CONNECTION_SETUP_INITIALIZING_MODEM);
		if (status == 0)
			status = wait_for_modem(port, st_settings.modem_init_string,
			    "OK", 5000, hooks,
			    ST_CONNECTION_SETUP_INITIALIZING_MODEM);
		if (status != 0) {
			(void)serial_port_set_dtr(port, false);
			(void)serial_port_close(port);
			return st_connection_fail(error, status,
			    ST_CONNECTION_SETUP_INITIALIZING_MODEM,
			    "modem initialization failed");
		}
		if (snprintf(dial, sizeof(dial), "%s%s",
		    st_settings.modem_dial_string, profile->address)
		    >= (int)sizeof(dial)) {
			(void)serial_port_set_dtr(port, false);
			(void)serial_port_close(port);
			return st_connection_fail(error, IO_ERROR_NAME_TOO_LONG,
			    ST_CONNECTION_SETUP_DIALING, "dial command is too long");
		}
		status = write_all(port, dial, strlen(dial), hooks,
		    ST_CONNECTION_SETUP_DIALING);
		if (status == 0)
			status = write_all(port, "\r", 1, hooks,
			    ST_CONNECTION_SETUP_DIALING);
		if (status == 0)
			status = wait_for_modem(port, dial, "CONNECT", 60000,
			    hooks, ST_CONNECTION_SETUP_DIALING);
		memset(dial, 0, sizeof(dial));
		if (status != 0) {
			(void)serial_port_set_dtr(port, false);
			(void)serial_port_close(port);
			return st_connection_fail(error, status,
			    ST_CONNECTION_SETUP_DIALING, "modem connection failed");
		}
	}
	transport = calloc(1, sizeof(*transport));
	if (transport == NULL) {
		(void)serial_port_set_dtr(port, false);
		(void)serial_port_close(port);
		return st_connection_fail(error, IO_ERROR_OUT_OF_MEMORY,
		    ST_CONNECTION_SETUP_NONE, "unable to allocate serial transport");
	}
	transport->port = port;
	transport->seven_bits = profile->data_bits == 7;
	transport->modem = modem;
	transport->monitored_line = modem ? SERIAL_MODEM_LINE_DCD
	    : SERIAL_MODEM_LINE_DSR;
	if (modem)
		transport->monitor_line = true;
	else {
		unsigned lines;
		transport->monitor_line = serial_port_get_modem_lines(port,
		    &lines) == 0 && (lines & SERIAL_MODEM_LINE_DSR) != 0;
	}
	status = st_connection_start(result, transport, &serial_operations,
	    modem ? "Modem" : three_wire ? "Serial-3-Wire" : "Serial",
	    hooks, NULL, error);
	if (status == 0)
		st_connection_log(*result, ST_CONNECTION_LOG_INFO,
		    "session established");
	return status;
}

int
st_connection_connect_serial(st_connection_t **result,
	const struct st_profile *profile, const struct st_connection_hooks *hooks,
	struct st_connection_error *error)
{
	if (profile == NULL || (profile->connection_type != ST_CONNECTION_SERIAL
	    && profile->connection_type != ST_CONNECTION_SERIAL_3WIRE))
		return st_connection_fail(error, IO_ERROR_INVALID_ARGUMENT,
		    ST_CONNECTION_SETUP_NONE, "profile is not a serial connection");
	return connect_serial_common(result, profile, hooks, error, false);
}

int
st_connection_connect_modem(st_connection_t **result,
	const struct st_profile *profile, const struct st_connection_hooks *hooks,
	struct st_connection_error *error)
{
	if (profile == NULL
	    || profile->connection_type != ST_CONNECTION_MODEM)
		return st_connection_fail(error, IO_ERROR_INVALID_ARGUMENT,
		    ST_CONNECTION_SETUP_NONE, "profile is not a modem connection");
	return connect_serial_common(result, profile, hooks, error, true);
}
