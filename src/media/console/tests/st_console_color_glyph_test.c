/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "console_internal.h"
#include "st_console_test.h"
#include "io_error.h"
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
check_geometry(uint32_t cell_width, bool double_height, bool narrow_color)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_TEST};
	const struct st_console_config config = {
		.columns = 2, .rows = 2, .cell_width = cell_width, .cell_height = 4,
		.color_emoji = true, .video_flags = ST_CONSOLE_VIDEO_EXPAND,
	};
	uint8_t mono[2] = {0}, wide[4] = {0};
	uint8_t rgba[128] = {0};
	uint32_t color_width = narrow_color ? 8 : 16;
	for (uint32_t y = 0; y < 2; ++y)
		for (uint32_t x = 0; x < color_width; ++x) {
			size_t i = ((size_t)y * color_width + x) * 4;
			rgba[i] = y == 0 ? 255 : 0;
			rgba[i + 1] = x >= color_width / 2 ? 255 : 0;
			rgba[i + 3] = 255;
		}
	const struct st_console_color_glyph color = {
		1, rgba, color_width * 2 * 4, color_width * 4, color_width,
	};
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

int main(void)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_TEST};
	const struct st_console_config config = {
		.columns = 2, .rows = 1, .cell_width = 8, .cell_height = 2,
		.color_emoji = true,
	};
	uint8_t mono[2] = {0x80, 0x80};
	uint8_t rgba[64] = {255, 0, 0, 255, 255, 0, 0, 128, 0, 255, 0, 0};
	struct st_console_color_glyph color = {0, rgba, sizeof(rgba), 32, 0};
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
	check_geometry(8, false, false);
	check_geometry(8, false, true);
	check_geometry(9, false, false);
	check_geometry(16, false, false);
	check_geometry(8, true, false);
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
	return EXIT_SUCCESS;
}
