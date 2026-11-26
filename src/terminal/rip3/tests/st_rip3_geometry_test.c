/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "io_error.h"
#include "st_rip3_geometry.h"

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

static int
examples(void)
{
	struct st_rip3_frame frame;
	struct st_rip3_point source;
	struct st_rip3_point point;
	const uint32_t       sizes[][2] = {
	    {640,  480},
            {800,  600},
            {1024, 768}
        };
	/* Literal half-frame results: the boundary maps to the device extent,
	 * not to its last pixel. Inverse input mapping uses the same dimensions. */
	const int32_t middle[][2] = {
	    {320, 240},
            {400, 300},
            {512, 384}
        };

	for (unsigned i = 0; i < 3; i++) {
		CHECK(st_rip3_frame_init(&frame, (enum st_rip3_canvas)i) == 0);
		CHECK(frame.device_width == sizes[i][0] && frame.device_height == sizes[i][1]);
		CHECK(frame.world_width == 640 && frame.world_height == 350);
		CHECK(st_rip3_frame_world(&frame, 1280, 960) == 0);
		source = (struct st_rip3_point) {640, 480};
		CHECK(st_rip3_world_to_device(&frame, &source, &point) == 0);
		CHECK(point.x == middle[i][0] && point.y == middle[i][1]);
		CHECK(st_rip3_device_to_world(&frame, &point, &point) == 0);
		CHECK(point.x == 640 && point.y == 480);
		source = (struct st_rip3_point) {1280, 960};
		CHECK(st_rip3_world_to_device(&frame, &source, &point) == 0);
		CHECK(point.x == (int32_t)sizes[i][0] && point.y == (int32_t)sizes[i][1]);
		source = (struct st_rip3_point) {0, 0};
		CHECK(st_rip3_world_to_device(&frame, &source, &point) == 0);
		CHECK(point.x == 0 && point.y == 0);
		source = (struct st_rip3_point) {-1, -1};
		CHECK(st_rip3_world_to_device(&frame, &source, &point) == 0);
		CHECK(point.x == -1 && point.y == -1);
	}
	CHECK(st_rip3_frame_init(&frame, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_frame_world(&frame, 1280, 960) == 0);
	/* The reference's precision example: adjacent logical pixels may map
	 * onto the same physical pixel when the logical frame is larger. */
	source = (struct st_rip3_point) {0, 1};
	CHECK(st_rip3_world_to_device(&frame, &source, &point) == 0);
	CHECK(point.x == 0 && point.y == 0);
	source = (struct st_rip3_point) {2, 3};
	CHECK(st_rip3_world_to_device(&frame, &source, &point) == 0);
	CHECK(point.x == 1 && point.y == 1);
	CHECK(st_rip3_frame_world(&frame, 320, 240) == 0);
	source = (struct st_rip3_point) {1, 2};
	CHECK(st_rip3_world_to_device(&frame, &source, &point) == 0);
	CHECK(point.x == 2 && point.y == 4);
	CHECK(st_rip3_frame_world(&frame, 1000, 1000) == 0);
	source = (struct st_rip3_point) {500, 500};
	CHECK(st_rip3_world_to_device(&frame, &source, &point) == 0);
	CHECK(point.x == 320 && point.y == 240);
	CHECK(st_rip3_frame_world(&frame, 32767, 32767) == 0);
	source = (struct st_rip3_point) {32766, 32766};
	CHECK(st_rip3_world_to_device(&frame, &source, &point) == 0);
	CHECK(point.x == 639 && point.y == 479);
	CHECK(st_rip3_device_to_world(&frame, &point, &point) == 0);
	CHECK(point.x == 32715 && point.y == 32698);
	CHECK(st_rip3_frame_world(&frame, 65535, 65535) == 0);
	source = (struct st_rip3_point) {32767, 32767};
	CHECK(st_rip3_world_to_device(&frame, &source, &point) == 0 && point.x == 319 && point.y == 239);
	source = (struct st_rip3_point) {65535, 65535};
	CHECK(st_rip3_world_to_device(&frame, &source, &point) == 0 && point.x == 640 && point.y == 480);
	return 0;
}

static int
failures(void)
{
	struct st_rip3_frame frame;
	struct st_rip3_frame saved;
	struct st_rip3_point input  = {INT32_MAX, INT32_MAX};
	struct st_rip3_point output = {17, 19};

	CHECK(st_rip3_frame_init(&frame, ST_RIP3_CANVAS_640_480) == 0);
	saved = frame;
	CHECK(st_rip3_frame_init(&frame, (enum st_rip3_canvas)3) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_frame_world(&frame, 0, 350) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_frame_world(&frame, 640, 65536) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_frame_world(&frame, UINT32_MAX, 350) == IO_ERROR_OUT_OF_RANGE);
	CHECK(memcmp(&frame, &saved, sizeof(frame)) == 0);
	CHECK(st_rip3_frame_world(&frame, 1, 1) == 0);
	CHECK(st_rip3_world_to_device(&frame, &input, &output) == IO_ERROR_OUT_OF_RANGE);
	CHECK(output.x == 17 && output.y == 19);
	input.x = 0;
	CHECK(st_rip3_world_to_device(&frame, &input, &output) == IO_ERROR_OUT_OF_RANGE);
	CHECK(output.x == 17 && output.y == 19);
	CHECK(st_rip3_frame_world(&frame, 32767, 32767) == 0);
	CHECK(st_rip3_device_to_world(&frame, &input, &output) == IO_ERROR_OUT_OF_RANGE);
	CHECK(output.x == 17 && output.y == 19);
	input = (struct st_rip3_point) {INT32_MIN, INT32_MIN};
	CHECK(st_rip3_device_to_world(&frame, &input, &output) == IO_ERROR_OUT_OF_RANGE);
	CHECK(output.x == 17 && output.y == 19);
	CHECK(st_rip3_world_to_device(NULL, &input, &output) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_world_to_device(&frame, NULL, &output) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_world_to_device(&frame, &input, NULL) == IO_ERROR_INVALID_ARGUMENT);
	frame.world_width = 0;
	CHECK(st_rip3_world_to_device(&frame, &input, &output) == IO_ERROR_INVALID_ARGUMENT);
	frame               = saved;
	frame.device_height = 0;
	CHECK(st_rip3_device_to_world(&frame, &input, &output) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(output.x == 17 && output.y == 19);
	return 0;
}

int
main(void)
{
	CHECK(examples() == 0);
	CHECK(failures() == 0);
	struct st_rip3_frame frame = {0};
	struct st_rip3_point point = {320, 175};
	CHECK(st_rip3_frame_init_device(&frame, 720, 400) == 0);
	CHECK(st_rip3_world_to_device(&frame, &point, &point) == 0 && point.x == 360 && point.y == 200);
	CHECK(st_rip3_device_to_world(&frame, &point, &point) == 0 && point.x == 320 && point.y == 175);
	CHECK(st_rip3_frame_init_device(&frame, 1280, 800) == 0);
	CHECK(st_rip3_world_to_device(&frame, &point, &point) == 0 && point.x == 640 && point.y == 400);
	struct st_rip3_frame saved = frame;
	CHECK(st_rip3_frame_init_device(&frame, 0, 400) == IO_ERROR_OUT_OF_RANGE
	      && memcmp(&frame, &saved, sizeof(frame)) == 0);
	CHECK(st_rip3_frame_init_device(&frame, 720, 0) == IO_ERROR_OUT_OF_RANGE
	      && memcmp(&frame, &saved, sizeof(frame)) == 0);
	CHECK(st_rip3_frame_init_device(&frame, UINT32_MAX, 400) == IO_ERROR_OUT_OF_RANGE);
	return 0;
}
