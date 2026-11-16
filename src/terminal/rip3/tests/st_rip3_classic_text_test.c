/* Independent classic-font style, layout and coherent bitmap expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_classic_text.h"
#include "st_rip3_ports.h"
#include "st_pixels.h"
#include "io_error.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static bool
same_plan(const struct st_rip3_classic_text_plan *a,
    const struct st_rip3_classic_text_plan *b)
{
	return a->advance == b->advance && a->ascent == b->ascent
	    && a->descent == b->descent && a->origin.x == b->origin.x
	    && a->origin.y == b->origin.y && a->matrix.xx == b->matrix.xx
	    && a->matrix.xy == b->matrix.xy && a->matrix.yx == b->matrix.yx
	    && a->matrix.yy == b->matrix.yy && a->advances_cursor == b->advances_cursor;
}

static int
styles(void)
{
	struct st_rip3_classic_text_style style, before;
	unsigned valid = 0;

	CHECK(st_rip3_classic_text_style_set(&style, 0, 0, 1, 0) == 0);
	for (unsigned flags = 0; flags < 64; flags++) {
		unsigned v = flags & 28;
		bool legal = (flags & 3) != 3 && (v == 0 || (v & (v - 1)) == 0);
		before = style;
		CHECK(st_rip3_classic_text_style_set(&style, 10, 1, 10, flags)
		    == (legal ? 0 : IO_ERROR_OUT_OF_RANGE));
		if (legal) valid++;
		else CHECK(style.font == before.font && style.direction == before.direction
		    && style.magnification == before.magnification && style.flags == before.flags);
	}
	CHECK(valid == 24);
	CHECK(st_rip3_classic_text_style_set(&style, 11, 0, 1, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_classic_text_style_set(&style, 0, 2, 1, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_classic_text_style_set(&style, 0, 0, 0, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_classic_text_style_set(&style, 0, 0, 11, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_classic_text_style_set(&style, 0, 0, 1, 64) == IO_ERROR_OUT_OF_RANGE);
	return 0;
}

static int
native_fonts(enum st_rip3_canvas canvas, unsigned cell_width, unsigned cell_height)
{
	struct st_rip3_classic_font font;
	struct st_rip3_classic_text_style style;
	struct st_rip3_classic_text_plan plan;
	const struct st_rip3_point anchor = {100,100};
	const uint8_t text[] = {'A','A'};

	CHECK(st_rip3_classic_font_builtin(&font, canvas, 0) == 0);
	CHECK(font.data.bitmap.width == cell_width && font.data.bitmap.height == cell_height);
	CHECK(st_rip3_classic_text_style_set(&style, 0, 0, 2, 0) == 0);
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 2, &anchor, &plan) == 0);
	CHECK(plan.advance == cell_width * 4 && plan.origin.x == 100 && plan.origin.y == 100);
	CHECK(fabs(plan.ascent - cell_height * 13.0 / 8) < 1e-10);
	CHECK(fabs(plan.ascent + plan.descent - (cell_height * 2 - 1)) < 1e-10);
	CHECK(plan.advances_cursor);
	for (unsigned number = 1; number <= 10; number++) {
		CHECK(st_rip3_classic_font_builtin(&font, canvas, number) == 0);
		for (unsigned mag = 1; mag <= 10; mag++) {
			CHECK(st_rip3_classic_text_style_set(&style, number, 0, mag, 0) == 0);
			CHECK(st_rip3_classic_text_plan(&font, &style, text, 2, &anchor, &plan) == 0);
			CHECK(plan.advance > 0 && plan.ascent >= 0 && plan.descent >= 0);
		}
	}
	return 0;
}

static int
bitmap_scene(void)
{
	uint8_t glyphs[255 * 8] = {0};
	struct st_rip3_classic_font font = {.canvas = ST_RIP3_CANVAS_640_480, .number = 0};
	struct st_rip3_classic_text_style style;
	struct st_rip3_ports *ports = NULL;
	struct st_pixel_buffer *capture = NULL;
	struct st_rip3_palette palette;
	struct st_rip3_classic_text_plan plan;
	const struct st_rip3_point anchor[] = {{20,10},{40,10},{60,10},{80,30},{100,30},{120,30}};
	const unsigned flags[] = {0,1,2,4,8,16};
	const uint8_t text[] = {'A','A'};

	/* A is a solid 3x8 cell, independent of bundled artwork. */
	memset(glyphs + ('A' - 1) * 8, 0xe0, 8);
	font.data.bitmap = (struct st_rip3_maf_font){3,8,1,8,glyphs};
	CHECK(st_rip3_ports_create(&ports, font.canvas) == 0);
	CHECK(st_rip3_ports_erase(ports, 1) == 0);
	for (unsigned i = 0; i < 6; i++) {
		CHECK(st_rip3_classic_text_style_set(&style, 0, i >= 3, 1, flags[i]) == 0);
		CHECK(st_rip3_classic_text_plan(&font, &style, text, 2, &anchor[i], &plan) == 0);
		CHECK(plan.advances_cursor == (i == 0));
		CHECK(st_rip3_draw_classic_text(ports, &font, &style, text, 2, &anchor[i],
		    12, 8, ST_RIP3_COPY) == 0);
	}
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < 480; y++) {
		for (uint32_t x = 0; x < 640; x++) {
			bool ink = y >= 10 && y < 18 && ((x >= 20 && x < 26)
			    || (x >= 34 && x < 40) || (x >= 57 && x < 63));
			ink |= y > 24 && y <= 30 && ((x >= 73 && x <= 80)
			    || (x >= 94 && x <= 101) || (x >= 117 && x <= 124));
			uint32_t expected = ST_PIXEL_COLOR_RGB_TAG | (ink ? 0xff5555 : 0x0000aa);
			CHECK(capture->primary[(size_t)y * 640 + x] == expected);
		}
	}
	st_pixel_buffer_destroy(capture);
	/* Empty text, raw zero glyph, mismatch and invalid flags cannot paint. */
	CHECK(st_rip3_classic_text_style_set(&style, 0, 0, 1, 0) == 0);
	CHECK(st_rip3_draw_classic_text(ports, &font, &style, NULL, 0, &anchor[0], 12, 8, ST_RIP3_COPY) == 0);
	const uint8_t zero = 0;
	CHECK(st_rip3_draw_classic_text(ports, &font, &style, &zero, 1, &anchor[0], 12, 8, ST_RIP3_XOR) == 0);
	uint8_t color;
	CHECK(st_rip3_ports_read(ports, 0, 20, 10, &color) == 0 && color == 12);
	CHECK(st_rip3_draw_classic_text(ports, NULL, &style, text, 2, &anchor[0], 12, 8, ST_RIP3_COPY) == IO_ERROR_INVALID_ARGUMENT);
	font.canvas = ST_RIP3_CANVAS_800_600;
	CHECK(st_rip3_draw_classic_text(ports, &font, &style, text, 2, &anchor[0], 12, 8, ST_RIP3_COPY) == IO_ERROR_CONFLICT);
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
vector_layout_and_scene(void)
{
	/* One glyph, a 4x35 rectangle, with advance 105. No encoder is involved. */
	uint8_t bytes[159] = {0};
	static const uint8_t program[] = {0x80,0, 0x84,0x80, 0x84,0xa3,
	    0x80,0xa3, 0x80,0x80, 0,0};
	struct st_rip3_classic_font font = {.canvas = ST_RIP3_CANVAS_640_480, .number = 1};
	struct st_rip3_classic_text_style style;
	struct st_rip3_classic_text_plan plan;
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	const struct st_rip3_point anchor = {100,100}, horizontal = {10,10}, vertical = {80,70};
	const uint8_t text[] = {'A','A'};
	static const double advances[] = {63,70,78.75,105,140,175,210,262.5,315,420};
	static const double heights[] = {28.8,32,36,48,64,80,96,120,144,192};

	memcpy(bytes, "PK\010\010TEST\032", 9);
	bytes[9] = 128; memcpy(bytes + 11, "TEST", 4); bytes[15] = 31;
	bytes[17] = bytes[19] = 1;
	bytes[128] = '+'; bytes[129] = 1; bytes[132] = 'A'; bytes[133] = 19;
	bytes[136] = 35; bytes[146] = 105;
	memcpy(bytes + 147, program, sizeof(program));
	CHECK(st_rip3_chr_open(&font.data.stroke, bytes, sizeof(bytes)) == 0);
	for (unsigned mag = 1; mag <= 10; mag++) {
		CHECK(st_rip3_classic_text_style_set(&style, 1, 0, mag, 0) == 0);
		CHECK(st_rip3_classic_text_plan(&font, &style, text, 1, &anchor, &plan) == 0);
		CHECK(fabs(plan.advance - advances[mag - 1]) < 1e-10);
		CHECK(fabs(plan.ascent - heights[mag - 1]) < 1e-10 && plan.descent == 0);
	}
	CHECK(st_rip3_classic_text_style_set(&style, 1, 0, 3, 0) == 0);
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 2, &anchor, &plan) == 0);
	CHECK(plan.advance == 157.5); /* No per-character integer rounding. */
	/* The other canvases scale physical dimensions, independently of world size. */
	CHECK(st_rip3_classic_text_style_set(&style, 1, 0, 4, 0) == 0);
	font.canvas = ST_RIP3_CANVAS_800_600;
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 1, &anchor, &plan) == 0);
	CHECK(plan.advance == 131.25 && plan.ascent == 60);
	font.canvas = ST_RIP3_CANVAS_1024_768;
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 1, &anchor, &plan) == 0);
	CHECK(plan.advance == 168 && fabs(plan.ascent - 76.8) < 1e-10);
	font.canvas = ST_RIP3_CANVAS_640_480;
	/* Rejected placement and invalid styles preserve a complete prior plan. */
	struct st_rip3_classic_text_plan saved = plan;
	const struct st_rip3_point extreme = {INT32_MIN,INT32_MIN};
	CHECK(st_rip3_classic_text_style_set(&style, 1, 0, 4, 1) == 0);
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 1, &extreme, &plan) == IO_ERROR_OUT_OF_RANGE);
	CHECK(same_plan(&plan, &saved));
	style.flags = 3;
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 1, &anchor, &plan) == IO_ERROR_OUT_OF_RANGE);
	CHECK(same_plan(&plan, &saved));
	style.font = 2; style.flags = 0;
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 1, &anchor, &plan) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(same_plan(&plan, &saved));
	CHECK(st_rip3_ports_create(&ports, font.canvas) == 0);
	CHECK(st_rip3_ports_erase(ports, 1) == 0);
	CHECK(st_rip3_classic_text_style_set(&style, 1, 0, 4, 0) == 0);
	CHECK(st_rip3_draw_classic_text(ports, &font, &style, text, 1, &horizontal,
	    12, 8, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_classic_text_style_set(&style, 1, 1, 4, 0) == 0);
	CHECK(st_rip3_draw_classic_text(ports, &font, &style, text, 1, &vertical,
	    12, 8, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < 480; y++) {
		for (uint32_t x = 0; x < 640; x++) {
			bool ink = x >= 10 && x <= 14 && y >= 10 && y <= 58
			    && (x == 10 || x == 14 || y == 10 || y == 58);
			ink |= x >= 80 && x <= 128 && y >= 66 && y <= 70
			    && (x == 80 || x == 128 || y == 66 || y == 70);
			CHECK(capture->primary[(size_t)y * 640 + x]
			    == (ST_PIXEL_COLOR_RGB_TAG | (ink ? 0xff5555 : 0x0000aa)));
		}
	}
	st_pixel_buffer_destroy(capture);
	/* A nonzero file baseline must be removed from justification offsets. */
	bytes[137] = 5; bytes[138] = 249; /* descender -7 */
	CHECK(st_rip3_chr_open(&font.data.stroke, bytes, sizeof(bytes)) == 0);
	CHECK(st_rip3_classic_text_style_set(&style, 1, 0, 4, 8) == 0);
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 1, &anchor, &plan) == 0 && plan.origin.y == 107);
	CHECK(plan.advances_cursor);
	CHECK(st_rip3_classic_text_style_set(&style, 1, 0, 4, 4) == 0);
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 1, &anchor, &plan) == 0 && plan.origin.y == 90);
	CHECK(st_rip3_classic_text_style_set(&style, 1, 0, 4, 16) == 0);
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 1, &anchor, &plan) == 0 && plan.origin.y == 119);
	CHECK(st_rip3_ports_erase(ports, 1) == 0);
	bytes[155] = 0; /* Active SCAN instead of the last DRAW. */
	CHECK(st_rip3_chr_open(&font.data.stroke, bytes, sizeof(bytes)) == 0);
	CHECK(st_rip3_draw_classic_text(ports, &font, &style, text, 1, &horizontal,
	    12, 8, ST_RIP3_COPY) == IO_ERROR_NOT_SUPPORTED);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	for (size_t i = 0; i < 640U * 480U; i++)
		CHECK(capture->primary[i] == (ST_PIXEL_COLOR_RGB_TAG | 0x0000aa));
	st_pixel_buffer_destroy(capture);
	st_rip3_ports_destroy(ports);
	return 0;
}

int
main(void)
{
	CHECK(styles() == 0);
	CHECK(native_fonts(ST_RIP3_CANVAS_640_480, 8, 11) == 0);
	CHECK(native_fonts(ST_RIP3_CANVAS_800_600, 10, 13) == 0);
	CHECK(native_fonts(ST_RIP3_CANVAS_1024_768, 12, 17) == 0);
	CHECK(bitmap_scene() == 0);
	CHECK(vector_layout_and_scene() == 0);
	return 0;
}
