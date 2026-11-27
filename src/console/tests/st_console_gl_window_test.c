/* Exercise changing scalers on the same native window. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <string.h>

#include "io_error.h"
#include "st_console.h"

int
main(int argc, char **argv)
{
	const struct {
		const char             *name;
		enum st_console_backend backend;
	} backends[] = {
	    {"sdl",     ST_CONSOLE_BACKEND_SDL    },
            {"wayland", ST_CONSOLE_BACKEND_WAYLAND},
	    {"gdi",     ST_CONSOLE_BACKEND_GDI    },
            {"quartz",  ST_CONSOLE_BACKEND_QUARTZ }
        };
	struct st_console_display_config window = {0};
	for (size_t i = 0; argc == 2 && i < sizeof(backends) / sizeof(*backends); ++i)
		if (strcmp(argv[1], backends[i].name) == 0)
			window.backend = backends[i].backend;
	if (window.backend == ST_CONSOLE_BACKEND_AUTO)
		return 1;

	const struct st_console_config config  = {.columns = 8, .rows = 4, .cell_width = 8, .cell_height = 16};
	st_console_display_t          *display = NULL;
	st_console_t                  *console = NULL;
	int                            status  = st_console_display_create(&display, &window);
	if (!status)
		status = st_console_create_on_display(&console, display, &config);
	if (!status)
		status = st_console_present(console);
	if (!status)
		status = st_console_display_set_gl_preset(display, "Nearest", true);
	bool unavailable = status == IO_ERROR_NOT_SUPPORTED || status == IO_ERROR_NOT_FOUND;
	if (!status)
		status = st_console_set_scaling_mode(console, ST_CONSOLE_SCALING_OPENGL);
	if (!status)
		status = st_console_set_pixel(console, 4, 6, ST_CONSOLE_COLOR_RGB_VALUE(10, 60, 120));
	if (!status)
		status = st_console_present(console);
	if (!status)
		status = st_console_display_set_gl_preset(display, "Pointy", true);
	if (!status)
		status = st_console_resize(console, 10, 5);
	if (!status)
		status = st_console_set_scaling_mode(console, ST_CONSOLE_SCALING_INTERNAL);
	if (!status)
		status = st_console_present(console);
	if (console)
		st_console_destroy(console);
	if (display)
		st_console_display_destroy(display);
	if (status)
		fprintf(stderr, "OpenGL window test returned %d\n", status);
	return unavailable ? 77 : status != 0;
}
