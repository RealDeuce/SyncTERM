/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_ansi_system.h"

#include "io_error.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

struct st_console_ansi_system {
	HANDLE input;
	HANDLE output;
	DWORD input_mode;
	DWORD output_mode;
	UINT input_code_page;
	UINT output_code_page;
	CONSOLE_CURSOR_INFO cursor_info;
	int input_file_mode;
	int output_file_mode;
	bool restore_input;
	bool restore_output;
	bool restore_cursor;
};

int
st_console_ansi_system_open(struct st_console_ansi_system **result,
	bool *input_available, bool utf8)
{
	struct st_console_ansi_system *system;
	DWORD mode;

	if (result == NULL || input_available == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	system = calloc(1, sizeof(*system));
	if (system == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	system->input = GetStdHandle(STD_INPUT_HANDLE);
	system->output = GetStdHandle(STD_OUTPUT_HANDLE);
	*input_available = false;
	if (system->output != INVALID_HANDLE_VALUE
	    && GetConsoleMode(system->output, &system->output_mode)) {
		system->restore_cursor = GetConsoleCursorInfo(system->output,
		    &system->cursor_info) != FALSE;
		mode = system->output_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
		if (!SetConsoleMode(system->output, mode)) {
			free(system);
			return IO_ERROR_NOT_SUPPORTED;
		}
		system->restore_output = true;
		system->output_code_page = GetConsoleOutputCP();
		(void)SetConsoleOutputCP(utf8 ? CP_UTF8 : 437);
	}
	if (system->input != INVALID_HANDLE_VALUE
	    && GetConsoleMode(system->input, &system->input_mode)) {
		mode = system->input_mode | ENABLE_VIRTUAL_TERMINAL_INPUT;
		mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
		if (!SetConsoleMode(system->input, mode)) {
			st_console_ansi_system_close(system);
			return IO_ERROR_NOT_SUPPORTED;
		}
		system->restore_input = true;
		system->input_code_page = GetConsoleCP();
		(void)SetConsoleCP(utf8 ? CP_UTF8 : 437);
		*input_available = true;
	}
	system->input_file_mode = _setmode(_fileno(stdin), _O_BINARY);
	system->output_file_mode = _setmode(_fileno(stdout), _O_BINARY);
	*result = system;
	return 0;
}

int
st_console_ansi_system_read(struct st_console_ansi_system *system,
	void *buffer, size_t size, size_t *received, unsigned int timeout_ms)
{
	DWORD count;
	DWORD amount = size > MAXDWORD ? MAXDWORD : (DWORD)size;
	DWORD wait;

	if (system == NULL || buffer == NULL || size == 0 || received == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*received = 0;
	wait = WaitForSingleObject(system->input, timeout_ms);
	if (wait == WAIT_TIMEOUT)
		return 0;
	if (wait != WAIT_OBJECT_0)
		return io_error_from_windows(GetLastError());
	if (!ReadFile(system->input, buffer, amount, &count, NULL))
		return io_error_from_windows(GetLastError());
	*received = count;
	return 0;
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
	if (system->restore_input) {
		(void)SetConsoleMode(system->input, system->input_mode);
		(void)SetConsoleCP(system->input_code_page);
	}
	if (system->restore_output) {
		(void)SetConsoleMode(system->output, system->output_mode);
		(void)SetConsoleOutputCP(system->output_code_page);
	}
	if (system->restore_cursor)
		(void)SetConsoleCursorInfo(system->output, &system->cursor_info);
	if (system->input_file_mode >= 0)
		(void)_setmode(_fileno(stdin), system->input_file_mode);
	if (system->output_file_mode >= 0)
		(void)_setmode(_fileno(stdout), system->output_file_mode);
	free(system);
}
