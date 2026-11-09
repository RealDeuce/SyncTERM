/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_internal.h"
#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
		    __FILE__, __LINE__, #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (false)

struct upload_log {
	struct st_console_rect rectangle;
	const void *image_identity;
	uint64_t generation;
	unsigned int calls;
	unsigned int failures;
};

static int
record_upload(void *context, const struct st_rgb_buffer *pixels,
	const struct st_console_rect *rectangle, const void *image_identity,
	uint64_t generation)
{
	struct upload_log *log = context;

	CHECK(pixels != NULL);
	CHECK(rectangle != NULL);
	CHECK(image_identity != NULL);
	CHECK(generation != 0);
	log->rectangle = *rectangle;
	log->image_identity = image_identity;
	log->generation = generation;
	++log->calls;
	if (log->failures != 0) {
		--log->failures;
		return IO_ERROR_IO;
	}
	return 0;
}

static void
test_pixel_cursor_overlay(void)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config config = {.columns = 4, .rows = 2,
		.cell_width = 8, .cell_height = 8, .scaling_mode = ST_CONSOLE_SCALING_EXTERNAL};
	st_console_t *console = NULL;
	struct st_console_bitmap_presenter *presenter = NULL;
	struct st_console_bitmap_frame frame;
	struct st_pixel_buffer *base = NULL, *copy = NULL;
	CHECK(st_console_create(&console, &display, &config) == 0);
	static const uint8_t blank_font[256 * 8] = {0};
	const struct st_console_font font = {.bitmap = blank_font,
		.bitmap_size = sizeof(blank_font), .glyph_count = 256,
		.width = 8, .height = 8, .row_stride = 1, .glyph_stride = 8};
	uint16_t identifier;
	CHECK(st_console_register_static_font(console, &font, &identifier) == 0);
	const struct st_console_style style = {.font = identifier,
		.foreground = ST_CONSOLE_COLOR_RGB_VALUE(255, 255, 255),
		.background = ST_CONSOLE_COLOR_RGB_TAG};
	CHECK(st_console_set_style(console, &style) == 0);
	CHECK(st_pixel_buffer_create(&base, 32, 16, true) == 0);
	CHECK(st_pixel_buffer_fill(base, ST_PIXEL_COLOR_RGB_VALUE(1, 2, 3),
		ST_PIXEL_COLOR_RGB_VALUE(4, 5, 6)) == 0);
	const struct st_pixel_blit blit = {.width = 32, .height = 16, .scale_x = 1, .scale_y = 1};
	CHECK(st_console_clear_window(console, ' ') == 0);
	CHECK(st_console_present(console) == 0);
	CHECK(st_console_begin_update(console) == 0);
	/* Clearing identical blank cells leaves a pending glyph invalidation.
	 * New explicit pixels must supersede it, even when another text row
	 * later causes the raster cache to be revisited. */
	CHECK(st_console_clear_window(console, ' ') == 0);
	CHECK(st_console_blit_pixels(console, base, NULL, &blit) == 0);
	struct st_console_cell status = *st_console_cell_at(console, 4);
	status.flags &= ~ST_CONSOLE_CELL_PIXEL_GRAPHICS;
	status.background = ST_CONSOLE_COLOR_RGB_VALUE(8, 9, 10);
	const struct st_console_rect row = {0, 1, 1, 1};
	CHECK(st_console_write_cells(console, &row, &status, 1) == 0);
	CHECK(st_console_end_update(console) == 0);
	const struct st_console_rect before_status = {0, 0, 32, 8};
	CHECK(st_console_get_pixels(&copy, console, &before_status, false) == 0);
	CHECK(copy->primary[0] == ST_PIXEL_COLOR_RGB_VALUE(1, 2, 3));
	CHECK(copy->alternate[0] == ST_PIXEL_COLOR_RGB_VALUE(4, 5, 6));
	st_pixel_buffer_destroy(copy);
	copy = NULL;
	CHECK(st_console_blit_pixels(console, base, NULL, &blit) == 0);
	struct st_console_pixel_cursor cursor = {.rectangle = {2, 7, 16, 1},
		.colour = ST_CONSOLE_COLOR_RGB_VALUE(250, 20, 30), .enabled = true, .visible = true};
	CHECK(st_console_set_pixel_cursor(NULL, &cursor) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_console_set_pixel_cursor(console, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_console_set_pixel_cursor(console, &cursor) == 0);
	CHECK(st_console_bitmap_presenter_create(&presenter, console) == 0);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 32, 16, 0, NULL, NULL, &frame) == 0);
	for (uint32_t y = 0; y < 16; ++y)
		for (uint32_t x = 0; x < 32; ++x)
			CHECK(frame.pixels->pixels[y * 32 + x] ==
				(y == 7 && x >= 2 && x < 18 ? UINT32_C(0xfa141e) : UINT32_C(0x010203)));
	const struct st_console_rect whole = {0, 0, 32, 16};
	CHECK(st_console_get_pixels(&copy, console, &whole, false) == 0);
	CHECK(copy->primary[7 * 32 + 2] == ST_PIXEL_COLOR_RGB_VALUE(1, 2, 3));
	CHECK(copy->alternate[7 * 32 + 2] == ST_PIXEL_COLOR_RGB_VALUE(4, 5, 6));
	st_pixel_buffer_destroy(copy);
	st_console_snapshot_t *snapshot = NULL;
	CHECK(st_console_snapshot_create(&snapshot, console) == 0);
	CHECK(st_console_set_cursor(console, false, ST_CONSOLE_CURSOR_BLOCK) == 0);
	CHECK(st_console_get_pixel_cursor(console, &cursor) == 0 && !cursor.enabled);
	CHECK(st_console_snapshot_restore(console, snapshot, NULL) == 0);
	CHECK(st_console_get_pixel_cursor(console, &cursor) == 0 && cursor.enabled);
	cursor.rectangle.x = 30;
	cursor.rectangle.y = 15;
	CHECK(st_console_set_pixel_cursor(console, &cursor) == 0);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 32, 16, 1, NULL, NULL, &frame) == 0);
	CHECK(frame.pixels->pixels[15 * 32 + 30] == UINT32_C(0xfa141e));
	CHECK(frame.pixels->pixels[7 * 32 + 2] == UINT32_C(0x010203));
	cursor.visible = false;
	CHECK(st_console_set_pixel_cursor(console, &cursor) == 0);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 32, 16, 2, NULL, NULL, &frame) == 0);
	CHECK(frame.pixels->pixels[15 * 32 + 30] == UINT32_C(0x010203));
	st_console_snapshot_destroy(snapshot);
	st_console_bitmap_presenter_destroy(presenter);
	st_pixel_buffer_destroy(base);
	st_console_destroy(console);
}

static void
test_upload_retry(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 1, .rows = 1, .cell_width = 1, .cell_height = 1,
		.scaling_mode = ST_CONSOLE_SCALING_EXTERNAL,
	};
	struct st_console_bitmap_presenter *presenter = NULL;
	struct st_console_bitmap_frame frame;
	struct upload_log log = {.failures = 1};
	st_console_t *console = NULL;
	uint64_t generation;
	const void *identity;

	CHECK(st_console_create(&console, &display, &config) == 0);
	console->cursor_visible = false;
	CHECK(st_console_bitmap_presenter_create(&presenter, console) == 0);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 1, 1, 0,
	    record_upload, &log, &frame) == IO_ERROR_IO);
	generation = frame.generation;
	identity = frame.image_identity;
	CHECK(log.calls == 1);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 1, 1, 1,
	    record_upload, &log, &frame) == 0);
	CHECK(log.calls == 2);
	CHECK(log.rectangle.x == 0 && log.rectangle.y == 0
	    && log.rectangle.width == 1 && log.rectangle.height == 1);
	CHECK(frame.generation == generation);
	CHECK(frame.image_identity == identity);
	console->pixel_surface_dirty = true;
	log.calls = 0;
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 1, 1, 2,
	    record_upload, &log, &frame) == 0);
	CHECK(log.calls == 1 && frame.changed);
	CHECK(frame.generation == generation);
	st_console_bitmap_presenter_destroy(presenter);
	st_console_destroy(console);
}

static void
test_vga_ninth_column(void)
{
	static const uint8_t bitmap[256] = {
		[UINT8_C(0xb0)] = UINT8_C(0x01),
		[UINT8_C(0xc0)] = UINT8_C(0x01),
	};
	const struct st_console_font font = {
		.bitmap = bitmap,
		.bitmap_size = sizeof(bitmap),
		.width = 8,
		.height = 1,
		.glyph_count = 256,
		.row_stride = 1,
		.glyph_stride = 1,
	};
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 2,
		.rows = 1,
		.cell_width = 9,
		.cell_height = 1,
		.video_flags = ST_CONSOLE_VIDEO_EXPAND
		    | ST_CONSOLE_VIDEO_LINE_GRAPHICS_EXPAND,
	};
	const struct st_console_rect rectangle = {0, 0, 2, 1};
	const struct st_console_cell cells[2] = {
		{.glyph = UINT32_C(0xc0),
		    .foreground = 1},
		{.glyph = UINT32_C(0xb0),
		    .foreground = 1, .flags = ST_CONSOLE_CELL_BOLD},
	};
	st_console_t *console = NULL;
	uint16_t identifier;

	CHECK(st_console_create(&console, &display, &config) == 0);
	console->cursor_visible = false;
	CHECK(st_console_register_static_font(console, &font, &identifier) == 0);
	CHECK(identifier == 0);
	CHECK(st_console_write_complete_cells(console, &rectangle, cells, 2) == 0);
	CHECK(st_console_present(console) == 0);
	CHECK(console->pixels->primary[7] == 1);
	CHECK(console->pixels->primary[8] == 1);
	CHECK(console->pixels->primary[16] == 1);
	CHECK(console->pixels->primary[17] == 0);
	CHECK(st_console_set_video_flags(console,
	    ST_CONSOLE_VIDEO_EXPAND) == 0);
	CHECK(st_console_present(console) == 0);
	CHECK(console->pixels->primary[7] == 1);
	CHECK(console->pixels->primary[8] == 0);
	st_console_destroy(console);
}

static void
fill_planes(st_console_t *console, uint32_t primary, uint32_t alternate)
{
	size_t count = (size_t)console->pixels->width * console->pixels->height;

	for (size_t i = 0; i < count; ++i) {
		console->pixels->primary[i] = ST_PIXEL_COLOR_RGB_TAG | primary;
		console->pixels->alternate[i] = ST_PIXEL_COLOR_RGB_TAG | alternate;
	}
}

static void
test_circular_text_raster(void)
{
	struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	struct st_console_config config = {
		.columns = 1,
		.rows = 2,
		.cell_width = 1,
		.cell_height = 1,
		.pixel_width = 1,
		.pixel_height = 3,
		.scaling_mode = ST_CONSOLE_SCALING_EXTERNAL,
	};
	struct st_console_bitmap_presenter *presenter = NULL;
	struct st_console_bitmap_frame frame;
	struct upload_log log = {0};
	st_console_t *console = NULL;
	const uint32_t red = UINT32_C(0xff0000);
	const uint32_t blue = UINT32_C(0x0000ff);
	const uint32_t green = UINT32_C(0x00ff00);
	const uint32_t yellow = UINT32_C(0xffff00);

	CHECK(st_console_create(&console, &config_display, &config) == 0);
	CHECK(st_console_set_pixel(console, 0, 0,
	    ST_PIXEL_COLOR_RGB_TAG | red) == 0);
	CHECK(st_console_set_pixel(console, 0, 1,
	    ST_PIXEL_COLOR_RGB_TAG | blue) == 0);
	CHECK(st_console_set_pixel(console, 0, 2,
	    ST_PIXEL_COLOR_RGB_TAG | green) == 0);
	console->cursor_visible = false;
	CHECK(st_console_bitmap_presenter_create(&presenter, console) == 0);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 1, 3,
	    1000, record_upload, &log, &frame) == 0);

	CHECK(st_console_begin_update(console) == 0);
	CHECK(st_console_scroll(console, 1, ' ') == 0);
	CHECK(st_console_rasterize_locked(console, false) == 0);
	log.calls = 0;
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 1, 3,
	    1000, record_upload, &log, &frame) == 0);
	CHECK(log.calls == 1);
	CHECK(frame.pixels->pixels[0] == blue);
	CHECK(frame.pixels->pixels[1] == 0);
	CHECK(frame.pixels->pixels[2] == green);
	CHECK(st_console_end_update(console) == 0);

	CHECK(st_console_begin_update(console) == 0);
	CHECK(st_console_set_pixel(console, 0, 2,
	    ST_PIXEL_COLOR_RGB_TAG | yellow) == 0);
	log.calls = 0;
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 1, 3,
	    1000, record_upload, &log, &frame) == 0);
	CHECK(log.calls == 1);
	CHECK(log.rectangle.width == 1 && log.rectangle.height == 3);
	CHECK(frame.pixels->pixels[2] == yellow);
	CHECK(st_console_end_update(console) == 0);

	st_console_bitmap_presenter_destroy(presenter);
	st_console_destroy(console);
}

static void
test_full_surface_scroll_gap(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 1, .rows = 2,
		.cell_width = 1, .cell_height = 1,
		.pixel_width = 1, .pixel_height = 3,
		.scaling_mode = ST_CONSOLE_SCALING_EXTERNAL,
	};
	const struct st_console_rect rect = {0, 0, 1, 3};
	struct st_pixel_buffer *pixels = NULL;
	st_console_t *console = NULL;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(st_console_set_pixel(console, 0, 0,
	    ST_PIXEL_COLOR_RGB_TAG | UINT32_C(0xff0000)) == 0);
	CHECK(st_console_set_pixel(console, 0, 1,
	    ST_PIXEL_COLOR_RGB_TAG | UINT32_C(0x0000ff)) == 0);
	CHECK(st_console_set_pixel(console, 0, 2,
	    ST_PIXEL_COLOR_RGB_TAG | UINT32_C(0x00ff00)) == 0);
	CHECK(st_console_set_full_surface_scroll(console, true) == 0);
	CHECK(st_console_scroll(console, 1, ' ') == 0);
	CHECK(st_console_get_pixels(&pixels, console, &rect, false) == 0);
	CHECK(pixels->primary[0] == (ST_PIXEL_COLOR_RGB_TAG | UINT32_C(0x0000ff)));
	CHECK(pixels->primary[1] == 0);
	CHECK(pixels->primary[2] == (ST_PIXEL_COLOR_RGB_TAG | UINT32_C(0xff0000)));
	st_pixel_buffer_destroy(pixels);
	CHECK(st_console_scroll(console, 1, ' ') == 0);
	CHECK(st_console_get_pixels(&pixels, console, &rect, false) == 0);
	CHECK(pixels->primary[0] == 0);
	CHECK(pixels->primary[1] == 0);
	CHECK(pixels->primary[2] == (ST_PIXEL_COLOR_RGB_TAG | UINT32_C(0x0000ff)));
	st_pixel_buffer_destroy(pixels);
	{
		const struct st_console_rect source = {0, 1, 1, 1};
		const struct st_console_cell blank = (*st_console_cell_at(console, 1));

		CHECK(st_console_set_pixel(console, 0, 0,
		    ST_PIXEL_COLOR_RGB_TAG | UINT32_C(0xff0000)) == 0);
		CHECK(st_console_move_cells_clear(console, &source, 0, 0,
		    &blank) == 0);
		CHECK(st_console_move_cells_clear(console, &source, 0, 0,
		    &blank) == 0);
		CHECK(st_console_get_pixels(&pixels, console, &rect, false) == 0);
		CHECK(pixels->primary[1] == 0);
		st_pixel_buffer_destroy(pixels);
	}
	CHECK(st_console_set_full_surface_scroll(console, false) == 0);
	CHECK(st_console_get_pixels(&pixels, console, &rect, false) == 0);
	CHECK(pixels->primary[1] == 0);
	st_pixel_buffer_destroy(pixels);
	st_console_destroy(console);
}

static void
test_full_surface_scroll_right_strip(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 1, .rows = 2,
		.cell_width = 1, .cell_height = 1,
		.pixel_width = 2, .pixel_height = 2,
		.scaling_mode = ST_CONSOLE_SCALING_EXTERNAL,
	};
	const struct st_console_rect source = {0, 1, 1, 1};
	const struct st_console_rect pixels_rect = {0, 0, 2, 2};
	struct st_pixel_buffer *pixels = NULL;
	st_console_t *console = NULL;
	struct st_console_cell blank;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(st_console_set_full_surface_scroll(console, true) == 0);
	CHECK(st_console_set_pixel(console, 1, 0,
	    ST_PIXEL_COLOR_RGB_TAG | UINT32_C(0xff0000)) == 0);
	blank = (*st_console_cell_at(console, 1));
	CHECK(st_console_move_cells_clear(console, &source, 0, 0,
	    &blank) == 0);
	CHECK(st_console_get_pixels(&pixels, console, &pixels_rect, false) == 0);
	CHECK(pixels->primary[3] == 0);
	st_pixel_buffer_destroy(pixels);
	st_console_destroy(console);
}

static void
test_indexed_pixel_palette_change(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 1, .rows = 1,
		.cell_width = 1, .cell_height = 1,
		.scaling_mode = ST_CONSOLE_SCALING_EXTERNAL,
	};
	const uint32_t red = UINT32_C(0x123456);
	const uint32_t blue = UINT32_C(0x654321);
	struct st_console_bitmap_presenter *presenter = NULL;
	struct st_console_bitmap_frame frame;
	struct upload_log log = {0};
	st_console_t *console = NULL;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(st_console_begin_update(console) == 0);
	CHECK(st_console_set_cursor(console, false, ST_CONSOLE_CURSOR_BLOCK) == 0);
	CHECK(st_console_set_palette(console, 500, &red, 1) == 0);
	CHECK(st_console_set_pixel(console, 0, 0, 500) == 0);
	CHECK(st_console_bitmap_presenter_create(&presenter, console) == 0);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 1, 1, 0,
	    record_upload, &log, &frame) == 0);
	CHECK(frame.pixels->pixels[0] == red);
	CHECK(st_console_end_update(console) == 0);

	CHECK(st_console_begin_update(console) == 0);
	log.calls = 0;
	CHECK(st_console_set_palette(console, 500, &blue, 1) == 0);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 1, 1, 0,
	    record_upload, &log, &frame) == 0);
	CHECK(log.calls == 1 && log.rectangle.x == 0 && log.rectangle.y == 0
	    && log.rectangle.width == 1 && log.rectangle.height == 1);
	CHECK(frame.pixels->pixels[0] == blue);
	CHECK(st_console_end_update(console) == 0);

	/* Palette cycling also updates an indexed cursor over direct pixels. */
	CHECK(st_console_begin_update(console) == 0);
	CHECK(st_console_set_pixel(console, 0, 0,
	    ST_PIXEL_COLOR_RGB_VALUE(0, 0, 0)) == 0);
	CHECK(st_console_set_palette(console, 501, &red, 1) == 0);
	console->style.foreground = 501;
	CHECK(st_console_set_cursor(console, true, ST_CONSOLE_CURSOR_BLOCK) == 0);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 1, 1, 0,
	    record_upload, &log, &frame) == 0);
	CHECK(frame.pixels->pixels[0] == red);
	CHECK(st_console_end_update(console) == 0);
	CHECK(st_console_begin_update(console) == 0);
	CHECK(st_console_set_palette(console, 501, &blue, 1) == 0);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 1, 1, 0,
	    record_upload, &log, &frame) == 0);
	CHECK(frame.pixels->pixels[0] == blue);
	CHECK(st_console_end_update(console) == 0);

	st_console_bitmap_presenter_destroy(presenter);
	st_console_destroy(console);
}

static void
test_initial_window_geometry(void)
{
	const struct st_console_bitmap_geometry geometry = {640, 400, 4, 3, false};
	struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
		.initial_scaling = 2.0,
	};
	struct st_console_config config = {
		.columns = 80,
		.rows = 25,
		.cell_width = 8,
		.cell_height = 16,
		.aspect_width = 4,
		.aspect_height = 3,
	};
	st_console_t *console = NULL;
	uint32_t width;
	uint32_t height;

	{
		struct st_console_display_config invalid = config_display;
		st_console_display_t *display = NULL;

		invalid.initial_window_width = 700;
		CHECK(st_console_display_create(&display, &invalid)
		    == IO_ERROR_INVALID_ARGUMENT);
		CHECK(display == NULL);
	}

	CHECK(st_console_create(&console, &config_display, &config) == 0);
	CHECK(console->display->initial_window_width == 1280);
	CHECK(console->display->initial_window_height == 960);
	{
		st_console_t *peer = NULL;
		struct st_console_config smaller = config;

		smaller.columns /= 2;
		smaller.rows /= 2;
		CHECK(st_console_create_on_display(&peer, console->display,
		    &smaller) == 0);
		CHECK(console->display->initial_window_width == 1280);
		CHECK(console->display->initial_window_height == 960);
		st_console_destroy(peer);
	}
	CHECK(st_console_snap_geometry_size(&geometry, 1280, 960, false, 0, 0,
	    &width, &height) == 0);
	CHECK(width == 640 && height == 480);
	CHECK(st_console_snap_geometry_size(&geometry, 700, 525, false, 0, 0,
	    &width, &height) == 0);
	CHECK(width == 640 && height == 480);
	CHECK(st_console_snap_geometry_size(&geometry, 700, 525, true, 0, 0,
	    &width, &height) == 0);
	CHECK(width == 1280 && height == 960);
	CHECK(st_console_snap_geometry_size(&geometry, 1280, 960, true, 1500, 1000,
	    &width, &height) == 0);
	CHECK(width == 1280 && height == 960);
	{
		const struct st_console_bitmap_geometry c64 = {320, 200, 6, 5, false};
		double scaling;
		CHECK(st_console_scaled_window_size(320, 200, 6, 5, 1,
		    &width, &height) == 0 && width == 320 && height == 267);
		CHECK(st_console_scaled_window_size(320, 200, 6, 5, 2,
		    &width, &height) == 0 && width == 640 && height == 533);
		CHECK(st_console_snap_geometry_size(&c64, 640, 533, true,
		    1920, 1080, &width, &height) == 0);
		if (width != 960 || height != 800)
			fprintf(stderr, "C64 2x -> 3x: expected 960x800, got %ux%u\n", width, height);
		CHECK(width == 960 && height == 800);
		CHECK(st_console_snap_geometry_size(&c64, 960, 800, true,
		    1920, 1080, &width, &height) == 0 && width == 1280 && height == 1067);
		CHECK(st_console_snap_geometry_size(&c64, 1280, 1067, false,
		    1920, 1080, &width, &height) == 0 && width == 960 && height == 800);
		CHECK(st_console_snap_geometry_size(&c64, 960, 800, true,
		    1920, 1040, &width, &height) == 0 && width == 960 && height == 800);
		CHECK(st_console_snap_geometry_size(&c64, 800, 667, false,
		    1920, 1080, &width, &height) == 0 && width == 640 && height == 533);
		CHECK(st_console_bitmap_geometry_scaling(&c64, 640, 533, &scaling) == 0
		    && scaling == 2.0);
		CHECK(st_console_bitmap_geometry_scaling(&c64, 960, 800, &scaling) == 0
		    && scaling == 3.0);
	}
	{
		const struct st_console_bitmap_geometry portrait = {200, 320, 5, 6, false};
		CHECK(st_console_snap_geometry_size(&portrait, 533, 640, true,
		    1080, 1920, &width, &height) == 0 && width == 800 && height == 960);
		CHECK(st_console_snap_geometry_size(&portrait, 800, 960, false,
		    1080, 1920, &width, &height) == 0 && width == 533 && height == 640);
	}
	st_console_destroy(console);
	config_display.initial_scaling = 0;
	config_display.initial_window_width = 700;
	config_display.initial_window_height = 450;
	CHECK(st_console_create(&console, &config_display, &config) == 0);
	CHECK(console->display->initial_window_width == 700);
	CHECK(console->display->initial_window_height == 480);
	st_console_destroy(console);
}

static void
test_mode_change_geometry(void)
{
	const struct st_console_bitmap_geometry pc = {640, 400, 4, 3, false};
	const struct st_console_bitmap_geometry c64 = {320, 200, 6, 5, false};
	const struct st_console_bitmap_geometry prestel = {480, 500, 3077, 2400, false};
	const struct st_console_bitmap_geometry lcd = {640, 400, 8, 5, false};
	const struct {
		const struct st_console_bitmap_geometry *previous, *next;
		uint32_t current_width, current_height, maximum_width, maximum_height;
		uint32_t width, height;
	} cases[] = {
		{&pc, &c64, 640, 480, 1920, 1080, 640, 533},
		{&pc, &c64, 1280, 960, 1920, 1200, 1280, 1067},
		{&pc, &c64, 1280, 960, 1920, 1040, 960, 800},
		{&c64, &pc, 640, 533, 1920, 1080, 640, 480},
		{&pc, &prestel, 640, 480, 1024, 768, 641, 500},
		{&pc, &lcd, 640, 480, 1024, 768, 640, 400},
		{&pc, &prestel, 800, 600, 1024, 768, 788, 615},
		{&prestel, &pc, 788, 615, 1024, 768, 800, 600},
		{&pc, &lcd, 800, 600, 1024, 768, 848, 530},
		{&lcd, &pc, 848, 530, 1024, 768, 800, 600},
		{&pc, &c64, 800, 600, 1024, 768, 768, 640},
		{&c64, &pc, 768, 640, 1024, 768, 800, 600},
		{&pc, &c64, 640, 480, 0, 0, 640, 533},
		{&pc, &c64, 640, 480, 100, 100, 320, 267},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		uint32_t width, height, minimum_width, minimum_height;
		CHECK(st_console_mode_change_size(cases[i].previous, cases[i].next,
		    cases[i].current_width, cases[i].current_height,
		    cases[i].maximum_width, cases[i].maximum_height, &width, &height,
		    &minimum_width, &minimum_height) == 0);
		if (width != cases[i].width || height != cases[i].height)
			fprintf(stderr, "mode case %zu: expected %ux%u, got %ux%u\n", i,
			    cases[i].width, cases[i].height, width, height);
		CHECK(width == cases[i].width && height == cases[i].height);
		CHECK(st_console_aspect_correct_size(cases[i].next->width,
		    cases[i].next->height, cases[i].next->aspect_width,
		    cases[i].next->aspect_height, &width, &height) == 0);
		CHECK(width == minimum_width && height == minimum_height);
	}
}

static void
test_preserved_window(void)
{
	const struct st_console_bitmap_geometry low = {320,200,4,3,true}, medium = {640,200,4,3,true};
	const uint32_t sizes[][2] = {{320,240},{960,720},{801,601}};
	for (size_t i = 0; i < sizeof(sizes)/sizeof(*sizes); ++i) {
		uint32_t w, h, mw, mh;
		CHECK(st_console_mode_change_size(&low, &medium, sizes[i][0], sizes[i][1], 0, 0,
		    &w, &h, &mw, &mh) == 0);
		CHECK(w == sizes[i][0] && h == sizes[i][1] && mw == 1 && mh == 1);
		CHECK(st_console_mode_change_size(&medium, &low, w, h, 0, 0, &w, &h, &mw, &mh) == 0);
		CHECK(w == sizes[i][0] && h == sizes[i][1]);
	}
	double scale;
	CHECK(st_console_bitmap_geometry_scaling(&medium, 320, 240, &scale) == 0 && scale == 0.5);
	uint32_t w, h, mw, mh;
	CHECK(st_console_preserve_geometry_scale(&medium, &medium, 320, 240, &w, &h, &mw, &mh) == 0);
	CHECK(w == 320 && h == 240 && mw == 1 && mh == 1);
	CHECK(st_console_snap_geometry_size(&medium, 320, 240, false, 0, 0, &w, &h) == 0 && w == 320 && h == 240);
	CHECK(st_console_snap_geometry_size(&medium, 320, 240, true, 0, 0, &w, &h) == 0 && w == 640 && h == 480);
	for (unsigned policy = 0; policy < 2; ++policy) {
		st_console_t *console = NULL;
		struct st_console_bitmap_presenter *presenter = NULL;
		struct st_console_bitmap_frame frame;
		const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
		const struct st_console_config config = {.columns = 80, .rows = 25,
		    .cell_width = 8, .cell_height = 8, .aspect_width = 4, .aspect_height = 3,
		    .preserve_window_size = true, .scaling_policy = (enum st_pixel_scaling_policy)policy};
		CHECK(st_console_create(&console, &display, &config) == 0);
		CHECK(st_console_set_cursor(console, false, ST_CONSOLE_CURSOR_BLOCK) == 0);
		struct st_pixel_buffer *pixels = NULL;
		CHECK(st_pixel_buffer_create(&pixels, 640, 200, false) == 0);
		const uint32_t colours[] = {0xff0000,0x00ff00,0x0000ff,0xffffff};
		for (uint32_t y = 0; y < 200; ++y)
			for (uint32_t x = 0; x < 640; ++x)
				pixels->primary[y * 640 + x] = ST_PIXEL_COLOR_RGB_TAG | colours[(y >= 100 ? 2 : 0) + (x >= 320)];
		const struct st_pixel_blit blit = {.width = 640, .height = 200, .scale_x = 1, .scale_y = 1};
		CHECK(st_console_blit_pixels(console, pixels, NULL, &blit) == 0);
		CHECK(st_console_bitmap_presenter_create(&presenter, console) == 0);
		CHECK(st_console_bitmap_presenter_frame(presenter, console, 320, 240, 0, NULL, NULL, &frame) == 0);
		CHECK(frame.destination.width == 320 && frame.destination.height == 240);
		CHECK(frame.pixels->width == 320 && frame.pixels->height == 240);
		CHECK(frame.pixels->pixels[30 * 320 + 40] == colours[0]);
		CHECK(frame.pixels->pixels[30 * 320 + 280] == colours[1]);
		CHECK(frame.pixels->pixels[210 * 320 + 40] == colours[2]);
		CHECK(frame.pixels->pixels[210 * 320 + 280] == colours[3]);
		st_pixel_buffer_destroy(pixels);
		st_console_bitmap_presenter_destroy(presenter);
		st_console_destroy(console);
	}
}

static void
test_mouse_coordinates(void)
{
	const struct st_console_bitmap_frame frame = {
		.destination = {10, 20, 160, 96},
		.source_width = 80,
		.source_height = 48,
		.cell_width = 8,
		.cell_height = 16,
	};
	struct st_console_event event = {.type = ST_CONSOLE_EVENT_MOUSE};

	st_console_bitmap_mouse_position(&frame, 90, 68, &event);
	CHECK(event.data.mouse.pixel_x == 40);
	CHECK(event.data.mouse.pixel_y == 24);
	CHECK(event.data.mouse.x == 5);
	CHECK(event.data.mouse.y == 1);
	st_console_bitmap_mouse_position(&frame, -20, 500, &event);
	CHECK(event.data.mouse.pixel_x == 0);
	CHECK(event.data.mouse.pixel_y == 47);
	CHECK(event.data.mouse.x == 0);
	CHECK(event.data.mouse.y == 2);
}

static void
test_default_cursor_scanlines(void)
{
	struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	struct st_console_config config = {
		.columns = 1,
		.rows = 1,
		.cell_width = 8,
		.cell_height = 14,
		.scaling_mode = ST_CONSOLE_SCALING_EXTERNAL,
		.default_cursor = {
			.start = 12, .end = 13, .range = 14,
			.visible = true, .blink = false,
		},
	};
	struct st_console_bitmap_presenter *presenter = NULL;
	struct st_console_bitmap_frame frame;
	struct upload_log log = {0};
	st_console_t *console = NULL;
	const uint32_t cursor_colour = UINT32_C(0xa8a8a8);

	CHECK(st_console_create(&console, &config_display, &config) == 0);
	CHECK(st_console_bitmap_presenter_create(&presenter, console) == 0);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 8, 14,
	    0, record_upload, &log, &frame) == 0);
	CHECK(frame.pixels->pixels[11 * 8] != cursor_colour);
	CHECK(frame.pixels->pixels[12 * 8] == cursor_colour);
	CHECK(frame.pixels->pixels[13 * 8] == cursor_colour);
	st_console_bitmap_presenter_destroy(presenter);
	st_console_destroy(console);
}

static void
test_legacy_bright_cursor_colour(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 1, .rows = 1,
		.cell_width = 1, .cell_height = 1,
		.scaling_mode = ST_CONSOLE_SCALING_EXTERNAL,
	};
	const struct st_console_style style = {
		.foreground = 3,
		.background = 1,
		.flags = ST_CONSOLE_CELL_LEGACY_FOREGROUND
		    | ST_CONSOLE_CELL_BRIGHT,
	};
	struct st_console_bitmap_presenter *presenter = NULL;
	struct st_console_bitmap_frame frame;
	struct upload_log log = {0};
	st_console_t *console = NULL;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(st_console_set_style(console, &style) == 0);
	CHECK(st_console_set_cursor(console, true,
	    ST_CONSOLE_CURSOR_BLOCK) == 0);
	CHECK(st_console_bitmap_presenter_create(&presenter, console) == 0);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 1, 1, 0,
	    record_upload, &log, &frame) == 0);
	CHECK(frame.pixels->pixels[0] == UINT32_C(0xffff54));
	CHECK(st_console_set_video_flags(console,
	    ST_CONSOLE_VIDEO_NO_BRIGHT) == 0);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 1, 1, 0,
	    record_upload, &log, &frame) == 0);
	CHECK(frame.pixels->pixels[0] == UINT32_C(0xa85400));
	st_console_bitmap_presenter_destroy(presenter);
	st_console_destroy(console);
}

static void
test_wide_glyph_cursor(void)
{
	static const uint8_t bitmap[4] = {0};
	static const uint8_t wide_bitmap[4] = {0};
	static const struct st_console_font_mapping mapping[] = {
		{UINT32_C(0x4e00), 1},
	};
	static const struct st_console_font font = {
		.bitmap = bitmap,
		.bitmap_size = sizeof(bitmap),
		.width = 2,
		.height = 4,
		.glyph_count = 1,
		.row_stride = 1,
		.glyph_stride = 4,
		.wide_bitmap = wide_bitmap,
		.wide_bitmap_size = sizeof(wide_bitmap),
		.wide_glyph_count = 1,
		.wide_row_stride = 1,
		.wide_glyph_stride = 4,
		.mapping = mapping,
		.mapping_count = sizeof(mapping) / sizeof(mapping[0]),
		.fallback_glyph = 0,
		.wide_fallback_glyph = 1,
	};
	const struct st_console_display_config display_config = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 3,
		.rows = 1,
		.cell_width = 2,
		.cell_height = 4,
		.scaling_mode = ST_CONSOLE_SCALING_EXTERNAL,
		.default_cursor = {
			.start = 3, .end = 3, .range = 4,
			.visible = true, .blink = false,
		},
	};
	struct st_console_bitmap_presenter *presenter = NULL;
	struct st_console_bitmap_frame frame;
	struct upload_log log = {0};
	st_console_t *console = NULL;
	uint16_t identifier;
	const uint32_t colour = UINT32_C(0x010203);

	CHECK(st_console_create(&console, &display_config, &config) == 0);
	CHECK(st_console_register_static_font(console, &font, &identifier) == 0);
	CHECK(identifier == 0);
	console->style.foreground = ST_CONSOLE_COLOR_RGB_VALUE(1, 2, 3);
	CHECK(st_console_put_codepoint(console, UINT32_C(0x4e00)) == 0);
	CHECK(st_console_set_position(console, 0, 0) == 0);
	CHECK(st_console_bitmap_presenter_create(&presenter, console) == 0);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 6, 4,
	    0, record_upload, &log, &frame) == 0);
	for (size_t x = 0; x < 4; ++x)
		CHECK(frame.pixels->pixels[3 * 6 + x] == colour);
	CHECK(frame.pixels->pixels[3 * 6 + 4] != colour);

	/* A cursor placed on the continuation still covers the whole glyph. */
	CHECK(st_console_set_position(console, 1, 0) == 0);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 6, 4,
	    0, record_upload, &log, &frame) == 0);
	for (size_t x = 0; x < 4; ++x)
		CHECK(frame.pixels->pixels[3 * 6 + x] == colour);
	CHECK(frame.pixels->pixels[3 * 6 + 4] != colour);

	/* Replacing the glyph shrinks a stationary cursor back to one cell. */
	CHECK(st_console_set_position(console, 0, 0) == 0);
	CHECK(st_console_put_codepoint(console, 'A') == 0);
	CHECK(st_console_set_position(console, 0, 0) == 0);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 6, 4,
	    0, record_upload, &log, &frame) == 0);
	CHECK(frame.pixels->pixels[3 * 6] == colour);
	CHECK(frame.pixels->pixels[3 * 6 + 1] == colour);
	CHECK(frame.pixels->pixels[3 * 6 + 2] != colour);

	st_console_bitmap_presenter_destroy(presenter);
	st_console_destroy(console);
}

static void
test_pixel_raster_cache(void)
{
	static const uint8_t bitmap[256] = {0};
	const struct st_console_font font = {
		.bitmap = bitmap,
		.bitmap_size = sizeof(bitmap),
		.width = 1,
		.height = 1,
		.glyph_count = 256,
		.row_stride = 1,
		.glyph_stride = 1,
	};
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 2, .rows = 1,
		.cell_width = 1, .cell_height = 1,
		.pixel_width = 2, .pixel_height = 1,
	};
	const struct st_console_rect cell_rect = {1, 0, 1, 1};
	const struct st_console_rect pixel_rect = {0, 0, 2, 1};
	const struct st_console_cell cell = {
		.glyph = ' ', .background = 4,
	};
	struct st_pixel_buffer *pixels = NULL;
	st_console_t *console = NULL;
	uint16_t identifier;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(st_console_begin_update(console) == 0);
	CHECK(st_console_register_static_font(console, &font, &identifier) == 0);
	CHECK(identifier == 0);
	CHECK(!console->raster_current);
	CHECK(st_console_set_pixel(console, 0, 0, 5) == 0);
	CHECK(console->raster_current);
	CHECK(st_console_set_pixel(console, 0, 0, 6) == 0);
	CHECK(console->raster_current);
	CHECK(st_console_write_complete_cells(console, &cell_rect,
	    &cell, 1) == 0);
	CHECK(!console->raster_current);
	CHECK(st_console_set_pixel(console, 0, 0, 7) == 0);
	CHECK(console->raster_current);
	CHECK(st_console_get_pixels(&pixels, console, &pixel_rect, false) == 0);
	CHECK(pixels->primary[0] == 7);
	CHECK(pixels->primary[1] == 4);
	st_pixel_buffer_destroy(pixels);
	CHECK(st_console_end_update(console) == 0);
	st_console_destroy(console);
}

int
main(void)
{
	struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	struct st_console_config config = {
		.columns = 2,
		.rows = 1,
		.cell_width = 2,
		.cell_height = 4,
		.blink_mode = ST_CONSOLE_BLINK_PC,
		.scaling_mode = ST_CONSOLE_SCALING_EXTERNAL,
		.aspect_width = 4,
		.aspect_height = 3,
	};
	struct st_console_bitmap_presenter *presenter = NULL;
	struct st_console_bitmap_frame frame;
	struct upload_log log = {0};
	struct st_console_custom_cursor cursor = {
		.start = 1,
		.end = 2,
		.range = 4,
		.visible = true,
		.blink = false,
	};
	st_console_t *console = NULL;
	uint32_t red = UINT32_C(0xff0000);
	uint32_t blue = UINT32_C(0x0000ff);
	uint32_t width;
	uint32_t height;
	double scaling;
	uint64_t initial_generation;
	const void *initial_identity;

	CHECK(st_console_aspect_correct_size(640, 400, 4, 3,
	    &width, &height) == 0);
	CHECK(width == 640 && height == 480);
	CHECK(st_console_aspect_correct_size(640, 480, 4, 3,
	    &width, &height) == 0);
	CHECK(width == 640 && height == 480);
	CHECK(st_console_aspect_correct_size(480, 500, 3077, 2400,
	    &width, &height) == 0);
	CHECK(width == 641 && height == 500);
	CHECK(st_console_scaled_window_size(640, 400, 4, 3, 2.0,
	    &width, &height) == 0);
	CHECK(width == 1280 && height == 960);
	CHECK(st_console_scaled_window_size(640, 400, 4, 3, 0.25,
	    &width, &height) == 0);
	CHECK(width == 640 && height == 480);

	{
		const struct st_console_bitmap_geometry geometry = {640, 400, 4, 3, false};
		const struct st_console_bitmap_geometry prestel = {
			480, 500, 3077, 2400, false};

		CHECK(st_console_bitmap_geometry_scaling(&geometry, 1280, 960,
		    &scaling) == 0);
		CHECK(scaling == 2.0);
		CHECK(st_console_bitmap_geometry_scaling(&geometry, 1400, 960,
		    &scaling) == 0);
		CHECK(scaling == 2.0);
		CHECK(st_console_bitmap_geometry_scaling(&geometry, 1280, 1000,
		    &scaling) == 0);
		CHECK(scaling == 2.0);
		CHECK(st_console_bitmap_geometry_scaling(&geometry, 800, 600,
		    &scaling) == 0);
		CHECK(scaling == 1.25);
		CHECK(st_console_bitmap_geometry_scaling(&prestel, 641, 500,
		    &scaling) == 0);
		CHECK(scaling == 1.0);
		CHECK(st_console_bitmap_geometry_scaling(&prestel, 801, 625,
		    &scaling) == 0);
		CHECK(scaling == 1.25);
		scaling = 17.0;
		CHECK(st_console_bitmap_geometry_scaling(&geometry, 639, 480,
		    &scaling) == IO_ERROR_OUT_OF_RANGE);
		CHECK(scaling == 17.0);
	}

	{
		struct st_console_bitmap_geometry previous = {640, 400, 4, 3, false};
		struct st_console_bitmap_geometry next = {320, 200, 4, 3, false};
		uint32_t minimum_width, minimum_height;

		CHECK(st_console_preserve_geometry_scale(&previous, &next,
		    1280, 960, &width, &height, &minimum_width,
		    &minimum_height) == 0);
		CHECK(width == 640 && height == 480);
		CHECK(minimum_width == 320 && minimum_height == 240);
		/* The old aspect establishes scale; the new aspect shapes output. */
		next.aspect_width = 0;
		next.aspect_height = 0;
		CHECK(st_console_preserve_geometry_scale(&previous, &next,
		    1280, 960, &width, &height, &minimum_width,
		    &minimum_height) == 0);
		CHECK(width == 640 && height == 400);
		CHECK(minimum_width == 320 && minimum_height == 200);
		CHECK(st_console_preserve_geometry_scale(&next, &previous,
		    width, height, &width, &height, &minimum_width,
		    &minimum_height) == 0);
		CHECK(width == 1280 && height == 960);
	}

	CHECK(st_console_create(&console, &config_display, &config) == 0);
	{
		uint32_t minimum_width;
		uint32_t minimum_height;

		const struct st_console_bitmap_geometry previous = {
			console->pixels->width, console->pixels->height,
			console->aspect_width, console->aspect_height, false};
		const struct st_console_bitmap_geometry next = {
			8, 8, console->aspect_width, console->aspect_height, false};

		CHECK(st_console_preserve_geometry_scale(&previous, &next, 10, 8,
		    &width, &height, &minimum_width, &minimum_height) == 0);
		CHECK(minimum_width == 11 && minimum_height == 8);
		CHECK(width == 21 && height == 16);
	}
	fill_planes(console, red, blue);
	st_console_cell_at(console, 0)->flags |= ST_CONSOLE_CELL_BLINK;
	console->cursor_visible = false;
	CHECK(st_console_bitmap_presenter_create(&presenter, console) == 0);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 100, 100,
	    1000, record_upload, &log, &frame) == 0);
	CHECK(log.calls == 1);
	CHECK(log.rectangle.width == 4 && log.rectangle.height == 4);
	CHECK(frame.destination.x == 0 && frame.destination.y == 12);
	CHECK(frame.destination.width == 100 && frame.destination.height == 75);
	CHECK(frame.pixels->pixels[0] == red);
	initial_generation = frame.generation;
	initial_identity = frame.image_identity;

	/* An undersized host clips; it never scales below the source raster. */
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 4, 4,
	    1000, record_upload, &log, &frame) == 0);
	CHECK(frame.destination.x == 0 && frame.destination.y == 0);
	CHECK(frame.destination.width == 5 && frame.destination.height == 4);

	log.calls = 0;
	CHECK(st_console_bitmap_presenter_frame(presenter, NULL, 100, 100,
	    1266, record_upload, &log, &frame) == 0);
	CHECK(log.calls == 1 && frame.changed);
	CHECK(frame.pixels->pixels[0] == blue);
	CHECK(frame.generation == initial_generation);
	CHECK(frame.image_identity != initial_identity);

	/* A one-cell change remains a one-cell upload with host scaling. */
	console->pixels->primary[2] = ST_PIXEL_COLOR_RGB_VALUE(0, 255, 0);
	st_console_cell_at(console, 0)->flags &= ~ST_CONSOLE_CELL_DIRTY;
	st_console_cell_at(console, 1)->flags |= ST_CONSOLE_CELL_DIRTY;
	log.calls = 0;
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 100, 100,
	    1266, record_upload, &log, &frame) == 0);
	CHECK(log.calls == 1);
	CHECK(log.rectangle.x == 2 && log.rectangle.y == 0);
	CHECK(log.rectangle.width == 2 && log.rectangle.height == 4);

	/* Custom cursor scan lines are composed into the native frame. */
	st_console_cell_at(console, 1)->flags &= ~ST_CONSOLE_CELL_DIRTY;
	console->cursor_x = 1;
	console->cursor_y = 0;
	console->cursor_custom = true;
	console->custom_cursor = cursor;
	console->style.foreground = ST_CONSOLE_COLOR_RGB_VALUE(1, 2, 3);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 100, 100,
	    1266, record_upload, &log, &frame) == 0);
	CHECK(frame.pixels->pixels[2] != UINT32_C(0x010203));
	CHECK(frame.pixels->pixels[6] == UINT32_C(0x010203));
	CHECK(frame.pixels->pixels[10] == UINT32_C(0x010203));
	CHECK(frame.pixels->pixels[14] != UINT32_C(0x010203));

	/* Software scaling produces the aspect-fitted final texture. */
	console->scaling_mode = ST_CONSOLE_SCALING_INTERNAL;
	log.calls = 0;
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 10, 10,
	    1266, record_upload, &log, &frame) == 0);
	CHECK(log.calls == 1);
	CHECK(frame.pixels->width == 10 && frame.pixels->height == 8);
	CHECK(frame.destination.x == 0 && frame.destination.y == 1);
	CHECK(frame.destination.width == 10 && frame.destination.height == 8);

	/* Internal blocky scaling preserves a dirty cell as scaled damage. */
	console->pixels->alternate[0] = ST_PIXEL_COLOR_RGB_VALUE(0x12, 0x34,
	    0x56);
	st_console_cell_at(console, 0)->flags |= ST_CONSOLE_CELL_DIRTY;
	log.calls = 0;
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 10, 10,
	    1266, record_upload, &log, &frame) == 0);
	CHECK(log.calls == 1);
	CHECK(log.rectangle.width < frame.pixels->width);
	CHECK(log.rectangle.height == frame.pixels->height);

	st_console_bitmap_presenter_destroy(presenter);
	console->scaling_mode = ST_CONSOLE_SCALING_EXTERNAL;
	CHECK(st_console_bitmap_presenter_create(&presenter, console) == 0);
	st_console_bitmap_presenter_set_external_scaling_available(presenter, false);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 10, 10,
	    1266, record_upload, &log, &frame) == 0);
	CHECK(frame.scaling_mode == ST_CONSOLE_SCALING_INTERNAL);
	CHECK(frame.pixels->width == 10 && frame.pixels->height == 8);
	CHECK(console->scaling_mode == ST_CONSOLE_SCALING_EXTERNAL);
	/* Timer-only frames retain the effective mode, not the unavailable request. */
	CHECK(st_console_bitmap_presenter_frame(presenter, NULL, 10, 10,
	    1266, record_upload, &log, &frame) == 0);
	CHECK(!frame.changed);
	st_console_bitmap_presenter_destroy(presenter);
	st_console_destroy(console);
	test_circular_text_raster();
	test_full_surface_scroll_gap();
	test_full_surface_scroll_right_strip();
	test_indexed_pixel_palette_change();
	test_initial_window_geometry();
	test_mode_change_geometry();
	test_preserved_window();
	test_mouse_coordinates();
	test_default_cursor_scanlines();
	test_legacy_bright_cursor_colour();
	test_wide_glyph_cursor();
	test_vga_ninth_column();
	test_upload_retry();
	test_pixel_cursor_overlay();
	test_pixel_raster_cache();
	return 0;
}
