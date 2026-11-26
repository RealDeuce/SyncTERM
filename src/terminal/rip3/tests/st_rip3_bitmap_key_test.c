/* Independently authored color-key/index expectations from the adopted delta. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "io_error.h"
#include "st_pixels.h"
#include "st_rip3_bitmap_stamp.h"
#include "st_rip3_ports.h"

extern bool st_rip3_bitmap_stamp_test_fail_allocate;

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

static void
gray(struct st_rip3_palette *palette)
{
	for (unsigned i = 0; i < 256; i++)
		palette->rgb[i] = i * UINT32_C(0x010101);
}

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, unsigned depth, bool zero_key)
{
	struct st_rip3_ports     *ports = NULL;
	struct st_rip3_palette    palette;
	struct st_rip3_bitmap    *capture  = NULL;
	uint32_t                  codes[4] = {0, 1, 2, 3};
	struct st_pixel_buffer    pixels   = {.width = 4, .height = 1, .primary = codes};
	struct st_rip3_bitmap     bitmap   = {.pixels = &pixels, .bit_depth = depth};
	const struct st_rip3_rect bounds = {10, 20, 21, 28}, viewport = {2, 1, 9, 5};
	/* Inverted indexed palette, with duplicate white entries. For 4/8-bit
	 * key255, source indices 0 AND 2 map to the key. 1-bit selects raw bits
	 * by zero/nonzero key instead of consulting the inverted RGB palette. */
	const uint8_t indexed[5][4] = {
	    {85, 0,   85, 254},
            {85, 85,  85, 171},
            {85, 85,  85, 255},
            {85, 0,   85, 84 },
            {85, 255, 85, 1  }
        };
	const uint8_t mono[5][4] = {
	    {255, 85, 255, 85},
            {170, 85, 170, 85},
            {255, 85, 255, 85},
            {85,  85, 85,  85},
            {0,   85, 0,   85}
        };
	const uint8_t mono_zero[5][4] = {
	    {85, 0,   85, 0  },
            {85, 85,  85, 85 },
            {85, 85,  85, 85 },
            {85, 0,   85, 0  },
            {85, 255, 85, 255}
        };
	/* Exact RGB128 keys, nearby RGB(128,128,129) remains opaque even though
	 * it maps to the SAME output index128. 32-bit retains its no-match defect. */
	const uint8_t rgb[5][4] = {
	    {85, 128, 0,   255},
            {85, 213, 85,  170},
            {85, 213, 85,  255},
            {85, 0,   0,   85 },
            {85, 127, 255, 0  }
        };
	const uint8_t rgb32[5][4] = {
	    {128, 128, 0,   255},
            {213, 213, 85,  170},
            {213, 213, 85,  255},
            {0,   0,   0,   85 },
            {127, 127, 255, 0  }
        };
	const uint8_t rgb16[5][4] = {
	    {131, 0,   255, 16 },
            {214, 85,  170, 69 },
            {215, 85,  255, 85 },
            {1,   0,   85,  16 },
            {124, 255, 0,   239}
        };
	const uint8_t (*expected)[4] = depth == 1    ? zero_key ? mono_zero : mono
	                               : depth == 16 ? rgb16
	                               : depth == 24 ? rgb
	                               : depth == 32 ? rgb32
	                                             : indexed;
	uint8_t       key            = depth >= 24 ? 128 : zero_key ? 0 : 255;
	gray(&palette);
	if (depth <= 8) {
		bitmap.palette_size = depth == 1 ? 2 : 4;
		bitmap.palette[0] = bitmap.palette[2] = 0xffffff;
		bitmap.palette[1]                     = 0;
		bitmap.palette[3]                     = 0xfefefe;
		if (depth == 1) {
			codes[2] = 0;
			codes[3] = 1;
		}
	}
	else {
		const uint32_t source[]   = {0x80808080, 0x80808081, 0x80000000, 0x80ffffff};
		const uint32_t source16[] = {0x80838383, 0x80000000, 0x80ffffff, 0x80101010};
		memcpy(codes, depth == 16 ? source16 : source, sizeof(codes));
	}
	uint32_t original[4];
	memcpy(original, codes, sizeof(original));
	CHECK(st_rip3_ports_create(&ports, canvas) == 0 && st_rip3_ports_define(ports, 1, &bounds, false) == 0
	      && st_rip3_ports_switch(ports, 1) == 0 && st_rip3_ports_viewport(ports, &viewport) == 0
	      && st_rip3_ports_protect(ports, 1, true) == 0);
	/* Native4, up8, down2, and left-clipped up8. Source sample positions
	 * are authored explicitly; the implementation does not generate them. */
	const int32_t  left[] = {0, 0, 0, -2}, right[] = {3, 7, 1, 5};
	const unsigned visible[]     = {4, 8, 2, 6};
	const uint8_t  samples[4][8] = {
	    {0, 1, 2, 3},
            {0, 0, 1, 1, 2, 2, 3, 3},
            {1, 3},
            {1, 1, 2, 2, 3, 3}
        };
	for (unsigned scale = 0; scale < 4; scale++) {
		CHECK(st_rip3_ports_erase(ports, 85) == 0);
		for (unsigned mode = 0; mode < 5; mode++) {
			const struct st_rip3_bitmap_destination area = {left[scale], mode, right[scale], mode};
			CHECK(st_rip3_draw_bitmap(ports, &bitmap, &area, &palette, ST_RIP3_BITMAP_MATCH,
				  (enum st_rip3_transfer_mode)mode, depth == 16 ? NULL : &key)
			      == 0);
		}
		CHECK(st_rip3_bitmap_capture(&capture, ports, 0, NULL, &palette) == 0);
		CHECK(capture->pixels->width == width && capture->pixels->height == height);
		for (uint32_t y = 0; y < height; y++)
			for (uint32_t x = 0; x < width; x++) {
				unsigned wanted = 0;
				if (x >= 12 && x <= 19 && y >= 21 && y <= 25) {
					unsigned vx = x - 12;
					wanted = vx < visible[scale] ? expected[y - 21][samples[scale][vx]] : 85;
				}
				CHECK(capture->pixels->primary[(size_t)y * width + x] == wanted);
			}
		CHECK(memcmp(capture->palette, palette.rgb, sizeof(palette.rgb)) == 0);
		st_rip3_bitmap_destroy(capture);
		capture = NULL;
	}
	CHECK(memcmp(codes, original, sizeof(codes)) == 0);
	struct st_rip3_port_info info;
	unsigned                 current;
	CHECK(st_rip3_ports_info(ports, 1, &info) == 0 && info.protected
	      && memcmp(&info.viewport, &viewport, sizeof(viewport)) == 0);
	CHECK(st_rip3_ports_current(ports, &current) == 0 && current == 1);
	if (depth == 16) {
		CHECK(st_rip3_ports_erase(ports, 85) == 0);
		st_rip3_bitmap_stamp_test_fail_allocate      = true;
		const struct st_rip3_bitmap_destination area = {0, 0, 3, 0};
		CHECK(st_rip3_draw_bitmap(ports, &bitmap, &area, &palette, ST_RIP3_BITMAP_MATCH,
			  ST_RIP3_TRANSFER_COPY, &key)
		          == IO_ERROR_NOT_SUPPORTED
		      && st_rip3_bitmap_stamp_test_fail_allocate);
		st_rip3_bitmap_stamp_test_fail_allocate = false;
		for (int32_t y = 1; y <= 5; y++)
			for (int32_t x = 2; x <= 9; x++) {
				uint8_t value;
				CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == 85);
			}
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
translation(void)
{
	struct st_rip3_palette palette;
	gray(&palette);
	palette.rgb[200] = palette.rgb[240] = palette.rgb[7];
	palette.rgb[236]                    = palette.rgb[235];
	palette.rgb[255]                    = palette.rgb[0];
	const unsigned limits[]             = {2, 16, 236};
	for (size_t i = 0; i < sizeof(limits) / sizeof(*limits); i++)
		for (unsigned key = 0; key < 256; key++) {
			uint8_t  actual   = 99;
			unsigned expected = key;
			if (key == 240 || (key == 200 && limits[i] != 236))
				expected = 7;
			if (key == 236)
				expected = 235;
			if (key == 255)
				expected = 0;
			CHECK(st_rip3_bitmap_translate_key(&palette, limits[i], key, &actual) == 0
			      && actual == expected);
		}
	const unsigned invalid[] = {0, 1, 3, 256};
	uint8_t        output    = 99;
	for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++)
		CHECK(st_rip3_bitmap_translate_key(&palette, invalid[i], 7, &output) == IO_ERROR_OUT_OF_RANGE
		      && output == 99);
	CHECK(st_rip3_bitmap_translate_key(&palette, 236, 256, &output) == IO_ERROR_OUT_OF_RANGE && output == 99);
	CHECK(st_rip3_bitmap_translate_key(NULL, 236, 7, &output) == IO_ERROR_INVALID_ARGUMENT && output == 99);
	CHECK(st_rip3_bitmap_translate_key(&palette, 236, 7, NULL) == IO_ERROR_INVALID_ARGUMENT);
	palette.rgb[255] = 0x1000000;
	CHECK(st_rip3_bitmap_translate_key(&palette, 236, 7, &output) == IO_ERROR_OUT_OF_RANGE && output == 99);
	return 0;
}

static int
dither_and_adoption(void)
{
	struct st_rip3_ports                   *ports = NULL;
	struct st_rip3_palette                  palette;
	uint32_t                                code     = 0;
	struct st_pixel_buffer                  pixels   = {.width = 1, .height = 1, .primary = &code};
	struct st_rip3_bitmap                   bitmap   = {.pixels = &pixels,
	    .bit_depth                                            = 8,
	    .palette_size                                         = 1,
	    .palette                                              = {0x808081}};
	const struct st_rip3_rect               viewport = {0, 0, 7, 7};
	const struct st_rip3_bitmap_destination area     = {0, 0, 7, 7};
	uint8_t                                 key      = 128;
	gray(&palette);
	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0
	      && st_rip3_ports_viewport(ports, &viewport) == 0);
	for (unsigned depth = 4; depth <= 8; depth += 4)
		for (unsigned mode = 0; mode < 5; mode++) {
			bitmap.bit_depth = depth;
			CHECK(st_rip3_ports_erase(ports, 85) == 0);
			CHECK(st_rip3_draw_bitmap(ports, &bitmap, &area, &palette, ST_RIP3_BITMAP_DITHER,
				  (enum st_rip3_transfer_mode)mode, &key)
			      == 0);
			for (int32_t y = 0; y < 8; y++)
				for (int32_t x = 0; x < 8; x++) {
					uint8_t actual;
					CHECK(st_rip3_ports_read(ports, 0, x, y, &actual) == 0 && actual == 85);
				}
		}
	/* Even with adoption preserving draw indexes, duplicate-RGB file entries
	 * collapse in the separate key-classification map. */
	palette.rgb[1] = palette.rgb[2] = 0x141414;
	key                             = 1;
	bitmap.palette_size             = 3;
	memcpy(bitmap.palette, palette.rgb, 3 * sizeof(uint32_t));
	bitmap.bit_depth = 4;
	for (code = 1; code <= 2; code++) {
		CHECK(st_rip3_ports_erase(ports, 85) == 0);
		CHECK(st_rip3_draw_bitmap(ports, &bitmap, &area, &palette, ST_RIP3_BITMAP_ADOPTED,
			  ST_RIP3_TRANSFER_COPY, &key)
		      == 0);
		uint8_t actual;
		CHECK(st_rip3_ports_read(ports, 0, 3, 4, &actual) == 0 && actual == 85);
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

int
main(void)
{
	const enum st_rip3_canvas canvases[] = {ST_RIP3_CANVAS_640_480, ST_RIP3_CANVAS_800_600,
	    ST_RIP3_CANVAS_1024_768};

	const uint32_t widths[] = {640, 800, 1024}, heights[] = {480, 600, 768};
	const unsigned depths[] = {4, 8, 1, 24, 32, 16};
	for (size_t c = 0; c < 3; c++) {
		for (size_t d = 0; d < sizeof(depths) / sizeof(*depths); d++)
			CHECK(scene(canvases[c], widths[c], heights[c], depths[d], false) == 0);
		CHECK(scene(canvases[c], widths[c], heights[c], 1, true) == 0);
	}
	return translation() || dither_and_adoption();
}
