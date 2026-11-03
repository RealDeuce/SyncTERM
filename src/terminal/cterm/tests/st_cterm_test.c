/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_cterm.h"
#include "cterm.h"
#include "cterm_dec.h"

#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, \
		    #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (false)

struct output {
	uint8_t bytes[4096];
	size_t size;
	size_t responses;
	size_t user;
};

static void
capture(void *context, enum st_cterm_output_origin origin,
	const uint8_t *bytes, size_t size)
{
	struct output *output = context;

	CHECK(size <= sizeof(output->bytes) - output->size);
	memcpy(output->bytes + output->size, bytes, size);
	output->size += size;
	if (origin == ST_CTERM_OUTPUT_RESPONSE)
		++output->responses;
	else
		++output->user;
}

static void
feed(st_cterm_t *terminal, const void *bytes, size_t size)
{
	size_t consumed;

	CHECK(st_cterm_write(terminal, bytes, size, &consumed) == 0);
	CHECK(consumed == size);
}

struct input_filter_state {
	uint32_t seen[16];
	size_t count;
	char captured[64];
	size_t captured_size;
};

static bool
input_filter_active(void *context)
{
	(void)context;
	return true;
}

static int
filter_input(void *context, uint32_t scalar, bool *consumed,
	char *replacement, size_t capacity, size_t *replacement_size)
{
	struct input_filter_state *state = context;
	const char *text = NULL;
	size_t size = 0;

	if (state->count < sizeof(state->seen) / sizeof(state->seen[0]))
		state->seen[state->count++] = scalar;
	*consumed = scalar == 'Q' || scalar == 0xe9 || scalar == '\n';
	*replacement_size = 0;
	if (scalar == 0xe9) { text = "X"; size = 1; }
	if (scalar == '\n') { text = "\r\n"; size = 2; }
	if (text != NULL) {
		if (size > capacity)
			return IO_ERROR_OUT_OF_RANGE;
		memcpy(replacement, text, size);
		*replacement_size = size;
	}
	return 0;
}

static int
capture_input(void *context, const char *utf8, size_t size, bool protocol)
{
	struct input_filter_state *state = context;

	if (protocol)
		return 0;
	if (size > sizeof(state->captured) - state->captured_size)
		return IO_ERROR_OUT_OF_RANGE;
	memcpy(state->captured + state->captured_size, utf8, size);
	state->captured_size += size;
	return 0;
}

static uint16_t
crc16(const void *data, size_t size)
{
	const uint8_t *bytes = data;
	uint16_t crc = 0;

	for (size_t i = 0; i < size; ++i) {
		crc ^= (uint16_t)bytes[i] << 8;
		for (unsigned bit = 0; bit < 8; ++bit)
			crc = (crc & UINT16_C(0x8000)) != 0
			    ? (uint16_t)((crc << 1) ^ UINT16_C(0x1021))
			    : (uint16_t)(crc << 1);
	}
	return crc;
}

static void
test_profile_presentation(void)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {
		.columns = 20, .rows = 4, .cell_width = 8, .cell_height = 16,
	};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	for (unsigned custom = 0; custom < 2; ++custom) {
		st_cterm_t *terminal = NULL;
		uint32_t palette[16];
		uint32_t actual;
		struct st_cterm_state state;
		for (unsigned i = 0; i < 16; ++i)
			palette[i] = 0x123400 + i;
		const struct st_cterm_config config = {
			.console = console, .fonts = fonts, .encoding = ST_CTERM_ENCODING_UTF8,
			.force_lcf = true, .yellow_is_yellow = true,
			.palette_override = custom ? palette : NULL,
		};
		CHECK(st_cterm_create(&terminal, &config) == 0);
		for (unsigned reset = 0; reset < 2; ++reset) {
			CHECK(st_cterm_state(terminal, &state) == 0);
			CHECK(state.last_column_flag.forced && state.last_column_flag.enabled);
			CHECK(state.has_palette_override == (custom != 0));
			CHECK(st_console_get_palette(console, 19, &actual, 1) == 0);
			CHECK(actual == (custom ? palette[6] : 0xa8a800));
			CHECK(st_cterm_music_mode(terminal) == 1);
			st_cterm_set_music_mode(terminal, 2);
			CHECK(st_cterm_music_mode(terminal) == 2);
			CHECK(st_cterm_reset_terminal(terminal) == 0);
		}
		st_cterm_destroy(&terminal);
	}
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
}

static void
test_lf_expand(void)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {
		.columns = 20, .rows = 4, .cell_width = 8, .cell_height = 16,
	};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	static const enum st_cterm_encoding encodings[] = {
		ST_CTERM_ENCODING_UTF8, ST_CTERM_ENCODING_CODEPAGE,
	};

	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	for (size_t encoding = 0; encoding < sizeof(encodings) / sizeof(encodings[0]); ++encoding) {
		for (unsigned enabled = 0; enabled < 2; ++enabled) {
			st_cterm_t *terminal = NULL;
			struct output output = {0};
			const struct st_cterm_config config = {
				.console = console, .fonts = fonts, .emulation = ST_CTERM_ANSI_BBS,
				.encoding = encodings[encoding], .codepage = ST_CODEPAGE_CP437,
				.expand_lf = enabled != 0, .quiet = true,
				.output = capture, .output_context = &output,
			};
			CHECK(st_cterm_create(&terminal, &config) == 0);
			for (unsigned split = 0; split < 2; ++split) {
				static const char input[] = "AB\nC\r\nD";
				struct st_cterm_state state;
				struct st_console_cell cell;
				const struct st_console_rect area = {enabled ? 0 : 2, 1, 1, 1};
				char text[8];
				size_t size;

				/* Reset must retain this local entry policy. */
				CHECK(st_cterm_reset_terminal(terminal) == 0);
				feed(terminal, "\x1b[2J\x1b[H", 7);
				if (split) {
					for (size_t i = 0; i < sizeof(input) - 1; ++i)
						feed(terminal, input + i, 1);
				} else {
					feed(terminal, input, sizeof(input) - 1);
				}
				CHECK(st_console_read_cells(console, &area, &cell, 1) == 0);
				CHECK(st_console_font_glyph_text(console, cell.glyph_font, cell.glyph,
				    text, sizeof(text), &size) == 0 && size == 1 && text[0] == 'C');
				CHECK(st_cterm_state(terminal, &state) == 0);
				CHECK(state.x == 2 && state.y == 3);
				CHECK(output.size == 0);
			}
			st_cterm_destroy(&terminal);
		}
	}
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
}

static void
test_disconnect_boundary(void)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {
		.columns = 40, .rows = 4, .cell_width = 8, .cell_height = 16,
	};
	for (int status_type = 0; status_type <= 2; ++status_type) {
		st_console_t *console = NULL;
		st_font_catalog_t *fonts = NULL;
		st_cterm_t *terminal = NULL;
		struct output output = {0};
		struct st_console_cell *before = NULL, *after = NULL;
		uint32_t before_width, after_width;
		size_t before_rows, after_rows;
		CHECK(st_console_create(&console, &display, &geometry) == 0);
		CHECK(st_font_catalog_create(&fonts, console) == 0);
		const struct st_cterm_config config = {
			.console = console, .fonts = fonts, .emulation = ST_CTERM_ANSI_BBS,
			.encoding = ST_CTERM_ENCODING_UTF8, .codepage = ST_CODEPAGE_CP437,
			.terminal_rows = 4, .scrollback_lines = 10,
			.output = capture, .output_context = &output, .quiet = true,
		};
		CHECK(st_cterm_create(&terminal, &config) == 0);
		if (status_type == 1) feed(terminal, "\x1b[1$~", 5);
		if (status_type == 2) feed(terminal, "\x1b[2$~", 5);
		uint32_t columns, rows;
		CHECK(st_cterm_size(terminal, &columns, &rows) == 0);
		for (uint32_t y = 0; y < rows; ++y) {
			CHECK(st_console_set_position(console, 0, y) == 0);
			CHECK(st_console_put_codepoint(console, 'A' + y) == 0);
		}
		/* Disconnect must preserve host-set margins and terminal geometry. */
		feed(terminal, "\x1b[2;2r", 6);
		CHECK(st_cterm_copy_history(terminal, &before, &before_width, &before_rows) == 0);
		struct st_cterm_state before_state, after_state;
		CHECK(st_cterm_state(terminal, &before_state) == 0);
		size_t output_size = output.size;
		CHECK(st_cterm_prepare_disconnect(terminal) == 0);
		CHECK(output.size == output_size && st_cterm_status_display(terminal) == status_type);
		uint32_t after_columns, after_height;
		CHECK(st_cterm_size(terminal, &after_columns, &after_height) == 0);
		CHECK(after_columns == columns && after_height == rows);
		CHECK(st_cterm_state(terminal, &after_state) == 0);
		CHECK(before_state.top_margin == after_state.top_margin
		    && before_state.bottom_margin == after_state.bottom_margin
		    && before_state.left_margin == after_state.left_margin
		    && before_state.right_margin == after_state.right_margin
		    && before_state.x == after_state.x && before_state.y == after_state.y);
		CHECK(st_cterm_copy_history(terminal, &after, &after_width, &after_rows) == 0);
		CHECK(before_width == 40 && after_width == 40 && after_rows == before_rows);
		CHECK(memcmp(before, after, before_rows * 40 * sizeof(*before)) == 0);
		free(before); free(after);
		st_cterm_destroy(&terminal);
		st_font_catalog_destroy(fonts);
		st_console_destroy(console);
	}
}

static void
test_zero_width_text(void)
{
	enum { COLUMNS = 80, ROWS = 43, CELLS = COLUMNS * ROWS };
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {
		.columns = COLUMNS, .rows = ROWS, .cell_width = 8, .cell_height = 8,
		.pixel_width = 640, .pixel_height = 350,
	};
	const struct st_console_rect full = {0, 0, COLUMNS, ROWS};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	struct input_filter_state captured = {0};
	struct st_console_cell before[CELLS], after[CELLS];
	struct st_cterm_state state;
	uint32_t x, y;

	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config config = {
		.console = console, .fonts = fonts, .emulation = ST_CTERM_ANSI_BBS,
		.encoding = ST_CTERM_ENCODING_UTF8, .codepage = ST_CODEPAGE_CP437,
		.quiet = true, .capture = capture_input,
		.rip_version = ST_CTERM_RIP_1,
		.input_filter = filter_input, .input_filter_active = input_filter_active,
		.input_filter_context = &captured,
		.capture_active = input_filter_active, .capture_context = &captured,
	};
	CHECK(st_cterm_create(&terminal, &config) == 0);
	static const char input[] = "A\xe2\x81\xa0\xef\xbb\xbf" "B";
	for (size_t i = 0; i < sizeof(input) - 1; ++i)
		feed(terminal, input + i, 1);
	CHECK(st_console_get_position(console, &x, &y) == 0 && x == 2 && y == 0);
	CHECK(captured.captured_size == sizeof(input) - 1
	    && memcmp(captured.captured, input, sizeof(input) - 1) == 0);
	CHECK(captured.count == 4 && captured.seen[0] == 'A'
	    && captured.seen[1] == 0x2060 && captured.seen[2] == 0xfeff
	    && captured.seen[3] == 'B');
	/* Preserve the pending ANSI wrap even at the bottom-right corner. */
	feed(terminal, "\x1b[43;80HZ", 9);
	CHECK(st_cterm_state(terminal, &state) == 0 && state.last_column_flag.set);
	CHECK(st_console_read_cells(console, &full, before, COLUMNS) == 0);
	static const char zero[] = "\xe2\x81\xa0\xef\xbb\xbf\0";
	for (size_t i = 0; i < sizeof(zero) - 1; ++i)
		feed(terminal, zero + i, 1);
	CHECK(st_console_get_position(console, &x, &y) == 0 && x == COLUMNS - 1 && y == ROWS - 1);
	CHECK(st_cterm_state(terminal, &state) == 0 && state.last_column_flag.set);
	CHECK(st_console_read_cells(console, &full, after, COLUMNS) == 0);
	CHECK(memcmp(before, after, sizeof(before)) == 0);
	/* Prepend U+0600 is initially zero width. A following base revises the
	 * same cluster and must draw at the current position, not a stale one. */
	feed(terminal, "\x1b[1;2H", 6);
	feed(terminal, "\xd8\x80", 2);
	CHECK(st_console_get_position(console, &x, &y) == 0 && x == 1 && y == 0);
	feed(terminal, "A", 1);
	CHECK(st_console_get_position(console, &x, &y) == 0 && x == 2 && y == 0);
	/* Doorway literal-byte cells bypass Unicode text suppression. */
	static const char doorway[] = "\x1b[1;1H\x1b[=255h\0\0";
	feed(terminal, doorway, sizeof(doorway) - 1);
	const struct st_console_rect first = {0, 0, 1, 1};
	struct st_console_cell literal;
	CHECK(st_console_read_cells(console, &first, &literal, 1) == 0);
	CHECK(literal.glyph == 0 && (literal.flags & ST_CONSOLE_CELL_CONTINUATION) == 0);
	CHECK(st_console_get_position(console, &x, &y) == 0 && x == 1 && y == 0);
	st_cterm_destroy(&terminal);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
}

static void
test_status_display_routing(void)
{
	static const struct {
		const char *stream;
		const char *main_text;
		char status_text;
		int type;
	} cases[] = {
		{"M\033[2$~\033[1$}S\033[0$}N", "MN", 'S', 2},
		{"M\033[2$~\033[1$}S\033[1$~N", "MN", ' ', 1},
		{"M\033[2$~\033[1$}S\033[0$~N\033[2$~\033[1$}T\033[0$}O",
		    "MNO", 'T', 2},
	};
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config geometry = {
		.columns = 20, .rows = 4, .cell_width = 8, .cell_height = 16,
	};
	const enum st_cterm_encoding encodings[] = {
		ST_CTERM_ENCODING_UTF8, ST_CTERM_ENCODING_CODEPAGE,
	};

	for (size_t encoding = 0; encoding < sizeof(encodings) / sizeof(encodings[0]); ++encoding) {
		for (size_t scenario = 0; scenario < sizeof(cases) / sizeof(cases[0]); ++scenario) {
			const char *stream = cases[scenario].stream;
			size_t length = strlen(stream);
			size_t main_length = strlen(cases[scenario].main_text);
			for (size_t chunk = 1; chunk <= length; ++chunk) {
				st_console_t *console = NULL;
				st_font_catalog_t *fonts = NULL;
				st_cterm_t *terminal = NULL;
				struct st_console_cell cells[3];
				const struct st_console_rect main_row = {0, 0, (uint32_t)main_length, 1};
				const struct st_console_rect status_row = {0, 3, 1, 1};
				char text[8];
				size_t required;

				CHECK(st_console_create(&console, &display, &geometry) == 0);
				CHECK(st_font_catalog_create(&fonts, console) == 0);
				const struct st_cterm_config config = {
					.console = console, .fonts = fonts,
					.emulation = ST_CTERM_ANSI_BBS, .encoding = encodings[encoding],
					.codepage = ST_CODEPAGE_CP437, .status_line = true,
					.quiet = true,
				};
				CHECK(st_cterm_create(&terminal, &config) == 0);
				for (size_t offset = 0; offset < length;) {
					size_t size = length - offset;

					if (size > chunk)
						size = chunk;
					feed(terminal, stream + offset, size);
					offset += size;
				}
				CHECK(st_cterm_finish(terminal) == 0);
				CHECK(st_cterm_status_display(terminal) == cases[scenario].type);
				CHECK(st_console_read_cells(console, &main_row, cells, main_length) == 0);
				for (size_t i = 0; i < main_length; ++i) {
					CHECK(st_console_font_glyph_text(console, cells[i].glyph_font,
					    cells[i].glyph, text, sizeof(text), &required) == 0);
					if (required != 1 || text[0] != cases[scenario].main_text[i])
						fprintf(stderr, "status routing: encoding=%d scenario=%zu chunk=%zu column=%zu\n",
						    (int)encodings[encoding], scenario, chunk, i);
					CHECK(required == 1 && text[0] == cases[scenario].main_text[i]);
				}
				CHECK(st_console_read_cells(console, &status_row, cells, 1) == 0);
				CHECK(st_console_font_glyph_text(console, cells[0].glyph_font,
				    cells[0].glyph, text, sizeof(text), &required) == 0);
				if (required != 1 || text[0] != cases[scenario].status_text)
					fprintf(stderr, "status row: encoding=%d scenario=%zu chunk=%zu actual=%.*s\n",
					    (int)encodings[encoding], scenario, chunk, (int)required, text);
				CHECK(required == 1 && text[0] == cases[scenario].status_text);
				st_cterm_destroy(&terminal);
				st_font_catalog_destroy(fonts);
				st_console_destroy(console);
			}
		}
	}
}

int
main(void)
{
	test_status_display_routing();
	test_lf_expand();
	test_profile_presentation();
	test_disconnect_boundary();
	test_zero_width_text();
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config console_config = {
		.columns = 20,
		.rows = 4,
		.cell_width = 8,
		.cell_height = 16,
		.wrap = true,
	};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	struct output output = {0};
	struct st_console_cell cell;
	struct st_console_rect one = {0, 0, 1, 1};
	uint32_t palette[2];
	char text[8];
	size_t required;

	CHECK(st_console_create(&console, &display, &console_config) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	/* Standalone parsers retain speed state without session callbacks;
	 * both displays share the terminal's communication speed. */
	for (unsigned encoding = 0; encoding < 2; ++encoding) {
		const struct st_cterm_config speed_config = {
			.console = console, .fonts = fonts,
			.emulation = ST_CTERM_ANSI_BBS,
			.encoding = encoding ? ST_CTERM_ENCODING_CODEPAGE : ST_CTERM_ENCODING_UTF8,
			.codepage = ST_CODEPAGE_CP437, .quiet = true,
			.output = capture, .output_context = &output,
		};
		static const char input[] =
		    "\x1bP$q*r\x1b\\\x1b[;1*r\x1bP$q*r\x1b\\"
		    "\x1b[2$~\x1b[1$}\x1bP$q*r\x1b\\"
		    "\x1b[;3*r\x1b[0$}\x1bP$q*r\x1b\\";
		static const char expected[] =
		    "\x1bP1$r;0*r\x1b\\\x1bP1$r;1*r\x1b\\"
		    "\x1bP1$r;1*r\x1b\\\x1bP1$r;3*r\x1b\\";
		CHECK(st_cterm_create(&terminal, &speed_config) == 0);
		feed(terminal, input, sizeof(input) - 1);
		CHECK(output.size == sizeof(expected) - 1);
		CHECK(memcmp(output.bytes, expected, output.size) == 0);
		st_cterm_destroy(&terminal);
		output = (struct output){0};
	}
	/* DECDMAC's body parser must not depend on plain-char signedness.
	 * Isolate it from the outer command-string framer's 7-bit restrictions. */
	{
		const struct st_cterm_config macro_config = {
			.console = console, .fonts = fonts,
			.emulation = ST_CTERM_ANSI_BBS,
			.encoding = ST_CTERM_ENCODING_CODEPAGE,
			.codepage = ST_CODEPAGE_CP437, .quiet = true,
		};
		static const uint8_t bytes[] = {0x20, 0x7e, 0xa0, 0xff, 0x7f};
		for (size_t i = 0; i < sizeof(bytes); ++i) {
			CHECK(st_cterm_create(&terminal, &macro_config) == 0);
			feed(terminal, "\033P0;0;0!z", sizeof("\033P0;0;0!z") - 1);
			CHECK(terminal->macro == MACRO_STARTED);
			terminal->strbuf[0] = (char)bytes[i];
			terminal->strbuf[1] = '\0';
			terminal->strbuflen = 1;
			cterm_dec_dcs_byte(terminal);
			cterm_dec_dcs_finish(terminal);
			if (terminal->macro_lens[0] != (bytes[i] == 0x7f ? 0U : 1U))
				fprintf(stderr, "macro byte=%02x length=%zu state=%d\n",
				    bytes[i], terminal->macro_lens[0], terminal->macro);
			CHECK(terminal->macro_lens[0] == (bytes[i] == 0x7f ? 0U : 1U));
			if (bytes[i] != 0x7f)
				CHECK((uint8_t)terminal->macros[0][0] == bytes[i]);
			st_cterm_destroy(&terminal);
		}
	}
	const struct st_cterm_config config = {
		.console = console,
		.fonts = fonts,
		.emulation = ST_CTERM_ANSI_BBS,
		.encoding = ST_CTERM_ENCODING_UTF8,
		.codepage = ST_CODEPAGE_CP437,
		.font_selector = 0,
		.terminal_rows = 3,
		.status_line = true,
		.output = capture,
		.output_context = &output,
		.quiet = true,
	};
	CHECK(st_cterm_create(&terminal, &config) == 0);
	{
		static const uint8_t blank_font[4096] = {0};
		const char path[] = "st_cterm_test_runtime_font.f16";
		struct st_cterm_state state;
		uint16_t identifier = UINT16_MAX;
		FILE *stream;

		CHECK(st_cterm_state(terminal, &state) == 0);
		CHECK(state.alt_fonts[0] == 0);
		CHECK(st_cterm_register_font(terminal, 250,
		    "missing-font-file", ST_CODEPAGE_CP1251, &identifier, NULL) == IO_ERROR_NOT_FOUND);
		CHECK(identifier == UINT16_MAX);
		stream = fopen(path, "wb");
		CHECK(stream != NULL);
		CHECK(fwrite(blank_font, 1, sizeof(blank_font), stream)
		    == sizeof(blank_font));
		CHECK(fclose(stream) == 0);
		CHECK(st_cterm_register_font(terminal, 250, path,
		    ST_CODEPAGE_CP1251, &identifier, NULL) == 0);
		CHECK(identifier != UINT16_MAX);
		CHECK(st_cterm_set_fonts(terminal, 250, ST_CTERM_FONT_NORMAL | ST_CTERM_FONT_BRIGHT) == 0);
		CHECK(st_cterm_state(terminal, &state) == 0);
		CHECK(state.alt_fonts[0] == 250 && state.alt_fonts[1] == 250
		    && state.alt_fonts[2] == 0 && state.alt_fonts[3] == 0);
		enum st_codepage mapping;
		uint16_t actual;
		CHECK(st_cterm_font_info(terminal, 250, &actual, &mapping));
		CHECK(actual == identifier && mapping == ST_CODEPAGE_CP1251);
		CHECK(st_cterm_set_fonts(terminal, 0, 0) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(st_cterm_set_fonts(terminal, 0, 16) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(st_cterm_state(terminal, &state) == 0 && state.alt_fonts[0] == 250);
		CHECK(st_cterm_set_fonts(terminal, 0, ST_CTERM_FONT_ALL) == 0);
		CHECK(st_cterm_set_fonts(terminal, 251, ST_CTERM_FONT_ALL)
		    == IO_ERROR_NOT_FOUND);
		/* Each remote geometry remembers its mapping at download time. A
		 * missing geometry restores the original registration and mapping. */
		CHECK(st_cterm_set_encoding(terminal, ST_CTERM_ENCODING_UTF8, ST_CODEPAGE_CP437) == 0);
		feed(terminal, "\x1b[=250;0{", sizeof("\x1b[=250;0{") - 1);
		feed(terminal, blank_font, sizeof(blank_font));
		CHECK(st_cterm_font_info(terminal, 250, &actual, &mapping));
		CHECK(actual != identifier && mapping == ST_CODEPAGE_CP437);
		CHECK(st_cterm_set_encoding(terminal, ST_CTERM_ENCODING_UTF8, ST_CODEPAGE_CP1251) == 0);
		feed(terminal, "\x1b[=250;2{", sizeof("\x1b[=250;2{") - 1);
		feed(terminal, blank_font, 2048);
		CHECK(st_cterm_font_info(terminal, 250, &actual, &mapping) && mapping == ST_CODEPAGE_CP437);
		uint32_t columns, rows;
		CHECK(st_console_size(console, &columns, &rows) == 0);
		CHECK(st_console_regrid_preserve_pixels(console, columns, rows, 8, 8) == 0);
		CHECK(st_cterm_resize(terminal, columns, rows) == 0);
		CHECK(st_cterm_font_info(terminal, 250, &actual, &mapping) && mapping == ST_CODEPAGE_CP1251);
		CHECK(st_console_regrid_preserve_pixels(console, columns, rows, 8, 14) == 0);
		CHECK(st_cterm_resize(terminal, columns, rows) == 0);
		CHECK(st_cterm_font_info(terminal, 250, &actual, &mapping));
		CHECK(actual == identifier && mapping == ST_CODEPAGE_CP1251);
		CHECK(st_console_regrid_preserve_pixels(console, columns, rows, 8, 16) == 0);
		CHECK(st_cterm_resize(terminal, columns, rows) == 0);
		CHECK(st_cterm_font_info(terminal, 250, &actual, &mapping) && mapping == ST_CODEPAGE_CP437);
		CHECK(st_cterm_set_encoding(terminal, ST_CTERM_ENCODING_UTF8, ST_CODEPAGE_CP437) == 0);
		CHECK(remove(path) == 0);
	}
	CHECK(!st_cterm_bracketed_paste(terminal));
	feed(terminal, "\x1b[?2004h", 8);
	CHECK(st_cterm_bracketed_paste(terminal));
	feed(terminal, "\x1b[?2004l", 8);
	CHECK(!st_cterm_bracketed_paste(terminal));

	feed(terminal, "\x1b[31m", 5);
	feed(terminal, "e", 1);
	CHECK(st_console_get_palette(console, 16, palette, 2) == 0);
	CHECK(palette[0] == 0x000000 && palette[1] == 0xa80000);
	CHECK(st_console_read_cells(console, &one, &cell, 1) == 0);
	CHECK(cell.foreground == 17 && cell.background == 16);
	CHECK(st_console_font_glyph_text(console, cell.glyph_font, cell.glyph,
	    text, sizeof(text), &required) == 0);
	CHECK(required == 1 && text[0] == 'e');
	{
		FILE *snapshot = tmpfile();
		uint8_t bytes[129];
		int native_error = -1;

		CHECK(snapshot != NULL);
		CHECK(st_cterm_save_binary(terminal, snapshot, true,
		    "Board", "User", &native_error) == 0);
		CHECK(native_error == 0 && ftell(snapshot) == 20 * 3 * 2 + 129);
		CHECK(fseek(snapshot, 0, SEEK_SET) == 0);
		CHECK(fread(bytes, 1, 2, snapshot) == 2);
		CHECK(bytes[0] == 'e');
		CHECK(fseek(snapshot, -(long)sizeof(bytes), SEEK_END) == 0);
		CHECK(fread(bytes, 1, sizeof(bytes), snapshot)
		    == sizeof(bytes));
		CHECK(bytes[0] == 0x1a
		    && memcmp(bytes + 1, "SAUCE00", 7) == 0
		    && memcmp(bytes + 8, "Board", 5) == 0
		    && memcmp(bytes + 43, "User", 4) == 0
		    && bytes[91] == 120 && bytes[95] == 5
		    && bytes[96] == 10);
		CHECK(fclose(snapshot) == 0);
	}
	feed(terminal, "\xcc", 1);
	feed(terminal, "\x81", 1);
	CHECK(st_console_read_cells(console, &one, &cell, 1) == 0);
	CHECK(st_console_font_glyph_text(console, cell.glyph_font, cell.glyph,
	    text, sizeof(text), &required) == 0);
	CHECK(required == 2 && memcmp(text, "\xc3\xa9", 2) == 0);
	CHECK(st_cterm_finish(terminal) == 0);
	/* A control in the same receive buffer still reaches the parser. */
	feed(terminal, "x\ry", 3);
	CHECK(st_console_read_cells(console, &one, &cell, 1) == 0);
	CHECK(st_console_font_glyph_text(console, cell.glyph_font, cell.glyph,
	    text, sizeof(text), &required) == 0);
	CHECK(required == 1 && text[0] == 'y');

	/* BEL is local: it neither becomes a cell nor emits host bytes. */
	feed(terminal, "\a", 1);
	CHECK(output.size == 0);
	CHECK(st_cterm_handle_key(terminal, 3, 0));
	CHECK(output.size == 1 && output.bytes[0] == 3);
	CHECK(output.user == 1 && output.responses == 0);
	output = (struct output){0};

	/* A host-writable status display routes Unicode into the reserved row. */
	feed(terminal, "\x1b[2$~\x1b[1$}S",
	    strlen("\x1b[2$~\x1b[1$}S"));
	one.x = 0;
	one.y = 3;
	CHECK(st_console_read_cells(console, &one, &cell, 1) == 0);
	CHECK(st_console_font_glyph_text(console, cell.glyph_font, cell.glyph,
	    text, sizeof(text), &required) == 0);
	CHECK(required == 1 && text[0] == 'S');
	feed(terminal, "\x1b[0$}\x1b[0$~",
	    strlen("\x1b[0$}\x1b[0$~"));
	CHECK(st_console_read_cells(console, &one, &cell, 1) == 0);
	CHECK(st_console_font_glyph_text(console, cell.glyph_font, cell.glyph,
	    text, sizeof(text), &required) == 0);
	CHECK(required == 1 && text[0] == ' ');
	feed(terminal, "\x1b[1$~", strlen("\x1b[1$~"));

	/* DEC cursor styles retain their block/underline distinction. */
	{
		bool visible;
		enum st_console_cursor_shape shape;

		feed(terminal, "\x1b[4 q", strlen("\x1b[4 q"));
		CHECK(st_console_get_cursor(console, &visible, &shape) == 0);
		CHECK(visible && shape == ST_CONSOLE_CURSOR_UNDERLINE);
		feed(terminal, "\x1b[2 q", strlen("\x1b[2 q"));
		CHECK(st_console_get_cursor(console, &visible, &shape) == 0);
		CHECK(visible && shape == ST_CONSOLE_CURSOR_BLOCK);
	}

	/* DEC mouse state is owned by CTerm and emits on the user lane. */
	feed(terminal, "\x1b[?1000h\x1b[?1006h",
	    strlen("\x1b[?1000h\x1b[?1006h"));
	struct st_console_event mouse = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {
			.x = 2, .y = 1,
			.pixel_x = -1, .pixel_y = -1,
			.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT,
			.action = ST_CONSOLE_MOUSE_BUTTON,
		},
	};
	CHECK(st_cterm_handle_mouse(terminal, &mouse));
	CHECK(output.user == 1 && output.size == strlen("\x1b[<0;3;2M")
	    && memcmp(output.bytes, "\x1b[<0;3;2M", output.size) == 0);
	output.size = 0;
	output.user = 0;
	output.responses = 0;

	/* Geometry reports the actual configured cell dimensions. */
	feed(terminal, "\x1b[=3n", strlen("\x1b[=3n"));
	CHECK(output.responses == 1
	    && output.size == strlen("\x1b[=3;16;8n")
	    && memcmp(output.bytes, "\x1b[=3;16;8n", output.size) == 0);
	output.size = 0;
	output.responses = 0;
	/* Font selector zero is a successful selection, not a false failure. */
	feed(terminal, "\x1b[0;0 D\x1b[=1n",
	    strlen("\x1b[0;0 D\x1b[=1n"));
	CHECK(output.responses == 1 && output.size > strlen("\x1b[=1;46;0"));
	CHECK(memcmp(output.bytes, "\x1b[=1;46;0", strlen("\x1b[=1;46;0")) == 0);
	output.size = 0;
	output.responses = 0;
	feed(terminal, "\x1b[<c", strlen("\x1b[<c"));
	CHECK(output.responses == 1
	    && output.size == strlen("\x1b[<0;1;2;3;4;5;6;7;8c")
	    && memcmp(output.bytes, "\x1b[<0;1;2;3;4;5;6;7;8c",
	    output.size) == 0);
	output.size = 0;
	output.responses = 0;

	/* A truncated sequence produces exactly one decode-error glyph. */
	feed(terminal, "\xf0\x9f", 2);
	CHECK(st_cterm_finish(terminal) == 0);
	one.x = 1;
	CHECK(st_console_read_cells(console, &one, &cell, 1) == 0);
	CHECK(cell.glyph != 0);
	uint32_t decode_error_glyph = cell.glyph;
	feed(terminal, "z", 1);
	CHECK(st_console_read_cells(console, &one, &cell, 1) == 0
	    && cell.glyph == decode_error_glyph);
	{
		struct st_console_cell row[20];
		struct st_console_rect row_rect = {0, 0, 20, 1};
		bool found = false;

		CHECK(st_console_read_cells(console, &row_rect, row, 20) == 0);
		for (size_t i = 0; i < 20; ++i) {
			if (st_console_font_glyph_text(console, row[i].glyph_font,
			    row[i].glyph, text, sizeof(text), &required) == 0
			    && required == 1 && text[0] == 'z')
				found = true;
		}
		CHECK(found);
	}

	/* DECCKSR returns a response over the response lane. */
	struct st_console_cell checked[2];
	struct st_console_rect checked_rect = {0, 0, 2, 1};
	char expected[32];
	CHECK(st_console_read_cells(console, &checked_rect, checked, 2) == 0);
	int expected_size = snprintf(expected, sizeof(expected),
	    "\x1bP7!~%04X\x1b\\", crc16(checked, sizeof(checked)));
	CHECK(expected_size > 0 && (size_t)expected_size < sizeof(expected));
	feed(terminal, "\x1b[7;1;1;1;1;2*y",
	    strlen("\x1b[7;1;1;1;1;2*y"));
	CHECK(output.responses == 1);
	CHECK(output.size == (size_t)expected_size
	    && memcmp(output.bytes, expected, output.size) == 0);
	CHECK(output.user == 0);
	{
		struct st_console_cell *history = NULL;
		uint32_t columns;
		size_t rows;

		CHECK(st_cterm_copy_history(terminal, &history, &columns, &rows) == 0);
		CHECK(history != NULL && columns == 20 && rows == 3);
		free(history);
	}
	{
		uint32_t columns;
		uint32_t rows;

		CHECK(st_cterm_resize(terminal, 24, 6) == 0);
		CHECK(st_console_size(console, &columns, &rows) == 0);
		CHECK(columns == 24 && rows == 6);
		CHECK(st_cterm_size(terminal, &columns, &rows) == 0);
		CHECK(columns == 24 && rows == 5);
		feed(terminal, "\x1b[2$~\x1b[1$}R",
		    strlen("\x1b[2$~\x1b[1$}R"));
		one.x = 0;
		one.y = 5;
		CHECK(st_console_read_cells(console, &one, &cell, 1) == 0);
		CHECK(st_console_font_glyph_text(console, cell.glyph_font, cell.glyph,
		    text, sizeof(text), &required) == 0);
		CHECK(required == 1 && text[0] == 'R');
	}

	st_cterm_destroy(&terminal);
	CHECK(st_console_resize(console, 20, 4) == 0);
	for (int encoding = ST_CTERM_ENCODING_UTF8;
	    encoding <= ST_CTERM_ENCODING_CODEPAGE; ++encoding) {
		struct input_filter_state state = {0};
		const struct st_cterm_config filter_config = {
			.console = console, .fonts = fonts,
			.emulation = ST_CTERM_ANSI_BBS,
			.encoding = (enum st_cterm_encoding)encoding,
			.codepage = ST_CODEPAGE_CP437,
			.quiet = true,
			.input_filter = filter_input,
			.input_filter_active = input_filter_active,
			.input_filter_context = &state,
			.capture = capture_input,
			.capture_active = input_filter_active,
			.capture_context = &state,
		};
		const struct st_console_rect first = {0, 0, 1, 1};
		const struct st_console_rect next = {0, 1, 1, 1};
		size_t consumed;

		CHECK(st_cterm_create(&terminal, &filter_config) == 0);
		if (encoding == ST_CTERM_ENCODING_UTF8) {
			feed(terminal, "\xc3", 1);
			CHECK(state.count == 0);
			feed(terminal, "\xa9Q\nB", 4);
		}
		else {
			feed(terminal, "\x82Q\nB", 4);
		}
		CHECK(state.count == 4 && state.seen[0] == 0xe9
		    && state.seen[1] == 'Q' && state.seen[2] == '\n'
		    && state.seen[3] == 'B');
		CHECK(state.captured_size == 4
		    && memcmp(state.captured, "X\r\nB", 4) == 0);
		CHECK(st_console_read_cells(console, &first, &cell, 1) == 0
		    && cell.glyph == 'X');
		CHECK(st_console_read_cells(console, &next, &cell, 1) == 0
		    && cell.glyph == 'B');
		CHECK(st_cterm_write_utf8(terminal, "!", 1, &consumed) == 0
		    && consumed == 1 && state.count == 4);
		CHECK(state.captured_size == 5
		    && memcmp(state.captured, "X\r\nB!", 5) == 0);
		st_cterm_destroy(&terminal);
	}
	{
		const struct st_cterm_config codepage_config = {
			.console = console,
			.fonts = fonts,
			.emulation = ST_CTERM_ANSI_BBS,
			.encoding = ST_CTERM_ENCODING_CODEPAGE,
			.codepage = ST_CODEPAGE_CP437,
			.quiet = true,
		};
		size_t consumed;
		uint32_t utf8_glyph;
		uint16_t utf8_font;

		CHECK(st_cterm_create(&terminal, &codepage_config) == 0);
		CHECK(st_cterm_write_utf8(terminal, "\xc3\xa9", 2,
		    &consumed) == 0 && consumed == 2);
		one = (struct st_console_rect){0, 0, 1, 1};
		CHECK(st_console_read_cells(console, &one, &cell, 1) == 0);
		utf8_glyph = cell.glyph;
		utf8_font = cell.glyph_font;
		st_cterm_destroy(&terminal);
		CHECK(st_cterm_create(&terminal, &codepage_config) == 0);
		feed(terminal, "\x82", 1);
		CHECK(st_console_read_cells(console, &one, &cell, 1) == 0);
		CHECK(cell.glyph == utf8_glyph && cell.glyph_font == utf8_font);
		st_cterm_destroy(&terminal);
	}
	{
		static const struct {
			enum st_cterm_emulation emulation;
			enum st_codepage codepage;
			uint8_t bytes[2];
		} smoke[] = {
			{ST_CTERM_PETSCII, ST_CODEPAGE_PETSCII_UPPER, {147, 'A'}},
			{ST_CTERM_ATASCII, ST_CODEPAGE_ATASCII, {125, 'A'}},
			{ST_CTERM_PRESTEL, ST_CODEPAGE_PRESTEL, {12, 'A'}},
			{ST_CTERM_BBC, ST_CODEPAGE_PRESTEL, {12, 'A'}},
			{ST_CTERM_ATARI_ST_VT52, ST_CODEPAGE_ATARI_ST,
			    {0x1b, 'E'}},
		};

		for (size_t i = 0; i < sizeof(smoke) / sizeof(smoke[0]); ++i) {
			const struct st_cterm_config smoke_config = {
				.console = console,
				.fonts = fonts,
				.emulation = smoke[i].emulation,
				.encoding = ST_CTERM_ENCODING_CODEPAGE,
				.codepage = smoke[i].codepage,
				.font_selector = 0,
				.terminal_rows = 3,
				.quiet = true,
			};

			CHECK(st_cterm_create(&terminal, &smoke_config) == 0);
			feed(terminal, smoke[i].bytes, sizeof(smoke[i].bytes));
			st_cterm_destroy(&terminal);
		}
	}
	{
		uint16_t mapping[16];
		struct st_console_cell *history = NULL;
		uint32_t history_columns;
		size_t history_rows;
		bool found = false;

		for (size_t i = 0; i < 16; ++i)
			mapping[i] = (uint16_t)(100 + i);
		const struct st_cterm_config mapped_config = {
			.console = console,
			.fonts = fonts,
			.emulation = ST_CTERM_ANSI_BBS,
			.encoding = ST_CTERM_ENCODING_CODEPAGE,
			.codepage = ST_CODEPAGE_CP437,
			.palette_mapping = mapping,
			.font_selector = 0,
			.scrollback_lines = 2,
			.quiet = true,
		};

		CHECK(st_cterm_create(&terminal, &mapped_config) == 0);
		feed(terminal, "A", 1);
		one = (struct st_console_rect){0, 0, 1, 1};
		CHECK(st_console_read_cells(console, &one, &cell, 1) == 0);
		cell.foreground = mapping[4] + 16;
		cell.background = mapping[0] + 16;
		cell.flags &= ~ST_CONSOLE_CELL_BRIGHT;
		CHECK(st_console_write_complete_cells(console, &one, &cell, 1) == 0);
		feed(terminal, "\x1b[1;1;1;1;1$r",
		    strlen("\x1b[1;1;1;1;1$r"));
		CHECK(st_console_read_cells(console, &one, &cell, 1) == 0);
		CHECK(cell.foreground == (uint32_t)mapping[12] + 16);
		/* Fill both history slots with received rows before resizing. */
		feed(terminal, "\r\nB\r\nC\r\nD\r\nE\r\nF",
		    strlen("\r\nB\r\nC\r\nD\r\nE\r\nF"));
		CHECK(st_cterm_resize(terminal, 10, 5) == 0);
		CHECK(st_cterm_copy_history(terminal, &history, &history_columns,
		    &history_rows) == 0);
		{
			uint32_t ring_columns;
			size_t ring_rows;
			struct st_console_cell copied[20];

			CHECK(st_cterm_scrollback_size(terminal, &ring_columns,
			    &ring_rows) == 0);
			CHECK(ring_columns == history_columns && ring_rows == 2);
			CHECK(st_cterm_copy_scrollback_rows(terminal, copied,
			    ring_columns, 2, 0, 2, 0) == 2);
			CHECK(memcmp(copied, history, sizeof(copied)) == 0);
			CHECK(st_cterm_copy_scrollback_rows(terminal, copied,
			    ring_columns, 2, 2, 1, 0) == 0);
		}
		for (size_t i = 0; i < (size_t)history_columns * history_rows; ++i) {
			if (st_console_font_glyph_text(console, history[i].glyph_font,
			    history[i].glyph, text, sizeof(text), &required) == 0
			    && required == 1 && text[0] == 'A') {
				CHECK(history[i].foreground == (uint32_t)mapping[12] + 16);
				found = true;
			}
		}
		CHECK(found);
		free(history);
		st_cterm_destroy(&terminal);
		CHECK(st_console_resize(console, 20, 4) == 0);
	}
	{
		const struct st_cterm_config c128_config = {
			.console = console,
			.fonts = fonts,
			.emulation = ST_CTERM_PETSCII,
			.encoding = ST_CTERM_ENCODING_CODEPAGE,
			.codepage = ST_CODEPAGE_PETSCII_UPPER,
			.petscii_machine = ST_CTERM_PETSCII_C128_80,
			.font_selector = 0,
			.quiet = true,
		};

		CHECK(st_cterm_create(&terminal, &c128_config) == 0);
		feed(terminal, "A", 1);
		one = (struct st_console_rect){0, 0, 1, 1};
		CHECK(st_console_read_cells(console, &one, &cell, 1) == 0);
		CHECK(cell.foreground == 31);
		st_cterm_destroy(&terminal);
	}
	{
		const struct st_cterm_config prestel_config = {
			.console = console,
			.fonts = fonts,
			.emulation = ST_CTERM_PRESTEL,
			.encoding = ST_CTERM_ENCODING_CODEPAGE,
			.codepage = ST_CODEPAGE_PRESTEL,
			.font_selector = 0,
			.terminal_rows = 3,
			.quiet = true,
		};
		const uint8_t concealed[] = {0x1b, 'X', 'A'};
		struct st_console_cell row[20];
		struct st_console_rect row_rect = {0, 0, 20, 1};
		bool found_concealed = false;
		bool found_revealed = false;

		CHECK(st_cterm_create(&terminal, &prestel_config) == 0);
		feed(terminal, concealed, sizeof(concealed));
		CHECK(st_console_read_cells(console, &row_rect, row, 20) == 0);
		for (size_t i = 0; i < 20; ++i)
			if ((row[i].flags & ST_CONSOLE_CELL_CONCEAL) != 0
			    && (row[i].flags & ST_CONSOLE_CELL_REVEAL) == 0)
				found_concealed = true;
		CHECK(found_concealed);
		(void)st_cterm_handle_key(terminal, ST_CONSOLE_KEY_F3, 0);
		CHECK(st_console_read_cells(console, &row_rect, row, 20) == 0);
		for (size_t i = 0; i < 20; ++i)
			if ((row[i].flags & (ST_CONSOLE_CELL_CONCEAL
			    | ST_CONSOLE_CELL_REVEAL))
			    == (ST_CONSOLE_CELL_CONCEAL | ST_CONSOLE_CELL_REVEAL))
				found_revealed = true;
		CHECK(found_revealed);
		st_cterm_destroy(&terminal);
		CHECK(st_cterm_create(&terminal, &prestel_config) == 0);
		feed(terminal, concealed, sizeof(concealed));
		CHECK(st_console_read_cells(console, &row_rect, row, 20) == 0);
		found_revealed = false;
		for (size_t i = 0; i < 20; ++i)
			if ((row[i].flags & (ST_CONSOLE_CELL_CONCEAL
			    | ST_CONSOLE_CELL_REVEAL))
			    == (ST_CONSOLE_CELL_CONCEAL | ST_CONSOLE_CELL_REVEAL))
				found_revealed = true;
		CHECK(!found_revealed);
		st_cterm_destroy(&terminal);
	}
	{
		size_t consumed = 99;
		const struct st_cterm_config failure_config = {
			.console = console,
			.fonts = fonts,
			.emulation = ST_CTERM_ANSI_BBS,
			.encoding = ST_CTERM_ENCODING_CODEPAGE,
			.codepage = ST_CODEPAGE_CP437,
			.font_selector = 0,
			.terminal_rows = 3,
			.quiet = true,
		};

		CHECK(st_cterm_create(&terminal, &failure_config) == 0);
		CHECK(st_console_resize(console, 1, 1) == 0);
		CHECK(st_cterm_write(terminal, "x", 1, &consumed) != 0);
		CHECK(consumed == 1);
		st_cterm_destroy(&terminal);
		CHECK(st_console_resize(console, 20, 4) == 0);
		CHECK(st_cterm_create(&terminal, &failure_config) == 0);
		CHECK(st_console_resize(console, 256, 4) == 0);
		CHECK(st_cterm_write(terminal, "\x1b[=3n", 5, &consumed)
		    == 0);
		CHECK(consumed == 5);
		st_cterm_destroy(&terminal);
	}
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	{
		const struct st_console_config wide_config = {
			.columns = 256, .rows = 25, .cell_width = 8,
			.cell_height = 16,
		};
		const struct st_cterm_config wide_cterm_config = {
			.console = NULL,
			.fonts = NULL,
			.emulation = ST_CTERM_ANSI_BBS,
			.encoding = ST_CTERM_ENCODING_CODEPAGE,
			.codepage = ST_CODEPAGE_CP437,
		};
		struct st_cterm_config attempted = wide_cterm_config;

		CHECK(st_console_create(&console, &display, &wide_config) == 0);
		CHECK(st_font_catalog_create(&fonts, console) == 0);
		attempted.console = console;
		attempted.fonts = fonts;
		CHECK(st_cterm_create(&terminal, &attempted) == 0);
		CHECK(terminal != NULL);
		st_cterm_destroy(&terminal);
		st_font_catalog_destroy(fonts);
		st_console_destroy(console);
	}
	return EXIT_SUCCESS;
}
