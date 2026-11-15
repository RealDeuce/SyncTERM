/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_bitmap.h"
#include "io_error.h"
#include "st_pixels.h"

#include <stdlib.h>

static unsigned
word(const uint8_t *data)
{
	return (unsigned)data[0] | (unsigned)data[1] << 8;
}

static uint32_t
dword(const uint8_t *data)
{
	return (uint32_t)data[0] | (uint32_t)data[1] << 8
	    | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

void
st_rip3_bitmap_destroy(struct st_rip3_bitmap *bitmap)
{
	if (bitmap == NULL) return;
	st_pixel_buffer_destroy(bitmap->pixels);
	free(bitmap);
}

int
st_rip3_bitmap_decode(struct st_rip3_bitmap **result,
	const void *bytes, size_t size)
{
	const uint8_t *data = bytes;
	struct st_rip3_bitmap *bitmap;
	uint32_t width, height, offset, length, colours = 0;
	unsigned depth;
	size_t stride;
	int status;

	if (result == NULL || data == NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (size < 54 || data[0] != 'B' || data[1] != 'M')
		return IO_ERROR_INVALID_FORMAT;
	length = dword(data + 2);
	offset = dword(data + 10);
	if (length < 54 || length > size || offset < 54 || offset > length)
		return IO_ERROR_INVALID_FORMAT;
	if (dword(data + 14) != 40 || dword(data + 30) != 0)
		return IO_ERROR_NOT_SUPPORTED;
	if (word(data + 26) != 1) return IO_ERROR_INVALID_FORMAT;
	depth = word(data + 28);
	if (depth != 1 && depth != 4 && depth != 8 && depth != 24)
		return IO_ERROR_NOT_SUPPORTED;
	width = dword(data + 18);
	height = dword(data + 22);
	if (width == 0 || height == 0) return IO_ERROR_INVALID_FORMAT;
	if (width > INT32_MAX || height > INT32_MAX)
		return IO_ERROR_NOT_SUPPORTED;
	if (width > (SIZE_MAX - 31) / depth) return IO_ERROR_OUT_OF_RANGE;
	stride = (((size_t)width * depth + 31) / 32) * 4;
	if (height > (length - offset) / stride)
		return IO_ERROR_INVALID_FORMAT;
	if (depth < 24) {
		colours = dword(data + 46);
		if (colours == 0) colours = 1U << depth;
		if (colours > (1U << depth) || colours > (offset - 54) / 4)
			return IO_ERROR_INVALID_FORMAT;
	}
	bitmap = calloc(1, sizeof(*bitmap));
	if (bitmap == NULL) return IO_ERROR_OUT_OF_MEMORY;
	bitmap->bit_depth = depth;
	bitmap->palette_size = colours;
	for (size_t i = 0; i < colours; ++i) {
		const uint8_t *entry = data + 54 + 4 * i;
		bitmap->palette[i] = (uint32_t)entry[2] << 16
		    | (uint32_t)entry[1] << 8 | entry[0];
	}
	status = st_pixel_buffer_create(&bitmap->pixels, width, height, false);
	if (status != 0) { st_rip3_bitmap_destroy(bitmap); return status; }
	for (uint32_t y = 0; y < height; ++y) {
		const uint8_t *row = data + offset + (size_t)(height - 1 - y) * stride;
		for (uint32_t x = 0; x < width; ++x) {
			uint32_t pixel;
			if (depth == 24) {
				const uint8_t *bgr = row + (size_t)x * 3;
				pixel = ST_PIXEL_COLOR_RGB_VALUE(bgr[2], bgr[1], bgr[0]);
			}
			else {
				if (depth == 1) pixel = (row[x / 8] >> (7 - (x % 8))) & 1;
				else if (depth == 4) pixel = (row[x / 2] >> ((x & 1) == 0 ? 4 : 0)) & 15;
				else pixel = row[x];
				if (pixel >= colours) {
					st_rip3_bitmap_destroy(bitmap);
					return IO_ERROR_INVALID_FORMAT;
				}
			}
			bitmap->pixels->primary[(size_t)y * width + x] = pixel;
		}
	}
	*result = bitmap;
	return 0;
}

static void
put16(uint8_t *data, unsigned value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
}

static void
put32(uint8_t *data, uint32_t value)
{
	put16(data, value & 65535);
	put16(data + 2, value >> 16);
}

int
st_rip3_bitmap_encode(uint8_t **result, size_t *result_size,
	const struct st_rip3_bitmap *bitmap)
{
	const struct st_pixel_buffer *pixels;
	unsigned depth;
	size_t stride, offset, size;
	uint8_t *data;

	if (result == NULL || result_size == NULL || bitmap == NULL
	    || bitmap->pixels == NULL || bitmap->pixels->primary == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	pixels = bitmap->pixels;
	depth = bitmap->bit_depth;
	if (depth != 1 && depth != 4 && depth != 8 && depth != 24)
		return IO_ERROR_NOT_SUPPORTED;
	if (pixels->width == 0 || pixels->height == 0
	    || pixels->width > INT32_MAX || pixels->height > INT32_MAX
	    || bitmap->palette_size > (depth == 24 ? 0 : 1U << depth)
	    || (depth != 24 && bitmap->palette_size == 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (pixels->width > (SIZE_MAX - 31) / depth)
		return IO_ERROR_OUT_OF_RANGE;
	stride = (((size_t)pixels->width * depth + 31) / 32) * 4;
	offset = 54 + bitmap->palette_size * 4;
	if (pixels->height > (UINT32_MAX - offset) / stride
	    || pixels->height > (SIZE_MAX - offset) / stride
	    || pixels->height > SIZE_MAX / sizeof(uint32_t) / pixels->width)
		return IO_ERROR_OUT_OF_RANGE;
	size = offset + stride * pixels->height;
	for (size_t i = 0; i < bitmap->palette_size; ++i)
		if (bitmap->palette[i] > 0xffffff) return IO_ERROR_INVALID_ARGUMENT;
	data = calloc(1, size);
	if (data == NULL) return IO_ERROR_OUT_OF_MEMORY;
	data[0] = 'B'; data[1] = 'M';
	put32(data + 2, (uint32_t)size); put32(data + 10, (uint32_t)offset);
	put32(data + 14, 40); put32(data + 18, pixels->width); put32(data + 22, pixels->height);
	put16(data + 26, 1); put16(data + 28, depth);
	/* Match the documented RIP writer's advisory fields and zero image size. */
	put32(data + 38, pixels->width); put32(data + 42, pixels->height);
	if (depth < 24 && bitmap->palette_size != (1U << depth))
		put32(data + 46, (uint32_t)bitmap->palette_size);
	for (size_t i = 0; i < bitmap->palette_size; ++i) {
		uint32_t rgb = bitmap->palette[i];
		data[54 + 4 * i] = (uint8_t)rgb;
		data[55 + 4 * i] = (uint8_t)(rgb >> 8);
		data[56 + 4 * i] = (uint8_t)(rgb >> 16);
	}
	for (uint32_t y = 0; y < pixels->height; ++y) {
		uint8_t *row = data + offset + (size_t)(pixels->height - 1 - y) * stride;
		for (uint32_t x = 0; x < pixels->width; ++x) {
			uint32_t pixel = pixels->primary[(size_t)y * pixels->width + x];
			if (depth == 24) {
				if (!ST_PIXEL_COLOR_IS_RGB(pixel)) { free(data); return IO_ERROR_INVALID_ARGUMENT; }
				row[(size_t)x * 3] = (uint8_t)pixel;
				row[(size_t)x * 3 + 1] = (uint8_t)(pixel >> 8);
				row[(size_t)x * 3 + 2] = (uint8_t)(pixel >> 16);
			}
			else {
				if (pixel >= bitmap->palette_size) { free(data); return IO_ERROR_INVALID_ARGUMENT; }
				if (depth == 1) row[x / 8] |= (uint8_t)(pixel << (7 - (x % 8)));
				else if (depth == 4) row[x / 2] |= (uint8_t)(pixel << ((x & 1) == 0 ? 4 : 0));
				else row[x] = (uint8_t)pixel;
			}
		}
	}
	*result = data;
	*result_size = size;
	return 0;
}
