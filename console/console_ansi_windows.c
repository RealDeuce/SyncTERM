/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_ansi_system.h"

#include "io_error.h"
#include "st_codepage.h"
#include "st_utf8.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

enum ansi_input_kind {
	ANSI_INPUT_NONE,
	ANSI_INPUT_CONSOLE,
	ANSI_INPUT_PIPE,
	ANSI_INPUT_FILE,
};

struct st_console_ansi_system {
	HANDLE input;
	HANDLE output;
	HANDLE stop_event;
	enum ansi_input_kind input_kind;
	DWORD input_mode;
	DWORD output_mode;
	UINT input_code_page;
	UINT output_code_page;
	CONSOLE_CURSOR_INFO cursor_info;
	int input_file_mode;
	int output_file_mode;
	unsigned char pending[4];
	size_t pending_length;
	size_t pending_offset;
	WORD pending_repetitions;
	WCHAR high_surrogate;
	WORD high_surrogate_repetitions;
	WCHAR deferred_character;
	WORD deferred_repetitions;
	bool have_deferred;
	bool utf8;
	bool restore_input;
	bool restore_output;
	bool restore_cursor;
};

static bool
valid_handle(HANDLE handle)
{
	return handle != NULL && handle != INVALID_HANDLE_VALUE;
}

static enum ansi_input_kind
classify_input(HANDLE handle, DWORD *mode)
{
	DWORD type;

	if (!valid_handle(handle))
		return ANSI_INPUT_NONE;
	if (GetConsoleMode(handle, mode))
		return ANSI_INPUT_CONSOLE;
	type = GetFileType(handle);
	if (type == FILE_TYPE_PIPE)
		return ANSI_INPUT_PIPE;
	if (type == FILE_TYPE_DISK)
		return ANSI_INPUT_FILE;
	return ANSI_INPUT_NONE;
}

static void
queue_scalar(struct st_console_ansi_system *system, uint32_t codepoint,
	WORD repetitions)
{
	if (system->utf8) {
		ptrdiff_t length = st_utf8_encode_one((char *)system->pending,
		    sizeof(system->pending), codepoint);

		if (length < 0)
			length = st_utf8_encode_one((char *)system->pending,
			    sizeof(system->pending), UINT32_C(0xfffd));
		system->pending_length = (size_t)length;
	}
	else {
		uint8_t encoded;

		if (!st_codepage_from_unicode(&encoded, ST_CODEPAGE_CP437,
		    codepoint))
			encoded = '?';
		system->pending[0] = encoded;
		system->pending_length = 1;
	}
	system->pending_offset = 0;
	system->pending_repetitions = repetitions == 0 ? 1 : repetitions;
}

static void
drain_pending(struct st_console_ansi_system *system, unsigned char *buffer,
	size_t size, size_t *received)
{
	while (*received < size && system->pending_repetitions != 0) {
		buffer[(*received)++] = system->pending[system->pending_offset++];
		if (system->pending_offset == system->pending_length) {
			system->pending_offset = 0;
			--system->pending_repetitions;
		}
	}
}

static void
queue_console_character(struct st_console_ansi_system *system,
	WCHAR character, WORD repetitions)
{
	if (character >= 0xd800 && character <= 0xdbff) {
		if (system->high_surrogate != 0)
			queue_scalar(system, UINT32_C(0xfffd),
			    system->high_surrogate_repetitions);
		system->high_surrogate = character;
		system->high_surrogate_repetitions = repetitions;
		return;
	}
	if (character >= 0xdc00 && character <= 0xdfff) {
		if (system->high_surrogate != 0) {
			uint32_t codepoint = UINT32_C(0x10000)
			    + (((uint32_t)system->high_surrogate - UINT32_C(0xd800))
			    << 10) + ((uint32_t)character - UINT32_C(0xdc00));

			system->high_surrogate = 0;
			system->high_surrogate_repetitions = 0;
			queue_scalar(system, codepoint, repetitions);
		}
		else
			queue_scalar(system, UINT32_C(0xfffd), repetitions);
		return;
	}
	if (system->high_surrogate != 0) {
		queue_scalar(system, UINT32_C(0xfffd),
		    system->high_surrogate_repetitions);
		system->high_surrogate = 0;
		system->high_surrogate_repetitions = 0;
		system->deferred_character = character;
		system->deferred_repetitions = repetitions;
		system->have_deferred = true;
		return;
	}
	queue_scalar(system, character, repetitions);
}

static void
finish_high_surrogate(struct st_console_ansi_system *system,
	unsigned char *buffer, size_t size, size_t *received)
{
	if (system->high_surrogate == 0)
		return;
	queue_scalar(system, UINT32_C(0xfffd),
	    system->high_surrogate_repetitions);
	system->high_surrogate = 0;
	system->high_surrogate_repetitions = 0;
	drain_pending(system, buffer, size, received);
}

static int
read_console(struct st_console_ansi_system *system, void *buffer, size_t size,
	size_t *received, unsigned int timeout_ms)
{
	unsigned char *bytes = buffer;
	HANDLE handles[2] = {system->stop_event, system->input};
	DWORD wait;

	drain_pending(system, bytes, size, received);
	if (*received == size)
		return 0;
	if (system->have_deferred) {
		queue_console_character(system, system->deferred_character,
		    system->deferred_repetitions);
		system->have_deferred = false;
		drain_pending(system, bytes, size, received);
		if (*received == size)
			return 0;
	}
	wait = WaitForMultipleObjects(2, handles, FALSE, timeout_ms);
	if (wait == WAIT_OBJECT_0)
		return IO_ERROR_CANCELLED;
	if (wait == WAIT_TIMEOUT) {
		finish_high_surrogate(system, bytes, size, received);
		return 0;
	}
	if (wait != WAIT_OBJECT_0 + 1) {
		finish_high_surrogate(system, bytes, size, received);
		return io_error_from_windows(GetLastError());
	}
	for (;;) {
		INPUT_RECORD record;
		DWORD count;
		DWORD available;

		if (!ReadConsoleInputW(system->input, &record, 1, &count)) {
			finish_high_surrogate(system, bytes, size, received);
			return io_error_from_windows(GetLastError());
		}
		if (count == 0) {
			finish_high_surrogate(system, bytes, size, received);
			return IO_ERROR_NOT_CONNECTED;
		}
		if (record.EventType == KEY_EVENT
		    && record.Event.KeyEvent.bKeyDown
		    && record.Event.KeyEvent.uChar.UnicodeChar != 0) {
			queue_console_character(system,
			    record.Event.KeyEvent.uChar.UnicodeChar,
			    record.Event.KeyEvent.wRepeatCount);
			drain_pending(system, bytes, size, received);
			if (*received == size || system->pending_repetitions != 0
			    || system->have_deferred)
				return 0;
		}
		if (!GetNumberOfConsoleInputEvents(system->input, &available)) {
			finish_high_surrogate(system, bytes, size, received);
			return io_error_from_windows(GetLastError());
		}
		if (available == 0)
			return 0;
		if (WaitForSingleObject(system->stop_event, 0) == WAIT_OBJECT_0)
			return IO_ERROR_CANCELLED;
	}
}

static int
read_pipe(struct st_console_ansi_system *system, void *buffer, size_t size,
	size_t *received, unsigned int timeout_ms)
{
	DWORD available;
	DWORD count;
	DWORD amount;
	DWORD error;

	if (WaitForSingleObject(system->stop_event, 0) == WAIT_OBJECT_0)
		return IO_ERROR_CANCELLED;
	/* Bound every pipe read so shutdown never depends on cancelling ReadFile. */
	if (!PeekNamedPipe(system->input, NULL, 0, NULL, &available, NULL)) {
		error = GetLastError();
		return error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED
		    ? IO_ERROR_NOT_CONNECTED : io_error_from_windows(error);
	}
	if (available == 0) {
		DWORD wait = WaitForSingleObject(system->stop_event, timeout_ms);

		if (wait == WAIT_OBJECT_0)
			return IO_ERROR_CANCELLED;
		if (wait != WAIT_TIMEOUT)
			return io_error_from_windows(GetLastError());
		if (!PeekNamedPipe(system->input, NULL, 0, NULL, &available, NULL)) {
			error = GetLastError();
			return error == ERROR_BROKEN_PIPE
			    || error == ERROR_PIPE_NOT_CONNECTED
			    ? IO_ERROR_NOT_CONNECTED : io_error_from_windows(error);
		}
		if (available == 0)
			return 0;
	}
	amount = size < available ? (DWORD)size : available;
	if (!ReadFile(system->input, buffer, amount, &count, NULL)) {
		error = GetLastError();
		if (error != ERROR_MORE_DATA || count == 0)
			return error == ERROR_BROKEN_PIPE
			    || error == ERROR_PIPE_NOT_CONNECTED
			    ? IO_ERROR_NOT_CONNECTED : io_error_from_windows(error);
	}
	if (count == 0)
		return IO_ERROR_NOT_CONNECTED;
	*received = count;
	return 0;
}

static int
read_file(struct st_console_ansi_system *system, void *buffer, size_t size,
	size_t *received)
{
	DWORD count;
	DWORD amount = size > MAXDWORD ? MAXDWORD : (DWORD)size;

	if (WaitForSingleObject(system->stop_event, 0) == WAIT_OBJECT_0)
		return IO_ERROR_CANCELLED;
	if (!ReadFile(system->input, buffer, amount, &count, NULL))
		return io_error_from_windows(GetLastError());
	if (count == 0)
		return IO_ERROR_NOT_CONNECTED;
	*received = count;
	return 0;
}

int
st_console_ansi_system_open(struct st_console_ansi_system **result,
	bool *input_available, bool utf8)
{
	struct st_console_ansi_system *system;
	intptr_t input_handle;
	intptr_t output_handle;
	int input_descriptor;
	int output_descriptor;
	DWORD mode;
	int error;

	if (result == NULL || input_available == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	system = calloc(1, sizeof(*system));
	if (system == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	system->input = INVALID_HANDLE_VALUE;
	system->output = INVALID_HANDLE_VALUE;
	system->input_file_mode = -1;
	system->output_file_mode = -1;
	system->utf8 = utf8;
	*input_available = false;
	input_descriptor = _fileno(stdin);
	output_descriptor = _fileno(stdout);
	if (input_descriptor < 0 || output_descriptor < 0) {
		error = IO_ERROR_INVALID_ARGUMENT;
		goto failed;
	}
	system->input_file_mode = _setmode(input_descriptor, _O_BINARY);
	if (system->input_file_mode < 0) {
		error = io_error_from_errno(errno);
		goto failed;
	}
	system->output_file_mode = _setmode(output_descriptor, _O_BINARY);
	if (system->output_file_mode < 0) {
		error = io_error_from_errno(errno);
		goto failed;
	}
	input_handle = _get_osfhandle(input_descriptor);
	output_handle = _get_osfhandle(output_descriptor);
	if (input_handle != -1)
		system->input = (HANDLE)input_handle;
	if (output_handle != -1)
		system->output = (HANDLE)output_handle;
	system->input_kind = classify_input(system->input, &system->input_mode);
	if (valid_handle(system->output)
	    && GetConsoleMode(system->output, &system->output_mode)) {
		system->restore_cursor = GetConsoleCursorInfo(system->output,
		    &system->cursor_info) != FALSE;
		system->output_code_page = GetConsoleOutputCP();
		if (system->output_code_page == 0) {
			error = IO_ERROR_IO;
			goto failed;
		}
		mode = system->output_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
		if (!SetConsoleMode(system->output, mode)) {
			error = IO_ERROR_NOT_SUPPORTED;
			goto failed;
		}
		system->restore_output = true;
		if (!SetConsoleOutputCP(utf8 ? CP_UTF8 : 437)) {
			error = io_error_from_windows(GetLastError());
			goto failed;
		}
	}
	if (system->input_kind == ANSI_INPUT_CONSOLE) {
		system->input_code_page = GetConsoleCP();
		if (system->input_code_page == 0) {
			error = IO_ERROR_IO;
			goto failed;
		}
		mode = system->input_mode | ENABLE_VIRTUAL_TERMINAL_INPUT;
		mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT
		    | ENABLE_PROCESSED_INPUT);
		if (!SetConsoleMode(system->input, mode)) {
			error = IO_ERROR_NOT_SUPPORTED;
			goto failed;
		}
		system->restore_input = true;
		if (!SetConsoleCP(utf8 ? CP_UTF8 : 437)) {
			error = io_error_from_windows(GetLastError());
			goto failed;
		}
	}
	if (system->input_kind != ANSI_INPUT_NONE) {
		system->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
		if (system->stop_event == NULL) {
			error = io_error_from_windows(GetLastError());
			goto failed;
		}
		*input_available = true;
	}
	*result = system;
	return 0;

failed:
	st_console_ansi_system_close(system);
	return error;
}

int
st_console_ansi_system_read(struct st_console_ansi_system *system,
	void *buffer, size_t size, size_t *received, unsigned int timeout_ms)
{
	if (system == NULL || buffer == NULL || size == 0 || received == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*received = 0;
	switch (system->input_kind) {
	case ANSI_INPUT_CONSOLE:
		return read_console(system, buffer, size, received, timeout_ms);
	case ANSI_INPUT_PIPE:
		return read_pipe(system, buffer, size, received, timeout_ms);
	case ANSI_INPUT_FILE:
		return read_file(system, buffer, size, received);
	case ANSI_INPUT_NONE:
		return IO_ERROR_NOT_CONNECTED;
	}
	return IO_ERROR_NOT_CONNECTED;
}

void
st_console_ansi_system_stop(struct st_console_ansi_system *system)
{
	if (system != NULL && system->stop_event != NULL)
		(void)SetEvent(system->stop_event);
}

bool
st_console_ansi_system_size(struct st_console_ansi_system *system,
	uint32_t *columns, uint32_t *rows)
{
	CONSOLE_SCREEN_BUFFER_INFO information;

	if (system == NULL || !GetConsoleScreenBufferInfo(system->output,
	    &information))
		return false;
	*columns = (uint32_t)(information.srWindow.Right
	    - information.srWindow.Left + 1);
	*rows = (uint32_t)(information.srWindow.Bottom
	    - information.srWindow.Top + 1);
	return *columns != 0 && *rows != 0;
}

void
st_console_ansi_system_close(struct st_console_ansi_system *system)
{
	if (system == NULL)
		return;
	if (system->stop_event != NULL)
		(void)CloseHandle(system->stop_event);
	if (system->restore_input) {
		(void)SetConsoleCP(system->input_code_page);
		(void)SetConsoleMode(system->input, system->input_mode);
	}
	if (system->restore_output) {
		(void)SetConsoleOutputCP(system->output_code_page);
		(void)SetConsoleMode(system->output, system->output_mode);
	}
	if (system->restore_cursor)
		(void)SetConsoleCursorInfo(system->output, &system->cursor_info);
	if (system->output_file_mode >= 0)
		(void)_setmode(_fileno(stdout), system->output_file_mode);
	if (system->input_file_mode >= 0)
		(void)_setmode(_fileno(stdin), system->input_file_mode);
	free(system);
}
