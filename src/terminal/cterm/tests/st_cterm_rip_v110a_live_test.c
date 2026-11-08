/* Byte-exact SyncTERM v1.10a RIP framebuffer regression. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_cterm.h"
#include "st_pixels.h"

#include "data/v110a_gl_morfr_expected.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef RIP_V110A_GLMORFR_PATH
#error RIP_V110A_GLMORFR_PATH must name the unmodified corpus stream
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		return 1; \
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

int
st_cterm_rip_v110a_live_test_main(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config screen = {
		.columns = 80, .rows = 25,
		.cell_width = 8, .cell_height = 14,
		.pixel_width = 640, .pixel_height = 350,
	};
	const struct st_console_rect full = {0, 0, 640, 350};
	const struct st_pixel_blit initial_blit = {
		.width = 640, .height = 350,
		.scale_x = 1, .scale_y = 1,
	};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	struct st_pixel_buffer *initial = NULL;
	struct st_pixel_buffer *pixels = NULL;
	uint32_t palette[256];
	uint8_t bytes[4096];
	size_t position = 0;
	size_t differences = 0;
	FILE *stream;

	CHECK(st_console_create(&console, &display, &screen) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config config = {
		.console = console,
		.fonts = fonts,
		.emulation = ST_CTERM_ANSI_BBS,
		.encoding = ST_CTERM_ENCODING_CODEPAGE,
		.graphics_mode = ST_CTERM_GRAPHICS_RIP_1,
		.codepage = ST_CODEPAGE_CP437,
	};
	CHECK(st_cterm_create(&terminal, &config) == 0);
	CHECK(st_pixel_buffer_create(&initial, 640, 350, false) == 0);
	CHECK(st_pixel_buffer_fill(initial, 4, 0) == 0);
	CHECK(st_console_blit_pixels(console, initial, NULL,
	    &initial_blit) == 0);
	st_pixel_buffer_destroy(initial);
	stream = fopen(RIP_V110A_GLMORFR_PATH, "rb");
	CHECK(stream != NULL);
	for (;;) {
		size_t size = fread(bytes, 1, sizeof(bytes), stream);
		size_t consumed = 0;

		if (size == 0)
			break;
		CHECK(st_cterm_write(terminal, bytes, size, &consumed) == 0);
		CHECK(consumed == size);
	}
	CHECK(ferror(stream) == 0);
	CHECK(fclose(stream) == 0);
	/* v1.10a's socket remains live at capture; do not flush EOF. */
	CHECK(st_console_get_pixels(&pixels, console, &full, false) == 0);
	CHECK(st_console_get_palette(console, 0, palette, 256) == 0);
	for (size_t chunk = 0; chunk < sizeof(v110a_gl_morfr_runs)
	    / sizeof(v110a_gl_morfr_runs[0]); chunk++) {
		const char *encoded = v110a_gl_morfr_runs[chunk];
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
			    && (size_t)colour < sizeof(v110a_gl_morfr_colours)
			    / sizeof(v110a_gl_morfr_colours[0]));
			for (int i = 0; i < count; i++) {
				uint32_t actual;

				CHECK(position < 640u * 350u);
				actual = palette[pixels->primary[position]];
				if (actual != v110a_gl_morfr_colours[colour]) {
					if (differences < 20)
						fprintf(stderr,
						    "(%zu,%zu): expected %#x, got %#x\n",
						    position % 640, position / 640,
						    v110a_gl_morfr_colours[colour], actual);
					differences++;
				}
				position++;
			}
		}
	}
	CHECK(position == 640u * 350u);
	if (differences != 0)
		fprintf(stderr, "%zu RIP pixels differ\n", differences);
	CHECK(differences == 0);
	st_pixel_buffer_destroy(pixels);
	st_cterm_destroy(&terminal);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	return 0;
}
