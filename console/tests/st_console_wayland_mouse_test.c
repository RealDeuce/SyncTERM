/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Exercise coordinate conversion without connecting to a compositor. */
#include "../console_wayland.c"

int
main(void)
{
	struct st_pixel_buffer pixels = {.width = 80, .height = 40};
	struct state state = {
		.frame = {
			.pixels = &pixels,
			.destination = {10, 10, 160, 80},
		},
	};
	int32_t x, y;

	mouse_position(&state, wl_fixed_from_int(5), wl_fixed_from_int(5),
	    &x, &y);
	fprintf(stderr, "upper-left margin maps to (%d, %d), expected (0, 0)\n",
	    (int)x, (int)y);
	return x == 0 && y == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
