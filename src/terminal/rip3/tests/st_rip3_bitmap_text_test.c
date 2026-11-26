/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io_error.h"
#include "st_pixels.h"
#include "st_rip3_bitmap_text.h"
#include "st_rip3_font_data.h"
#include "st_rip3_ports.h"

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

static const uint8_t  text[] = {1, 255, 0};
static const unsigned rows[] = {0x40, 0xa0, 0xe0};

static int
raster(void)
{
	struct st_rip3_ports              *ports           = NULL;
	uint8_t                            glyphs[255 * 3] = {0};
	struct st_rip3_maf_font            font            = {3, 3, 1, 3, glyphs};
	struct st_rip3_bitmap_text_style   style           = {1, false, false, false};
	struct st_rip3_bitmap_text_metrics metrics;
	const struct st_rip3_rect          bounds          = {0, 0, 23, 15};
	const struct st_rip3_text_position origin          = {1, 2};
	const unsigned                     expected_rows[] = {0x50, 0xa8, 0xe4};

	glyphs[0]           = 0x40;
	glyphs[1]           = 0xa0;
	glyphs[2]           = 0xe0;
	glyphs[254 * 3]     = 0x80;
	glyphs[254 * 3 + 1] = 0x40;
	glyphs[254 * 3 + 2] = 0x20;
	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	for (unsigned scale = 1; scale <= 2; scale++) {
		style.magnification = scale;
		CHECK(st_rip3_bitmap_text_measure(&font, &style, 3, &metrics) == 0);
		CHECK(metrics.width == 9 * scale && metrics.height == 3 * scale
		      && metrics.advance.x == (int32_t)(9 * scale) && metrics.advance.y == 0);
		for (unsigned opaque = 0; opaque <= 1; opaque++) {
			style.opaque = opaque != 0;
			for (unsigned mode = ST_RIP3_COPY; mode <= ST_RIP3_NOT; mode++) {
				CHECK(st_rip3_ports_erase(ports, 9) == 0);
				CHECK(st_rip3_draw_bitmap_text(ports, &font, &style, text, 3, &origin, 7, 3,
					  (enum st_rip3_write_mode)mode)
				      == 0);
				for (int32_t y = 0; y < 16; y++) {
					for (int32_t x = 0; x < 24; x++) {
						uint8_t value, expected = 9;
						int     dx = x - 1, dy = y - 2;

						if (dx >= 0 && dy >= 0 && dx < (int)(9 * scale)
						    && dy < (int)(3 * scale)) {
							unsigned column = (unsigned)dx / scale;
							bool     ink    = column < 8
							                  && (expected_rows[(unsigned)dy / scale]
							                         & (0x80U >> column))
							                         != 0;

							if (ink || opaque) {
								uint8_t       source       = ink ? 7 : 3;
								const uint8_t operations[] = {source, 9 ^ source,
								    9 | source, 9 & source, 246};

								expected = operations[mode];
							}
						}
						CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
						CHECK(value == expected);
					}
				}
			}
		}
	}
	/* Multibyte rows: nonzero padding bits must not paint the next cell. */
	{
		uint8_t                            wide_glyphs[255 * 4] = {0};
		struct st_rip3_maf_font            wide                 = {9, 2, 2, 4, wide_glyphs};
		const uint8_t                      two[]                = {1, 1};
		const struct st_rip3_text_position zero                 = {0, 0};

		wide_glyphs[0] = 0x80;
		wide_glyphs[1] = 0xff;
		wide_glyphs[2] = wide_glyphs[3] = 0xff;
		style                           = (struct st_rip3_bitmap_text_style) {1, false, false, false};
		CHECK(st_rip3_ports_erase(ports, 0) == 0);
		CHECK(st_rip3_draw_bitmap_text(ports, &wide, &style, two, 2, &zero, 7, 3, ST_RIP3_COPY) == 0);
		for (int32_t x = 0; x < 24; x++) {
			uint8_t value;

			CHECK(st_rip3_ports_read(ports, 1, x, 0, &value) == 0);
			CHECK(value == (x == 0 || x == 8 || x == 9 || x == 17 ? 7 : 0));
			CHECK(st_rip3_ports_read(ports, 1, x, 1, &value) == 0);
			CHECK(value == (x < 18 ? 7 : 0));
		}
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
vertical_and_shadow(void)
{
	struct st_rip3_ports              *ports           = NULL;
	uint8_t                            glyphs[255 * 3] = {0};
	struct st_rip3_maf_font            font            = {3, 3, 1, 3, glyphs};
	struct st_rip3_bitmap_text_style   style           = {1, true, false, false};
	struct st_rip3_bitmap_text_metrics metrics;
	const struct st_rip3_rect          bounds     = {0, 0, 11, 9};
	const struct st_rip3_text_position origin     = {2, 6};
	const uint8_t                      character  = 1;
	const char *const                  expected[] = {
	    "............",
	    "............",
	    "............",
	    "............",
	    "...##.......",
	    "..#.#.......",
	    "...##.......",
	    "............",
	    "............",
	    "............",
	};

	for (size_t i = 0; i < 3; i++)
		glyphs[i] = (uint8_t)rows[i];
	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_800_600) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_bitmap_text_measure(&font, &style, 2, &metrics) == 0);
	CHECK(metrics.width == 3 && metrics.height == 6 && metrics.advance.x == 0 && metrics.advance.y == -6);
	for (unsigned shadow = 0; shadow <= 1; shadow++) {
		style.shadow = shadow != 0;
		CHECK(st_rip3_ports_erase(ports, 0) == 0);
		CHECK(st_rip3_draw_bitmap_text(ports, &font, &style, &character, 1, &origin, 7, 3, ST_RIP3_XOR)
		      == 0);
		for (int32_t y = 0; y < 10; y++) {
			for (int32_t x = 0; x < 12; x++) {
				uint8_t value;
				uint8_t reference = expected[y][x] == '#' ? 7
				                    : shadow && x > 0 && y > 0 && expected[y - 1][x - 1] == '#'
				                        ? 3
				                        : 0;

				CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
				CHECK(value == reference);
			}
		}
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
errors_and_clipping(void)
{
	struct st_rip3_ports              *ports           = NULL;
	uint8_t                            glyphs[255 * 3] = {0};
	struct st_rip3_maf_font            font            = {3, 3, 1, 3, glyphs};
	struct st_rip3_maf_font            invalid;
	struct st_rip3_bitmap_text_style   style   = {1, false, false, false};
	struct st_rip3_bitmap_text_metrics metrics = {
	    77, 88, {99, 100}
        };
	struct st_rip3_text_position origin    = {-1, -1};
	const struct st_rip3_rect    bounds    = {0, 0, 5, 5};
	const uint8_t                character = 1;

	for (size_t i = 0; i < 3; i++)
		glyphs[i] = (uint8_t)rows[i];
	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_1024_768) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_draw_bitmap_text(ports, &font, &style, &character, 1, &origin, 7, 3, ST_RIP3_COPY) == 0);
	/* Clipping crops the lower-right two columns/rows of the literal glyph. */
	for (int32_t y = 0; y < 6; y++) {
		for (int32_t x = 0; x < 6; x++) {
			uint8_t value;
			bool    ink = (y == 0 && x == 1) || (y == 1 && x <= 1);

			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (ink ? 7 : 0));
		}
	}
	{
		uint8_t                           *long_text   = calloc(65536, 1);
		const struct st_rip3_text_position long_origin = {-196605, 0};

		CHECK(long_text != NULL);
		long_text[65535] = 1;
		CHECK(st_rip3_ports_erase(ports, 0) == 0);
		CHECK(st_rip3_draw_bitmap_text(ports, &font, &style, long_text, 65536, &long_origin, 7, 3,
			  ST_RIP3_COPY)
		      == 0);
		for (int32_t y = 0; y < 6; y++) {
			for (int32_t x = 0; x < 6; x++) {
				uint8_t value;
				bool    ink = y < 3 && x < 3 && (rows[y] & (0x80U >> x)) != 0;

				CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
				CHECK(value == (ink ? 7 : 0));
			}
		}
		free(long_text);
	}
	CHECK(st_rip3_ports_erase(ports, 5) == 0);
	CHECK(st_rip3_bitmap_text_measure(&font, &style, SIZE_MAX, &metrics) == IO_ERROR_OUT_OF_RANGE);
	CHECK(metrics.width == 77 && metrics.height == 88 && metrics.advance.x == 99);
	invalid           = font;
	invalid.row_bytes = SIZE_MAX;
	CHECK(st_rip3_draw_bitmap_text(ports, &invalid, &style, &character, 1, &origin, 7, 3, ST_RIP3_COPY)
	      == IO_ERROR_INVALID_FORMAT);
	style.magnification = 0;
	CHECK(st_rip3_draw_bitmap_text(ports, &font, &style, &character, 1, &origin, 7, 3, ST_RIP3_COPY)
	      == IO_ERROR_OUT_OF_RANGE);
	style.magnification = 11;
	CHECK(st_rip3_bitmap_text_measure(&font, &style, 0, &metrics) == IO_ERROR_OUT_OF_RANGE);
	style.magnification = 10;
	CHECK(st_rip3_bitmap_text_measure(&font, &style, 1, &metrics) == 0 && metrics.width == 30
	      && metrics.height == 30);
	{
		const struct st_rip3_text_position magnified = {-20, -10};

		CHECK(st_rip3_draw_bitmap_text(ports, &font, &style, &character, 1, &magnified, 7, 3, ST_RIP3_COPY)
		      == 0);
		for (int32_t y = 0; y < 6; y++) {
			for (int32_t x = 0; x < 6; x++) {
				uint8_t value;

				CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == 7);
			}
		}
		CHECK(st_rip3_ports_erase(ports, 5) == 0);
	}
	style.magnification = 1;
	CHECK(st_rip3_draw_bitmap_text(ports, &font, &style, NULL, 1, &origin, 7, 3, ST_RIP3_COPY)
	      == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_draw_bitmap_text(ports, &font, &style, NULL, 0, &origin, 7, 3, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_bitmap_text_measure(&font, &style, 0, &metrics) == 0 && metrics.width == 0
	      && metrics.height == 0 && metrics.advance.x == 0);
	origin = (struct st_rip3_text_position) {INT32_MAX, INT32_MIN};
	CHECK(st_rip3_draw_bitmap_text(ports, &font, &style, &character, 1, &origin, 7, 3, ST_RIP3_COPY) == 0);
	origin         = (struct st_rip3_text_position) {INT32_MIN, INT32_MAX};
	style.vertical = true;
	CHECK(st_rip3_draw_bitmap_text(ports, &font, &style, &character, 1, &origin, 7, 3, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_activate(ports, false) == 0);
	origin = (struct st_rip3_text_position) {0, 0};
	CHECK(st_rip3_draw_bitmap_text(ports, &font, &style, &character, 1, &origin, 7, 3, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 6; y++) {
		for (int32_t x = 0; x < 6; x++) {
			uint8_t value;

			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == 5);
		}
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
original_font(void)
{
	const struct st_rip3_font_asset       *asset = NULL;
	struct st_rip3_maf                     file;
	struct st_rip3_maf_font                font;
	struct st_rip3_ports                  *ports     = NULL;
	const struct st_rip3_bitmap_text_style style     = {1, false, false, false};
	const struct st_rip3_rect              bounds    = {10, 20, 29, 35};
	const struct st_rip3_rect              view      = {2, 1, 17, 14};
	const struct st_rip3_text_position     origin    = {2, 1};
	const uint8_t                          character = 1;
	/* Pinned documentation's 640x480 8x11 CP437 smiley, not sampled from
	 * the decoder or generated from a renderer capture. */
	const unsigned          smiley[] = {0x7e, 0x81, 0xa5, 0xa5, 0x81, 0x81, 0xbd, 0x99, 0x81, 0x81, 0x7e};
	struct st_rip3_palette  palette;
	struct st_pixel_buffer *capture = NULL;

	for (size_t i = 0; i < st_rip3_font_asset_count(); i++) {
		const struct st_rip3_font_asset *candidate = st_rip3_font_asset_at(i);

		if (strcmp(candidate->filename, "RIPscrip.maf") == 0)
			asset = candidate;
	}
	CHECK(asset != NULL);
	CHECK(st_rip3_maf_open(&file, asset->data, asset->size) == 0);
	CHECK(st_rip3_maf_find(&font, &file, 640, 480, ST_RIP3_MAF_80X43) == 0);
	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ports_viewport(ports, &view) == 0);
	CHECK(st_rip3_ports_protect(ports, 1, true) == 0);
	CHECK(st_rip3_ports_erase(ports, 3) == 0);
	CHECK(st_rip3_draw_bitmap_text(ports, &font, &style, &character, 1, &origin, 7, 0, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < 480; y++) {
		for (uint32_t x = 0; x < 640; x++) {
			uint32_t expected = ST_PIXEL_COLOR_RGB_TAG;

			if (x >= 12 && x <= 27 && y >= 21 && y <= 34)
				expected = ST_PIXEL_COLOR_RGB_VALUE(0, 170, 170);
			if (x >= 14 && x < 22 && y >= 22 && y < 33 && (smiley[y - 22] & (0x80U >> (x - 14))) != 0)
				expected = ST_PIXEL_COLOR_RGB_VALUE(170, 170, 170);
			CHECK(capture->primary[(size_t)y * 640 + x] == expected);
		}
	}
	st_pixel_buffer_destroy(capture);
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
wide_positions(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_ports                  *ports           = NULL;
	uint8_t                                glyphs[255 * 3] = {0x40, 0xa0, 0xe0};
	struct st_rip3_maf_font                font            = {3, 3, 1, 3, glyphs};
	const struct st_rip3_bitmap_text_style style           = {.magnification = 1};
	const struct st_rip3_rect              viewport        = {0, 0, 7, 5};
	const uint8_t                          character       = 1;
	struct st_rip3_text_position           origin          = {-0.5, 1.5};
	const char *const expected[] = {".......#", "......#.", ".#....##", "#.#.....", "###.....", "........"};
	CHECK(st_rip3_ports_create(&ports, canvas) == 0 && st_rip3_ports_viewport(ports, &viewport) == 0
	      && st_rip3_ports_erase(ports, 9) == 0);
	CHECK(st_rip3_draw_bitmap_text(ports, &font, &style, &character, 1, &origin, 7, 3, ST_RIP3_COPY) == 0
	      && origin.x == -0.5 && origin.y == 1.5);
	origin = (struct st_rip3_text_position) {5.5, -0.5};
	CHECK(st_rip3_draw_bitmap_text(ports, &font, &style, &character, 1, &origin, 7, 3, ST_RIP3_COPY) == 0);
	const struct st_rip3_text_position skipped[] = {
	    {1099511627776.0,  1099511627776.0 },
            {-1099511627776.0, -1099511627776.0}
        };
	for (size_t i = 0; i < 2; i++)
		CHECK(
		    st_rip3_draw_bitmap_text(ports, &font, &style, &character, 1, skipped + i, 7, 3, ST_RIP3_COPY)
		    == 0);
	const struct st_rip3_text_position invalid[] = {
	    {NAN,                    0       },
            {0,                      INFINITY},
            {1024.0 * INT32_MAX + 1, 0       }
        };
	for (size_t i = 0; i < 3; i++)
		CHECK(
		    st_rip3_draw_bitmap_text(ports, &font, &style, &character, 1, invalid + i, 7, 3, ST_RIP3_COPY)
		    == IO_ERROR_OUT_OF_RANGE);
	struct st_rip3_palette  palette;
	struct st_pixel_buffer *capture = NULL;
	CHECK(st_rip3_palette_defaults(&palette) == 0 && st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++)
		for (uint32_t x = 0; x < width; x++) {
			uint8_t index = 0;
			if (x < 8 && y < 6)
				index = expected[y][x] == '#' ? 7 : 9;
			CHECK(capture->primary[(size_t)y * width + x]
			      == (ST_PIXEL_COLOR_RGB_TAG | palette.rgb[index]));
		}
	st_pixel_buffer_destroy(capture);
	st_rip3_ports_destroy(ports);
	return 0;
}

int
main(void)
{
	CHECK(wide_positions(ST_RIP3_CANVAS_640_480, 640, 480) == 0);
	CHECK(wide_positions(ST_RIP3_CANVAS_800_600, 800, 600) == 0);
	CHECK(wide_positions(ST_RIP3_CANVAS_1024_768, 1024, 768) == 0);
	CHECK(raster() == 0);
	CHECK(vertical_and_shadow() == 0);
	CHECK(errors_and_clipping() == 0);
	CHECK(original_font() == 0);
	return 0;
}
