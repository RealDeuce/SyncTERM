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
	};
	st_console_t *console = NULL;
	st_console_t *peer = NULL;
	st_console_display_t *headless = NULL;
	st_console_display_t *display = NULL;
	const struct st_console_display_config display_config = {
		.backend = ST_CONSOLE_BACKEND_LIBRETRO,
	};
	const struct st_console_display_config headless_config = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	uint32_t pixels[48] = {0};
	uint32_t width;
	uint32_t height;
	struct st_console_config other = config;

	CHECK(st_console_display_create(&display, &display_config) == 0);
	CHECK(st_console_create_on_display(&console, display, &config) == 0);
	{
		struct st_console_config text = config;
		st_console_t *rejected = console;

		text.cell_width = 0;
		text.cell_height = 0;
		CHECK(st_console_create_on_display(&rejected, display, &text)
		    == IO_ERROR_NOT_SUPPORTED);
		CHECK(rejected == console);
	}
	CHECK(st_console_display_libretro_frame_size(display, &width, &height) == 0);
	CHECK(width == 4 && height == 4);
	CHECK(st_console_set_pixel(console, 1, 2, UINT32_C(0x123456)) == 0);
	CHECK(st_console_display_libretro_frame_copy(display, pixels, 6, 8) == 0);
	CHECK(pixels[2 * 6 + 1] == UINT32_C(0x123456));
	CHECK(st_console_display_libretro_frame_copy(display, pixels, 3, 8)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_console_resize(console, 3, 2) == 0);
	CHECK(st_console_display_libretro_frame_size(display, &width, &height) == 0);
	CHECK(width == 6 && height == 4);
	CHECK(st_console_display_libretro_frame_copy(display, pixels, 6, 8) == 0);
	other.rows = 3;
	CHECK(st_console_create_on_display(&peer, display, &other) == 0);
	CHECK(st_console_display_activate(display, peer) == 0);
	pixels[0] = UINT32_C(0x123456);
	CHECK(st_console_display_libretro_frame_copy(display, pixels, 6, 4)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(pixels[0] == UINT32_C(0x123456));
	CHECK(st_console_display_libretro_frame_size(display, &width, &height) == 0);
	CHECK(width == 4 && height == 6);
	CHECK(st_console_display_libretro_frame_copy(display, pixels, 6, 8) == 0);
	CHECK(st_console_display_create(&headless, &headless_config) == 0);
	CHECK(st_console_display_libretro_frame_size(headless, &width, &height)
	    == IO_ERROR_NOT_SUPPORTED);
	st_console_display_destroy(headless);
	st_console_destroy(peer);
	st_console_destroy(console);
	st_console_display_destroy(display);
	return 0;
}
