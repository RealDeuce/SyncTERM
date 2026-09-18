/* Copyright (c) 2026 Stephen Hurd */
/* SPDX-License-Identifier: BSD-2-Clause */

#include "st_unicode_fonts.h"
#include "st_unifont.h"

#include "st_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
		    __FILE__, __LINE__, #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (false)

static const uint8_t *
glyph_bitmap(const struct st_console_font *font, uint32_t glyph)
{
	if (glyph < font->glyph_count)
		return font->bitmap + (size_t)glyph * font->glyph_stride;
	glyph -= font->glyph_count;
	CHECK(glyph < font->wide_glyph_count);
	return font->wide_bitmap + (size_t)glyph * font->wide_glyph_stride;
}

int
main(void)
{
	const struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 2,
		.rows = 1,
	};
	st_console_t *console = NULL;
	uint16_t unscii;
	uint16_t composite;
	uint16_t glyph_font;
	uint32_t unscii_glyph;
	uint32_t composite_glyph;
	uint8_t columns;

	CHECK(st_console_create(&console, &config_display, &config) == 0);
	CHECK(st_console_register_static_font(console, &st_unifont_font,
	    &composite) == 0);
	CHECK(st_console_register_static_font(console, &st_unscii_16_font,
	    &unscii) == 0);
	CHECK(composite == 0 && unscii == 1);
	CHECK(st_console_font_lookup(console, unscii, UINT32_C(0x2592),
	    &glyph_font, &unscii_glyph, &columns) == 0
	    && glyph_font == unscii && columns == 1);
	CHECK(st_console_font_lookup(console, composite, UINT32_C(0x2592),
	    &glyph_font, &composite_glyph, &columns) == 0
	    && glyph_font == composite && columns == 1);
	CHECK(memcmp(glyph_bitmap(&st_unscii_16_font, unscii_glyph),
	    glyph_bitmap(&st_unifont_font, composite_glyph), 16) == 0);
	CHECK(st_console_font_lookup(console, unscii, UINT32_C(0x4e00),
	    &glyph_font, &composite_glyph, &columns) == 0);
	CHECK(glyph_font == composite && columns == 2
	    && composite_glyph >= st_unifont_font.glyph_count);
	CHECK(st_console_font_lookup(console, composite, UINT32_C(0xfffd),
	    &glyph_font, &composite_glyph, &columns) == 0
	    && glyph_font == composite && columns == 1);
	st_console_destroy(console);
	return EXIT_SUCCESS;
}
