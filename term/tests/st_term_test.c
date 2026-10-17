/* Focused policy tests for the minimal connected-session loop. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_term.h"

#include "io_error.h"
#include "st_pixels.h"
#include "cterm.h"
#include "cterm_apc.h"

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

static int
test_paste_preparation(void)
{
	static const char input[] = "A\nB\r\n\x1b[201~\xc3\xa9";
	static const char plain[] = "A\rB\r\x1b[201~\xc3\xa9";
	static const char bracketed[] =
	    "\x1b[200~A\rB\r\xc3\xa9\x1b[201~";
	char *prepared = NULL;
	size_t size = 0;

	CHECK(st_term_prepare_paste(&prepared, &size, input,
	    sizeof(input) - 1, false) == 0);
	CHECK(size == sizeof(plain) - 1
	    && memcmp(prepared, plain, size) == 0);
	free(prepared);
	prepared = NULL;
	CHECK(st_term_prepare_paste(&prepared, &size, input,
	    sizeof(input) - 1, true) == 0);
	CHECK(size == sizeof(bracketed) - 1
	    && memcmp(prepared, bracketed, size) == 0);
	free(prepared);
	return 0;
}

#if !SYNCTERM_PLATFORM_WINDOWS
static bool
screen_contains(st_console_t *console, const char *wanted, bool diagnose)
{
	const struct st_console_rect area = {0, 0, 40, 4};
	struct st_console_cell cells[160];
	char text[161];

	if (st_console_read_cells(console, &area, cells, 40) != 0)
		return false;
	for (size_t i = 0; i < 160; ++i) {
		char glyph[8];
		size_t size;

		text[i] = st_console_font_glyph_text(console, cells[i].glyph_font,
		    cells[i].glyph, glyph, sizeof(glyph), &size) == 0 && size == 1 && glyph[0] != '\0'
		    ? glyph[0] : ' ';
	}
	text[160] = '\0';
	if (diagnose && strstr(text, wanted) == NULL)
		fprintf(stderr, "terminal contents: [%s]\n", text);
	return strstr(text, wanted) != NULL;
}

static int
test_shell_apc(void)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {.columns = 40, .rows = 4, .cell_width = 8, .cell_height = 16};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_connection_t *connection = NULL;
	st_term_session_t *session = NULL;
	struct st_profile profile;
	enum st_term_disposition disposition;
	bool finished = false;

	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
	strcpy(profile.address, "stty raw -echo; printf '\\033_SyncTERM:VER\\033\\\\'; "
	    "dd bs=1 count=18 2>/dev/null | od -An -tx1 | tr -d ' \\n'; "
	    "printf '\\r\\nREADY'; dd bs=1 count=9 2>/dev/null | od -An -tx1 | tr -d ' \\n'");
	CHECK(st_term_connect_profile(&connection, &profile, console, NULL, NULL) == 0);
	const struct st_term_config config = {
		.console = console, .connection = connection, .fonts = fonts,
		.screen_mode = ST_SCREEN_MODE_80X25, .encoding = ST_CTERM_ENCODING_UTF8,
		.codepage = ST_CODEPAGE_CP437, .hide_status = true,
		.apc_directory = ST_TERM_APC_TEST_DIRECTORY, .application_version = "X",
	};
	CHECK(st_term_session_create(&session, &config) == 0);
	for (unsigned attempt = 0; attempt < 500 && !screen_contains(console, "READY", false); ++attempt)
		CHECK(st_term_session_pump(session, &disposition, &finished) == 0 && !finished);
	CHECK(screen_contains(console, "1b5f53796e635445524d3a5645523b581b5c", true));
	CHECK(screen_contains(console, "READY", true));
	/* White-box completion setup: an observed running channel has just
	 * stopped. No incoming data remains to trigger another parser write.
	 * The suspended session must nevertheless poll and send its report. */
	st_cterm_t *terminal = st_term_session_cterm(session);
	terminal->apc->notify[2] = terminal->apc->was_running[2] = true;
	st_term_session_set_suspended(session, true);
	CHECK(st_term_session_pump(session, &disposition, &finished) == 0 && !finished);
	CHECK(!terminal->apc->notify[2]);
	st_term_session_set_suspended(session, false);
	for (unsigned attempt = 0; attempt < 500 && !finished; ++attempt)
		CHECK(st_term_session_pump(session, &disposition, &finished) == 0);
	CHECK(finished && disposition == ST_TERM_RETURN_TO_PICKER);
	CHECK(screen_contains(console, "1b5b3d373b323b306e", true));
	/* An already-closed peer must not turn a due notification into a
	 * send error, even if the owner pumps again during disconnect UI. */
	CHECK(!st_connection_is_connected(connection));
	terminal->apc->notify[2] = terminal->apc->was_running[2] = true;
	CHECK(st_term_session_pump(session, &disposition, &finished) == 0 && finished);
	st_term_session_destroy(&session);
	CHECK(st_connection_close(connection) == 0);
	st_profile_dispose(&profile); st_font_catalog_destroy(fonts); st_console_destroy(console);
	return 0;
}

static int
test_prestel_keyboard_session(void)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {
		.columns = 40, .rows = 4, .cell_width = 8, .cell_height = 16,
	};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_connection_t *connection = NULL;
	st_term_session_t *session = NULL;
	struct st_profile profile;
	enum st_term_disposition disposition;
	bool finished = false;

	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
	profile.encoding = ST_CODEPAGE_PRESTEL;
	strcpy(profile.address, "stty raw -echo; printf 'READY\\r\\n'; od -An -tx1 -N 6 | tr -d ' \\n'");
	CHECK(st_term_connect_profile(&connection, &profile, console, NULL, NULL) == 0);
	const struct st_term_config config = {
		.console = console, .connection = connection, .fonts = fonts,
		.screen_mode = ST_SCREEN_MODE_PRESTEL,
		.encoding = ST_CTERM_ENCODING_CODEPAGE, .codepage = ST_CODEPAGE_PRESTEL,
		.hide_status = true,
	};
	CHECK(st_term_session_create(&session, &config) == 0);
	const uint8_t concealed[] = {0x1b, 'X', 'A'};
	size_t consumed;
	CHECK(st_cterm_write(st_term_session_cterm(session), concealed,
	    sizeof(concealed), &consumed) == 0 && consumed == sizeof(concealed));
	for (unsigned attempt = 0; attempt < 500 && !screen_contains(console, "READY", false); ++attempt)
		CHECK(st_term_session_pump(session, &disposition, &finished) == 0 && !finished);
	CHECK(screen_contains(console, "READY", true));
	const struct st_console_event events[] = {
		{.type = ST_CONSOLE_EVENT_TEXT, .data.text.codepoint = '#'},
		{.type = ST_CONSOLE_EVENT_TEXT, .data.text.codepoint = '_'},
		{.type = ST_CONSOLE_EVENT_TEXT, .data.text.codepoint = '`'},
		{.type = ST_CONSOLE_EVENT_KEY_CODE, .data.key.code = '\r'},
		{.type = ST_CONSOLE_EVENT_KEY_CODE,
		 .data.key = {.code = '\r', .modifiers = ST_CONSOLE_MODIFIER_CONTROL}},
		{.type = ST_CONSOLE_EVENT_KEY_CODE,
		 .data.key = {.code = ST_CONSOLE_KEY_F1, .modifiers = ST_CONSOLE_MODIFIER_SHIFT}},
		{.type = ST_CONSOLE_EVENT_KEY_CODE, .data.key.code = ST_CONSOLE_KEY_F3},
	};
	for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); ++i)
		CHECK(st_console_post_event(console, &events[i]) == 0);
	for (unsigned attempt = 0; attempt < 500 && !finished; ++attempt)
		CHECK(st_term_session_pump(session, &disposition, &finished) == 0);
	CHECK(finished && disposition == ST_TERM_RETURN_TO_PICKER);
	CHECK(screen_contains(console, "5f60235f0d91", true));
	const struct st_console_rect concealed_cell = {1, 0, 1, 1};
	struct st_console_cell cell;
	CHECK(st_console_read_cells(console, &concealed_cell, &cell, 1) == 0);
	CHECK((cell.flags & (ST_CONSOLE_CELL_CONCEAL | ST_CONSOLE_CELL_REVEAL))
	    == (ST_CONSOLE_CELL_CONCEAL | ST_CONSOLE_CELL_REVEAL));
	st_term_session_destroy(&session);
	CHECK(st_connection_close(connection) == 0);
	st_profile_dispose(&profile);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	return 0;
}

static int
test_wire_filter(void *context, uint8_t byte, bool *consumed,
	uint8_t *replacement, size_t capacity, size_t *replacement_size)
{
	(void)context;
	*consumed = byte == 'A' || byte == '#' || byte == 'Z';
	*replacement_size = 0;
	if (byte == 'A') {
		if (capacity < 1)
			return IO_ERROR_OUT_OF_RANGE;
		replacement[0] = 'C';
		*replacement_size = 1;
	}
	else if (byte == '#') {
		static const uint8_t colour[] = "\x1b[31m";

		if (capacity < sizeof(colour) - 1)
			return IO_ERROR_OUT_OF_RANGE;
		memcpy(replacement, colour, sizeof(colour) - 1);
		*replacement_size = sizeof(colour) - 1;
	}
	return 0;
}

static int
test_shell_wire_filter(void)
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
	struct st_console_rect row = {0, 0, 3, 1};
	struct st_console_cell cells[3];
	enum st_term_disposition disposition;
	bool finished;
	bool found = false;
	char text[8];
	size_t required;

	CHECK(st_console_create(&console, &display, &console_config) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
	profile.screen_mode = ST_SCREEN_MODE_80X25;
	strcpy(profile.address, "printf 'A#XZ'; sleep 1");
	CHECK(st_term_connect_profile(&connection, &profile, console, NULL,
	    NULL) == 0);
	const struct st_term_config config = {
		.console = console,
		.connection = connection,
		.fonts = fonts,
		.screen_mode = ST_SCREEN_MODE_80X25,
		.encoding = ST_CTERM_ENCODING_UTF8,
		.codepage = ST_CODEPAGE_CP437,
		.font_selector = 0,
		.hide_status = true,
		.wire_filter = test_wire_filter,
	};
	CHECK(st_term_session_create(&session, &config) == 0);
	for (size_t attempt = 0; attempt < 100 && !found; ++attempt) {
		CHECK(st_term_session_pump(session, &disposition, &finished) == 0);
		CHECK(st_console_read_cells(console, &row, cells, 3) == 0);
		found = st_console_font_glyph_text(console, cells[0].glyph_font,
		    cells[0].glyph, text, sizeof(text), &required) == 0
		    && required == 1 && text[0] == 'C'
		    && st_console_font_glyph_text(console, cells[1].glyph_font,
		    cells[1].glyph, text, sizeof(text), &required) == 0
		    && required == 1 && text[0] == 'X';
	}
	CHECK(found);
	CHECK(cells[1].foreground == 17);
	CHECK(st_console_font_glyph_text(console, cells[2].glyph_font,
	    cells[2].glyph, text, sizeof(text), &required) == 0);
	CHECK(required == 1 && text[0] == ' ');
	st_term_session_destroy(&session);
	CHECK(st_connection_close(connection) == 0);
	st_profile_dispose(&profile);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	return 0;
}

static int
test_shell_throttle(void)
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
	struct st_console_rect row = {0, 0, 4, 1};
	struct st_console_cell cells[4];
	enum st_term_disposition disposition;
	bool finished;
	bool first = false;
	bool complete = false;

	CHECK(st_console_create(&console, &display, &console_config) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
	profile.screen_mode = ST_SCREEN_MODE_80X25;
	strcpy(profile.address, "printf ABCD; sleep 1");
	CHECK(st_term_connect_profile(&connection, &profile, console, NULL,
	    NULL) == 0);
	const struct st_term_config config = {
		.console = console, .connection = connection, .fonts = fonts,
		.screen_mode = ST_SCREEN_MODE_80X25,
		.encoding = ST_CTERM_ENCODING_UTF8,
		.codepage = ST_CODEPAGE_CP437,
		.hide_status = true,
		.throttle_supported = true,
		.initial_throttle_bps = 300,
	};
	CHECK(st_term_session_create(&session, &config) == 0);
	CHECK(st_term_session_throttle_speed(session) == 300);
	for (size_t attempt = 0; attempt < 100 && !first; ++attempt) {
		CHECK(st_term_session_pump(session, &disposition, &finished) == 0);
		CHECK(st_console_read_cells(console, &row, cells, 4) == 0);
		first = cells[0].glyph == 'A';
	}
	CHECK(first && cells[1].glyph != 'B');
	st_term_session_set_throttle_speed(session, 0);
	CHECK(st_term_session_throttle_speed(session) == 0);
	for (size_t attempt = 0; attempt < 100 && !complete; ++attempt) {
		CHECK(st_term_session_pump(session, &disposition, &finished) == 0);
		CHECK(st_console_read_cells(console, &row, cells, 4) == 0);
		complete = cells[0].glyph == 'A' && cells[1].glyph == 'B'
		    && cells[2].glyph == 'C' && cells[3].glyph == 'D';
	}
	CHECK(complete);
	st_term_session_destroy(&session);
	CHECK(st_connection_close(connection) == 0);
	st_profile_dispose(&profile);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	return 0;
}

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
	uint32_t columns;
	uint32_t rows;
	char text[8];
	size_t required;
	bool found = false;
	int status;

	CHECK(st_console_create(&console, &display, &console_config) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
	profile.screen_mode = ST_SCREEN_MODE_80X25;
	profile.encoding = ST_PROFILE_ENCODING_UTF8;
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
	const struct st_console_event resize = {
		.type = ST_CONSOLE_EVENT_RESIZE,
		.data.resize = {.columns = 90, .rows = 30},
	};
	CHECK(st_console_post_event(console, &resize) == 0);
	CHECK(st_term_session_pump(session, &disposition, &finished) == 0);
	CHECK(!finished);
	CHECK(st_console_size(console, &columns, &rows) == 0);
	CHECK(columns == 90 && rows == 30);
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
	size_t queued_wire = 0;
	CHECK(st_term_session_send_text_poll(session, "\xc3\xa9", 2,
	    &queued_wire) == 0);
	CHECK(queued_wire == 2);
	st_term_session_request_end(session, false);
	CHECK(st_term_session_pump(session, &disposition, &finished) == 0);
	CHECK(finished && disposition == ST_TERM_RETURN_TO_PICKER);
	CHECK(!st_term_session_remote_closed(session));
	st_term_session_destroy(&session);
	CHECK(st_connection_close(connection) == 0);
	st_profile_dispose(&profile);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	return 0;
}

static int
test_remote_disconnect(void)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {
		.columns = 40, .rows = 4, .cell_width = 8, .cell_height = 16,
	};
	struct st_profile profile;
	st_console_t *console = NULL;
	st_connection_t *connection = NULL;
	st_font_catalog_t *fonts = NULL;
	st_term_session_t *session = NULL;
	st_session_history_t *history = NULL;
	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	CHECK(st_session_history_create(&history, 20) == 0);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
	strcpy(profile.address, "printf 'completed session'");
	CHECK(st_term_connect_profile(&connection, &profile, console, NULL, NULL) == 0);
	const struct st_term_config config = {
		.console = console, .connection = connection, .fonts = fonts, .history = history,
		.screen_mode = ST_SCREEN_MODE_80X25, .encoding = ST_CTERM_ENCODING_UTF8,
		.codepage = ST_CODEPAGE_CP437,
	};
	CHECK(st_term_session_create(&session, &config) == 0);
	enum st_term_disposition disposition;
	bool finished = false;
	for (unsigned attempt = 0; attempt < 500 && !finished; ++attempt)
		CHECK(st_term_session_pump(session, &disposition, &finished) == 0);
	CHECK(finished && disposition == ST_TERM_RETURN_TO_PICKER);
	CHECK(st_term_session_remote_closed(session));
	/* Auxiliary work can retain a terminal after EOF, and releasing its
	 * owner-thread hold must take the ordinary return-to-picker path. */
	st_term_session_set_auxiliary_active(session, true);
	/* A final queued terminal query has no shell to reply to. It must not
	 * turn EOF into an output failure that kills side-band work. */
	size_t written;
	CHECK(st_cterm_write(st_term_session_cterm(session), "\x1b[5n", 4, &written) == 0 && written == 4);
	CHECK(st_term_session_pump(session, &disposition, &finished) == 0 && !finished);
	st_term_session_set_auxiliary_active(session, false);
	CHECK(st_term_session_pump(session, &disposition, &finished) == 0 && finished);
	struct st_console_cell *before = NULL, *after = NULL;
	uint32_t before_columns;
	size_t before_rows;
	CHECK(st_cterm_copy_history(st_term_session_cterm(session), &before,
	    &before_columns, &before_rows) == 0);
	CHECK(st_connection_close(connection) == 0);
	/* Preparation and history publication must not touch the freed connection. */
	CHECK(st_term_session_prepare_disconnect(session) == 0);
	st_term_session_destroy(&session);
	uint32_t columns;
	size_t rows;
	enum st_screen_mode mode;
	bool has_status;
	CHECK(st_session_history_copy(history, &after, &columns, &rows, &mode, &has_status) == 0);
	CHECK(columns == before_columns && rows == before_rows + 1 && !has_status);
	CHECK(memcmp(before, after, before_rows * columns * sizeof(*before)) == 0);
	free(before); free(after);
	st_session_history_destroy(history);
	st_profile_dispose(&profile);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	return 0;
}

static int
test_shell_rip_profile(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config console_config = {
		.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 16,
	};
	const struct st_console_rect pixel = {100, 100, 1, 1};
	struct st_profile profile;
	st_console_t *console = NULL;
	st_connection_t *connection = NULL;
	st_font_catalog_t *fonts = NULL;
	st_term_session_t *session = NULL;
	struct st_pixel_buffer *pixels = NULL;
	enum st_term_disposition disposition;
	bool finished;
	bool drawn = false;
	uint32_t columns, rows, cell_width, cell_height;

	CHECK(st_console_create(&console, &display, &console_config) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
	profile.rip_version = ST_RIP_VERSION_1;
	profile.encoding = ST_PROFILE_ENCODING_UTF8;
	strcpy(profile.address, "printf '!|*\\r\\n!|X2S2S\\r'; sleep 2");
	CHECK(st_term_connect_profile(&connection, &profile, console, NULL, NULL)
	    == 0);
	const struct st_term_config config = {
		.console = console,
		.connection = connection,
		.fonts = fonts,
		.screen_mode = ST_SCREEN_MODE_80X25,
		.encoding = st_term_profile_encoding(&profile),
		.rip_version = st_term_profile_rip_version(&profile),
		.codepage = ST_CODEPAGE_CP437,
		.font_selector = 0,
		.hide_status = true,
	};
	CHECK(st_term_session_create(&session, &config) == 0);
	for (size_t attempt = 0; attempt < 100 && !drawn; attempt++) {
		CHECK(st_term_session_pump(session, &disposition, &finished) == 0);
		CHECK(st_console_get_pixels(&pixels, console, &pixel, false) == 0);
		drawn = pixels->primary[0] == 0x14;
		st_pixel_buffer_destroy(pixels);
		pixels = NULL;
	}
	CHECK(drawn);
	CHECK(st_console_size(console, &columns, &rows) == 0);
	CHECK(columns == 80 && rows == 43);
	CHECK(st_console_cell_pixel_size(console, &cell_width,
	    &cell_height) == 0);
	CHECK(cell_width == 8 && cell_height == 8);
	st_term_session_destroy(&session);
	CHECK(st_connection_close(connection) == 0);
	st_profile_dispose(&profile);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	return 0;
}

static int
test_shell_rip_cp437_boundary(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config screen = {
		.columns = 80, .rows = 43,
		.cell_width = 8, .cell_height = 8,
		.pixel_width = 640, .pixel_height = 350,
	};
	const struct st_console_rect glyph = {50, 50, 8, 8};
	struct st_profile profile;
	st_console_t *console = NULL, *reference = NULL;
	st_font_catalog_t *fonts = NULL, *reference_fonts = NULL;
	st_connection_t *connection = NULL;
	st_term_session_t *session = NULL;
	st_cterm_t *reference_terminal = NULL;
	struct st_pixel_buffer *expected = NULL, *actual = NULL;
	uint32_t expected_palette[256], actual_palette[256];
	enum st_term_disposition disposition;
	bool finished, matched = false;
	size_t mismatch = 0;
	uint32_t mismatch_expected = 0, mismatch_actual = 0;
	size_t consumed;
	static const char raw[] = "!|c0F\r\n!|@1E1E\x82\r\n";

	CHECK(st_console_create(&console, &display, &screen) == 0);
	CHECK(st_console_create(&reference, &display, &screen) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	CHECK(st_font_catalog_create(&reference_fonts, reference) == 0);
	const struct st_cterm_config reference_config = {
		.console = reference,
		.fonts = reference_fonts,
		.emulation = ST_CTERM_ANSI_BBS,
		.encoding = ST_CTERM_ENCODING_CODEPAGE,
		.rip_version = ST_CTERM_RIP_1,
		.codepage = ST_CODEPAGE_CP437,
	};
	CHECK(st_cterm_create(&reference_terminal, &reference_config) == 0);
	CHECK(st_cterm_write(reference_terminal, raw, sizeof(raw) - 1,
	    &consumed) == 0 && consumed == sizeof(raw) - 1);
	CHECK(st_console_get_pixels(&expected, reference, &glyph, false) == 0);
	CHECK(st_console_get_palette(reference, 0, expected_palette, 256) == 0);
	bool painted = false;
	for (size_t i = 0; i < 64; i++)
		painted |= expected_palette[expected->primary[i]]
		    != expected_palette[0];
	CHECK(painted);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
	profile.rip_version = ST_RIP_VERSION_1;
	strcpy(profile.address,
	    "printf '!|c0F\\r\\n!|@1E1E\\202\\r\\n'; sleep 2");
	CHECK(st_term_connect_profile(&connection, &profile, console, NULL,
	    NULL) == 0);
	const struct st_term_config config = {
		.console = console,
		.connection = connection,
		.fonts = fonts,
		.screen_mode = ST_SCREEN_MODE_80X43,
		.encoding = ST_CTERM_ENCODING_CODEPAGE,
		.rip_version = ST_CTERM_RIP_1,
		.codepage = ST_CODEPAGE_CP437,
		.hide_status = true,
	};
	CHECK(st_term_session_create(&session, &config) == 0);
	for (size_t attempt = 0; attempt < 100 && !matched; attempt++) {
		CHECK(st_term_session_pump(session, &disposition, &finished) == 0);
		CHECK(st_console_get_pixels(&actual, console, &glyph, false) == 0);
		CHECK(st_console_get_palette(console, 0, actual_palette, 256) == 0);
		matched = true;
		for (size_t i = 0; i < 64; i++) {
			if (expected_palette[expected->primary[i]]
			    != actual_palette[actual->primary[i]]) {
				matched = false;
				mismatch = i;
				mismatch_expected = expected_palette[expected->primary[i]];
				mismatch_actual = actual_palette[actual->primary[i]];
				break;
			}
		}
		st_pixel_buffer_destroy(actual);
		actual = NULL;
	}
	if (!matched)
		fprintf(stderr, "RIP CP437 glyph mismatch at %zu: %#x != %#x\n",
		    mismatch, mismatch_expected, mismatch_actual);
	CHECK(matched);
	st_term_session_destroy(&session);
	CHECK(st_connection_close(connection) == 0);
	st_cterm_destroy(&reference_terminal);
	st_pixel_buffer_destroy(expected);
	st_profile_dispose(&profile);
	st_font_catalog_destroy(fonts);
	st_font_catalog_destroy(reference_fonts);
	st_console_destroy(console);
	st_console_destroy(reference);
	return 0;
}

static int
test_shell_rip_cp437_query_output(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config screen = {
		.columns = 80, .rows = 43,
		.cell_width = 8, .cell_height = 8,
		.pixel_width = 640, .pixel_height = 350,
	};
	const struct st_console_rect first = {0, 0, 1, 1};
	struct st_profile profile;
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_connection_t *connection = NULL;
	st_term_session_t *session = NULL;
	struct st_console_cell cell;
	enum st_term_disposition disposition;
	char text[8] = {0};
	size_t required;
	bool finished, echoed = false;

	CHECK(st_console_create(&console, &display, &screen) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
	profile.rip_version = ST_RIP_VERSION_1;
	strcpy(profile.address,
	    "stty raw -echo;printf '!|1\\0330000\\202^M\\r\\n';head -c2");
	CHECK(st_term_connect_profile(&connection, &profile, console, NULL,
	    NULL) == 0);
	const struct st_term_config config = {
		.console = console,
		.connection = connection,
		.fonts = fonts,
		.screen_mode = ST_SCREEN_MODE_80X43,
		.encoding = ST_CTERM_ENCODING_CODEPAGE,
		.rip_version = ST_CTERM_RIP_1,
		.codepage = ST_CODEPAGE_CP437,
		.hide_status = true,
	};
	CHECK(st_term_session_create(&session, &config) == 0);
	for (size_t attempt = 0; attempt < 100 && !echoed; attempt++) {
		CHECK(st_term_session_pump(session, &disposition, &finished) == 0);
		CHECK(st_console_read_cells(console, &first, &cell, 1) == 0);
		echoed = st_console_font_glyph_text(console, cell.glyph_font,
		    cell.glyph, text, sizeof(text), &required) == 0
		    && required == 2 && memcmp(text, "\xc3\xa9", 2) == 0;
	}
	if (!echoed)
		fprintf(stderr, "RIP query echo first glyph: font=%u glyph=%u text=%02x %02x\n",
		    (unsigned)cell.glyph_font, (unsigned)cell.glyph,
		    (unsigned)(uint8_t)text[0], (unsigned)(uint8_t)text[1]);
	CHECK(echoed);
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
	CHECK(test_paste_preparation() == 0);
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
	char decoded[8];
	size_t decoded_size;
	static const uint8_t rip_wire[] = {'!', '|', 0x82, '\r'};

	CHECK(st_term_rip_wire_to_utf8(decoded, sizeof(decoded),
	    &decoded_size, rip_wire, sizeof(rip_wire), ST_CODEPAGE_CP437) == 0);
	CHECK(decoded_size == 5
	    && memcmp(decoded, "!|\xc3\xa9\r", 5) == 0);

	for (unsigned value = 0x80; value <= 0xff; value++) {
		uint8_t encoded = 0;
		uint32_t scalar = st_codepage_to_unicode(ST_CODEPAGE_CP437,
		    (uint8_t)value, false);

		CHECK(st_codepage_from_unicode(&encoded, ST_CODEPAGE_CP437,
		    scalar) && encoded == value);
	}

	CHECK(st_console_create(&console, &display, &console_config) == 0);
	st_profile_init(&profile);
	CHECK(st_term_profile_rip_version(NULL) == ST_CTERM_RIP_OFF);
	CHECK(st_term_profile_rip_version(&profile) == ST_CTERM_RIP_OFF);
	profile.rip_version = ST_RIP_VERSION_1;
	CHECK(st_term_profile_rip_version(&profile) == ST_CTERM_RIP_1);
	profile.rip_version = ST_RIP_VERSION_3;
	CHECK(st_term_profile_rip_version(&profile) == ST_CTERM_RIP_3);
	profile.rip_version = ST_RIP_VERSION_NONE;
	profile.connection_type = ST_CONNECTION_TELNET;
	CHECK(st_term_profile_encoding(&profile) == ST_CTERM_ENCODING_CODEPAGE);
	profile.connection_type = ST_CONNECTION_SHELL;
	CHECK(st_term_profile_encoding(&profile) == ST_CTERM_ENCODING_CODEPAGE);
	profile.encoding = ST_PROFILE_ENCODING_UTF8;
	CHECK(st_term_profile_encoding(&profile) == ST_CTERM_ENCODING_UTF8);
	profile.connection_type = ST_CONNECTION_UNKNOWN;
	profile.encoding = ST_CODEPAGE_COUNT;
	CHECK(st_term_connect_profile(&connection, &profile, console, NULL, NULL)
	    == IO_ERROR_INVALID_ARGUMENT);
	profile.encoding = ST_PROFILE_ENCODING_UTF8;
	CHECK(st_term_connect_profile(&connection, &profile, console, NULL, NULL)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(connection == NULL);
	{
		const int types[] = {ST_CONNECTION_TELNET, ST_CONNECTION_TELNET_TLS,
		    ST_CONNECTION_SSH, ST_CONNECTION_SSH_NO_AUTH,
		    ST_CONNECTION_MBBS_GHOST, ST_CONNECTION_MQTT};
		for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
			profile.connection_type = types[i];
			CHECK(st_term_connect_profile(&connection, &profile, console, NULL, NULL)
			    == IO_ERROR_INVALID_ARGUMENT);
			CHECK(connection == NULL);
		}
	}
	CHECK(st_term_session_create(&session, NULL)
	    == IO_ERROR_INVALID_ARGUMENT && session == NULL);
	st_profile_dispose(&profile);
	st_console_destroy(console);
#if !SYNCTERM_PLATFORM_WINDOWS
	CHECK(test_shell_apc() == 0);
	CHECK(test_prestel_keyboard_session() == 0);
	CHECK(test_shell_wire_filter() == 0);
	CHECK(test_shell_throttle() == 0);
	CHECK(test_shell_utf8_boundary() == 0);
	CHECK(test_remote_disconnect() == 0);
	CHECK(test_shell_rip_profile() == 0);
	CHECK(test_shell_rip_cp437_boundary() == 0);
	CHECK(test_shell_rip_cp437_query_output() == 0);
#endif
	return EXIT_SUCCESS;
}
