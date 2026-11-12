/* Copyright (c) 2026 Stephen Hurd */
/* SPDX-License-Identifier: BSD-2-Clause */

#include "st_unicode_fonts.h"

#include "st_builtin_fonts.h"
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

static void
check_box_drawing(const struct st_console_font *font)
{
	struct st_console_font cp437;
	size_t count = 0;

	CHECK(st_builtin_font_get(0, 8, font->height, &cp437));
	for (size_t i = 0; i < cp437.mapping_count; ++i) {
		const struct st_console_font_mapping *original = &cp437.mapping[i];

		if (original->codepoint < 0x2500 || original->codepoint > 0x257f)
			continue;
		bool found = false;
		for (size_t j = 0; j < font->mapping_count; ++j) {
			const struct st_console_font_mapping *entry = &font->mapping[j];

			if (entry->codepoint != original->codepoint)
				continue;
			CHECK(entry->glyph < font->glyph_count);
			CHECK(memcmp(font->bitmap + entry->glyph * font->glyph_stride,
			    cp437.bitmap + original->glyph * cp437.glyph_stride,
			    cp437.glyph_stride) == 0);
			found = true;
			break;
		}
		CHECK(found);
		++count;
	}
	CHECK(count == 40);
	/* Independent pixel expectations for the new fallback-only strokes. */
	count = 0;
	for (size_t i = 0; i < font->mapping_count; ++i) {
		const struct st_console_font_mapping *entry = &font->mapping[i];
		size_t centre = font->height == 8 ? 4 : 7;

		if (entry->codepoint != 0x2501 && entry->codepoint != 0x250d
		    && entry->codepoint != 0x256d)
			continue;
		CHECK(entry->glyph < font->glyph_count);
		const uint8_t *bitmap = font->bitmap + entry->glyph * font->glyph_stride;
		++count;
		for (size_t y = 0; y < font->height; ++y) {
			uint8_t expected;

			if (entry->codepoint == 0x2501)
				expected = y >= centre - 1 && y <= centre + 1 ? 0xff : 0;
			else if (entry->codepoint == 0x250d)
				expected = y < centre - 1 ? 0 : y <= centre + 1 ? 0x1f : 0x18;
			else
				expected = y < centre ? 0 : y == centre ? 0x07 : y == centre + 1 ? 0x0c : 0x18;
			CHECK(bitmap[y] == expected);
		}
	}
	CHECK(count == 3);
}

int
st_fonts_test_main(void)
{
	static const enum st_codepage codepages[ST_BUILTIN_FONT_COUNT] = {
		ST_CODEPAGE_CP437, ST_CODEPAGE_CP1251, ST_CODEPAGE_KOI8_R,
		ST_CODEPAGE_ISO_8859_2, ST_CODEPAGE_ISO_8859_4,
		ST_CODEPAGE_CP866M, ST_CODEPAGE_ISO_8859_9, ST_CODEPAGE_HAIK8,
		ST_CODEPAGE_ISO_8859_8, ST_CODEPAGE_KOI8_U,
		ST_CODEPAGE_ISO_8859_15, ST_CODEPAGE_ISO_8859_4,
		ST_CODEPAGE_KOI8_R, ST_CODEPAGE_ISO_8859_4,
		ST_CODEPAGE_ISO_8859_5, ST_CODEPAGE_ARMSCII8,
		ST_CODEPAGE_ISO_8859_15, ST_CODEPAGE_CP850, ST_CODEPAGE_CP850,
		ST_CODEPAGE_CP865, ST_CODEPAGE_CP1251, ST_CODEPAGE_ISO_8859_7,
		ST_CODEPAGE_KOI8_R, ST_CODEPAGE_ISO_8859_4,
		ST_CODEPAGE_ISO_8859_1, ST_CODEPAGE_CP866M2, ST_CODEPAGE_CP437,
		ST_CODEPAGE_CP866M2, ST_CODEPAGE_CP865, ST_CODEPAGE_CP866U,
		ST_CODEPAGE_ISO_8859_1, ST_CODEPAGE_CP1131,
		ST_CODEPAGE_PETSCII_UPPER, ST_CODEPAGE_PETSCII_LOWER,
		ST_CODEPAGE_PETSCII_UPPER, ST_CODEPAGE_PETSCII_LOWER,
		ST_CODEPAGE_ATASCII, ST_CODEPAGE_ISO_8859_1,
		ST_CODEPAGE_ISO_8859_1, ST_CODEPAGE_ISO_8859_1,
		ST_CODEPAGE_ISO_8859_1, ST_CODEPAGE_ISO_8859_1,
		ST_CODEPAGE_ISO_8859_1, ST_CODEPAGE_PRESTEL,
		ST_CODEPAGE_ATARI_ST, ST_CODEPAGE_CP437,
	};
	static const unsigned availability[ST_BUILTIN_FONT_COUNT] = {
		0x0f, 0x01, 0x07, 0x07, 0x01, 0x01, 0x01, 0x07,
		0x07, 0x07, 0x01, 0x07, 0x01, 0x01, 0x07, 0x07,
		0x07, 0x05, 0x07, 0x05, 0x07, 0x07, 0x01, 0x07,
		0x07, 0x07, 0x05, 0x01, 0x07, 0x07, 0x01, 0x01,
		0x05, 0x05, 0x05, 0x05, 0x05, 0x03, 0x05, 0x01,
		0x01, 0x05, 0x03, 0x09, 0x05, 0x06,
	};
	static const struct {
		uint32_t width;
		uint32_t height;
		unsigned bit;
	} sizes[] = {
		{8, 16, 0x01},
		{8, 14, 0x02},
		{8, 8, 0x04},
		{12, 20, 0x08},
	};
	const struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 2,
		.rows = 1,
	};
	const struct st_console_rect screen = {
		.x = 0,
		.y = 0,
		.width = 2,
		.height = 1,
	};
	st_console_t *console = NULL;
	struct st_console_cell cells[2];
	struct st_console_font builtin;
	struct st_console_font cp437_builtin;
	struct st_console_style style;
	uint16_t font8;
	uint16_t font16;
	uint16_t prestel;
	uint16_t cp437;
	uint16_t glyph_font;
	uint32_t glyph;
	uint8_t columns;
	char glyph_text[4];
	size_t glyph_text_size;

	check_box_drawing(&st_unscii_8_font);
	check_box_drawing(&st_unscii_16_font);
	CHECK(st_console_create(&console, &config_display, &config) == 0);
	CHECK(st_console_register_static_font(console, &st_unscii_8_font,
	    &font8) == 0);
	CHECK(st_console_register_static_font(console, &st_unscii_16_font,
	    &font16) == 0);
	CHECK(font8 == 0 && font16 == 1);
	CHECK(st_builtin_font_count() == ST_BUILTIN_FONT_COUNT);
	CHECK(st_builtin_font_name(ST_BUILTIN_FONT_COUNT) == NULL);
	CHECK(strcmp(st_builtin_font_name(0), "Codepage 437 English") == 0);
	CHECK(strcmp(st_builtin_font_name(43), "Prestel") == 0);
	CHECK(strcmp(st_builtin_font_name(45), "RIPterm") == 0);
	CHECK(st_builtin_font_broken_vertical(0));
	CHECK(!st_builtin_font_broken_vertical(1));
	CHECK(!st_builtin_font_broken_vertical(ST_BUILTIN_FONT_COUNT));
	for (size_t selector = 0; selector < ST_BUILTIN_FONT_COUNT; ++selector) {
		CHECK(st_builtin_font_codepage(selector) == codepages[selector]);
		for (size_t size = 0; size < sizeof(sizes) / sizeof(sizes[0]);
		    ++size) {
			bool present = st_builtin_font_get(selector,
			    sizes[size].width, sizes[size].height, &builtin);

			CHECK(present == ((availability[selector] & sizes[size].bit)
			    != 0));
			if (!present)
				continue;
			CHECK(builtin.width == sizes[size].width);
			CHECK(builtin.height == sizes[size].height);
			CHECK(builtin.glyph_count == 256);
			CHECK(builtin.bitmap != NULL && builtin.bitmap_size != 0);
			CHECK(builtin.mapping != NULL && builtin.mapping_count != 0);
			CHECK(st_console_register_static_font(console, &builtin,
			    &glyph_font) == 0);
		}
	}
	CHECK(st_builtin_font_codepage(ST_BUILTIN_FONT_COUNT)
	    == ST_CODEPAGE_COUNT);
	CHECK(!st_builtin_font_get(0, 9, 16, &builtin));
	CHECK(!st_builtin_font_get(ST_BUILTIN_FONT_COUNT, 8, 16, &builtin));
	CHECK(!st_builtin_font_get(0, 8, 16, NULL));
	CHECK(st_builtin_font_get(0, 8, 16, &cp437_builtin));
	CHECK(st_builtin_font_get(14, 8, 16, &builtin));
	CHECK(cp437_builtin.glyph_stride == builtin.glyph_stride);
	CHECK(memcmp(cp437_builtin.bitmap + 'q' * cp437_builtin.glyph_stride,
	    builtin.bitmap + 'q' * builtin.glyph_stride,
	    builtin.glyph_stride) == 0);
	CHECK(st_builtin_font_get(43, 8, 16, &builtin));
	CHECK(st_console_register_static_font(console, &builtin,
	    &prestel) == 0);
	CHECK(st_console_font_lookup(console, prestel, 'A', &glyph_font, &glyph,
	    &columns) == 0);
	CHECK(glyph_font == prestel && glyph == 'A' && columns == 1);
	CHECK(st_console_font_lookup(console, prestel, '#', &glyph_font, &glyph,
	    &columns) == 0);
	CHECK(glyph_font == prestel && glyph == '?' && columns == 1);
	CHECK(st_console_font_lookup(console, prestel, UINT32_C(0x00a3),
	    &glyph_font, &glyph, &columns) == 0);
	CHECK(glyph_font == prestel && glyph == 0x23 && columns == 1);
	CHECK(st_builtin_font_get(0, 8, 16, &builtin));
	CHECK(st_console_register_static_font(console, &builtin,
	    &cp437) == 0);
	CHECK(st_console_font_lookup(console, cp437, 'A', &glyph_font, &glyph,
	    &columns) == 0);
	CHECK(glyph_font == cp437 && glyph == 'A' && columns == 1);
	CHECK(st_console_font_lookup(console, cp437, UINT32_C(0x263a),
	    &glyph_font, &glyph, &columns) == 0);
	CHECK(glyph_font == cp437 && glyph == 1 && columns == 1);
	CHECK(st_console_font_lookup(console, cp437, '|', &glyph_font, &glyph,
	    &columns) == 0);
	CHECK(glyph_font == cp437 && glyph == 0x7c && columns == 1);
	CHECK(st_console_font_lookup(console, cp437, UINT32_C(0x00a6),
	    &glyph_font, &glyph, &columns) == 0);
	CHECK(glyph_font == cp437 && glyph == 0x7c && columns == 1);
	CHECK(st_console_font_lookup(console, cp437, UINT32_C(0x2302),
	    &glyph_font, &glyph, &columns) == 0);
	CHECK(glyph_font == cp437 && glyph == 0x7f && columns == 1);
	CHECK(st_console_font_glyph_text(console, cp437, 0x7c, glyph_text,
	    sizeof(glyph_text), &glyph_text_size) == 0);
	CHECK(glyph_text_size == 1 && glyph_text[0] == '|');
	CHECK(st_console_font_lookup(console, cp437, UINT32_C(0x1f642),
	    &glyph_font, &glyph, &columns) != 0);
	CHECK(st_console_font_lookup(console, font8, 'A', &glyph_font, &glyph,
	    &columns) == 0);
	CHECK(glyph_font == font8 && columns == 1
	    && glyph < st_unscii_8_font.glyph_count);
	CHECK(st_console_font_lookup(console, font16, UINT32_C(0x2592),
	    &glyph_font, &glyph, &columns) == 0);
	CHECK(glyph_font == font16 && columns == 1
	    && glyph < st_unscii_16_font.glyph_count);
	CHECK(st_console_font_lookup(console, font16, UINT32_C(0x10ffff),
	    &glyph_font, &glyph, &columns) == 0);
	CHECK(glyph_font == font16 && columns == 1
	    && glyph == st_unscii_16_font.fallback_glyph);
	CHECK(st_console_font_lookup(console, font16, UINT32_C(0x20000),
	    &glyph_font, &glyph, &columns) == 0);
	CHECK(glyph_font == font16 && columns == 2
	    && glyph == st_unscii_16_font.wide_fallback_glyph);
	CHECK(st_console_get_style(console, &style) == 0);
	style.font = font16;
	CHECK(st_console_set_style(console, &style) == 0);
	CHECK(st_console_put_codepoint(console, UINT32_C(0x20000)) == 0);
	CHECK(st_console_read_cells(console, &screen, cells, 2) == 0);
	CHECK(cells[0].glyph == st_unscii_16_font.wide_fallback_glyph
	    && cells[0].font == font16
	    && cells[0].glyph_font == font16
	    && (cells[0].flags & ST_CONSOLE_CELL_CONTINUATION) == 0
	    && (cells[0].flags & ~ST_CONSOLE_CELL_PUBLIC_FLAGS) == 0);
	CHECK(cells[1].glyph == st_unscii_16_font.wide_fallback_glyph
	    && cells[1].font == font16
	    && cells[1].glyph_font == font16
	    && (cells[1].flags & ST_CONSOLE_CELL_CONTINUATION) != 0
	    && (cells[1].flags & ~ST_CONSOLE_CELL_PUBLIC_FLAGS) == 0);
	st_console_destroy(console);
	return EXIT_SUCCESS;
}
