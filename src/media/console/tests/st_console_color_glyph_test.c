/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "console_internal.h"
#include "st_console_test.h"
#include "io_error.h"
#include "color_rll_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", \
    __FILE__, __LINE__, #e); exit(EXIT_FAILURE); } } while (false)

static struct st_pixel_buffer *
pixels(st_console_t *console)
{
	struct st_console_test_capture *capture = NULL;
	CHECK(st_console_test_capture(console->display, &capture) == 0);
	CHECK(capture->columns == console->columns && capture->rows == console->rows);
	struct st_pixel_buffer *result = capture->planes;
	capture->planes = NULL;
	st_console_test_capture_destroy(capture);
	return result;
}

static void
check_geometry(uint32_t cell_width, bool double_height, uint32_t color_width, bool rll)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_TEST};
	const struct st_console_config config = {
		.columns = 2, .rows = 2, .cell_width = cell_width, .cell_height = 4,
		.color_emoji = true, .video_flags = ST_CONSOLE_VIDEO_EXPAND,
	};
	uint8_t mono[2] = {0}, wide[4] = {0};
	uint8_t rgba[128] = {0};
	for (uint32_t y = 0; y < 2; ++y)
		for (uint32_t x = 0; x < color_width; ++x) {
			size_t i = ((size_t)y * color_width + x) * 4;
			rgba[i] = y == 0 ? 255 : 0;
			rgba[i + 1] = x >= color_width / 2 ? 255 : 0;
			rgba[i + 3] = 255;
		}
	struct st_console_color_glyph color = {
		1, rgba, color_width * 2 * 4, color_width * 4, color_width,
	};
	uint8_t encoded[sizeof(rgba) + 2];
	if (rll) {
		color.size = encode_color_rll(rgba, color.row_stride, color_width, 2, encoded);
		color.rgba = encoded;
		color.row_stride = 0;
	}
	const struct st_console_font font = {
		.bitmap = mono, .bitmap_size = sizeof(mono),
		.width = 8, .height = 2, .glyph_count = 1, .row_stride = 1, .glyph_stride = 2,
		.wide_bitmap = wide, .wide_bitmap_size = sizeof(wide),
		.wide_glyph_count = 1, .wide_row_stride = 2, .wide_glyph_stride = 4,
		.color_glyphs = &color, .color_glyph_count = 1,
	};
	st_console_t *console = NULL;
	uint16_t id;
	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(st_console_register_static_font(console, &font, &id) == 0);
	struct st_console_cell cells[4] = {
		{.glyph = 1, .font = id},
		{.glyph = 1, .font = id, .flags = ST_CONSOLE_CELL_CONTINUATION},
		{.glyph = 1, .font = id},
		{.glyph = 1, .font = id, .flags = ST_CONSOLE_CELL_CONTINUATION},
	};
	if (double_height)
		for (size_t i = 0; i < 4; ++i)
			cells[i].flags |= ST_CONSOLE_CELL_SAA5050 | ST_CONSOLE_CELL_DOUBLE_HEIGHT;
	const struct st_console_rect grid = {0, 0, 2, 2};
	struct st_pixel_buffer *p = NULL;
	CHECK(st_console_write_cells(console, &grid, cells, 2) == 0);
	p = pixels(console);
	CHECK(p->primary[0] == ST_CONSOLE_COLOR_RGB_VALUE(255, 0, 0));
	CHECK(p->primary[cell_width] == ST_CONSOLE_COLOR_RGB_VALUE(255, 255, 0));
	uint32_t bottom = p->primary[4 * cell_width * 2];
	CHECK(bottom == (double_height ? ST_CONSOLE_COLOR_RGB_VALUE(0, 0, 0)
	    : ST_CONSOLE_COLOR_RGB_VALUE(255, 0, 0)));
	if (cell_width == 9)
		CHECK(p->primary[8] == 0 && p->primary[17] == 0);
	st_pixel_buffer_destroy(p);
	st_console_destroy(console);
}

static void
check_pixels(bool rll)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_TEST};
	const struct st_console_config config = {
		.columns = 2, .rows = 1, .cell_width = 8, .cell_height = 2,
		.color_emoji = true,
	};
	uint8_t mono[2] = {0x80, 0x80};
	uint8_t rgba[64] = {255, 0, 0, 255, 255, 0, 0, 128, 0, 255, 0, 0};
	struct st_console_color_glyph color = {0, rgba, sizeof(rgba), 32, 0};
	/* Two literal pixels followed by transparent row spans in each half. */
	uint8_t encoded[] = {12, 0, 255, 255, 0, 0, 128, 255, 0, 0, 0, 5, 0, 7};
	if (rll) {
		color.rgba = encoded;
		color.size = sizeof(encoded);
		color.row_stride = 0;
	}
	struct st_console_font font = {
		.bitmap = mono, .bitmap_size = sizeof(mono),
		.width = 8, .height = 2, .glyph_count = 1,
		.row_stride = 1, .glyph_stride = 2,
		.color_glyphs = &color, .color_glyph_count = 1,
	};
	struct st_console_cell cell = {
		.foreground = ST_CONSOLE_COLOR_RGB_VALUE(0, 255, 0), .background = 42,
	};
	const struct st_console_rect rect = {0, 0, 1, 1};
	st_console_t *console = NULL;
	uint16_t id;
	bool enabled = false;
	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(st_console_get_color_emoji(console, &enabled) == 0 && enabled);
	CHECK(st_console_register_font_copy(console, &font, &id) == 0);
	cell.font = id;
	memset(rgba, 0, sizeof(rgba)); /* Registered pixels must be owned. */
	memset(encoded, 0, sizeof(encoded));
	uint32_t blue = 0x0000ff;
	CHECK(st_console_set_palette(console, 42, &blue, 1) == 0);
	CHECK(st_console_write_cells(console, &rect, &cell, 1) == 0);
	struct st_pixel_buffer *p = pixels(console);
	CHECK(p->primary[0] == ST_CONSOLE_COLOR_RGB_VALUE(255, 0, 0));
	CHECK(p->primary[1] == ST_CONSOLE_COLOR_RGB_VALUE(128, 0, 127));
	CHECK(p->primary[2] == 42 && p->alternate[1] == p->primary[1]);
	st_pixel_buffer_destroy(p);
	blue = 0xffffff;
	CHECK(st_console_set_palette(console, 42, &blue, 1) == 0);
	p = pixels(console);
	CHECK(p->primary[1] == ST_CONSOLE_COLOR_RGB_VALUE(255, 127, 127));
	st_pixel_buffer_destroy(p);
	cell.flags = ST_CONSOLE_CELL_REVERSE | ST_CONSOLE_CELL_BOLD | ST_CONSOLE_CELL_BLINK;
	CHECK(st_console_write_cells(console, &rect, &cell, 1) == 0);
	p = pixels(console);
	CHECK(p->primary[0] == ST_CONSOLE_COLOR_RGB_VALUE(255, 0, 0));
	CHECK(p->primary[1] == ST_CONSOLE_COLOR_RGB_VALUE(128, 127, 0));
	CHECK(p->primary[2] == cell.foreground && p->alternate[0] == cell.foreground);
	st_pixel_buffer_destroy(p);
	cell.flags = ST_CONSOLE_CELL_CONCEAL;
	CHECK(st_console_write_cells(console, &rect, &cell, 1) == 0);
	p = pixels(console);
	CHECK(p->primary[0] == 42);
	st_pixel_buffer_destroy(p);
	cell.flags = ST_CONSOLE_CELL_UNDERLINE;
	CHECK(st_console_write_cells(console, &rect, &cell, 1) == 0);
	p = pixels(console);
	CHECK(p->primary[16] == cell.foreground && p->primary[23] == cell.foreground);
	st_pixel_buffer_destroy(p);
	cell.flags = 0;
	cell.hyperlink = 1;
	CHECK(st_console_write_cells(console, &rect, &cell, 1) == 0);
	p = pixels(console);
	CHECK(p->primary[16] == cell.foreground && p->primary[20] == cell.foreground
	    && p->primary[17] == cell.background);
	st_pixel_buffer_destroy(p);
	cell.hyperlink = 0;
	CHECK(st_console_write_cells(console, &rect, &cell, 1) == 0);
	st_console_snapshot_t *snapshot = NULL;
	CHECK(st_console_set_pixel(console, 8, 0, cell.foreground) == 0);
	CHECK(st_console_snapshot_create(&snapshot, console) == 0);
	CHECK(st_console_set_color_emoji(console, false) == 0);
	CHECK(st_console_snapshot_restore(console, snapshot) == 0);
	CHECK(st_console_get_color_emoji(console, &enabled) == 0 && !enabled);
	p = pixels(console);
	CHECK(p->primary[0] == cell.foreground && p->primary[1] == 42);
	CHECK(p->primary[8] == cell.foreground); /* Graphics survive preference changes. */
	st_pixel_buffer_destroy(p);
	st_console_snapshot_destroy(snapshot);
	CHECK(st_console_set_color_emoji(console, true) == 0);
	p = pixels(console);
	CHECK(p->primary[0] == ST_CONSOLE_COLOR_RGB_VALUE(255, 0, 0));
	st_pixel_buffer_destroy(p);
	color.row_stride = 31;
	CHECK(st_console_register_static_font(console, &font, &id) == IO_ERROR_INVALID_ARGUMENT);
	color.row_stride = 32;
	color.size = 63;
	CHECK(st_console_register_static_font(console, &font, &id) == IO_ERROR_INVALID_ARGUMENT);
	color.size = sizeof(rgba);
	color.glyph = 1;
	CHECK(st_console_register_static_font(console, &font, &id) == IO_ERROR_INVALID_ARGUMENT);
	color.glyph = 0;
	color.rgba = NULL;
	CHECK(st_console_register_static_font(console, &font, &id) == IO_ERROR_INVALID_ARGUMENT);
	color.rgba = rgba;
	color.row_stride = SIZE_MAX;
	CHECK(st_console_register_static_font(console, &font, &id) == IO_ERROR_INVALID_ARGUMENT);
	color.row_stride = 32;
	color.width = 9;
	CHECK(st_console_register_static_font(console, &font, &id) == IO_ERROR_INVALID_ARGUMENT);
	color.width = 0;
	const struct st_console_color_glyph duplicates[] = {color, color};
	font.color_glyphs = duplicates;
	font.color_glyph_count = 2;
	CHECK(st_console_register_static_font(console, &font, &id) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_console_set_color_emoji(NULL, true) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_console_get_color_emoji(console, NULL) == IO_ERROR_INVALID_ARGUMENT);
	/* Exercise replacement/freeing of copied RGBA without filling 65536 slots. */
	console->next_font_identifier = UINT32_C(65536);
	font.color_glyphs = NULL;
	font.color_glyph_count = 0;
	CHECK(st_console_register_font_copy(console, &font, &id) == 0 && id == 0);
	/* Replacing a font behind existing cells requires an explicit full raster
	 * refresh before capturing the new frame. */
	CHECK(st_console_begin_update(console) == 0);
	CHECK(st_console_rasterize_locked(console, true) == 0);
	CHECK(st_console_end_update(console) == 0);
	p = pixels(console);
	CHECK(p->primary[0] == cell.foreground && p->primary[1] == cell.background);
	st_pixel_buffer_destroy(p);
	st_console_destroy(console);
	check_geometry(8, false, 16, rll);
	check_geometry(8, false, 8, rll);
	check_geometry(9, false, 16, rll);
	check_geometry(16, false, 16, rll);
	check_geometry(8, true, 16, rll);
	check_geometry(16, true, 7, rll);
	check_geometry(9, false, 7, rll);
	/* Text-only consoles have no raster or drawn-cell allocation. */
	const struct st_console_config text_config = {.columns = 2, .rows = 1};
	const struct st_console_display_config text_display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	CHECK(st_console_create(&console, &text_display, &text_config) == 0);
	CHECK(st_console_set_color_emoji(console, true) == 0);
	CHECK(st_console_set_palette(console, 42, &blue, 1) == 0);
	CHECK(st_console_get_color_emoji(console, &enabled) == 0 && enabled);
	st_console_destroy(console);
}

static void
check_rll_validation(void)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config config = {.columns = 1, .rows = 1};
	uint8_t mono[32] = {0};
	const uint8_t empty[] = {4, 0, 0, 127, 0, 127};
	struct st_console_color_glyph color = {0, empty, sizeof(empty), 0, 0};
	struct st_console_font font = {
		.bitmap = mono, .bitmap_size = sizeof(mono),
		.width = 8, .height = 32, .glyph_count = 1, .row_stride = 1, .glyph_stride = 32,
		.color_glyphs = &color, .color_glyph_count = 1,
	};
	st_console_t *console = NULL;
	uint16_t id;
	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(st_console_register_static_font(console, &font, &id) == 0);
	const struct {
		uint8_t data[10];
		size_t size;
	} invalid[] = {
		{{0}, 0},                         /* Missing header. */
		{{4}, 1},                         /* Truncated offset. */
		{{1, 0, 0, 127, 0, 127}, 6},      /* Offset inside header. */
		{{7, 0, 0, 127, 0, 127}, 6},      /* Offset past buffer. */
		{{3, 0, 0, 0, 127}, 5},           /* Truncated left skip. */
		{{5, 0, 255, 1, 2, 0, 127}, 7},   /* Truncated left literal. */
		{{4, 0, 0, 126, 0, 127}, 6},      /* Underfilled left half. */
		{{4, 0, 0, 128, 0, 126}, 6},      /* Run crosses halfway. */
		{{4, 0, 0, 127, 0}, 5},           /* Truncated right skip. */
		{{4, 0, 0, 127, 255, 1, 2}, 7},   /* Truncated right literal. */
		{{4, 0, 0, 127, 0, 126}, 6},      /* Underfilled right half. */
		{{4, 0, 0, 127, 0, 128}, 6},      /* Oversized right run. */
		{{4, 0, 0, 127, 0, 127, 0, 0}, 8}, /* Trailing packet. */
	};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
		color.rgba = invalid[i].data;
		color.size = invalid[i].size;
		CHECK(st_console_register_static_font(console, &font, &id) == IO_ERROR_INVALID_ARGUMENT);
	}
	/* Check byte order and half-row traversal against independent bytes. */
	uint8_t data[33 * 8 * 4] = {0}, encoded[sizeof(data) + 2];
	data[3] = 1;
	data[11] = 255;
	font.height = 33;
	font.glyph_stride = 33;
	uint8_t taller_mono[33] = {0};
	font.bitmap = taller_mono;
	font.bitmap_size = sizeof(taller_mono);
	color.rgba = encoded;
	color.size = encode_color_rll(data, 32, 8, 33, encoded);
	const uint8_t expected[] = {14, 0, 1, 0, 0, 0, 0, 0, 255, 0, 0, 0, 0, 128, 0, 131};
	CHECK(color.size == sizeof(expected) && memcmp(encoded, expected, sizeof(expected)) == 0);
	CHECK(st_console_register_font_copy(console, &font, &id) == 0);
	/* A 264-pixel half requires two runs, neither crossing halfway. */
	uint8_t long_data[8 * 66 * 4] = {0}, long_encoded[sizeof(long_data) + 2];
	const uint8_t long_expected[] = {6, 0, 0, 255, 0, 7, 0, 255, 0, 7};
	size_t size = encode_color_rll(long_data, 32, 8, 66, long_encoded);
	CHECK(size == sizeof(long_expected)
	    && memcmp(long_encoded, long_expected, size) == 0);
	uint8_t long_mono[66] = {0};
	font.height = 66;
	font.glyph_stride = 66;
	font.bitmap = long_mono;
	font.bitmap_size = sizeof(long_mono);
	color.rgba = long_encoded;
	color.size = size;
	CHECK(st_console_register_static_font(console, &font, &id) == 0);
	st_console_destroy(console);
}

int main(void)
{
	check_rll_validation();
	check_pixels(false);
	check_pixels(true);
	return EXIT_SUCCESS;
}
