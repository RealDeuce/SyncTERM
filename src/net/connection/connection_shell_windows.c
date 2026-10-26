/* Windows ConPTY shell transport. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "connection_internal.h"
#include "connection_shell_codec.h"

#include "io_error.h"
#include "st_windows_utf.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <bcrypt.h>

typedef HRESULT (WINAPI *create_pseudo_console_fn)(COORD, HANDLE, HANDLE,
	DWORD, HPCON *);
typedef HRESULT (WINAPI *resize_pseudo_console_fn)(HPCON, COORD);
typedef void (WINAPI *close_pseudo_console_fn)(HPCON);

struct shell_transport {
	struct st_connection_hooks hooks;
	resize_pseudo_console_fn resize_console;
	close_pseudo_console_fn close_console;
	HANDLE input_pipe;
	HANDLE output_pipe;
	HANDLE read_event;
	HANDLE write_event;
	HANDLE stop_event;
	HPCON console;
	HANDLE process;
	st_mutex_t console_mutex; /* Protects console ownership and resize. */
	bool console_mutex_initialized;
	st_thread_t lifecycle_thread;
	bool lifecycle_started;
	bool utf8;
	struct st_shell_codec codec;
};

/* The process handle and stop event outlive this thread. On natural exit,
 * close ConPTY while the input worker still drains its final output. On a
 * local stop, destruction closes the pipe endpoints before joining us, so
 * an older blocking ClosePseudoConsole cannot wait for an abandoned reader.
 * Only this thread or destruction (after joining it) closes the console. */
static int
shell_lifecycle(void *state)
{
	struct shell_transport *transport = state;
	HANDLE handles[2] = {transport->stop_event, transport->process};
	DWORD result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
	HPCON console = NULL;

	if (result == WAIT_OBJECT_0 + 1) {
		(void)st_mutex_lock(&transport->console_mutex);
		console = transport->console;
		transport->console = NULL;
		(void)st_mutex_unlock(&transport->console_mutex);
		transport->close_console(console);
	} else if (result == WAIT_FAILED) {
		st_connection_log_hooks(&transport->hooks, "Shell", ST_CONNECTION_LOG_ERROR,
		    "ConPTY process wait failed Windows error=%lu", (unsigned long)GetLastError());
		(void)SetEvent(transport->stop_event);
	}
	return 0;
}

static DWORD
shell_wait_timeout(int64_t timeout_ms)
{
	if (timeout_ms < 0)
		return INFINITE;
	if ((uint64_t)timeout_ms >= (uint64_t)INFINITE)
		return INFINITE - 1;
	return (DWORD)timeout_ms;
}

static ssize_t
shell_io_error(struct shell_transport *transport, DWORD error, bool reading)
{
	if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED
	    || error == ERROR_NO_DATA)
		return reading ? 0 : IO_ERROR_NOT_CONNECTED;
	st_connection_log_hooks(&transport->hooks, "Shell", ST_CONNECTION_LOG_ERROR,
	    "ConPTY %s failed Windows error=%lu", reading ? "read" : "write",
	    (unsigned long)error);
	return IO_ERROR_IO;
}

static ssize_t
shell_overlapped_io(struct shell_transport *transport, void *data,
	size_t size, int64_t timeout_ms, bool reading)
{
	OVERLAPPED overlapped = {0};
	HANDLE pipe = reading ? transport->output_pipe : transport->input_pipe;
	HANDLE handles[2];
	DWORD transferred = 0;
	DWORD requested;
	DWORD wait_result;
	DWORD error;
	bool stopped;
	bool timed_out;

	if (timeout_ms < -1)
		return IO_ERROR_INVALID_ARGUMENT;
	if (size == 0)
		return 0;
	if (WaitForSingleObject(transport->stop_event, 0) == WAIT_OBJECT_0)
		return IO_ERROR_NOT_CONNECTED;
	overlapped.hEvent = reading ? transport->read_event
	    : transport->write_event;
	if (!ResetEvent(overlapped.hEvent))
		return shell_io_error(transport, GetLastError(), reading);
	requested = size > UINT32_MAX ? UINT32_MAX : (DWORD)size;
	if (reading ? ReadFile(pipe, data, requested, &transferred,
	    &overlapped) : WriteFile(pipe, data, requested,
	    &transferred, &overlapped))
		return (ssize_t)transferred;
	error = GetLastError();
	if (error != ERROR_IO_PENDING)
		return shell_io_error(transport, error, reading);
	handles[0] = overlapped.hEvent;
	handles[1] = transport->stop_event;
	wait_result = WaitForMultipleObjects(2, handles, FALSE,
	    shell_wait_timeout(timeout_ms));
	if (wait_result == WAIT_FAILED)
		st_connection_log_hooks(&transport->hooks, "Shell", ST_CONNECTION_LOG_ERROR,
		    "ConPTY I/O wait failed Windows error=%lu", (unsigned long)GetLastError());
	if (wait_result == WAIT_OBJECT_0) {
		if (GetOverlappedResult(pipe, &overlapped,
		    &transferred, TRUE))
			return (ssize_t)transferred;
		return shell_io_error(transport, GetLastError(), reading);
	}
	stopped = wait_result == WAIT_OBJECT_0 + 1;
	timed_out = wait_result == WAIT_TIMEOUT;
	if (!CancelIoEx(pipe, &overlapped)
	    && GetLastError() != ERROR_NOT_FOUND) {
		error = GetLastError();
		(void)GetOverlappedResult(pipe, &overlapped,
		    &transferred, TRUE);
		return shell_io_error(transport, error, reading);
	}
	if (GetOverlappedResult(pipe, &overlapped,
	    &transferred, TRUE)) {
		if (stopped)
			return IO_ERROR_NOT_CONNECTED;
		return (ssize_t)transferred;
	}
	error = GetLastError();
	if (stopped)
		return IO_ERROR_NOT_CONNECTED;
	if (timed_out && error == ERROR_OPERATION_ABORTED)
		return timeout_ms == 0 ? IO_ERROR_WOULD_BLOCK
		    : IO_ERROR_TIMED_OUT;
	return shell_io_error(transport, error, reading);
}

static ssize_t
shell_read_utf8(void *state, void *data, size_t size,
	int64_t timeout_ms)
{
	struct shell_transport *transport = state;

	return shell_overlapped_io(transport, data, size, timeout_ms, true);
}

static ssize_t
shell_write_utf8(void *state, const void *data, size_t size,
	int64_t timeout_ms)
{
	struct shell_transport *transport = state;

	return shell_overlapped_io(transport, (void *)data, size, timeout_ms,
	    false);
}

static ssize_t
shell_read_transport(void *state, void *data, size_t size, int64_t timeout_ms)
{
	struct shell_transport *transport = state;
	return transport->utf8 ? shell_read_utf8(state, data, size, timeout_ms)
	    : st_shell_codec_read(&transport->codec, data, size, timeout_ms,
	    shell_read_utf8, state);
}

static ssize_t
shell_write_transport(void *state, const void *data, size_t size, int64_t timeout_ms)
{
	struct shell_transport *transport = state;
	return transport->utf8 ? shell_write_utf8(state, data, size, timeout_ms)
	    : st_shell_codec_write(&transport->codec, data, size, timeout_ms,
	    shell_write_utf8, state);
}

static void
shell_interrupt(void *state)
{
	struct shell_transport *transport = state;

	if (transport->stop_event != NULL) {
		st_connection_log_hooks(&transport->hooks, "Shell", ST_CONNECTION_LOG_DEBUG,
		    "requesting ConPTY shutdown");
		(void)SetEvent(transport->stop_event);
	}
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
	(void)st_mutex_lock(&transport->console_mutex);
	/* An exited shell has no remaining screen to resize. Its final output
	 * may still be draining before the connection reports EOF. */
	int status = transport->console == NULL ? 0
	    : SUCCEEDED(transport->resize_console(transport->console, size))
	    ? 0 : IO_ERROR_IO;
	(void)st_mutex_unlock(&transport->console_mutex);
	return status;
}

static void
shell_destroy(void *state)
{
	struct shell_transport *transport = state;

	if (transport->stop_event != NULL)
		(void)SetEvent(transport->stop_event);
	/* The connection layer has joined both workers before normal destruction.
	 * Closing the host endpoint first also prevents old ClosePseudoConsole
	 * implementations from waiting for output that nobody can drain. */
	if (transport->input_pipe != INVALID_HANDLE_VALUE) {
		(void)CloseHandle(transport->input_pipe);
		transport->input_pipe = INVALID_HANDLE_VALUE;
	}
	if (transport->output_pipe != INVALID_HANDLE_VALUE) {
		(void)CloseHandle(transport->output_pipe);
		transport->output_pipe = INVALID_HANDLE_VALUE;
	}
	if (transport->lifecycle_started)
		(void)st_thread_join(transport->lifecycle_thread, NULL);
	if (transport->console != NULL) {
		transport->close_console(transport->console);
		transport->console = NULL;
	}
	if (transport->process != NULL) {
		DWORD exit_code;
		if (WaitForSingleObject(transport->process, 10000)
		    == WAIT_TIMEOUT) {
			st_connection_log_hooks(&transport->hooks, "Shell", ST_CONNECTION_LOG_WARNING,
			    "shell did not exit after ConPTY close; terminating process");
			(void)TerminateProcess(transport->process, 1);
			(void)WaitForSingleObject(transport->process, INFINITE);
		}
		if (GetExitCodeProcess(transport->process, &exit_code)) {
			if (exit_code != STILL_ACTIVE)
				st_connection_log_hooks(&transport->hooks, "Shell", ST_CONNECTION_LOG_INFO,
				    "shell exited status=%lu", (unsigned long)exit_code);
			else
				st_connection_log_hooks(&transport->hooks, "Shell", ST_CONNECTION_LOG_WARNING,
				    "shell process still active after shutdown");
		} else
			st_connection_log_hooks(&transport->hooks, "Shell", ST_CONNECTION_LOG_ERROR,
			    "GetExitCodeProcess failed Windows error=%lu", (unsigned long)GetLastError());
		(void)CloseHandle(transport->process);
	}
	if (transport->read_event != NULL)
		(void)CloseHandle(transport->read_event);
	if (transport->write_event != NULL)
		(void)CloseHandle(transport->write_event);
	if (transport->stop_event != NULL)
		(void)CloseHandle(transport->stop_event);
	if (transport->console_mutex_initialized)
		st_mutex_destroy(&transport->console_mutex);
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

static int
make_pipe_name(wchar_t *name, size_t capacity)
{
	static const wchar_t prefix[] = L"\\\\.\\pipe\\SyncTERM-ConPTY-";
	static const wchar_t digits[] = L"0123456789abcdef";
	unsigned char random[16];
	size_t position = sizeof(prefix) / sizeof(prefix[0]) - 1;

	if (capacity < position + sizeof(random) * 2 + 1
	    || !BCRYPT_SUCCESS(BCryptGenRandom(NULL, random, sizeof(random),
	    BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
		return IO_ERROR_IO;
	memcpy(name, prefix, position * sizeof(*name));
	for (size_t i = 0; i < sizeof(random); ++i) {
		name[position++] = digits[random[i] >> 4];
		name[position++] = digits[random[i] & 0x0f];
	}
	name[position] = L'\0';
	return 0;
}

static int
create_conpty_pipe(HANDLE *host, HANDLE *client)
{
	wchar_t name[64];
	OVERLAPPED overlapped = {0};
	HANDLE event = NULL;
	HANDLE server = INVALID_HANDLE_VALUE;
	HANDLE conpty = INVALID_HANDLE_VALUE;
	DWORD transferred;
	DWORD connect_error = ERROR_SUCCESS;
	int status = IO_ERROR_IO;

	if (make_pipe_name(name, sizeof(name) / sizeof(name[0])) != 0)
		goto done;
	/* ConPTY receives the synchronous client; workers retain the cancellable
	 * overlapped server endpoint. */
	server = CreateNamedPipeW(name,
	    PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED
	    | FILE_FLAG_FIRST_PIPE_INSTANCE,
	    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT
	    | PIPE_REJECT_REMOTE_CLIENTS, 1, ST_CONNECTION_BUFFER_SIZE,
	    ST_CONNECTION_BUFFER_SIZE, 0, NULL);
	if (server == INVALID_HANDLE_VALUE)
		goto done;
	event = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (event == NULL)
		goto done;
	overlapped.hEvent = event;
	if (!ConnectNamedPipe(server, &overlapped)) {
		connect_error = GetLastError();
		if (connect_error != ERROR_IO_PENDING
		    && connect_error != ERROR_PIPE_CONNECTED)
			goto done;
	}
	conpty = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
	    OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
	    NULL);
	if (conpty == INVALID_HANDLE_VALUE)
		goto done;
	if (connect_error == ERROR_IO_PENDING
	    && !GetOverlappedResult(server, &overlapped, &transferred, TRUE))
		goto done;
	*host = server;
	*client = conpty;
	server = INVALID_HANDLE_VALUE;
	conpty = INVALID_HANDLE_VALUE;
	status = 0;

done:
	if (status != 0 && connect_error == ERROR_IO_PENDING
	    && server != INVALID_HANDLE_VALUE) {
		(void)CancelIoEx(server, &overlapped);
		(void)GetOverlappedResult(server, &overlapped, &transferred, TRUE);
	}
	if (conpty != INVALID_HANDLE_VALUE)
		(void)CloseHandle(conpty);
	if (server != INVALID_HANDLE_VALUE)
		(void)CloseHandle(server);
	if (event != NULL)
		(void)CloseHandle(event);
	return status;
}

int
st_connection_connect_shell(st_connection_t **result,
	const struct st_profile *profile,
	const struct st_screen_geometry *current_screen,
	const struct st_connection_shell_environment *shell_environment,
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
	HANDLE conpty_input = INVALID_HANDLE_VALUE;
	HANDLE conpty_output = INVALID_HANDLE_VALUE;
	SIZE_T attributes_size = 0;
	LPPROC_THREAD_ATTRIBUTE_LIST attributes = NULL;
	wchar_t *command = NULL;
	COORD size;
	COORD resize_probe;
	HMODULE kernel;
	bool attributes_initialized = false;
	HRESULT console_status;
	int status;

	(void)shell_environment;

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
	st_connection_log_hooks(hooks, "Shell", ST_CONNECTION_LOG_INFO,
	    "starting ConPTY shell (command and environment omitted)");
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
	transport = calloc(1, sizeof(*transport));
	if (transport == NULL)
		return st_connection_fail(error, IO_ERROR_OUT_OF_MEMORY,
		    ST_CONNECTION_SETUP_STARTING_SHELL,
		    "unable to allocate ConPTY shell state");
	transport->resize_console = resize_console;
	transport->utf8 = profile->encoding == ST_PROFILE_ENCODING_UTF8;
	transport->codec.codepage = (enum st_codepage)profile->encoding;
	if (hooks != NULL) transport->hooks = *hooks;
	transport->close_console = close_console;
	transport->input_pipe = INVALID_HANDLE_VALUE;
	transport->output_pipe = INVALID_HANDLE_VALUE;
	if (st_mutex_init(&transport->console_mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS)
		goto failed;
	transport->console_mutex_initialized = true;
	transport->read_event = CreateEventW(NULL, TRUE, FALSE, NULL);
	transport->write_event = CreateEventW(NULL, TRUE, FALSE, NULL);
	transport->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (transport->read_event == NULL || transport->write_event == NULL
	    || transport->stop_event == NULL
	    || create_conpty_pipe(&transport->input_pipe, &conpty_input) != 0
	    || create_conpty_pipe(&transport->output_pipe, &conpty_output) != 0)
		goto failed;
	size.X = (SHORT)geometry.columns;
	size.Y = (SHORT)geometry.rows;
	/* Synchronous I/O on one handle is serialized by Windows. Sharing a
	 * duplex handle here lets ConPTY's pending input read block its output. */
	console_status = create_console(size, conpty_input, conpty_output, 0,
	    &transport->console);
	if (FAILED(console_status)) {
		st_connection_log_hooks(hooks, "Shell", ST_CONNECTION_LOG_ERROR,
		    "CreatePseudoConsole failed HRESULT=0x%08lX", (unsigned long)console_status);
		goto failed;
	}
	/* Resize support is part of this provider's contract, not optional. */
	resize_probe = size;
	resize_probe.X += resize_probe.X < INT16_MAX ? 1 : -1;
	if (FAILED(resize_console(transport->console, resize_probe))
	    || FAILED(resize_console(transport->console, size)))
		goto unsupported;
	(void)InitializeProcThreadAttributeList(NULL, 1, 0, &attributes_size);
	attributes = malloc(attributes_size);
	if (attributes == NULL
	    || !InitializeProcThreadAttributeList(attributes, 1, 0,
	    &attributes_size))
		goto failed;
	attributes_initialized = true;
	if (!UpdateProcThreadAttribute(attributes, 0,
	    PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, transport->console,
	    sizeof(transport->console), NULL, NULL))
		goto failed;
	command = wide_copy(profile->address[0] != '\0'
	    ? profile->address : "cmd.exe");
	if (command == NULL)
		goto failed;
	memset(&startup, 0, sizeof(startup));
	memset(&process, 0, sizeof(process));
	startup.StartupInfo.cb = sizeof(startup);
	/* Null standard handles plus this flag force connection to ConPTY.
	 * Without it Windows can copy the parent's redirected standard handles
	 * even with bInheritHandles FALSE, bypassing our pipes entirely. */
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	startup.lpAttributeList = attributes;
	if (!CreateProcessW(NULL, command, NULL, NULL, FALSE,
	    EXTENDED_STARTUPINFO_PRESENT, NULL, NULL, &startup.StartupInfo,
	    &process)) {
		st_connection_log_hooks(hooks, "Shell", ST_CONNECTION_LOG_ERROR,
		    "CreateProcessW failed Windows error=%lu", (unsigned long)GetLastError());
		goto failed;
	}
	transport->process = process.hProcess;
	(void)CloseHandle(process.hThread);
	(void)CloseHandle(conpty_input);
	conpty_input = INVALID_HANDLE_VALUE;
	(void)CloseHandle(conpty_output);
	conpty_output = INVALID_HANDLE_VALUE;
	free(command);
	command = NULL;
	DeleteProcThreadAttributeList(attributes);
	attributes_initialized = false;
	free(attributes);
	attributes = NULL;
	if (st_thread_create(&transport->lifecycle_thread, shell_lifecycle,
	    transport) != ST_THREAD_SUCCESS)
		goto failed;
	transport->lifecycle_started = true;
	status = st_connection_start(result, transport, &shell_operations,
	    "Shell", hooks, NULL, error);
	if (status == 0)
		st_connection_log(*result, ST_CONNECTION_LOG_INFO,
		    "session established");
	return status;

unsupported:
	st_connection_log_hooks(hooks, "Shell", ST_CONNECTION_LOG_ERROR,
	    "ConPTY resize support is unavailable");
	if (conpty_input != INVALID_HANDLE_VALUE)
		(void)CloseHandle(conpty_input);
	if (conpty_output != INVALID_HANDLE_VALUE)
		(void)CloseHandle(conpty_output);
	shell_destroy(transport);
	return st_connection_fail(error, IO_ERROR_NOT_SUPPORTED,
	    ST_CONNECTION_SETUP_STARTING_SHELL,
	    "ConPTY resize support is not available on this Windows system");

failed:
	st_connection_log_hooks(hooks, "Shell", ST_CONNECTION_LOG_ERROR,
	    "ConPTY setup failed");
	free(command);
	if (attributes != NULL) {
		if (attributes_initialized)
			DeleteProcThreadAttributeList(attributes);
		free(attributes);
	}
	if (conpty_input != INVALID_HANDLE_VALUE)
		(void)CloseHandle(conpty_input);
	if (conpty_output != INVALID_HANDLE_VALUE)
		(void)CloseHandle(conpty_output);
	shell_destroy(transport);
	return st_connection_fail(error, IO_ERROR_IO,
	    ST_CONNECTION_SETUP_STARTING_SHELL,
	    "unable to start ConPTY shell process");
}
