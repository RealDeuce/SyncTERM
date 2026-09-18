/* Shared hot-path helpers for the pixels component. */

/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#ifndef ST_PIXELS_INTERNAL_H_
#define ST_PIXELS_INTERNAL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline bool
st_pixels_count(uint32_t width, uint32_t height, size_t element_size,
	size_t *result)
{
	if (width == 0 || height == 0 || element_size == 0
	    || (size_t)width > SIZE_MAX / (size_t)height)
		return false;
	*result = (size_t)width * height;
	return *result <= SIZE_MAX / element_size;
}

static inline bool
st_pixels_count_u32(uint32_t width, uint32_t height, size_t *result)
{
	return st_pixels_count(width, height, sizeof(uint32_t), result);
}

struct st_pixels_ycocg {
	int32_t y;
	int32_t co;
	int32_t cg;
};

static inline struct st_pixels_ycocg
st_pixels_rgb_to_ycocg(uint32_t pixel)
{
	int32_t red = (int32_t)((pixel >> 16) & UINT32_C(0xff));
	int32_t green = (int32_t)((pixel >> 8) & UINT32_C(0xff));
	int32_t blue = (int32_t)(pixel & UINT32_C(0xff));
	int32_t co = red - blue;
	int32_t temporary = blue + co / 2;
	int32_t cg = green - temporary;

	return (struct st_pixels_ycocg){temporary + cg / 2, co, cg};
}

static inline uint32_t
st_pixels_clamp_component(int32_t value)
{
	return value < 0 ? 0 : value > 255 ? 255 : (uint32_t)value;
}

static inline uint32_t
st_pixels_ycocg_to_rgb(struct st_pixels_ycocg colour)
{
	int32_t temporary = colour.y - colour.cg / 2;
	int32_t green = colour.cg + temporary;
	int32_t blue = temporary - colour.co / 2;
	int32_t red = blue + colour.co;

	return (st_pixels_clamp_component(red) << 16)
	    | (st_pixels_clamp_component(green) << 8)
	    | st_pixels_clamp_component(blue);
}

static inline int32_t
st_pixels_blend_component(int32_t left, int32_t right, uint32_t weight)
{
	int32_t factor = (int32_t)weight;
	int32_t inverse = INT32_C(65535) - factor;

	return (left * inverse + right * factor) / INT32_C(65535);
}

static inline uint32_t
st_pixels_blend(uint32_t left, uint32_t right, uint32_t weight)
{
	struct st_pixels_ycocg first;
	struct st_pixels_ycocg second;
	struct st_pixels_ycocg result;

	if (left == right || weight == 0)
		return left;
	first = st_pixels_rgb_to_ycocg(left);
	second = st_pixels_rgb_to_ycocg(right);
	result.y = st_pixels_blend_component(first.y, second.y, weight);
	result.co = st_pixels_blend_component(first.co, second.co, weight);
	result.cg = st_pixels_blend_component(first.cg, second.cg, weight);
	return st_pixels_ycocg_to_rgb(result);
}

#endif
