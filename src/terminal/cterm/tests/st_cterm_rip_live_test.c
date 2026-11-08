/* RIP recognition in the live CTerm write path. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_cterm.h"
#include "cterm_rip_fonts.h"
#include "st_pixels.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static int
feed(st_cterm_t *terminal, const void *bytes, size_t size)
{
	size_t consumed;
	int status = st_cterm_write(terminal, bytes, size, &consumed);

	return status == 0 && consumed == size ? 0 : 1;
}

static int
test_rip_deferred_wrap(enum st_cterm_encoding encoding)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config console_config = {
		.columns = 80, .rows = 43,
		.cell_width = 8, .cell_height = 8,
		.pixel_width = 640, .pixel_height = 350,
	};
	const struct st_cterm_config config = {
		.emulation = ST_CTERM_ANSI_BBS,
		.encoding = encoding,
		.graphics_mode = ST_CTERM_GRAPHICS_RIP_1,
		.codepage = ST_CODEPAGE_CP437,
	};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	struct st_cterm_config terminal_config = config;
	struct st_console_cell cell;
	char text[8];
	size_t required;
	uint32_t cursor_x, cursor_y;
	char line[80];
	const struct st_console_rect expected = {0, 1, 1, 1};
	const struct st_console_rect extra = {0, 2, 1, 1};

	CHECK(st_console_create(&console, &display, &console_config) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	terminal_config.console = console;
	terminal_config.fonts = fonts;
	CHECK(st_cterm_create(&terminal, &terminal_config) == 0);
	CHECK(feed(terminal, "\r!|*\r\n", 6) == 0);
	memset(line, 'A', sizeof(line));
	CHECK(feed(terminal, line, sizeof(line)) == 0);
	CHECK(feed(terminal, "\r\nB", 3) == 0);
	CHECK(st_cterm_finish(terminal) == 0);
	CHECK(st_console_read_cells(console, &expected, &cell, 1) == 0);
	CHECK(st_console_font_glyph_text(console, cell.glyph_font,
	    cell.glyph, text, sizeof(text), &required) == 0);
	CHECK(required == 1 && text[0] == 'B');
	CHECK(st_console_read_cells(console, &extra, &cell, 1) == 0);
	CHECK(st_console_font_glyph_text(console, cell.glyph_font,
	    cell.glyph, text, sizeof(text), &required) == 0);
	CHECK(required != 1 || text[0] != 'B');
	CHECK(feed(terminal, "\r!|*\r\n", 6) == 0);
	CHECK(feed(terminal, line, sizeof(line)) == 0);
	CHECK(st_console_get_position(console, &cursor_x, &cursor_y) == 0);
	CHECK(cursor_x == 79 && cursor_y == 0);
	CHECK(feed(terminal, "B", 1) == 0);
	CHECK(st_cterm_finish(terminal) == 0);
	CHECK(st_console_read_cells(console, &expected, &cell, 1) == 0);
	CHECK(st_console_font_glyph_text(console, cell.glyph_font,
	    cell.glyph, text, sizeof(text), &required) == 0);
	CHECK(required == 1 && text[0] == 'B');
	CHECK(st_console_read_cells(console, &extra, &cell, 1) == 0);
	CHECK(st_console_font_glyph_text(console, cell.glyph_font,
	    cell.glyph, text, sizeof(text), &required) == 0);
	CHECK(required != 1 || text[0] != 'B');
	if (encoding == ST_CTERM_ENCODING_UTF8) {
		CHECK(feed(terminal, "\r!|*\r\n", 6) == 0);
		CHECK(feed(terminal, line, sizeof(line) - 1) == 0);
		CHECK(feed(terminal, "\xc3", 1) == 0);
		CHECK(feed(terminal, "\xa9", 1) == 0);
		CHECK(feed(terminal, "B", 1) == 0);
		CHECK(st_cterm_finish(terminal) == 0);
		CHECK(st_console_read_cells(console, &expected, &cell, 1) == 0);
		CHECK(st_console_font_glyph_text(console, cell.glyph_font,
		    cell.glyph, text, sizeof(text), &required) == 0);
		CHECK(required == 1 && text[0] == 'B');
	}
	st_cterm_destroy(&terminal);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	return 0;
}

static int
test_text_window_reinit_wrap(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config console_config = {
		.columns = 80, .rows = 43,
		.cell_width = 8, .cell_height = 8,
		.pixel_width = 640, .pixel_height = 350,
	};
	const struct st_console_rect rect = {0, 0, 2, 2};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	struct st_pixel_buffer *initial = NULL;
	struct st_pixel_buffer *sample = NULL;
	struct st_console_cell cells[4];
	char text[8];
	size_t required;
	const char *stream = "!|*\r\n!|w0000010100\r\nABC";

	CHECK(st_console_create(&console, &display, &console_config) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config config = {
		.console = console,
		.fonts = fonts,
		.emulation = ST_CTERM_ANSI_BBS,
		.encoding = ST_CTERM_ENCODING_CODEPAGE,
		.graphics_mode = ST_CTERM_GRAPHICS_RIP_1,
		.codepage = ST_CODEPAGE_CP437,
	};
	CHECK(st_cterm_create(&terminal, &config) == 0);
	const struct st_pixel_blit blit = {
		.width = 640, .height = 350,
		.scale_x = 1, .scale_y = 1,
	};
	const struct st_console_rect pixel_rect = {100, 100, 1, 1};

	CHECK(st_pixel_buffer_create(&initial, 640, 350, false) == 0);
	CHECK(st_pixel_buffer_fill(initial, 4, 0) == 0);
	CHECK(st_console_blit_pixels(console, initial, NULL, &blit) == 0);
	st_pixel_buffer_destroy(initial);
	initial = NULL;
	CHECK(feed(terminal, "\r", 1) == 0);
	CHECK(st_console_get_pixels(&sample, console, &pixel_rect, false) == 0);
	CHECK(sample->primary[0] == 16);
	st_pixel_buffer_destroy(sample);
	sample = NULL;
	CHECK(feed(terminal, stream, strlen(stream)) == 0);
	CHECK(st_cterm_finish(terminal) == 0);
	CHECK(st_console_read_cells(console, &rect, cells, 2) == 0);
	CHECK(st_console_font_glyph_text(console, cells[0].glyph_font,
	    cells[0].glyph, text, sizeof(text), &required) == 0);
	CHECK(required == 1 && text[0] == 'A');
	CHECK(st_console_font_glyph_text(console, cells[1].glyph_font,
	    cells[1].glyph, text, sizeof(text), &required) == 0);
	CHECK(required == 1 && text[0] == 'B');
	CHECK(st_console_font_glyph_text(console, cells[2].glyph_font,
	    cells[2].glyph, text, sizeof(text), &required) == 0);
	CHECK(required == 1 && text[0] == 'C');
	const char *large_window = "\r!|*\r\n!|w0000270N02\r\n";
	CHECK(feed(terminal, large_window, strlen(large_window)) == 0);
	char long_line[81];

	memset(long_line, 'A', 80);
	long_line[80] = 'B';
	CHECK(feed(terminal, long_line, sizeof(long_line)) == 0);
	CHECK(st_cterm_finish(terminal) == 0);
	const struct st_console_rect last = {79, 0, 1, 1};
	const struct st_console_rect below = {0, 1, 1, 1};

	CHECK(st_console_read_cells(console, &last, cells, 1) == 0);
	CHECK(st_console_font_glyph_text(console, cells[0].glyph_font,
	    cells[0].glyph, text, sizeof(text), &required) == 0);
	CHECK(required == 1 && text[0] == 'A');
	CHECK(st_console_read_cells(console, &below, cells, 1) == 0);
	CHECK(st_console_font_glyph_text(console, cells[0].glyph_font,
	    cells[0].glyph, text, sizeof(text), &required) == 0);
	CHECK(required == 1 && text[0] == 'B');
	st_cterm_destroy(&terminal);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	return 0;
}

static int
test_text_window_reinit_keeps_start_state(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config console_config = {
		.columns = 80, .rows = 25,
		.cell_width = 8, .cell_height = 14,
		.pixel_width = 640, .pixel_height = 350,
	};
	const struct st_pixel_blit blit = {
		.width = 640, .height = 350,
		.scale_x = 1, .scale_y = 1,
	};
	const struct st_console_rect point = {0, 300, 1, 1};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	struct st_pixel_buffer *initial = NULL;
	struct st_pixel_buffer *sample = NULL;

	CHECK(st_console_create(&console, &display, &console_config) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config config = {
		.console = console, .fonts = fonts,
		.emulation = ST_CTERM_ANSI_BBS,
		.encoding = ST_CTERM_ENCODING_CODEPAGE,
		.graphics_mode = ST_CTERM_GRAPHICS_RIP_1,
		.codepage = ST_CODEPAGE_CP437,
	};
	CHECK(st_cterm_create(&terminal, &config) == 0);
	CHECK(st_pixel_buffer_create(&initial, 640, 350, false) == 0);
	CHECK(st_pixel_buffer_fill(initial, 4, 0) == 0);
	CHECK(st_console_blit_pixels(console, initial, NULL, &blit) == 0);
	st_pixel_buffer_destroy(initial);
	const char *stream = "!|1K|w0010271610|#|#|#\r\n";
	CHECK(feed(terminal, stream, strlen(stream)) == 0);
	CHECK(st_console_get_pixels(&sample, console, &point, false) == 0);
	CHECK(sample->primary[0] == 4);
	st_pixel_buffer_destroy(sample);
	sample = NULL;
	CHECK(feed(terminal, "\r", 1) == 0);
	CHECK(st_console_get_pixels(&sample, console, &point, false) == 0);
	CHECK(sample->primary[0] == 4);
	st_pixel_buffer_destroy(sample);
	st_cterm_destroy(&terminal);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	return 0;
}

static int
test_rip3_reset_default_font(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config console_config = {
		.columns = 80, .rows = 25,
		.cell_width = 8, .cell_height = 14,
		.pixel_width = 640, .pixel_height = 350,
	};
	const struct st_console_rect text_rect = {0, 0, 80, 3};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	struct st_console_cell cells[80 * 3];
	uint16_t default_font;
	uint32_t width, height, cell_width, cell_height;
	char text[8];
	size_t required;
	bool found = false;

	CHECK(st_console_create(&console, &display, &console_config) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	CHECK(st_font_catalog_identifier(fonts, 0, &default_font));
	const struct st_cterm_config config = {
		.console = console, .fonts = fonts,
		.emulation = ST_CTERM_ANSI_BBS,
		.encoding = ST_CTERM_ENCODING_CODEPAGE,
		.graphics_mode = ST_CTERM_GRAPHICS_RIP_3,
		.codepage = ST_CODEPAGE_CP437,
	};
	CHECK(st_cterm_create(&terminal, &config) == 0);
	CHECK(feed(terminal, "!|*\r\nX", 6) == 0);
	CHECK(st_cterm_finish(terminal) == 0);
	CHECK(st_console_size(console, &width, &height) == 0);
	CHECK(width == 80 && height == 25);
	CHECK(st_console_cell_pixel_size(console, &cell_width,
	    &cell_height) == 0);
	CHECK(cell_width == 8 && cell_height == 14);
	CHECK(st_console_read_cells(console, &text_rect, cells, 80) == 0);
	for (size_t index = 0; index < 80 * 3; index++) {
		if (st_console_font_glyph_text(console, cells[index].glyph_font,
		    cells[index].glyph, text, sizeof(text), &required) != 0
		    || required != 1 || text[0] != 'X')
			continue;
		CHECK(cells[index].glyph_font == default_font);
		found = true;
		break;
	}
	CHECK(found);
	const char *text_window = "\r!|w0000270N00\r";

	CHECK(feed(terminal, text_window, strlen(text_window)) == 0);
	CHECK(st_console_cell_pixel_size(console, &cell_width,
	    &cell_height) == 0);
	CHECK(cell_width == 8 && cell_height == 8);
	CHECK(feed(terminal, "\r!|*\r\nY", 7) == 0);
	CHECK(st_cterm_finish(terminal) == 0);
	CHECK(st_console_cell_pixel_size(console, &cell_width,
	    &cell_height) == 0);
	CHECK(cell_width == 8 && cell_height == 14);
	CHECK(st_console_read_cells(console, &text_rect, cells, 80) == 0);
	found = false;
	for (size_t index = 0; index < 80 * 3; index++) {
		if (st_console_font_glyph_text(console, cells[index].glyph_font,
		    cells[index].glyph, text, sizeof(text), &required) != 0
		    || required != 1 || text[0] != 'Y')
			continue;
		CHECK(cells[index].glyph_font == default_font);
		found = true;
		break;
	}
	CHECK(found);
	st_cterm_destroy(&terminal);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	return 0;
}

struct output_capture {
	char bytes[32];
	size_t size;
	char user_bytes[32];
	size_t user_size;
};

struct prompt_capture {
	const char *reply;
	char question[80];
	char initial[80];
	size_t width;
	bool reject_blank;
	int calls;
};

static int
capture_prompt(void *context, const char *question, const char *initial,
    size_t width, bool reject_blank, char **result)
{
	struct prompt_capture *capture = context;

	capture->calls++;
	snprintf(capture->question, sizeof(capture->question), "%s", question);
	snprintf(capture->initial, sizeof(capture->initial), "%s", initial);
	capture->width = width;
	capture->reject_blank = reject_blank;
	*result = NULL;
	if (capture->reply != NULL) {
		*result = malloc(strlen(capture->reply) + 1);
		if (*result == NULL)
			return 1;
		strcpy(*result, capture->reply);
	}
	return 0;
}

static void
capture_output(void *context, enum st_cterm_output_origin origin,
	const uint8_t *bytes, size_t size)
{
	struct output_capture *capture = context;

	if (origin == ST_CTERM_OUTPUT_RESPONSE
	    && size <= sizeof(capture->bytes) - capture->size) {
		memcpy(capture->bytes + capture->size, bytes, size);
		capture->size += size;
	}
	else if (origin == ST_CTERM_OUTPUT_USER
	    && size <= sizeof(capture->user_bytes) - capture->user_size) {
		memcpy(capture->user_bytes + capture->user_size, bytes, size);
		capture->user_size += size;
	}
}

static int
sample_pixel(st_console_t *console, uint32_t x, uint32_t y,
    uint32_t *value)
{
	const struct st_console_rect rect = {x, y, 1, 1};
	struct st_pixel_buffer *pixels = NULL;
	int status = st_console_get_pixels(&pixels, console, &rect, false);

	if (status != 0)
		return status;
	*value = pixels->primary[0];
	st_pixel_buffer_destroy(pixels);
	return 0;
}

static int
click_pixel(st_cterm_t *terminal, struct st_console_event *mouse,
    int x, int y)
{
	mouse->data.mouse.pixel_x = x;
	mouse->data.mouse.pixel_y = y;
	mouse->data.mouse.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT;
	if (!st_cterm_handle_mouse(terminal, mouse))
		return 1;
	mouse->data.mouse.buttons = 0;
	return st_cterm_handle_mouse(terminal, mouse) ? 0 : 1;
}

/* Full-frame colours for these streams were captured from SyncTERM v1.10a. */
static int
test_v110a_flood(const char *stream, bool patterned)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config screen = {
		.columns = 80, .rows = 25,
		.cell_width = 8, .cell_height = 14,
		.pixel_width = 640, .pixel_height = 350,
	};
	const struct st_console_rect full = {0, 0, 640, 350};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	struct st_pixel_buffer *pixels = NULL;
	uint32_t palette[256];

	CHECK(st_console_create(&console, &display, &screen) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config config = {
		.console = console,
		.fonts = fonts,
		.emulation = ST_CTERM_ANSI_BBS,
		.encoding = ST_CTERM_ENCODING_CODEPAGE,
		.graphics_mode = ST_CTERM_GRAPHICS_RIP_1,
		.codepage = ST_CODEPAGE_CP437,
	};
	CHECK(st_cterm_create(&terminal, &config) == 0);
	CHECK(feed(terminal, stream, strlen(stream)) == 0);
	CHECK(st_console_get_pixels(&pixels, console, &full, false) == 0);
	CHECK(st_console_get_palette(console, 0, palette, 256) == 0);
	for (size_t y = 0; y < 350; y++) {
		for (size_t x = 0; x < 640; x++) {
			uint32_t index = pixels->primary[y * 640 + x];
			uint32_t expected = 0;

			if (x >= 10 && x <= 50 && y >= 10 && y <= 50) {
				if (x == 10 || x == 50 || y == 10 || y == 50)
					expected = 0xffffff;
				else if (!patterned || ((x + y) & 7) == 7)
					expected = 0xff5555;
			}
			if (index >= 256 || palette[index] != expected) {
				fprintf(stderr,
				    "v1.10a %s flood pixel (%zu,%zu): expected %#x, got %#x\n",
				    patterned ? "patterned" : "solid",
				    x, y, expected,
				    index < 256 ? palette[index] : index);
				return 1;
			}
		}
	}
	st_pixel_buffer_destroy(pixels);
	st_cterm_destroy(&terminal);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	return 0;
}

int
st_cterm_rip_live_test_main(void)
{
	CHECK(test_rip_deferred_wrap(ST_CTERM_ENCODING_CODEPAGE) == 0);
	CHECK(test_rip_deferred_wrap(ST_CTERM_ENCODING_UTF8) == 0);
	CHECK(test_text_window_reinit_wrap() == 0);
	CHECK(test_text_window_reinit_keeps_start_state() == 0);
	CHECK(test_rip3_reset_default_font() == 0);
	CHECK(test_v110a_flood(
	    "!|*|c0F|R0A0A1E1E|S010C|F0K0K0F|#\n", false) == 0);
	CHECK(test_v110a_flood(
	    "!|*|c0F|R0A0A1E1E|S030C|F0K0K0F|#\n", true) == 0);
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config console_config = {
		.columns = 80, .rows = 43,
		.cell_width = 8, .cell_height = 8,
		.pixel_width = 640, .pixel_height = 350,
	};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	struct st_pixel_buffer *pixels = NULL;
	const struct st_console_rect pixel_rect = {100, 100, 1, 1};
	const struct st_console_rect cell_rect = {0, 0, 1, 1};
	struct st_console_cell cell;
	struct st_console_custom_cursor cursor;
	struct output_capture output = {0};
	struct prompt_capture prompt = {.reply = "Bob"};
	char text[8];
	size_t required;
	uint32_t columns, rows, cell_width, cell_height;
	const uint8_t *rip_font;
	size_t rip_font_size;

	CHECK(st_console_create(&console, &display, &console_config) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config config = {
		.console = console,
		.fonts = fonts,
		.emulation = ST_CTERM_ANSI_BBS,
		.encoding = ST_CTERM_ENCODING_UTF8,
		.graphics_mode = ST_CTERM_GRAPHICS_RIP_1,
		.codepage = ST_CODEPAGE_CP437,
		.font_selector = 0,
		.output = capture_output,
		.output_context = &output,
		.rip_prompt = capture_prompt,
		.rip_prompt_context = &prompt,
	};
	CHECK(st_cterm_create(&terminal, &config) == 0);
	const char *definition = "!|1D00000NAME,30:?Your name?Alice\r\n";
	CHECK(feed(terminal, definition, strlen(definition)) == 0);
	CHECK(prompt.calls == 1 && strcmp(prompt.question, "Your name") == 0);
	CHECK(strcmp(prompt.initial, "Alice") == 0 && prompt.width == 30);
	const char *variable_query = "!|1\x1b" "0000$name$^M\r\n";
	CHECK(feed(terminal, variable_query, strlen(variable_query)) == 0);
	CHECK(output.user_size == 4
	    && memcmp(output.user_bytes, "Bob\r", 4) == 0);
	output.user_size = 0;
	definition = "!|1D00400nAmE,10:Charlie\r\n";
	CHECK(feed(terminal, definition, strlen(definition)) == 0);
	CHECK(prompt.calls == 1);
	variable_query = "!|1\x1b" "0000$NAME$^M\r\n";
	CHECK(feed(terminal, variable_query, strlen(variable_query)) == 0);
	CHECK(output.user_size == 8
	    && memcmp(output.user_bytes, "Charlie\r", 8) == 0);
	output.user_size = 0;
	definition = "!|1D00400NAME,10:\r\n";
	CHECK(feed(terminal, definition, strlen(definition)) == 0);
	CHECK(prompt.calls == 1);
	CHECK(feed(terminal, "!|*\r\n", 5) == 0);
	CHECK(feed(terminal, variable_query, strlen(variable_query)) == 0);
	CHECK(output.user_size == 8
	    && memcmp(output.user_bytes, "Charlie\r", 8) == 0);
	output.user_size = 0;
	definition = "!|1D000009BAD:ignored\r\n";
	CHECK(feed(terminal, definition, strlen(definition)) == 0);
	CHECK(prompt.calls == 1);
	prompt.reply = "New";
	definition = "!|1D00400OTHER:Seed\r\n";
	CHECK(feed(terminal, definition, strlen(definition)) == 0);
	CHECK(prompt.calls == 2 && strcmp(prompt.initial, "Seed") == 0);
	variable_query = "!|1\x1b" "0000$OTHER$^M\r\n";
	CHECK(feed(terminal, variable_query, strlen(variable_query)) == 0);
	CHECK(output.user_size == 4
	    && memcmp(output.user_bytes, "New\r", 4) == 0);
	output.user_size = 0;
	CHECK(feed(terminal, "\x1b[!", 3) == 0);
	CHECK(output.size == 14
	    && memcmp(output.bytes, "RIPSCRIP015410", 14) == 0);
	const char *query = "!|1\x1b" "0000$RIPVER$^M\r";
	CHECK(feed(terminal, query, strlen(query)) == 0);
	CHECK(output.user_size == 15
	    && memcmp(output.user_bytes, "RIPSCRIP015410\r", 15) == 0);
	output.user_size = 0;
	query = "!|1\x1b" "0000$COFF$\r";
	CHECK(feed(terminal, query, strlen(query)) == 0);
	bool cursor_visible;
	enum st_console_cursor_shape cursor_shape;
	CHECK(st_console_get_cursor(console, &cursor_visible,
	    &cursor_shape) == 0);
	CHECK(!cursor_visible);
	query = "!|1\x1b" "0000$CON$\r";
	CHECK(feed(terminal, query, strlen(query)) == 0);
	CHECK(st_console_get_cursor(console, &cursor_visible,
	    &cursor_shape) == 0);
	CHECK(cursor_visible);
	CHECK(feed(terminal, "!|c0F|X2", 8) == 0);
	CHECK(feed(terminal, "S2S\r", 4) == 0);
	CHECK(st_console_get_pixels(&pixels, console, &pixel_rect, false) == 0);
	CHECK(pixels->primary[0] == 0x1f);
	st_pixel_buffer_destroy(pixels);
	CHECK(feed(terminal, "\n!|*\r\n", 6) == 0);
	CHECK(st_console_get_pixels(&pixels, console, &pixel_rect, false) == 0);
	CHECK(pixels->primary[0] == 0x10);
	st_pixel_buffer_destroy(pixels);
	const char *world = "!|fM8JG\r!|X2S2S\r";
	CHECK(feed(terminal, world, strlen(world)) == 0);
	uint32_t world_pixel;
	CHECK(sample_pixel(console, 80, 50, &world_pixel) == 0
	    && world_pixel == 0x1f);
	const char *world_mouse = "!|1M00" "2S2S3232" "0000000" "^M\r";
	CHECK(feed(terminal, world_mouse, strlen(world_mouse)) == 0);
	struct st_console_event world_click = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {
			.pixel_x = 84, .pixel_y = 52,
			.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT,
			.action = ST_CONSOLE_MOUSE_BUTTON,
		},
	};
	CHECK(st_cterm_handle_mouse(terminal, &world_click));
	world_click.data.mouse.buttons = 0;
	CHECK(st_cterm_handle_mouse(terminal, &world_click));
	CHECK(output.user_size == 1 && output.user_bytes[0] == '\r');
	output.user_size = 0;
	CHECK(feed(terminal, "!|*\r\n", 5) == 0);
	query = "!|1\x1b" "1000G^M\r";
	CHECK(feed(terminal, query, strlen(query)) == 0);
	/* RIP reset discards mouse fields, not deferred query templates. */
	CHECK(feed(terminal, "!|*\r\n", 5) == 0);
	struct st_console_event query_click = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {
			.x = 50, .y = 25,
			.pixel_x = 400, .pixel_y = 200,
			.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT,
			.action = ST_CONSOLE_MOUSE_BUTTON,
		},
	};
	(void)st_cterm_handle_mouse(terminal, &query_click);
	query_click.data.mouse.buttons = 0;
	CHECK(st_cterm_handle_mouse(terminal, &query_click));
	CHECK(output.user_size == 2
	    && memcmp(output.user_bytes, "G\r", 2) == 0);
	output.user_size = 0;
	query = "!|1\x1b" "2000T^M\r";
	CHECK(feed(terminal, query, strlen(query)) == 0);
	query_click.data.mouse.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT;
	(void)st_cterm_handle_mouse(terminal, &query_click);
	query_click.data.mouse.buttons = 0;
	CHECK(st_cterm_handle_mouse(terminal, &query_click));
	CHECK(output.user_size == 4
	    && memcmp(output.user_bytes, "T\rG\r", 4) == 0);
	output.user_size = 0;
	query = "!|1\x1b" "1000$XYM$^M\r";
	CHECK(feed(terminal, query, strlen(query)) == 0);
	query_click.data.mouse.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT;
	(void)st_cterm_handle_mouse(terminal, &query_click);
	query_click.data.mouse.buttons = 0;
	CHECK(st_cterm_handle_mouse(terminal, &query_click));
	CHECK(output.user_size == strlen("T\r0190:00c8:100\r")
	    && memcmp(output.user_bytes, "T\r0190:00c8:100\r",
	    output.user_size) == 0);
	output.user_size = 0;
	query = "!|1\x1b" "1000G^M\r";
	CHECK(feed(terminal, query, strlen(query)) == 0);
	query = "!|1\x1b" "2000$XYM$^M\r";
	CHECK(feed(terminal, query, strlen(query)) == 0);
	query_click.data.mouse.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT;
	(void)st_cterm_handle_mouse(terminal, &query_click);
	query_click.data.mouse.buttons = 0;
	CHECK(st_cterm_handle_mouse(terminal, &query_click));
	CHECK(output.user_size == strlen("32:19:100\rG\r")
	    && memcmp(output.user_bytes, "32:19:100\rG\r",
	    output.user_size) == 0);
	output.user_size = 0;
	query = "!|1\x1b" "2000T^M\r";
	CHECK(feed(terminal, query, strlen(query)) == 0);
	query_click.data.mouse.action = ST_CONSOLE_MOUSE_MOVE;
	query_click.data.mouse.pixel_x = 401;
	query_click.data.mouse.pixel_y = 201;
	CHECK(!st_cterm_handle_mouse(terminal, &query_click));
	query = "!|1\x1b" "0000$XY$^M\r";
	CHECK(feed(terminal, query, strlen(query)) == 0);
	CHECK(output.user_size == strlen("0191:00c9\r")
	    && memcmp(output.user_bytes, "0191:00c9\r",
	    output.user_size) == 0);
	output.user_size = 0;
	query_click.data.mouse.action = ST_CONSOLE_MOUSE_BUTTON;
	query_click.data.mouse.pixel_x = 400;
	query_click.data.mouse.pixel_y = 200;
	const char *field = "!|1M00" "0000HR9P" "0000000" "F^M\r";
	CHECK(feed(terminal, field, strlen(field)) == 0);
	query_click.data.mouse.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT;
	CHECK(st_cterm_handle_mouse(terminal, &query_click));
	query_click.data.mouse.buttons = 0;
	CHECK(st_cterm_handle_mouse(terminal, &query_click));
	CHECK(output.user_size == 2
	    && memcmp(output.user_bytes, "F\r", 2) == 0);
	output.user_size = 0;
	CHECK(feed(terminal, "!|1K\r", 5) == 0);
	query = "!|1\x1b" "1000$OFF$\r";
	CHECK(feed(terminal, query, strlen(query)) == 0);
	query_click.data.mouse.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT;
	(void)st_cterm_handle_mouse(terminal, &query_click);
	query_click.data.mouse.buttons = 0;
	CHECK(st_cterm_handle_mouse(terminal, &query_click));
	CHECK(output.user_size == 2
	    && memcmp(output.user_bytes, "T\r", 2) == 0);
	output.user_size = 0;
	/* RIP's post-command suppression consumes this LF, not UTF-8 input. */
	query = "!|1\x1b" "2000$OFF$\r\n";
	CHECK(feed(terminal, query, strlen(query)) == 0);
	CHECK(feed(terminal, "\xc3", 1) == 0);
	CHECK(feed(terminal, "\xa9", 1) == 0);
	CHECK(st_cterm_finish(terminal) == 0);
	CHECK(st_console_read_cells(console, &cell_rect, &cell, 1) == 0);
	CHECK(st_console_font_glyph_text(console, cell.glyph_font,
	    cell.glyph, text, sizeof(text), &required) == 0);
	CHECK(required == 2 && memcmp(text, "\xc3\xa9", 2) == 0);
	CHECK(st_cterm_reset_terminal(terminal) == 0);
	CHECK(feed(terminal, "!|X2T2T\r", 8) == 0);
	const struct st_console_rect reset_pixel = {101, 101, 1, 1};
	CHECK(st_console_get_pixels(&pixels, console, &reset_pixel, false) == 0);
	CHECK(pixels->primary[0] == 0x14);
	st_pixel_buffer_destroy(pixels);
	CHECK(feed(terminal, "\r!|w00002I1611\r", 15) == 0);
	CHECK(st_cterm_size(terminal, &columns, &rows) == 0);
	CHECK(columns == 91 && rows == 43);
	CHECK(st_console_cell_pixel_size(console, &cell_width,
	    &cell_height) == 0);
	CHECK(cell_width == 7 && cell_height == 8);
	rip_font = cterm_rip_text_font_data(1, &rip_font_size);
	CHECK(rip_font != NULL && rip_font_size == 256u * 8u);
	CHECK(feed(terminal, "\nA", 2) == 0);
	const struct st_console_rect glyph_rect = {0, 0, 7, 8};
	CHECK(st_console_get_pixels(&pixels, console, &glyph_rect, false) == 0);
	uint32_t background = pixels->primary[0];
	uint32_t foreground = pixels->primary[2];
	CHECK(background != foreground);
	for (uint32_t row = 0; row < 8; row++) {
		for (uint32_t column = 0; column < 7; column++) {
			bool ink = (rip_font['A' * 8u + row]
			    & (0x80u >> column)) != 0;
			CHECK(pixels->primary[row * 7u + column]
			    == (ink ? foreground : background));
		}
	}
	st_pixel_buffer_destroy(pixels);
	CHECK(st_console_get_pixels(&pixels, console, &reset_pixel, false) == 0);
	CHECK(pixels->primary[0] == 0x14);
	st_pixel_buffer_destroy(pixels);
	CHECK(feed(terminal, "\r", 1) == 0);
	CHECK(st_console_get_pixels(&pixels, console, &reset_pixel, false) == 0);
	CHECK(pixels->primary[0] == 0x14);
	st_pixel_buffer_destroy(pixels);
	CHECK(feed(terminal, "!|w0000130O14\r", 14) == 0);
	CHECK(st_cterm_size(terminal, &columns, &rows) == 0);
	CHECK(columns == 40 && rows == 25);
	CHECK(st_console_cell_pixel_size(console, &cell_width,
	    &cell_height) == 0);
	CHECK(cell_width == 16 && cell_height == 14);
	CHECK(st_console_get_default_cursor(console, &cursor) == 0);
	CHECK(cursor.start == 12 && cursor.end == 13 && cursor.range == 14);
	CHECK(st_console_get_pixels(&pixels, console, &reset_pixel, false) == 0);
	CHECK(pixels->primary[0] == 0x14);
	st_pixel_buffer_destroy(pixels);
	CHECK(feed(terminal, "!|*\r", 4) == 0);
	CHECK(st_cterm_size(terminal, &columns, &rows) == 0);
	CHECK(columns == 80 && rows == 43);
	const char *mouse_field = "!|1M002S2S2U2U1000000^M\r";
	CHECK(feed(terminal, mouse_field, strlen(mouse_field)) == 0);
	const char *second_field = "!|1M003C3C3E3E1000000^N\r";
	CHECK(feed(terminal, second_field, strlen(second_field)) == 0);
	{
		const struct st_console_rect focus = {97, 97, 8, 8};
		struct st_pixel_buffer *before = NULL, *highlight = NULL;
		struct st_pixel_buffer *cleared = NULL;

		CHECK(st_console_get_pixels(&before, console, &focus, false) == 0);
		CHECK(st_cterm_handle_key(terminal, '\t', 0));
		CHECK(st_console_get_pixels(&highlight, console, &focus, false)
		    == 0);
		CHECK(memcmp(before->primary, highlight->primary,
		    64 * sizeof(uint32_t)) != 0);
		output.user_size = 0;
		CHECK(st_cterm_handle_key(terminal, '\r', 0));
		CHECK(output.user_size == 1 && output.user_bytes[0] == '\r');
		CHECK(st_console_get_pixels(&cleared, console, &focus, false)
		    == 0);
		CHECK(memcmp(before->primary, cleared->primary,
		    64 * sizeof(uint32_t)) == 0);
		st_pixel_buffer_destroy(cleared);
		CHECK(st_cterm_handle_key(terminal, '\t', 0));
		CHECK(feed(terminal, "!|$TABOFF$\r", 11) == 0);
		CHECK(st_console_get_pixels(&cleared, console, &focus, false)
		    == 0);
		CHECK(memcmp(before->primary, cleared->primary,
		    64 * sizeof(uint32_t)) == 0);
		st_pixel_buffer_destroy(cleared);
		CHECK(feed(terminal, "!|$TABON$\r", 10) == 0);
		output.user_size = 0;
		CHECK(st_cterm_handle_key(terminal, '\t',
		    ST_CONSOLE_MODIFIER_SHIFT));
		CHECK(st_cterm_handle_key(terminal, '\r', 0));
		CHECK(output.user_size == 1 && output.user_bytes[0] == '\x0e');
		CHECK(st_cterm_handle_key(terminal, '\t', 0));
		(void)st_cterm_handle_doorway_key(terminal, 0x48);
		CHECK(st_console_get_pixels(&cleared, console, &focus, false)
		    == 0);
		CHECK(memcmp(before->primary, cleared->primary,
		    64 * sizeof(uint32_t)) == 0);
		st_pixel_buffer_destroy(cleared);
		st_pixel_buffer_destroy(highlight);
		st_pixel_buffer_destroy(before);
	}
	CHECK(feed(terminal, "!|X2T2T\r", 8) == 0);
	output.user_size = 0;
	CHECK(st_console_get_pixels(&pixels, console, &reset_pixel, false) == 0);
	uint32_t original = pixels->primary[0];
	st_pixel_buffer_destroy(pixels);
	struct st_console_event mouse = {
		.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {
			.pixel_x = 101, .pixel_y = 101,
			.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT,
			.action = ST_CONSOLE_MOUSE_BUTTON,
		},
	};
	CHECK(st_cterm_handle_mouse(terminal, &mouse));
	CHECK(st_console_get_pixels(&pixels, console, &reset_pixel, false) == 0);
	CHECK(pixels->primary[0] != original);
	st_pixel_buffer_destroy(pixels);
	mouse.data.mouse.buttons = 0;
	CHECK(st_cterm_handle_mouse(terminal, &mouse));
	CHECK(output.user_size == 1 && output.user_bytes[0] == '\r');
	CHECK(st_console_get_pixels(&pixels, console, &reset_pixel, false) == 0);
	CHECK(pixels->primary[0] == original);
	st_pixel_buffer_destroy(pixels);
	CHECK(feed(terminal, "!|1K\r", 5) == 0);
	mouse.data.mouse.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT;
	CHECK(!st_cterm_handle_mouse(terminal, &mouse));
	const char *button_wire = "\r!|1B140A0200ZK0300080F080700000707000000"
	    "|1U1O1E00002G00<>EXIT<>^M\r";
	CHECK(feed(terminal, button_wire, strlen(button_wire)) == 0);
	output.user_size = 0;
	CHECK(st_cterm_handle_rip_text(terminal, 'x'));
	CHECK(output.user_size == 1 && output.user_bytes[0] == '\r');
	CHECK(feed(terminal, "!|$HKEYOFF$\r", 12) == 0);
	CHECK(!st_cterm_handle_rip_text(terminal, 'X'));
	CHECK(feed(terminal, "!|$HKEYON$\r", 11) == 0);
	output.user_size = 0;
	CHECK(st_cterm_handle_rip_text(terminal, 'X'));
	CHECK(output.user_size == 1 && output.user_bytes[0] == '\r');
	CHECK(feed(terminal,
	    "!|1U1O1E00003M00<>ACCENT<>^M\r", 29) == 0);
	output.user_size = 0;
	CHECK(st_cterm_handle_rip_text(terminal, 0x00e9));
	CHECK(output.user_size == 1 && output.user_bytes[0] == '\r');
	mouse.data.mouse.pixel_x = 61;
	mouse.data.mouse.pixel_y = 51;
	mouse.data.mouse.buttons = 0;
	CHECK(!st_cterm_handle_mouse(terminal, &mouse));
	CHECK(feed(terminal, "!|$SMF$\r", 8) == 0);
	CHECK(feed(terminal, "!|$MKILL$\r", 10) == 0);
	mouse.data.mouse.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT;
	CHECK(!st_cterm_handle_mouse(terminal, &mouse));
	mouse.data.mouse.buttons = 0;
	CHECK(!st_cterm_handle_mouse(terminal, &mouse));
	CHECK(feed(terminal, "!|$RMF$\r", 8) == 0);
	CHECK(feed(terminal, "!|$SAVEALL$\r", 12) == 0);
	CHECK(feed(terminal, "!|$MKILL$\r", 10) == 0);
	CHECK(feed(terminal, "!|$RESTOREALL$\r", 15) == 0);
	mouse.data.mouse.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT;
	output.user_size = 0;
	CHECK(st_cterm_handle_mouse(terminal, &mouse));
	mouse.data.mouse.buttons = 0;
	CHECK(st_cterm_handle_mouse(terminal, &mouse));
	CHECK(output.user_size == 1 && output.user_bytes[0] == '\r');
	CHECK(feed(terminal, "!|1K\r", 5) == 0);
	mouse.data.mouse.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT;
	CHECK(!st_cterm_handle_mouse(terminal, &mouse));
	mouse.data.mouse.buttons = 0;
	CHECK(!st_cterm_handle_mouse(terminal, &mouse));
	const char *bevel_button = "\r!|1B140A0201DU0100080F080700000707000000"
	    "|1U2S2800000000<>EDGE<>^M\r";
	CHECK(feed(terminal, bevel_button, strlen(bevel_button)) == 0);
	const struct st_console_rect bevel_corner = {99, 79, 1, 1};
	CHECK(st_console_get_pixels(&pixels, console, &bevel_corner, false) == 0);
	uint32_t bevel_before = pixels->primary[0];
	st_pixel_buffer_destroy(pixels);
	mouse.data.mouse.pixel_x = 101;
	mouse.data.mouse.pixel_y = 81;
	mouse.data.mouse.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT;
	CHECK(st_cterm_handle_mouse(terminal, &mouse));
	CHECK(st_console_get_pixels(&pixels, console, &bevel_corner, false) == 0);
	CHECK(pixels->primary[0] != bevel_before);
	st_pixel_buffer_destroy(pixels);
	mouse.data.mouse.buttons = 0;
	CHECK(st_cterm_handle_mouse(terminal, &mouse));
	CHECK(st_console_get_pixels(&pixels, console, &bevel_corner, false) == 0);
	CHECK(pixels->primary[0] == bevel_before);
	st_pixel_buffer_destroy(pixels);
	CHECK(feed(terminal, "!|1K\r", 5) == 0);
	const char *radio_buttons = "\r!|1B140A020DMO0300080F080701000707000000"
	    "|1U4G2800000000<>A<>|1U642800000000<>B<>\r";
	CHECK(feed(terminal, radio_buttons, strlen(radio_buttons)) == 0);
	uint32_t radio_off, radio_on, radio_other;
	CHECK(sample_pixel(console, 161, 81, &radio_off) == 0);
	CHECK(sample_pixel(console, 221, 81, &radio_other) == 0);
	CHECK(radio_other == radio_off);
	const char *checkbox_style = "\r!|1B140A0200ZK0300080F080700010707000000\r";
	CHECK(feed(terminal, checkbox_style, strlen(checkbox_style)) == 0);
	CHECK(click_pixel(terminal, &mouse, 161, 81) == 0);
	CHECK(sample_pixel(console, 161, 81, &radio_on) == 0);
	CHECK(radio_on != radio_off);
	CHECK(click_pixel(terminal, &mouse, 221, 81) == 0);
	CHECK(sample_pixel(console, 161, 81, &radio_other) == 0);
	CHECK(radio_other == radio_off);
	CHECK(sample_pixel(console, 221, 81, &radio_other) == 0);
	CHECK(radio_other == radio_on);
	CHECK(feed(terminal, "!|1K\r", 5) == 0);
	const char *checkbox = "\r!|1B140A0200ZK0300080F080700010707000000"
	    "|1U7S2800000000<>C<>\r";
	CHECK(feed(terminal, checkbox, strlen(checkbox)) == 0);
	uint32_t checkbox_off, checkbox_on;
	CHECK(sample_pixel(console, 281, 81, &checkbox_off) == 0);
	CHECK(click_pixel(terminal, &mouse, 281, 81) == 0);
	CHECK(sample_pixel(console, 281, 81, &checkbox_on) == 0);
	CHECK(checkbox_on != checkbox_off);
	CHECK(click_pixel(terminal, &mouse, 281, 81) == 0);
	CHECK(sample_pixel(console, 281, 81, &radio_other) == 0);
	CHECK(radio_other == checkbox_off);
	CHECK(feed(terminal, "!|1K\r", 5) == 0);
	const char *clipboard_button =
	    "\r!|c0F|X0A0A|1C0A0A0A0A00"
	    "|1B140A0200SJ0300080F080700000707000000"
	    "|1U2S2800000000<>CLIP<>^M\r";
	CHECK(feed(terminal, clipboard_button,
	    strlen(clipboard_button)) == 0);
	uint32_t clipboard_normal, clipboard_pressed;
	CHECK(sample_pixel(console, 100, 80, &clipboard_normal) == 0);
	mouse.data.mouse.pixel_x = 100;
	mouse.data.mouse.pixel_y = 80;
	mouse.data.mouse.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT;
	CHECK(st_cterm_handle_mouse(terminal, &mouse));
	CHECK(sample_pixel(console, 100, 80, &clipboard_pressed) == 0);
	CHECK(clipboard_pressed != clipboard_normal);
	output.user_size = 0;
	mouse.data.mouse.buttons = 0;
	CHECK(st_cterm_handle_mouse(terminal, &mouse));
	CHECK(output.user_size == 1 && output.user_bytes[0] == '\r');
	CHECK(feed(terminal, "!|1K\r", 5) == 0);
	const char *icon_button =
	    "\r!|1B140A0200W20300080F080700000707000000"
	    "|1U3C2800000000BUTTON<>ICON<>^M\r";
	CHECK(feed(terminal, icon_button, strlen(icon_button)) == 0);
	uint32_t icon_normal, icon_pressed;
	CHECK(sample_pixel(console, 120, 80, &icon_normal) == 0);
	mouse.data.mouse.pixel_x = 120;
	mouse.data.mouse.pixel_y = 80;
	mouse.data.mouse.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT;
	CHECK(st_cterm_handle_mouse(terminal, &mouse));
	CHECK(sample_pixel(console, 120, 80, &icon_pressed) == 0);
	CHECK(icon_pressed != icon_normal);
	output.user_size = 0;
	mouse.data.mouse.buttons = 0;
	CHECK(st_cterm_handle_mouse(terminal, &mouse));
	CHECK(output.user_size == 1 && output.user_bytes[0] == '\r');
	CHECK(feed(terminal, "!|1K\r", 5) == 0);
	const char *hot_icon_button =
	    "\r!|1B140A02041U0300080F080700000707000000"
	    "|1U4G2800000000BUTTON<><>^M"
	    "|1I5K2800000BUTTON.HIC\r";
	CHECK(feed(terminal, hot_icon_button,
	    strlen(hot_icon_button)) == 0);
	const struct st_console_rect hot_button_rect = {160, 80, 22, 17};
	const struct st_console_rect hot_icon_rect = {200, 80, 22, 17};
	struct st_pixel_buffer *hot_button_pixels = NULL;
	struct st_pixel_buffer *hot_icon_pixels = NULL;
	mouse.data.mouse.pixel_x = 160;
	mouse.data.mouse.pixel_y = 80;
	mouse.data.mouse.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT;
	CHECK(st_cterm_handle_mouse(terminal, &mouse));
	CHECK(st_console_get_pixels(&hot_button_pixels, console,
	    &hot_button_rect, false) == 0);
	CHECK(st_console_get_pixels(&hot_icon_pixels, console,
	    &hot_icon_rect, false) == 0);
	CHECK(memcmp(hot_button_pixels->primary, hot_icon_pixels->primary,
	    22 * 17 * sizeof(uint32_t)) == 0);
	st_pixel_buffer_destroy(hot_icon_pixels);
	st_pixel_buffer_destroy(hot_button_pixels);
	output.user_size = 0;
	mouse.data.mouse.buttons = 0;
	CHECK(st_cterm_handle_mouse(terminal, &mouse));
	CHECK(output.user_size == 1 && output.user_bytes[0] == '\r');
	CHECK(feed(terminal, "!|1K\r", 5) == 0);
	const char *autostamp_buttons =
	    "\r!|1B140A02011E0300080F080700000707000000"
	    "|1U4G2800000000<>AUTO<>^M"
	    "|1U5K2800000000<>COPY<>^M\r";
	CHECK(feed(terminal, autostamp_buttons,
	    strlen(autostamp_buttons)) == 0);
	uint32_t autostamp_normal, autostamp_pressed;
	CHECK(sample_pixel(console, 200, 80, &autostamp_normal) == 0);
	mouse.data.mouse.pixel_x = 200;
	mouse.data.mouse.pixel_y = 80;
	mouse.data.mouse.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT;
	CHECK(st_cterm_handle_mouse(terminal, &mouse));
	CHECK(sample_pixel(console, 200, 80, &autostamp_pressed) == 0);
	CHECK(autostamp_pressed != autostamp_normal);
	output.user_size = 0;
	mouse.data.mouse.buttons = 0;
	CHECK(st_cterm_handle_mouse(terminal, &mouse));
	CHECK(output.user_size == 1 && output.user_bytes[0] == '\r');
	CHECK(feed(terminal, "!|1K\r", 5) == 0);
	const char *extended_seed = "\r!|c0E|X2S2S\r";
	CHECK(feed(terminal, extended_seed, strlen(extended_seed)) == 0);
	uint32_t extended_before;
	CHECK(sample_pixel(console, 100, 100, &extended_before) == 0);
	CHECK(extended_before != 0x10);
	const char *extended_window = "\r!|b080G934V140A2000X\r";
	CHECK(feed(terminal, extended_window,
	    strlen(extended_window)) == 0);
	CHECK(st_cterm_size(terminal, &columns, &rows) == 0);
	CHECK(columns == 80 && rows == 21);
	CHECK(st_console_cell_pixel_size(console, &cell_width,
	    &cell_height) == 0);
	CHECK(cell_width == 8 && cell_height == 16);
	struct st_console_rect extended_cells;
	CHECK(st_console_get_window(console, &extended_cells) == 0);
	CHECK(extended_cells.x == 0 && extended_cells.y == 0
	    && extended_cells.width == 40 && extended_cells.height == 10);
	CHECK(sample_pixel(console, 100, 100, &extended_before) == 0);
	CHECK(extended_before == 0x10);
	const char *wide_window = "\r!|b0G0E4V490A0A4000G\r";
	CHECK(feed(terminal, wide_window, strlen(wide_window)) == 0);
	CHECK(st_cterm_size(terminal, &columns, &rows) == 0);
	CHECK(columns == 40 && rows == 25);
	CHECK(st_console_cell_pixel_size(console, &cell_width,
	    &cell_height) == 0);
	CHECK(cell_width == 16 && cell_height == 14);
	CHECK(st_console_get_window(console, &extended_cells) == 0);
	CHECK(extended_cells.x == 0 && extended_cells.y == 0
	    && extended_cells.width == 10 && extended_cells.height == 10);
	CHECK(feed(terminal, "!|*\r", 4) == 0);
	CHECK(feed(terminal, "!|c01\r", 6) == 0);
	const char *status_on = "\n\x1b[1$~";
	CHECK(feed(terminal, status_on, strlen(status_on)) == 0);
	CHECK(st_cterm_size(terminal, &columns, &rows) == 0);
	CHECK(columns == 80 && rows == 42);
	query = "\r!|1\x1b" "0000$SBAROFF$\r";
	CHECK(feed(terminal, query, strlen(query)) == 0);
	CHECK(st_cterm_size(terminal, &columns, &rows) == 0);
	CHECK(columns == 80 && rows == 43);
	const char *poly_wire = "\n!|t030102S2S03C2S03C3C\r";
	size_t poly_wire_size = strlen(poly_wire);
	CHECK(feed(terminal, poly_wire, 11) == 0);
	CHECK(feed(terminal, poly_wire + 11, poly_wire_size - 11) == 0);
	const struct st_console_rect poly_pixel = {110, 100, 1, 1};
	CHECK(st_console_get_pixels(&pixels, console, &poly_pixel, false) == 0);
	CHECK(pixels->primary[0] == 0x14);
	st_pixel_buffer_destroy(pixels);
	/* The legacy RIP_COPY_REGION handler is a no-op, but it must not
	 * stop the remaining commands in the same RIP line. */
	const char *copy_region = "\r!|1G000010101000|c0E|X2S2S\r";
	CHECK(feed(terminal, copy_region, strlen(copy_region)) == 0);
	CHECK(sample_pixel(console, 100, 100, &original) == 0);
	CHECK(original == 0x1b);
	st_cterm_destroy(&terminal);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	return 0;
}
