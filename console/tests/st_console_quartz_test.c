/* Quartz backend integration tests. */

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
		.backend = ST_CONSOLE_BACKEND_QUARTZ,
	};
	const struct st_console_config config = {
		.columns = 8,
		.rows = 4,
		.cell_width = 8,
		.cell_height = 16,
		.wrap = true,
	};
	struct st_console_display_config automatic_display = config_display;
	struct st_console_config automatic = config;
	st_console_t *first = NULL;
	st_console_t *second = NULL;
	st_console_display_t *first_display = NULL;
	uint32_t width;
	uint32_t height;
	double scaling;
	int status;

	status = st_console_display_create(&first_display, &config_display);
	if (status == 0)
		status = st_console_create_on_display(&first, first_display, &config);
	if (status == IO_ERROR_NOT_FOUND || status == IO_ERROR_NOT_SUPPORTED)
		return 77;
	CHECK(status == 0);
	CHECK(st_console_display_get_scaling(first_display, &scaling) == 0);
	CHECK(scaling == 1.0);
	automatic_display.backend = ST_CONSOLE_BACKEND_AUTO;
	CHECK(st_console_create(&second, &automatic_display, &automatic) == 0);
	CHECK(st_console_set_title(first, "st_console Quartz one") == 0);
	CHECK(st_console_set_title(second, "st_console Quartz two") == 0);
	CHECK(st_console_set_pointer_visible(first, false) == 0);
	CHECK(st_console_set_pointer_visible(first, true) == 0);
	CHECK(st_console_set_pointer_shape(first, ST_CONSOLE_POINTER_TEXT) == 0);
	CHECK(st_console_set_pointer_shape(first, ST_CONSOLE_POINTER_LINK) == 0);
	CHECK(st_console_set_pointer_shape(first, ST_CONSOLE_POINTER_DEFAULT) == 0);
	CHECK(st_console_set_pixel(first, 4, 5,
	    ST_CONSOLE_COLOR_RGB_VALUE(0x12, 0x34, 0x56)) == 0);
	CHECK(st_console_present(first) == 0);
	CHECK(st_console_present(second) == 0);
	CHECK(st_console_resize(first, 10, 5) == 0);
	CHECK(st_console_pixel_size(first, &width, &height) == 0);
	CHECK(width == 80 && height == 80);
	st_console_destroy(second);
	st_console_destroy(first);
	st_console_display_destroy(first_display);
	return 0;
}
