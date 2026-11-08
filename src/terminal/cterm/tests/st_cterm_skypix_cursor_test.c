/* SkyTERM cursor RectFill expectations, independent of SyncTERM output.
 * Reference: SKYTERM_PROTOCOL.adoc, H01:2cfe..2d68, 5d0c..5d82;
 * native/ROM cursor probe rectangles for 8x8, 9x12 and 20x20 metrics. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_skypix.h"
#include "st_amiga_fonts.h"
#include "st_cterm.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%d: %s\n", __LINE__, #e); exit(1); } } while (false)

static void feed(st_cterm_t *t, const char *wire)
{
	size_t used, size = strlen(wire);
	CHECK(st_cterm_write(t, wire, size, &used) == 0 && used == size);
	CHECK(st_cterm_finish(t) == 0);
}

static void rectangle(st_cterm_t *t, unsigned x, unsigned y, unsigned width, uint32_t colour)
{
	struct st_pixel_buffer *pixels = NULL;
	const struct st_console_rect area = {x, y, width, 1};
	CHECK(st_console_get_pixels(&pixels, t->screen.console, &area, false) == 0);
	for (unsigned i = 0; i < width; ++i) {
		if (pixels->primary[i] != colour || pixels->alternate[i] != colour)
			fprintf(stderr, "pixel %u,%u: %u/%u expected %u\n", x + i, y,
				pixels->primary[i], pixels->alternate[i], colour);
		CHECK(pixels->primary[i] == colour && pixels->alternate[i] == colour);
	}
	st_pixel_buffer_destroy(pixels);
}

static void run(enum st_cterm_graphics_mode mode, bool utf8, bool status)
{
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *t = NULL;
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config grid = {.columns = 80, .rows = 25,
		.cell_width = 8, .cell_height = 8, .wrap = true};
	CHECK(st_console_create(&console, &display, &grid) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config config = {.console = console, .fonts = fonts,
		.emulation = ST_CTERM_ANSI_BBS, .graphics_mode = mode, .status_line = status,
		.encoding = utf8 ? ST_CTERM_ENCODING_UTF8 : ST_CTERM_ENCODING_CODEPAGE,
		.codepage = ST_CODEPAGE_CP437, .scrollback_lines = 3, .quiet = true};
	CHECK(st_cterm_create(&t, &config) == 0);
	feed(t, "\f");
	/* Default home is zero-origin with the complete Pearl/8 strike. The
	 * cursor is at its text baseline, not one row below it. */
	rectangle(t, 0, 6, 8, 519);
	struct st_console_pixel_cursor overlay;
	CHECK(st_console_get_pixel_cursor(console, &overlay) == 0 && !overlay.enabled);
	bool visible;
	enum st_console_cursor_shape shape;
	CHECK(st_console_get_cursor(console, &visible, &shape) == 0 && !visible);
	feed(t, "A");
	for (unsigned x = 0; x < 8; ++x)
		rectangle(t, x, 6, 1, (0xc6u & (128u >> x)) ? 514 : 512);
	rectangle(t, 8, 6, 8, 519);
	feed(t, "\b");
	for (unsigned x = 0; x < 8; ++x)
		rectangle(t, x, 6, 1, (0xc6u & (128u >> x)) ? 517 : 519);
	feed(t, "\x1b[C");
	for (unsigned x = 0; x < 8; ++x)
		rectangle(t, x, 6, 1, (0xc6u & (128u >> x)) ? 514 : 512);
	rectangle(t, 8, 6, 8, 519);
	CHECK(cterm_skypix_poll(t->skypix_engine, 1) == 0);
	CHECK(cterm_skypix_poll(t->skypix_engine, 10000) == 0);
	rectangle(t, 8, 6, 8, 519); /* Idle time has no phase effect. */
	feed(t, "\x1b" "7B\x1b" "8");
	rectangle(t, 16, 6, 8, 512); /* DECSC/DECRC must not abandon B's cursor. */
	for (unsigned x = 0; x < 8; ++x)
		rectangle(t, 8 + x, 6, 1, (0xfcu & (128u >> x)) ? 517 : 519);
	feed(t, "\fA");
	feed(t, "\x1b[65535!"); /* Unknown SkyPix commands still erase/disable C. */
	rectangle(t, 8, 6, 8, 512);
	feed(t, "B");
	rectangle(t, 16, 6, 8, 512);
	feed(t, "\x1b[10;0!"); /* Enable operations without drawing a new cursor. */
	rectangle(t, 16, 6, 8, 512);
	feed(t, "A");
	rectangle(t, 24, 6, 8, 519);
	feed(t, "\x1b[99!");
	rectangle(t, 24, 6, 8, 512);
	feed(t, "\x1b[0mA");
	rectangle(t, 32, 6, 8, 519);
	feed(t, "\x1b[10;20!Diamond.font!\f");
	rectangle(t, 0, 14, 20, 512);
	/* Cursor entry happens at the FIRST bang, before a split font name. */
	feed(t, "\x1b[10;0!\fA");
	size_t used;
	CHECK(st_cterm_write(t, "\x1b[10;20!", 8, &used) == 0 && used == 8);
	CHECK(!t->skypix_engine->cursor_enabled);
	rectangle(t, 8, 6, 8, 512);
	feed(t, "Diamond.font!");
	CHECK(t->skypix_engine->custom);
	/* Save/restore moves one live cursor cleanly. A brush cannot capture
	 * an underline abandoned at an earlier position. */
	feed(t, "\x1b[10;0!\fA\x1b[sB\x1b[u\x1b[6;16;5;8;1!");
	CHECK(t->skypix_engine->brush != NULL && t->skypix_engine->brush->width == 8);
	for (unsigned i = 0; i < 8; ++i) CHECK(t->skypix_engine->brush->primary[i] == 512);
	feed(t, "\x1b[7;0;0;32;6;8;1;192;255!");
	rectangle(t, 32, 6, 8, 512);
	/* A cursor temporarily moved home must not be retired into history
	 * or copied upward by the next LF at the lower edge. */
	char scroll[100];
	snprintf(scroll, sizeof(scroll), "\x1b[0m\x1b[2J\x1b[%d;1HA\x1b[s\x1b[H\x1b[u\n", t->height);
	feed(t, scroll);
	uint32_t columns;
	size_t rows;
	CHECK(st_cterm_scrollback_size(t, &columns, &rows) == 0 && rows != 0);
	struct st_pixel_buffer *history = NULL;
	CHECK(st_cterm_copy_history_pixels(t, rows - 1, &history) == 0 && history != NULL);
	for (unsigned i = 0; i < 8; ++i)
		CHECK(history->primary[6 * 640 + i] == ST_PIXEL_COLOR_RGB_TAG);
	st_pixel_buffer_destroy(history);
	rectangle(t, 8, (unsigned)t->height * 8 - 10, 8, 512);
	rectangle(t, 8, (unsigned)t->height * 8 - 2, 8, 519);
	/* Full depth and palette indices, never a dedicated cursor RGB colour. */
	feed(t, "\x1b[17;2!\x1b[0m\f");
	rectangle(t, 0, 6, 8, 527);
	/* Every input split, including named-font framing and reset/recovery. */
	const char wire[] = "\fA\x1b[1;0!B\x1b[10;0!A";
	for (size_t split = 0; split < sizeof(wire); ++split) {
		CHECK(st_cterm_reset_terminal(t) == 0);
		CHECK(st_cterm_write(t, wire, split, &used) == 0 && used == split);
		CHECK(st_cterm_write(t, wire + split, sizeof(wire) - 1 - split, &used) == 0 && used == sizeof(wire) - 1 - split);
		CHECK(st_cterm_finish(t) == 0);
		rectangle(t, 24, 6, 8, 519);
	}
	if (utf8) {
		/* Independent Pearl/8 e-acute rows; revise e after the first cursor
		 * operation without leaving a double-toggle or a stale underline. */
		const uint8_t accent[] = {0x0c, 0x10, 0x3c, 0x66, 0x7e, 0x60, 0x3c, 0};
		feed(t, "\f");
		const char composed[] = "e\xcc\x81";
		for (size_t i = 0; i < sizeof(composed) - 1; ++i)
			CHECK(st_cterm_write(t, composed + i, 1, &used) == 0 && used == 1);
		CHECK(st_cterm_finish(t) == 0);
		for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 8; ++x)
			rectangle(t, x, y, 1, accent[y] & (128u >> x) ? 514 : 512);
		rectangle(t, 8, 6, 8, 519);
	}
	st_cterm_destroy(&t);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
}

static void widths(enum st_cterm_graphics_mode mode, const char *name,
	unsigned height, unsigned baseline, unsigned width)
{
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *t = NULL;
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config grid = {.columns = 80, .rows = 25,
		.cell_width = 8, .cell_height = 8};
	CHECK(st_console_create(&console, &display, &grid) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config config = {.console = console, .fonts = fonts,
		.emulation = ST_CTERM_ANSI_BBS, .graphics_mode = mode,
		.encoding = ST_CTERM_ENCODING_CODEPAGE, .codepage = ST_CODEPAGE_CP437,
		.amiga_font = st_amiga_font_builtin(name, height), .quiet = true};
	CHECK(st_cterm_create(&t, &config) == 0);
	feed(t, "\f");
	rectangle(t, 0, baseline, width, 519);
	rectangle(t, width, baseline, 1, 512);
	feed(t, "\x1b[19;2;11!\x1b[10;0!\x1b[K");
	rectangle(t, 2, 11 + height - 1, width, 519);
	feed(t, "\x1b[10;0!"); /* Hiding restores the exact background. */
	rectangle(t, 2, 11 + height - 1, width, 512);
	feed(t, "\x1b[19;65533;11!\x1b[10;0!\x1b[K");
	rectangle(t, 0, 11 + height - 1, width - 3, 519);
	rectangle(t, width - 3, 11 + height - 1, 1, 512);
	feed(t, "\x1b[19;638;11!\x1b[10;0!\x1b[K");
	rectangle(t, 638, 11 + height - 1, 2, 519);
	/* Restore both actual phase backgrounds, rather than guessing a visible
	 * phase from C or flattening a differently coloured alternate plane. */
	feed(t, "\x1b[19;2;11!\x1b[10;0!");
	struct st_pixel_buffer *background = NULL;
	CHECK(st_pixel_buffer_create(&background, width, 1, true) == 0);
	st_pixel_buffer_fill(background, 515, 516);
	const struct st_pixel_blit blit = {.width = width, .height = 1,
		.destination_x = 2, .destination_y = (int)(11 + height - 1), .scale_x = 1, .scale_y = 1};
	CHECK(st_console_blit_pixels(console, background, NULL, &blit) == 0);
	st_pixel_buffer_destroy(background);
	feed(t, "\x1b[?25h");
	const struct st_console_rect strip = {2, 11 + height - 1, width, 1};
	CHECK(st_console_get_pixels(&background, console, &strip, false) == 0);
	for (unsigned i = 0; i < width; ++i)
		CHECK(background->primary[i] == 516 && background->alternate[i] == 515);
	st_pixel_buffer_destroy(background);
	feed(t, "\x1b[10;0!");
	CHECK(st_console_get_pixels(&background, console, &strip, false) == 0);
	for (unsigned i = 0; i < width; ++i)
		CHECK(background->primary[i] == 515 && background->alternate[i] == 516);
	st_pixel_buffer_destroy(background);
	st_cterm_destroy(&t);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
}

int main(void)
{
	for (int kick = 0; kick < 2; ++kick) {
		for (int utf8 = 0; utf8 < 2; ++utf8)
			for (int status = 0; status < 2; ++status)
				run(kick ? ST_CTERM_GRAPHICS_SKYPIX_31 : ST_CTERM_GRAPHICS_SKYPIX_13, utf8, status);
		widths(kick ? ST_CTERM_GRAPHICS_SKYPIX_31 : ST_CTERM_GRAPHICS_SKYPIX_13, "Opal.font", 12, 8, 9);
		widths(kick ? ST_CTERM_GRAPHICS_SKYPIX_31 : ST_CTERM_GRAPHICS_SKYPIX_13, "Diamond.font", 20, 14, 20);
	}
	return 0;
}
