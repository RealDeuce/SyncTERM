/* Native Windows console backend integration tests. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_console.h"

#include "io_error.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, \
		    __LINE__, #expression); \
		return 1; \
	} \
} while (0)

int
main(void)
{
	const struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_WIN32,
	};
	const struct st_console_config config = {
		.columns = 80,
		.rows = 25,
		.wrap = true,
	};
	st_console_t *console = NULL;
	st_console_t *second = NULL;
	HANDLE original_output = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_CURSOR_INFO cursor_before;
	CONSOLE_CURSOR_INFO cursor_after;
	CONSOLE_SCREEN_BUFFER_INFO screen_before;
	CONSOLE_SCREEN_BUFFER_INFO screen_after;
	uint32_t capabilities;
	bool cursor_known = original_output != INVALID_HANDLE_VALUE
	    && original_output != NULL
	    && GetConsoleCursorInfo(original_output, &cursor_before)
	    && GetConsoleScreenBufferInfo(original_output, &screen_before);
	int status = st_console_create(&console, &config_display, &config);

	if (status == IO_ERROR_NOT_FOUND || status == IO_ERROR_NOT_SUPPORTED)
		return 77;
	CHECK(status == 0);
	CHECK(st_console_capabilities(console, &capabilities) == 0);
	CHECK((capabilities & ST_CONSOLE_CAP_TEXT_TERMINAL) != 0);
	CHECK((capabilities & ST_CONSOLE_CAP_LOCAL_CONTROL_KEYS) == 0);
	CHECK((capabilities & ST_CONSOLE_CAP_PALETTE_CHANGE) != 0);
	CHECK((capabilities & ST_CONSOLE_CAP_EXTENDED_PALETTE) == 0);
	CHECK(st_console_create(&second, &config_display, &config) == IO_ERROR_BUSY);
	CHECK(second == NULL);
	CHECK(st_console_set_title(console, "st_console native Windows test")
	    == 0);
	CHECK(st_console_write_utf8(console, "native console", 14) == 0);
	CHECK(st_console_present(console) == 0);
	CHECK(st_console_resize(console, 80, 24) == 0);
	st_console_destroy(console);
	if (cursor_known) {
		CHECK(GetConsoleCursorInfo(original_output, &cursor_after));
		CHECK(GetConsoleScreenBufferInfo(original_output, &screen_after));
		CHECK(cursor_after.bVisible == cursor_before.bVisible);
		CHECK(cursor_after.dwSize == cursor_before.dwSize);
		CHECK(screen_after.dwCursorPosition.X
		    == screen_before.dwCursorPosition.X);
		CHECK(screen_after.dwCursorPosition.Y
		    == screen_before.dwCursorPosition.Y);
	}
	return 0;
}
