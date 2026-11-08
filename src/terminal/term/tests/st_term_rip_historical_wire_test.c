/* Check an unchanged RIP stream through the connection and Unicode boundary. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_term.h"
#include "st_pixels.h"

#include "../../cterm/tests/data/v110a_surfer1_expected.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RIP_SURFER1_PATH
#error RIP_SURFER1_PATH must name the unmodified corpus stream
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		return EXIT_FAILURE; \
	} \
} while (0)

static int
base36(char digit)
{
	if (digit >= '0' && digit <= '9')
		return digit - '0';
	if (digit >= 'A' && digit <= 'Z')
		return digit - 'A' + 10;
	return -1;
}

static int
check_pixels(st_console_t *console)
{
	const struct st_console_rect full = {0, 0, 640, 350};
	struct st_pixel_buffer *pixels = NULL;
	uint32_t palette[256];
	size_t position = 0, differences = 0;

	CHECK(st_console_get_pixels(&pixels, console, &full, false) == 0);
	CHECK(st_console_get_palette(console, 0, palette, 256) == 0);
	for (size_t chunk = 0; chunk < sizeof(v110a_surfer1_runs)
	    / sizeof(v110a_surfer1_runs[0]); chunk++) {
		const char *encoded = v110a_surfer1_runs[chunk];
		size_t length = strlen(encoded);

		CHECK(length % 5 == 0);
		for (size_t offset = 0; offset < length; offset += 5) {
			int count = 0;
			int colour = base36(encoded[offset + 4]);

			for (size_t i = 0; i < 4; i++) {
				int digit = base36(encoded[offset + i]);

				CHECK(digit >= 0);
				count = count * 36 + digit;
			}
			CHECK(count > 0 && colour >= 0
			    && (size_t)colour < sizeof(v110a_surfer1_colours)
			    / sizeof(v110a_surfer1_colours[0]));
			for (int i = 0; i < count; i++) {
				uint32_t actual;
				size_t x, y;

				CHECK(position < 640u * 350u);
				actual = palette[pixels->primary[position]];
				x = position % 640;
				y = position / 640;
				if (actual != v110a_surfer1_colours[colour]) {
					if (differences < 20)
						fprintf(stderr,
						    "(%zu,%zu): expected %#x, got %#x\n",
						    x, y, v110a_surfer1_colours[colour],
						    actual);
					differences++;
				}
				position++;
			}
		}
	}
	if (differences != 0)
		fprintf(stderr, "%zu RIP pixels differ\n", differences);
	CHECK(position == 640u * 350u);
	CHECK(differences == 0);
	st_pixel_buffer_destroy(pixels);
	return 0;
}

int
st_term_rip_historical_wire_test_main(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config screen = {
		.columns = 80, .rows = 25,
		.cell_width = 8, .cell_height = 14,
		.pixel_width = 640, .pixel_height = 350,
	};
	const struct st_pixel_blit initial_blit = {
		.width = 640, .height = 350,
		.scale_x = 1, .scale_y = 1,
	};
	struct st_profile profile;
	st_console_t *console = NULL;
	st_connection_t *connection = NULL;
	st_font_catalog_t *fonts = NULL;
	st_term_session_t *session = NULL;
	struct st_pixel_buffer *initial = NULL;
	enum st_term_disposition disposition;
	bool finished = false;

	CHECK(st_console_create(&console, &display, &screen) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	CHECK(st_pixel_buffer_create(&initial, 640, 350, false) == 0);
	CHECK(st_pixel_buffer_fill(initial, 4, 0) == 0);
	CHECK(st_console_blit_pixels(console, initial, NULL,
	    &initial_blit) == 0);
	st_pixel_buffer_destroy(initial);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_SHELL;
	profile.graphics_mode = ST_GRAPHICS_RIP_1;
	CHECK(setenv("ST_RIP_TEST_SOURCE", RIP_SURFER1_PATH, 1) == 0);
	strcpy(profile.address,
	    "stty raw -echo; cat \"$ST_RIP_TEST_SOURCE\"");
	CHECK(st_term_connect_profile(&connection, &profile, console, NULL, NULL)
	    == 0);
	const struct st_term_config config = {
		.console = console,
		.connection = connection,
		.fonts = fonts,
		.screen_mode = ST_SCREEN_MODE_80X25,
		.encoding = ST_CTERM_ENCODING_CODEPAGE,
		.graphics_mode = ST_CTERM_GRAPHICS_RIP_1,
		.codepage = ST_CODEPAGE_CP437,
		.hide_status = true,
	};
	CHECK(st_term_session_create(&session, &config) == 0);
	for (size_t attempt = 0; attempt < 300 && !finished; attempt++)
		CHECK(st_term_session_pump(session, &disposition, &finished) == 0);
	CHECK(finished);
	st_term_session_destroy(&session);
	CHECK(check_pixels(console) == 0);
	CHECK(st_connection_close(connection) == 0);
	st_profile_dispose(&profile);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	return 0;
}
