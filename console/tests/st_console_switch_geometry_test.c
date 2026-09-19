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
		.columns = 4, .rows = 1, .cell_width = 4, .cell_height = 1,
	};
	const struct st_console_config second_config = {
		.columns = 2, .rows = 1, .cell_width = 8, .cell_height = 1,
	};
	const struct st_console_custom_cursor cursor = {
		.start = 0, .end = 0, .range = 1, .visible = true, .blink = false,
	};
	st_console_display_t *display = NULL;
	st_console_t *first = NULL;
	st_console_t *second = NULL;
	uint32_t pixels[16];
	uint32_t width;
	uint32_t height;
	bool correct;

	CHECK(st_console_display_create(&display, &display_config) == 0);
	CHECK(st_console_create_on_display(&first, display, &first_config) == 0);
	CHECK(st_console_create_on_display(&second, display, &second_config) == 0);
	CHECK(st_console_set_position(second, 1, 0) == 0);
	CHECK(st_console_set_custom_cursor(second, &cursor) == 0);
	CHECK(st_console_display_activate(display, second) == 0);
	CHECK(st_console_display_libretro_frame_size(display, &width, &height) == 0);
	CHECK(width == 16 && height == 1);
	CHECK(st_console_display_libretro_frame_copy(display, pixels, 16, 1) == 0);
	correct = true;
	for (size_t x = 0; x < 16; ++x) {
		uint32_t expected = x < 8 ? 0 : UINT32_C(0xaaaaaa);

		if (pixels[x] != expected) {
			fprintf(stderr, "pixel %zu: got %06x, expected %06x\n",
			    x, (unsigned int)pixels[x], (unsigned int)expected);
			correct = false;
		}
	}
	st_console_destroy(second);
	st_console_destroy(first);
	st_console_display_destroy(display);
	CHECK(correct);
	return EXIT_SUCCESS;
}
