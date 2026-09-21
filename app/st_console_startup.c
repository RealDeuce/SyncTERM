/* SyncTERM console startup policy. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_console_startup.h"

#include "io_error.h"
#include "st_command_line.h"
#include "st_profile.h"
#include "st_screen_mode.h"
#include "st_settings.h"

#include <limits.h>
#include <stdint.h>

enum output_mode {
	OUTPUT_AUTO,
	OUTPUT_CURSES,
	OUTPUT_CURSES_437,
	OUTPUT_CURSES_ASCII,
	OUTPUT_ANSI,
	OUTPUT_X11,
	OUTPUT_X11_FULLSCREEN,
	OUTPUT_WIN32,
	OUTPUT_WIN32_FULLSCREEN,
	OUTPUT_SDL,
	OUTPUT_SDL_FULLSCREEN,
	OUTPUT_GDI,
	OUTPUT_GDI_FULLSCREEN,
	OUTPUT_LIBRETRO,
	OUTPUT_WAYLAND,
	OUTPUT_WAYLAND_FULLSCREEN,
	OUTPUT_QUARTZ,
	OUTPUT_QUARTZ_FULLSCREEN,
};

static int
screen_mode(struct st_console_config *config,
	const struct st_settings *settings)
{
	struct st_screen_mode_descriptor custom;
	const struct st_screen_mode_descriptor *mode;

	if (settings->startup_mode == ST_SCREEN_MODE_CUSTOM) {
		if (st_screen_mode_custom(&custom,
		    (uint32_t)settings->custom_columns,
		    (uint32_t)settings->custom_rows,
		    (uint32_t)settings->custom_font_height,
		    (uint32_t)settings->custom_aspect_width,
		    (uint32_t)settings->custom_aspect_height) != 0)
			return IO_ERROR_INVALID_ARGUMENT;
		mode = &custom;
	}
	else {
		mode = st_screen_mode_get((enum st_screen_mode)settings->startup_mode);
		if (mode == NULL || mode->mode == ST_SCREEN_MODE_CUSTOM)
			mode = st_screen_mode_get(ST_SCREEN_MODE_CURRENT);
	}
	*config = mode->console;
	return 0;
}

static const struct st_console_config *
line_mode(uint32_t rows)
{
	static const struct st_console_config lines[] = {
		{.columns = 80, .rows = 14, .cell_width = 8, .cell_height = 16,
		    .pixel_width = 640, .pixel_height = 224,
		    .default_cursor = {14, 15, 16, true, true}, .wrap = true,
		    .blink_mode = ST_CONSOLE_BLINK_PC,
		    .scaling_policy = ST_PIXEL_SCALING_BLOCKY,
		    .aspect_width = 4, .aspect_height = 3},
		{.columns = 80, .rows = 21, .cell_width = 8, .cell_height = 16,
		    .pixel_width = 640, .pixel_height = 336,
		    .default_cursor = {14, 15, 16, true, true}, .wrap = true,
		    .blink_mode = ST_CONSOLE_BLINK_PC,
		    .scaling_policy = ST_PIXEL_SCALING_BLOCKY,
		    .aspect_width = 4, .aspect_height = 3},
	};
	static const enum st_screen_mode mapped[] = {
		ST_SCREEN_MODE_80X25, ST_SCREEN_MODE_80X28,
		ST_SCREEN_MODE_80X30, ST_SCREEN_MODE_80X43,
		ST_SCREEN_MODE_80X50, ST_SCREEN_MODE_80X60,
	};

	for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
		if (lines[i].rows == rows)
			return &lines[i];
	}
	for (size_t i = 0; i < sizeof(mapped) / sizeof(mapped[0]); ++i) {
		const struct st_screen_mode_descriptor *mode
		    = st_screen_mode_get(mapped[i]);

		if (mode->console.rows == rows)
			return &mode->console;
	}
	return NULL;
}

static void
configure_default_cursor(struct st_console_config *config, int cursor)
{
	uint32_t height = config->cell_height == 0 ? 1 : config->cell_height;

	switch (cursor) {
	case ST_CURSOR_BLINK_UNDERLINE:
	case ST_CURSOR_SOLID_UNDERLINE:
		config->default_cursor.start = height - (height > 8 ? 2U : 1U);
		config->default_cursor.end = height - 1;
		break;
	case ST_CURSOR_BLINK_BLOCK:
	case ST_CURSOR_SOLID_BLOCK:
		config->default_cursor.start = 0;
		config->default_cursor.end = height - 1;
		break;
	case ST_CURSOR_DEFAULT:
	default:
		break;
	}
	config->default_cursor.range = height;
	config->default_cursor.visible = true;
	config->default_cursor.blink = cursor != ST_CURSOR_SOLID_UNDERLINE
	    && cursor != ST_CURSOR_SOLID_BLOCK;
}

static void
configured_backend(int mode, enum st_console_backend *backend,
	enum st_console_text_presentation *text_presentation, bool *fullscreen)
{
	*fullscreen = false;
	*text_presentation = ST_CONSOLE_TEXT_UNICODE;
	switch (mode) {
	case OUTPUT_CURSES: *backend = ST_CONSOLE_BACKEND_NCURSES; break;
	case OUTPUT_CURSES_437:
		*backend = ST_CONSOLE_BACKEND_NCURSES;
		*text_presentation = ST_CONSOLE_TEXT_CP437;
		break;
	case OUTPUT_CURSES_ASCII:
		*backend = ST_CONSOLE_BACKEND_NCURSES;
		*text_presentation = ST_CONSOLE_TEXT_ASCII;
		break;
	case OUTPUT_ANSI: *backend = ST_CONSOLE_BACKEND_ANSI; break;
	case OUTPUT_X11: *backend = ST_CONSOLE_BACKEND_X11; break;
	case OUTPUT_X11_FULLSCREEN:
		*backend = ST_CONSOLE_BACKEND_X11; *fullscreen = true; break;
	case OUTPUT_WIN32: *backend = ST_CONSOLE_BACKEND_WIN32; break;
	case OUTPUT_WIN32_FULLSCREEN:
		*backend = ST_CONSOLE_BACKEND_WIN32; *fullscreen = true; break;
	case OUTPUT_SDL: *backend = ST_CONSOLE_BACKEND_SDL; break;
	case OUTPUT_SDL_FULLSCREEN:
		*backend = ST_CONSOLE_BACKEND_SDL; *fullscreen = true; break;
	case OUTPUT_GDI: *backend = ST_CONSOLE_BACKEND_GDI; break;
	case OUTPUT_GDI_FULLSCREEN:
		*backend = ST_CONSOLE_BACKEND_GDI; *fullscreen = true; break;
	case OUTPUT_LIBRETRO: *backend = ST_CONSOLE_BACKEND_LIBRETRO; break;
	case OUTPUT_WAYLAND: *backend = ST_CONSOLE_BACKEND_WAYLAND; break;
	case OUTPUT_WAYLAND_FULLSCREEN:
		*backend = ST_CONSOLE_BACKEND_WAYLAND; *fullscreen = true; break;
	case OUTPUT_QUARTZ: *backend = ST_CONSOLE_BACKEND_QUARTZ; break;
	case OUTPUT_QUARTZ_FULLSCREEN:
		*backend = ST_CONSOLE_BACKEND_QUARTZ; *fullscreen = true; break;
	default: *backend = ST_CONSOLE_BACKEND_AUTO; break;
	}
}

int
st_console_startup_configure(struct st_console_startup *result,
	const struct st_settings *settings, const struct st_command_line *command)
{
	struct st_console_startup startup = {0};
	const struct st_console_config *lines;

	if (result == NULL || settings == NULL || command == NULL
	    || settings->custom_columns <= 0 || settings->custom_rows <= 0
	    || settings->custom_font_height <= 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (screen_mode(&startup.config, settings) != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	startup.config.wrap = true;
	startup.config.scaling_mode = settings->external_scaling
	    ? ST_CONSOLE_SCALING_EXTERNAL : ST_CONSOLE_SCALING_INTERNAL;
	startup.config.scaling_policy = settings->blocky_scaling
	    ? ST_PIXEL_SCALING_BLOCKY : ST_PIXEL_SCALING_POINTY;
	startup.display_config.initial_scaling = settings->scaling_factor;
	if (settings->window_width > 0 && settings->window_height > 0) {
		startup.display_config.initial_window_width
		    = (uint32_t)settings->window_width;
		startup.display_config.initial_window_height
		    = (uint32_t)settings->window_height;
	}
	configured_backend(settings->output_mode, &startup.display_config.backend,
	    &startup.display_config.text_presentation, &startup.fullscreen);
	if (command->backend_set) {
		startup.display_config.backend = command->backend;
		startup.display_config.text_presentation = command->text_presentation;
		startup.fullscreen = command->fullscreen;
	}
	if (command->rows_set) {
		lines = line_mode(command->rows);
		if (lines == NULL)
			return IO_ERROR_INVALID_ARGUMENT;
		startup.config = *lines;
	}
	configure_default_cursor(&startup.config, settings->default_cursor);
	*result = startup;
	return 0;
}
