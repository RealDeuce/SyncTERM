/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "io_error.h"
#include "st_rip3_geometry.h"

static bool
frame_valid(const struct st_rip3_frame *frame)
{
	return frame != NULL && frame->world_width != 0 && frame->world_width <= 65535 && frame->world_height != 0
	       && frame->world_height <= 65535 && frame->device_width != 0 && frame->device_width <= INT32_MAX
	       && frame->device_height != 0 && frame->device_height <= INT32_MAX;
}

int
st_rip3_frame_init_device(struct st_rip3_frame *frame, uint32_t width, uint32_t height)
{
	if (frame == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (width == 0 || height == 0 || width > INT32_MAX || height > INT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	*frame = (struct st_rip3_frame) {width, height, 640, 350};
	return 0;
}

int
st_rip3_frame_init(struct st_rip3_frame *frame, enum st_rip3_canvas canvas)
{
	struct st_rip3_frame value = {0, 0, 640, 350};

	if (frame == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	switch (canvas) {
		case ST_RIP3_CANVAS_640_480:
			value.device_width  = 640;
			value.device_height = 480;
			break;
		case ST_RIP3_CANVAS_800_600:
			value.device_width  = 800;
			value.device_height = 600;
			break;
		case ST_RIP3_CANVAS_1024_768:
			value.device_width  = 1024;
			value.device_height = 768;
			break;
		default:
			return IO_ERROR_INVALID_ARGUMENT;
	}
	*frame = value;
	return 0;
}

int
st_rip3_frame_world(struct st_rip3_frame *frame, uint32_t width, uint32_t height)
{
	if (!frame_valid(frame))
		return IO_ERROR_INVALID_ARGUMENT;
	if (width == 0 || height == 0 || width > 65535 || height > 65535)
		return IO_ERROR_OUT_OF_RANGE;
	frame->world_width  = width;
	frame->world_height = height;
	return 0;
}

static int
scale(int32_t input, uint32_t numerator, uint32_t denominator, int32_t *result)
{
	int64_t product  = (int64_t)input * numerator;
	int64_t quotient = product / denominator;

	if (product < 0 && product % denominator != 0)
		quotient--;
	if (quotient < INT32_MIN || quotient > INT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	*result = (int32_t)quotient;
	return 0;
}

static int
map(const struct st_rip3_frame *frame, const struct st_rip3_point *input, struct st_rip3_point *result,
    bool inverse)
{
	struct st_rip3_point value;
	int                  status;

	if (!frame_valid(frame) || input == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (inverse) {
		status = scale(input->x, frame->world_width, frame->device_width, &value.x);
		if (status == 0)
			status = scale(input->y, frame->world_height, frame->device_height, &value.y);
	}
	else {
		status = scale(input->x, frame->device_width, frame->world_width, &value.x);
		if (status == 0)
			status = scale(input->y, frame->device_height, frame->world_height, &value.y);
	}
	if (status == 0)
		*result = value;
	return status;
}

int
st_rip3_world_to_device(const struct st_rip3_frame *frame, const struct st_rip3_point *world,
    struct st_rip3_point *device)
{
	return map(frame, world, device, false);
}

int
st_rip3_device_to_world(const struct st_rip3_frame *frame, const struct st_rip3_point *device,
    struct st_rip3_point *world)
{
	return map(frame, device, world, true);
}
