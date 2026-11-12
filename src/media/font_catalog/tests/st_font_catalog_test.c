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
	CHECK(st_font_catalog_load_amiga(catalog, 0, st_amiga_font_builtin(ST_AMIGA_FONT_SET_2PLUS, "pearl.font", 8),
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
check_mode_change_retained_cells(void)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	struct st_console_config config = {
		.columns = 3, .rows = 2, .cell_width = 8, .cell_height = 16,
	};
	const struct st_console_rect cells = {0, 0, 3, 2};
	static const struct {
		uint32_t width;
		uint32_t height;
		uint8_t z[16];
	} modes[] = {
		/* Literal CP437 Z rows from the pinned 8x8/14/16 font strikes. */
		{8, 8, {0xfe, 0xc6, 0x8c, 0x18, 0x32, 0x66, 0xfe, 0}},
		{8, 14, {0, 0, 0xfe, 0xc6, 0x8c, 0x18, 0x30, 0x60,
		    0xc2, 0xc6, 0xfe, 0, 0, 0}},
		{9, 16, {0, 0, 0xfe, 0xc6, 0x86, 0x0c, 0x18, 0x30,
		    0x60, 0xc2, 0xc6, 0xfe, 0, 0, 0, 0}},
		{8, 16, {0, 0, 0xfe, 0xc6, 0x86, 0x0c, 0x18, 0x30,
		    0x60, 0xc2, 0xc6, 0xfe, 0, 0, 0, 0}},
	};
	st_console_t *console = NULL;
	st_font_catalog_t *catalog = NULL;
	struct st_console_cell retained[6], actual[6];
	uint16_t identifier;
	uint8_t columns;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(st_console_set_cursor(console, false, ST_CONSOLE_CURSOR_BLOCK) == 0);
	CHECK(st_font_catalog_create(&catalog, console) == 0);
	CHECK(st_font_catalog_identifier(catalog, 0, &identifier));
	struct st_console_cell z = {.font = identifier, .foreground = 7,
		.background = 1, .flags = ST_CONSOLE_CELL_BLINK};
	CHECK(st_console_font_lookup(console, identifier, 'Z', &z.glyph_font,
	    &z.glyph, &columns) == 0);
	CHECK(st_console_fill(console, &cells, &z) == 0);
	/* Retain a Unicode glyph owned by the presentation fallback as well.
	 * Its full-block artwork is a solid rectangle in every strike, while
	 * its resolved glyph index differs between the font banks. */
	uint16_t fallback;
	CHECK(st_console_get_fallback_font(console, 8, 16, &fallback) == 0);
	CHECK(st_console_font_lookup(console, fallback, UINT32_C(0x2588),
	    &z.glyph_font, &z.glyph, &columns) == 0);
	const struct st_console_rect last = {2, 1, 1, 1};
	CHECK(st_console_write_complete_cells(console, &last, &z, 1) == 0);
	CHECK(st_console_read_cells(console, &cells, retained, 3) == 0);
	st_font_catalog_destroy(catalog);
	for (size_t mode = 0; mode < sizeof(modes) / sizeof(modes[0]); ++mode) {
		config.cell_width = modes[mode].width;
		config.cell_height = modes[mode].height;
		config.video_flags = config.cell_width == 9 ? ST_CONSOLE_VIDEO_EXPAND : 0;
		CHECK(st_console_reconfigure(console, &config) == 0);
		/* Resubmit the exact saved cells, including their original font IDs.
		 * Register the current strikes afterwards to test cache invalidation. */
		CHECK(st_console_write_complete_cells(console, &cells, retained, 3) == 0);
		CHECK(st_console_present(console) == 0);
		CHECK(st_font_catalog_create(&catalog, console) == 0);
		CHECK(st_console_read_cells(console, &cells, actual, 3) == 0);
		for (size_t i = 0; i < 6; ++i) {
			CHECK(actual[i].font == retained[i].font);
			CHECK(actual[i].glyph_font == retained[i].glyph_font);
			CHECK(actual[i].glyph == retained[i].glyph);
		}
		struct st_pixel_buffer *pixels = NULL;
		const struct st_console_rect area = {0, 0, 3 * config.cell_width, 2 * config.cell_height};
		CHECK(st_console_get_pixels(&pixels, console, &area, false) == 0);
		for (uint32_t y = 0; y < area.height; ++y)
			for (uint32_t x = 0; x < area.width; ++x) {
				uint32_t column = x % config.cell_width;
				uint32_t expected = (modes[mode].z[y % config.cell_height]
				    & (0x80u >> column)) != 0 ? 7 : 1;
				if (x >= 2 * config.cell_width && y >= config.cell_height && column < 8)
					expected = 7;
				CHECK(pixels->primary[y * area.width + x] == expected);
				CHECK(pixels->alternate[y * area.width + x] == 1);
			}
		st_pixel_buffer_destroy(pixels);
		/* Newly painted cells can coexist with cells retained from startup. */
		CHECK(st_font_catalog_identifier(catalog, 0, &retained[0].font));
		CHECK(st_console_font_lookup(console, retained[0].font, 'Z',
		    &retained[0].glyph_font, &retained[0].glyph, &columns) == 0);
		st_font_catalog_destroy(catalog);
	}
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
	CHECK(check_mode_change_retained_cells() == 0);
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
