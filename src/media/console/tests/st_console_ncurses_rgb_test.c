/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Exercise colour mapping without opening a terminal or loading curses. */
#include <stddef.h>

#include "console_config.h"

/* This fixture installs its own API table and simulates opaque WINDOWs.
 * Native linked/runtime loader behavior has separate PTY coverage. */
#undef ST_DEP_CURSES_LINKED
#define ST_DEP_CURSES_LINKED 0

#if ST_CONSOLE_HAVE_CURSES_H && defined(ST_CONSOLE_TEST_OPAQUE_WINDOW)
#include <curses.h>
/* Exercise opaque windows even when the host headers expose their layout. */
#define WINDOW struct st_test_opaque_window
#endif

#include "../console_ncurses.c"

#if ST_CONSOLE_HAVE_CURSES_H
#include <locale.h>
#include <stdarg.h>
#include <wchar.h>

static size_t
decode_locale_character(void)
{
	mbstate_t state = {0};

	return mbrtowc(NULL, "\xc3\xa9", 2, &state);
}

static int
byte_locale_callback(void *argument)
{
#if ST_CONSOLE_HAVE_THREAD_LOCALE
	/* The C locale may reject high bytes or decode them individually. */
	size_t decoded = decode_locale_character();

	if (decoded != 1 && decoded != (size_t)-1)
		return IO_ERROR_INVALID_ENCODING;
#endif
	return *(int *)argument;
}

static bool
test_locale_scope(void)
{
	struct st_curses_locale *locale = NULL;
	size_t caller_decoded;
	int status = 0;
	bool correct;

	/* A UTF-8 caller must regain its locale even when the operation fails. */
	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL)
		(void)setlocale(LC_CTYPE, "en_US.UTF-8");
	caller_decoded = decode_locale_character();
	if (st_curses_locale_create(&locale) != 0)
		return false;
	correct = st_curses_locale_call(locale, byte_locale_callback, &status) == 0
	    && decode_locale_character() == caller_decoded;
	status = IO_ERROR_CANCELLED;
	correct = correct
	    && st_curses_locale_call(locale, byte_locale_callback, &status) == status
	    && decode_locale_character() == caller_decoded;
	st_curses_locale_destroy(locale);
	return correct;
}

static bool missing_clear, non_ansi_cursor, missing_colours;

static char *
ansi_test_capability(const char *name)
{
	if (strcmp(name, "clear") == 0)
		return missing_clear ? NULL : (char *)"\033[H\033[2J";
	if (strcmp(name, "sgr0") == 0)
		return (char *)"\033(B\033[m";
	if (strcmp(name, "setaf") == 0 || strcmp(name, "setab") == 0)
		return missing_colours ? (char *)(intptr_t)-1 : (char *)name;
	return (char *)name;
}

static char *
ansi_test_expand(const char *capability, ...)
{
	va_list arguments;
	int first, second;

	va_start(arguments, capability);
	first = va_arg(arguments, int);
	second = va_arg(arguments, int);
	va_end(arguments);
	if (strcmp(capability, "cup") == 0 && first == 1 && second == 2)
		return non_ansi_cursor ? (char *)"\033Y!\"" : (char *)"\033[2;3H";
	if (strcmp(capability, "setaf") == 0 && first == 2)
		return (char *)"\033[32m";
	if (strcmp(capability, "setab") == 0 && first == 2)
		return (char *)"\033[42m";
	return NULL;
}

static bool
test_ansi_capabilities(void)
{
	struct curses_state state = {.colours = true};

	state.api.call_tigetstr = ansi_test_capability;
	if (ansi_cell_output(&state))
		return false;
	state.api.call_tiparm = ansi_test_expand;
	if (!ansi_cell_output(&state))
		return false;
	non_ansi_cursor = true;
	if (ansi_cell_output(&state))
		return false;
	non_ansi_cursor = false;
	missing_clear = true;
	if (ansi_cell_output(&state))
		return false;
	missing_clear = false;
	missing_colours = true;
	if (ansi_cell_output(&state))
		return false;
	state.colours = false;
	bool correct = ansi_cell_output(&state);
	missing_colours = false;
	return correct;
}

static bool
test_sgr_palette(void)
{
	int indexed_colours[UINT16_MAX + 1U];
	struct curses_state state = {
		.colours = true, .colour_count = 256, .terminal_colour_count = 256,
		.indexed_colours = indexed_colours,
	};
	st_console_display_t display = {.backend_state = &state};
	st_console_t console = {.display = &display};
	struct sgr_writer writer = {0};
	struct st_console_cell cell = {
		.foreground = ST_CONSOLE_COLOR_RGB_VALUE(255, 84, 84),
		.background = 0, .flags = ST_CONSOLE_CELL_BOLD,
	};
	int foreground, background;
	bool correct = false;
	static const char indexed[] = "\033[0m\033[1m\033[38;5;203m\033[40m";
	static const char basic[] = "\033[0m\033[1m\033[31m\033[104m";
	static const char eight[] = "\033[0m\033[1m\033[31m\033[40m";
	static const char rgb[] = "\033[0m\033[1m\033[38;2;128;0;0m\033[48;2;0;0;255m";

	state.terminal_colours = calloc(256, sizeof(*state.terminal_colours));
	if (state.terminal_colours == NULL)
		return false;
	for (unsigned i = 0; i < 256; ++i) {
		xterm_terminal_colour(i, 256, &state.terminal_colours[i].red,
		    &state.terminal_colours[i].green, &state.terminal_colours[i].blue);
		state.terminal_colours[i].valid = true;
	}
	rebuild_colour_maps(&console);
	sgr_style(&console, &state, &writer, &cell, SIZE_MAX, &foreground, &background);
	if (foreground != 203 || background != 0 || writer.length != sizeof(indexed) - 1
	    || memcmp(writer.data, indexed, sizeof(indexed) - 1) != 0)
		goto done;
	state.colour_count = state.terminal_colour_count = 16;
	cell.foreground = ST_CONSOLE_COLOR_RGB_VALUE(128, 0, 0);
	cell.background = ST_CONSOLE_COLOR_RGB_VALUE(0, 0, 255);
	writer = (struct sgr_writer){0};
	sgr_style(&console, &state, &writer, &cell, SIZE_MAX, &foreground, &background);
	if (foreground != 1 || background != 12 || writer.length != sizeof(basic) - 1
	    || memcmp(writer.data, basic, sizeof(basic) - 1) != 0)
		goto done;
	state.colour_count = state.terminal_colour_count = 8;
	console.palette[9] = UINT32_C(0xff0000);
	cell.foreground = 9;
	cell.background = 0;
	cell.flags = 0;
	writer = (struct sgr_writer){0};
	sgr_style(&console, &state, &writer, &cell, SIZE_MAX, &foreground, &background);
	if (foreground != 1 || background != 0 || writer.length != sizeof(eight) - 1
	    || memcmp(writer.data, eight, sizeof(eight) - 1) != 0)
		goto done;
	state.sgr_direct = true;
	cell.foreground = ST_CONSOLE_COLOR_RGB_VALUE(128, 0, 0);
	cell.background = ST_CONSOLE_COLOR_RGB_VALUE(0, 0, 255);
	cell.flags = ST_CONSOLE_CELL_BOLD;
	writer = (struct sgr_writer){0};
	sgr_style(&console, &state, &writer, &cell, SIZE_MAX, &foreground, &background);
	correct = foreground == -1 && background == -1
	    && writer.length == sizeof(rgb) - 1
	    && memcmp(writer.data, rgb, sizeof(rgb) - 1) == 0;
done:
	free(state.terminal_colours);
	return correct;
}

static unsigned initialized_pairs;

static int
initialize_test_pair(NCURSES_PAIRS_T pair, NCURSES_COLOR_T foreground,
	NCURSES_COLOR_T background)
{
	(void)pair;
	(void)foreground;
	(void)background;
	++initialized_pairs;
	return OK;
}

static bool
test_pair_limits(void)
{
	struct curses_state state = {
		.colours = true, .colour_count = 256, .pair_count = 256,
		.sgr_output = true,
	};

	state.api.call_init_pair = initialize_test_pair;
	configure_colour_pairs(&state);
	if (state.colour_count != 256 || initialized_pairs != 0)
		return false;
	state.sgr_output = false;
	configure_colour_pairs(&state);
	if (state.colour_count != 15 || initialized_pairs != 225)
		return false;
	state.extended_pairs = true;
	state.colour_count = 256;
	configure_colour_pairs(&state);
	return state.colour_count == 256 && initialized_pairs == 225;
}

static bool expanded_initc, emitted_initc;

static char *
expand_test_initc(const char *capability, ...)
{
	va_list arguments;

	va_start(arguments, capability);
	expanded_initc = strcmp(capability, "initialize colour") == 0
	    && va_arg(arguments, int) == 203
	    && va_arg(arguments, int) == 1000
	    && va_arg(arguments, int) == 329
	    && va_arg(arguments, int) == 329;
	va_end(arguments);
	return expanded_initc ? (char *)"expanded colour$<1>" : NULL;
}

static int
emit_test_initc(const char *text, int affected, int (*output)(int))
{
	emitted_initc = strcmp(text, "expanded colour$<1>") == 0
	    && affected == 1 && output == putchar;
	return emitted_initc ? OK : ERR;
}

static bool
test_sgr_palette_programming(void)
{
	struct curses_state state = {.sgr_output = true};

	state.initialize_colour = "initialize colour";
	state.api.call_tiparm = expand_test_initc;
	state.api.call_tputs = emit_test_initc;
	return write_terminal_colour(&state, 203, 1000, 329, 329) == OK
	    && expanded_initc && emitted_initc;
}

static unsigned resize_requests;
static bool window_deleted;
/* Only the fake API receives this aligned token; no WINDOW data is accessed. */
static max_align_t window_token;

static int
notify_resize(int rows, int columns)
{
	(void)rows;
	(void)columns;
	++resize_requests;
	return OK;
}

static WINDOW *
dimension_window(int rows, int columns, int y, int x)
{
	(void)rows;
	(void)columns;
	(void)y;
	(void)x;
	window_deleted = false;
	return (WINDOW *)(void *)&window_token;
}

static int dimension_columns(WINDOW *window) { (void)window; return 101; }
static int dimension_rows(WINDOW *window) { (void)window; return 43; }
static int
dimension_delete(WINDOW *window)
{
	if ((void *)window != &window_token)
		return ERR;
	window_deleted = true;
	return OK;
}

static int last_row, last_column;
static unsigned margin_cells;
static int layout_move(WINDOW *window, int y, int x)
{
	(void)window;
	last_row = y;
	last_column = x;
	return OK;
}
static int layout_add(WINDOW *window, chtype value)
{
	(void)window;
	if ((value & A_CHARTEXT) == ' ')
		++margin_cells;
	return OK;
}
static int layout_refresh(WINDOW *window) { (void)window; return OK; }
static int layout_cursor(int cursor) { (void)cursor; return OK; }
static int layout_attributes(WINDOW *window, attr_t attributes,
	NCURSES_PAIRS_T pair, void *extended)
{
	(void)window; (void)attributes; (void)pair; (void)extended;
	return OK;
}
#ifdef KEY_MOUSE
static int layout_mouse(MEVENT *event)
{
	memset(event, 0, sizeof(*event));
	event->x = 3;
	event->y = 3;
	return OK;
}
#endif

static bool
test_layout(void)
{
	struct st_console_cell cells[8] = {0}, drawn[8] = {0};
	int foreground[8], background[8];
	struct curses_state state = {
		.api = {.call_move = layout_move, .call_addch = layout_add,
		    .call_attributes = layout_attributes,
		    .call_refresh = layout_refresh, .call_cursor = layout_cursor},
		.drawn = drawn, .drawn_foreground = foreground,
		.drawn_background = background,
		.presentation = ST_CONSOLE_TEXT_ASCII,
	};
	st_console_display_t display = {.backend_state = &state};
	st_console_t console = {
		.display = &display, .columns = 4, .rows = 2, .cells = cells,
		.cursor_visible = true, .cursor_x = 1, .cursor_y = 1,
	};

	atomic_init(&state.host_columns, 8);
	atomic_init(&state.host_rows, 6);
	atomic_init(&state.host_resized, false);
	for (size_t i = 0; i < 8; ++i) {
		cells[i].glyph = 'A';
		cells[i].foreground = 7;
		cells[i].flags = ST_CONSOLE_CELL_DIRTY;
		foreground[i] = background[i] = -1;
	}
	if (curses_present(&console) != 0 || last_row != 3 || last_column != 3
	    || margin_cells != 40)
		return false;
#ifdef KEY_MOUSE
	{
		struct st_console_event event;

		state.mouse_enabled = true;
		state.api.call_getmouse = layout_mouse;
		if (!mouse_event(&state, &event) || event.data.mouse.x != 1
		    || event.data.mouse.y != 1)
			return false;
	}
#endif
	display.config.text_left_justify = true;
	state.margins_initialized = false;
	if (curses_present(&console) != 0 || last_row != 3 || last_column != 1)
		return false;
	atomic_store(&state.host_columns, 3);
	atomic_store(&state.host_rows, 1);
	if (curses_present(&console) != 0 || state.viewport.x != 0
	    || state.viewport.y != 0 || state.viewport.width != 3
	    || state.viewport.height != 1)
		return false;
	state.margin_visible = true;
	state.margin_colour = 3;
	state.margin_rgb = UINT32_C(0x0000a8);
	return terminal_colour_conflicts(&console, &state, 3, SIZE_MAX,
	    UINT32_C(0xa80000));
}

#if ST_CONSOLE_HAVE_CURSES_WIDE
static wchar_t written_text[64];
static int written_count;
static int
record_wide(WINDOW *window, const wchar_t *text, int count)
{
	(void)window;
	if (count < 0 || (size_t)count >= sizeof(written_text) / sizeof(written_text[0]))
		return ERR;
	memcpy(written_text, text, ((size_t)count + 1) * sizeof(*text));
	written_count = count;
	return OK;
}
#endif

static bool
test_emoji_text(void)
{
	static const char joystick[] = "\xf0\x9f\x95\xb9\xef\xb8\x8f";
	static const char toned_hand[] = "\xf0\x9f\x91\x8b\xf0\x9f\x8f\xbd";
	/* Longer than the output helper's small buffer, with ZWJ and selectors. */
	static const char kiss[] = "\xf0\x9f\x91\xa9\xf0\x9f\x8f\xbd"
	    "\xe2\x80\x8d\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d"
	    "\xf0\x9f\x92\x8b\xe2\x80\x8d\xf0\x9f\x91\xa8\xf0\x9f\x8f\xbd";
	static const uint8_t bitmap[1] = {0}, wide_bitmap[6] = {0};
	static const struct st_console_font_mapping mapping[] = {{'?', 0}};
	static const struct st_console_font_cluster_mapping clusters[] = {
		{joystick, sizeof(joystick) - 1, 1},
		{toned_hand, sizeof(toned_hand) - 1, 2},
		{kiss, sizeof(kiss) - 1, 3},
	};
	const struct st_console_font font = {
		.bitmap = bitmap, .bitmap_size = sizeof(bitmap),
		.width = 8, .height = 1, .glyph_count = 1,
		.row_stride = 1, .glyph_stride = 1,
		.wide_bitmap = wide_bitmap, .wide_bitmap_size = sizeof(wide_bitmap),
		.wide_glyph_count = 3, .wide_row_stride = 2, .wide_glyph_stride = 2,
		.mapping = mapping, .mapping_count = 1,
		.cluster_mapping = clusters, .cluster_mapping_count = 3,
	};
	const struct st_console_display_config output = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {.columns = 8, .rows = 2};
	struct curses_state state = {.presentation = ST_CONSOLE_TEXT_UNICODE};
	st_console_t *console = NULL;
	uint16_t identifier;
	bool correct = false;

	if (st_console_create(&console, &output, &config) != 0)
		return false;
	if (st_console_register_static_font(console, &font, &identifier) != 0)
		goto done;
	for (size_t i = 0; i < 3; ++i) {
		struct st_console_cell cell = {
			.glyph_font = identifier, .glyph = clusters[i].glyph,
		};
		struct sgr_writer writer = {0};

		sgr_character(&writer, console, &state, &cell);
		if (writer.result != 0 || writer.length != clusters[i].text_size
		    || memcmp(writer.data, clusters[i].text, writer.length) != 0)
			goto done;
#if ST_CONSOLE_HAVE_CURSES_WIDE
		{
			size_t offset = 0;
			int count = 0;

			state.wide = true;
			state.api.call_add_wide = record_wide;
			if (write_cell(console, &state, &cell) != OK)
				goto done;
			while (offset < clusters[i].text_size) {
				uint32_t scalar;
				size_t consumed;

				if (st_utf8_decode_one(&scalar, &consumed,
				    clusters[i].text + offset,
				    clusters[i].text_size - offset) != 0
				    || count >= written_count
				    || written_text[count++] != (wchar_t)scalar)
					goto done;
				offset += consumed;
			}
			if (count != written_count || written_text[count] != 0)
				goto done;
		}
#endif
	}
	correct = true;
done:
	st_console_destroy(console);
	return correct;
}
#endif

int
main(void)
{
#if ST_CONSOLE_HAVE_CURSES_H
	if (!test_locale_scope()) {
		fprintf(stderr, "curses byte locale leaked into the caller\n");
		return EXIT_FAILURE;
	}
	struct curses_state state = {
		.direct_colour = true,
		.colour_count = 16777216,
		.red_bits = 8, .green_bits = 8, .blue_bits = 8,
	};
	st_console_display_t display = {.backend_state = &state};
	st_console_t console = {.display = &display};
	int indexed_colours[UINT16_MAX + 1U];
	int indexed;
	int direct;
	uint32_t columns = 0, rows = 0;

	if (!test_layout()) {
		fprintf(stderr, "text layout, cursor, or mouse coordinates failed\n");
		return EXIT_FAILURE;
	}
	if (!test_emoji_text()) {
		fprintf(stderr, "curses lost an emoji sequence\n");
		return EXIT_FAILURE;
	}
	if (!test_ansi_capabilities() || !test_sgr_palette()
	    || !test_pair_limits() || !test_sgr_palette_programming()) {
		fprintf(stderr, "curses ANSI capabilities or indexed output failed\n");
		return EXIT_FAILURE;
	}

	state.api.call_newwin = dimension_window;
	state.api.call_delwin = dimension_delete;
	state.api.call_getmaxx = dimension_columns;
	state.api.call_getmaxy = dimension_rows;
	state.api.call_resizeterm = notify_resize;
	/* NetBSD resizeterm() queues KEY_RESIZE even for an unchanged size.
	 * Reading dimensions in response must not trigger the next resize. */
	if (!terminal_dimensions(&state, &columns, &rows)
	    || columns != 101 || rows != 43 || resize_requests != 0
	    || !window_deleted)
		return EXIT_FAILURE;

	state.indexed_colours = indexed_colours;
	/* Both indexed and direct colours resolve through their logical RGB. */
	console.palette[15] = UINT32_C(0xffffff);
	rebuild_colour_maps(&console);
	indexed = cell_colour(&console, &state, 15, 0);
	direct = cell_colour(&console, &state,
	    ST_CONSOLE_COLOR_RGB_VALUE(255, 255, 255), 0);
	fprintf(stderr, "native RGB: indexed white=%06x, direct white=%06x\n",
	    (unsigned int)indexed, (unsigned int)direct);
	return indexed == 0xffffff && direct == 0xffffff
	    ? EXIT_SUCCESS : EXIT_FAILURE;
#else
	fprintf(stderr, "curses headers unavailable\n");
	return 77;
#endif
}
