/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_internal.h"

#include "console_config.h"
#include "dynamic_library.h"
#include "io_error.h"
#include "st_utf8.h"

#include <inttypes.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if ST_CONSOLE_HAVE_CURSES_H
	#include <curses.h>
	#if ST_CONSOLE_HAVE_CURSES_WIDE
		#include <wchar.h>
	#endif

struct curses_api {
	SCREEN *(*call_newterm)(const char *, FILE *, FILE *);
	void (*call_delscreen)(SCREEN *);
	int (*call_endwin)(void);
	WINDOW *(*call_newwin)(int, int, int, int);
	int (*call_delwin)(WINDOW *);
	int (*call_getmaxx)(WINDOW *);
	int (*call_getmaxy)(WINDOW *);
	int (*call_move)(WINDOW *, int, int);
	int (*call_addch)(WINDOW *, const chtype);
	int (*call_refresh)(WINDOW *);
	int (*call_erase)(WINDOW *);
	int (*call_cbreak)(void);
	int (*call_raw)(void);
	int (*call_noecho)(void);
	int (*call_nonl)(void);
	int (*call_keypad)(WINDOW *, bool);
	int (*call_nodelay)(WINDOW *, bool);
	int (*call_scrollok)(WINDOW *, bool);
	int (*call_cursor)(int);
	bool (*call_has_colors)(void);
	bool (*call_can_change_color)(void);
	int (*call_start_color)(void);
	int (*call_color_content)(NCURSES_COLOR_T, NCURSES_COLOR_T *,
	    NCURSES_COLOR_T *, NCURSES_COLOR_T *);
	int (*call_init_color)(NCURSES_COLOR_T, NCURSES_COLOR_T,
	    NCURSES_COLOR_T, NCURSES_COLOR_T);
	int (*call_init_pair)(NCURSES_PAIRS_T, NCURSES_COLOR_T,
	    NCURSES_COLOR_T);
	int (*call_alloc_pair)(int, int);
	int (*call_find_pair)(int, int);
	int (*call_tigetflag)(const char *);
	int (*call_tigetnum)(const char *);
	char *(*call_tigetstr)(const char *);
	int (*call_use_extended_names)(bool);
	int (*call_attributes)(WINDOW *, attr_t, NCURSES_PAIRS_T, void *);
	int (*call_getch)(WINDOW *);
	int (*call_resizeterm)(int, int);
#ifdef KEY_MOUSE
	mmask_t (*call_mousemask)(mmask_t, mmask_t *);
	int (*call_mouseinterval)(int);
	int (*call_getmouse)(MEVENT *);
#endif
#if ST_CONSOLE_HAVE_CURSES_WIDE
	int (*call_add_wide)(WINDOW *, const wchar_t *, int);
	int (*call_get_wide)(WINDOW *, wint_t *);
#endif
};

typedef SCREEN *(*curses_call_newterm_fn)(const char *, FILE *, FILE *);
typedef void (*curses_call_delscreen_fn)(SCREEN *);
typedef int (*curses_call_endwin_fn)(void);
typedef WINDOW *(*curses_call_newwin_fn)(int, int, int, int);
typedef int (*curses_call_delwin_fn)(WINDOW *);
typedef int (*curses_call_getmaxx_fn)(WINDOW *);
typedef int (*curses_call_getmaxy_fn)(WINDOW *);
typedef int (*curses_call_move_fn)(WINDOW *, int, int);
typedef int (*curses_call_addch_fn)(WINDOW *, const chtype);
typedef int (*curses_call_refresh_fn)(WINDOW *);
typedef int (*curses_call_erase_fn)(WINDOW *);
typedef int (*curses_call_cbreak_fn)(void);
typedef int (*curses_call_raw_fn)(void);
typedef int (*curses_call_noecho_fn)(void);
typedef int (*curses_call_nonl_fn)(void);
typedef int (*curses_call_keypad_fn)(WINDOW *, bool);
typedef int (*curses_call_nodelay_fn)(WINDOW *, bool);
typedef int (*curses_call_scrollok_fn)(WINDOW *, bool);
typedef int (*curses_call_cursor_fn)(int);
typedef bool (*curses_call_has_colors_fn)(void);
typedef bool (*curses_call_can_change_color_fn)(void);
typedef int (*curses_call_start_color_fn)(void);
typedef int (*curses_call_color_content_fn)(NCURSES_COLOR_T,
	NCURSES_COLOR_T *, NCURSES_COLOR_T *, NCURSES_COLOR_T *);
typedef int (*curses_call_init_color_fn)(NCURSES_COLOR_T, NCURSES_COLOR_T,
	NCURSES_COLOR_T, NCURSES_COLOR_T);
typedef int (*curses_call_init_pair_fn)(NCURSES_PAIRS_T, NCURSES_COLOR_T,
	NCURSES_COLOR_T);
typedef int (*curses_call_alloc_pair_fn)(int, int);
typedef int (*curses_call_find_pair_fn)(int, int);
typedef int (*curses_call_tigetflag_fn)(const char *);
typedef int (*curses_call_tigetnum_fn)(const char *);
typedef char *(*curses_call_tigetstr_fn)(const char *);
typedef int (*curses_call_use_extended_names_fn)(bool);
typedef int (*curses_call_attributes_fn)(WINDOW *, attr_t, NCURSES_PAIRS_T,
	void *);
typedef int (*curses_call_getch_fn)(WINDOW *);
typedef int (*curses_call_resizeterm_fn)(int, int);
#ifdef KEY_MOUSE
typedef mmask_t (*curses_call_mousemask_fn)(mmask_t, mmask_t *);
typedef int (*curses_call_mouseinterval_fn)(int);
typedef int (*curses_call_getmouse_fn)(MEVENT *);
#endif
#if ST_CONSOLE_HAVE_CURSES_WIDE
typedef int (*curses_call_add_wide_fn)(WINDOW *, const wchar_t *, int);
typedef int (*curses_call_get_wide_fn)(WINDOW *, wint_t *);
#endif

struct curses_state {
	dynamic_library_t *library;
	struct curses_api api;
	st_console_display_t *display;
	SCREEN *screen;
	WINDOW *window;
	struct st_console_cell *drawn;
	st_thread_t input_thread;
	atomic_bool stop;
	atomic_uint host_columns;
	atomic_uint host_rows;
	atomic_bool host_resized;
	bool input_started;
	bool colours;
	bool wide;
	bool extended_pairs;
	bool direct_colour;
	bool sgr_direct;
	bool sgr_initialized;
	unsigned int red_bits;
	unsigned int green_bits;
	unsigned int blue_bits;
	int colour_count;
	int pair_count;
	bool palette_mutable;
	enum st_console_text_presentation presentation;
	int indexed_colours[256];
	struct {
		int red;
		int green;
		int blue;
		bool valid;
	} terminal_colours[256];
	struct {
		uint32_t rgb;
		int colour;
		bool valid;
	} rgb_cache[64];
	unsigned int next_rgb_cache;
#ifdef KEY_MOUSE
	uint32_t mouse_buttons;
	bool mouse_enabled;
#endif
	bool original_colour_valid[256];
	NCURSES_COLOR_T original_colours[256][3];
};

static int curses_resize(st_console_t *console, uint32_t columns,
	uint32_t rows);

static int
replace_window(struct curses_state *state, uint32_t columns, uint32_t rows)
{
	WINDOW *window;

	if (columns > INT_MAX || rows > INT_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	window = state->api.call_newwin((int)rows, (int)columns, 0, 0);
	if (window == NULL)
		return IO_ERROR_OUT_OF_RANGE;
	(void)state->api.call_keypad(window, true);
	(void)state->api.call_nodelay(window, true);
	(void)state->api.call_scrollok(window, false);
	(void)state->api.call_erase(window);
	(void)state->api.call_delwin(state->window);
	state->window = window;
	state->sgr_initialized = false;
	return 0;
}

static atomic_flag curses_in_use = ATOMIC_FLAG_INIT;

#define LOAD_REQUIRED(state, member, name) \
	(((state)->api.member = (curses_ ## member ## _fn) \
	    dynamic_library_symbol((state)->library, (name))) != NULL)
#define LOAD_OPTIONAL(state, member, name) LOAD_REQUIRED(state, member, name)

struct sgr_writer {
	unsigned char data[8192];
	size_t length;
	int result;
};

static void
sgr_flush(struct sgr_writer *writer)
{
	if (writer->result != 0 || writer->length == 0)
		return;
	if (fwrite(writer->data, 1, writer->length, stdout) != writer->length)
		writer->result = IO_ERROR_IO;
	writer->length = 0;
}

static void
sgr_emit(struct sgr_writer *writer, const void *data, size_t length)
{
	const unsigned char *bytes = data;

	while (writer->result == 0 && length != 0) {
		size_t available = sizeof(writer->data) - writer->length;
		size_t amount = length < available ? length : available;

		memcpy(&writer->data[writer->length], bytes, amount);
		writer->length += amount;
		bytes += amount;
		length -= amount;
		if (writer->length == sizeof(writer->data))
			sgr_flush(writer);
	}
}

static void
sgr_format(struct sgr_writer *writer, const char *format, uint32_t first,
	uint32_t second, uint32_t third)
{
	char buffer[96];
	int length = snprintf(buffer, sizeof(buffer), format, first, second, third);

	if (length < 0 || (size_t)length >= sizeof(buffer))
		writer->result = IO_ERROR_IO;
	else
		sgr_emit(writer, buffer, (size_t)length);
}

static int
dos_colour(unsigned int colour)
{
	static const unsigned char map[16] = {
		COLOR_BLACK, COLOR_BLUE, COLOR_GREEN, COLOR_CYAN,
		COLOR_RED, COLOR_MAGENTA, COLOR_YELLOW, COLOR_WHITE,
		8, 12, 10, 14, 9, 13, 11, 15,
	};

	return map[colour & 15U];
}

static uint32_t
scale_component(uint32_t component, unsigned int bits)
{
	uint32_t maximum;

	if (bits == 0)
		return 0;
	if (bits >= 31)
		maximum = INT_MAX;
	else
		maximum = (UINT32_C(1) << bits) - 1;
	return (uint32_t)(((uint64_t)component * maximum + 127) / 255);
}

static int
direct_colour(const struct curses_state *state, uint32_t colour)
{
	uint32_t red = scale_component((colour >> 16) & 0xff,
	    state->red_bits);
	uint32_t green = scale_component((colour >> 8) & 0xff,
	    state->green_bits);
	uint32_t blue = scale_component(colour & 0xff, state->blue_bits);
	uint64_t value = ((uint64_t)red
	    << (state->green_bits + state->blue_bits))
	    | ((uint64_t)green << state->blue_bits) | blue;

	return value < (uint64_t)state->colour_count ? (int)value : COLOR_WHITE;
}

static int
nearest_colour(const struct curses_state *state, uint32_t rgb)
{
	int red = (int)((rgb >> 16) & 0xff) * 1000 / 255;
	int green = (int)((rgb >> 8) & 0xff) * 1000 / 255;
	int blue = (int)(rgb & 0xff) * 1000 / 255;
	uint64_t best_distance = UINT64_MAX;
	int best = COLOR_WHITE;
	int limit = state->colour_count < 256 ? state->colour_count : 256;

	for (int i = 0; i < limit; ++i) {
		int64_t red_difference;
		int64_t green_difference;
		int64_t blue_difference;
		uint64_t distance;

		if (!state->terminal_colours[i].valid)
			continue;
		red_difference = red - state->terminal_colours[i].red;
		green_difference = green - state->terminal_colours[i].green;
		blue_difference = blue - state->terminal_colours[i].blue;
		distance = (uint64_t)(red_difference * red_difference)
		    + (uint64_t)(green_difference * green_difference)
		    + (uint64_t)(blue_difference * blue_difference);
		if (distance < best_distance) {
			best_distance = distance;
			best = i;
		}
	}
	return best;
}

static int
rgb_colour(struct curses_state *state, uint32_t rgb)
{
	if (state->direct_colour)
		return direct_colour(state, rgb);
	for (size_t i = 0; i < sizeof(state->rgb_cache)
	    / sizeof(state->rgb_cache[0]); ++i) {
		if (state->rgb_cache[i].valid && state->rgb_cache[i].rgb == rgb)
			return state->rgb_cache[i].colour;
	}
	unsigned int slot = state->next_rgb_cache++
	    % (sizeof(state->rgb_cache) / sizeof(state->rgb_cache[0]));
	state->rgb_cache[slot].rgb = rgb;
	state->rgb_cache[slot].colour = nearest_colour(state, rgb);
	state->rgb_cache[slot].valid = true;
	return state->rgb_cache[slot].colour;
}

static int
cell_colour(struct curses_state *state, uint32_t colour)
{
	if (ST_CONSOLE_COLOR_IS_RGB(colour))
		return rgb_colour(state, colour & UINT32_C(0xffffff));
	return state->indexed_colours[colour & UINT32_C(0xff)];
}

static attr_t
cell_attributes(const struct curses_state *state,
	const struct st_console_cell *cell)
{
	attr_t attributes = A_NORMAL;

	if ((cell->flags & ST_CONSOLE_CELL_BOLD) != 0
	    || (!ST_CONSOLE_COLOR_IS_RGB(cell->foreground)
	    && (cell->foreground & 8) != 0 && state->colour_count < 16))
		attributes |= A_BOLD;
	if ((cell->flags & ST_CONSOLE_CELL_UNDERLINE) != 0)
		attributes |= A_UNDERLINE;
	if ((cell->flags & ST_CONSOLE_CELL_BLINK) != 0)
		attributes |= A_BLINK;
	if ((cell->flags & ST_CONSOLE_CELL_REVERSE) != 0)
		attributes |= A_REVERSE;
#ifdef A_INVIS
	if ((cell->flags & ST_CONSOLE_CELL_CONCEAL) != 0)
		attributes |= A_INVIS;
#endif
	return attributes;
}

static int
cell_pair(struct curses_state *state,
	const struct st_console_cell *cell)
{
	int foreground;
	int background;
	int pair;

	if (!state->colours)
		return 0;
	foreground = cell_colour(state, cell->foreground);
	background = cell_colour(state, cell->background);
	if (state->extended_pairs) {
		pair = state->api.call_find_pair(foreground, background);
		if (pair == ERR)
			pair = state->api.call_alloc_pair(foreground, background);
		return pair == ERR ? 0 : pair;
	}
	return foreground * state->colour_count + background + 1;
}

static void
xterm_terminal_colour(unsigned int colour, unsigned int colour_count,
	int *red, int *green, int *blue)
{
	static const unsigned char ansi[16][3] = {
		{0, 0, 0}, {128, 0, 0}, {0, 128, 0}, {128, 128, 0},
		{0, 0, 128}, {128, 0, 128}, {0, 128, 128}, {192, 192, 192},
		{128, 128, 128}, {255, 0, 0}, {0, 255, 0}, {255, 255, 0},
		{0, 0, 255}, {255, 0, 255}, {0, 255, 255}, {255, 255, 255},
	};
	static const unsigned char cube256[] = {0, 95, 135, 175, 215, 255};
	static const unsigned char cube88[] = {0, 139, 205, 255};
	static const unsigned char grey88[] = {
		46, 92, 115, 139, 162, 185, 208, 231,
	};
	unsigned int r;
	unsigned int g;
	unsigned int b;

	if (colour < 16) {
		r = ansi[colour][0];
		g = ansi[colour][1];
		b = ansi[colour][2];
	}
	else if (colour_count == 88 && colour < 80) {
		unsigned int value = colour - 16;

		r = cube88[value / 16];
		g = cube88[(value / 4) % 4];
		b = cube88[value % 4];
	}
	else if (colour_count == 88) {
		r = g = b = grey88[colour - 80];
	}
	else if (colour < 232) {
		unsigned int value = colour - 16;

		r = cube256[value / 36];
		g = cube256[(value / 6) % 6];
		b = cube256[value % 6];
	}
	else {
		r = g = b = 8 + (colour - 232) * 10;
	}
	*red = (int)(r * 1000 / 255);
	*green = (int)(g * 1000 / 255);
	*blue = (int)(b * 1000 / 255);
}

static void
read_terminal_colours(struct curses_state *state)
{
	int limit = state->colour_count < 256 ? state->colour_count : 256;

	for (int i = 0; i < limit; ++i) {
		NCURSES_COLOR_T red;
		NCURSES_COLOR_T green;
		NCURSES_COLOR_T blue;

		if (!state->palette_mutable
		    && (state->colour_count == 88 || state->colour_count >= 256)
		    && i >= 16) {
			xterm_terminal_colour((unsigned int)i,
			    (unsigned int)state->colour_count,
			    &state->terminal_colours[i].red,
			    &state->terminal_colours[i].green,
			    &state->terminal_colours[i].blue);
		}
		else if (state->api.call_color_content((NCURSES_COLOR_T)i, &red,
		    &green, &blue) == OK) {
			state->terminal_colours[i].red = red;
			state->terminal_colours[i].green = green;
			state->terminal_colours[i].blue = blue;
		}
		else
			xterm_terminal_colour((unsigned int)i,
			    (unsigned int)state->colour_count,
			    &state->terminal_colours[i].red,
			    &state->terminal_colours[i].green,
			    &state->terminal_colours[i].blue);
		state->terminal_colours[i].valid = true;
	}
}

static void
rebuild_colour_maps(st_console_t *console)
{
	struct curses_state *state = console->display->backend_state;

	memset(state->rgb_cache, 0, sizeof(state->rgb_cache));
	state->next_rgb_cache = 0;
	for (unsigned int i = 0; i < 256; ++i) {
		int terminal_index = i < 16 ? dos_colour(i) : (int)i;

		if (terminal_index < state->colour_count)
			state->indexed_colours[i] = terminal_index;
		else
			state->indexed_colours[i] = nearest_colour(state,
			    console->palette[i]);
	}
}

static chtype
ascii_cell(const struct st_console_cell *cell)
{
	bool alternate;
	chtype result = st_console_ascii_character(cell, &alternate);

	if (alternate)
		result |= A_ALTCHARSET;
	else if (cell->codepoint > 0 && cell->codepoint < 0x20)
		result |= A_REVERSE;
	return result;
}

static int
write_cell(struct curses_state *state, const struct st_console_cell *cell)
{
	if (state->presentation == ST_CONSOLE_TEXT_CP437)
		return state->api.call_addch(state->window,
		    (chtype)(cell->glyph <= UCHAR_MAX ? cell->glyph : '?'));
	if (state->presentation == ST_CONSOLE_TEXT_ASCII)
		return state->api.call_addch(state->window, ascii_cell(cell));
#if ST_CONSOLE_HAVE_CURSES_WIDE
	if (state->wide) {
		wchar_t value[2];

		if (cell->codepoint <= (uint32_t)WCHAR_MAX
		    && !(cell->codepoint >= UINT32_C(0xd800)
		    && cell->codepoint <= UINT32_C(0xdfff))) {
			value[0] = (wchar_t)cell->codepoint;
			value[1] = 0;
			return state->api.call_add_wide(state->window, value, 1);
		}
	}
#else
	(void)state;
#endif
	return state->api.call_addch(state->window, ascii_cell(cell));
}

static void
sgr_colour(struct sgr_writer *writer, uint32_t colour, bool background)
{
	if (!ST_CONSOLE_COLOR_IS_RGB(colour)) {
		uint32_t index = colour & UINT32_C(0xff);

		if (index < 16)
			index = (uint32_t)dos_colour(index);
		sgr_format(writer, background ? "\033[48;5;%" PRIu32 "m"
		    : "\033[38;5;%" PRIu32 "m", index, 0, 0);
		return;
	}
	colour &= UINT32_C(0xffffff);
	sgr_format(writer, background ? "\033[48;2;%" PRIu32
	    ";%" PRIu32 ";%" PRIu32 "m" : "\033[38;2;%" PRIu32
	    ";%" PRIu32 ";%" PRIu32 "m", (colour >> 16) & 0xff,
	    (colour >> 8) & 0xff, colour & 0xff);
}

static void
sgr_style(struct sgr_writer *writer, const struct st_console_cell *cell)
{

	sgr_emit(writer, "\033[0m", 4);
	if ((cell->flags & ST_CONSOLE_CELL_BOLD) != 0)
		sgr_emit(writer, "\033[1m", 4);
	if ((cell->flags & ST_CONSOLE_CELL_UNDERLINE) != 0)
		sgr_emit(writer, "\033[4m", 4);
	if ((cell->flags & ST_CONSOLE_CELL_BLINK) != 0)
		sgr_emit(writer, "\033[5m", 4);
	if ((cell->flags & ST_CONSOLE_CELL_REVERSE) != 0)
		sgr_emit(writer, "\033[7m", 4);
	if ((cell->flags & ST_CONSOLE_CELL_CONCEAL) != 0)
		sgr_emit(writer, "\033[8m", 4);
	sgr_colour(writer, cell->foreground, false);
	sgr_colour(writer, cell->background, true);
}

static void
sgr_character(struct sgr_writer *writer, const struct curses_state *state,
	const struct st_console_cell *cell)
{
	char encoded[4];
	ptrdiff_t length;
	uint32_t codepoint = cell->codepoint;

	if (state->presentation == ST_CONSOLE_TEXT_CP437) {
		unsigned char character = cell->glyph <= UCHAR_MAX
		    ? (unsigned char)cell->glyph : '?';

		sgr_emit(writer, &character, 1);
		return;
	}
	if (state->presentation == ST_CONSOLE_TEXT_ASCII) {
		chtype value = ascii_cell(cell);
		unsigned char character = (unsigned char)(value & A_CHARTEXT);

		if ((value & A_ALTCHARSET) != 0)
			sgr_emit(writer, "\016", 1);
		sgr_emit(writer, &character, 1);
		if ((value & A_ALTCHARSET) != 0)
			sgr_emit(writer, "\017", 1);
		return;
	}

	if (!st_unicode_scalar_valid(codepoint))
		codepoint = UINT32_C(0xfffd);
	length = st_utf8_encode_one(encoded, sizeof(encoded), codepoint);
	if (length < 0)
		writer->result = (int)length;
	else
		sgr_emit(writer, encoded, (size_t)length);
}

static bool
sgr_styles_equal(const struct st_console_cell *left,
	const struct st_console_cell *right)
{
	const uint32_t style_flags = ST_CONSOLE_CELL_BOLD
	    | ST_CONSOLE_CELL_UNDERLINE | ST_CONSOLE_CELL_BLINK
	    | ST_CONSOLE_CELL_REVERSE | ST_CONSOLE_CELL_CONCEAL;

	return left->foreground == right->foreground
	    && left->background == right->background
	    && ((left->flags ^ right->flags) & style_flags) == 0;
}

static int
sgr_present(st_console_t *console, struct curses_state *state)
{
	struct sgr_writer writer = {0};
	struct st_console_cell current_style = {0};
	bool style_known = false;
	bool position_known = false;
	bool initializing = !state->sgr_initialized;
	uint32_t output_x = 0;
	uint32_t output_y = 0;
	uint32_t visible_columns = atomic_load_explicit(&state->host_columns,
	    memory_order_acquire);
	uint32_t visible_rows = atomic_load_explicit(&state->host_rows,
	    memory_order_acquire);

	if (visible_columns == 0 || visible_columns > console->columns)
		visible_columns = console->columns;
	if (visible_rows == 0 || visible_rows > console->rows)
		visible_rows = console->rows;

	if (initializing) {
		const struct st_console_cell blank = {
			.codepoint = ' ',
			.glyph = ' ',
			.foreground = 7,
			.background = 0,
		};
		static const char clear_and_hide[] = "\033[2J\033[?25l";

		sgr_style(&writer, &blank);
		sgr_emit(&writer, clear_and_hide, sizeof(clear_and_hide) - 1);
		for (size_t index = 0;
		    index < (size_t)console->columns * console->rows; ++index)
			state->drawn[index] = blank;
		state->sgr_initialized = true;
	}
	for (uint32_t y = 0; y < visible_rows; ++y) {
		for (uint32_t x = 0; x < visible_columns; ++x) {
			size_t index = (size_t)y * console->columns + x;
			const struct st_console_cell *cell = &console->cells[index];

			if (((cell->flags & ST_CONSOLE_CELL_DIRTY) == 0 || initializing)
			    && st_console_cells_equal(cell, &state->drawn[index]))
				continue;
			state->drawn[index] = *cell;
			state->drawn[index].flags &= ~ST_CONSOLE_CELL_DIRTY;
			if ((cell->flags & ST_CONSOLE_CELL_CONTINUATION) != 0)
				continue;
			if (!position_known || output_x != x || output_y != y)
				sgr_format(&writer, "\033[%" PRIu32 ";%" PRIu32 "H",
				    y + 1, x + 1, 0);
			if (!style_known || !sgr_styles_equal(cell, &current_style)) {
				sgr_style(&writer, cell);
				current_style = *cell;
				style_known = true;
			}
			sgr_character(&writer, state, cell);
			output_x = x + 1;
			output_y = y;
			if (x + 1 < console->columns
			    && (console->cells[index + 1].flags
			    & ST_CONSOLE_CELL_CONTINUATION) != 0)
				++output_x;
			position_known = output_x < visible_columns;
		}
	}
	if (console->cursor_visible && console->cursor_x < visible_columns
	    && console->cursor_y < visible_rows) {
		uint32_t shape = console->cursor_shape == ST_CONSOLE_CURSOR_BLOCK
		    ? 2 : console->cursor_shape == ST_CONSOLE_CURSOR_UNDERLINE ? 4 : 6;

		sgr_format(&writer, "\033[%" PRIu32 ";%" PRIu32 "H",
		    console->cursor_y + 1, console->cursor_x + 1, 0);
		sgr_format(&writer, "\033[%" PRIu32 " q", shape, 0, 0);
		sgr_emit(&writer, "\033[0m\033[?25h", 10);
	}
	else
		sgr_emit(&writer, "\033[0m\033[?25l", 10);
	sgr_flush(&writer);
	if (writer.result == 0 && fflush(stdout) != 0)
		writer.result = IO_ERROR_IO;
	return writer.result;
}

static uint16_t
curses_key_code(int value)
{
	if (value == '\n' || value == '\r')
		return 13;
	if (value == '\b' || value == 0x7f)
		return 8;
	if (value > 0 && value <= 0x1b)
		return (uint16_t)value;
	switch (value) {
	case KEY_UP: return ST_CONSOLE_KEY_UP;
	case KEY_DOWN: return ST_CONSOLE_KEY_DOWN;
	case KEY_LEFT: return ST_CONSOLE_KEY_LEFT;
	case KEY_RIGHT: return ST_CONSOLE_KEY_RIGHT;
	case KEY_IC: return ST_CONSOLE_KEY_INSERT;
	case KEY_DC: return ST_CONSOLE_KEY_DELETE;
	#ifdef KEY_EIC
	case KEY_EIC: return ST_CONSOLE_KEY_INSERT;
	#endif
	case KEY_HOME: return ST_CONSOLE_KEY_HOME;
	case KEY_END: return ST_CONSOLE_KEY_END;
	case KEY_BACKSPACE: return 8;
	case KEY_ENTER: return 13;
	#ifdef KEY_A1
	case KEY_A1: return ST_CONSOLE_KEY_HOME;
	#endif
	#ifdef KEY_A3
	case KEY_A3: return ST_CONSOLE_KEY_PAGE_UP;
	#endif
	#ifdef KEY_C1
	case KEY_C1: return ST_CONSOLE_KEY_END;
	#endif
	#ifdef KEY_C3
	case KEY_C3: return ST_CONSOLE_KEY_PAGE_DOWN;
	#endif
	#ifdef KEY_BEG
	case KEY_BEG: return ST_CONSOLE_KEY_HOME;
	#endif
	#ifdef KEY_CANCEL
	case KEY_CANCEL: return 27;
	#endif
	#ifdef KEY_SELECT
	case KEY_SELECT: return ST_CONSOLE_KEY_END;
	#endif
	case KEY_PPAGE: return ST_CONSOLE_KEY_PAGE_UP;
	case KEY_NPAGE: return ST_CONSOLE_KEY_PAGE_DOWN;
	case KEY_F(1): return ST_CONSOLE_KEY_F1;
	case KEY_F(2): return ST_CONSOLE_KEY_F2;
	case KEY_F(3): return ST_CONSOLE_KEY_F3;
	case KEY_F(4): return ST_CONSOLE_KEY_F4;
	case KEY_F(5): return ST_CONSOLE_KEY_F5;
	case KEY_F(6): return ST_CONSOLE_KEY_F6;
	case KEY_F(7): return ST_CONSOLE_KEY_F7;
	case KEY_F(8): return ST_CONSOLE_KEY_F8;
	case KEY_F(9): return ST_CONSOLE_KEY_F9;
	case KEY_F(10): return ST_CONSOLE_KEY_F10;
	case KEY_F(11): return ST_CONSOLE_KEY_F11;
	case KEY_F(12): return ST_CONSOLE_KEY_F12;
	default: return 0;
	}
}

static bool
window_dimensions(struct curses_state *state, WINDOW *window,
	uint32_t *columns, uint32_t *rows)
{
	int terminal_columns;
	int terminal_rows;

	terminal_columns = state->api.call_getmaxx(window);
	terminal_rows = state->api.call_getmaxy(window);
	if (terminal_columns <= 0 || terminal_rows <= 0)
		return false;
	*columns = (uint32_t)terminal_columns;
	*rows = (uint32_t)terminal_rows;
	return true;
}

static bool
terminal_dimensions(struct curses_state *state, uint32_t *columns,
	uint32_t *rows)
{
	WINDOW *probe;
	bool result;

	if (state->api.call_resizeterm != NULL)
		(void)state->api.call_resizeterm(0, 0);
	probe = state->api.call_newwin(0, 0, 0, 0);
	if (probe == NULL)
		return false;
	result = window_dimensions(state, probe, columns, rows);
	(void)state->api.call_delwin(probe);
	return result;
}

#ifdef KEY_MOUSE
static bool
mouse_event(struct curses_state *state, struct st_console_event *event)
{
	MEVENT mouse;
	mmask_t buttons;
	uint32_t previous;

	if (!state->mouse_enabled || state->api.call_getmouse(&mouse) != OK)
		return false;
	buttons = mouse.bstate;
	previous = state->mouse_buttons;
#ifdef BUTTON1_PRESSED
	if ((buttons & BUTTON1_PRESSED) != 0)
		state->mouse_buttons |= ST_CONSOLE_MOUSE_BUTTON_LEFT;
#endif
#ifdef BUTTON1_RELEASED
	if ((buttons & BUTTON1_RELEASED) != 0)
		state->mouse_buttons &= ~ST_CONSOLE_MOUSE_BUTTON_LEFT;
#endif
#ifdef BUTTON2_PRESSED
	if ((buttons & BUTTON2_PRESSED) != 0)
		state->mouse_buttons |= ST_CONSOLE_MOUSE_BUTTON_MIDDLE;
#endif
#ifdef BUTTON2_RELEASED
	if ((buttons & BUTTON2_RELEASED) != 0)
		state->mouse_buttons &= ~ST_CONSOLE_MOUSE_BUTTON_MIDDLE;
#endif
#ifdef BUTTON3_PRESSED
	if ((buttons & BUTTON3_PRESSED) != 0)
		state->mouse_buttons |= ST_CONSOLE_MOUSE_BUTTON_RIGHT;
#endif
#ifdef BUTTON3_RELEASED
	if ((buttons & BUTTON3_RELEASED) != 0)
		state->mouse_buttons &= ~ST_CONSOLE_MOUSE_BUTTON_RIGHT;
#endif
	*event = (struct st_console_event){
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {
			.x = mouse.x,
			.y = mouse.y,
			.buttons = state->mouse_buttons,
			.action = previous == state->mouse_buttons
			    ? ST_CONSOLE_MOUSE_MOVE : ST_CONSOLE_MOUSE_BUTTON,
		},
	};
#ifdef BUTTON4_PRESSED
	if ((buttons & BUTTON4_PRESSED) != 0) {
		event->data.mouse.action = ST_CONSOLE_MOUSE_WHEEL;
		event->data.mouse.wheel_y = 1;
	}
#endif
#ifdef BUTTON5_PRESSED
	if ((buttons & BUTTON5_PRESSED) != 0) {
		event->data.mouse.action = ST_CONSOLE_MOUSE_WHEEL;
		event->data.mouse.wheel_y = -1;
	}
#endif
	return true;
}
#endif

static int
curses_present(st_console_t *console)
{
	struct curses_state *state = console->display->backend_state;
	uint32_t x;
	uint32_t y;
	int cursor;
	bool resized = atomic_exchange_explicit(&state->host_resized, false,
	    memory_order_acq_rel);
	uint32_t visible_columns = atomic_load_explicit(&state->host_columns,
	    memory_order_acquire);
	uint32_t visible_rows = atomic_load_explicit(&state->host_rows,
	    memory_order_acquire);
	int status;

	if (resized) {
		status = replace_window(state, visible_columns, visible_rows);
		if (status != 0) {
			atomic_store_explicit(&state->host_resized, true,
			    memory_order_release);
			return status;
		}
		memset(state->drawn, 0, (size_t)console->columns * console->rows
		    * sizeof(*state->drawn));
	}

	if (visible_columns == 0 || visible_columns > console->columns)
		visible_columns = console->columns;
	if (visible_rows == 0 || visible_rows > console->rows)
		visible_rows = console->rows;

	if (state->sgr_direct)
		return sgr_present(console, state);

	for (y = 0; y < visible_rows; ++y) {
		for (x = 0; x < visible_columns; ++x) {
			size_t index = (size_t)y * console->columns + x;
			const struct st_console_cell *cell = &console->cells[index];
			int pair;
			int write_result;

			if ((cell->flags & ST_CONSOLE_CELL_DIRTY) == 0
			    && st_console_cells_equal(cell, &state->drawn[index]))
				continue;
			if ((cell->flags & ST_CONSOLE_CELL_CONTINUATION) != 0) {
				state->drawn[index] = *cell;
				state->drawn[index].flags &= ~ST_CONSOLE_CELL_DIRTY;
				continue;
			}
			pair = cell_pair(state, cell);
			if (state->api.call_move(state->window, (int)y, (int)x) == ERR
			    || state->api.call_attributes(state->window,
				    cell_attributes(state, cell),
			    state->extended_pairs ? 0 : (NCURSES_PAIRS_T)pair,
			    state->extended_pairs ? &pair : NULL) == ERR)
				return IO_ERROR_IO;
			write_result = write_cell(state, cell);
			if (write_result == ERR && !(x + 1 == visible_columns
			    && y + 1 == visible_rows))
				return IO_ERROR_IO;
			state->drawn[index] = *cell;
			state->drawn[index].flags &= ~ST_CONSOLE_CELL_DIRTY;
		}
	}
	if (console->cursor_visible && console->cursor_x < visible_columns
	    && console->cursor_y < visible_rows
	    && state->api.call_move(state->window, (int)console->cursor_y,
	    (int)console->cursor_x) == ERR)
		return IO_ERROR_IO;
	cursor = !console->cursor_visible || console->cursor_x >= visible_columns
	    || console->cursor_y >= visible_rows ? 0
	    : console->cursor_shape == ST_CONSOLE_CURSOR_BLOCK ? 2 : 1;
	(void)state->api.call_cursor(cursor);
	return state->api.call_refresh(state->window) == ERR
	    ? IO_ERROR_IO : 0;
}

static int
curses_input(void *argument)
{
	struct curses_state *state = argument;
	st_console_display_t *display = state->display;
	const struct timespec idle = {0, 10000000};

	while (!atomic_load_explicit(&state->stop, memory_order_acquire)) {
		struct st_console_event event = {0};
		bool have_event = false;
		bool resize = false;
		uint32_t resize_columns = 0;
		uint32_t resize_rows = 0;
		bool have_key = false;
		bool have_text = false;
		uint32_t text_value = 0;
		int key_value = 0;
		int value;

		if (st_mutex_lock(&display->backend_mutex) != ST_THREAD_SUCCESS)
			break;
#if ST_CONSOLE_HAVE_CURSES_WIDE
		if (state->wide) {
			wint_t wide_value = 0;

			value = state->api.call_get_wide(state->window, &wide_value);
			if (value == OK
			    && st_unicode_scalar_valid((uint32_t)wide_value)) {
				if (wide_value < 0x20 || wide_value == 0x7f) {
					have_key = true;
					key_value = (int)wide_value;
				}
				else {
					have_text = true;
					text_value = (uint32_t)wide_value;
				}
			}
			else if (value == KEY_CODE_YES) {
				have_key = true;
				key_value = (int)wide_value;
			}
		}
		else
#endif
		{
			value = state->api.call_getch(state->window);
			if (value >= 0x20 && value < 0x7f) {
				have_text = true;
				text_value = (uint32_t)value;
			}
			else if (value >= 0 && value <= 0x7f) {
				have_key = true;
				key_value = value;
			}
			else if (value != ERR) {
				have_key = true;
				key_value = value;
			}
		}
		if (have_text) {
			event.type = ST_CONSOLE_EVENT_TEXT;
			event.data.text.codepoint = text_value;
			have_event = true;
		}
		else if (have_key) {
#ifdef KEY_RESIZE
			if (key_value == KEY_RESIZE)
				resize = terminal_dimensions(state, &resize_columns,
				    &resize_rows);
			else
#endif
#ifdef KEY_MOUSE
			if (key_value == KEY_MOUSE)
				have_event = mouse_event(state, &event);
			else
#endif
			{
				uint16_t key_code = curses_key_code(key_value);

				if (key_code != 0) {
					event.type = ST_CONSOLE_EVENT_KEY_CODE;
					event.data.key.code = key_code;
					have_event = true;
				}
			}
		}
		(void)st_mutex_unlock(&display->backend_mutex);
		if (resize) {
			resize = resize_columns != 0 && resize_rows != 0;
			if (resize) {
				atomic_store_explicit(&state->host_columns,
				    resize_columns, memory_order_release);
				atomic_store_explicit(&state->host_rows, resize_rows,
				    memory_order_release);
				atomic_store_explicit(&state->host_resized, true,
				    memory_order_release);
				st_console_display_request_repaint(display);
			}
		}
		if (resize) {
			struct st_console_event resize_event = {
				.type = ST_CONSOLE_EVENT_RESIZE,
				.data.resize = {
					.columns = resize_columns,
					.rows = resize_rows,
				},
			};

			(void)st_console_display_post_event(display, &resize_event);
		}
		if (have_event)
			(void)st_console_display_post_event(display, &event);
		else
			(void)st_thread_sleep(&idle, NULL);
	}
	return 0;
}

static void
curses_destroy(st_console_display_t *display)
{
	struct curses_state *state = display->backend_state;

	if (state == NULL)
		return;
	atomic_store_explicit(&state->stop, true, memory_order_release);
	if (state->input_started)
		(void)st_thread_join(state->input_thread, NULL);
	if (state->palette_mutable) {
		for (unsigned int i = 0; i < 256; ++i) {
			if (state->original_colour_valid[i])
				(void)state->api.call_init_color((NCURSES_COLOR_T)i,
				    state->original_colours[i][0],
				    state->original_colours[i][1],
				    state->original_colours[i][2]);
		}
	}
	if (state->window != NULL)
		(void)state->api.call_delwin(state->window);
	if (state->screen != NULL) {
		/* The normal curses path lets endwin() emit cnorm.  The direct-SGR
		 * path changes visibility and shape behind curses' back, so restore
		 * those explicitly after curses has returned to shell mode. */
		if (!state->sgr_direct)
			(void)state->api.call_cursor(1);
		(void)state->api.call_endwin();
		if (state->sgr_direct) {
			static const char restore_cursor[] =
			    "\033[0m\033[?25h\033[0 q";

			(void)fwrite(restore_cursor, 1,
			    sizeof(restore_cursor) - 1, stdout);
			(void)fflush(stdout);
		}
		state->api.call_delscreen(state->screen);
	}
	(void)dynamic_library_close(state->library);
	free(state->drawn);
	free(state);
	atomic_flag_clear_explicit(&curses_in_use, memory_order_release);
}

static int
curses_palette_changed(st_console_t *console, uint16_t first, uint16_t count)
{
	struct curses_state *state = console->display->backend_state;

	if (state->palette_mutable) {
		for (uint16_t offset = 0; offset < count; ++offset) {
			unsigned int index = (unsigned int)first + offset;
			unsigned int terminal_index = index < 16
			    ? (unsigned int)dos_colour(index) : index;
			uint32_t rgb;
			NCURSES_COLOR_T red;
			NCURSES_COLOR_T green;
			NCURSES_COLOR_T blue;

			if (terminal_index >= (unsigned int)state->colour_count
			    || terminal_index >= 256)
				continue;
			if (!state->original_colour_valid[terminal_index]) {
				if (state->api.call_color_content(
				    (NCURSES_COLOR_T)terminal_index,
				    &state->original_colours[terminal_index][0],
				    &state->original_colours[terminal_index][1],
				    &state->original_colours[terminal_index][2]) == ERR)
					return IO_ERROR_IO;
				state->original_colour_valid[terminal_index] = true;
			}
			rgb = console->palette[index];
			red = (NCURSES_COLOR_T)((((rgb >> 16) & 0xff) * 1000 + 127)
			    / 255);
			green = (NCURSES_COLOR_T)((((rgb >> 8) & 0xff) * 1000 + 127)
			    / 255);
			blue = (NCURSES_COLOR_T)(((rgb & 0xff) * 1000 + 127) / 255);
			if (state->api.call_init_color(
			    (NCURSES_COLOR_T)terminal_index, red, green, blue) == ERR)
				return IO_ERROR_IO;
		}
	}
	read_terminal_colours(state);
	rebuild_colour_maps(console);
	return 0;
}

static int
curses_resize(st_console_t *console, uint32_t columns, uint32_t rows)
{
	struct curses_state *state = console->display->backend_state;
	struct st_console_cell *drawn;
	size_t count;
	uint32_t host_columns;
	uint32_t host_rows;

	if (columns > INT_MAX || rows > INT_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	if ((size_t)columns > SIZE_MAX / (size_t)rows)
		return IO_ERROR_OUT_OF_RANGE;
	count = (size_t)columns * rows;
	drawn = calloc(count, sizeof(*drawn));
	if (drawn == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	host_columns = atomic_load_explicit(&state->host_columns,
	    memory_order_acquire);
	host_rows = atomic_load_explicit(&state->host_rows, memory_order_acquire);
	if (host_columns == 0)
		host_columns = columns;
	if (host_rows == 0)
		host_rows = rows;
	if (replace_window(state, host_columns, host_rows) != 0) {
		free(drawn);
		return IO_ERROR_OUT_OF_RANGE;
	}
	free(state->drawn);
	state->drawn = drawn;
	atomic_store_explicit(&state->host_resized, false, memory_order_release);
	return 0;
}

static int curses_attach(st_console_t *console);

static const struct st_console_backend_ops curses_ops = {
	.attach = curses_attach,
	.deferred_present = true,
	.present = curses_present,
	.resize = curses_resize,
	.palette_changed = curses_palette_changed,
	.destroy = curses_destroy,
};

static bool
load_required_api(struct curses_state *state)
{
	return LOAD_REQUIRED(state, call_newterm, "newterm")
	    && LOAD_REQUIRED(state, call_delscreen, "delscreen")
	    && LOAD_REQUIRED(state, call_endwin, "endwin")
	    && LOAD_REQUIRED(state, call_newwin, "newwin")
	    && LOAD_REQUIRED(state, call_delwin, "delwin")
	    && LOAD_REQUIRED(state, call_getmaxx, "getmaxx")
	    && LOAD_REQUIRED(state, call_getmaxy, "getmaxy")
	    && LOAD_REQUIRED(state, call_move, "wmove")
	    && LOAD_REQUIRED(state, call_addch, "waddch")
	    && LOAD_REQUIRED(state, call_refresh, "wrefresh")
	    && LOAD_REQUIRED(state, call_erase, "werase")
	    && LOAD_REQUIRED(state, call_cbreak, "cbreak")
	    && LOAD_REQUIRED(state, call_raw, "raw")
	    && LOAD_REQUIRED(state, call_noecho, "noecho")
	    && LOAD_REQUIRED(state, call_nonl, "nonl")
	    && LOAD_REQUIRED(state, call_keypad, "keypad")
	    && LOAD_REQUIRED(state, call_nodelay, "nodelay")
	    && LOAD_REQUIRED(state, call_scrollok, "scrollok")
	    && LOAD_REQUIRED(state, call_cursor, "curs_set")
	    && LOAD_REQUIRED(state, call_has_colors, "has_colors")
	    && LOAD_REQUIRED(state, call_can_change_color, "can_change_color")
	    && LOAD_REQUIRED(state, call_start_color, "start_color")
	    && LOAD_REQUIRED(state, call_color_content, "color_content")
	    && LOAD_REQUIRED(state, call_init_color, "init_color")
	    && LOAD_REQUIRED(state, call_init_pair, "init_pair")
	    && LOAD_REQUIRED(state, call_tigetflag, "tigetflag")
	    && LOAD_REQUIRED(state, call_tigetnum, "tigetnum")
	    && LOAD_REQUIRED(state, call_tigetstr, "tigetstr")
	    && LOAD_REQUIRED(state, call_attributes, "wattr_set")
	    && LOAD_REQUIRED(state, call_getch, "wgetch");
}

static bool
parse_bit_count(const char **text, unsigned int *result)
{
	unsigned int value = 0;
	const char *current = *text;

	if (*current < '0' || *current > '9')
		return false;
	do {
		unsigned int digit = (unsigned int)(*current - '0');

		if (value > (unsigned int)INT_MAX / 10
		    || value * 10 > (unsigned int)INT_MAX - digit)
			return false;
		value = value * 10 + digit;
		++current;
	} while (*current >= '0' && *current <= '9');
	if (value == 0 || value > 30)
		return false;
	*text = current;
	*result = value;
	return true;
}

static bool
direct_colour_bits(struct curses_state *state)
{
	char *value;
	const char *current;
	int numeric;
	unsigned int total = 0;
	uint64_t described_colours;

	if (state->api.call_tigetflag("RGB") > 0) {
		uint32_t maximum = (uint32_t)state->colour_count - 1;

		while (maximum != 0) {
			++total;
			maximum >>= 1;
		}
		state->red_bits = (total + 2) / 3;
		state->green_bits = (total + 1) / 3;
		state->blue_bits = total / 3;
		return total != 0 && total < 31;
	}
	numeric = state->api.call_tigetnum("RGB");
	if (numeric > 0 && numeric <= 10) {
		state->red_bits = (unsigned int)numeric;
		state->green_bits = (unsigned int)numeric;
		state->blue_bits = (unsigned int)numeric;
		total = (unsigned int)numeric * 3;
	}
	else {
		value = state->api.call_tigetstr("RGB");
		if (value == NULL || value == (char *)(intptr_t)-1)
			return false;
		current = value;
		if (!parse_bit_count(&current, &state->red_bits)
		    || *current++ != '/'
		    || !parse_bit_count(&current, &state->green_bits)
		    || *current++ != '/'
		    || !parse_bit_count(&current, &state->blue_bits)
		    || *current != '\0')
			return false;
		total = state->red_bits + state->green_bits
		    + state->blue_bits;
	}
	if (total == 0 || total >= 31)
		return false;
	described_colours = UINT64_C(1) << total;
	return described_colours == (uint64_t)state->colour_count;
}

static bool
ascii_equal_case_insensitive(const char *left, const char *right)
{
	for (;;) {
		unsigned char l = (unsigned char)*left++;
		unsigned char r = (unsigned char)*right++;

		if (l >= 'A' && l <= 'Z')
			l = (unsigned char)(l + ('a' - 'A'));
		if (r >= 'A' && r <= 'Z')
			r = (unsigned char)(r + ('a' - 'A'));
		if (l != r)
			return false;
		if (l == '\0')
			return true;
	}
}

static bool
colorterm_direct_colour(void)
{
	const char *value = getenv("COLORTERM");

	return value != NULL
	    && (ascii_equal_case_insensitive(value, "truecolor")
	    || ascii_equal_case_insensitive(value, "24bit"));
}

static int
curses_attach(st_console_t *console)
{
	if (console->columns > INT_MAX || console->rows > INT_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	st_pixel_buffer_destroy(console->pixels);
	free(console->raster_rows);
	free(console->rasterized);
	console->pixels = NULL;
	console->raster_rows = NULL;
	console->rasterized = NULL;
	console->cell_width = 0;
	console->cell_height = 0;
	console->pixel_extra_width = 0;
	console->pixel_extra_height = 0;
	console->pixel_top = 0;
	return 0;
}

int
st_console_ncurses_init(st_console_t *console)
{
	static const char *const names[] = {"ncursesw", "ncurses", "curses"};
	struct curses_state *state;
	size_t count;
	uint32_t terminal_columns;
	uint32_t terminal_rows;
	int result;

	if (console->columns > INT_MAX || console->rows > INT_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	if (atomic_flag_test_and_set_explicit(&curses_in_use,
	    memory_order_acquire))
		return IO_ERROR_BUSY;
	state = calloc(1, sizeof(*state));
	if (state == NULL) {
		atomic_flag_clear_explicit(&curses_in_use, memory_order_release);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	atomic_init(&state->stop, false);
	atomic_init(&state->host_columns, 0);
	atomic_init(&state->host_rows, 0);
	atomic_init(&state->host_resized, false);
	state->display = console->display;
	state->presentation = console->display->config.text_presentation;
	result = dynamic_library_open(&state->library, names,
	    sizeof(names) / sizeof(names[0]), 10, DYNAMIC_LIBRARY_LOCAL);
	if (result != 0 || !load_required_api(state)) {
		(void)dynamic_library_close(state->library);
		free(state);
		atomic_flag_clear_explicit(&curses_in_use, memory_order_release);
		return result != 0 ? result : IO_ERROR_NOT_SUPPORTED;
	}
	if (LOAD_OPTIONAL(state, call_use_extended_names, "use_extended_names"))
		(void)state->api.call_use_extended_names(true);
	(void)LOAD_OPTIONAL(state, call_resizeterm, "resizeterm");
#if ST_CONSOLE_HAVE_CURSES_WIDE
	state->wide = LOAD_REQUIRED(state, call_add_wide, "waddnwstr")
	    && LOAD_REQUIRED(state, call_get_wide, "wget_wch");
#endif
	state->screen = state->api.call_newterm(NULL, stdout, stdin);
	if (state->screen == NULL) {
		(void)dynamic_library_close(state->library);
		free(state);
		atomic_flag_clear_explicit(&curses_in_use, memory_order_release);
		return IO_ERROR_NOT_SUPPORTED;
	}
	state->window = state->api.call_newwin(0, 0, 0, 0);
	if (state->window == NULL || !window_dimensions(state, state->window,
	    &terminal_columns, &terminal_rows)) {
		if (state->window != NULL)
			(void)state->api.call_delwin(state->window);
		(void)state->api.call_endwin();
		state->api.call_delscreen(state->screen);
		(void)dynamic_library_close(state->library);
		free(state);
		atomic_flag_clear_explicit(&curses_in_use, memory_order_release);
		return IO_ERROR_OUT_OF_RANGE;
	}
	count = (size_t)terminal_columns * terminal_rows;
	atomic_store_explicit(&state->host_columns, terminal_columns,
	    memory_order_release);
	atomic_store_explicit(&state->host_rows, terminal_rows,
	    memory_order_release);
	state->drawn = calloc(count, sizeof(*state->drawn));
	if (state->drawn == NULL) {
		(void)state->api.call_delwin(state->window);
		(void)state->api.call_endwin();
		state->api.call_delscreen(state->screen);
		(void)dynamic_library_close(state->library);
		free(state);
		atomic_flag_clear_explicit(&curses_in_use, memory_order_release);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	if (state->api.call_cbreak() == ERR || state->api.call_raw() == ERR
	    || state->api.call_noecho() == ERR || state->api.call_nonl() == ERR) {
		(void)state->api.call_delwin(state->window);
		(void)state->api.call_endwin();
		state->api.call_delscreen(state->screen);
		(void)dynamic_library_close(state->library);
		free(state->drawn);
		free(state);
		atomic_flag_clear_explicit(&curses_in_use, memory_order_release);
		return IO_ERROR_NOT_SUPPORTED;
	}
	(void)state->api.call_keypad(state->window, true);
	(void)state->api.call_nodelay(state->window, true);
	(void)state->api.call_scrollok(state->window, false);
	state->colours = state->api.call_has_colors()
	    && state->api.call_start_color() != ERR;
	if (state->colours) {
		state->colour_count = state->api.call_tigetnum("colors");
		state->pair_count = state->api.call_tigetnum("pairs");
		state->extended_pairs = state->colour_count > 0
		    && state->pair_count > 0
		    && LOAD_OPTIONAL(state, call_alloc_pair, "alloc_pair")
		    && LOAD_OPTIONAL(state, call_find_pair, "find_pair");
		state->direct_colour = state->extended_pairs
		    && direct_colour_bits(state);
		state->palette_mutable = !state->direct_colour
		    && state->colour_count > 0
		    && state->api.call_can_change_color();
		if (!state->extended_pairs) {
			int usable = state->colour_count < 16
			    ? state->colour_count : 16;

			while (usable > 0 && usable * usable >= state->pair_count)
				--usable;
			state->colour_count = usable;
			if (usable == 0)
				state->colours = false;
			for (int pair = 0; pair < usable * usable; ++pair)
				(void)state->api.call_init_pair(
				    (NCURSES_PAIRS_T)(pair + 1),
				    (NCURSES_COLOR_T)(pair / usable),
				    (NCURSES_COLOR_T)(pair % usable));
		}
	}
	state->sgr_direct = !state->direct_colour
	    && colorterm_direct_colour();
	if (state->sgr_direct)
		state->palette_mutable = false;
	if (state->presentation == ST_CONSOLE_TEXT_UNICODE && !state->wide
	    && !state->sgr_direct)
		state->presentation = ST_CONSOLE_TEXT_ASCII;
	console->display->backend_state = state;
	console->display->backend_ops = &curses_ops;
	result = curses_palette_changed(console, 0, 256);
	if (result != 0) {
		curses_destroy(console->display);
		console->display->backend_state = NULL;
		console->display->backend_ops = NULL;
		return result;
	}
#ifdef KEY_MOUSE
	state->api.call_mousemask = (curses_call_mousemask_fn)
	    dynamic_library_symbol(state->library, "mousemask");
	state->api.call_mouseinterval = (curses_call_mouseinterval_fn)
	    dynamic_library_symbol(state->library, "mouseinterval");
	state->api.call_getmouse = (curses_call_getmouse_fn)
	    dynamic_library_symbol(state->library, "getmouse");
	if (state->api.call_mousemask != NULL
	    && state->api.call_mouseinterval != NULL
	    && state->api.call_getmouse != NULL) {
		mmask_t requested = 0;

	#ifdef BUTTON1_PRESSED
		requested |= BUTTON1_PRESSED | BUTTON1_RELEASED;
	#endif
	#ifdef BUTTON2_PRESSED
		requested |= BUTTON2_PRESSED | BUTTON2_RELEASED;
	#endif
	#ifdef BUTTON3_PRESSED
		requested |= BUTTON3_PRESSED | BUTTON3_RELEASED;
	#endif
	#ifdef BUTTON4_PRESSED
		requested |= BUTTON4_PRESSED;
	#endif
	#ifdef BUTTON5_PRESSED
		requested |= BUTTON5_PRESSED;
	#endif
	#ifdef REPORT_MOUSE_POSITION
		requested |= REPORT_MOUSE_POSITION;
	#endif
		state->mouse_enabled = requested != 0
		    && (state->api.call_mousemask(requested, NULL) & requested)
		    != 0;
		if (state->mouse_enabled)
			(void)state->api.call_mouseinterval(0);
	}
#endif
	(void)curses_attach(console);
	if (terminal_columns != console->columns
	    || terminal_rows != console->rows) {
		result = st_console_resize(console, terminal_columns, terminal_rows);
		if (result != 0) {
			curses_destroy(console->display);
			console->display->backend_state = NULL;
			console->display->backend_ops = NULL;
			return result;
		}
	}
	result = st_thread_create(&state->input_thread, curses_input, state);
	if (result != ST_THREAD_SUCCESS) {
		curses_destroy(console->display);
		console->display->backend_state = NULL;
		console->display->backend_ops = NULL;
		return IO_ERROR_IO;
	}
	state->input_started = true;
	return 0;
}

#else

int
st_console_ncurses_init(st_console_t *console)
{
	(void)console;
	return IO_ERROR_NOT_SUPPORTED;
}

#endif
