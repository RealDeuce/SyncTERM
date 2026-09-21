/* Screen-mode contract tests. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_screen_mode.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, \
		    #expression); \
		return 1; \
	} \
} while (0)

int
main(void)
{
	const struct st_screen_mode_descriptor *mode;
	struct st_screen_mode_descriptor custom;
	uint32_t console_palette[16];

	CHECK(st_screen_mode_count() == ST_SCREEN_MODE_ATARIST_80X25_MONO + 1U);
	for (size_t i = 0; i < st_screen_mode_count(); ++i) {
		mode = st_screen_mode_at(i);
		CHECK(mode != NULL);
		CHECK(mode->mode == (enum st_screen_mode)i);
		CHECK(mode->name != NULL && mode->ini_name != NULL);
	}
	CHECK(st_screen_mode_at(st_screen_mode_count()) == NULL);
	CHECK(st_screen_mode_get((enum st_screen_mode)-1) == NULL);

	mode = st_screen_mode_get(ST_SCREEN_MODE_ATARI_XEP80);
	CHECK(mode->console.columns == 80 && mode->console.rows == 25);
	CHECK(mode->palette == ST_SCREEN_PALETTE_GREYSCALE);
	CHECK(mode->palette_count == 4);
	mode = st_screen_mode_get(ST_SCREEN_MODE_PRESTEL);
	CHECK(mode->console.columns == 40 && mode->console.rows == 25);
	CHECK(mode->console.cell_width == 12 && mode->console.cell_height == 20);
	CHECK(mode->font_selector == 43 && mode->palette_count == 8);
	CHECK(mode->palette_rgb[1] == 0x0000ff);
	mode = st_screen_mode_get(ST_SCREEN_MODE_C64);
	CHECK(mode->default_attribute == 0x6e && mode->font_selector == 33);
	CHECK(mode->palette_rgb[1] == 0xffffff);
	st_screen_mode_console_palette(mode, console_palette);
	CHECK(console_palette[4] == 0xffffff);
	mode = st_screen_mode_get(ST_SCREEN_MODE_ATARIST_80X25_MONO);
	CHECK(mode->palette_count == 2);
	CHECK(mode->palette_rgb[0] == 0xffffff);
	CHECK(mode->palette_rgb[1] == 0x000000);

	CHECK(st_screen_mode_custom(&custom, 100, 40, 14, 4, 3) == 0);
	CHECK(custom.mode == ST_SCREEN_MODE_CUSTOM);
	CHECK(custom.console.columns == 100 && custom.console.rows == 40);
	CHECK(custom.console.cell_width == 8 && custom.console.cell_height == 14);
	CHECK(custom.console.pixel_width == 0 && custom.console.pixel_height == 0);
	CHECK(custom.console.default_cursor.start == 12);
	CHECK(custom.console.default_cursor.end == 13);
	CHECK(strcmp(custom.name, "Custom") == 0);

	return 0;
}
