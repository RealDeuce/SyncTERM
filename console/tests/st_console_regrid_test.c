/* Text-grid changes must preserve an already rendered pixel surface. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_console.h"
#include "st_pixels.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		return 1; \
	} \
} while (0)

int
main(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 4, .rows = 2,
		.cell_width = 8, .cell_height = 8,
		.pixel_width = 32, .pixel_height = 16,
	};
	const struct st_console_rect rect = {0, 0, 32, 16};
	st_console_t *console = NULL;
	struct st_pixel_buffer *before = NULL;
	struct st_pixel_buffer *after = NULL;
	uint32_t columns, rows, width, height;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(st_console_set_pixel(console, 20, 5,
	    ST_PIXEL_COLOR_RGB_VALUE(0x12, 0x34, 0x56)) == 0);
	CHECK(st_console_get_pixels(&before, console, &rect, false) == 0);
	CHECK(st_console_regrid_preserve_pixels(console,
	    16, 1, 2, 16) == 0);
	CHECK(st_console_size(console, &columns, &rows) == 0);
	CHECK(columns == 16 && rows == 1);
	CHECK(st_console_cell_pixel_size(console, &width, &height) == 0);
	CHECK(width == 2 && height == 16);
	CHECK(st_console_get_pixels(&after, console, &rect, false) == 0);
	CHECK(memcmp(before->primary, after->primary,
	    32u * 16u * sizeof(before->primary[0])) == 0);
	CHECK(memcmp(before->alternate, after->alternate,
	    32u * 16u * sizeof(before->alternate[0])) == 0);
	st_pixel_buffer_destroy(after);
	CHECK(st_console_regrid_preserve_pixels(console,
	    17, 1, 2, 16) != 0);
	CHECK(st_console_size(console, &columns, &rows) == 0);
	CHECK(columns == 16 && rows == 1);
	CHECK(st_console_get_pixels(&after, console, &rect, false) == 0);
	CHECK(memcmp(before->primary, after->primary,
	    32u * 16u * sizeof(before->primary[0])) == 0);
	st_pixel_buffer_destroy(after);
	st_pixel_buffer_destroy(before);
	st_console_destroy(console);
	return 0;
}
