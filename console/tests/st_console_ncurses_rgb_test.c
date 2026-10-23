/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Exercise colour mapping without opening a terminal or loading curses. */
#include <stddef.h>

#include "console_config.h"

#if ST_CONSOLE_HAVE_CURSES_H && defined(ST_CONSOLE_TEST_OPAQUE_WINDOW)
#include <curses.h>
/* Exercise opaque windows even when the host headers expose their layout. */
#define WINDOW struct st_test_opaque_window
#endif

#include "../console_ncurses.c"

#if ST_CONSOLE_HAVE_CURSES_H
static unsigned resize_requests;
static bool window_deleted;
/* Only the fake API receives this aligned token; no WINDOW data is accessed. */
static max_align_t window_token;

static int
notify_resize(int rows, int columns)
{
	(void)rows;
	(void)columns;
	++resize_requests;
	return OK;
}

static WINDOW *
dimension_window(int rows, int columns, int y, int x)
{
	(void)rows;
	(void)columns;
	(void)y;
	(void)x;
	window_deleted = false;
	return (WINDOW *)(void *)&window_token;
}

static int dimension_columns(WINDOW *window) { (void)window; return 101; }
static int dimension_rows(WINDOW *window) { (void)window; return 43; }
static int
dimension_delete(WINDOW *window)
{
	if ((void *)window != &window_token)
		return ERR;
	window_deleted = true;
	return OK;
}
#endif

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
	uint32_t columns = 0, rows = 0;

	state.api.call_newwin = dimension_window;
	state.api.call_delwin = dimension_delete;
	state.api.call_getmaxx = dimension_columns;
	state.api.call_getmaxy = dimension_rows;
	state.api.call_resizeterm = notify_resize;
	/* NetBSD resizeterm() queues KEY_RESIZE even for an unchanged size.
	 * Reading dimensions in response must not trigger the next resize. */
	if (!terminal_dimensions(&state, &columns, &rows)
	    || columns != 101 || rows != 43 || resize_requests != 0
	    || !window_deleted)
		return EXIT_FAILURE;

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
