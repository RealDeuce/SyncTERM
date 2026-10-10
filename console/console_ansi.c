/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_internal.h"
#include "console_ansi_system.h"

#include "dynamic_library.h"
#include "io_error.h"
#include "st_codepage.h"
#include "st_utf8.h"

#include <inttypes.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ansi_state {
	struct st_console_ansi_io io;
	struct st_console_ansi_system *system;
	st_console_display_t *display;
	struct st_console_cell *drawn;
	st_thread_t input_thread;
	atomic_bool stop;
	atomic_uint host_columns;
	atomic_uint host_rows;
	atomic_bool host_resized;
	uint32_t columns;
	uint32_t rows;
	unsigned int indexed_colours;
	bool direct_colour;
	enum st_console_text_presentation presentation;
	unsigned char escape[64];
	size_t escape_length;
	unsigned int escape_idle;
	struct st_utf8_decoder utf8_decoder;
	uint32_t utf8_modifiers;
	unsigned int utf8_idle;
	unsigned int doorway_phase;
	bool doorway_enabled;
	bool input_started;
	bool initialized;
	bool last_was_carriage_return;
	bool invalid_colour;
	uint32_t invalid_colour_value;
};

struct ansi_writer {
	struct ansi_state *state;
	unsigned char data[8192];
	size_t length;
	int result;
};

static int
stdio_write(void *context, const void *data, size_t size)
{
	FILE *stream = context;

	return fwrite(data, 1, size, stream) == size && fflush(stream) == 0
	    ? 0 : IO_ERROR_IO;
}

static void
flush(struct ansi_writer *writer)
{
	if (writer->result != 0 || writer->length == 0)
		return;
	writer->result = writer->state->io.write(writer->state->io.context,
	    writer->data, writer->length);
	writer->length = 0;
}

static void
emit(struct ansi_writer *writer, const void *data, size_t length)
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
			flush(writer);
	}
}

static void
emit_format(struct ansi_writer *writer, const char *format, uint32_t first,
	uint32_t second, uint32_t third)
{
	char buffer[96];
	int length = snprintf(buffer, sizeof(buffer), format, first, second, third);

	if (length < 0 || (size_t)length >= sizeof(buffer))
		writer->result = IO_ERROR_IO;
	else
		emit(writer, buffer, (size_t)length);
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

struct ansi_colour_capabilities {
	unsigned int indexed_colours;
	bool direct_colour;
};

static struct ansi_colour_capabilities
terminal_colour_capabilities(void)
{
	static const char *const names[] = {
		"ncursesw", "ncurses", "tinfo", "curses",
	};
	typedef int (*setupterm_fn)(const char *, int, int *);
	typedef int (*tigetnum_fn)(const char *);
	typedef int (*tigetflag_fn)(const char *);
	dynamic_library_t *library = NULL;
	setupterm_fn call_setupterm;
	tigetnum_fn call_tigetnum;
	tigetflag_fn call_tigetflag;
	const char *colorterm = getenv("COLORTERM");
	struct ansi_colour_capabilities result = {.indexed_colours = 16};
	int error = 0;

	if (colorterm != NULL
	    && (ascii_equal_case_insensitive(colorterm, "truecolor")
	    || ascii_equal_case_insensitive(colorterm, "24bit")))
		result.direct_colour = true;
	if (dynamic_library_open(&library, names,
	    sizeof(names) / sizeof(names[0]), 10, DYNAMIC_LIBRARY_LOCAL) != 0)
		return result;
	call_setupterm = (setupterm_fn)dynamic_library_symbol(library,
	    "setupterm");
	call_tigetnum = (tigetnum_fn)dynamic_library_symbol(library, "tigetnum");
	call_tigetflag = (tigetflag_fn)dynamic_library_symbol(library,
	    "tigetflag");
	if (call_setupterm != NULL && call_tigetnum != NULL
	    && call_setupterm(NULL, 1, &error) == 0) {
		int colours = call_tigetnum("colors");

		if (colours > 0)
			result.indexed_colours = colours < 16 ? 16
			    : colours > 256 ? 256 : (unsigned int)colours;
		if (call_tigetflag != NULL && call_tigetflag("RGB") > 0)
			result.direct_colour = true;
	}
	(void)dynamic_library_close(library);
	return result;
}

static void
xterm_colour(unsigned int colour, unsigned int colour_count,
	unsigned int *red, unsigned int *green, unsigned int *blue)
{
	static const unsigned char ansi[16][3] = {
		{0, 0, 0}, {128, 0, 0}, {0, 128, 0}, {128, 128, 0},
		{0, 0, 128}, {128, 0, 128}, {0, 128, 128}, {192, 192, 192},
		{128, 128, 128}, {255, 0, 0}, {0, 255, 0}, {255, 255, 0},
		{0, 0, 255}, {255, 0, 255}, {0, 255, 255}, {255, 255, 255},
	};
	static const unsigned char cube[] = {0, 95, 135, 175, 215, 255};
	static const unsigned char cube88[] = {0, 139, 205, 255};
	static const unsigned char grey88[] = {
		46, 92, 115, 139, 162, 185, 208, 231,
	};

	if (colour < 16) {
		*red = ansi[colour][0];
		*green = ansi[colour][1];
		*blue = ansi[colour][2];
	}
	else if (colour_count == 88 && colour < 80) {
		unsigned int value = colour - 16;

		*red = cube88[value / 16];
		*green = cube88[(value / 4) % 4];
		*blue = cube88[value % 4];
	}
	else if (colour_count == 88)
		*red = *green = *blue = grey88[colour - 80];
	else if (colour < 232) {
		unsigned int value = colour - 16;

		*red = cube[value / 36];
		*green = cube[(value / 6) % 6];
		*blue = cube[value % 6];
	}
	else
		*red = *green = *blue = 8 + (colour - 232) * 10;
}

static unsigned int
nearest_xterm_colour(uint32_t rgb, unsigned int count)
{
	int red = (int)((rgb >> 16) & 0xff);
	int green = (int)((rgb >> 8) & 0xff);
	int blue = (int)(rgb & 0xff);
	uint64_t best_distance = UINT64_MAX;
	unsigned int best = 7;

	for (unsigned int i = 0; i < count; ++i) {
		unsigned int candidate_red;
		unsigned int candidate_green;
		unsigned int candidate_blue;
		int64_t red_difference;
		int64_t green_difference;
		int64_t blue_difference;
		uint64_t distance;

		xterm_colour(i, count, &candidate_red, &candidate_green,
		    &candidate_blue);
		red_difference = red - (int)candidate_red;
		green_difference = green - (int)candidate_green;
		blue_difference = blue - (int)candidate_blue;
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

static void
emit_indexed_colour(struct ansi_writer *writer, unsigned int index,
	bool background)
{
	if (writer->state->indexed_colours > 16) {
		emit_format(writer, background ? "\033[48;5;%" PRIu32 "m"
		    : "\033[38;5;%" PRIu32 "m", index, 0, 0);
	}
	else {
		unsigned int code = index % 8 + (background ? 40 : 30);

		if (index >= 8)
			code += 60;
		emit_format(writer, "\033[%" PRIu32 "m", code, 0, 0);
	}
}

static void
emit_colour(struct ansi_writer *writer, const st_console_t *console,
	uint32_t colour, bool background)
{
	uint32_t rgb;

	if (!st_console_colour_rgb_locked(console, colour, &rgb)
	    && !writer->state->invalid_colour) {
		writer->state->invalid_colour = true;
		writer->state->invalid_colour_value = colour;
	}
	if (writer->state->direct_colour) {
		emit_format(writer, background ? "\033[48;2;%" PRIu32
		    ";%" PRIu32 ";%" PRIu32 "m" : "\033[38;2;%" PRIu32
		    ";%" PRIu32 ";%" PRIu32 "m", (rgb >> 16) & 0xff,
		    (rgb >> 8) & 0xff, rgb & 0xff);
	}
	else {
		unsigned int count = writer->state->indexed_colours;
		unsigned int index = nearest_xterm_colour(rgb, count);

		emit_indexed_colour(writer, index, background);
	}
}

static void
emit_style(struct ansi_writer *writer, const st_console_t *console,
	const struct st_console_cell *cell)
{
	emit(writer, "\033[0m", 4);
	if ((cell->flags & ST_CONSOLE_CELL_BOLD) != 0)
		emit(writer, "\033[1m", 4);
	if ((cell->flags & ST_CONSOLE_CELL_UNDERLINE) != 0)
		emit(writer, "\033[4m", 4);
	if ((cell->flags & ST_CONSOLE_CELL_BLINK) != 0)
		emit(writer, "\033[5m", 4);
	if ((cell->flags & ST_CONSOLE_CELL_REVERSE) != 0)
		emit(writer, "\033[7m", 4);
	if ((cell->flags & ST_CONSOLE_CELL_CONCEAL) != 0)
		emit(writer, "\033[8m", 4);
	emit_colour(writer, console, cell->foreground, false);
	emit_colour(writer, console, cell->background, true);
}

static void
emit_character(struct ansi_writer *writer, st_console_t *console,
	const struct st_console_cell *cell)
{
	uint32_t codepoint;

	(void)st_console_cell_codepoint_locked(console, cell, &codepoint);
	if (writer->state->presentation == ST_CONSOLE_TEXT_CP437) {
		unsigned char character = st_console_cp437_character(codepoint);

		if (writer->state->doorway_enabled && character < 0x20)
			emit(writer, "\0", 1);
		emit(writer, &character, 1);
	}
	else if (writer->state->presentation == ST_CONSOLE_TEXT_ASCII) {
		bool alternate;
		unsigned char character = st_console_ascii_character(console, cell,
		    &alternate);

		if (alternate)
			emit(writer, "\016", 1);
		emit(writer, &character, 1);
		if (alternate)
			emit(writer, "\017", 1);
	}
	else {
		char local[32];
		char *encoded = local;
		size_t length;

		if (writer->state->doorway_enabled && codepoint < 0x20)
			emit(writer, "\0", 1);
		if (!st_console_font_glyph_text_locked(console, cell->glyph_font,
		    cell->glyph, local, sizeof(local), &length))
			length = (size_t)st_utf8_encode_one(local, sizeof(local),
			    UINT32_C(0xfffd));
		else if (length > sizeof(local)) {
			encoded = malloc(length);
			if (encoded == NULL || !st_console_font_glyph_text_locked(console,
			    cell->glyph_font, cell->glyph, encoded, length, &length)) {
				free(encoded);
				encoded = local;
				length = (size_t)st_utf8_encode_one(local,
				    sizeof(local), UINT32_C(0xfffd));
			}
		}
		emit(writer, encoded, length);
		if (encoded != local)
			free(encoded);
	}
}

static bool
styles_equal(const struct st_console_cell *left,
	const struct st_console_cell *right)
{
	const uint32_t flags = ST_CONSOLE_CELL_BOLD | ST_CONSOLE_CELL_UNDERLINE
	    | ST_CONSOLE_CELL_BLINK | ST_CONSOLE_CELL_REVERSE
	    | ST_CONSOLE_CELL_CONCEAL;

	return left->foreground == right->foreground
	    && left->background == right->background
	    && ((left->flags ^ right->flags) & flags) == 0;
}

static void
prepare_initial_screen(struct ansi_writer *writer, st_console_t *console,
	struct ansi_state *state)
{
	const struct st_console_cell blank = {
		.glyph = ' ', .foreground = 7, .background = 0,
	};
	static const char clear_and_hide[] = "\033[2J\033[H\033[?25l";

	emit_style(writer, console, &blank);
	emit(writer, clear_and_hide, sizeof(clear_and_hide) - 1);
	for (size_t i = 0; i < (size_t)console->columns * console->rows; ++i)
		state->drawn[i] = blank;
	state->initialized = true;
}

static bool
scroll_up_one(struct ansi_writer *writer, st_console_t *console,
	struct ansi_state *state)
{
	const struct st_console_rect *window = &console->window;
	uint32_t host_rows = atomic_load_explicit(&state->host_rows,
	    memory_order_acquire);
	size_t row_size;

	if (!state->initialized || window->x != 0
	    || window->width != console->columns || window->height < 2
	    || (host_rows != 0 && window->y + window->height > host_rows))
		return false;
	row_size = (size_t)console->columns * sizeof(*state->drawn);
	for (uint32_t y = 0; y + 1 < window->height; ++y) {
		const struct st_console_cell *current = &console->cells[
		    (size_t)(window->y + y) * console->columns];
		const struct st_console_cell *previous = &state->drawn[
		    (size_t)(window->y + y + 1) * console->columns];

		for (uint32_t x = 0; x < console->columns; ++x)
			if (!st_console_cells_equal(&current[x], &previous[x]))
				return false;
	}
	emit_format(writer, "\033[%" PRIu32 ";%" PRIu32 "r",
	    window->y + 1, window->y + window->height, 0);
	{
		static const char scroll[] = "\033[1S\033[r";

		emit(writer, scroll, sizeof(scroll) - 1);
	}
	memmove(&state->drawn[(size_t)window->y * console->columns],
	    &state->drawn[(size_t)(window->y + 1) * console->columns],
	    (size_t)(window->height - 1) * row_size);
	memset(&state->drawn[(size_t)(window->y + window->height - 1)
	    * console->columns], 0, row_size);
	return true;
}

static int
ansi_present(st_console_t *console)
{
	struct ansi_state *state = console->display->backend_state;
	struct ansi_writer writer = {.state = state};
	struct st_console_cell current_style = {0};
	bool style_known = false;
	bool position_known = false;
	bool initializing;
	uint32_t output_x = 0;
	uint32_t output_y = 0;
	uint32_t cursor_x;
	uint32_t cursor_columns;
	bool resized = atomic_exchange_explicit(&state->host_resized, false,
	    memory_order_acq_rel);
	uint32_t visible_columns = atomic_load_explicit(&state->host_columns,
	    memory_order_acquire);
	uint32_t visible_rows = atomic_load_explicit(&state->host_rows,
	    memory_order_acquire);

	state->invalid_colour = false;
	if (resized) {
		state->initialized = false;
		memset(state->drawn, 0, (size_t)state->columns * state->rows
		    * sizeof(*state->drawn));
	}
	initializing = !state->initialized;
	if (visible_columns == 0 || visible_columns > console->columns)
		visible_columns = console->columns;
	if (visible_rows == 0 || visible_rows > console->rows)
		visible_rows = console->rows;
	st_console_cursor_span_locked(console, &cursor_x, &cursor_columns);
	(void)cursor_columns;

	if (initializing)
		prepare_initial_screen(&writer, console, state);
	else
		(void)scroll_up_one(&writer, console, state);
	for (uint32_t y = 0; y < visible_rows; ++y) {
		for (uint32_t x = 0; x < visible_columns; ++x) {
			size_t index = (size_t)y * console->columns + x;
			const struct st_console_cell *source = &console->cells[index];
			struct st_console_cell effective;
			const struct st_console_cell *cell;

			if (((source->flags & ST_CONSOLE_CELL_DIRTY) == 0 || initializing)
			    && st_console_cells_equal(source, &state->drawn[index]))
				continue;
			state->drawn[index] = *source;
			state->drawn[index].flags &= ~ST_CONSOLE_CELL_DIRTY;
			st_console_effective_cell_locked(console, source, &effective);
			cell = &effective;
			if ((cell->flags & ST_CONSOLE_CELL_CONTINUATION) != 0)
				continue;
			if (!position_known || output_x != x || output_y != y)
				emit_format(&writer, "\033[%" PRIu32 ";%" PRIu32 "H",
				    y + 1, x + 1, 0);
			if (!style_known || !styles_equal(cell, &current_style)) {
				emit_style(&writer, console, cell);
				current_style = *cell;
				style_known = true;
			}
			emit_character(&writer, console, cell);
			output_x = x + 1;
			output_y = y;
			if (x + 1 < console->columns
			    && (console->cells[index + 1].flags
			    & ST_CONSOLE_CELL_CONTINUATION) != 0)
				++output_x;
			position_known = output_x < visible_columns;
		}
	}
	emit_format(&writer, "\033[%" PRIu32 ";%" PRIu32 "H",
	    console->cursor_y + 1, cursor_x + 1, 0);
	if (console->cursor_visible && cursor_x < visible_columns
	    && console->cursor_y < visible_rows) {
		uint32_t shape = console->cursor_shape == ST_CONSOLE_CURSOR_BLOCK
		    ? 2 : console->cursor_shape == ST_CONSOLE_CURSOR_UNDERLINE ? 4 : 6;

		emit_format(&writer, "\033[%" PRIu32 " q", shape, 0, 0);
		emit(&writer, "\033[0m\033[?25h", 10);
	}
	else
		emit(&writer, "\033[0m\033[?25l", 10);
	flush(&writer);
	if (state->invalid_colour)
		fprintf(stderr, "Invalid colour value: %08" PRIx32 "\n",
		    state->invalid_colour_value);
	return writer.result;
}

static void
ansi_palette_changed(st_console_t *console, uint16_t first,
	const uint32_t *colours, uint32_t count)
{
	struct ansi_state *state = console->display->backend_state;

	(void)first;
	(void)colours;
	(void)count;
	state->initialized = false;
}

static uint32_t
ansi_capabilities(const st_console_t *console)
{
	const struct ansi_state *state = console->display->backend_state;
	uint32_t result = 0;

	if (state->direct_colour)
		result |= ST_CONSOLE_CAP_PALETTE_CHANGE
		    | ST_CONSOLE_CAP_EXTENDED_PALETTE;
	if (state->direct_colour || state->indexed_colours >= 16)
		result |= ST_CONSOLE_CAP_BRIGHT_BACKGROUND;
	return result;
}

static void
post_key(st_console_display_t *display, uint16_t code, uint32_t modifiers)
{
	const struct st_console_event event = {
		.type = ST_CONSOLE_EVENT_KEY_CODE,
		.data.key = {.code = code, .modifiers = modifiers},
	};

	(void)st_console_display_post_event(display, &event);
}

static void
post_text(st_console_display_t *display, uint32_t codepoint,
	uint32_t modifiers)
{
	const struct st_console_event event = {
		.type = ST_CONSOLE_EVENT_TEXT,
		.data.text = {.codepoint = codepoint, .modifiers = modifiers},
	};

	(void)st_console_display_post_event(display, &event);
}

static void
post_doorway_key(st_console_display_t *display, unsigned char scan_code,
	bool enhanced)
{
	const struct st_console_event event = {
		.type = ST_CONSOLE_EVENT_DOORWAY_KEY,
		.data.doorway_key = {
			.scan_code = scan_code,
			.enhanced = enhanced,
		},
	};

	(void)st_console_display_post_event(display, &event);
}

static void
flush_input_text(st_console_display_t *display, struct ansi_state *state)
{
	struct st_utf8_decode_event event;

	if (st_utf8_decoder_flush(&state->utf8_decoder, &event) == 0
	    && event.kind == ST_UTF8_DECODE_EVENT_MALFORMED)
		post_text(display, UINT32_C(0xfffd), state->utf8_modifiers);
	state->utf8_idle = 0;
}

static void
input_text_byte(st_console_display_t *display, struct ansi_state *state,
	unsigned char byte, uint32_t modifiers)
{
	struct st_utf8_decode_event event;
	size_t consumed;

	if (st_utf8_decoder_incomplete(&state->utf8_decoder)
	    && modifiers != state->utf8_modifiers)
		flush_input_text(display, state);
	if (st_utf8_decoder_incomplete(&state->utf8_decoder)) {
		(void)st_utf8_decoder_feed(&state->utf8_decoder, &byte, 1,
		    &consumed, &event);
		if (event.kind == ST_UTF8_DECODE_EVENT_SCALAR) {
			post_text(display, event.data.scalar, state->utf8_modifiers);
			state->utf8_idle = 0;
		}
		else if (event.kind == ST_UTF8_DECODE_EVENT_MALFORMED) {
			post_text(display, UINT32_C(0xfffd), state->utf8_modifiers);
			state->utf8_idle = 0;
		}
		if (consumed == 0)
			input_text_byte(display, state, byte, modifiers);
		return;
	}
	if (byte < 0x20 || byte == 0x7f) {
		uint16_t code = byte == '\n' || byte == '\r' ? 13
		    : byte == 0x7f ? 8 : byte;

		if (byte == '\n' && state->last_was_carriage_return) {
			state->last_was_carriage_return = false;
			return;
		}
		state->last_was_carriage_return = byte == '\r';
		post_key(display, code, modifiers);
		return;
	}
	state->last_was_carriage_return = false;
	if (byte < 0x80) {
		post_text(display, byte, modifiers);
		return;
	}
	if (state->presentation == ST_CONSOLE_TEXT_CP437) {
		post_text(display, st_codepage_to_unicode(ST_CODEPAGE_CP437,
		    byte, false), modifiers);
		return;
	}
	if (state->presentation == ST_CONSOLE_TEXT_ASCII) {
		post_text(display, UINT32_C(0xfffd), modifiers);
		return;
	}
	state->utf8_modifiers = modifiers;
	state->utf8_idle = 0;
	(void)st_utf8_decoder_feed(&state->utf8_decoder, &byte, 1,
	    &consumed, &event);
	if (event.kind == ST_UTF8_DECODE_EVENT_SCALAR)
		post_text(display, event.data.scalar, modifiers);
	else if (event.kind == ST_UTF8_DECODE_EVENT_MALFORMED)
		post_text(display, UINT32_C(0xfffd), modifiers);
}

static uint32_t
xterm_modifiers(unsigned int parameter)
{
	unsigned int value = parameter > 0 ? parameter - 1 : 0;
	uint32_t result = 0;

	if ((value & 1) != 0)
		result |= ST_CONSOLE_MODIFIER_SHIFT;
	if ((value & 2) != 0)
		result |= ST_CONSOLE_MODIFIER_ALT;
	if ((value & 4) != 0)
		result |= ST_CONSOLE_MODIFIER_CONTROL;
	return result;
}

static bool
parse_escape(st_console_display_t *display, struct ansi_state *state)
{
	const unsigned char *sequence = state->escape;
	size_t length = state->escape_length;
	unsigned int parameters[3] = {0, 0, 0};
	size_t parameter_count = 0;
	uint16_t code = 0;
	uint32_t modifiers = 0;
	unsigned char final;

	if (length == 2 && sequence[1] != '[' && sequence[1] != 'O') {
		input_text_byte(display, state, sequence[1],
		    ST_CONSOLE_MODIFIER_ALT);
		return true;
	}
	if (length < 3)
		return false;
	final = sequence[length - 1];
	if (final < 0x40 || final > 0x7e)
		return false;
	if (sequence[1] == 'O') {
		switch (final) {
		case 'A': code = ST_CONSOLE_KEY_UP; break;
		case 'B': code = ST_CONSOLE_KEY_DOWN; break;
		case 'C': code = ST_CONSOLE_KEY_RIGHT; break;
		case 'D': code = ST_CONSOLE_KEY_LEFT; break;
		case 'H': code = ST_CONSOLE_KEY_HOME; break;
		case 'F': code = ST_CONSOLE_KEY_END; break;
		case 'P': code = ST_CONSOLE_KEY_F1; break;
		case 'Q': code = ST_CONSOLE_KEY_F2; break;
		case 'R': code = ST_CONSOLE_KEY_F3; break;
		case 'S': code = ST_CONSOLE_KEY_F4; break;
		case 'M': code = 13; break;
		default: break;
		}
	}
	else if (sequence[1] == '[') {
		unsigned int value = 0;
		bool have_value = false;
		bool valid = true;

		for (size_t i = 2; i + 1 < length; ++i) {
			if (sequence[i] >= '0' && sequence[i] <= '9') {
				unsigned int digit = (unsigned int)(sequence[i] - '0');

				if (value > (UINT_MAX - digit) / 10) {
					valid = false;
					break;
				}
				value = value * 10 + digit;
				have_value = true;
			}
			else if (sequence[i] == ';') {
				if (parameter_count == sizeof(parameters)
				    / sizeof(parameters[0])) {
					valid = false;
					break;
				}
				parameters[parameter_count++] = have_value ? value : 0;
				value = 0;
				have_value = false;
			}
			else {
				valid = false;
				break;
			}
		}
		if (!valid || parameter_count == sizeof(parameters)
		    / sizeof(parameters[0]))
			goto raw;
		parameters[parameter_count++] = have_value ? value : 0;
		if (parameter_count > 1)
			modifiers = xterm_modifiers(parameters[1]);
		switch (final) {
		case 'A': code = ST_CONSOLE_KEY_UP; break;
		case 'B': code = ST_CONSOLE_KEY_DOWN; break;
		case 'C': code = ST_CONSOLE_KEY_RIGHT; break;
		case 'D': code = ST_CONSOLE_KEY_LEFT; break;
		case 'H': code = ST_CONSOLE_KEY_HOME; break;
		case 'F': code = ST_CONSOLE_KEY_END; break;
		case 'Z': code = 9; modifiers |= ST_CONSOLE_MODIFIER_SHIFT; break;
		case '~':
			switch (parameters[0]) {
			case 1: case 7: code = ST_CONSOLE_KEY_HOME; break;
			case 2: code = ST_CONSOLE_KEY_INSERT; break;
			case 3: code = ST_CONSOLE_KEY_DELETE; break;
			case 4: case 8: code = ST_CONSOLE_KEY_END; break;
			case 5: code = ST_CONSOLE_KEY_PAGE_UP; break;
			case 6: code = ST_CONSOLE_KEY_PAGE_DOWN; break;
			case 11: code = ST_CONSOLE_KEY_F1; break;
			case 12: code = ST_CONSOLE_KEY_F2; break;
			case 13: code = ST_CONSOLE_KEY_F3; break;
			case 14: code = ST_CONSOLE_KEY_F4; break;
			case 15: code = ST_CONSOLE_KEY_F5; break;
			case 17: code = ST_CONSOLE_KEY_F6; break;
			case 18: code = ST_CONSOLE_KEY_F7; break;
			case 19: code = ST_CONSOLE_KEY_F8; break;
			case 20: code = ST_CONSOLE_KEY_F9; break;
			case 21: code = ST_CONSOLE_KEY_F10; break;
			case 23: code = ST_CONSOLE_KEY_F11; break;
			case 24: code = ST_CONSOLE_KEY_F12; break;
			default: break;
			}
			break;
		default: break;
		}
	}
	if (code != 0)
		post_key(display, code, modifiers);
	else {
	raw:
		post_key(display, 27, 0);
		for (size_t i = 1; i < length; ++i)
			input_text_byte(display, state, sequence[i], 0);
	}
	return true;
}

static void
finish_input(st_console_display_t *display, struct ansi_state *state)
{
	if (state->escape_length != 0) {
		post_key(display, 27, 0);
		for (size_t i = 1; i < state->escape_length; ++i)
			input_text_byte(display, state, state->escape[i], 0);
		state->escape_length = 0;
	}
	if (st_utf8_decoder_incomplete(&state->utf8_decoder))
		flush_input_text(display, state);
	state->doorway_phase = 0;
}

static void
input_byte(st_console_display_t *display, struct ansi_state *state,
	unsigned char byte)
{
	if (state->doorway_phase != 0) {
		if (state->doorway_phase == 1 && byte == 0xe0) {
			state->doorway_phase = 2;
			return;
		}
		post_doorway_key(display, byte, state->doorway_phase == 2);
		state->doorway_phase = 0;
		return;
	}
	if (byte == 0) {
		if (st_utf8_decoder_incomplete(&state->utf8_decoder))
			flush_input_text(display, state);
		if (state->escape_length != 0) {
			post_key(display, 27, 0);
			for (size_t i = 1; i < state->escape_length; ++i)
				input_text_byte(display, state, state->escape[i], 0);
			state->escape_length = 0;
		}
		state->doorway_phase = 1;
		return;
	}
	if (st_utf8_decoder_incomplete(&state->utf8_decoder)) {
		input_text_byte(display, state, byte, state->utf8_modifiers);
		return;
	}
	if (state->escape_length == 0) {
		if (byte == 0x1b) {
			state->escape[0] = byte;
			state->escape_length = 1;
			state->escape_idle = 0;
		}
		else
			input_text_byte(display, state, byte, 0);
		return;
	}
	if (state->escape_length == sizeof(state->escape)) {
		post_key(display, 27, 0);
		state->escape_length = 0;
		input_byte(display, state, byte);
		return;
	}
	state->escape[state->escape_length++] = byte;
	state->escape_idle = 0;
	if (parse_escape(display, state))
		state->escape_length = 0;
}

static int
ansi_input(void *argument)
{
	struct ansi_state *state = argument;
	st_console_display_t *display = state->display;
	unsigned char bytes[64];
	uint32_t known_columns = state->columns;
	uint32_t known_rows = state->rows;

	while (!atomic_load_explicit(&state->stop, memory_order_acquire)) {
		size_t received = 0;
		uint32_t columns;
		uint32_t rows;

		int result = st_console_ansi_system_read(state->system, bytes,
		    sizeof(bytes), &received, 25);
		for (size_t i = 0; i < received; ++i)
			input_byte(display, state, bytes[i]);
		if (result != 0) {
			if (!atomic_load_explicit(&state->stop,
			    memory_order_acquire)) {
				const struct st_console_event event = {
					.type = ST_CONSOLE_EVENT_CLOSE,
				};

				finish_input(display, state);
				(void)st_console_display_post_event(display, &event);
			}
			break;
		}
		if (received == 0 && state->escape_length != 0
		    && ++state->escape_idle >= 2) {
			post_key(display, 27, 0);
			for (size_t i = 1; i < state->escape_length; ++i)
				input_text_byte(display, state, state->escape[i], 0);
			state->escape_length = 0;
		}
		if (received == 0
		    && st_utf8_decoder_incomplete(&state->utf8_decoder)
		    && ++state->utf8_idle >= 2) {
			flush_input_text(display, state);
		}
		if (st_console_ansi_system_size(state->system, &columns, &rows)
		    && (columns != known_columns || rows != known_rows)) {
			const struct st_console_event event = {
				.type = ST_CONSOLE_EVENT_RESIZE,
				.data.resize = {.columns = columns, .rows = rows},
			};

			known_columns = columns;
			known_rows = rows;
			atomic_store_explicit(&state->host_columns, columns,
			    memory_order_release);
			atomic_store_explicit(&state->host_rows, rows,
			    memory_order_release);
			atomic_store_explicit(&state->host_resized, true,
			    memory_order_release);
			st_console_display_request_repaint(display);
			(void)st_console_display_post_event(display, &event);
		}
	}
	return 0;
}

static int
ansi_resize(st_console_t *console, uint32_t columns, uint32_t rows)
{
	struct ansi_state *state = console->display->backend_state;
	struct st_console_cell *drawn;

	if (rows == 0 || (size_t)columns > SIZE_MAX / rows)
		return IO_ERROR_OUT_OF_RANGE;
	drawn = calloc((size_t)columns * rows, sizeof(*drawn));
	if (drawn == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	free(state->drawn);
	state->drawn = drawn;
	state->columns = columns;
	state->rows = rows;
	if (state->system == NULL) {
		atomic_store_explicit(&state->host_columns, columns,
		    memory_order_release);
		atomic_store_explicit(&state->host_rows, rows,
		    memory_order_release);
	}
	state->initialized = false;
	return 0;
}

static void
ansi_destroy(st_console_display_t *display)
{
	struct ansi_state *state = display->backend_state;
	static const char restore[] = "\033[r\033[0m\033[?25h\033[0 q";
	static const char disable_doorway[] = "\033[=255l";

	if (state == NULL)
		return;
	atomic_store_explicit(&state->stop, true, memory_order_release);
	st_console_ansi_system_stop(state->system);
	if (state->input_started)
		(void)st_thread_join(state->input_thread, NULL);
	(void)state->io.write(state->io.context, restore, sizeof(restore) - 1);
	if (state->doorway_enabled)
		(void)state->io.write(state->io.context, disable_doorway,
		    sizeof(disable_doorway) - 1);
	st_console_ansi_system_close(state->system);
	free(state->drawn);
	free(state);
}

static const struct st_console_backend_ops ansi_ops = {
	.present = ansi_present,
	.resize = ansi_resize,
	.palette_changed = ansi_palette_changed,
	.capabilities = ansi_capabilities,
	.destroy = ansi_destroy,
};

static const struct st_console_backend_ops ansi_deferred_ops = {
	.deferred_present = true,
	.present = ansi_present,
	.resize = ansi_resize,
	.palette_changed = ansi_palette_changed,
	.capabilities = ansi_capabilities,
	.destroy = ansi_destroy,
};

int
st_console_ansi_init(st_console_t *console,
	const struct st_console_ansi_io *io)
{
	struct ansi_state *state;
	bool input_available = false;
	uint32_t columns;
	uint32_t rows;
	int result;

	if (io != NULL && (io->write == NULL
	    || io->indexed_colours > 256))
		return IO_ERROR_INVALID_ARGUMENT;
	state = calloc(1, sizeof(*state));
	if (state == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	atomic_init(&state->stop, false);
	atomic_init(&state->host_columns, 0);
	atomic_init(&state->host_rows, 0);
	atomic_init(&state->host_resized, false);
	state->display = console->display;
	state->presentation = console->display->config.text_presentation;
	if (io != NULL) {
		state->io = *io;
		state->indexed_colours = io->indexed_colours == 0 ? 16
		    : io->indexed_colours;
		state->direct_colour = io->direct_colour;
	}
	else {
		struct ansi_colour_capabilities capabilities;

		state->io.write = stdio_write;
		state->io.context = stdout;
		capabilities = terminal_colour_capabilities();
		state->indexed_colours = capabilities.indexed_colours;
		state->direct_colour = capabilities.direct_colour;
		result = st_console_ansi_system_open(&state->system,
		    &input_available,
		    state->presentation != ST_CONSOLE_TEXT_CP437);
		if (result != 0) {
			free(state);
			return result;
		}
		if (input_available) {
			static const char enable_doorway[] = "\033[=255h";

			result = state->io.write(state->io.context, enable_doorway,
			    sizeof(enable_doorway) - 1);
			if (result != 0) {
				st_console_ansi_system_close(state->system);
				free(state);
				return result;
			}
			state->doorway_enabled = true;
		}
	}
	console->display->backend_ops = io == NULL ? &ansi_deferred_ops : &ansi_ops;
	console->display->backend_state = state;
	result = ansi_resize(console, console->columns, console->rows);
	if (result == 0 && state->system != NULL
	    && st_console_ansi_system_size(state->system, &columns, &rows)
	    && (columns != console->columns || rows != console->rows)) {
		atomic_store_explicit(&state->host_columns, columns,
		    memory_order_release);
		atomic_store_explicit(&state->host_rows, rows,
		    memory_order_release);
		result = st_console_resize(console, columns, rows);
	}
	if (result == 0 && input_available) {
		result = st_thread_create(&state->input_thread, ansi_input, state);
		if (result == ST_THREAD_SUCCESS)
			state->input_started = true;
		else
			result = IO_ERROR_IO;
	}
	if (result != 0) {
		ansi_destroy(console->display);
		console->display->backend_ops = NULL;
		console->display->backend_state = NULL;
	}
	return result;
}
