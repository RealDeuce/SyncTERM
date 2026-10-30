/* Encoding-independent input, grapheme placement, and display ownership. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#include "st_cterm.h"
#include "cterm.h"
#include "io_error.h"
#include "st_utf8.h"
#include "st_builtin_fonts.h"
#include "st_presentation_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (false)

struct result {
	struct st_console_cell cells[80];
	struct st_cterm_state state;
	struct st_console_cell history[160];
	size_t history_rows;
	uint8_t response[256];
	size_t response_size;
};

static void
capture(void *context, enum st_cterm_output_origin origin,
	const uint8_t *bytes, size_t size)
{
	struct result *result = context;
	(void)origin;
	CHECK(size <= sizeof(result->response) - result->response_size);
	memcpy(result->response + result->response_size, bytes, size);
	result->response_size += size;
}

static void
run_mode(struct result *result, enum st_cterm_encoding encoding,
	const char *input, size_t split, bool status_line,
	enum st_cterm_emulation emulation, enum st_codepage codepage)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config geometry = {
		.columns = 20, .rows = 4, .cell_width = 8, .cell_height = 16,
		.wrap = true,
	};
	const struct st_console_rect full = {0, 0, 20, 4};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	struct st_console_font base;
	struct st_presentation_font presentation = {0};
	uint16_t identifier;
	size_t length = strlen(input);
	size_t consumed;

	*result = (struct result){0};
	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_builtin_font_get(0, 8, 16, &base));
	CHECK(st_presentation_font_create(&presentation, &base) == 0);
	CHECK(st_console_register_static_font(console, &presentation.font, &identifier) == 0);
	CHECK(identifier == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config config = {
		.console = console, .fonts = fonts,
		.emulation = emulation, .encoding = encoding,
		.codepage = codepage, .force_lcf = true,
		.status_line = status_line, .scrollback_lines = 4,
		.output = capture, .output_context = result, .quiet = true,
	};
	CHECK(st_cterm_create(&terminal, &config) == 0);
	if (split == SIZE_MAX) {
		for (size_t offset = 0; offset < length; ++offset) {
			int status = st_cterm_write(terminal, input + offset, 1, &consumed);
			if (status != 0)
				fprintf(stderr, "write encoding=%d emulation=%d byte=%zu (%02x): %d\n",
				    (int)encoding, (int)emulation, offset, (unsigned char)input[offset], status);
			CHECK(status == 0);
			CHECK(consumed == 1);
		}
	}
	else {
		CHECK(split <= length);
		CHECK(st_cterm_write(terminal, input, split, &consumed) == 0);
		CHECK(consumed == split);
		CHECK(st_cterm_write(terminal, input + split, length - split, &consumed) == 0);
		CHECK(consumed == length - split);
	}
	CHECK(st_cterm_finish(terminal) == 0);
	CHECK(st_console_read_cells(console, &full, result->cells, 20) == 0);
	CHECK(st_cterm_state(terminal, &result->state) == 0);
	struct st_console_cell *history = NULL;
	uint32_t columns;
	CHECK(st_cterm_copy_history(terminal, &history, &columns, &result->history_rows) == 0);
	CHECK(columns == 20 && result->history_rows <= 8);
	memcpy(result->history, history, result->history_rows * columns * sizeof(*history));
	free(history);
	st_cterm_destroy(&terminal);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	st_presentation_font_destroy(&presentation);
}

static void
run(struct result *result, enum st_cterm_encoding encoding,
	const char *input, size_t split, bool status_line)
{
	run_mode(result, encoding, input, split, status_line,
	    ST_CTERM_ANSI_BBS, ST_CODEPAGE_CP437);
}

static void
compare(const struct result *expected, const struct result *actual)
{
	CHECK(memcmp(expected->cells, actual->cells, sizeof(expected->cells)) == 0);
	CHECK(expected->state.x == actual->state.x);
	CHECK(expected->state.y == actual->state.y);
	CHECK(expected->state.last_column_flag.set == actual->state.last_column_flag.set);
	CHECK(expected->state.scrollback_pos == actual->state.scrollback_pos);
	CHECK(expected->state.top_margin == actual->state.top_margin);
	CHECK(expected->state.bottom_margin == actual->state.bottom_margin);
	CHECK(expected->state.left_margin == actual->state.left_margin);
	CHECK(expected->state.right_margin == actual->state.right_margin);
	CHECK(expected->state.attr == actual->state.attr);
	CHECK(expected->history_rows == actual->history_rows);
	CHECK(memcmp(expected->history, actual->history,
	    expected->history_rows * 20 * sizeof(expected->history[0])) == 0);
	CHECK(expected->response_size == actual->response_size);
	CHECK(memcmp(expected->response, actual->response, expected->response_size) == 0);
}

static void
test_codepages(void)
{
	char encoded[225];
	for (int codepage = 0; codepage < ST_CODEPAGE_ATASCII; ++codepage) {
		char utf8[900];
		size_t count = 0, length = 0;
		struct result expected, actual;
		for (unsigned byte = 0x20; byte <= 0xff; ++byte) {
			if (byte == 0x7f)
				continue;
			encoded[count++] = (char)byte;
			ptrdiff_t size = st_utf8_encode_one(utf8 + length, sizeof(utf8) - length,
			    st_codepage_to_unicode((enum st_codepage)codepage, (uint8_t)byte, true));
			CHECK(size > 0);
			length += (size_t)size;
		}
		encoded[count] = '\0';
		utf8[length] = '\0';
		run_mode(&expected, ST_CTERM_ENCODING_CODEPAGE, encoded, count, false,
		    ST_CTERM_ANSI_BBS, (enum st_codepage)codepage);
		run_mode(&actual, ST_CTERM_ENCODING_CODEPAGE, encoded, SIZE_MAX, false,
		    ST_CTERM_ANSI_BBS, (enum st_codepage)codepage);
		compare(&expected, &actual);
		run_mode(&actual, ST_CTERM_ENCODING_UTF8, utf8, SIZE_MAX, false,
		    ST_CTERM_ANSI_BBS, (enum st_codepage)codepage);
		compare(&expected, &actual);
	}
}

static void
test_scrollback_text(void)
{
	static const char *const input[] = {
		"\xc3\xa9 one\r\n\xc3\xa9 two\r\n\xc3\xa9 three\r\n\xc3\xa9 four\r\n"
		"\xc3\xa9 five\r\n\xc3\xa9 six\r\n\xc3\xa9 seven\r\n\xc3\xa9 eight",
		"\x82 one\r\n\x82 two\r\n\x82 three\r\n\x82 four\r\n"
		"\x82 five\r\n\x82 six\r\n\x82 seven\r\n\x82 eight",
	};
	static const char *const labels[] = {
		"one", "two", "three", "four", "five", "six", "seven", "eight",
	};
	for (int encoding = ST_CTERM_ENCODING_UTF8; encoding <= ST_CTERM_ENCODING_CODEPAGE; ++encoding) {
		struct result actual;
		run(&actual, (enum st_cterm_encoding)encoding, "A", SIZE_MAX, false);
		CHECK(actual.history_rows == 4); /* No pre-session console rows. */
		run(&actual, (enum st_cterm_encoding)encoding,
		    input[encoding == ST_CTERM_ENCODING_UTF8 ? 0 : 1], SIZE_MAX, false);
		CHECK(actual.history_rows == 8);
		for (size_t row = 0; row < 8; ++row) {
			CHECK(actual.history[row * 20].glyph == 0x82);
			for (size_t column = 0; column < strlen(labels[row]); ++column)
				CHECK(actual.history[row * 20 + column + 2].glyph
				    == (unsigned char)labels[row][column]);
		}
		char numbered[2048];
		size_t used = 0;
		for (unsigned line = 0; line < 45; ++line) {
			int length = snprintf(numbered + used, sizeof(numbered) - used,
			    "%s %02u%s", encoding == ST_CTERM_ENCODING_UTF8 ? "\xc3\xa9" : "\x82",
			    line, line == 44 ? "" : "\r\n");
			CHECK(length > 0 && (size_t)length < sizeof(numbered) - used);
			used += (size_t)length;
		}
		run(&actual, (enum st_cterm_encoding)encoding, numbered, SIZE_MAX, false);
		CHECK(actual.history_rows == 8);
		for (size_t row = 0; row < 8; ++row) {
			unsigned line = 37 + (unsigned)row;
			CHECK(actual.history[row * 20 + 2].glyph == '0' + line / 10);
			CHECK(actual.history[row * 20 + 3].glyph == '0' + line % 10);
		}
	}
}

static void
test_scrollback_clears(void)
{
	for (int encoding = ST_CTERM_ENCODING_UTF8; encoding <= ST_CTERM_ENCODING_CODEPAGE; ++encoding) {
		struct result actual;
		/* Preserve whole cleared pages, including trailing blank rows. */
		run(&actual, (enum st_cterm_encoding)encoding,
		    "A\r\nB\033[2JC", SIZE_MAX, false);
		CHECK(actual.history_rows == 8);
		CHECK(actual.history[0].glyph == 'A');
		CHECK(actual.history[20].glyph == 'B');
		CHECK(actual.history[40].glyph == ' ');
		CHECK(actual.history[60].glyph == ' ');
		CHECK(actual.history[80].glyph == 'C');
		/* Completely blank pages remain part of the captured stream. */
		run(&actual, (enum st_cterm_encoding)encoding,
		    "A\033[2J\033[2JC", SIZE_MAX, false);
		CHECK(actual.history_rows == 8);
		for (size_t row = 0; row < 4; ++row)
			CHECK(actual.history[row * 20].glyph == ' ');
		CHECK(actual.history[80].glyph == 'C');
		/* Internal spacing also survives page archival. */
		run(&actual, (enum st_cterm_encoding)encoding,
		    "A\033[3;1HB\033[2JC", SIZE_MAX, false);
		CHECK(actual.history_rows == 8);
		CHECK(actual.history[0].glyph == 'A');
		CHECK(actual.history[20].glyph == ' ');
		CHECK(actual.history[40].glyph == 'B');
		CHECK(actual.history[60].glyph == ' ');
		CHECK(actual.history[80].glyph == 'C');
		/* A scrolling margin alone does not move the display's origin. */
		run(&actual, (enum st_cterm_encoding)encoding,
		    "A\r\nB\r\nC\r\nD\033[2;3r\033[2JE", SIZE_MAX, false);
		CHECK(actual.history_rows == 8);
		for (size_t row = 0; row < 5; ++row)
			CHECK(actual.history[row * 20].glyph == 'A' + row);
		/* Origin mode must not shift or truncate the saved screen either. */
		run(&actual, (enum st_cterm_encoding)encoding,
		    "A\r\nB\r\nC\r\nD\033[2;3r\033[?6h\033[2JE", SIZE_MAX, false);
		CHECK(actual.history_rows == 8);
		for (size_t row = 0; row < 4; ++row)
			CHECK(actual.history[row * 20].glyph == 'A' + row);
		CHECK(actual.history[80].glyph == 'A');
		CHECK(actual.history[100].glyph == 'E');
		CHECK(actual.history[140].glyph == 'D');
		/* Spaces with a different background can be meaningful ANSI art. */
		run(&actual, (enum st_cterm_encoding)encoding,
		    "\033[41m\033[2J\033[40m\033[2J", SIZE_MAX, false);
		CHECK(actual.history_rows == 8);
		CHECK(actual.history[0].background != actual.history[80].background);
	}
}

static void
test_scrollback_resets(void)
{
	for (int encoding = ST_CTERM_ENCODING_UTF8; encoding <= ST_CTERM_ENCODING_CODEPAGE; ++encoding) {
		struct result actual;
		/* RIS must preserve a partly filled ring's write position. Otherwise
		 * F overwrites A and the reported history includes an unwritten row. */
		run(&actual, (enum st_cterm_encoding)encoding,
		    "A\r\nB\r\nC\r\nD\r\nE\033cF\r\nG\r\nH\r\nI\r\nJ", SIZE_MAX, false);
		CHECK(actual.history_rows == 6);
		CHECK(actual.history[0].glyph == 'A');
		CHECK(actual.history[20].glyph == 'F');
		for (size_t row = 2; row < 6; ++row)
			CHECK(actual.history[row * 20].glyph == 'E' + row);
		/* After wrap, resetting just the write position mixes older and
		 * newer pages even though every retained slot contains real text. */
		run(&actual, (enum st_cterm_encoding)encoding,
		    "A\r\nB\r\nC\r\nD\r\nE\r\nF\r\nG\r\nH\r\nI"
		    "\033cJ\r\nK\r\nL\r\nM\r\nN", SIZE_MAX, false);
		CHECK(actual.history_rows == 8);
		CHECK(actual.history[0].glyph == 'C');
		CHECK(actual.history[20].glyph == 'D');
		CHECK(actual.history[40].glyph == 'E');
		for (size_t row = 3; row < 8; ++row)
			CHECK(actual.history[row * 20].glyph == 'G' + row);
	}
}

static void
test_emulations(void)
{
	static const struct {
		enum st_cterm_emulation emulation;
		enum st_codepage codepage;
		const char *stream;
	} fixtures[] = {
		{ST_CTERM_ATASCII, ST_CODEPAGE_ATASCII, "A\033\034B\233C\176D"},
		{ST_CTERM_PETSCII, ST_CODEPAGE_PETSCII_UPPER, "A\022B\222C\034D"},
		{ST_CTERM_PRESTEL, ST_CODEPAGE_PRESTEL, "A\033QB\033^_\033XC"},
		{ST_CTERM_BBC, ST_CODEPAGE_PRESTEL, "A\033QB\177C\034\001\002"},
		{ST_CTERM_ATARI_ST_VT52, ST_CODEPAGE_ATARI_ST, "A\033b\001B\033Y\042\043C"},
	};
	for (size_t i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); ++i) {
		struct result expected, actual;
		size_t length = strlen(fixtures[i].stream);
		run_mode(&expected, ST_CTERM_ENCODING_CODEPAGE, fixtures[i].stream, length,
		    false, fixtures[i].emulation, fixtures[i].codepage);
		CHECK(expected.cells[0].glyph == 'A');
		for (size_t split = 0; split <= length; ++split) {
			run_mode(&actual, ST_CTERM_ENCODING_CODEPAGE, fixtures[i].stream, split,
			    false, fixtures[i].emulation, fixtures[i].codepage);
			compare(&expected, &actual);
		}
		run_mode(&actual, ST_CTERM_ENCODING_CODEPAGE, fixtures[i].stream, SIZE_MAX,
		    false, fixtures[i].emulation, fixtures[i].codepage);
		compare(&expected, &actual);
		if (fixtures[i].emulation == ST_CTERM_PETSCII) {
			CHECK(expected.cells[1].foreground == expected.cells[0].background);
			CHECK(expected.cells[1].background != expected.cells[0].background);
		}
		if (fixtures[i].emulation == ST_CTERM_PRESTEL)
			CHECK((expected.cells[1].flags & ST_CONSOLE_CELL_SAA5050_CONTROL_MASK) != 0);
	}
}

struct rig {
	st_console_t *console;
	st_font_catalog_t *fonts;
	st_cterm_t *terminal;
};

static void
open_mapped_rig(struct rig *rig, enum st_cterm_encoding encoding, size_t lines,
	const uint16_t *palette)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {
		.columns = 20, .rows = 4, .cell_width = 8, .cell_height = 16,
	};
	*rig = (struct rig){0};
	CHECK(st_console_create(&rig->console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&rig->fonts, rig->console) == 0);
	const struct st_cterm_config config = {
		.console = rig->console, .fonts = rig->fonts,
		.emulation = ST_CTERM_ANSI_BBS, .encoding = encoding,
		.codepage = ST_CODEPAGE_CP437, .force_lcf = true, .quiet = true,
		.scrollback_lines = lines,
		.palette_mapping = palette,
	};
	CHECK(st_cterm_create(&rig->terminal, &config) == 0);
}

static void
open_history_rig(struct rig *rig, enum st_cterm_encoding encoding, size_t lines)
{
	open_mapped_rig(rig, encoding, lines, NULL);
}

static void
open_rig(struct rig *rig, enum st_cterm_encoding encoding)
{
	open_history_rig(rig, encoding, 4);
}

static void
close_rig(struct rig *rig)
{
	st_cterm_destroy(&rig->terminal);
	st_font_catalog_destroy(rig->fonts);
	st_console_destroy(rig->console);
}

static void
feed(struct rig *rig, const void *bytes, size_t size)
{
	size_t consumed;
	CHECK(st_cterm_write(rig->terminal, bytes, size, &consumed) == 0);
	CHECK(consumed == size);
}

static void
test_tabstop_insertion(void)
{
	for (int encoding = ST_CTERM_ENCODING_UTF8; encoding <= ST_CTERM_ENCODING_CODEPAGE; ++encoding) {
		struct rig rig;
		open_rig(&rig, (enum st_cterm_encoding)encoding);
		/* Clear preserves the old allocation, including its former first
		 * stop at column 9. That stale value must not suppress insertion. */
		static const char first[] = "\033[3g\033[1;9H\033H";
		feed(&rig, first, sizeof(first) - 1);
		CHECK(rig.terminal->tab_count == 1 && rig.terminal->tabs[0] == 9);
		static const char more[] = "\033[1;20H\033H\033[1;5H\033H\033[1;17H\033H\033H";
		feed(&rig, more, sizeof(more) - 1);
		CHECK(rig.terminal->tab_count == 4);
		const int expected[] = {5, 9, 17, 20};
		CHECK(memcmp(rig.terminal->tabs, expected, sizeof(expected)) == 0);
		static const char clear[] = "\033[3g\033[1;5H\033H";
		feed(&rig, clear, sizeof(clear) - 1);
		CHECK(rig.terminal->tab_count == 1 && rig.terminal->tabs[0] == 5);
		close_rig(&rig);
	}
}

static void
test_cursor_and_blink_background(void)
{
	for (int encoding = ST_CTERM_ENCODING_UTF8; encoding <= ST_CTERM_ENCODING_CODEPAGE; ++encoding) {
		struct rig rig;
		open_rig(&rig, (enum st_cterm_encoding)encoding);
		for (unsigned style = 1; style <= 4; ++style) {
			char sequence[32];
			int size = snprintf(sequence, sizeof(sequence), "\033[%u qPrompt\r\n", style);
			CHECK(size > 0 && (size_t)size < sizeof(sequence));
			feed(&rig, sequence, (size_t)size);
			struct st_console_custom_cursor cursor;
			CHECK(st_console_get_custom_cursor(rig.console, &cursor) == 0);
			CHECK(cursor.visible && cursor.blink == (style % 2 != 0));
			CHECK(cursor.start == (style <= 2 ? 0U : 14U) && cursor.end == 15U);
		}
		CHECK(st_cterm_reset_terminal(rig.terminal) == 0);
		struct st_console_custom_cursor cursor;
		CHECK(st_console_get_custom_cursor(rig.console, &cursor) == 0 && cursor.blink);
		CHECK(st_console_get_default_cursor(rig.console, &cursor) == 0);
		cursor.blink = false;
		cursor.start = 0;
		CHECK(st_console_set_default_cursor(rig.console, &cursor) == 0);
		CHECK(st_cterm_reset_terminal(rig.terminal) == 0);
		feed(&rig, "default", 7);
		CHECK(st_console_get_custom_cursor(rig.console, &cursor) == 0 && !cursor.blink);
		feed(&rig, "\033[4 qsteady", 11);
		CHECK(st_console_get_custom_cursor(rig.console, &cursor) == 0);
		CHECK(cursor.start == 14 && !cursor.blink);
		close_rig(&rig);
		struct result actual;
		run(&actual, (enum st_cterm_encoding)encoding,
		    "\033[0;5;41m \033[1;1;1;1;1;1*y"
		    "\033[?33h\033[1;1H\033[0;5;41m \033[1;1;1;1;1;1*y"
		    "\033[?33l\033[1;1H\033[0;5;41m \033[1;1;1;1;1;1*y", SIZE_MAX, false);
		size_t reply = strlen("\033P1!~0000\033\\");
		CHECK(actual.response_size == 3 * reply);
		CHECK(memcmp(actual.response, actual.response + reply, reply) != 0);
		CHECK(memcmp(actual.response, actual.response + 2 * reply, reply) == 0);
	}
}

static void
test_mapped_palette_policy(void)
{
	static const uint16_t palette[16] = {
		63, 2, 181, 44, 207, 5, 98, 19, 109, 47, 31, 213, 11, 76, 148, 233,
	};
	for (int encoding = ST_CTERM_ENCODING_UTF8; encoding <= ST_CTERM_ENCODING_CODEPAGE; ++encoding) {
		struct rig rig;
		open_mapped_rig(&rig, (enum st_cterm_encoding)encoding, 4, palette);
		const struct st_console_rect first = {0, 0, 1, 1};
		struct st_console_cell normal, bright;
		static const char base[] = "\033[0;5;41m ";
		feed(&rig, base, sizeof(base) - 1);
		CHECK(st_console_read_cells(rig.console, &first, &normal, 1) == 0);
		static const char enable[] = "\033[?33h\033[1;1H ";
		feed(&rig, enable, sizeof(enable) - 1);
		CHECK(st_console_read_cells(rig.console, &first, &bright, 1) == 0);
		/* Intensity selects another logical colour before mapping; physical
		 * palette entries need not be adjacent or in any particular bank. */
		CHECK((int)bright.background - (int)normal.background
		    == (int)palette[12] - (int)palette[4]);
		CHECK((bright.flags & (ST_CONSOLE_CELL_LEGACY_FOREGROUND
		    | ST_CONSOLE_CELL_LEGACY_BACKGROUND)) == 0);
		static const char disable[] = "\033[?33l\033[1;1H ";
		feed(&rig, disable, sizeof(disable) - 1);
		CHECK(st_console_read_cells(rig.console, &first, &bright, 1) == 0);
		CHECK(bright.background == normal.background);
		static const char rgb[] = "\033[48;2;9;17;33m\033[?33h\033[1;1H ";
		feed(&rig, rgb, sizeof(rgb) - 1);
		CHECK(st_console_read_cells(rig.console, &first, &bright, 1) == 0);
		CHECK(bright.background == ST_CONSOLE_COLOR_RGB_VALUE(9, 17, 33));
		close_rig(&rig);
	}
}

static void
test_scrollback_termtest_reset(void)
{
	for (int encoding = ST_CTERM_ENCODING_UTF8; encoding <= ST_CTERM_ENCODING_CODEPAGE; ++encoding) {
		struct rig rig;
		open_history_rig(&rig, (enum st_cterm_encoding)encoding, 2000);
		CHECK(st_cterm_resize(rig.terminal, 80, 25) == 0);
		for (unsigned line = 0; line < 200; ++line) {
			char text[32];
			int size = snprintf(text, sizeof(text), "Test %03u\r\n", line);
			CHECK(size > 0 && (size_t)size < sizeof(text));
			feed(&rig, text, (size_t)size);
		}
		struct st_console_cell *before = NULL;
		uint32_t columns;
		size_t rows;
		CHECK(st_cterm_copy_history(rig.terminal, &before, &columns, &rows) == 0);
		CHECK(columns == 80 && rows == 201);
		int filled = rig.terminal->backfilled;
		int start = rig.terminal->backstart;
		int position = rig.terminal->backpos;
		/* termtest.js sends RIS after main(), then clears the display and
		 * prints the summary. RIS itself must leave every history byte and
		 * ring coordinate unchanged. The following clear is a real page. */
		feed(&rig, "\033c", 2);
		CHECK(rig.terminal->backfilled == filled);
		CHECK(rig.terminal->backstart == start && rig.terminal->backpos == position);
		CHECK(memcmp(rig.terminal->scrollback, before,
		    (size_t)filled * columns * sizeof(*before)) == 0);
		feed(&rig, "\033[2J", 4);
		for (unsigned line = 0; line < 100; ++line) {
			char text[32];
			int size = snprintf(text, sizeof(text), "Summary %03u\r\n", line);
			CHECK(size > 0 && (size_t)size < sizeof(text));
			feed(&rig, text, (size_t)size);
		}
		struct st_console_cell *after = NULL;
		CHECK(st_cterm_copy_history(rig.terminal, &after, &columns, &rows) == 0);
		CHECK(rows == 302);
		CHECK(memcmp(after, before, (size_t)filled * columns * sizeof(*before)) == 0);
		for (size_t row = (size_t)filled; row < (size_t)filled + 25; ++row) {
			for (size_t col = 0; col < columns; ++col)
				CHECK(after[row * columns + col].glyph == ' ');
		}
		for (unsigned line = 0; line < 100; ++line) {
			char text[32];
			int size = snprintf(text, sizeof(text), "Summary %03u", line);
			CHECK(size > 0 && (size_t)size < sizeof(text));
			for (int col = 0; col < size; ++col)
				CHECK(after[((size_t)filled + 25 + line) * columns + (size_t)col].glyph
				    == (unsigned char)text[col]);
		}
		free(after);
		free(before);
		close_rig(&rig);
	}
}

static void
test_scrollback_read_failure(void)
{
	struct rig rig;
	open_rig(&rig, ST_CTERM_ENCODING_CODEPAGE);
	static const char text[] = "A\r\nB\r\nC\r\nD\r\nE\r\nF\r\nG\r\nH\r\nI";
	feed(&rig, text, sizeof(text) - 1);
	struct st_console_cell before[80];
	CHECK(rig.terminal->backfilled == 4);
	memcpy(before, rig.terminal->scrollback, sizeof(before));
	int start = rig.terminal->backstart;
	int position = rig.terminal->backpos;
	CHECK(position != 0);
	/* The local reset API preserves the same ring state as wire RIS. */
	CHECK(st_cterm_reset_terminal(rig.terminal) == 0);
	CHECK(rig.terminal->backfilled == 4);
	CHECK(rig.terminal->backstart == start && rig.terminal->backpos == position);
	CHECK(memcmp(before, rig.terminal->scrollback, sizeof(before)) == 0);
	/* Simulate a console geometry change before CTerm's resize reaches it. */
	CHECK(st_console_resize(rig.console, 20, 1) == 0);
	rig.terminal->top_margin = 2;
	cterm_scrollup(rig.terminal);
	CHECK(rig.terminal->backfilled == 4);
	CHECK(rig.terminal->backstart == start);
	CHECK(rig.terminal->backpos == position);
	CHECK(memcmp(before, rig.terminal->scrollback, sizeof(before)) == 0);
	CHECK(st_cterm_finish(rig.terminal) == IO_ERROR_OUT_OF_RANGE);
	close_rig(&rig);
}

static void
expect_text(struct rig *rig, uint32_t x, uint32_t y, const char *expected)
{
	struct st_console_cell cell;
	const struct st_console_rect area = {x, y, 1, 1};
	char text[32];
	size_t size;
	CHECK(st_console_read_cells(rig->console, &area, &cell, 1) == 0);
	CHECK(st_console_font_glyph_text(rig->console, cell.glyph_font,
	    cell.glyph, text, sizeof(text), &size) == 0);
	CHECK(size == strlen(expected) && memcmp(text, expected, size) == 0);
}

static void
test_macro_retirement(void)
{
	for (int encoding = ST_CTERM_ENCODING_UTF8; encoding <= ST_CTERM_ENCODING_CODEPAGE; ++encoding) {
		struct rig rig;
		open_rig(&rig, (enum st_cterm_encoding)encoding);
		static const char setup[] = "M\033[2$~\033[1$}S";
		feed(&rig, setup, sizeof(setup) - 1);
		/* Define the executing macro on the status subterminal. It hides
		 * its own display and creates a replacement before returning. */
		static const char macro[] =
		    "\033P0;0;1!z1B5B30247E4E1B5B32247E1B5B31247D541B5B30247D4F\033\\\033[0*z";
		for (size_t i = 0; i < sizeof(macro) - 1; ++i)
			feed(&rig, macro + i, 1);
		CHECK(st_cterm_finish(rig.terminal) == 0);
		CHECK(rig.terminal->retired_status == NULL);
		CHECK(rig.terminal->status_sub != NULL);
		expect_text(&rig, 0, 0, "M");
		expect_text(&rig, 1, 0, "N");
		expect_text(&rig, 2, 0, "O");
		expect_text(&rig, 0, 3, "T");
		close_rig(&rig);
	}
	struct rig rig;
	open_rig(&rig, ST_CTERM_ENCODING_UTF8);
	/* A replay ending in ESC must leave its sequence open for wire input. */
	static const char partial[] = "\033P0;0;1!z1B\033\\\033[0*z[31mA";
	feed(&rig, partial, sizeof(partial) - 1);
	struct st_console_cell cell;
	const struct st_console_rect first = {0, 0, 1, 1};
	CHECK(st_console_read_cells(rig.console, &first, &cell, 1) == 0);
	CHECK(cell.glyph == 'A' && cell.foreground == 17);
	/* Reset frees the definition being played; its copied input survives. */
	static const char reset[] = "\033P0;0;1!z1B6351\033\\\033[0*z";
	feed(&rig, reset, sizeof(reset) - 1);
	expect_text(&rig, 0, 0, "Q");
	close_rig(&rig);
}

static void
test_graphemes_and_boundaries(void)
{
	struct result expected, actual;
	run(&expected, ST_CTERM_ENCODING_UTF8, "\xc3\xa9\033[2b", SIZE_MAX, false);
	run(&actual, ST_CTERM_ENCODING_UTF8, "e\xcc\x81\033[2b", SIZE_MAX, false);
	compare(&expected, &actual);
	struct rig rig;
	open_rig(&rig, ST_CTERM_ENCODING_CODEPAGE);
	feed(&rig, "e", 1);
	size_t consumed;
	CHECK(st_cterm_write_utf8(rig.terminal, "\xcc\x81", 2, &consumed) == 0 && consumed == 2);
	expect_text(&rig, 0, 0, "\xc3\xa9");
	CHECK(st_cterm_set_encoding(rig.terminal, ST_CTERM_ENCODING_UTF8, ST_CODEPAGE_CP437) == 0);
	feed(&rig, "\xc3", 1);
	CHECK(st_cterm_set_encoding(rig.terminal, ST_CTERM_ENCODING_CODEPAGE, ST_CODEPAGE_CP437) == 0);
	CHECK(rig.terminal->xpos == 3); /* One replacement for the truncated scalar. */
	feed(&rig, "\x82", 1);
	expect_text(&rig, 2, 0, "\xc3\xa9");
	CHECK(st_cterm_reset_terminal(rig.terminal) == 0);
	CHECK(rig.terminal->last_text == NULL && rig.terminal->last_text_size == 0);
	close_rig(&rig);
	/* A missing wide glyph still occupies a pair and wraps as one unit. */
	run(&actual, ST_CTERM_ENCODING_UTF8, "\033[1;20H\xe4\xb8\x80", SIZE_MAX, false);
	CHECK(actual.state.x == 3 && actual.state.y == 2);
	CHECK((actual.cells[21].flags & ST_CONSOLE_CELL_CONTINUATION) != 0);
	/* Revising a narrow keycap prefix at the margin must not leave it
	 * behind when its completed grapheme needs two columns. */
	run(&actual, ST_CTERM_ENCODING_UTF8, "\033[1;20H1\xef\xb8\x8f\xe2\x83\xa3", SIZE_MAX, false);
	CHECK(actual.state.x == 3 && actual.state.y == 2);
	CHECK((actual.cells[21].flags & ST_CONSOLE_CELL_CONTINUATION) != 0);
	CHECK(actual.cells[19].glyph == ' ');
	run(&actual, ST_CTERM_ENCODING_UTF8, "\xc3", SIZE_MAX, false);
	struct st_console_font base;
	CHECK(st_builtin_font_get(0, 8, 16, &base));
	CHECK(actual.cells[0].glyph_font == 0 && actual.cells[0].glyph == base.glyph_count + 1);
	CHECK(actual.state.x == 2 && actual.state.y == 1);
	/* Immediate wrapping can scroll the provisional drawable before a
	 * combining scalar arrives. Revision must follow that drawable. */
	open_rig(&rig, ST_CTERM_ENCODING_UTF8);
	rig.terminal->last_column_flag = 0;
	static const char scroll[] = "1\r\n2\r\n3\r\n4\033[4;20He";
	feed(&rig, scroll, sizeof(scroll) - 1);
	feed(&rig, "\xcc\x81", 2);
	expect_text(&rig, 0, 0, "2");
	expect_text(&rig, 19, 2, "\xc3\xa9");
	CHECK(rig.terminal->xpos == 1 && rig.terminal->ypos == 4);
	close_rig(&rig);
	/* Origin mode uses coordinates relative to both margin pairs. */
	run(&actual, ST_CTERM_ENCODING_UTF8,
	    "\033[?69h\033[4;8s\033[?6h\033[1;1H12345Z", SIZE_MAX, false);
	CHECK(actual.cells[23].glyph == 'Z');
}

struct payload_hooks {
	size_t calls;
	size_t captured;
	size_t protocol;
	uint8_t bytes[2304];
	size_t size;
};

static int
reject_payload(void *context, uint32_t scalar, bool *consumed,
	char *replacement, size_t capacity, size_t *size)
{
	struct payload_hooks *hooks = context;
	(void)replacement; (void)capacity;
	++hooks->calls;
	*consumed = scalar == 'Q';
	*size = 0;
	return 0;
}

static int
capture_payload(void *context, const char *text, size_t size, bool protocol)
{
	struct payload_hooks *hooks = context;
	CHECK(size <= sizeof(hooks->bytes) - hooks->size);
	memcpy(hooks->bytes + hooks->size, text, size);
	hooks->size += size;
	if (protocol)
		hooks->protocol += size;
	else
		hooks->captured += size;
	return 0;
}

static void
test_raw_payloads(void)
{
	struct rig rig;
	struct payload_hooks hooks = {0};
	open_rig(&rig, ST_CTERM_ENCODING_UTF8);
	rig.terminal->api_input_filter = reject_payload;
	rig.terminal->api_input_filter_context = &hooks;
	rig.terminal->api_capture = capture_payload;
	rig.terminal->api_capture_context = &hooks;
	static const char intro[] = "\033[=255;2{";
	uint8_t payload[2048];
	for (size_t i = 0; i < sizeof(payload); ++i)
		payload[i] = (uint8_t)i;
	feed(&rig, intro, sizeof(intro) - 1);
	for (size_t i = 0; i < sizeof(payload); ++i)
		feed(&rig, payload + i, 1);
	CHECK(rig.terminal->font_size == 0);
	CHECK(memcmp(payload, rig.terminal->fontbuf, sizeof(payload)) == 0);
	static const char macro[] = "\033P0;0;1!z001B80FF\033\\";
	static const uint8_t expected[] = {0, 0x1b, 0x80, 0xff};
	feed(&rig, macro, sizeof(macro) - 1);
	CHECK(rig.terminal->macro_lens[0] == sizeof(expected));
	CHECK(memcmp(rig.terminal->macros[0], expected, sizeof(expected)) == 0);
	CHECK(hooks.calls == 2 && hooks.captured == 2);
	CHECK(hooks.protocol == sizeof(intro) + sizeof(payload) + sizeof(macro) - 4);
	CHECK(hooks.size == sizeof(intro) + sizeof(payload) + sizeof(macro) - 2);
	CHECK(memcmp(hooks.bytes, intro, sizeof(intro) - 1) == 0);
	CHECK(memcmp(hooks.bytes + sizeof(intro) - 1, payload, sizeof(payload)) == 0);
	CHECK(memcmp(hooks.bytes + sizeof(intro) - 1 + sizeof(payload),
	    macro, sizeof(macro) - 1) == 0);
	feed(&rig, "QZ", 2);
	CHECK(hooks.calls == 4 && hooks.captured == 3);
	expect_text(&rig, 0, 0, "Z");
	close_rig(&rig);
}

static void
test_resize_pending_utf8(void)
{
	static const char *const scalars[] = {
		"\xc3\xa9", "\xe2\x94\x80", "\xf0\x9d\x84\x9e",
	};
	for (size_t index = 0; index < sizeof(scalars) / sizeof(scalars[0]); ++index) {
		size_t length = strlen(scalars[index]);
		for (size_t split = 1; split < length; ++split) {
			for (unsigned status = 0; status < 2; ++status) {
				struct rig rig;
				struct payload_hooks captured = {0};
				open_rig(&rig, ST_CTERM_ENCODING_UTF8);
				if (status != 0)
					feed(&rig, "\033[2$~\033[1$}", 10);
				rig.terminal->api_capture = capture_payload;
				rig.terminal->api_capture_context = &captured;
				feed(&rig, scalars[index], split);
				CHECK(captured.size == 0);
				CHECK(st_cterm_resize(rig.terminal, 24, 5) == 0);
				CHECK(captured.size == 0);
				feed(&rig, scalars[index] + split, length - split);
				CHECK(st_cterm_finish(rig.terminal) == 0);
				CHECK(captured.size == length && captured.protocol == 0);
				CHECK(memcmp(captured.bytes, scalars[index], length) == 0);
				close_rig(&rig);
			}
		}
	}
}

static int
fail_protocol_capture(void *context, const char *bytes, size_t size,
	bool protocol)
{
	(void)context; (void)bytes; (void)size;
	return protocol ? IO_ERROR_IO : 0;
}

static void
test_shutdown_and_errors(void)
{
	struct rig rig;
	size_t consumed = 99;
	CHECK(st_cterm_write(NULL, "x", 1, &consumed) == IO_ERROR_INVALID_ARGUMENT && consumed == 0);
	consumed = 99;
	CHECK(st_cterm_write_utf8(NULL, "x", 1, &consumed) == IO_ERROR_INVALID_ARGUMENT && consumed == 0);
	uint32_t glyph = UINT32_MAX;
	CHECK(st_console_font_lookup_decode_error(NULL, &glyph) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(glyph == UINT32_MAX);
	open_rig(&rig, ST_CTERM_ENCODING_UTF8);
	rig.terminal->api_capture = fail_protocol_capture;
	CHECK(st_cterm_write(rig.terminal, "\033[31mZ", 6, &consumed)
	    == IO_ERROR_IO && consumed == 2);
	rig.terminal->api_capture = NULL;
	feed(&rig, "31mZ", 4);
	expect_text(&rig, 0, 0, "Z");
	close_rig(&rig);
	open_rig(&rig, ST_CTERM_ENCODING_UTF8);
	static const char status[] = "\033[2$~\033[1$}\xc3";
	feed(&rig, status, sizeof(status) - 1);
	CHECK(st_cterm_finish(rig.terminal) == 0);
	CHECK(rig.terminal->status_sub->xpos == 2);
	close_rig(&rig);
	for (int encoding = ST_CTERM_ENCODING_UTF8; encoding <= ST_CTERM_ENCODING_CODEPAGE; ++encoding) {
		open_rig(&rig, (enum st_cterm_encoding)encoding);
		CHECK(st_console_resize(rig.console, 1, 1) == 0);
		CHECK(st_cterm_write(rig.terminal, "x", 1, &consumed) != 0 && consumed == 1);
		close_rig(&rig);
	}
}

static int
replace_status(void *context, uint32_t scalar, bool *consumed,
	char *replacement, size_t capacity, size_t *size)
{
	static const char text[] = "\033[0$~N\033[2$~\033[1$}Y\033[0$}O";
	(void)context;
	*consumed = scalar == 'X';
	*size = 0;
	if (*consumed) {
		CHECK(sizeof(text) - 1 <= capacity);
		memcpy(replacement, text, sizeof(text) - 1);
		*size = sizeof(text) - 1;
	}
	return 0;
}

static void
test_reentrant_replacement(void)
{
	for (int encoding = ST_CTERM_ENCODING_UTF8; encoding <= ST_CTERM_ENCODING_CODEPAGE; ++encoding) {
		struct rig rig;
		open_rig(&rig, (enum st_cterm_encoding)encoding);
		static const char setup[] = "M\033[2$~\033[1$}S";
		feed(&rig, setup, sizeof(setup) - 1);
		rig.terminal->api_input_filter = replace_status;
		feed(&rig, "X", 1);
		CHECK(rig.terminal->retired_status == NULL);
		expect_text(&rig, 0, 0, "M");
		expect_text(&rig, 1, 0, "N");
		expect_text(&rig, 2, 0, "O");
		expect_text(&rig, 0, 3, "Y");
		close_rig(&rig);
	}
}

static void
test_font_selection(void)
{
	for (int encoding = ST_CTERM_ENCODING_UTF8; encoding <= ST_CTERM_ENCODING_CODEPAGE; ++encoding) {
		struct rig rig;
		open_rig(&rig, (enum st_cterm_encoding)encoding);
		unsigned selector = 1;
		while (selector < 256 && !rig.terminal->screen.font_valid[selector])
			++selector;
		CHECK(selector < 256);
		char sequence[32];
		int size = snprintf(sequence, sizeof(sequence), "\033[0;%u D", selector);
		CHECK(size > 0 && (size_t)size < sizeof(sequence));
		feed(&rig, sequence, (size_t)size);
		if (encoding == ST_CTERM_ENCODING_UTF8)
			feed(&rig, "\xc3\xa9", 2);
		else
			feed(&rig, "\x82", 1);
		struct st_console_cell cell;
		const struct st_console_rect first = {0, 0, 1, 1};
		CHECK(st_console_read_cells(rig.console, &first, &cell, 1) == 0);
		CHECK(cell.font == rig.terminal->screen.font_identifiers[selector]);
		close_rig(&rig);
	}
}

struct diagnostics {
	unsigned count;
	enum st_cterm_log_level level;
	char text[8192];
};

static void
diagnostic(void *context, enum st_cterm_log_level level, const char *message)
{
	struct diagnostics *log = context;
	++log->count;
	log->level = level;
	size_t used = strlen(log->text);
	CHECK(snprintf(log->text + used, sizeof(log->text) - used, "%s\n", message) > 0);
}

static void
test_diagnostics(void)
{
	struct rig rig;
	struct diagnostics log = {0};
	open_rig(&rig, ST_CTERM_ENCODING_UTF8);
	st_cterm_destroy(&rig.terminal);
	const struct st_cterm_config config = {
		.console = rig.console, .fonts = rig.fonts,
		.emulation = ST_CTERM_ANSI_BBS, .encoding = ST_CTERM_ENCODING_UTF8,
		.codepage = ST_CODEPAGE_CP437, .quiet = true,
		.log_level = ST_CTERM_LOG_DEBUG, .diagnostic = diagnostic,
		.diagnostic_context = &log,
	};
	CHECK(st_cterm_create(&rig.terminal, &config) == 0);
	/* A mapped glyph may deliberately have the same index as .noglyph.
	 * A font-zero match is also a real glyph, not a missing one. */
	struct st_console_font font;
	const struct st_console_font_mapping mapping[] = {{.codepoint = 'A', .glyph = 0}};
	uint16_t identifier;
	CHECK(st_builtin_font_get(0, 8, 16, &font));
	font.mapping = mapping;
	font.mapping_count = 1;
	font.fallback_glyph = 0;
	CHECK(st_console_register_static_font(rig.console, &font, &identifier) == 0);
	rig.terminal->screen.font_identifiers[46] = identifier;
	rig.terminal->screen.font_valid[46] = true;
	feed(&rig, "\033[0;46 DAB", sizeof("\033[0;46 DAB") - 1);
	CHECK(st_cterm_finish(rig.terminal) == 0 && log.count == 0);
	const char missing[] = "\xf4\x8f\xbf\xbf\xcc\x81"; /* U+10FFFF + U+0301 */
	for (size_t i = 0; i < sizeof(missing) - 1; ++i) feed(&rig, missing + i, 1);
	CHECK(log.count == 0); /* provisional clusters do not create records */
	CHECK(st_cterm_finish(rig.terminal) == 0);
	CHECK(log.count == 1 && log.level == ST_CTERM_LOG_DEBUG);
	CHECK(strstr(log.text, "Missing glyph (.noglyph)") != NULL);
	CHECK(strstr(log.text, "U+10FFFF U+0301") != NULL);
	CHECK(strstr(log.text, "column=3 row=1") != NULL);
	log = (struct diagnostics){0};
	/* Status subterminals route diagnostics to their parent's callback. */
	feed(&rig, "\033[2$~\033[1$}", sizeof("\033[2$~\033[1$}") - 1);
	feed(&rig, missing, sizeof(missing) - 1);
	feed(&rig, "\033[0$}", sizeof("\033[0$}") - 1);
	CHECK(log.count == 1 && strstr(log.text, "row=4") != NULL);
	log = (struct diagnostics){0};
	feed(&rig, "\033[12345;67~", sizeof("\033[12345;67~") - 1);
	CHECK(log.level == ST_CTERM_LOG_WARNING);
	CHECK(strstr(log.text, "Unsupported sequence: ESC [12345;67~") != NULL);
	feed(&rig, "\033[1?m", sizeof("\033[1?m") - 1);
	CHECK(strstr(log.text, "Invalid sequence") != NULL);
	feed(&rig, "\033[0;99*r", sizeof("\033[0;99*r") - 1);
	CHECK(strstr(log.text, "Invalid DECSCS parameters: ESC [0;99*r") != NULL);
	feed(&rig, "\033[=999;1{", sizeof("\033[=999;1{") - 1);
	CHECK(strstr(log.text, "Invalid font slot") != NULL);
	char oversized[256];
	memset(oversized, ';', sizeof(oversized));
	memcpy(oversized, "\033[", 2);
	oversized[sizeof(oversized) - 1] = 'm';
	feed(&rig, oversized, sizeof(oversized));
	CHECK(strstr(log.text, "Invalid sequence parameters (limit exceeded)") != NULL);
	feed(&rig, "\xff", 1);
	CHECK(strstr(log.text, "Invalid UTF-8 input") != NULL);
	log = (struct diagnostics){0};
	rig.terminal->api_log_level = ST_CTERM_LOG_ERROR;
	feed(&rig, missing, sizeof(missing) - 1);
	feed(&rig, "\033[12345~", sizeof("\033[12345~") - 1);
	CHECK(st_cterm_finish(rig.terminal) == 0 && log.count == 0);
	cterm_render_record_error(rig.terminal, IO_ERROR_OUT_OF_MEMORY);
	CHECK(log.count == 1 && log.level == ST_CTERM_LOG_ERROR);
	CHECK(cterm_render_take_error(rig.terminal) == IO_ERROR_OUT_OF_MEMORY);
	rig.terminal->api_diagnostic = NULL;
	cterm_render_record_error(rig.terminal, IO_ERROR_IO);
	CHECK(log.count == 1);
	CHECK(cterm_render_take_error(rig.terminal) == IO_ERROR_IO);
	close_rig(&rig);
}

int
main(void)
{
	test_tabstop_insertion();
	test_diagnostics();
	test_codepages();
	test_scrollback_text();
	test_scrollback_clears();
	test_scrollback_resets();
	test_scrollback_termtest_reset();
	test_scrollback_read_failure();
	test_cursor_and_blink_background();
	test_mapped_palette_policy();
	test_emulations();
	test_macro_retirement();
	test_graphemes_and_boundaries();
	test_raw_payloads();
	test_resize_pending_utf8();
	test_shutdown_and_errors();
	test_reentrant_replacement();
	test_font_selection();
	static const struct {
		const char *codepage;
		const char *utf8;
		bool status_line;
	} fixtures[] = {
		{"\033]0;\x82", "\033]0;\xc3\xa9", false},
		{"\033P0;0;1!z4D1B5B32247E1B5B31247D821B5B30247D4E\033\\\033[0*z",
		    "\033P0;0;1!z4D1B5B32247E1B5B31247DC3A91B5B30247D4E\033\\\033[0*z", true},
		{"A\x82\033[3b", "A\xc3\xa9\033[3b", false},
		{"M\033[2$~\033[1$}\x82\033[0$}N", "M\033[2$~\033[1$}\xc3\xa9\033[0$}N", true},
		{"M\033[2$~\033[1$}\x82\033[0$~N\033[2$~\033[1$}T\033[0$}O",
		    "M\033[2$~\033[1$}\xc3\xa9\033[0$~N\033[2$~\033[1$}T\033[0$}O", true},
		{"1234567890123456789\x82Z\033[6n", "1234567890123456789\xc3\xa9Z\033[6n", false},
		{"\033[2;3r\033[3;1Habc\n\x82\nXYZ", "\033[2;3r\033[3;1Habc\n\xc3\xa9\nXYZ", false},
		{"\033[31m\x82\033[44mQ\bX\rY\nZ", "\033[31m\xc3\xa9\033[44mQ\bX\rY\nZ", false},
	};
	for (size_t fixture = 0; fixture < sizeof(fixtures) / sizeof(fixtures[0]); ++fixture) {
		struct result expected;
		struct result actual;

		run(&expected, ST_CTERM_ENCODING_CODEPAGE, fixtures[fixture].codepage,
		    strlen(fixtures[fixture].codepage), fixtures[fixture].status_line);
		run(&actual, ST_CTERM_ENCODING_CODEPAGE, fixtures[fixture].codepage,
		    SIZE_MAX, fixtures[fixture].status_line);
		compare(&expected, &actual);
		run(&actual, ST_CTERM_ENCODING_UTF8, fixtures[fixture].utf8,
		    SIZE_MAX, fixtures[fixture].status_line);
		compare(&expected, &actual);
		for (size_t split = 0; split <= strlen(fixtures[fixture].codepage); ++split) {
			run(&actual, ST_CTERM_ENCODING_CODEPAGE, fixtures[fixture].codepage,
			    split, fixtures[fixture].status_line);
			compare(&expected, &actual);
		}
		for (size_t split = 0; split <= strlen(fixtures[fixture].utf8); ++split) {
			run(&actual, ST_CTERM_ENCODING_UTF8, fixtures[fixture].utf8,
			    split, fixtures[fixture].status_line);
			compare(&expected, &actual);
		}
	}
	return EXIT_SUCCESS;
}
