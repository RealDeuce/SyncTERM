/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#include "st_builtin_fonts.h"
#include "st_emoji_fonts.h"
#include "st_presentation_font.h"
#include "st_unicode_fonts.h"
#include "io_error.h"

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

static const struct st_console_color_glyph *
color_glyph(const struct st_console_font *font, uint32_t glyph)
{
	for (size_t i = 0; i < font->color_glyph_count; ++i)
		if (font->color_glyphs[i].glyph == glyph)
			return &font->color_glyphs[i];
	return NULL;
}

static void
check_font(uint32_t height, bool fallback)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config config = {
		.columns = 4, .rows = 1, .cell_width = 8, .cell_height = height,
	};
	static const char *const sequences[] = {
		"\xf0\x9f\x98\x80", /* grinning face */
		"\xf0\x9f\xa4\xb7\xf0\x9f\x8f\xbd", /* shrug, medium skin tone */
		"\xf0\x9f\x91\xa9\xf0\x9f\x8f\xbd\xe2\x80\x8d\xf0\x9f\x92\xbb", /* technologist */
		"\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa6", /* family */
		"\xf0\x9f\x87\xa8\xf0\x9f\x87\xa6", /* Canada */
		"1\xef\xb8\x8f\xe2\x83\xa3", /* keycap */
		"\xe2\x9d\xa4\xef\xb8\x8f", /* emoji heart */
	};
	struct st_console_font base;
	struct st_presentation_font font = {0};
	struct st_presentation_font shared = {0};
	st_console_t *console = NULL;
	uint16_t identifier, owner;
	uint32_t glyph;
	uint8_t columns;
	CHECK(st_builtin_font_get(0, 8, height, &base));
	CHECK((fallback ? st_presentation_font_create_fallback(&font, &base)
	    : st_presentation_font_create(&font, &base)) == 0);
	CHECK(memcmp(font.font.bitmap, base.bitmap, base.bitmap_size) == 0);
	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(st_console_register_static_font(console, &font.font, &identifier) == 0);
	uint16_t fallback_identifier = identifier;
	if (!fallback) {
		CHECK(st_presentation_font_create_fallback(&shared, &base) == 0);
		CHECK(st_console_register_static_font(console, &shared.font, &fallback_identifier) == 0);
	}
	CHECK(st_console_set_fallback_font(console, fallback_identifier) == 0);
	uint16_t queried = UINT16_MAX;
	CHECK(st_console_get_fallback_font(console, 8, height, &queried) == 0
	    && queried == fallback_identifier);
	CHECK(st_console_get_fallback_font(console, 8, height + 1, &queried)
	    == IO_ERROR_NOT_FOUND && queried == fallback_identifier);
	CHECK(st_console_set_fallback_font(console, UINT16_MAX) == IO_ERROR_NOT_FOUND);
	CHECK(st_console_set_fallback_font(NULL, identifier) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_console_get_fallback_font(console, 0, height, &queried) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_console_get_fallback_font(console, 8, height, NULL) == IO_ERROR_INVALID_ARGUMENT);
	const struct st_console_font *emoji_font = fallback ? &font.font : &shared.font;
	CHECK(st_console_font_lookup(console, identifier, 'A', &owner, &glyph, &columns) == 0);
	CHECK(owner == identifier && glyph == 'A' && columns == 1);
	CHECK(st_console_font_lookup(console, identifier, UINT32_C(0x263a), &owner,
	    &glyph, &columns) == 0);
	CHECK((glyph == 1) == !fallback); /* Native CP437 smiley wins in a BBS font. */
	for (size_t i = 0; i < sizeof(sequences) / sizeof(sequences[0]); ++i) {
		bool missing = true;
		CHECK(st_console_font_lookup_cluster_ex(console, identifier, sequences[i],
		    strlen(sequences[i]), 2, &owner, &glyph, &missing) == 0);
		CHECK(!missing && owner == fallback_identifier && glyph >= emoji_font->glyph_count
		    && glyph != emoji_font->wide_fallback_glyph);
		size_t offset = (size_t)(glyph - emoji_font->glyph_count) * emoji_font->wide_glyph_stride;
		bool visible = false;
		for (size_t byte = 0; byte < emoji_font->wide_glyph_stride; ++byte)
			visible |= emoji_font->wide_bitmap[offset + byte] != 0;
		CHECK(visible);
		const struct st_console_color_glyph *color = color_glyph(emoji_font, glyph);
		CHECK(color != NULL && color->row_stride == 16 * 4
		    && color->size == 16 * height * 4);
		CHECK(st_console_set_position(console, 0, 0) == 0);
		CHECK(st_console_write_utf8(console, sequences[i], strlen(sequences[i])) == 0);
		struct st_console_cell cells[2];
		const struct st_console_rect area = {0, 0, 2, 1};
		CHECK(st_console_read_cells(console, &area, cells, 2) == 0);
		CHECK(cells[0].glyph == glyph && cells[1].glyph == glyph
		    && (cells[0].flags & ST_CONSOLE_CELL_CONTINUATION) == 0
		    && (cells[1].flags & ST_CONSOLE_CELL_CONTINUATION) != 0);
	}
	/* Default text and VS15 must not share a colour-bearing glyph ID. */
	CHECK(st_console_font_lookup(console, identifier, UINT32_C(0x2764),
	    &owner, &glyph, &columns) == 0);
	CHECK(owner == fallback_identifier && color_glyph(emoji_font, glyph) == NULL);
	uint32_t plain_heart = glyph;
	const char *text_heart = "\xe2\x9d\xa4\xef\xb8\x8e";
	CHECK(st_console_font_lookup_cluster(console, identifier, text_heart,
	    strlen(text_heart), 1, &owner, &glyph) == 0);
	CHECK(color_glyph(emoji_font, glyph) == NULL);
	const char *emoji_heart = "\xe2\x9d\xa4\xef\xb8\x8f";
	CHECK(st_console_font_lookup_cluster(console, identifier, emoji_heart,
	    strlen(emoji_heart), 2, &owner, &glyph) == 0);
	CHECK(glyph != plain_heart && color_glyph(emoji_font, glyph) != NULL);
	/* Same monochrome outlines may have different intrinsic colours. */
	const char *tones[] = {"\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbb",
	    "\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbf"};
	const struct st_console_color_glyph *tone[2];
	for (size_t i = 0; i < 2; ++i) {
		CHECK(st_console_font_lookup_cluster(console, identifier, tones[i],
		    strlen(tones[i]), 2, &owner, &glyph) == 0);
		tone[i] = color_glyph(emoji_font, glyph);
		CHECK(tone[i] != NULL);
	}
	CHECK(tone[0]->glyph != tone[1]->glyph && tone[0]->size == tone[1]->size
	    && memcmp(tone[0]->rgba, tone[1]->rgba, tone[0]->size) != 0);
	bool missing = false;
	CHECK(st_console_font_lookup_cluster_ex(console, identifier, "\xf4\x8f\xbf\xbf",
	    4, 1, &owner, &glyph, &missing) == 0);
	CHECK(missing && owner == fallback_identifier && glyph == emoji_font->fallback_glyph);
	if (!fallback) {
		CHECK(st_console_set_fallback_font(console, identifier) == 0);
		CHECK(st_console_get_fallback_font(console, 8, height, &queried) == 0
		    && queried == identifier);
		CHECK(st_console_set_fallback_font(console, fallback_identifier) == 0);
	}
	st_console_destroy(console);
	st_presentation_font_destroy(&font);
	st_presentation_font_destroy(&shared);
}

static void
check_color_rebasing(void)
{
	static const uint8_t mono[2] = {0};
	static const uint8_t wide[4] = {0};
	static const uint8_t narrow_rgba[64] = {255, 0, 0, 255};
	static const uint8_t wide_rgba[128] = {0, 255, 0, 255};
	static const struct st_console_color_glyph colors[] = {
		{0, narrow_rgba, sizeof(narrow_rgba), 32},
		{1, wide_rgba, sizeof(wide_rgba), 64},
	};
	const struct st_console_font base = {
		.bitmap = mono, .bitmap_size = sizeof(mono),
		.width = 8, .height = 2, .glyph_count = 1,
		.row_stride = 1, .glyph_stride = 2,
		.wide_bitmap = wide, .wide_bitmap_size = sizeof(wide),
		.wide_glyph_count = 1, .wide_row_stride = 2, .wide_glyph_stride = 4,
		.color_glyphs = colors, .color_glyph_count = 2,
	};
	struct st_presentation_font font = {0};
	CHECK(st_presentation_font_create(&font, &base) == 0);
	CHECK(font.font.color_glyph_count == 2);
	CHECK(font.font.color_glyphs[0].glyph == 0
	    && font.font.color_glyphs[0].rgba == narrow_rgba);
	CHECK(font.font.color_glyphs[1].glyph == 3
	    && font.font.color_glyphs[1].rgba == wide_rgba);
	st_presentation_font_destroy(&font);
	CHECK(narrow_rgba[0] == 255 && wide_rgba[1] == 255);
}

int
main(void)
{
	check_color_rebasing();
	for (size_t i = 0; i < 3; ++i) {
		uint32_t height = i == 0 ? 8 : i == 1 ? 14 : 16;
		check_font(height, false);
		check_font(height, true);
	}
	struct st_presentation_font result = {0};
	CHECK(st_presentation_font_create_fallback(NULL, &st_unscii_8_font) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_presentation_font_create_fallback(&result, NULL) == IO_ERROR_INVALID_ARGUMENT);
	struct st_console_font invalid = st_unscii_8_font;
	invalid.glyph_count = UINT32_MAX - 2;
	CHECK(st_presentation_font_create_fallback(&result, &invalid) == IO_ERROR_INVALID_ARGUMENT);
	st_presentation_font_destroy(&result);
	return EXIT_SUCCESS;
}
