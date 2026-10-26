/* Deterministic serial-port implementation for connection provider tests. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_connection_serial_fake.h"

#include "io_error.h"
#include "st_threads.h"

#include <stdint.h>
#include <string.h>
#include <time.h>

struct serial_port {
	int unused;
};

static struct {
	st_mutex_t mutex;
	st_condition_t changed;
	struct serial_port port;
	struct serial_config config;
	uint8_t input[4096];
	uint8_t output[4096];
	size_t input_count;
	size_t output_count;
	size_t open_attempts;
	unsigned lines;
	bool initialized;
	bool open;
	bool dtr;
	bool rts;
} fake;

static struct timespec
fake_deadline(int64_t milliseconds)
{
	struct timespec result;

	(void)timespec_get(&result, TIME_UTC);
	result.tv_sec += (time_t)(milliseconds / 1000);
	result.tv_nsec += (long)(milliseconds % 1000) * 1000000L;
	if (result.tv_nsec >= 1000000000L) {
		++result.tv_sec;
		result.tv_nsec -= 1000000000L;
	}
	return result;
}

static void
append_input_locked(const void *data, size_t size)
{
	if (size > sizeof(fake.input) - fake.input_count)
		size = sizeof(fake.input) - fake.input_count;
	memcpy(fake.input + fake.input_count, data, size);
	fake.input_count += size;
	(void)st_condition_broadcast(&fake.changed);
}

void
st_connection_serial_fake_reset(unsigned lines)
{
	if (!fake.initialized) {
		(void)st_mutex_init(&fake.mutex, ST_MUTEX_PLAIN);
		(void)st_condition_init(&fake.changed);
		fake.initialized = true;
	}
	(void)st_mutex_lock(&fake.mutex);
	fake.config = (struct serial_config){0};
	fake.input_count = 0;
	fake.output_count = 0;
	fake.open_attempts = 0;
	fake.lines = lines;
	fake.open = false;
	fake.dtr = false;
	fake.rts = true;
	(void)st_mutex_unlock(&fake.mutex);
}

void
st_connection_serial_fake_dispose(void)
{
	if (!fake.initialized)
		return;
	st_condition_destroy(&fake.changed);
	st_mutex_destroy(&fake.mutex);
	fake.initialized = false;
}

void
st_connection_serial_fake_input(const void *data, size_t size)
{
	(void)st_mutex_lock(&fake.mutex);
	append_input_locked(data, size);
	(void)st_mutex_unlock(&fake.mutex);
}

void
st_connection_serial_fake_set_lines(unsigned lines)
{
	(void)st_mutex_lock(&fake.mutex);
	fake.lines = lines;
	(void)st_condition_broadcast(&fake.changed);
	(void)st_mutex_unlock(&fake.mutex);
}

size_t
st_connection_serial_fake_output(void *data, size_t capacity)
{
	size_t count;

	(void)st_mutex_lock(&fake.mutex);
	count = fake.output_count < capacity ? fake.output_count : capacity;
	memcpy(data, fake.output, count);
	(void)st_mutex_unlock(&fake.mutex);
	return count;
}

struct serial_config
st_connection_serial_fake_config(void)
{
	struct serial_config result;

	(void)st_mutex_lock(&fake.mutex);
	result = fake.config;
	(void)st_mutex_unlock(&fake.mutex);
	return result;
}

bool
st_connection_serial_fake_open(void)
{
	bool result;

	(void)st_mutex_lock(&fake.mutex);
	result = fake.open;
	(void)st_mutex_unlock(&fake.mutex);
	return result;
}

size_t
st_connection_serial_fake_open_attempts(void)
{
	size_t result;

	(void)st_mutex_lock(&fake.mutex);
	result = fake.open_attempts;
	(void)st_mutex_unlock(&fake.mutex);
	return result;
}

bool
st_connection_serial_fake_dtr(void)
{
	bool result;

	(void)st_mutex_lock(&fake.mutex);
	result = fake.dtr;
	(void)st_mutex_unlock(&fake.mutex);
	return result;
}

bool
st_connection_serial_fake_rts(void)
{
	bool result;

	(void)st_mutex_lock(&fake.mutex);
	result = fake.rts;
	(void)st_mutex_unlock(&fake.mutex);
	return result;
}

int
serial_port_open(serial_port_t **result, const char *device)
{
	if (result == NULL || device == NULL || device[0] == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	(void)st_mutex_lock(&fake.mutex);
	++fake.open_attempts;
	if (fake.open) {
		(void)st_mutex_unlock(&fake.mutex);
		return IO_ERROR_BUSY;
	}
	fake.open = true;
	*result = &fake.port;
	(void)st_mutex_unlock(&fake.mutex);
	return 0;
}

int
serial_port_close(serial_port_t *port)
{
	if (port == NULL)
		return 0;
	(void)st_mutex_lock(&fake.mutex);
	fake.open = false;
	(void)st_condition_broadcast(&fake.changed);
	(void)st_mutex_unlock(&fake.mutex);
	return 0;
}

int
serial_port_configure(serial_port_t *port, const struct serial_config *config)
{
	if (port == NULL || config == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	(void)st_mutex_lock(&fake.mutex);
	fake.config = *config;
	(void)st_mutex_unlock(&fake.mutex);
	return 0;
}

ssize_t
serial_port_read(serial_port_t *port, void *buffer, size_t size,
	int64_t timeout_ms)
{
	struct timespec deadline;
	size_t count;
	int result;

	if (port == NULL || (buffer == NULL && size != 0) || timeout_ms < -1)
		return IO_ERROR_INVALID_ARGUMENT;
	if (size == 0)
		return 0;
	if (timeout_ms > 0)
		deadline = fake_deadline(timeout_ms);
	(void)st_mutex_lock(&fake.mutex);
	while (fake.input_count == 0 && fake.open) {
		if (timeout_ms == 0) {
			(void)st_mutex_unlock(&fake.mutex);
			return IO_ERROR_TIMED_OUT;
		}
		result = timeout_ms < 0
		    ? st_condition_wait(&fake.changed, &fake.mutex)
		    : st_condition_timedwait(&fake.changed, &fake.mutex,
		    &deadline);
		if (result == ST_THREAD_TIMED_OUT) {
			(void)st_mutex_unlock(&fake.mutex);
			return IO_ERROR_TIMED_OUT;
		}
	}
	if (!fake.open) {
		(void)st_mutex_unlock(&fake.mutex);
		return IO_ERROR_NOT_CONNECTED;
	}
	count = fake.input_count < size ? fake.input_count : size;
	memcpy(buffer, fake.input, count);
	memmove(fake.input, fake.input + count, fake.input_count - count);
	fake.input_count -= count;
	(void)st_mutex_unlock(&fake.mutex);
	return (ssize_t)count;
}

ssize_t
serial_port_write(serial_port_t *port, const void *buffer, size_t size,
	int64_t timeout_ms)
{
	const uint8_t *bytes = buffer;
	size_t count;

	if (port == NULL || (buffer == NULL && size != 0) || timeout_ms < -1)
		return IO_ERROR_INVALID_ARGUMENT;
	(void)st_mutex_lock(&fake.mutex);
	if (!fake.open) {
		(void)st_mutex_unlock(&fake.mutex);
		return IO_ERROR_NOT_CONNECTED;
	}
	count = size < sizeof(fake.output) - fake.output_count
	    ? size : sizeof(fake.output) - fake.output_count;
	memcpy(fake.output + fake.output_count, buffer, count);
	fake.output_count += count;
	if (fake.output_count >= 4
	    && memcmp(fake.output + fake.output_count - 4, "ATZ\r", 4) == 0)
		append_input_locked("ATZ\r\nOK\r", 8);
	if (fake.output_count >= 8
	    && memcmp(fake.output + fake.output_count - 8, "ATDT555\r", 8) == 0)
		append_input_locked("ATDT555\r\nCONNECT\r", 17);
	(void)bytes;
	(void)st_mutex_unlock(&fake.mutex);
	return (ssize_t)count;
}

int
serial_port_get_modem_lines(serial_port_t *port, unsigned *lines)
{
	if (port == NULL || lines == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	(void)st_mutex_lock(&fake.mutex);
	*lines = fake.lines;
	(void)st_mutex_unlock(&fake.mutex);
	return 0;
}

int
serial_port_set_dtr(serial_port_t *port, bool enabled)
{
	if (port == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	(void)st_mutex_lock(&fake.mutex);
	fake.dtr = enabled;
	if (!enabled)
		fake.lines &= ~SERIAL_MODEM_LINE_DCD;
	(void)st_mutex_unlock(&fake.mutex);
	return 0;
}

int
serial_port_set_rts(serial_port_t *port, bool enabled)
{
	if (port == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	(void)st_mutex_lock(&fake.mutex);
	fake.rts = enabled;
	(void)st_mutex_unlock(&fake.mutex);
	return 0;
}

int
serial_port_get_baud_rates(serial_port_t *port, uint32_t *rates,
	size_t capacity, size_t *count)
{
	(void)port;
	(void)rates;
	(void)capacity;
	(void)count;
	return IO_ERROR_NOT_SUPPORTED;
}
