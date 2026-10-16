/* Resize callbacks must observe one consistent destination geometry. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_internal.h"
#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (false)

struct resize_state {
	const struct st_console_config *expected;
	struct st_console_bitmap_geometry geometry;
	unsigned int resizes;
	unsigned int presents;
	bool fail;
};

static int
resize_console(st_console_t *console, uint32_t columns, uint32_t rows)
{
	struct resize_state *state = console->display->backend_state;
	const struct st_console_config *expected = state->expected;
	uint32_t width = expected->pixel_width != 0 ? expected->pixel_width
	    : expected->columns * expected->cell_width;
	uint32_t height = expected->pixel_height != 0 ? expected->pixel_height
	    : expected->rows * expected->cell_height;

	CHECK(columns == expected->columns && rows == expected->rows);
	CHECK(console->cell_width == expected->cell_width);
	CHECK(console->cell_height == expected->cell_height);
	CHECK(columns * console->cell_width + console->pixel_extra_width == width);
	CHECK(rows * console->cell_height + console->pixel_extra_height == height);
	CHECK(console->aspect_width == expected->aspect_width);
	CHECK(console->aspect_height == expected->aspect_height);
	++state->resizes;
	if (state->fail)
		return IO_ERROR_IO;
	state->geometry = (struct st_console_bitmap_geometry){
		width, height, console->aspect_width, console->aspect_height,
	};
	return 0;
}

static int
present_console(st_console_t *console)
{
	struct resize_state *state = console->display->backend_state;

	CHECK(console->pixels->width == state->geometry.width);
	CHECK(console->pixels->height == state->geometry.height);
	CHECK(console->aspect_width == state->geometry.aspect_width);
	CHECK(console->aspect_height == state->geometry.aspect_height);
	++state->presents;
	return 0;
}

static const struct st_console_backend_ops backend = {
	.resize = resize_console,
	.present = present_console,
};

int
main(void)
{
	const struct st_console_display_config display_config = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config original = {
		.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 16,
		.pixel_width = 656, .pixel_height = 408,
		.aspect_width = 4, .aspect_height = 3,
	};
	struct st_console_config next = {
		.columns = 40, .rows = 25, .cell_width = 12, .cell_height = 20,
		.aspect_width = 3077, .aspect_height = 2400,
	};
	struct resize_state state = {
		.expected = &next,
		.geometry = {656, 408, 4, 3},
	};
	st_console_display_t *display = NULL;
	st_console_t *console = NULL, *inactive = NULL;
	const uint32_t marker = ST_PIXEL_COLOR_RGB_VALUE(0x12, 0x34, 0x56);

	CHECK(st_console_display_create(&display, &display_config) == 0);
	CHECK(st_console_create_on_display(&console, display, &original) == 0);
	CHECK(st_console_set_pixel(console, 3, 5, marker) == 0);
	struct st_pixel_buffer *old_pixels = console->pixels;
	struct st_console_cell *old_cells = console->cells;
	struct st_console_cell old_cell = *st_console_cell_at(console, 0);
	display->backend_ops = &backend;
	display->backend_state = &state;
	state.fail = true;
	CHECK(st_console_reconfigure(console, &next) == IO_ERROR_IO);
	CHECK(state.resizes == 1 && state.presents == 0);
	CHECK(console->columns == 80 && console->rows == 25);
	CHECK(console->cell_width == 8 && console->cell_height == 16);
	CHECK(console->pixel_extra_width == 16 && console->pixel_extra_height == 8);
	CHECK(console->aspect_width == 4 && console->aspect_height == 3);
	CHECK(console->pixels == old_pixels && console->cells == old_cells);
	CHECK(memcmp(st_console_cell_at(console, 0), &old_cell, sizeof(old_cell)) == 0);
	CHECK(console->pixels->primary[5 * 656 + 3] == marker);
	CHECK(st_console_present(console) == 0);
	CHECK(state.presents == 1);
	CHECK(console->pixels->primary[5 * 656 + 3] == marker);
	state.fail = false;
	for (unsigned int cycle = 0; cycle < 3; ++cycle) {
		state.expected = &next;
		CHECK(st_console_reconfigure(console, &next) == 0);
		state.expected = &original;
		CHECK(st_console_reconfigure(console, &original) == 0);
	}
	/* An aspect-only change must still reach the resize callback. */
	next = original;
	next.aspect_width = 8;
	next.aspect_height = 5;
	state.expected = &next;
	CHECK(st_console_reconfigure(console, &next) == 0);
	/* Inactive reconfiguration waits until activation to touch the window. */
	CHECK(st_console_create_on_display(&inactive, display, &original) == 0);
	unsigned int resizes = state.resizes;
	next.aspect_width = next.aspect_height = 0;
	CHECK(st_console_reconfigure(inactive, &next) == 0);
	CHECK(state.resizes == resizes);
	CHECK(st_console_display_activate(display, inactive) == 0);
	CHECK(state.resizes == resizes + 1);
	state.expected = &next;
	next.aspect_width = 8;
	next.aspect_height = 5;
	CHECK(st_console_display_activate(display, console) == 0);
	st_console_destroy(inactive);
	display->backend_ops = NULL;
	display->backend_state = NULL;
	st_console_destroy(console);
	st_console_display_destroy(display);
	return EXIT_SUCCESS;
}
