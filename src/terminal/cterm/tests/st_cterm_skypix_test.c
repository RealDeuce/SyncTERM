/* Independent wire, pixel, font, and transfer-boundary expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_skypix.h"
#include "io_error.h"
#include "st_builtin_fonts.h"
#include "st_cterm.h"
#include "st_session_history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
	do {                                                                                           \
		if (!(x)) {                                                                                \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
			exit(1);                                                                               \
		}                                                                                          \
	} while (false)
struct parser_result {
	unsigned commands;
	unsigned values[17];
	size_t count;
	char name[100];
	char plain[100];
	size_t size;
	bool paused;
};
static int plain(void *context, const uint8_t *bytes, size_t size)
{
	struct parser_result *r = context;
	CHECK(size <= sizeof(r->plain) - r->size);
	memcpy(r->plain + r->size, bytes, size);
	r->size += size;
	return 0;
}
static int parsed(void *context, const unsigned *p, size_t count, const uint8_t *bytes, size_t size)
{
	struct parser_result *r = context;
	++r->commands;
	memcpy(r->values, p, count * sizeof(*p));
	r->count = count;
	CHECK(size < sizeof(r->name));
	if (size != 0)
		memcpy(r->name, bytes, size);
	r->name[size] = 0;
	if (p[0] == 16)
		r->paused = true;
	return 0;
}
static bool paused(void *context) { return ((struct parser_result *)context)->paused; }
static void parser_tests(void)
{
	const char wire[] = "A\x1b[31mB\x1b[10;19!Sapphire.font!C\x1b[16;1;20;10!brush.iff!\nTAIL";
	size_t boundary = strlen(wire) - 5;
	for (size_t split = 0; split <= boundary; ++split) {
		struct cterm_skypix_parser parser = {0};
		struct parser_result r = {0};
		struct cterm_skypix_sink s = {&r, plain, parsed, paused};
		size_t used;
		CHECK(cterm_skypix_parser_feed(&parser, &s, (const uint8_t *)wire, split, &used) == 0 &&
			  used == split);
		CHECK(cterm_skypix_parser_feed(&parser, &s, (const uint8_t *)wire + split,
									   strlen(wire) - split, &used) == 0 &&
			  used == boundary - split);
		CHECK(r.commands == 2 && r.count == 4 && r.values[1] == 1 &&
			  strcmp(r.name, "brush.iff") == 0);
		CHECK(r.size == 8 && memcmp(r.plain, "A\x1b[31mBC", 8) == 0);
	}
	struct cterm_skypix_parser parser = {0};
	struct parser_result r = {0};
	struct cterm_skypix_sink s = {&r, plain, parsed, paused};
	size_t used;
	const char malformed[] = "\x1b[1;999999999999999999999!\x1b[15;2!\x1b]x\x1b[15;4!\aZ";
	CHECK(cterm_skypix_parser_feed(&parser, &s, (const uint8_t *)malformed, strlen(malformed),
								   &used) == 0 &&
		  used == strlen(malformed));
	CHECK(r.commands == 2 && r.values[1] == 2 && r.plain[r.size - 1] == 'Z');
}
static void feed(st_cterm_t *t, const char *text)
{
	size_t used;
	CHECK(st_cterm_write(t, text, strlen(text), &used) == 0 && used == strlen(text));
}
static void engine_tests(bool status_line, enum st_cterm_graphics_mode graphics)
{
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *t = NULL;
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {.columns = 80,
											   .rows = status_line ? 26 : 25,
											   .cell_width = 8,
											   .cell_height = 8,
											   .wrap = true};
	CHECK(st_console_create(&console, &display, &geometry) == 0);
	struct st_console_font font;
	uint16_t identifier;
	CHECK(st_builtin_font_get(0, 8, 8, &font));
	CHECK(st_console_register_static_font(console, &font, &identifier) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config config = {.console = console,
										   .fonts = fonts,
										   .emulation = ST_CTERM_ANSI_BBS,
										   .encoding = ST_CTERM_ENCODING_CODEPAGE,
										   .codepage = ST_CODEPAGE_ISO_8859_1,
										   .graphics_mode = graphics,
										   .status_line = status_line,
										   .scrollback_lines = 5,
										   .quiet = true};
	CHECK(st_cterm_create(&t, &config) == 0);
	struct st_cterm_state state;
	CHECK(st_cterm_state(t, &state) == 0 && state.skypix && state.graphics_mode == graphics);
	CHECK(t->rip_engine == NULL && t->skypix_engine != NULL);
	CHECK(st_cterm_resize(t, 100, geometry.rows) == IO_ERROR_NOT_SUPPORTED);
	CHECK(st_cterm_resize(t, 80, geometry.rows + 1) == IO_ERROR_NOT_SUPPORTED);
	CHECK(st_cterm_resize(t, 80, geometry.rows) == 0);
	CHECK(t->width == 80 && t->height == 25);
	CHECK(st_cterm_set_encoding(t, ST_CTERM_ENCODING_UTF8, ST_CODEPAGE_ISO_8859_1)
		== 0);
	CHECK(st_cterm_set_encoding(t, ST_CTERM_ENCODING_CODEPAGE, ST_CODEPAGE_ISO_8859_1) == 0);
	uint32_t palette[16];
	CHECK(st_console_get_palette(console, 512, palette, 16) == 0);
	CHECK(palette[1] == 0x1111ff && palette[3] == 0xff0000);
	feed(t, "\x1b[15;3!\x1b[4;10;10;14;14!\x1b[6;10;9;5;5!\x1b[7;0;0;20;20;5;5;192;255!");
	feed(t, "\x1b[15;4!\x1b[1;20;20!\x1b[7;0;0;20;20;1;1;96;1!"); /* XOR plane zero only: green 4 ->
																	 5 */
	struct st_pixel_buffer *pixels = NULL;
	const struct st_console_rect rect = {0, 0, 640, 200};
	CHECK(st_console_get_pixels(&pixels, console, &rect, false) == 0);
	CHECK(memcmp(pixels->primary, pixels->alternate, 640 * 200 * sizeof(*pixels->primary)) == 0);
	CHECK(pixels->primary[10 * 640 + 10] == 515 && pixels->primary[14 * 640 + 14] == 515 &&
		  pixels->primary[15 * 640 + 14] == 512);
	CHECK(pixels->primary[20 * 640 + 20] == 517 && pixels->primary[24 * 640 + 24] == 515);
	st_pixel_buffer_destroy(pixels);
	/* JR-Comm's 0x80 packet repeats 129 bytes; the brush can exceed the
	 * viewport, and only matching source/destination planes participate. */
	uint8_t ilbm[80] = {0};
	memcpy(ilbm, "FORM", 4);
	ilbm[7] = 72;
	memcpy(ilbm + 8, "ILBMBMHD", 8);
	ilbm[19] = 20;
	ilbm[20] = 4;
	ilbm[21] = 128;
	ilbm[23] = 1;
	ilbm[28] = 1;
	ilbm[30] = 1;
	memcpy(ilbm + 40, "CMAP", 4);
	ilbm[47] = 6;
	memcpy(ilbm + 54, "BODY", 4);
	ilbm[61] = 18;
	ilbm[62] = 128;
	ilbm[63] = 0x55;
	ilbm[64] = 14;
	CHECK(st_cterm_skypix_receive(t, ilbm, sizeof(ilbm)) == 0);
	CHECK(t->skypix_engine->brush->width == 1152 && t->skypix_engine->brush_depth == 1);
	CHECK(t->skypix_engine->brush->primary[0] == 0 && t->skypix_engine->brush->primary[1] == 1);
	CHECK(t->skypix_engine->brush->primary[1031] == 1 &&
		  t->skypix_engine->brush->primary[1032] == 0);
	feed(t, "\x1b[15;4!\x1b[4;100;100;109;100!\x1b[7;0;0;100;100;8;1;192;255!");
	const struct st_console_rect planes = {100, 100, 8, 1};
	CHECK(st_console_get_pixels(&pixels, console, &planes, false) == 0);
	for (unsigned x = 0; x < 8; ++x)
		CHECK(pixels->primary[x] == 516 + (x & 1));
	st_pixel_buffer_destroy(pixels);
	memcpy(ilbm + 40, "JUNK", 4);
	CHECK(st_cterm_skypix_receive(t, ilbm, sizeof(ilbm)) == IO_ERROR_INVALID_FORMAT);
	CHECK(t->skypix_engine->brush->width == 1152);
	feed(t, "\x1b[11;3840;15;0;0;0;0;0;0;0;0;0;0;0;0;0;0!");
	CHECK(st_console_get_palette(console, 512, palette, 16) == 0 && palette[0] == 0xff0000 &&
		  palette[1] == 0x0000ff);
	feed(t, "\x1b[19;100;50!\x1b[10;19!Sapphire.font!R\r\n");
	CHECK(t->skypix_engine->custom && t->skypix_engine->x == 0 && t->skypix_engine->y == 69);
	feed(t, "\x1b[10;0!");
	CHECK(!t->skypix_engine->custom && t->skypix_engine->y == 69);
	feed(t, "\x1b[10;77!missing.font!");
	CHECK(t->skypix_engine->custom && st_amiga_font_height(t->skypix_engine->font) == 8);
	const char delay[] = "\x1b[14;60!\x1b[15;4!";
	size_t used;
	CHECK(st_cterm_write(t, delay, strlen(delay), &used) == 0 && used == 8 &&
		  st_cterm_input_paused(t));
	CHECK(st_cterm_reset_terminal(t) == 0 && !st_cterm_input_paused(t));
	feed(t, "\x1b[2J\x1b[H\x1b[19;16;21!\x1b[10;20!Diamond.font!<");
	CHECK(st_cterm_finish(t) == 0);
	const struct st_console_rect overhang = {26, 29, 1, 9};
	CHECK(st_console_get_pixels(&pixels, console, &overhang, false) == 0);
	unsigned ink = graphics == ST_CTERM_GRAPHICS_SKYPIX_13 ? 512 : 514;
	CHECK(pixels->primary[0] == ink && pixels->primary[8] == ink);
	st_pixel_buffer_destroy(pixels);
	feed(t, " ");
	CHECK(st_cterm_finish(t) == 0);
	CHECK(st_console_get_pixels(&pixels, console, &overhang, false) == 0);
	CHECK(pixels->primary[0] == 514 && pixels->primary[8] == 514);
	st_pixel_buffer_destroy(pixels);
	/* Updated independent cursor probes: Diamond's underline follows its
	 * baseline and proportional advance, with two-pixel sprite quantization. */
	feed(t, "\x1b[10;20!Diamond.font!\f");
	CHECK(st_cterm_finish(t) == 0);
	struct st_console_pixel_cursor cursor;
	CHECK(st_console_get_pixel_cursor(console, &cursor) == 0);
	CHECK(cursor.enabled && cursor.visible && cursor.rectangle.x == 0 &&
		cursor.rectangle.y == 15 && cursor.rectangle.width == 14 && cursor.rectangle.height == 1);
	uint32_t cursor_colour = cursor.colour;
	feed(t, "i\x1b[5n");
	CHECK(st_cterm_finish(t) == 0);
	CHECK(st_console_get_pixel_cursor(console, &cursor) == 0 && cursor.rectangle.x == 2);
	feed(t, "W");
	CHECK(st_cterm_finish(t) == 0);
	CHECK(st_console_get_pixel_cursor(console, &cursor) == 0 && cursor.rectangle.x == 22);
	t->skypix_engine->cursor_deadline = 1000;
	CHECK(cterm_skypix_poll(t->skypix_engine, 999) == 0);
	CHECK(st_console_get_pixel_cursor(console, &cursor) == 0 && cursor.visible);
	CHECK(cterm_skypix_poll(t->skypix_engine, 1000) == 0);
	CHECK(st_console_get_pixel_cursor(console, &cursor) == 0 && !cursor.visible);
	feed(t, "\x1b[19;101;40!");
	CHECK(st_console_get_pixel_cursor(console, &cursor) == 0 && cursor.visible &&
		cursor.rectangle.x == 98 && cursor.rectangle.y == 60 && cursor.rectangle.width == 16);
	CHECK(t->skypix_engine->cursor_deadline == 1185);
	cterm_skypix_cursor_mode(t->skypix_engine, true, true);
	CHECK(st_console_get_pixel_cursor(console, &cursor) == 0 && !cursor.visible);
	cterm_skypix_cursor_mode(t->skypix_engine, false, true);
	CHECK(st_console_get_pixel_cursor(console, &cursor) == 0 && !cursor.visible);
	CHECK(cterm_skypix_poll(t->skypix_engine, 1185) == 0);
	CHECK(st_console_get_pixel_cursor(console, &cursor) == 0 && cursor.visible);
	cterm_skypix_cursor_mode(t->skypix_engine, false, false);
	CHECK(cterm_skypix_poll(t->skypix_engine, 1370) == 0);
	CHECK(st_console_get_pixel_cursor(console, &cursor) == 0 && cursor.visible);
	feed(t, "\x1b[11;0;0;0;0;0;0;0;0;0;0;0;0;0;0;0;0!");
	CHECK(st_console_get_pixel_cursor(console, &cursor) == 0 && cursor.colour == cursor_colour);
	feed(t, "\x1b[12!\x1b[10;0!");
	t->skypix_engine->cursor_deadline = 0;
	/* JR-Comm position probes: CR/LF are independent and BS/HT use
	 * nominal Diamond/20 width, never the last proportional advance. */
	feed(t, "\x1b[19;100;40!\x1b[10;20!Diamond.font!i\r");
	CHECK(st_cterm_finish(t) == 0);
	CHECK(t->skypix_engine->x == 0 && t->skypix_engine->y == 40);
	feed(t, "\n");
	CHECK(t->skypix_engine->x == 0 && t->skypix_engine->y == 60);
	feed(t, "\x1b[19;0;40!i\b");
	CHECK(t->skypix_engine->x == -16 && t->skypix_engine->y == 40);
	feed(t, "\b");
	CHECK(t->skypix_engine->x == -16);
	feed(t, "\x1b[19;100;40!\t");
	CHECK(t->skypix_engine->x == 160 && t->skypix_engine->y == 40);
	feed(t, "\v");
	CHECK(t->skypix_engine->x == 160 && t->skypix_engine->y == 40);
	feed(t, "\x1b[19;13;51!\x1b[10;0!B\rC\nD");
	CHECK(st_cterm_finish(t) == 0);
	CHECK(t->skypix_engine->x == 16 && t->skypix_engine->y == 59);
	CHECK(st_cterm_finish(t) == 0);
	uint32_t width, height;
	CHECK(st_console_pixel_size(console, &width, &height) == 0 && width == 640 &&
		  height == (status_line ? 208u : 200u));
	st_cterm_destroy(&t);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
}
static void graphics_history_test(void)
{
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {
		.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 8, .wrap = true};
	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config config = {.console = console, .fonts = fonts,
		.emulation = ST_CTERM_ANSI_BBS, .encoding = ST_CTERM_ENCODING_CODEPAGE,
		.codepage = ST_CODEPAGE_ISO_8859_1, .graphics_mode = ST_CTERM_GRAPHICS_SKYPIX_31,
		.scrollback_lines = 3};
	CHECK(st_cterm_create(&terminal, &config) == 0);
	feed(terminal, "\x1b[10;8!Pearl.font!\x1b[H");
	for (unsigned row = 0; row < 30; ++row)
		feed(terminal, "A\r\n");
	uint32_t columns;
	size_t lines;
	CHECK(st_cterm_scrollback_size(terminal, &columns, &lines) == 0 && lines == 3);
	struct st_pixel_buffer *pixels = NULL;
	CHECK(st_cterm_copy_history_pixels(terminal, 0, &pixels) == 0 && pixels != NULL);
	CHECK(pixels->width == 640 && pixels->height == 8);
	/* Pearl A, first source row 0x10. Historical pixels are RGB snapshots. */
	CHECK(pixels->primary[3] == ST_PIXEL_COLOR_RGB_VALUE(221, 221, 221));
	CHECK(pixels->primary[0] == ST_PIXEL_COLOR_RGB_VALUE(0, 0, 0));
	st_pixel_buffer_destroy(pixels);
	int position = terminal->backpos;
	feed(terminal, "\x1b[19;0;32700!\n");
	CHECK(terminal->backpos == position);
	st_cterm_destroy(&terminal);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
}
int main(void)
{
	graphics_history_test();
	parser_tests();
	engine_tests(false, ST_CTERM_GRAPHICS_SKYPIX_13);
	engine_tests(true, ST_CTERM_GRAPHICS_SKYPIX_13);
	engine_tests(false, ST_CTERM_GRAPHICS_SKYPIX_31);
	engine_tests(true, ST_CTERM_GRAPHICS_SKYPIX_31);
	return 0;
}
