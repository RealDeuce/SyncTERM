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
		CHECK(st_console_set_position(console, 0, 0) == 0);
		CHECK(st_console_write_utf8(console, sequences[i], strlen(sequences[i])) == 0);
		struct st_console_cell cells[2];
		const struct st_console_rect area = {0, 0, 2, 1};
		CHECK(st_console_read_cells(console, &area, cells, 2) == 0);
		CHECK(cells[0].glyph == glyph && cells[1].glyph == glyph
		    && (cells[0].flags & ST_CONSOLE_CELL_CONTINUATION) == 0
		    && (cells[1].flags & ST_CONSOLE_CELL_CONTINUATION) != 0);
	}
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

int
main(void)
{
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
