/* SyncTERM console startup policy. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_console_startup.h"

#include "io_error.h"
#include "st_command_line.h"
#include "st_profile.h"
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

struct video_mode {
	uint16_t columns;
	uint16_t rows;
	uint8_t cursor_start;
	uint8_t cursor_end;
	uint8_t cell_width;
	uint8_t cell_height;
	uint16_t pixel_width;
	uint16_t pixel_height;
	uint16_t aspect_width;
	uint16_t aspect_height;
	enum st_console_blink_mode blink_mode;
};

#define VIDEO_MODE(columns, rows, cursor_start, cursor_end, cell_width, \
	cell_height, pixel_width, pixel_height, aspect_width, aspect_height, \
	blink_mode) \
	{columns, rows, cursor_start, cursor_end, cell_width, cell_height, \
	    pixel_width, pixel_height, aspect_width, aspect_height, blink_mode}
#define PC_MODE(columns, rows, cursor_start, cursor_end, cell_width, \
	cell_height, pixel_width, pixel_height, aspect_width, aspect_height) \
	VIDEO_MODE(columns, rows, cursor_start, cursor_end, cell_width, \
	    cell_height, pixel_width, pixel_height, aspect_width, aspect_height, \
	    ST_CONSOLE_BLINK_PC)

static const struct video_mode modes[] = {
	[ST_SCREEN_MODE_CURRENT] = PC_MODE(80, 25, 14, 15, 8, 16, 640, 400, 4, 3),
	[ST_SCREEN_MODE_80X25] = PC_MODE(80, 25, 14, 15, 8, 16, 640, 400, 4, 3),
	[ST_SCREEN_MODE_LCD_80X25] = PC_MODE(80, 25, 14, 15, 8, 16, 640, 400, 8, 5),
	[ST_SCREEN_MODE_80X28] = PC_MODE(80, 28, 12, 13, 8, 14, 640, 392, 4, 3),
	[ST_SCREEN_MODE_80X30] = PC_MODE(80, 30, 14, 15, 8, 16, 640, 480, 4, 3),
	[ST_SCREEN_MODE_80X43] = PC_MODE(80, 43, 7, 7, 8, 8, 640, 350, 4, 3),
	[ST_SCREEN_MODE_80X50] = PC_MODE(80, 50, 7, 7, 8, 8, 640, 400, 4, 3),
	[ST_SCREEN_MODE_80X60] = PC_MODE(80, 60, 7, 7, 8, 8, 640, 480, 4, 3),
	[ST_SCREEN_MODE_132X37] = PC_MODE(132, 37, 14, 15, 8, 16, 1056, 592, 16, 9),
	[ST_SCREEN_MODE_132X52] = PC_MODE(132, 52, 14, 15, 8, 16, 1056, 832, 5, 4),
	[ST_SCREEN_MODE_132X25] = PC_MODE(132, 25, 14, 15, 8, 16, 1056, 400, 4, 3),
	[ST_SCREEN_MODE_132X28] = PC_MODE(132, 28, 12, 13, 8, 14, 1056, 392, 4, 3),
	[ST_SCREEN_MODE_132X30] = PC_MODE(132, 30, 14, 15, 8, 16, 1056, 480, 4, 3),
	[ST_SCREEN_MODE_132X34] = PC_MODE(132, 34, 12, 13, 8, 14, 1056, 476, 4, 3),
	[ST_SCREEN_MODE_132X43] = PC_MODE(132, 43, 7, 7, 8, 8, 1056, 350, 4, 3),
	[ST_SCREEN_MODE_132X50] = PC_MODE(132, 50, 7, 7, 8, 8, 1056, 400, 4, 3),
	[ST_SCREEN_MODE_132X60] = PC_MODE(132, 60, 7, 7, 8, 8, 1056, 480, 4, 3),
	[ST_SCREEN_MODE_C64] = VIDEO_MODE(40, 25, 0, 7, 8, 8, 320, 200,
	    6, 5, ST_CONSOLE_BLINK_COMMODORE),
	[ST_SCREEN_MODE_C128_40] = VIDEO_MODE(40, 25, 0, 7, 8, 8, 320, 200,
	    6, 5, ST_CONSOLE_BLINK_COMMODORE),
	[ST_SCREEN_MODE_C128_80] = VIDEO_MODE(80, 25, 0, 7, 8, 8, 640, 200,
	    4, 3, ST_CONSOLE_BLINK_COMMODORE),
	[ST_SCREEN_MODE_ATARI] = VIDEO_MODE(40, 24, 0, 7, 8, 8, 320, 192,
	    4, 3, ST_CONSOLE_BLINK_ATARI),
	[ST_SCREEN_MODE_ATARI_XEP80] = VIDEO_MODE(80, 25, 0, 15, 8, 16,
	    640, 400, 4, 3, ST_CONSOLE_BLINK_ATARI),
	[ST_SCREEN_MODE_EGA_80X25] = PC_MODE(80, 25, 12, 13, 8, 14, 640, 350, 4, 3),
	[ST_SCREEN_MODE_VGA_80X25] = PC_MODE(80, 25, 14, 15, 9, 16, 720, 400, 4, 3),
	[ST_SCREEN_MODE_PRESTEL] = VIDEO_MODE(40, 25, 14, 15, 12, 20,
	    480, 500, 3077, 2400, ST_CONSOLE_BLINK_PRESTEL),
	[ST_SCREEN_MODE_BEEB] = VIDEO_MODE(40, 25, 14, 15, 12, 20,
	    480, 500, 3077, 2400, ST_CONSOLE_BLINK_PRESTEL),
	[ST_SCREEN_MODE_ATARIST_40X25] = VIDEO_MODE(40, 25, 0, 7, 8, 8,
	    320, 200, 4, 3, ST_CONSOLE_BLINK_ATARI_ST),
	[ST_SCREEN_MODE_ATARIST_80X25] = VIDEO_MODE(80, 25, 0, 7, 8, 8,
	    640, 200, 4, 3, ST_CONSOLE_BLINK_ATARI_ST),
	[ST_SCREEN_MODE_ATARIST_80X25_MONO] = VIDEO_MODE(80, 25, 0, 15, 8,
	    16, 640, 400, 4, 3, ST_CONSOLE_BLINK_ATARI_ST),
};

static void
apply_mode(struct st_console_config *config, const struct video_mode *mode)
{
	config->columns = mode->columns;
	config->rows = mode->rows;
	config->cell_width = mode->cell_width;
	config->cell_height = mode->cell_height;
	config->pixel_width = mode->pixel_width;
	config->pixel_height = mode->pixel_height;
	config->aspect_width = mode->aspect_width;
	config->aspect_height = mode->aspect_height;
	config->blink_mode = mode->blink_mode;
	config->default_cursor = (struct st_console_custom_cursor){
		.start = mode->cursor_start,
		.end = mode->cursor_end,
		.range = mode->cell_height,
		.visible = true,
		.blink = true,
	};
}

static void
screen_mode(struct st_console_config *config,
	const struct st_settings *settings)
{
	int mode = settings->startup_mode;

	if (mode == ST_SCREEN_MODE_CUSTOM) {
		config->columns = (uint32_t)settings->custom_columns;
		config->rows = (uint32_t)settings->custom_rows;
		config->cell_width = 8;
		config->cell_height = (uint32_t)settings->custom_font_height;
		config->aspect_width = (uint32_t)settings->custom_aspect_width;
		config->aspect_height = (uint32_t)settings->custom_aspect_height;
		config->default_cursor = (struct st_console_custom_cursor){
			.start = config->cell_height
			    - (config->cell_height > 8 ? 2U : 1U),
			.end = config->cell_height - 1,
			.range = config->cell_height,
			.visible = true,
			.blink = true,
		};
	}
	else if (mode >= 0 && (size_t)mode < sizeof(modes) / sizeof(modes[0])
	    && modes[mode].columns != 0)
		apply_mode(config, &modes[mode]);
	else
		apply_mode(config, &modes[ST_SCREEN_MODE_CURRENT]);
}

static const struct video_mode *
line_mode(uint32_t rows)
{
	static const struct video_mode lines[] = {
		PC_MODE(80, 14, 14, 15, 8, 16, 640, 224, 4, 3),
		PC_MODE(80, 21, 14, 15, 8, 16, 640, 336, 4, 3),
		PC_MODE(80, 25, 14, 15, 8, 16, 640, 400, 4, 3),
		PC_MODE(80, 28, 12, 13, 8, 14, 640, 392, 4, 3),
		PC_MODE(80, 30, 14, 15, 8, 16, 640, 480, 4, 3),
		PC_MODE(80, 43, 7, 7, 8, 8, 640, 350, 4, 3),
		PC_MODE(80, 50, 7, 7, 8, 8, 640, 400, 4, 3),
		PC_MODE(80, 60, 7, 7, 8, 8, 640, 480, 4, 3),
	};

	for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
		if (lines[i].rows == rows)
			return &lines[i];
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
	const struct video_mode *lines;

	if (result == NULL || settings == NULL || command == NULL
	    || settings->custom_columns <= 0 || settings->custom_rows <= 0
	    || settings->custom_font_height <= 0)
		return IO_ERROR_INVALID_ARGUMENT;
	screen_mode(&startup.config, settings);
	startup.config.wrap = true;
	startup.config.scaling_mode = settings->external_scaling
	    ? ST_CONSOLE_SCALING_EXTERNAL : ST_CONSOLE_SCALING_INTERNAL;
	startup.config.scaling_policy = settings->blocky_scaling
	    ? ST_PIXEL_SCALING_BLOCKY : ST_PIXEL_SCALING_POINTY;
	startup.config.initial_scaling = settings->scaling_factor;
	if (settings->window_width > 0 && settings->window_height > 0) {
		startup.config.initial_window_width
		    = (uint32_t)settings->window_width;
		startup.config.initial_window_height
		    = (uint32_t)settings->window_height;
	}
	configured_backend(settings->output_mode, &startup.config.backend,
	    &startup.config.text_presentation, &startup.fullscreen);
	if (command->backend_set) {
		startup.config.backend = command->backend;
		startup.config.text_presentation = command->text_presentation;
		startup.fullscreen = command->fullscreen;
	}
	if (command->rows_set) {
		lines = line_mode(command->rows);
		if (lines == NULL)
			return IO_ERROR_INVALID_ARGUMENT;
		apply_mode(&startup.config, lines);
	}
	configure_default_cursor(&startup.config, settings->default_cursor);
	*result = startup;
	return 0;
}
