/* Tests for console-specific terminal font catalogs. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_font_catalog.h"

#include "io_error.h"
#include "st_builtin_fonts.h"
#include "st_pixels.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, \
		    #expression); \
		return EXIT_FAILURE; \
	} \
} while (0)

static int check_amiga_descenders(void)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {
		.columns = 4, .rows = 1, .cell_width = 8, .cell_height = 8};
	st_console_t *console = NULL;
	st_font_catalog_t *catalog = NULL;
	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&catalog, console) == 0);
	CHECK(st_font_catalog_load_amiga(catalog, 0, st_amiga_font_builtin("pearl.font", 8),
									 ST_CODEPAGE_ISO_8859_1) == 0);
	uint16_t identifier;
	CHECK(st_font_catalog_identifier(catalog, 0, &identifier));
	struct st_console_style style = {.font = identifier,
									 .foreground = ST_CONSOLE_COLOR_RGB_VALUE(255, 255, 255),
									 .background = ST_CONSOLE_COLOR_RGB_VALUE(0, 0, 0)};
	CHECK(st_console_set_style(console, &style) == 0);
	CHECK(st_console_write_utf8(console, "gpy", 3) == 0);
	struct st_pixel_buffer *pixels = NULL;
	const struct st_console_rect area = {0, 0, 24, 8};
	CHECK(st_console_get_pixels(&pixels, console, &area, false) == 0);
	/* Literal rows from the pinned Pearl/8 input, including its final
	 * descender rows. No expected raster is generated with the renderer. */
	static const uint8_t expected[3][8] = {
		{0, 0, 0x7e, 0xc6, 0xc6, 0x7e, 0x06, 0x7c},
		{0, 0, 0xfc, 0xc6, 0xc6, 0xfc, 0xc0, 0xc0},
		{0, 0, 0xc3, 0x66, 0x3c, 0x18, 0x30, 0x60},
	};
	for (unsigned g = 0; g < 3; ++g)
		for (unsigned y = 0; y < 8; ++y)
			for (unsigned x = 0; x < 8; ++x)
				CHECK(pixels->primary[y * 24 + g * 8 + x] ==
					  ((expected[g][y] & (0x80u >> x)) ? style.foreground : style.background));
	st_pixel_buffer_destroy(pixels);
	st_font_catalog_destroy(catalog);
	st_console_destroy(console);
	return 0;
}

static int
check_small_font_tofu(uint32_t height)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config config = {
		.columns = 4, .rows = 1, .cell_width = 8, .cell_height = height,
	};
	st_console_t *console = NULL;
	st_font_catalog_t *catalog = NULL;
	struct st_console_font font;
	uint16_t identifier, owner;
	uint32_t glyph;
	uint8_t columns;
	CHECK(st_console_create(&console, &display, &config) == 0);
	/* Mode changes retain the startup font in slot zero, so exercise an
	 * incompatible 8x16 fallback rather than another small font. */
	CHECK(st_builtin_font_get(0, 8, height == 16 ? 8 : 16, &font));
	CHECK(st_console_register_static_font(console, &font, &identifier) == 0 && identifier == 0);
	CHECK(st_font_catalog_create(&catalog, console) == 0);
	uint16_t fallback;
	CHECK(st_console_get_fallback_font(console, 8, height, &fallback) == 0);
	for (size_t selector = 0; selector < st_builtin_font_count(); ++selector) {
		if (!st_builtin_font_get(selector, 8, height, &font)) continue;
		CHECK(st_font_catalog_identifier(catalog, (uint8_t)selector, &identifier));
		/* The original byte glyphs and their Unicode mappings retain indexes. */
		for (size_t i = 0; i < font.mapping_count; ++i) {
			if (font.mapping[i].codepoint != 'A') continue;
			CHECK(st_console_font_lookup(console, identifier, 'A', &owner, &glyph, &columns) == 0);
			CHECK(owner == identifier && glyph == font.mapping[i].glyph && columns == 1);
			break;
		}
		bool missing = false;
		CHECK(st_console_font_lookup_cluster_ex(console, identifier,
		    "\xf4\x8f\xbf\xbf", 4, 1, &owner, &glyph, &missing) == 0);
		CHECK(owner == fallback && missing && glyph >= 256);
		CHECK(st_console_font_lookup(console, identifier, UINT32_C(0x20000),
		    &owner, &glyph, &columns) == 0);
		CHECK(owner == fallback && columns == 2 && glyph >= 256);
		CHECK(st_console_font_lookup(console, identifier, UINT32_C(0x1f642),
		    &owner, &glyph, &columns) == 0);
		CHECK(owner == fallback && columns == 2 && glyph >= 256);
		missing = true;
		CHECK(st_console_font_lookup_cluster_ex(console, identifier,
		    "\xf0\x9f\x91\xa9\xf0\x9f\x8f\xbd\xe2\x80\x8d\xf0\x9f\x92\xbb",
		    15, 2, &owner, &glyph, &missing) == 0);
		CHECK(owner == fallback && !missing);
		struct st_console_style style;
		CHECK(st_console_get_style(console, &style) == 0);
		style.font = identifier;
		style.foreground = ST_CONSOLE_COLOR_RGB_VALUE(255, 255, 255);
		style.background = ST_CONSOLE_COLOR_RGB_VALUE(0, 0, 0);
		CHECK(st_console_set_style(console, &style) == 0);
		CHECK(st_console_set_position(console, 0, 0) == 0);
		CHECK(st_console_write_utf8(console, "\xf4\x8f\xbf\xbf\xf0\xa0\x80\x80", 8) == 0);
		const struct st_console_rect area = {0, 0, 24, height};
		struct st_pixel_buffer *pixels = NULL;
		CHECK(st_console_get_pixels(&pixels, console, &area, false) == 0);
		/* Both the narrow and double-cell tofu must have visible box edges
		 * and an unfilled interior, not a question mark or solid block. */
		for (uint32_t y = 0; y < height; ++y) {
			for (uint32_t x = 0; x < 24; ++x) {
				if (x == 0 || x == 7 || x == 8 || x == 23 || y == 0 || y == height - 1)
					CHECK(pixels->primary[(size_t)y * 24 + x] == style.foreground);
			}
		}
		CHECK(pixels->primary[24 + 6] == style.background);
		CHECK(pixels->primary[24 + 12] == style.background);
		st_pixel_buffer_destroy(pixels);
	}
	st_font_catalog_destroy(catalog);
	CHECK(st_font_catalog_create(&catalog, console) == 0);
	uint16_t reused;
	CHECK(st_console_get_fallback_font(console, 8, height, &reused) == 0
	    && reused == fallback);
	st_font_catalog_destroy(catalog);
	st_console_destroy(console);
	return 0;
}

int
st_font_catalog_test_main(void)
{
	CHECK(check_amiga_descenders()==0);
	CHECK(check_small_font_tofu(8) == 0);
	CHECK(check_small_font_tofu(14) == 0);
	CHECK(check_small_font_tofu(16) == 0);
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 8, .rows = 2, .cell_width = 8, .cell_height = 16,
	};
	static const uint8_t zeros[4096] = {0};
	const char path[] = "st_font_catalog_test.f16";
	st_console_t *console = NULL;
	st_font_catalog_t *catalog = NULL;
	uint16_t identifier;
	uint16_t glyph_font;
	uint32_t glyph;
	uint8_t columns;
	uint8_t selector;
	FILE *stream;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(st_font_catalog_create(&catalog, console) == 0);
	CHECK(st_font_catalog_console(catalog) == console);
	CHECK(st_font_catalog_identifier(catalog, 0, &identifier));
	CHECK(st_font_catalog_selector(catalog, identifier, &selector)
	    && selector == 0);
	CHECK(!st_font_catalog_identifier(catalog,
	    ST_FONT_CATALOG_FIRST_CUSTOM, &identifier));
	CHECK(st_font_catalog_codepage(catalog, ST_FONT_CATALOG_FIRST_CUSTOM) == ST_CODEPAGE_COUNT);
	CHECK(st_font_catalog_load_custom(catalog,
	    ST_FONT_CATALOG_FIRST_CUSTOM, "missing-font-file")
	    == IO_ERROR_NOT_FOUND);
	CHECK(!st_font_catalog_identifier(catalog,
	    ST_FONT_CATALOG_FIRST_CUSTOM, &identifier));
	stream = fopen(path, "wb");
	CHECK(stream != NULL);
	CHECK(fwrite(zeros, 1, sizeof(zeros), stream) == sizeof(zeros));
	CHECK(fclose(stream) == 0);
	CHECK(st_font_catalog_load_custom(catalog,
	    ST_FONT_CATALOG_FIRST_CUSTOM, path) == 0);
	CHECK(st_font_catalog_identifier(catalog,
	    ST_FONT_CATALOG_FIRST_CUSTOM, &identifier));
	CHECK(st_console_font_lookup(console, identifier, 'A', &glyph_font,
	    &glyph, &columns) == 0);
	CHECK(glyph_font == identifier && glyph == 'A');
	bool missing = true;
	CHECK(st_console_font_lookup_cluster_ex(console, identifier,
	    "\xf0\x9f\xa4\xb7\xf0\x9f\x8f\xbd", 8, 2,
	    &glyph_font, &glyph, &missing) == 0);
	uint16_t fallback;
	CHECK(st_console_get_fallback_font(console, 8, 16, &fallback) == 0);
	CHECK(glyph_font == fallback && !missing);
	CHECK(st_font_catalog_load_custom_codepage(catalog,
	    ST_FONT_CATALOG_FIRST_CUSTOM, path, ST_CODEPAGE_CP1251) == 0);
	CHECK(st_font_catalog_identifier(catalog,
	    ST_FONT_CATALOG_FIRST_CUSTOM, &identifier));
	CHECK(st_console_font_lookup(console, identifier, UINT32_C(0x0410),
	    &glyph_font, &glyph, &columns) == 0);
	CHECK(glyph_font == identifier && glyph == 0xc0);
	CHECK(st_font_catalog_selector(catalog, identifier, &selector)
	    && selector == ST_FONT_CATALOG_FIRST_CUSTOM);
	{
		uint16_t retained = identifier;

		stream = fopen(path, "wb");
		CHECK(stream != NULL);
		CHECK(fputc(0, stream) == 0 && fclose(stream) == 0);
		CHECK(st_font_catalog_load_custom(catalog,
		    ST_FONT_CATALOG_FIRST_CUSTOM, path)
		    == IO_ERROR_INVALID_ENCODING);
		CHECK(st_font_catalog_identifier(catalog,
		    ST_FONT_CATALOG_FIRST_CUSTOM, &identifier)
		    && identifier == retained);
	}
	CHECK(remove(path) == 0);
	CHECK(st_font_catalog_codepage(catalog,
	    ST_FONT_CATALOG_FIRST_CUSTOM) == ST_CODEPAGE_CP1251);
	st_font_catalog_destroy(catalog);
	st_console_destroy(console);
	return EXIT_SUCCESS;
}
