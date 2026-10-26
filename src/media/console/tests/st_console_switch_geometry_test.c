/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_console.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
		    __FILE__, __LINE__, #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (false)

int
main(void)
{
	const struct st_console_display_config display_config = {
		.backend = ST_CONSOLE_BACKEND_LIBRETRO,
	};
	const struct st_console_config first_config = {
		.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 16,
		.scaling_mode = ST_CONSOLE_SCALING_EXTERNAL,
	};
	const struct st_console_config second_config = {
		.columns = 80, .rows = 50, .cell_width = 8, .cell_height = 8,
		.scaling_mode = ST_CONSOLE_SCALING_EXTERNAL,
	};
	const struct st_console_config reconfigured = {
		.columns = 40, .rows = 25, .cell_width = 12, .cell_height = 20,
		.pixel_width = 480, .pixel_height = 500,
		.default_cursor = {
			.start = 14, .end = 15, .range = 20,
			.visible = true, .blink = true,
		},
		.wrap = true,
		.blink_mode = ST_CONSOLE_BLINK_PRESTEL,
		.scaling_mode = ST_CONSOLE_SCALING_EXTERNAL,
		.aspect_width = 3077, .aspect_height = 2400,
	};
	const struct st_console_custom_cursor cursor = {
		.start = 0, .end = 7, .range = 8, .visible = true, .blink = false,
	};
	const struct st_console_custom_cursor tall_cursor = {
		.start = 0, .end = 15, .range = 16, .visible = true, .blink = false,
	};
	st_console_display_t *display = NULL;
	st_console_t *first = NULL;
	st_console_t *second = NULL;
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
	struct st_console_cell cell;
	struct st_console_rect one = {0, 0, 1, 1};
	struct st_console_custom_cursor mode_cursor;
	enum st_console_blink_mode blink;
	bool correct;

	CHECK(st_console_display_create(&display, &display_config) == 0);
	CHECK(st_console_create_on_display(&first, display, &first_config) == 0);
	CHECK(st_console_create_on_display(&second, display, &second_config) == 0);
	pixels = malloc((size_t)640 * 400 * sizeof(*pixels));
	CHECK(pixels != NULL);
	CHECK(st_console_set_position(second, 79, 49) == 0);
	CHECK(st_console_set_custom_cursor(second, &cursor) == 0);
	CHECK(st_console_display_activate(display, second) == 0);
	CHECK(st_console_display_libretro_frame_size(display, &width, &height) == 0);
	CHECK(width == 640 && height == 400);
	CHECK(st_console_display_libretro_frame_copy(display, pixels, 640, 400)
	    == 0);
	correct = true;
	for (uint32_t y = 392; y < 400; ++y) {
		for (uint32_t x = 632; x < 640; ++x) {
			if (pixels[(size_t)y * 640 + x] != UINT32_C(0xa8a8a8)) {
				fprintf(stderr, "pixel %u,%u: got %06x\n", x, y,
				    (unsigned int)pixels[(size_t)y * 640 + x]);
				correct = false;
			}
		}
	}
	CHECK(st_console_reconfigure(second, &reconfigured) == 0);
	CHECK(st_console_size(second, &width, &height) == 0);
	CHECK(width == 40 && height == 25);
	CHECK(st_console_cell_pixel_size(second, &width, &height) == 0);
	CHECK(width == 12 && height == 20);
	CHECK(st_console_get_blink_mode(second, &blink) == 0);
	CHECK(blink == ST_CONSOLE_BLINK_PRESTEL);
	CHECK(st_console_get_default_cursor(second, &mode_cursor) == 0);
	CHECK(mode_cursor.start == 14 && mode_cursor.end == 15
	    && mode_cursor.range == 20 && mode_cursor.blink);
	CHECK(st_console_read_cells(second, &one, &cell, 1) == 0);
	CHECK(cell.glyph == ' ' && cell.foreground == 7
	    && cell.background == 0 && cell.font == 0);
	CHECK(st_console_set_position(first, 79, 24) == 0);
	CHECK(st_console_set_custom_cursor(first, &tall_cursor) == 0);
	CHECK(st_console_display_activate(display, first) == 0);
	CHECK(st_console_display_libretro_frame_copy(display, pixels, 640, 400)
	    == 0);
	for (uint32_t y = 384; y < 400; ++y) {
		for (uint32_t x = 632; x < 640; ++x)
			CHECK(pixels[(size_t)y * 640 + x] == UINT32_C(0xa8a8a8));
	}
	free(pixels);
	st_console_destroy(second);
	st_console_destroy(first);
	st_console_display_destroy(display);
	CHECK(correct);
	return EXIT_SUCCESS;
}
