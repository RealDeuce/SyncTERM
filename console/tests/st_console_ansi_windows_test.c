/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_console.h"

#include "io_error.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

static bool
next_event(st_console_t *console, struct st_console_event *event)
{
	struct timespec deadline;

	(void)timespec_get(&deadline, TIME_UTC);
	deadline.tv_sec += 5;
	return st_console_event_timedwait(console, event, &deadline) == 0;
}

static int
child_test(bool stop_only, bool cp437)
{
	const struct st_console_display_config display_config = {
		.backend = ST_CONSOLE_BACKEND_ANSI,
		.text_presentation = cp437 ? ST_CONSOLE_TEXT_CP437
		    : ST_CONSOLE_TEXT_UNICODE,
	};
	const struct st_console_config console_config = {
		.columns = 8,
		.rows = 3,
		.wrap = true,
	};
	st_console_t *console = NULL;
	struct st_console_event event;
	int result = EXIT_FAILURE;

	if (st_console_create(&console, &display_config, &console_config) != 0)
		return EXIT_FAILURE;
	if (stop_only) {
		result = EXIT_SUCCESS;
		goto done;
	}
	if (cp437) {
		if (!next_event(console, &event)
		    || event.type != ST_CONSOLE_EVENT_TEXT
		    || event.data.text.codepoint != UINT32_C(0x00e9)
		    || !next_event(console, &event)
		    || event.type != ST_CONSOLE_EVENT_CLOSE
		    || st_console_event_try(console, &event)
		    != IO_ERROR_WOULD_BLOCK)
			goto done;
		result = EXIT_SUCCESS;
		goto done;
	}
	if (!next_event(console, &event)
	    || event.type != ST_CONSOLE_EVENT_KEY_CODE
	    || event.data.key.code != 13
	    || !next_event(console, &event)
	    || event.type != ST_CONSOLE_EVENT_KEY_CODE
	    || event.data.key.code != ST_CONSOLE_KEY_UP
	    || event.data.key.modifiers != ST_CONSOLE_MODIFIER_ALT
	    || !next_event(console, &event)
	    || event.type != ST_CONSOLE_EVENT_TEXT
	    || event.data.text.codepoint != UINT32_C(0x00e9)
	    || !next_event(console, &event)
	    || event.type != ST_CONSOLE_EVENT_DOORWAY_KEY
	    || event.data.doorway_key.scan_code != 0x2e
	    || event.data.doorway_key.enhanced
	    || !next_event(console, &event)
	    || event.type != ST_CONSOLE_EVENT_CLOSE
	    || st_console_event_try(console, &event) != IO_ERROR_WOULD_BLOCK)
		goto done;
	result = EXIT_SUCCESS;

done:
	st_console_destroy(console);
	return result;
}

static bool
contains(const unsigned char *data, size_t size, const char *text)
{
	size_t length = strlen(text);

	if (length > size)
		return false;
	for (size_t i = 0; i <= size - length; ++i)
		if (memcmp(&data[i], text, length) == 0)
			return true;
	return false;
}

static bool
run_child(const wchar_t *mode, HANDLE input, HANDLE open_writer)
{
	SECURITY_ATTRIBUTES security = {
		.nLength = sizeof(security),
		.bInheritHandle = TRUE,
	};
	STARTUPINFOW startup = {.cb = sizeof(startup)};
	PROCESS_INFORMATION process;
	HANDLE output_read = INVALID_HANDLE_VALUE;
	HANDLE output_write = INVALID_HANDLE_VALUE;
	wchar_t executable[MAX_PATH];
	wchar_t command[MAX_PATH * 2];
	unsigned char output[4096];
	size_t output_size = 0;
	DWORD status;
	DWORD exit_code;
	bool success = false;

	if (GetModuleFileNameW(NULL, executable, MAX_PATH) == 0
	    || !CreatePipe(&output_read, &output_write, &security, 0)
	    || !SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0))
		goto done;
	if (swprintf(command, sizeof(command) / sizeof(command[0]),
	    L"\"%ls\" %ls", executable, mode) < 0)
		goto done;
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = input;
	startup.hStdOutput = output_write;
	startup.hStdError = output_write;
	if (!CreateProcessW(executable, command, NULL, NULL, TRUE, 0, NULL, NULL,
	    &startup, &process))
		goto done;
	(void)CloseHandle(output_write);
	output_write = INVALID_HANDLE_VALUE;
	status = WaitForSingleObject(process.hProcess, 5000);
	if (status != WAIT_OBJECT_0) {
		(void)TerminateProcess(process.hProcess, EXIT_FAILURE);
		(void)WaitForSingleObject(process.hProcess, 5000);
		goto process_done;
	}
	if (!GetExitCodeProcess(process.hProcess, &exit_code)
	    || exit_code != EXIT_SUCCESS)
		goto process_done;
	for (;;) {
		DWORD count;

		if (!ReadFile(output_read, &output[output_size],
		    (DWORD)(sizeof(output) - output_size), &count, NULL)) {
			if (GetLastError() == ERROR_BROKEN_PIPE)
				break;
			goto process_done;
		}
		if (count == 0)
			break;
		output_size += count;
		if (output_size == sizeof(output))
			break;
	}
	success = contains(output, output_size, "\033[=255h")
	    && contains(output, output_size,
	    "\033[r\033[0m\033[?25h\033[0 q\033[=255l");

process_done:
	(void)CloseHandle(process.hThread);
	(void)CloseHandle(process.hProcess);

done:
	if (open_writer != INVALID_HANDLE_VALUE)
		(void)CloseHandle(open_writer);
	if (output_read != INVALID_HANDLE_VALUE)
		(void)CloseHandle(output_read);
	if (output_write != INVALID_HANDLE_VALUE)
		(void)CloseHandle(output_write);
	return success;
}

static bool
pipe_case(bool stop_only, bool cp437)
{
	static const unsigned char input_data[] = {
		'\r', 0x1b, '[', '1', ';', '3', 'A', 0xc3, 0xa9, 0x00, 0x2e,
	};
	static const unsigned char cp437_input[] = {0x82};
	const unsigned char *data = cp437 ? cp437_input : input_data;
	size_t size = cp437 ? sizeof(cp437_input) : sizeof(input_data);
	SECURITY_ATTRIBUTES security = {
		.nLength = sizeof(security),
		.bInheritHandle = TRUE,
	};
	HANDLE input_read = INVALID_HANDLE_VALUE;
	HANDLE input_write = INVALID_HANDLE_VALUE;
	DWORD count;
	bool result;

	if (!CreatePipe(&input_read, &input_write, &security, 0)
	    || !SetHandleInformation(input_write, HANDLE_FLAG_INHERIT, 0))
		return false;
	if (!stop_only) {
		if (!WriteFile(input_write, data, size, &count, NULL)
		    || count != (DWORD)size) {
			(void)CloseHandle(input_read);
			(void)CloseHandle(input_write);
			return false;
		}
		(void)CloseHandle(input_write);
		input_write = INVALID_HANDLE_VALUE;
	}
	result = run_child(stop_only ? L"--stop-child"
	    : cp437 ? L"--cp437-child" : L"--input-child",
	    input_read, input_write);
	(void)CloseHandle(input_read);
	return result;
}

static bool
file_case(void)
{
	static const unsigned char input_data[] = {
		'\r', 0x1b, '[', '1', ';', '3', 'A', 0xc3, 0xa9, 0x00, 0x2e,
	};
	SECURITY_ATTRIBUTES security = {
		.nLength = sizeof(security),
		.bInheritHandle = TRUE,
	};
	wchar_t directory[MAX_PATH];
	wchar_t path[MAX_PATH];
	HANDLE input = INVALID_HANDLE_VALUE;
	DWORD count;
	bool result = false;

	if (GetTempPathW(MAX_PATH, directory) == 0
	    || GetTempFileNameW(directory, L"sta", 0, path) == 0)
		return false;
	input = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
	    &security, TRUNCATE_EXISTING, FILE_ATTRIBUTE_TEMPORARY, NULL);
	if (input == INVALID_HANDLE_VALUE)
		goto done;
	if (!WriteFile(input, input_data, sizeof(input_data), &count, NULL)
	    || count != (DWORD)sizeof(input_data))
		goto done;
	if (SetFilePointer(input, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER
	    && GetLastError() != ERROR_SUCCESS)
		goto done;
	result = run_child(L"--input-child", input, INVALID_HANDLE_VALUE);

done:
	if (input != INVALID_HANDLE_VALUE)
		(void)CloseHandle(input);
	(void)DeleteFileW(path);
	return result;
}

int
main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "--input-child") == 0)
		return child_test(false, false);
	if (argc == 2 && strcmp(argv[1], "--cp437-child") == 0)
		return child_test(false, true);
	if (argc == 2 && strcmp(argv[1], "--stop-child") == 0)
		return child_test(true, false);
	return pipe_case(false, false) && pipe_case(false, true)
	    && pipe_case(true, false) && file_case()
	    ? EXIT_SUCCESS : EXIT_FAILURE;
}
