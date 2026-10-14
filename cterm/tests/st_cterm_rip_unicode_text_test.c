/* UTF-8 RIP text must select the same historical font bytes as CP437. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_cterm.h"
#include "st_pixels.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static int
feed(st_cterm_t *terminal, const char *wire)
{
	size_t size = strlen(wire);
	size_t consumed;
	int status = st_cterm_write(terminal, wire, size, &consumed);

	return status == 0 && consumed == size ? 0 : 1;
}

int
main(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config screen = {
		.columns = 80, .rows = 43,
		.cell_width = 8, .cell_height = 8,
		.pixel_width = 640, .pixel_height = 350,
	};
	const struct st_console_rect full = {0, 0, 640, 350};
	st_console_t *console[2] = {NULL, NULL};
	st_font_catalog_t *fonts[2] = {NULL, NULL};
	st_cterm_t *terminal[2] = {NULL, NULL};
	struct st_pixel_buffer *pixels[2] = {NULL, NULL};
	uint32_t palette[2][256];
	size_t painted = 0;

	for (size_t i = 0; i < 2; i++) {
		CHECK(st_console_create(&console[i], &display, &screen) == 0);
		CHECK(st_font_catalog_create(&fonts[i], console[i]) == 0);
		const struct st_cterm_config config = {
			.console = console[i],
			.fonts = fonts[i],
			.emulation = ST_CTERM_ANSI_BBS,
			.encoding = i == 0 ? ST_CTERM_ENCODING_CODEPAGE
			    : ST_CTERM_ENCODING_UTF8,
			.rip_version = ST_CTERM_RIP_1,
			.codepage = ST_CODEPAGE_CP437,
		};
		CHECK(st_cterm_create(&terminal[i], &config) == 0);
		CHECK(feed(terminal[i], "!|c0F\r\n") == 0);
	}
	CHECK(feed(terminal[0], "!|@1E1E\x82\r\n") == 0);
	CHECK(feed(terminal[1], "!|@1E1E\xc3\xa9\r\n") == 0);
	CHECK(feed(terminal[0],
	    "!|1B140A0200ZK0300080F080700000707000000"
	    "|1U1O1E00000000<>E\x82<>^M\r\n") == 0);
	CHECK(feed(terminal[1],
	    "!|1B140A0200ZK0300080F080700000707000000"
	    "|1U1O1E00000000<>E\xc3\xa9<>^M\r\n") == 0);
	/* A non-ASCII character must stay whole when it breaks a pending
	 * ANSI escape with RIP text disabled, even when its UTF-8 bytes
	 * arrive in separate writes. */
	CHECK(feed(terminal[0], "!|w0000000000\r\n") == 0);
	CHECK(feed(terminal[1], "!|w0000000000\r\n") == 0);
	CHECK(feed(terminal[0], "\x1b\x86") == 0);
	CHECK(feed(terminal[1], "\x1b\xc3") == 0);
	CHECK(feed(terminal[1], "\xa5") == 0);
	for (size_t i = 0; i < 2; i++) {
		CHECK(st_console_get_pixels(&pixels[i], console[i], &full,
		    false) == 0);
		CHECK(st_console_get_palette(console[i], 0, palette[i], 256) == 0);
	}
	for (size_t i = 0; i < 640u * 350u; i++) {
		uint32_t left = palette[0][pixels[0]->primary[i]];
		uint32_t right = palette[1][pixels[1]->primary[i]];

		if (left != palette[0][0])
			painted++;
		if (left != right) {
			fprintf(stderr, "pixel (%zu,%zu): %#x != %#x\n",
			    i % 640, i / 640, left, right);
			return 1;
		}
	}
	CHECK(painted != 0);
	for (size_t i = 0; i < 2; i++) {
		st_pixel_buffer_destroy(pixels[i]);
		st_cterm_destroy(&terminal[i]);
		st_font_catalog_destroy(fonts[i]);
		st_console_destroy(console[i]);
	}
	return 0;
}
