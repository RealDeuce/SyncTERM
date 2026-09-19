/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Exercise colour mapping without opening a terminal or loading curses. */
#include "../console_ncurses.c"

int
main(void)
{
#if ST_CONSOLE_HAVE_CURSES_H
	struct curses_state state = {
		.direct_colour = true,
		.colour_count = 16777216,
		.red_bits = 8, .green_bits = 8, .blue_bits = 8,
	};
	st_console_display_t display = {.backend_state = &state};
	st_console_t console = {.display = &display};
	int indexed_colours[UINT16_MAX + 1U];
	int indexed;
	int direct;

	state.indexed_colours = indexed_colours;
	/* Both indexed and direct colours resolve through their logical RGB. */
	console.palette[15] = UINT32_C(0xffffff);
	rebuild_colour_maps(&console);
	indexed = cell_colour(&console, &state, 15, 0);
	direct = cell_colour(&console, &state,
	    ST_CONSOLE_COLOR_RGB_VALUE(255, 255, 255), 0);
	fprintf(stderr, "native RGB: indexed white=%06x, direct white=%06x\n",
	    (unsigned int)indexed, (unsigned int)direct);
	return indexed == 0xffffff && direct == 0xffffff
	    ? EXIT_SUCCESS : EXIT_FAILURE;
#else
	fprintf(stderr, "curses headers unavailable\n");
	return 77;
#endif
}
