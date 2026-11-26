/* Independent classic-font style, layout and coherent bitmap expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io_error.h"
#include "st_pixels.h"
#include "st_rip3_classic_text.h"
#include "st_rip3_outline_font.h"
#include "st_rip3_outline_layout.h"
#include "st_rip3_ports.h"

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

static bool
same_plan(const struct st_rip3_classic_text_plan *a, const struct st_rip3_classic_text_plan *b)
{
	return a->advance == b->advance && a->integer_advance == b->integer_advance && a->ascent == b->ascent
	       && a->descent == b->descent && a->origin.x == b->origin.x && a->origin.y == b->origin.y
	       && a->matrix.xx == b->matrix.xx && a->matrix.xy == b->matrix.xy && a->matrix.yx == b->matrix.yx
	       && a->matrix.yy == b->matrix.yy;
}

static int
styles(void)
{
	struct st_rip3_classic_text_style style, before;
	unsigned                          valid = 0;

	CHECK(st_rip3_classic_text_style_set(&style, 0, 0, 1, 0) == 0);
	CHECK(style.flags == 9);
	CHECK(st_rip3_classic_text_style_set(&style, 0, 1, 1, 0) == 0 && style.flags == 10);
	for (unsigned flags = 0; flags < 256; flags++) {
		unsigned h = flags % 8, v = (flags / 8) % 16;
		bool     legal =
		    (h == 0 || h == 1 || h == 2 || h == 4) && (v == 0 || v == 1 || v == 2 || v == 4 || v == 8);
		before = style;
		CHECK(st_rip3_classic_text_style_set(&style, 10, 1, 10, flags)
		      == (legal ? 0 : IO_ERROR_OUT_OF_RANGE));
		if (legal)
			valid++;
		else
			CHECK(style.font == before.font && style.direction == before.direction
			      && style.magnification == before.magnification && style.flags == before.flags);
	}
	CHECK(valid == 40);
	CHECK(st_rip3_classic_text_style_set(&style, 11, 0, 1, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_classic_text_style_set(&style, 0, 2, 1, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_classic_text_style_set(&style, 0, 0, 0, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_classic_text_style_set(&style, 0, 0, 11, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_classic_text_style_set(&style, 0, 0, 1, 4096) == IO_ERROR_OUT_OF_RANGE);
	return 0;
}

static int
default_outline(void)
{
	static const struct {
		uint32_t                          width, height, world_width, world_height;
		struct st_rip3_classic_text_style style;
		unsigned                          points, spacing;
		const char                       *text;
		struct st_rip3_text_position      anchor, first, last;
		int64_t                           advance;
	} vectors[] = {
	    {640,  480, 1280, 960,  {0, 0, 2, 9},  22, 107, "B.Pommenville 95-2", {488, 444}, {488, 452}, {607, 452},
	     126                                                                                                        },
	    {800,  600, 800,  600,  {0, 0, 5, 65}, 56, 104, "AB",                 {100, 80},  {100, 94},  {140, 94},  80},
	    {800,  600, 800,  1200, {0, 1, 5, 0},  56, 104, "AB",                 {100, 80},  {142, 118}, {142, 99},  38},
	    {1024, 768, 1024, 768,  {0, 0, 1, 0},  11, 109, "AB",                 {100, 80},  {100, 88},  {107, 88},  14},
	    {640,  481, 640,  481,  {0, 0, 1, 36}, 11, 108, "AB",                 {100, 80},  {93, 80},   {100, 80},  14},
	};

	for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
		struct st_rip3_frame          frame;
		struct st_rip3_outline_font   font;
		struct st_rip3_outline_layout layout;
		CHECK(st_rip3_frame_init_device(&frame, vectors[i].width, vectors[i].height) == 0);
		CHECK(st_rip3_frame_world(&frame, vectors[i].world_width, vectors[i].world_height) == 0);
		int status = st_rip3_classic_default_outline(&font, &layout, &vectors[i].style, &frame);
		if (status != 0)
			fprintf(stderr, "DEFAULT EX layout vector %zu returned %d\n", i, status);
		CHECK(status == 0);
		CHECK(strcmp(font.style.name, "DEFAULT Ex") == 0 && font.style.horizontal_percent == 140
		      && !font.style.hollow && font.points == vectors[i].points
		      && layout.spacing == vectors[i].spacing);
		const uint8_t               *text   = (const uint8_t *)vectors[i].text;
		size_t                       length = strlen(vectors[i].text);
		int32_t                      widths[18];
		struct st_rip3_text_position positions[18];
		int64_t                      total;
		CHECK(length <= sizeof(widths) / sizeof(widths[0]));
		CHECK(st_rip3_outline_font_advances(widths, &font, vectors[i].style.direction * 900, text, length)
		      == 0);
		CHECK(st_rip3_outline_layout_positions(&layout, &vectors[i].anchor, widths, length, 0, positions,
			  &total)
		      == 0);
		CHECK(total == vectors[i].advance && positions[0].x == vectors[i].first.x
		      && positions[0].y == vectors[i].first.y && positions[length - 1].x == vectors[i].last.x
		      && positions[length - 1].y == vectors[i].last.y);
	}
	return 0;
}

static int
native_fonts(enum st_rip3_canvas canvas, unsigned cell_width, unsigned cell_height)
{
	struct st_rip3_classic_font        font;
	struct st_rip3_classic_text_style  style;
	struct st_rip3_classic_text_plan   plan;
	const struct st_rip3_text_position anchor = {100, 100};
	const uint8_t                      text[] = {'A', 'A'};

	CHECK(st_rip3_classic_font_builtin(&font, canvas, 0) == 0);
	CHECK(font.data.bitmap.width == cell_width && font.data.bitmap.height == cell_height);
	CHECK(st_rip3_classic_text_style_set(&style, 0, 0, 2, 0) == 0);
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 2, &anchor, &plan) == 0);
	CHECK(plan.advance == cell_width * 4 && plan.origin.x == 100 && plan.origin.y == 100);
	CHECK(fabs(plan.ascent - cell_height * 13.0 / 8) < 1e-10);
	CHECK(fabs(plan.ascent + plan.descent - (cell_height * 2 - 1)) < 1e-10);
	CHECK(st_rip3_classic_text_plan(&font, &style, NULL, 0, &anchor, &plan) == 0);
	CHECK(plan.advance == 0 && plan.cell_height == cell_height * 2);
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
	uint8_t                            glyphs[255 * 8] = {0};
	struct st_rip3_classic_font        font            = {.canvas = ST_RIP3_CANVAS_640_480, .number = 0};
	struct st_rip3_classic_text_style  style;
	struct st_rip3_ports              *ports   = NULL;
	struct st_pixel_buffer            *capture = NULL;
	struct st_rip3_palette             palette;
	struct st_rip3_classic_text_plan   plan;
	const struct st_rip3_text_position anchor[] = {
	    {20,  10},
            {40,  10},
            {60,  10},
            {80,  30},
            {100, 30},
            {120, 30}
        };
	const unsigned flags[] = {0, 2, 4, 17, 33, 65};
	const uint8_t  text[]  = {'A', 'A'};

	/* A is a solid 3x8 cell, independent of bundled artwork. */
	memset(glyphs + ('A' - 1) * 8, 0xe0, 8);
	font.data.bitmap = (struct st_rip3_maf_font) {3, 8, 1, 8, glyphs};
	CHECK(st_rip3_ports_create(&ports, font.canvas) == 0);
	CHECK(st_rip3_ports_erase(ports, 1) == 0);
	for (unsigned i = 0; i < 6; i++) {
		CHECK(st_rip3_classic_text_style_set(&style, 0, i >= 3, 1, flags[i]) == 0);
		CHECK(st_rip3_classic_text_plan(&font, &style, text, 2, &anchor[i], &plan) == 0);
		CHECK(st_rip3_draw_classic_text(ports, &font, &style, text, 2, &anchor[i], 12, 8, ST_RIP3_COPY)
		      == 0);
	}
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < 480; y++) {
		for (uint32_t x = 0; x < 640; x++) {
			bool ink = y >= 10 && y < 18
			           && ((x >= 20 && x < 26) || (x >= 34 && x < 40) || (x >= 57 && x < 63));
			ink |= y > 24 && y <= 30
			       && ((x >= 73 && x <= 80) || (x >= 94 && x <= 101) || (x >= 117 && x <= 124));
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
	CHECK(st_rip3_draw_classic_text(ports, NULL, &style, text, 2, &anchor[0], 12, 8, ST_RIP3_COPY)
	      == IO_ERROR_INVALID_ARGUMENT);
	font.canvas = ST_RIP3_CANVAS_800_600;
	CHECK(st_rip3_draw_classic_text(ports, &font, &style, text, 2, &anchor[0], 12, 8, ST_RIP3_COPY)
	      == IO_ERROR_CONFLICT);
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
vector_layout_and_scene(void)
{
	/* One glyph, a 4x35 rectangle, with advance 105. No encoder is involved. */
	uint8_t                     bytes[159] = {0};
	static const uint8_t        program[]  = {0x80, 0, 0x84, 0x80, 0x84, 0xa3, 0x80, 0xa3, 0x80, 0x80, 0, 0};
	struct st_rip3_classic_font font       = {.canvas = ST_RIP3_CANVAS_640_480, .number = 1};
	struct st_rip3_classic_text_style  style;
	struct st_rip3_classic_text_plan   plan;
	struct st_rip3_ports              *ports = NULL;
	struct st_rip3_palette             palette;
	struct st_pixel_buffer            *capture = NULL;
	const struct st_rip3_text_position anchor = {100, 100}, horizontal = {10, 10}, vertical = {80, 70};
	const uint8_t                      text[]     = {'A', 'A'};
	static const double                advances[] = {63, 70, 78.75, 105, 140, 175, 210, 262.5, 315, 420};
	static const double                heights[]  = {28.8, 32, 36, 48, 64, 80, 96, 120, 144, 192};
	static const unsigned              inclusive_heights[] = {29, 33, 37, 49, 65, 81, 97, 121, 145, 193};

	memcpy(bytes, "PK\010\010TEST\032", 9);
	bytes[9] = 128;
	memcpy(bytes + 11, "TEST", 4);
	bytes[15] = 31;
	bytes[17] = bytes[19] = 1;
	bytes[128]            = '+';
	bytes[129]            = 1;
	bytes[132]            = 'A';
	bytes[133]            = 19;
	bytes[136]            = 35;
	bytes[146]            = 105;
	memcpy(bytes + 147, program, sizeof(program));
	CHECK(st_rip3_chr_open(&font.data.stroke, bytes, sizeof(bytes)) == 0);
	for (unsigned mag = 1; mag <= 10; mag++) {
		CHECK(st_rip3_classic_text_style_set(&style, 1, 0, mag, 0) == 0);
		CHECK(st_rip3_classic_text_plan(&font, &style, text, 1, &anchor, &plan) == 0);
		CHECK(fabs(plan.advance - advances[mag - 1]) < 1e-10);
		CHECK(fabs(plan.ascent - heights[mag - 1]) < 1e-10 && plan.descent == 0);
		CHECK(plan.cell_height == inclusive_heights[mag - 1]);
	}
	CHECK(st_rip3_classic_text_style_set(&style, 1, 0, 3, 0) == 0);
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 2, &anchor, &plan) == 0);
	CHECK(plan.advance == 157.5); /* No per-character integer rounding. */
	struct st_rip3_classic_text_style aligned = style;
	struct st_rip3_classic_text_plan  fractional;
	struct st_rip3_text_position      fractional_anchor = {100.25, 100.25};
	aligned.flags                                       = 2;
	CHECK(st_rip3_classic_text_plan(&font, &aligned, text, 1, &fractional_anchor, &fractional) == 0
	      && fractional.advance == 78.75 && fractional.origin.x == 22 && fractional.origin.y == 136);
	fractional_anchor.x = 78.25;
	CHECK(st_rip3_classic_text_plan(&font, &aligned, text, 1, &fractional_anchor, &fractional) == 0
	      && fractional.origin.x == 0); /* -0.5 rounds toward the positive tie. */
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
	struct st_rip3_classic_text_plan   saved   = plan;
	const struct st_rip3_text_position extreme = {-1024.0 * INT32_MAX, -1024.0 * INT32_MAX};
	CHECK(st_rip3_classic_text_style_set(&style, 1, 0, 4, 2) == 0);
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 1, &extreme, &plan) == IO_ERROR_OUT_OF_RANGE);
	CHECK(same_plan(&plan, &saved));
	style.flags = 3;
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 1, &anchor, &plan) == IO_ERROR_OUT_OF_RANGE);
	CHECK(same_plan(&plan, &saved));
	style.font  = 2;
	style.flags = 0;
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 1, &anchor, &plan) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(same_plan(&plan, &saved));
	CHECK(st_rip3_ports_create(&ports, font.canvas) == 0);
	CHECK(st_rip3_ports_erase(ports, 1) == 0);
	CHECK(st_rip3_classic_text_style_set(&style, 1, 0, 4, 0) == 0);
	CHECK(st_rip3_draw_classic_text(ports, &font, &style, text, 1, &horizontal, 12, 8, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_classic_text_style_set(&style, 1, 1, 4, 0) == 0);
	CHECK(st_rip3_draw_classic_text(ports, &font, &style, text, 1, &vertical, 12, 8, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < 480; y++) {
		for (uint32_t x = 0; x < 640; x++) {
			bool ink =
			    x >= 10 && x <= 14 && y >= 10 && y <= 58 && (x == 10 || x == 14 || y == 10 || y == 58);
			ink |= x >= 80 && x <= 128 && y >= 66 && y <= 70
			       && (x == 80 || x == 128 || y == 66 || y == 70);
			CHECK(capture->primary[(size_t)y * 640 + x]
			      == (ST_PIXEL_COLOR_RGB_TAG | (ink ? 0xff5555 : 0x0000aa)));
		}
	}
	st_pixel_buffer_destroy(capture);
	/* A nonzero file baseline must be removed from justification offsets. */
	bytes[137] = 5;
	bytes[138] = 249; /* descender -7 */
	CHECK(st_rip3_chr_open(&font.data.stroke, bytes, sizeof(bytes)) == 0);
	CHECK(st_rip3_classic_text_style_set(&style, 1, 0, 4, 32) == 0);
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 1, &anchor, &plan) == 0 && plan.origin.y == 107);
	CHECK(st_rip3_classic_text_style_set(&style, 1, 0, 4, 16) == 0);
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 1, &anchor, &plan) == 0 && plan.origin.y == 90);
	CHECK(st_rip3_classic_text_style_set(&style, 1, 0, 4, 64) == 0);
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 1, &anchor, &plan) == 0 && plan.origin.y == 119);
	CHECK(st_rip3_ports_erase(ports, 1) == 0);
	bytes[155] = 0; /* Replace the closing DRAW with SCAN. */
	CHECK(st_rip3_chr_open(&font.data.stroke, bytes, sizeof(bytes)) == 0);
	CHECK(st_rip3_classic_text_style_set(&style, 1, 0, 4, ST_RIP3_CLASSIC_BASELINE) == 0);
	CHECK(st_rip3_draw_classic_text(ports, &font, &style, text, 1, &anchor, 12, 8, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	for (unsigned y = 0; y < 480; y++) {
		for (unsigned x = 0; x < 640; x++) {
			bool ink =
			    x >= 100 && x <= 104 && y >= 59 && y <= 107 && (y == 59 || x == 104 || y == 107);
			CHECK(capture->primary[y * 640 + x]
			      == (ST_PIXEL_COLOR_RGB_TAG | (ink ? 0xff5555 : 0x0000aa)));
		}
	}
	st_pixel_buffer_destroy(capture);
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
wide_positions(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_classic_font       font  = {.canvas = canvas, .number = 0};
	struct st_rip3_classic_text_style style = {0, 0, 1, 0};
	struct st_rip3_classic_text_plan  plan;
	struct st_rip3_text_position      far    = {4294967300.25, 2};
	uint8_t                          *glyphs = calloc(255, 8192), *text = malloc(32769);
	struct st_rip3_ports             *ports = NULL;
	CHECK(glyphs != NULL && text != NULL);
	font.data.bitmap = (struct st_rip3_maf_font) {65534, 1, 8192, 8192, glyphs};
	glyphs[0]        = 0x90;
	memset(text, ' ', 32769);
	text[0] = 1;
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 1, &far, &plan) == 0 && plan.origin.x == 4294967300.0
	      && plan.origin.y == 2 && plan.advance == 65534);
	/* 32769 cells of 65534 pixels advance 2147483646 pixels. The far anchor
	 * right-justifies back to device x=2; clipping must preserve those glyph
	 * columns instead of rejecting/narrowing the original anchor. */
	far         = (struct st_rip3_text_position) {2147483648.25, 2};
	style.flags = 2;
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 32769, &far, &plan) == 0
	      && plan.advance == 2147483646.0 && plan.origin.x == 2 && plan.origin.y == 2);
	CHECK(st_rip3_ports_create(&ports, canvas) == 0);
	const struct st_rip3_rect viewport = {0, 0, 7, 4};
	CHECK(st_rip3_ports_viewport(ports, &viewport) == 0 && st_rip3_ports_erase(ports, 9) == 0);
	CHECK(st_rip3_draw_classic_text(ports, &font, &style, text, 32769, &far, 7, 3, ST_RIP3_COPY) == 0);
	struct st_rip3_palette  palette;
	struct st_pixel_buffer *capture = NULL;
	CHECK(st_rip3_palette_defaults(&palette) == 0 && st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++)
		for (uint32_t x = 0; x < width; x++) {
			uint8_t expected = x < 8 && y < 5 ? 9 : 0;
			if (y == 2 && (x == 2 || x == 5))
				expected = 7;
			CHECK(capture->primary[(size_t)y * width + x]
			      == (ST_PIXEL_COLOR_RGB_TAG | palette.rgb[expected]));
		}
	struct st_rip3_classic_text_plan saved = plan;
	far                                    = (struct st_rip3_text_position) {NAN, 0};
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 1, &far, &plan) == IO_ERROR_OUT_OF_RANGE
	      && same_plan(&plan, &saved));
	far = (struct st_rip3_text_position) {0, INFINITY};
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 1, &far, &plan) == IO_ERROR_OUT_OF_RANGE
	      && same_plan(&plan, &saved));
	far = (struct st_rip3_text_position) {1024.0 * INT32_MAX + 1, 0};
	CHECK(st_rip3_classic_text_plan(&font, &style, text, 1, &far, &plan) == IO_ERROR_OUT_OF_RANGE
	      && same_plan(&plan, &saved));
	st_pixel_buffer_destroy(capture);
	st_rip3_ports_destroy(ports);
	free(glyphs);
	free(text);
	return 0;
}

int
main(void)
{
	CHECK(wide_positions(ST_RIP3_CANVAS_640_480, 640, 480) == 0);
	CHECK(wide_positions(ST_RIP3_CANVAS_800_600, 800, 600) == 0);
	CHECK(wide_positions(ST_RIP3_CANVAS_1024_768, 1024, 768) == 0);
	CHECK(styles() == 0);
	CHECK(default_outline() == 0);
	CHECK(native_fonts(ST_RIP3_CANVAS_640_480, 8, 11) == 0);
	CHECK(native_fonts(ST_RIP3_CANVAS_800_600, 10, 13) == 0);
	CHECK(native_fonts(ST_RIP3_CANVAS_1024_768, 12, 17) == 0);
	CHECK(bitmap_scene() == 0);
	CHECK(vector_layout_and_scene() == 0);
	return 0;
}
