/* SyncTERM command-line and console-startup policy tests. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_command_line.h"
#include "st_console_startup.h"
#include "st_app_state.h"
#include "st_settings.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, \
		    __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static int
test_command_line(void)
{
	char *arguments[] = {
		"syncterm", "-6", "-b/tmp/list.ini", "-N", "/tmp/st.ini",
		"-c", "-isf", "-l43", "-r", "-q", "-s", "-Wstartup.wren",
		"example.com",
	};
	struct st_command_line command;
	const char *invalid = NULL;
	char *terminfo[] = {"syncterm", "-T"};
	char *telnet[] = {"syncterm", "-T", "example.com"};
	char *bad[] = {"syncterm", "-isx"};
	char *curses[] = {"syncterm", "-ic"};
	char *curses437[] = {"syncterm", "-if"};
	char *curses_ascii[] = {"syncterm", "-ii"};
	char *ansi[] = {"syncterm", "-ia"};
	char *ansi_utf8[] = {"syncterm", "-iau"};
	char *ansi_cp437[] = {"syncterm", "-iac"};

	CHECK(st_command_line_parse(&command,
	    (int)(sizeof(arguments) / sizeof(arguments[0])), arguments,
	    &invalid) == 0);
	CHECK(invalid == NULL);
	CHECK(command.action == ST_COMMAND_RUN);
	CHECK(command.address_family == ST_ADDRESS_FAMILY_IPV6);
	CHECK(strcmp(command.list_path, "/tmp/list.ini") == 0);
	CHECK(strcmp(command.settings_path, "/tmp/st.ini") == 0);
	CHECK(command.hide_status && command.hide_popups && command.safe_mode);
	CHECK(command.backend_set && command.backend == ST_CONSOLE_BACKEND_SDL);
	CHECK(command.fullscreen && command.rows_set && command.rows == 43);
	CHECK(command.connection_type_set
	    && command.connection_type == ST_CONNECTION_RLOGIN);
	CHECK(strcmp(command.wren_script, "startup.wren") == 0);
	CHECK(strcmp(command.url, "example.com") == 0);
	CHECK(st_command_line_parse(&command, 2, terminfo, NULL) == 0
	    && command.action == ST_COMMAND_TERMINFO);
	CHECK(st_command_line_parse(&command, 3, telnet, NULL) == 0
	    && command.action == ST_COMMAND_RUN && command.connection_type_set
	    && command.connection_type == ST_CONNECTION_TELNET);
	CHECK(st_command_line_parse(&command, 2, bad, &invalid) != 0);
	CHECK(invalid == bad[1]);
	CHECK(st_command_line_parse(&command, 2, curses, NULL) == 0
	    && command.backend == ST_CONSOLE_BACKEND_NCURSES
	    && command.text_presentation == ST_CONSOLE_TEXT_UNICODE);
	CHECK(st_command_line_parse(&command, 2, curses437, NULL) == 0
	    && command.backend == ST_CONSOLE_BACKEND_NCURSES
	    && command.text_presentation == ST_CONSOLE_TEXT_CP437);
	CHECK(st_command_line_parse(&command, 2, curses_ascii, NULL) == 0
	    && command.backend == ST_CONSOLE_BACKEND_NCURSES
	    && command.text_presentation == ST_CONSOLE_TEXT_ASCII);
	CHECK(st_command_line_parse(&command, 2, ansi, NULL) == 0
	    && command.backend == ST_CONSOLE_BACKEND_ANSI
	    && command.text_presentation == ST_CONSOLE_TEXT_UNICODE);
	CHECK(st_command_line_parse(&command, 2, ansi_utf8, NULL) == 0
	    && command.backend == ST_CONSOLE_BACKEND_ANSI
	    && command.text_presentation == ST_CONSOLE_TEXT_UNICODE);
	CHECK(st_command_line_parse(&command, 2, ansi_cp437, NULL) == 0
	    && command.backend == ST_CONSOLE_BACKEND_ANSI
	    && command.text_presentation == ST_CONSOLE_TEXT_CP437);
	return 0;
}

static int
test_application_state(void)
{
	CHECK(st_app_should_persist_scaling(true, false, ST_SCREEN_MODE_80X25,
	    ST_SCREEN_MODE_80X25));
	CHECK(!st_app_should_persist_scaling(false, false, ST_SCREEN_MODE_80X25,
	    ST_SCREEN_MODE_80X25));
	CHECK(!st_app_should_persist_scaling(true, true, ST_SCREEN_MODE_80X25,
	    ST_SCREEN_MODE_80X25));
	CHECK(!st_app_should_persist_scaling(true, false, ST_SCREEN_MODE_80X25,
	    ST_SCREEN_MODE_80X43));
	CHECK(st_app_should_persist_scaling(true, false, ST_SCREEN_MODE_CURRENT,
	    ST_SCREEN_MODE_CURRENT));
	return 0;
}

static int
test_console_startup(void)
{
	struct st_settings settings;
	struct st_command_line command = {0};
	struct st_console_startup startup;

	st_settings_init(&settings);
	settings.startup_mode = ST_SCREEN_MODE_80X28;
	settings.output_mode = 10;
	settings.custom_columns = 80;
	settings.custom_rows = 25;
	settings.custom_font_height = 16;
	settings.custom_aspect_width = 4;
	settings.custom_aspect_height = 3;
	settings.blocky_scaling = true;
	settings.color_emoji = true;
	settings.scaling_factor = 2.0;
	CHECK(st_console_startup_configure(&startup, &settings, &command) == 0);
	CHECK(startup.config.columns == 80 && startup.config.rows == 28);
	CHECK(startup.config.cell_width == 8 && startup.config.cell_height == 14);
	CHECK(startup.config.default_cursor.start == 12
	    && startup.config.default_cursor.end == 13
	    && startup.config.default_cursor.range == 14
	    && startup.config.default_cursor.blink);
	CHECK(startup.config.pixel_width == 640
	    && startup.config.pixel_height == 392);
	CHECK(startup.config.aspect_width == 4
	    && startup.config.aspect_height == 3);
	CHECK(startup.display_config.backend == ST_CONSOLE_BACKEND_SDL);
	CHECK(startup.fullscreen);
	CHECK(startup.config.scaling_policy == ST_PIXEL_SCALING_BLOCKY);
	CHECK(startup.config.color_emoji);
	settings.color_emoji = false;
	CHECK(startup.display_config.initial_scaling == 2.0);
	CHECK(!startup.display_config.text_left_justify);
	CHECK(startup.display_config.text_margin_colour
	    == ST_CONSOLE_COLOR_RGB_VALUE(0, 0, 168));
	settings.left_justify = true;
	CHECK(st_console_startup_configure(&startup, &settings, &command) == 0);
	CHECK(startup.display_config.text_left_justify);
	CHECK(startup.display_config.text_margin_colour
	    == ST_CONSOLE_COLOR_RGB_VALUE(0, 0, 0));
	settings.left_justify = false;
	settings.window_width = 800;
	settings.window_height = 600;
	CHECK(st_console_startup_configure(&startup, &settings, &command) == 0);
	CHECK(startup.display_config.initial_window_width == 800);
	CHECK(!startup.config.color_emoji);
	CHECK(startup.display_config.initial_window_height == 600);
	settings.window_width = 0;
	settings.window_height = 0;
	settings.output_mode = 1;
	CHECK(st_console_startup_configure(&startup, &settings, &command) == 0);
	CHECK(startup.display_config.backend == ST_CONSOLE_BACKEND_NCURSES
	    && startup.display_config.text_presentation == ST_CONSOLE_TEXT_UNICODE);
	settings.output_mode = 2;
	CHECK(st_console_startup_configure(&startup, &settings, &command) == 0);
	CHECK(startup.display_config.text_presentation == ST_CONSOLE_TEXT_CP437);
	settings.output_mode = 3;
	CHECK(st_console_startup_configure(&startup, &settings, &command) == 0);
	CHECK(startup.display_config.text_presentation == ST_CONSOLE_TEXT_ASCII);
	settings.output_mode = 10;

	command.backend_set = true;
	command.backend = ST_CONSOLE_BACKEND_ANSI;
	command.rows_set = true;
	command.rows = 60;
	CHECK(st_console_startup_configure(&startup, &settings, &command) == 0);
	CHECK(startup.display_config.backend == ST_CONSOLE_BACKEND_ANSI);
	CHECK(!startup.fullscreen && startup.config.columns == 80
	    && startup.config.rows == 60);
	CHECK(startup.config.cell_width == 8 && startup.config.cell_height == 8);
	CHECK(startup.config.default_cursor.start == 7
	    && startup.config.default_cursor.end == 7
	    && startup.config.default_cursor.range == 8);
	CHECK(startup.config.pixel_width == 640
	    && startup.config.pixel_height == 480);
	command.rows_set = false;
	settings.startup_mode = ST_SCREEN_MODE_VGA_80X25;
	CHECK(st_console_startup_configure(&startup, &settings, &command) == 0);
	CHECK(startup.config.cell_width == 9 && startup.config.cell_height == 16);
	CHECK(startup.config.default_cursor.start == 14
	    && startup.config.default_cursor.end == 15);
	CHECK(startup.config.pixel_width == 720
	    && startup.config.pixel_height == 400);
	settings.startup_mode = ST_SCREEN_MODE_PRESTEL;
	CHECK(st_console_startup_configure(&startup, &settings, &command) == 0);
	CHECK(startup.config.columns == 40 && startup.config.rows == 25);
	CHECK(startup.config.cell_width == 12 && startup.config.cell_height == 20);
	CHECK(startup.config.pixel_width == 480
	    && startup.config.pixel_height == 500);
	CHECK(startup.config.aspect_width == 3077
	    && startup.config.aspect_height == 2400);
	CHECK(startup.config.blink_mode == ST_CONSOLE_BLINK_PRESTEL);
	CHECK(startup.config.default_cursor.start == 14
	    && startup.config.default_cursor.end == 15
	    && startup.config.default_cursor.range == 20);
	settings.default_cursor = ST_CURSOR_SOLID_UNDERLINE;
	CHECK(st_console_startup_configure(&startup, &settings, &command) == 0);
	CHECK(startup.config.default_cursor.start == 18
	    && startup.config.default_cursor.end == 19
	    && !startup.config.default_cursor.blink);
	settings.default_cursor = ST_CURSOR_BLINK_BLOCK;
	CHECK(st_console_startup_configure(&startup, &settings, &command) == 0);
	CHECK(startup.config.default_cursor.start == 0
	    && startup.config.default_cursor.end == 19
	    && startup.config.default_cursor.blink);
	st_settings_dispose(&settings);
	return 0;
}

static int
test_legacy_mode_geometry(void)
{
	/* Expected geometry from legacy conio/vidmodes.c, not the new table.
	 * CURRENT and CUSTOM are policies rather than fixed legacy modes.
	 */
	static const struct {
		int mode;
		uint32_t columns, rows, start, end, width, height;
		uint32_t pixels_x, pixels_y, aspect_x, aspect_y;
	} expected[] = {
		{ST_SCREEN_MODE_80X25, 80, 25, 14, 15, 8, 16, 640, 400, 4, 3},
		{ST_SCREEN_MODE_LCD_80X25, 80, 25, 14, 15, 8, 16, 640, 400, 8, 5},
		{ST_SCREEN_MODE_132X37, 132, 37, 14, 15, 8, 16, 1056, 592, 16, 9},
		{ST_SCREEN_MODE_132X52, 132, 52, 14, 15, 8, 16, 1056, 832, 5, 4},
		{ST_SCREEN_MODE_C64, 40, 25, 0, 7, 8, 8, 320, 200, 6, 5},
		{ST_SCREEN_MODE_C128_40, 40, 25, 0, 7, 8, 8, 320, 200, 6, 5},
		{ST_SCREEN_MODE_C128_80, 80, 25, 0, 7, 8, 8, 640, 200, 4, 3},
		{ST_SCREEN_MODE_ATARI, 40, 24, 0, 7, 8, 8, 320, 192, 4, 3},
		{ST_SCREEN_MODE_ATARI_XEP80, 80, 25, 0, 15, 8, 16, 640, 400, 4, 3},
		{ST_SCREEN_MODE_EGA_80X25, 80, 25, 12, 13, 8, 14, 640, 350, 4, 3},
		{ST_SCREEN_MODE_VGA_80X25, 80, 25, 14, 15, 9, 16, 720, 400, 4, 3},
		{ST_SCREEN_MODE_PRESTEL, 40, 25, 14, 15, 12, 20, 480, 500, 3077, 2400},
		{ST_SCREEN_MODE_BEEB, 40, 25, 14, 15, 12, 20, 480, 500, 3077, 2400},
		{ST_SCREEN_MODE_ATARIST_40X25, 40, 25, 0, 7, 8, 8, 320, 200, 4, 3},
		{ST_SCREEN_MODE_ATARIST_80X25, 80, 25, 0, 7, 8, 8, 640, 200, 4, 3},
		{ST_SCREEN_MODE_ATARIST_80X25_MONO, 80, 25, 0, 15, 8, 16, 640, 400, 4, 3},
		{ST_SCREEN_MODE_80X28, 80, 28, 12, 13, 8, 14, 640, 392, 4, 3},
		{ST_SCREEN_MODE_80X30, 80, 30, 14, 15, 8, 16, 640, 480, 4, 3},
		{ST_SCREEN_MODE_80X43, 80, 43, 7, 7, 8, 8, 640, 350, 4, 3},
		{ST_SCREEN_MODE_80X50, 80, 50, 7, 7, 8, 8, 640, 400, 4, 3},
		{ST_SCREEN_MODE_80X60, 80, 60, 7, 7, 8, 8, 640, 480, 4, 3},
		{ST_SCREEN_MODE_132X25, 132, 25, 14, 15, 8, 16, 1056, 400, 4, 3},
		{ST_SCREEN_MODE_132X28, 132, 28, 12, 13, 8, 14, 1056, 392, 4, 3},
		{ST_SCREEN_MODE_132X30, 132, 30, 14, 15, 8, 16, 1056, 480, 4, 3},
		{ST_SCREEN_MODE_132X34, 132, 34, 12, 13, 8, 14, 1056, 476, 4, 3},
		{ST_SCREEN_MODE_132X43, 132, 43, 7, 7, 8, 8, 1056, 350, 4, 3},
		{ST_SCREEN_MODE_132X50, 132, 50, 7, 7, 8, 8, 1056, 400, 4, 3},
		{ST_SCREEN_MODE_132X60, 132, 60, 7, 7, 8, 8, 1056, 480, 4, 3},
	};
	const uint32_t bright_background = ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND
	    | ST_CONSOLE_VIDEO_NO_BLINK;
	struct st_settings settings;
	struct st_command_line command = {0};
	struct st_console_startup startup;

	st_settings_init(&settings);
	settings.default_cursor = ST_CURSOR_DEFAULT;
	settings.custom_columns = 80;
	settings.custom_rows = 25;
	settings.custom_font_height = 16;
	for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
		settings.startup_mode = expected[i].mode;
		CHECK(st_console_startup_configure(&startup, &settings, &command) == 0);
		CHECK(startup.config.columns == expected[i].columns);
		CHECK(startup.config.rows == expected[i].rows);
		CHECK(startup.config.cell_width == expected[i].width);
		CHECK(startup.config.cell_height == expected[i].height);
		CHECK(startup.config.pixel_width == expected[i].pixels_x);
		CHECK(startup.config.pixel_height == expected[i].pixels_y);
		CHECK(startup.config.aspect_width == expected[i].aspect_x);
		CHECK(startup.config.aspect_height == expected[i].aspect_y);
		CHECK(startup.config.default_cursor.start == expected[i].start);
		CHECK(startup.config.default_cursor.end == expected[i].end);
		uint32_t expected_flags = 0;

		if (expected[i].mode == ST_SCREEN_MODE_C64
		    || expected[i].mode == ST_SCREEN_MODE_C128_40
		    || expected[i].mode == ST_SCREEN_MODE_C128_80
		    || expected[i].mode == ST_SCREEN_MODE_ATARIST_40X25
		    || expected[i].mode == ST_SCREEN_MODE_ATARIST_80X25
		    || expected[i].mode == ST_SCREEN_MODE_ATARIST_80X25_MONO)
			expected_flags = bright_background;
		else if (expected[i].mode == ST_SCREEN_MODE_VGA_80X25)
			expected_flags = ST_CONSOLE_VIDEO_EXPAND
			    | ST_CONSOLE_VIDEO_LINE_GRAPHICS_EXPAND;
		CHECK(startup.config.video_flags == expected_flags);
	}
	st_settings_dispose(&settings);
	return 0;
}

int
main(void)
{
	if (test_command_line() != 0)
		return 1;
	if (test_application_state() != 0)
		return 1;
	if (test_legacy_mode_geometry() != 0)
		return 1;
	return test_console_startup();
}
