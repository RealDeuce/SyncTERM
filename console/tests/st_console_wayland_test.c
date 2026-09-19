/* Wayland backend integration tests. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_console.h"
#include "io_error.h"

#include <stdint.h>
#include <stdio.h>

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
	const struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_WAYLAND,
	};
	const struct st_console_config config = {
		.columns = 8,
		.rows = 4,
		.cell_width = 8,
		.cell_height = 16,
		.wrap = true,
	};
	st_console_t *first = NULL;
	st_console_t *second = NULL;
	st_console_t *peer = NULL;
	st_console_display_t *display = NULL;
	const struct st_console_display_config display_config = {
		.backend = ST_CONSOLE_BACKEND_WAYLAND,
	};
	uint32_t width;
	uint32_t height;
	int status = st_console_create(&first, &config_display, &config);

	if (status == IO_ERROR_NOT_FOUND || status == IO_ERROR_NOT_SUPPORTED)
		return 77;
	CHECK(status == 0);
	CHECK(st_console_create(&second, &config_display, &config) == 0);
	CHECK(st_console_set_title(first, "st_console Wayland one") == 0);
	CHECK(st_console_set_title(second, "st_console Wayland two") == 0);
	CHECK(st_console_set_pixel(first, 4, 5,
	    ST_CONSOLE_COLOR_RGB_VALUE(0x12, 0x34, 0x56)) == 0);
	CHECK(st_console_present(first) == 0);
	CHECK(st_console_present(second) == 0);
	CHECK(st_console_resize(first, 10, 5) == 0);
	CHECK(st_console_pixel_size(first, &width, &height) == 0);
	CHECK(width == 80 && height == 80);
	CHECK(st_console_set_fullscreen(first, true) == 0);
	CHECK(st_console_set_fullscreen(first, false) == 0);
	/* No pointer entry is required to set visibility or switch consoles. */
	CHECK(st_console_display_create(&display, &display_config) == 0);
	CHECK(st_console_create_on_display(&peer, display, &config) == 0);
	CHECK(st_console_set_pointer_visible(peer, false) == 0);
	CHECK(st_console_set_pointer_visible(peer, true) == 0);
	{
		st_console_t *other = NULL;

		CHECK(st_console_create_on_display(&other, display, &config) == 0);
		CHECK(st_console_display_activate(display, other) == 0);
		CHECK(st_console_display_activate(display, peer) == 0);
		st_console_destroy(other);
	}
	st_console_destroy(peer);
	st_console_display_destroy(display);
	st_console_destroy(second);
	st_console_destroy(first);
	return 0;
}
