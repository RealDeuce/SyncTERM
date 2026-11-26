/* Literal policy/placement oracles from the pinned ordinary-text reference. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "io_error.h"
#include "st_rip3_outline_layout.h"
#include "st_rip3_styles.h"

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

static struct st_rip3_outline_style
normal(void)
{
	return (struct st_rip3_outline_style) {.size = 25, .spacing = 100};
}

static int
branches(void)
{
	static const struct st_rip3_point direction[] = {
	    {1,  0 },
            {0,  -1},
            {-1, 0 },
            {0,  1 }
        };
	static const struct st_rip3_point baseline[][4] = {
	    {{0, 11}, {11, 0}, {0, 0},   {0, 0}},
	    {{11, 0}, {11, 0}, {0, 0},   {0, 0}},
	    {{0, 0},  {11, 0}, {0, -11}, {0, 0}},
	    {{0, 11}, {0, 0},  {0, 0},   {0, 0}},
	};
	static const unsigned orientation[] = {0, 3, 2, 1};
	struct st_rip3_frame  frame;
	CHECK(
	    st_rip3_frame_init(&frame, ST_RIP3_CANVAS_640_480) == 0 && st_rip3_frame_world(&frame, 320, 160) == 0);
	for (unsigned string = 0; string < 4; string++)
		for (unsigned character = 0; character < 4; character++) {
			for (unsigned justification = 0; justification < 4; justification++)
				for (unsigned negative = 0; negative < 2; negative++) {
					struct st_rip3_outline_style  style = normal();
					struct st_rip3_outline_layout layout;
					style.string_rotation    = 900 * string;
					style.character_rotation = 900 * character;
					style.baseline           = 11;
					style.flags = 0x200 | (justification << 4) | (negative ? 0x100 : 0);
					CHECK(st_rip3_outline_layout_prepare(&layout, &style, &frame) == 0);
					int sign = negative ? -1 : 1;
					CHECK(layout.direction.x == direction[string].x
					      && layout.direction.y == direction[string].y);
					CHECK(layout.character_turns == character
					      && layout.font_orientation == orientation[character]);
					CHECK(layout.baseline.x == sign * baseline[string][character].x
					      && layout.baseline.y == sign * baseline[string][character].y);
					CHECK(layout.shadow.x == 0 && layout.shadow.y == 0 && layout.spacing == 100
					      && !layout.bounding_box_spacing);
					/* B is stored device magnitude. Changing both frame ratios has no
			 * effect, even when the retained definition-time bit is set. */
					struct st_rip3_outline_layout changed;
					struct st_rip3_frame          later = frame;
					CHECK(st_rip3_frame_world(&later, 640, 480) == 0);
					CHECK(st_rip3_outline_layout_prepare(&changed, &style, &later) == 0
					      && changed.baseline.x == layout.baseline.x
					      && changed.baseline.y == layout.baseline.y);
				}
		}
	return 0;
}

static int
shadows(void)
{
	static const struct st_rip3_point offsets[] = {
	    {-5, -5},
            {5,  -5},
            {-5, 5 },
            {5,  5 }
        };
	struct st_rip3_frame frame;
	CHECK(
	    st_rip3_frame_init(&frame, ST_RIP3_CANVAS_640_480) == 0 && st_rip3_frame_world(&frame, 1000, 7) == 0);
	for (unsigned direction = 0; direction < 4; direction++)
		for (unsigned string = 0; string < 4; string++) {
			for (unsigned character = 0; character < 4; character++) {
				struct st_rip3_outline_style  style = normal();
				struct st_rip3_outline_layout layout;
				style.flags              = direction << 6;
				style.shadow             = 9;
				style.string_rotation    = 900 * string;
				style.character_rotation = 900 * character;
				CHECK(st_rip3_outline_layout_prepare(&layout, &style, &frame) == 0);
				CHECK(layout.shadow.x == offsets[direction].x
				      && layout.shadow.y == offsets[direction].y);
				style.shadow = 0;
				CHECK(st_rip3_outline_layout_prepare(&layout, &style, &frame) == 0
				      && layout.shadow.x == 0 && layout.shadow.y == 0);
			}
		}
	for (unsigned canvas = 0; canvas < 3; canvas++) {
		struct st_rip3_outline_style  style = normal();
		struct st_rip3_outline_layout layout;
		CHECK(st_rip3_frame_init(&frame, (enum st_rip3_canvas)canvas) == 0
		      && st_rip3_frame_world(&frame, 32767, 1) == 0);
		style.shadow = 1;
		style.flags  = 0xc0;
		CHECK(st_rip3_outline_layout_prepare(&layout, &style, &frame) == 0 && layout.shadow.x == 1
		      && layout.shadow.y == 1);
		CHECK(st_rip3_frame_world(&frame, 1, 32767) == 0);
		style.shadow = 4095;
		CHECK(st_rip3_outline_layout_prepare(&layout, &style, &frame) == 0
		      && layout.shadow.x == (int32_t)(4095 * frame.device_width)
		      && layout.shadow.y == layout.shadow.x);
	}
	return 0;
}

static int
metrics_and_spacing(void)
{
	int32_t metric = 77;
	CHECK(st_rip3_outline_metric_device(&metric, 1001, 72) == 0 && metric == 50);
	CHECK(st_rip3_outline_metric_device(&metric, 1001, 144) == 0 && metric == 100);
	CHECK(st_rip3_outline_metric_device(&metric, -1001, 72) == 0 && metric == -50);
	CHECK(st_rip3_outline_metric_device(&metric, INT32_MIN, 1440) == 0 && metric == INT32_MIN);
	metric = 77;
	CHECK(st_rip3_outline_metric_device(&metric, INT32_MAX, 73728) == IO_ERROR_OUT_OF_RANGE && metric == 77);
	CHECK(st_rip3_outline_metric_device(&metric, 1, 0) == IO_ERROR_OUT_OF_RANGE && metric == 77);
	CHECK(st_rip3_outline_metric_device(&metric, 1, 73729) == 0 && metric == 51);
	CHECK(st_rip3_outline_metric_device(NULL, 1, 72) == IO_ERROR_INVALID_ARGUMENT);
	struct st_rip3_frame          frame;
	struct st_rip3_outline_style  style = normal();
	struct st_rip3_outline_layout layout;
	struct st_rip3_text_position  origin     = {100.5, 80.25}, positions[3];
	const int32_t                 advances[] = {8, 5, 0};
	int64_t                       total;
	CHECK(st_rip3_frame_init(&frame, ST_RIP3_CANVAS_640_480) == 0);
	style.spacing         = 125;
	style.string_rotation = style.character_rotation = 900;
	style.baseline                                   = 7;
	style.flags                                      = 0x400;
	CHECK(st_rip3_outline_layout_prepare(&layout, &style, &frame) == 0);
	CHECK(st_rip3_outline_layout_positions(&layout, &origin, advances, 3, 12, positions, &total) == 0
	      && total == 22);
	CHECK(positions[0].x == 107.5 && positions[0].y == 80.25 && positions[1].x == 107.5
	      && positions[1].y == 69.25 && positions[2].x == 107.5 && positions[2].y == 61.25);
	style.spacing = 50;
	CHECK(st_rip3_outline_layout_prepare(&layout, &style, &frame) == 0);
	CHECK(st_rip3_outline_layout_positions(&layout, &origin, advances, 3, 12, positions, &total) == 0
	      && total == -5);
	CHECK(positions[1].y == 78.25 && positions[2].y == 79.25);

	const struct {
		int32_t  advance, bbox;
		unsigned spacing;
		bool     bbox_reference;
		int64_t  expected;
	} cases[] = {
	    {8,         12,   125,  false, 10                   },
	    {8,         12,   125,  true,  11                   },
	    {7,         12,   50,   false, 4                    },
	    {8,         1200, 100,  true,  8                    },
	    {8,         1200, 100,  false, 8                    },
	    {-7,        12,   50,   false, -4                   },
	    {7,         12,   1,    false, 1                    },
	    {7,         12,   4095, false, 286                  },
	    {INT32_MAX, 12,   4095, false, INT64_C(87939455344) },
	    {INT32_MIN, 12,   4095, false, -INT64_C(87939455385)},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		style         = normal();
		style.spacing = cases[i].spacing;
		style.flags   = cases[i].bbox_reference ? 0x400 : 0;
		CHECK(st_rip3_outline_layout_prepare(&layout, &style, &frame) == 0);
		CHECK(st_rip3_outline_layout_positions(&layout, &origin, &cases[i].advance, 1, cases[i].bbox,
			  positions, &total)
		          == 0
		      && total == cases[i].expected);
	}
	return 0;
}

static int
justification(void)
{
	/* PR94's independent fixture supplies these spans and advances explicitly;
	 * it does not require a raster backend's bbox rotation convention. */
	static const int64_t negative[4][4] = {
	    {-27, -7,  -27, -7 },
            {-39, -19, -39, -19},
            {-27, -7,  -27, -7 },
            {-39, -19, -39, -19}
        };
	static const struct st_rip3_point end_positive[] = {
	    {-23, 0  },
            {0,   11 },
            {23,  0  },
            {0,   -11}
        };
	static const struct st_rip3_point center_positive[] = {
	    {-11, 0 },
            {0,   5 },
            {11,  0 },
            {0,   -5}
        };
	static const struct st_rip3_point end_negative[4][2] = {
	    {{27, 0},  {7, 0}  },
            {{0, -39}, {0, -19}},
            {{-27, 0}, {-7, 0} },
            {{0, 39},  {0, 19} }
        };
	static const struct st_rip3_point center_negative[4][2] = {
	    {{13, 0},  {3, 0} },
            {{0, -19}, {0, -9}},
            {{-13, 0}, {-3, 0}},
            {{0, 19},  {0, 9} }
        };
	struct st_rip3_frame frame;
	CHECK(
	    st_rip3_frame_init(&frame, ST_RIP3_CANVAS_640_480) == 0 && st_rip3_frame_world(&frame, 320, 480) == 0);
	const struct st_rip3_text_position anchor = {100, 80};
	for (unsigned s = 0; s < 4; s++)
		for (unsigned r = 0; r < 4; r++)
			for (unsigned profile = 0; profile < 2; profile++)
				for (unsigned j = 0; j < 4; j++) {
					struct st_rip3_outline_style style = normal();
					style.string_rotation              = 900 * s;
					style.character_rotation           = 900 * r;
					style.flags                        = (j << 4) | (profile ? 0x400 : 0);
					style.spacing                      = profile ? 10 : 100;
					struct st_rip3_outline_layout layout;
					CHECK(st_rip3_outline_layout_prepare(&layout, &style, &frame) == 0);
					const int32_t                advances[] = {s % 2 ? 7 : 15, s % 2 ? 4 : 8};
					struct st_rip3_text_position positions[2];
					int64_t                      total;
					CHECK(st_rip3_outline_layout_positions(&layout, &anchor, advances, 2,
						  r % 2 ? 17 : 28, positions, &total)
					      == 0);
					struct st_rip3_point offset = {0, 0};
					if (j == 1)
						offset = profile ? end_negative[s][r % 2] : end_positive[s];
					else if (j == 2)
						offset = profile ? center_negative[s][r % 2] : center_positive[s];
					CHECK(total == (profile ? negative[s][r] : s % 2 ? 11 : 23));
					CHECK(positions[0].x == 100 + offset.x && positions[0].y == 80 + offset.y);
					/* Reusing the same anchor with B=11 adds the existing device table
			 * exactly once after justification, without changing total. */
					style.baseline = 11;
					CHECK(st_rip3_outline_layout_prepare(&layout, &style, &frame) == 0);
					struct st_rip3_text_position shifted[2];
					int64_t                      after;
					CHECK(st_rip3_outline_layout_positions(&layout, &anchor, advances, 2,
						  r % 2 ? 17 : 28, shifted, &after)
					          == 0
					      && after == total);
					CHECK(shifted[0].x == positions[0].x + layout.baseline.x
					      && shifted[0].y == positions[0].y + layout.baseline.y);
				}
	return 0;
}

static int
errors(void)
{
	struct st_rip3_frame          frame;
	struct st_rip3_outline_style  style = normal();
	struct st_rip3_outline_layout layout, saved;
	CHECK(st_rip3_frame_init(&frame, ST_RIP3_CANVAS_800_600) == 0);
	CHECK(st_rip3_outline_layout_prepare(&layout, &style, &frame) == 0);
	memcpy(&saved, &layout, sizeof(saved));
	for (unsigned which = 0; which < 10; which++) {
		struct st_rip3_outline_style bad     = style;
		struct st_rip3_frame         invalid = frame;
		switch (which) {
			case 0:
				bad.size = 0;
				break;
			case 1:
				bad.size = 4096;
				break;
			case 2:
				bad.spacing = 0;
				break;
			case 3:
				bad.spacing = 4096;
				break;
			case 4:
				bad.string_rotation = 901;
				break;
			case 5:
				bad.character_rotation = 3600;
				break;
			case 6:
				bad.flags = 0x1000000;
				break;
			case 7:
				bad.shadow = 4096;
				break;
			case 8:
				bad.baseline = UINT32_MAX;
				break;
			default:
				invalid.world_width = 0;
				break;
		}
		CHECK(st_rip3_outline_layout_prepare(&layout, &bad, &invalid) != 0
		      && memcmp(&layout, &saved, sizeof(layout)) == 0);
	}
	CHECK(st_rip3_outline_layout_prepare(NULL, &style, &frame) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_outline_layout_prepare(&layout, NULL, &frame) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_outline_layout_prepare(&layout, &style, NULL) == IO_ERROR_INVALID_ARGUMENT);
	struct st_rip3_text_position positions[] =
	    {
		{1, 2},
                {3, 4}
        },
				     before[2], origin = {10.5, 12.5};
	const int32_t advances[] = {INT32_MAX, 1};
	memcpy(before, positions, sizeof(before));
	int64_t total  = 123;
	layout.spacing = 4095;
	CHECK(st_rip3_outline_layout_positions(&layout, &origin, advances, 2, 12, positions, &total)
	      == IO_ERROR_OUT_OF_RANGE);
	CHECK(memcmp(positions, before, sizeof(positions)) == 0 && total == 123);
	layout = saved;
	CHECK(st_rip3_outline_layout_positions(&layout, &origin, advances, 2, -1, positions, &total)
	      == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_outline_layout_positions(&layout, &origin, advances, SIZE_MAX, 12, positions, &total)
	      == IO_ERROR_OUT_OF_RANGE);
	origin.y = NAN;
	CHECK(st_rip3_outline_layout_positions(&layout, &origin, advances, 2, 12, positions, &total)
	      == IO_ERROR_OUT_OF_RANGE);
	origin             = (struct st_rip3_text_position) {10, 12};
	layout.direction.x = 2;
	CHECK(st_rip3_outline_layout_positions(&layout, &origin, advances, 2, 12, positions, &total)
	      == IO_ERROR_OUT_OF_RANGE);
	layout               = saved;
	layout.justification = 4;
	CHECK(st_rip3_outline_layout_positions(&layout, &origin, advances, 2, 12, positions, &total)
	      == IO_ERROR_OUT_OF_RANGE);
	layout = saved;
	CHECK(st_rip3_outline_layout_positions(&layout, &origin, NULL, 2, 12, positions, &total)
	      == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_outline_layout_positions(&layout, &origin, advances, 2, 12, NULL, &total)
	      == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_outline_layout_positions(NULL, &origin, advances, 2, 12, positions, &total)
	      == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_outline_layout_positions(&layout, NULL, advances, 2, 12, positions, &total)
	      == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_outline_layout_positions(&layout, &origin, advances, 2, 12, positions, NULL)
	      == IO_ERROR_INVALID_ARGUMENT);
	CHECK(memcmp(positions, before, sizeof(positions)) == 0 && total == 123);
	CHECK(st_rip3_outline_layout_positions(&layout, &origin, NULL, 0, 12, NULL, &total) == 0 && total == 0);
	style.compatibility_s = 63;
	style.compatibility_f = 63;
	style.flags           = 0xfff800;
	CHECK(st_rip3_outline_layout_prepare(&layout, &style, &frame) == 0 && layout.direction.x == 1
	      && layout.direction.y == 0 && layout.baseline.x == 0 && layout.baseline.y == 0);
	return 0;
}

int
main(void)
{
	CHECK(branches() == 0 && shadows() == 0 && metrics_and_spacing() == 0 && justification() == 0
	      && errors() == 0);
	return 0;
}
