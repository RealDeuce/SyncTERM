/* Libretro pull-backend tests. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_console.h"

#include "io_error.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, \
		    __LINE__, #expression); \
		return 1; \
	} \
} while (0)

int
main(void)
{
	const struct st_console_config config = {
		.columns = 2, .rows = 2,
		.cell_width = 2, .cell_height = 2,
		.backend = ST_CONSOLE_BACKEND_LIBRETRO,
	};
	st_console_t *console = NULL;
	st_console_t *headless = NULL;
	uint32_t pixels[48] = {0};
	uint32_t width;
	uint32_t height;
	struct st_console_config other = config;

	CHECK(st_console_create(&console, &config) == 0);
	CHECK(st_console_libretro_frame_size(console, &width, &height) == 0);
	CHECK(width == 4 && height == 4);
	CHECK(st_console_set_pixel(console, 1, 2, UINT32_C(0x123456)) == 0);
	CHECK(st_console_libretro_frame_copy(console, pixels, 6) == 0);
	CHECK(pixels[2 * 6 + 1] == UINT32_C(0x123456));
	CHECK(st_console_libretro_frame_copy(console, pixels, 3)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_console_resize(console, 3, 2) == 0);
	CHECK(st_console_libretro_frame_size(console, &width, &height) == 0);
	CHECK(width == 6 && height == 4);
	CHECK(st_console_libretro_frame_copy(console, pixels, 6) == 0);
	other.backend = ST_CONSOLE_BACKEND_HEADLESS;
	CHECK(st_console_create(&headless, &other) == 0);
	CHECK(st_console_libretro_frame_size(headless, &width, &height)
	    == IO_ERROR_NOT_SUPPORTED);
	st_console_destroy(headless);
	st_console_destroy(console);
	return 0;
}
