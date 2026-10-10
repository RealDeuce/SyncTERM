/* Focused policy tests for the minimal connected-session loop. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_term.h"

#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, \
		    #expression); \
		return EXIT_FAILURE; \
	} \
} while (0)

#if !SYNCTERM_PLATFORM_WINDOWS
static int
test_shell_utf8_boundary(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config console_config = {
		.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 16,
	};
	struct st_profile profile;
	st_console_t *console = NULL;
	st_connection_t *connection = NULL;
	st_font_catalog_t *fonts = NULL;
	st_term_session_t *session = NULL;
	struct st_console_rect one = {6, 0, 1, 1};
	struct st_console_cell cell;
	enum st_term_disposition disposition;
	bool finished;
	char text[8];
	size_t required;
	bool found = false;
	int status;

	CHECK(st_console_create(&console, &display, &console_config) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
	profile.screen_mode = ST_SCREEN_MODE_80X25;
	strcpy(profile.address, "printf '%s:' \"$MM_CHARSET\"; "
	    "printf '\\303'; sleep 1; printf '\\251'; sleep 2");
	CHECK(st_term_connect_profile(&connection, &profile, console, NULL, NULL)
	    == 0);
	const struct st_term_config config = {
		.console = console,
		.connection = connection,
		.fonts = fonts,
		.screen_mode = ST_SCREEN_MODE_80X25,
		.encoding = st_term_profile_encoding(&profile),
		.codepage = ST_CODEPAGE_CP437,
		.font_selector = 0,
		.hide_status = true,
	};
	CHECK(st_term_session_create(&session, &config) == 0);
	for (size_t attempt = 0; attempt < 100 && !found; ++attempt) {
		status = st_term_session_pump(session, &disposition, &finished);
		CHECK(status == 0);
		CHECK(st_console_read_cells(console, &one, &cell, 1) == 0);
		if (st_console_font_glyph_text(console, cell.glyph_font, cell.glyph,
		    text, sizeof(text), &required) == 0
		    && required == 2 && memcmp(text, "\xc3\xa9", 2) == 0)
			found = true;
	}
	CHECK(found);
	st_term_session_destroy(&session);
	CHECK(st_connection_close(connection) == 0);
	st_profile_dispose(&profile);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	return 0;
}
#endif

int
main(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config console_config = {
		.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 16,
	};
	struct st_profile profile;
	st_console_t *console = NULL;
	st_connection_t *connection = NULL;
	st_term_session_t *session = NULL;

	CHECK(st_console_create(&console, &display, &console_config) == 0);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_TELNET;
	CHECK(st_term_profile_encoding(&profile) == ST_CTERM_ENCODING_CODEPAGE);
	profile.connection_type = ST_CONNECTION_SHELL;
	CHECK(st_term_profile_encoding(&profile) == ST_CTERM_ENCODING_UTF8);
	profile.connection_type = ST_CONNECTION_TELNET;
	CHECK(st_term_connect_profile(&connection, &profile, console, NULL, NULL)
	    == IO_ERROR_NOT_SUPPORTED);
	CHECK(connection == NULL);
	CHECK(st_term_session_create(&session, NULL)
	    == IO_ERROR_INVALID_ARGUMENT && session == NULL);
	st_profile_dispose(&profile);
	st_console_destroy(console);
#if !SYNCTERM_PLATFORM_WINDOWS
	CHECK(test_shell_utf8_boundary() == 0);
#endif
	return EXIT_SUCCESS;
}
