/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "serial.h"

#include "st_time.h"
#include "st_windows_utf.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

struct serial_port {
	HANDLE handle;
};

static int
serial_windows_error(DWORD error)
{
	if (error == ERROR_DEV_NOT_EXIST)
		return IO_ERROR_NOT_FOUND;
	return io_error_from_windows(error);
}

static int
serial_device_name(const char *device, wchar_t **result)
{
	static const wchar_t prefix[] = L"\\\\.\\";
	wchar_t *wide;
	wchar_t *converted;
	size_t prefix_length = sizeof(prefix) / sizeof(prefix[0]) - 1;
	size_t length;
	int status;

	status = st_windows_utf8_to_utf16(&wide, device);
	if (status < 0)
		return status;
	if (wide[0] == L'\\' && wide[1] == L'\\') {
		*result = wide;
		return 0;
	}
	length = wcslen(wide) + 1;
	if (length > SIZE_MAX / sizeof(*converted) - prefix_length) {
		free(wide);
		return IO_ERROR_OUT_OF_RANGE;
	}
	converted = malloc((prefix_length + length) * sizeof(*converted));
	if (converted == NULL) {
		free(wide);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	memcpy(converted, prefix, prefix_length * sizeof(*converted));
	memcpy(converted + prefix_length, wide, length * sizeof(*converted));
	free(wide);
	*result = converted;
	return 0;
}

int
serial_port_open(serial_port_t **result, const char *device)
{
	serial_port_t *port;
	wchar_t *name;
	HANDLE handle;
	DCB state = {0};
	COMMTIMEOUTS timeouts = {0};
	int error;

	if (result == NULL || device == NULL || *device == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	error = serial_device_name(device, &name);
	if (error != 0)
		return error;
	handle = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
	    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
	free(name);
	if (handle == INVALID_HANDLE_VALUE)
		return serial_windows_error(GetLastError());
	state.DCBlength = sizeof(state);
	if (!GetCommState(handle, &state)) {
		error = serial_windows_error(GetLastError());
		(void)CloseHandle(handle);
		return error;
	}
	/* Make the actual read nonblocking; WaitCommEvent supplies the timeout. */
	timeouts.ReadIntervalTimeout = MAXDWORD;
	if (!SetCommTimeouts(handle, &timeouts) ||
	    !SetCommMask(handle, EV_RXCHAR)) {
		error = serial_windows_error(GetLastError());
		(void)CloseHandle(handle);
		return error;
	}
	port = malloc(sizeof(*port));
	if (port == NULL) {
		(void)CloseHandle(handle);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	port->handle = handle;
	*result = port;
	return 0;
}

int
serial_port_close(serial_port_t *port)
{
	int result;

	if (port == NULL)
		return 0;
	(void)PurgeComm(port->handle, PURGE_RXABORT | PURGE_RXCLEAR |
	    PURGE_TXABORT | PURGE_TXCLEAR);
	result = CloseHandle(port->handle) ? 0 :
	    serial_windows_error(GetLastError());
	free(port);
	return result;
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
	return 0;
}

int
serial_port_configure(serial_port_t *port,
	const struct serial_config *config)
{
	DCB state = {0};
	int result;

	if (port == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = serial_config_valid(config);
	if (result != 0)
		return result;
	state.DCBlength = sizeof(state);
	if (!GetCommState(port->handle, &state))
		return serial_windows_error(GetLastError());
	if (config->baud_rate != 0)
		state.BaudRate = config->baud_rate;
	state.ByteSize = (BYTE)config->data_bits;
	state.StopBits = config->stop_bits == 2 ? TWOSTOPBITS : ONESTOPBIT;
	state.fBinary = TRUE;
	state.fParity = config->parity != SERIAL_PARITY_NONE;
	state.Parity = config->parity == SERIAL_PARITY_ODD ? ODDPARITY :
	    config->parity == SERIAL_PARITY_EVEN ? EVENPARITY : NOPARITY;
	state.fOutxCtsFlow = (config->flow_control &
	    SERIAL_FLOW_CONTROL_RTS_CTS) != 0;
	state.fRtsControl = state.fOutxCtsFlow ? RTS_CONTROL_HANDSHAKE :
	    RTS_CONTROL_DISABLE;
	state.fOutxDsrFlow = (config->flow_control &
	    SERIAL_FLOW_CONTROL_DTR_DSR) != 0;
	state.fDsrSensitivity = state.fOutxDsrFlow;
	state.fDtrControl = state.fOutxDsrFlow ? DTR_CONTROL_HANDSHAKE :
	    DTR_CONTROL_DISABLE;
	state.fInX = (config->flow_control & SERIAL_FLOW_CONTROL_XON_XOFF) != 0;
	state.fOutX = state.fInX;
	if (!SetCommState(port->handle, &state)) {
		DWORD error = GetLastError();
		return error == ERROR_INVALID_PARAMETER ? IO_ERROR_NOT_SUPPORTED :
		    serial_windows_error(error);
	}
	return 0;
}

static int
serial_wait_timeout(const struct st_deadline *deadline, DWORD *result)
{
	int64_t remaining;
	int status;

	status = st_deadline_remaining_ms(deadline, &remaining);
	if (status != 0)
		return status;
	if (remaining < 0)
		*result = INFINITE;
	else
		*result = (uint64_t)remaining >= INFINITE ? INFINITE - 1
		    : (DWORD)remaining;
	return 0;
}

static int
serial_overlapped_wait(HANDLE handle, OVERLAPPED *operation,
	const struct st_deadline *deadline, DWORD *transferred)
{
	int64_t remaining;
	DWORD timeout;
	DWORD wait_result;
	int status;

	for (;;) {
		status = serial_wait_timeout(deadline, &timeout);
		if (status != 0)
			return status;
		wait_result = WaitForSingleObject(operation->hEvent, timeout);
		if (wait_result == WAIT_OBJECT_0) {
			if (GetOverlappedResult(handle, operation, transferred, FALSE))
				return 0;
			return serial_windows_error(GetLastError());
		}
		if (wait_result == WAIT_TIMEOUT) {
			status = st_deadline_remaining_ms(deadline, &remaining);
			if (status != 0)
				return status;
			if (remaining != 0)
				continue;
			(void)CancelIo(handle);
			if (GetOverlappedResult(handle, operation, transferred, TRUE))
				return 0;
			if (GetLastError() == ERROR_OPERATION_ABORTED)
				return IO_ERROR_TIMED_OUT;
			return serial_windows_error(GetLastError());
		}
		{
			DWORD error = wait_result == WAIT_FAILED ? GetLastError() :
			    ERROR_GEN_FAILURE;
			(void)CancelIo(handle);
			(void)GetOverlappedResult(handle, operation, transferred, TRUE);
			return serial_windows_error(error);
		}
	}
}

static int
serial_read_now(serial_port_t *port, void *buffer, DWORD size,
	DWORD *transferred)
{
	OVERLAPPED operation = {0};
	struct st_deadline deadline;
	BOOL started;
	int result;

	operation.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (operation.hEvent == NULL)
		return serial_windows_error(GetLastError());
	result = st_deadline_start(&deadline, 0);
	if (result != 0) {
		(void)CloseHandle(operation.hEvent);
		return result;
	}
	started = ReadFile(port->handle, buffer, size, transferred, &operation);
	if (!started && GetLastError() == ERROR_IO_PENDING)
		result = serial_overlapped_wait(port->handle, &operation,
		    &deadline, transferred);
	else if (!started)
		result = serial_windows_error(GetLastError());
	else
		result = 0;
	(void)CloseHandle(operation.hEvent);
	return result;
}

ssize_t
serial_port_read(serial_port_t *port, void *buffer, size_t size,
	int64_t timeout_ms)
{
	struct st_deadline deadline;
	DWORD transferred;
	int result;

	if (port == NULL || (buffer == NULL && size != 0) || timeout_ms < -1)
		return IO_ERROR_INVALID_ARGUMENT;
	if (size == 0)
		return 0;
	if (size > MAXDWORD)
		size = MAXDWORD;
	result = st_deadline_start(&deadline, timeout_ms);
	if (result != 0)
		return result;
	for (;;) {
		DWORD event_mask = 0;
		OVERLAPPED operation = {0};
		BOOL started;

		transferred = 0;
		result = serial_read_now(port, buffer, (DWORD)size, &transferred);
		if (result == 0 && transferred != 0)
			return (ssize_t)transferred;
		if (result != 0 && result != IO_ERROR_TIMED_OUT)
			return result;
		{
			int64_t remaining;

			result = st_deadline_remaining_ms(&deadline, &remaining);
			if (result != 0)
				return result;
			if (remaining == 0)
				return IO_ERROR_TIMED_OUT;
		}
		operation.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
		if (operation.hEvent == NULL)
			return serial_windows_error(GetLastError());
		started = WaitCommEvent(port->handle, &event_mask, &operation);
		if (!started && GetLastError() == ERROR_IO_PENDING)
			result = serial_overlapped_wait(port->handle, &operation,
			    &deadline, &transferred);
		else if (!started)
			result = serial_windows_error(GetLastError());
		else
			result = 0;
		(void)CloseHandle(operation.hEvent);
		if (result != 0)
			return result;
	}
}

ssize_t
serial_port_write(serial_port_t *port, const void *buffer, size_t size,
	int64_t timeout_ms)
{
	OVERLAPPED operation = {0};
	struct st_deadline deadline;
	DWORD transferred = 0;
	BOOL started;
	int result = 0;

	if (port == NULL || (buffer == NULL && size != 0) || timeout_ms < -1)
		return IO_ERROR_INVALID_ARGUMENT;
	if (size == 0)
		return 0;
	if (size > MAXDWORD)
		size = MAXDWORD;
	operation.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (operation.hEvent == NULL)
		return serial_windows_error(GetLastError());
	result = st_deadline_start(&deadline, timeout_ms);
	if (result != 0) {
		(void)CloseHandle(operation.hEvent);
		return result;
	}
	started = WriteFile(port->handle, buffer, (DWORD)size, &transferred,
	    &operation);
	if (!started && GetLastError() == ERROR_IO_PENDING)
		result = serial_overlapped_wait(port->handle, &operation,
		    &deadline, &transferred);
	else if (!started)
		result = serial_windows_error(GetLastError());
	(void)CloseHandle(operation.hEvent);
	return result == 0 ? (ssize_t)transferred : result;
}

int
serial_port_get_modem_lines(serial_port_t *port, unsigned *lines)
{
	DWORD status;
	unsigned result = 0;

	if (port == NULL || lines == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!GetCommModemStatus(port->handle, &status))
		return serial_windows_error(GetLastError());
	if ((status & MS_CTS_ON) != 0)
		result |= SERIAL_MODEM_LINE_CTS;
	if ((status & MS_DSR_ON) != 0)
		result |= SERIAL_MODEM_LINE_DSR;
	if ((status & MS_RING_ON) != 0)
		result |= SERIAL_MODEM_LINE_RING;
	if ((status & MS_RLSD_ON) != 0)
		result |= SERIAL_MODEM_LINE_DCD;
	*lines = result;
	return 0;
}

static int
serial_set_modem_line(serial_port_t *port, DWORD enabled_function,
	DWORD disabled_function, bool enabled)
{
	if (port == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (EscapeCommFunction(port->handle,
	    enabled ? enabled_function : disabled_function))
		return 0;
	return serial_windows_error(GetLastError());
}

int
serial_port_set_dtr(serial_port_t *port, bool enabled)
{
	return serial_set_modem_line(port, SETDTR, CLRDTR, enabled);
}

int
serial_port_set_rts(serial_port_t *port, bool enabled)
{
	return serial_set_modem_line(port, SETRTS, CLRRTS, enabled);
}

int
serial_port_get_baud_rates(serial_port_t *port, uint32_t *rates,
	size_t capacity, size_t *count)
{
	static const struct {
		DWORD flag;
		uint32_t rate;
	} baud_rates[] = {
		{BAUD_075, 75}, {BAUD_110, 110}, {BAUD_134_5, 134},
		{BAUD_150, 150}, {BAUD_300, 300}, {BAUD_600, 600},
		{BAUD_1200, 1200}, {BAUD_1800, 1800}, {BAUD_2400, 2400},
		{BAUD_4800, 4800}, {BAUD_7200, 7200}, {BAUD_9600, 9600},
		{BAUD_14400, 14400}, {BAUD_19200, 19200},
		{BAUD_38400, 38400}, {BAUD_56K, 56000},
		{BAUD_57600, 57600}, {BAUD_115200, 115200},
		{BAUD_128K, 128000},
	};
	COMMPROP properties = {0};
	size_t available = 0;
	size_t i;

	if (port == NULL || count == NULL || (rates == NULL && capacity != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (!GetCommProperties(port->handle, &properties))
		return serial_windows_error(GetLastError());
	for (i = 0; i < sizeof(baud_rates) / sizeof(baud_rates[0]); i++) {
		if ((properties.dwSettableBaud & baud_rates[i].flag) == 0)
			continue;
		if (available < capacity)
			rates[available] = baud_rates[i].rate;
		available++;
	}
	*count = available;
	return 0;
}
