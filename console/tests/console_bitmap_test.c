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
	unsigned int calls;
};

static int
record_upload(void *context, const struct st_pixel_buffer *pixels,
	const struct st_console_rect *rectangle)
{
	struct upload_log *log = context;

	CHECK(pixels != NULL);
	CHECK(rectangle != NULL);
	log->rectangle = *rectangle;
	++log->calls;
	return 0;
}

static void
fill_planes(st_console_t *console, uint32_t primary, uint32_t alternate)
{
	size_t count = (size_t)console->pixels->width * console->pixels->height;

	for (size_t i = 0; i < count; ++i) {
		console->pixels->primary[i] = primary;
		console->pixels->alternate[i] = alternate;
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
	CHECK(st_console_set_pixel(console, 0, 0, red) == 0);
	CHECK(st_console_set_pixel(console, 0, 1, blue) == 0);
	CHECK(st_console_set_pixel(console, 0, 2, green) == 0);
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
	CHECK(log.calls == 2);
	CHECK(frame.pixels->primary[0] == blue);
	CHECK(frame.pixels->primary[1] == 0);
	CHECK(frame.pixels->primary[2] == green);
	CHECK(st_console_end_update(console) == 0);

	CHECK(st_console_begin_update(console) == 0);
	CHECK(st_console_set_pixel(console, 0, 2, yellow) == 0);
	log.calls = 0;
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 1, 3,
	    1000, record_upload, &log, &frame) == 0);
	CHECK(log.calls == 1);
	CHECK(log.rectangle.width == 1 && log.rectangle.height == 3);
	CHECK(frame.pixels->primary[2] == yellow);
	CHECK(st_console_end_update(console) == 0);

	st_console_bitmap_presenter_destroy(presenter);
	st_console_destroy(console);
}

static void
test_initial_window_geometry(void)
{
	const struct st_console_bitmap_geometry geometry = {640, 400, 4, 3};
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
	const uint32_t cursor_colour = UINT32_C(0xaaaaaa);

	CHECK(st_console_create(&console, &config_display, &config) == 0);
	CHECK(st_console_bitmap_presenter_create(&presenter, console) == 0);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 8, 14,
	    0, record_upload, &log, &frame) == 0);
	CHECK(frame.pixels->primary[11 * 8] != cursor_colour);
	CHECK(frame.pixels->primary[12 * 8] == cursor_colour);
	CHECK(frame.pixels->primary[13 * 8] == cursor_colour);
	st_console_bitmap_presenter_destroy(presenter);
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
		struct st_console_bitmap_geometry previous = {640, 400, 4, 3};
		struct st_console_bitmap_geometry next = {320, 200, 4, 3};
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
			console->aspect_width, console->aspect_height};
		const struct st_console_bitmap_geometry next = {
			8, 8, console->aspect_width, console->aspect_height};

		CHECK(st_console_preserve_geometry_scale(&previous, &next, 10, 8,
		    &width, &height, &minimum_width, &minimum_height) == 0);
		CHECK(minimum_width == 11 && minimum_height == 8);
		CHECK(width == 21 && height == 16);
	}
	fill_planes(console, red, blue);
	console->cells[0].flags |= ST_CONSOLE_CELL_BLINK;
	console->cursor_visible = false;
	CHECK(st_console_bitmap_presenter_create(&presenter, console) == 0);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 100, 100,
	    1000, record_upload, &log, &frame) == 0);
	CHECK(log.calls == 1);
	CHECK(log.rectangle.width == 4 && log.rectangle.height == 4);
	CHECK(frame.destination.x == 0 && frame.destination.y == 12);
	CHECK(frame.destination.width == 100 && frame.destination.height == 75);
	CHECK(frame.pixels->primary[0] == red);

	/* An undersized host clips; it never scales below the source raster. */
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 4, 4,
	    1000, record_upload, &log, &frame) == 0);
	CHECK(frame.destination.x == 0 && frame.destination.y == 0);
	CHECK(frame.destination.width == 5 && frame.destination.height == 4);

	log.calls = 0;
	CHECK(st_console_bitmap_presenter_frame(presenter, NULL, 100, 100,
	    1266, record_upload, &log, &frame) == 0);
	CHECK(log.calls == 1 && frame.changed);
	CHECK(frame.pixels->primary[0] == blue);

	/* A one-cell change remains a one-cell upload with host scaling. */
	console->pixels->primary[2] = UINT32_C(0x00ff00);
	console->cells[0].flags &= ~ST_CONSOLE_CELL_DIRTY;
	console->cells[1].flags |= ST_CONSOLE_CELL_DIRTY;
	log.calls = 0;
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 100, 100,
	    1266, record_upload, &log, &frame) == 0);
	CHECK(log.calls == 1);
	CHECK(log.rectangle.x == 2 && log.rectangle.y == 0);
	CHECK(log.rectangle.width == 2 && log.rectangle.height == 4);

	/* Custom cursor scan lines are composed into the native frame. */
	console->cells[1].flags &= ~ST_CONSOLE_CELL_DIRTY;
	console->cursor_x = 1;
	console->cursor_y = 0;
	console->cursor_custom = true;
	console->custom_cursor = cursor;
	console->style.foreground = ST_CONSOLE_COLOR_RGB_VALUE(1, 2, 3);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 100, 100,
	    1266, record_upload, &log, &frame) == 0);
	CHECK(frame.pixels->primary[2] != UINT32_C(0x010203));
	CHECK(frame.pixels->primary[6] == UINT32_C(0x010203));
	CHECK(frame.pixels->primary[10] == UINT32_C(0x010203));
	CHECK(frame.pixels->primary[14] != UINT32_C(0x010203));

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
	console->pixels->alternate[0] = UINT32_C(0x123456);
	console->cells[0].flags |= ST_CONSOLE_CELL_DIRTY;
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
	test_initial_window_geometry();
	test_default_cursor_scanlines();
	return 0;
}
