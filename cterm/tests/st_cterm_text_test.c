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
open_rig(struct rig *rig, enum st_cterm_encoding encoding)
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
	};
	CHECK(st_cterm_create(&rig->terminal, &config) == 0);
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
capture_payload(void *context, const char *text, size_t size)
{
	struct payload_hooks *hooks = context;
	(void)text;
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
	feed(&rig, "QZ", 2);
	CHECK(hooks.calls == 4 && hooks.captured == 3);
	expect_text(&rig, 0, 0, "Z");
	close_rig(&rig);
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

int
main(void)
{
	test_codepages();
	test_emulations();
	test_macro_retirement();
	test_graphemes_and_boundaries();
	test_raw_payloads();
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
