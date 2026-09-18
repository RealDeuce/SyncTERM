/* Windows ConPTY shell transport. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "connection_internal.h"

#include "io_error.h"
#include "st_windows_utf.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

typedef HRESULT (WINAPI *create_pseudo_console_fn)(COORD, HANDLE, HANDLE,
	DWORD, HPCON *);
typedef HRESULT (WINAPI *resize_pseudo_console_fn)(HPCON, COORD);
typedef void (WINAPI *close_pseudo_console_fn)(HPCON);

struct shell_transport {
	resize_pseudo_console_fn resize_console;
	close_pseudo_console_fn close_console;
	atomic_uintptr_t input;
	atomic_uintptr_t output;
	HPCON console;
	HANDLE process;
};

static ssize_t
shell_read_transport(void *state, void *data, size_t size,
	int64_t timeout_ms)
{
	struct shell_transport *transport = state;
	HANDLE output = (HANDLE)atomic_load(&transport->output);
	DWORD received;

	(void)timeout_ms;
	if (output == NULL)
		return IO_ERROR_NOT_CONNECTED;
	if (!ReadFile(output, data,
	    size > UINT32_MAX ? UINT32_MAX : (DWORD)size, &received, NULL)) {
		DWORD error = GetLastError();
		return error == ERROR_BROKEN_PIPE ? 0 : IO_ERROR_IO;
	}
	return (ssize_t)received;
}

static ssize_t
shell_write_transport(void *state, const void *data, size_t size,
	int64_t timeout_ms)
{
	struct shell_transport *transport = state;
	HANDLE input = (HANDLE)atomic_load(&transport->input);
	DWORD written;

	(void)timeout_ms;
	if (input == NULL)
		return IO_ERROR_NOT_CONNECTED;
	if (!WriteFile(input, data,
	    size > UINT32_MAX ? UINT32_MAX : (DWORD)size, &written, NULL))
		return IO_ERROR_IO;
	return (ssize_t)written;
}

static void
close_atomic_handle(atomic_uintptr_t *value)
{
	HANDLE handle = (HANDLE)atomic_exchange(value, (uintptr_t)NULL);

	if (handle != NULL)
		(void)CloseHandle(handle);
}

static void
shell_interrupt(void *state)
{
	struct shell_transport *transport = state;

	close_atomic_handle(&transport->input);
	close_atomic_handle(&transport->output);
	(void)TerminateProcess(transport->process, 1);
}

static int
shell_update_terminal_size(void *state,
	const struct st_screen_geometry *geometry)
{
	struct shell_transport *transport = state;
	COORD size;

	if (geometry->columns > INT16_MAX || geometry->rows > INT16_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	size.X = (SHORT)geometry->columns;
	size.Y = (SHORT)geometry->rows;
	return SUCCEEDED(transport->resize_console(transport->console, size))
	    ? 0 : IO_ERROR_IO;
}

static void
shell_destroy(void *state)
{
	struct shell_transport *transport = state;

	close_atomic_handle(&transport->input);
	close_atomic_handle(&transport->output);
	if (WaitForSingleObject(transport->process, 10000) == WAIT_TIMEOUT) {
		(void)TerminateProcess(transport->process, 1);
		(void)WaitForSingleObject(transport->process, INFINITE);
	}
	(void)CloseHandle(transport->process);
	transport->close_console(transport->console);
	free(transport);
}

static const struct st_connection_transport_ops shell_operations = {
	.bind = NULL,
	.read = shell_read_transport,
	.write = shell_write_transport,
	.interrupt = shell_interrupt,
	.update_terminal_size = shell_update_terminal_size,
	.destroy = shell_destroy,
};

static wchar_t *
wide_copy(const char *source)
{
	wchar_t *result = NULL;

	return st_windows_utf8_to_utf16(&result, source) == 0 ? result : NULL;
}

int
st_connection_connect_shell(st_connection_t **result,
	const struct st_profile *profile,
	const struct st_screen_geometry *current_screen,
	const struct st_connection_hooks *hooks,
	struct st_connection_error *error)
{
	struct st_screen_geometry geometry;
	struct shell_transport *transport = NULL;
	STARTUPINFOEXW startup;
	PROCESS_INFORMATION process;
	create_pseudo_console_fn create_console;
	resize_pseudo_console_fn resize_console;
	close_pseudo_console_fn close_console;
	HANDLE input_read = NULL;
	HANDLE input_write = NULL;
	HANDLE output_read = NULL;
	HANDLE output_write = NULL;
	SIZE_T attributes_size = 0;
	LPPROC_THREAD_ATTRIBUTE_LIST attributes = NULL;
	wchar_t *command = NULL;
	HPCON console = NULL;
	COORD size;
	COORD resize_probe;
	HMODULE kernel;
	bool attributes_initialized = false;
	int status;

	if (result == NULL || profile == NULL
	    || profile->connection_type != ST_CONNECTION_SHELL)
		return st_connection_fail(error, IO_ERROR_INVALID_ARGUMENT,
		    ST_CONNECTION_SETUP_NONE, "profile is not a shell connection");
	status = st_connection_profile_geometry(profile, current_screen,
	    &geometry);
	if (status != 0 || geometry.columns > INT16_MAX
	    || geometry.rows > INT16_MAX)
		return st_connection_fail(error,
		    status != 0 ? status : IO_ERROR_OUT_OF_RANGE,
		    ST_CONNECTION_SETUP_STARTING_SHELL,
		    "invalid initial terminal geometry");
	if (st_connection_cancelled(hooks,
	    ST_CONNECTION_SETUP_STARTING_SHELL))
		return st_connection_fail(error, IO_ERROR_CANCELLED,
		    ST_CONNECTION_SETUP_STARTING_SHELL, "connection cancelled");
	kernel = GetModuleHandleW(L"kernel32.dll");
	if (kernel == NULL)
		return st_connection_fail(error, IO_ERROR_NOT_SUPPORTED,
		    ST_CONNECTION_SETUP_STARTING_SHELL,
		    "ConPTY is not available on this Windows system");
	create_console = (create_pseudo_console_fn)GetProcAddress(kernel,
	    "CreatePseudoConsole");
	resize_console = (resize_pseudo_console_fn)GetProcAddress(kernel,
	    "ResizePseudoConsole");
	close_console = (close_pseudo_console_fn)GetProcAddress(kernel,
	    "ClosePseudoConsole");
	if (create_console == NULL || resize_console == NULL
	    || close_console == NULL)
		return st_connection_fail(error, IO_ERROR_NOT_SUPPORTED,
		    ST_CONNECTION_SETUP_STARTING_SHELL,
		    "ConPTY is not available on this Windows system");
	if (!CreatePipe(&input_read, &input_write, NULL, 0)
	    || !CreatePipe(&output_read, &output_write, NULL, 0))
		goto failed;
	size.X = (SHORT)geometry.columns;
	size.Y = (SHORT)geometry.rows;
	if (FAILED(create_console(size, input_read, output_write, 0, &console)))
		goto failed;
	/* Resize support is part of this provider's contract, not optional. */
	resize_probe = size;
	resize_probe.X += resize_probe.X < INT16_MAX ? 1 : -1;
	if (FAILED(resize_console(console, resize_probe))
	    || FAILED(resize_console(console, size)))
		goto unsupported;
	(void)CloseHandle(input_read);
	input_read = NULL;
	(void)CloseHandle(output_write);
	output_write = NULL;
	(void)InitializeProcThreadAttributeList(NULL, 1, 0, &attributes_size);
	attributes = malloc(attributes_size);
	if (attributes == NULL
	    || !InitializeProcThreadAttributeList(attributes, 1, 0,
	    &attributes_size))
		goto failed;
	attributes_initialized = true;
	if (!UpdateProcThreadAttribute(attributes, 0,
	    PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, console, sizeof(console),
	    NULL, NULL))
		goto failed;
	command = wide_copy(profile->address[0] != '\0'
	    ? profile->address : "cmd.exe");
	if (command == NULL)
		goto failed;
	memset(&startup, 0, sizeof(startup));
	memset(&process, 0, sizeof(process));
	startup.StartupInfo.cb = sizeof(startup);
	startup.lpAttributeList = attributes;
	if (!CreateProcessW(NULL, command, NULL, NULL, FALSE,
	    EXTENDED_STARTUPINFO_PRESENT, NULL, NULL, &startup.StartupInfo,
	    &process))
		goto failed;
	(void)CloseHandle(process.hThread);
	free(command);
	DeleteProcThreadAttributeList(attributes);
	free(attributes);
	transport = calloc(1, sizeof(*transport));
	if (transport == NULL) {
		(void)TerminateProcess(process.hProcess, 1);
		(void)CloseHandle(process.hProcess);
		goto failed_console;
	}
	transport->resize_console = resize_console;
	transport->close_console = close_console;
	transport->console = console;
	transport->process = process.hProcess;
	atomic_init(&transport->input, (uintptr_t)input_write);
	atomic_init(&transport->output, (uintptr_t)output_read);
	status = st_connection_start(result, transport, &shell_operations,
	    "Shell", hooks, NULL, error);
	if (status == 0)
		st_connection_log(*result, ST_CONNECTION_LOG_INFO,
		    "session established");
	return status;

unsupported:
	if (console != NULL)
		close_console(console);
	if (input_read != NULL)
		(void)CloseHandle(input_read);
	if (input_write != NULL)
		(void)CloseHandle(input_write);
	if (output_read != NULL)
		(void)CloseHandle(output_read);
	if (output_write != NULL)
		(void)CloseHandle(output_write);
	return st_connection_fail(error, IO_ERROR_NOT_SUPPORTED,
	    ST_CONNECTION_SETUP_STARTING_SHELL,
	    "ConPTY resize support is not available on this Windows system");

failed:
	free(command);
	if (attributes != NULL) {
		if (attributes_initialized)
			DeleteProcThreadAttributeList(attributes);
		free(attributes);
	}
failed_console:
	if (console != NULL)
		close_console(console);
	if (input_read != NULL)
		(void)CloseHandle(input_read);
	if (input_write != NULL)
		(void)CloseHandle(input_write);
	if (output_read != NULL)
		(void)CloseHandle(output_read);
	if (output_write != NULL)
		(void)CloseHandle(output_write);
	return st_connection_fail(error, IO_ERROR_IO,
	    ST_CONNECTION_SETUP_STARTING_SHELL,
	    "unable to start ConPTY shell process");
}
