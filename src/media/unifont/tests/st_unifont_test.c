/* Copyright (c) 2026 Stephen Hurd */
/* SPDX-License-Identifier: BSD-2-Clause */

#include "st_unicode_fonts.h"
#include "st_unifont.h"

#include "st_console.h"
#include "st_presentation_font.h"
#include "st_emoji_fonts.h"
#include "st_pixels.h"

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
st_unifont_test_main(void)
{
	static const uint8_t armenian_small_et[] = {
		0x00, 0x00, 0x00, 0x40, 0x40, 0x78, 0x40, 0x40,
		0x40, 0x38, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
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
	/* Every overlay entry, including wide glyphs, must retain UNSCII's
	 * exact bitmap and width in the composite.
	 */
	for (size_t i = 0; i < st_unscii_16_font.mapping_count; ++i) {
		const struct st_console_font_mapping *entry = &st_unscii_16_font.mapping[i];
		bool wide = entry->glyph >= st_unscii_16_font.glyph_count;
		CHECK(st_console_font_lookup(console, composite, entry->codepoint,
		    &glyph_font, &composite_glyph, &columns) == 0);
		CHECK(glyph_font == composite);
		CHECK(columns == (wide ? 2 : 1));
		CHECK(memcmp(glyph_bitmap(&st_unscii_16_font, entry->glyph),
		    glyph_bitmap(&st_unifont_font, composite_glyph), wide ? 32 : 16) == 0);
	}
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
	/* Unicode 18 assigned U+0558 and Unifont 18 supplies its real glyph. */
	CHECK(st_console_font_lookup(console, composite, UINT32_C(0x0558),
	    &glyph_font, &composite_glyph, &columns) == 0
	    && glyph_font == composite && columns == 1);
	CHECK(memcmp(glyph_bitmap(&st_unifont_font, composite_glyph),
	    armenian_small_et, sizeof(armenian_small_et)) == 0);
	st_console_destroy(console);

	{
		static const char toned_thumb[] =
		    "\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd";
		struct st_presentation_font presentation = {0};
		struct st_console_cell presented[2];
		const struct st_console_rect row = {0, 0, 2, 1};
		uint32_t replacement;
		uint32_t replacement_character;
		uint16_t owner;
		uint8_t replacement_columns;
		const struct st_console_config raster_config = {
			.columns = 4, .rows = 2, .cell_width = 8, .cell_height = 16,
		};

		CHECK(st_presentation_font_create_fallback(&presentation,
		    &st_unifont_font) == 0);
		CHECK(presentation.font.fallback_glyph
		    != presentation.font.decode_error_glyph);
		CHECK(presentation.font.wide_fallback_glyph
		    >= presentation.font.glyph_count);
		CHECK(st_console_create(&console, &config_display, &raster_config) == 0);
		CHECK(st_console_register_static_font(console, &presentation.font,
		    &composite) == 0 && composite == 0);
		/* Adding Noto colour must preserve every native monochrome glyph. */
		for (size_t i = 0; i < st_unifont_font.mapping_count; ++i) {
			const struct st_console_font_mapping *entry = &st_unifont_font.mapping[i];
			bool wide = entry->glyph >= st_unifont_font.glyph_count;
			CHECK(st_console_font_lookup(console, 0, entry->codepoint, &owner,
			    &replacement, &replacement_columns) == 0);
			CHECK(owner == 0 && (replacement_columns == (wide ? 2 : 1)
			    || (!wide && replacement_columns == 2)));
			const uint8_t *source = glyph_bitmap(&st_unifont_font, entry->glyph);
			const uint8_t *actual = glyph_bitmap(&presentation.font, replacement);
			if (!wide && replacement_columns == 2) {
				for (size_t y = 0; y < 16; ++y) {
					CHECK(actual[2 * y] == source[y] >> 4);
					CHECK(actual[2 * y + 1] == (uint8_t)(source[y] << 4));
				}
			} else
				CHECK(memcmp(actual, source, wide ? 32 : 16) == 0);
		}
		CHECK(st_console_font_lookup(console, 0, UINT32_C(0x1f4b0), &owner,
		    &replacement, &replacement_columns) == 0);
		const struct st_console_color_glyph *money_color = NULL;
		for (size_t i = 0; i < presentation.font.color_glyph_count; ++i)
			if (presentation.font.color_glyphs[i].glyph == replacement)
				money_color = &presentation.font.color_glyphs[i];
		CHECK(money_color != NULL);
		bool noto_money = false;
		for (size_t i = 0; i < st_emoji_16_font.mapping_count; ++i) {
			if (st_emoji_16_font.mapping[i].codepoint != UINT32_C(0x1f4b0)) continue;
			for (size_t j = 0; j < st_emoji_16_font.color_glyph_count; ++j) {
				const struct st_console_color_glyph *source = &st_emoji_16_font.color_glyphs[j];
				if (source->glyph != st_emoji_16_font.mapping[i].glyph) continue;
				CHECK(money_color->size == source->size
				    && memcmp(money_color->rgba, source->rgba, source->size) == 0);
				CHECK(money_color->tall_size == source->tall_size
				    && money_color->tall_rgba == source->tall_rgba);
				noto_money = true;
			}
		}
		CHECK(noto_money);
		const struct st_console_rect money_pixels = {0, 0, 16, 16};
		struct st_pixel_buffer *mono_money = NULL, *color_money = NULL, *restored_money = NULL;
		CHECK(st_console_write_utf8(console, "\xf0\x9f\x92\xb0", 4) == 0);
		CHECK(st_console_get_pixels(&mono_money, console, &money_pixels, false) == 0);
		CHECK(st_console_set_color_emoji(console, true) == 0);
		CHECK(st_console_get_pixels(&color_money, console, &money_pixels, false) == 0);
		CHECK(memcmp(mono_money->primary, color_money->primary, 256 * sizeof(uint32_t)) != 0);
		CHECK(st_console_set_color_emoji(console, false) == 0);
		CHECK(st_console_get_pixels(&restored_money, console, &money_pixels, false) == 0);
		CHECK(memcmp(mono_money->primary, restored_money->primary, 256 * sizeof(uint32_t)) == 0);
		st_pixel_buffer_destroy(mono_money);
		st_pixel_buffer_destroy(color_money);
		st_pixel_buffer_destroy(restored_money);
		CHECK(st_console_set_position(console, 0, 0) == 0);
		CHECK(st_console_font_lookup(console, 0, UINT32_C(0xfffd), &owner,
		    &replacement_character, &replacement_columns) == 0);
		CHECK(owner == 0 && replacement_columns == 1
		    && replacement_character != presentation.font.fallback_glyph
		    && replacement_character != presentation.font.decode_error_glyph);
		CHECK(st_console_font_lookup_cluster(console, 0, toned_thumb,
		    sizeof(toned_thumb) - 1, 2, &owner, &replacement) == 0);
		CHECK(owner == 0 && replacement >= presentation.font.glyph_count
		    && replacement != presentation.font.wide_fallback_glyph);
		CHECK(st_console_write_utf8(console, toned_thumb,
		    sizeof(toned_thumb) - 1) == 0);
		CHECK(st_console_read_cells(console, &row, presented, 2) == 0);
		CHECK(presented[0].glyph == replacement
		    && presented[0].glyph_font == 0
		    && (presented[0].flags & ST_CONSOLE_CELL_CONTINUATION) == 0);
		CHECK(presented[1].glyph == replacement
		    && presented[1].glyph_font == 0
		    && (presented[1].flags & ST_CONSOLE_CELL_CONTINUATION) != 0);
		CHECK(st_console_set_position(console, 0, 0) == 0);
		CHECK(st_console_put_decode_error(console) == 0);
		CHECK(st_console_read_cells(console, &row, presented, 2) == 0);
		CHECK(presented[0].glyph == presentation.font.decode_error_glyph
		    && presented[0].glyph_font == 0
		    && presented[0].glyph != replacement_character
		    && presented[0].glyph != presentation.font.fallback_glyph);
		/* These are real wide diagnostic mappings in Unifont, not tofu.
		 * Neither may affect normal text, even at the final column. */
		for (uint32_t scalar = 0x2060; ; scalar = 0xfeff) {
			CHECK(st_console_font_lookup(console, 0, scalar, &owner,
			    &replacement, &replacement_columns) == 0);
			CHECK(owner == 0 && replacement_columns == 2
			    && replacement != presentation.font.wide_fallback_glyph);
			if (scalar == 0xfeff)
				break;
		}
		const struct st_console_rect all_cells = {0, 0, 4, 2};
		const struct st_console_rect all_pixels = {0, 0, 32, 32};
		struct st_console_cell baseline_cells[8], actual_cells[8];
		struct st_pixel_buffer *baseline = NULL, *actual = NULL;
		uint32_t x, y;
		CHECK(st_console_clear_window(console, ' ') == 0);
		CHECK(st_console_set_position(console, 0, 0) == 0);
		CHECK(st_console_write_utf8(console, "AB", 2) == 0);
		CHECK(st_console_read_cells(console, &all_cells, baseline_cells, 4) == 0);
		CHECK(st_console_get_pixels(&baseline, console, &all_pixels, false) == 0);
		CHECK(st_console_set_position(console, 0, 0) == 0);
		CHECK(st_console_write_utf8(console, "A\xe2\x81\xa0\xef\xbb\xbf" "B", 8) == 0);
		CHECK(st_console_get_position(console, &x, &y) == 0 && x == 2 && y == 0);
		CHECK(st_console_set_position(console, 3, 1) == 0);
		CHECK(st_console_write_utf8(console, "\xe2\x81\xa0\xef\xbb\xbf", 6) == 0);
		CHECK(st_console_get_position(console, &x, &y) == 0 && x == 3 && y == 1);
		CHECK(st_console_read_cells(console, &all_cells, actual_cells, 4) == 0);
		CHECK(memcmp(baseline_cells, actual_cells, sizeof(baseline_cells)) == 0);
		CHECK(st_console_get_pixels(&actual, console, &all_pixels, false) == 0);
		CHECK(memcmp(baseline->primary, actual->primary, 32 * 32 * sizeof(uint32_t)) == 0);
		CHECK(memcmp(baseline->alternate, actual->alternate, 32 * 32 * sizeof(uint32_t)) == 0);
		st_pixel_buffer_destroy(baseline);
		st_pixel_buffer_destroy(actual);
		st_console_destroy(console);
		st_presentation_font_destroy(&presentation);
	}
	return EXIT_SUCCESS;
}
