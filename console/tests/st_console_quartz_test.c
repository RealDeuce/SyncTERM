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
	const struct st_console_config config = {
		.columns = 8,
		.rows = 4,
		.cell_width = 8,
		.cell_height = 16,
		.backend = ST_CONSOLE_BACKEND_QUARTZ,
		.wrap = true,
	};
	struct st_console_config automatic = config;
	st_console_t *first = NULL;
	st_console_t *second = NULL;
	uint32_t width;
	uint32_t height;
	int status;

	status = st_console_create(&first, &config);
	if (status == IO_ERROR_NOT_FOUND || status == IO_ERROR_NOT_SUPPORTED)
		return 77;
	CHECK(status == 0);
	automatic.backend = ST_CONSOLE_BACKEND_AUTO;
	CHECK(st_console_create(&second, &automatic) == 0);
	CHECK(st_console_set_title(first, "st_console Quartz one") == 0);
	CHECK(st_console_set_title(second, "st_console Quartz two") == 0);
	CHECK(st_console_set_pointer_visible(first, false) == 0);
	CHECK(st_console_set_pointer_visible(first, true) == 0);
	CHECK(st_console_set_pixel(first, 4, 5, UINT32_C(0x123456)) == 0);
	CHECK(st_console_present(first) == 0);
	CHECK(st_console_present(second) == 0);
	CHECK(st_console_resize(first, 10, 5) == 0);
	CHECK(st_console_pixel_size(first, &width, &height) == 0);
	CHECK(width == 80 && height == 80);
	st_console_destroy(second);
	st_console_destroy(first);
	return 0;
}
