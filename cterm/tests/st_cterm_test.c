/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_cterm.h"

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

int
main(void)
{
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

	feed(terminal, "\x1b[31m", 5);
	feed(terminal, "e", 1);
	CHECK(st_console_get_palette(console, 16, palette, 2) == 0);
	CHECK(palette[0] == 0x000000 && palette[1] == 0xa80000);
	CHECK(st_console_read_cells(console, &one, &cell, 1) == 0);
	CHECK(cell.foreground == 17 && cell.background == 16);
	CHECK(st_console_font_glyph_text(console, cell.glyph_font, cell.glyph,
	    text, sizeof(text), &required) == 0);
	CHECK(required == 1 && text[0] == 'e');
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
	CHECK(st_cterm_handle_key(terminal, 3));
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
		feed(terminal, "\r\nB\r\nC\r\nD\r\nE",
		    strlen("\r\nB\r\nC\r\nD\r\nE"));
		CHECK(st_cterm_resize(terminal, 10, 5) == 0);
		CHECK(st_cterm_copy_history(terminal, &history, &history_columns,
		    &history_rows) == 0);
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
		(void)st_cterm_handle_key(terminal, ST_CONSOLE_KEY_F3);
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
