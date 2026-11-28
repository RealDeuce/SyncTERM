/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "io_error.h"
#include "pixels_internal.h"
#include "st_pixels.h"

#define pixel_count st_pixels_count

static bool
storage_count(uint32_t width, uint32_t height, size_t element_size, size_t *count)
{
	if (width == 0 || height == 0) {
		*count = 0;
		return true;
	}
	return pixel_count(width, height, element_size, count);
}

static size_t
packed_bytes(size_t count)
{
	return count / 8 + (count % 8 != 0);
}

void
st_pixel_fill_bits(uint8_t *bits, size_t at, size_t count, bool value)
{
	while (count && at % 8) {
		st_pixel_set_bit(bits, at++, value);
		--count;
	}
	size_t bytes = count / 8;
	memset(bits + at / 8, value ? 0xff : 0, bytes);
	at += bytes * 8;
	for (size_t i = 0; i < count % 8; ++i)
		st_pixel_set_bit(bits, at + i, value);
}

bool
st_pixel_copy_bits(uint8_t *dst, size_t to, const uint8_t *src, size_t from, size_t count)
{
	if ((to | from | count) % 8 == 0 && src) {
		size_t bytes = count / 8;
		if (memcmp(dst + to / 8, src + from / 8, bytes) == 0)
			return false;
		memmove(dst + to / 8, src + from / 8, bytes);
		return true;
	}
	bool backwards = src == dst && to > from && to - from < count;
	bool changed = false;
	for (size_t i = 0; i < count; ++i) {
		size_t offset = backwards ? count - 1 - i : i;
		bool value = st_pixel_bit(src, from + offset);
		if (st_pixel_bit(dst, to + offset) != value) {
			st_pixel_set_bit(dst, to + offset, value);
			changed = true;
		}
	}
	return changed;
}

int
st_pixel_buffer_create(struct st_pixel_buffer **result, uint32_t width, uint32_t height, bool alternate)
{
	struct st_pixel_buffer *buffer;
	size_t                  count;

	if (result == NULL || !storage_count(width, height, sizeof(uint32_t), &count))
		return IO_ERROR_INVALID_ARGUMENT;
	buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	buffer->primary = calloc(count != 0 ? count : 1, sizeof(buffer->primary[0]));
	if (buffer->primary == NULL) {
		free(buffer);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	if (alternate) {
		buffer->alternate = calloc(count != 0 ? count : 1, sizeof(buffer->alternate[0]));
		if (buffer->alternate == NULL) {
			free(buffer->primary);
			free(buffer);
			return IO_ERROR_OUT_OF_MEMORY;
		}
	}
	buffer->width  = width;
	buffer->height = height;
	*result        = buffer;
	return 0;
}

int
st_rgb_buffer_create(struct st_rgb_buffer **result, uint32_t width, uint32_t height)
{
	struct st_rgb_buffer *buffer;
	size_t                count;

	if (result == NULL || !pixel_count(width, height, sizeof(uint32_t), &count))
		return IO_ERROR_INVALID_ARGUMENT;
	buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	buffer->pixels = calloc(count, sizeof(buffer->pixels[0]));
	if (buffer->pixels == NULL) {
		free(buffer);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	buffer->width  = width;
	buffer->height = height;
	*result        = buffer;
	return 0;
}

void
st_rgb_buffer_destroy(struct st_rgb_buffer *buffer)
{
	if (buffer == NULL)
		return;
	free(buffer->pixels);
	free(buffer);
}

int
st_pixel_buffer_clone(struct st_pixel_buffer **result, const struct st_pixel_buffer *source)
{
	struct st_pixel_buffer *copy = NULL;
	size_t                  count;
	int                     status;

	if (result == NULL || source == NULL || source->primary == NULL
	    || !storage_count(source->width, source->height, sizeof(uint32_t), &count))
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_pixel_buffer_create(&copy, source->width, source->height, source->alternate != NULL);
	if (status != 0)
		return status;
	memcpy(copy->primary, source->primary, count * sizeof(copy->primary[0]));
	if (source->alternate != NULL)
		memcpy(copy->alternate, source->alternate, count * sizeof(copy->alternate[0]));
	if (source->fast_blink != NULL) {
		status = st_pixel_buffer_ensure_fast_blink(copy);
		if (status != 0) {
			st_pixel_buffer_destroy(copy);
			return status;
		}
		memcpy(copy->fast_blink, source->fast_blink, packed_bytes(count));
	}
	*result = copy;
	return 0;
}

int
st_pixel_buffer_ensure_alternate(struct st_pixel_buffer *buffer)
{
	size_t    count;
	uint32_t *alternate;

	if (buffer == NULL || buffer->primary == NULL
	    || !storage_count(buffer->width, buffer->height, sizeof(uint32_t), &count))
		return IO_ERROR_INVALID_ARGUMENT;
	if (buffer->alternate != NULL)
		return 0;
	alternate = malloc((count != 0 ? count : 1) * sizeof(alternate[0]));
	if (alternate == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	memcpy(alternate, buffer->primary, count * sizeof(alternate[0]));
	buffer->alternate = alternate;
	return 0;
}

void
st_pixel_buffer_copy_span(const struct st_pixel_buffer *destination, size_t to,
    const struct st_pixel_buffer *source, size_t from, size_t count)
{
	memmove(destination->primary + to, source->primary + from, count * sizeof(*source->primary));
	if (destination->alternate) {
		const uint32_t *alternate = source->alternate ? source->alternate : source->primary;
		memmove(destination->alternate + to, alternate + from, count * sizeof(*alternate));
	}
	if (destination->fast_blink)
		st_pixel_copy_bits(destination->fast_blink, to, source->fast_blink, from, count);
}

int
st_pixel_buffer_ensure_fast_blink(struct st_pixel_buffer *buffer)
{
	size_t count;
	if (buffer == NULL || !storage_count(buffer->width, buffer->height, 1, &count))
		return IO_ERROR_INVALID_ARGUMENT;
	if (buffer->fast_blink != NULL)
		return 0;
	uint8_t *bits = calloc(count ? packed_bytes(count) : 1, 1);
	if (bits == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	int status = st_pixel_buffer_ensure_alternate(buffer);
	if (status != 0)
		free(bits);
	else
		buffer->fast_blink = bits;
	return status;
}

void
st_pixel_buffer_remove_alternate(struct st_pixel_buffer *buffer)
{
	if (buffer == NULL)
		return;
	free(buffer->fast_blink);
	buffer->fast_blink = NULL;
	free(buffer->alternate);
	buffer->alternate = NULL;
}

void
st_pixel_buffer_destroy(struct st_pixel_buffer *buffer)
{
	if (buffer == NULL)
		return;
	st_pixel_buffer_remove_alternate(buffer);
	free(buffer->primary);
	free(buffer);
}

int
st_pixel_mask_create(struct st_pixel_mask **result, uint32_t width, uint32_t height)
{
	struct st_pixel_mask *mask;
	size_t                count;
	size_t                bytes;

	if (result == NULL || !storage_count(width, height, 1, &count) || count > SIZE_MAX - 7)
		return IO_ERROR_INVALID_ARGUMENT;
	bytes = (count + 7) / 8;
	mask  = calloc(1, sizeof(*mask));
	if (mask == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	mask->bits = calloc(bytes != 0 ? bytes : 1, 1);
	if (mask->bits == NULL) {
		free(mask);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	mask->width  = width;
	mask->height = height;
	*result      = mask;
	return 0;
}

static bool
mask_position(const struct st_pixel_mask *mask, uint32_t x, uint32_t y, size_t *position)
{
	if (mask == NULL || mask->bits == NULL || x >= mask->width || y >= mask->height)
		return false;
	*position = (size_t)y * mask->width + x;
	return true;
}

int
st_pixel_mask_get(const struct st_pixel_mask *mask, uint32_t x, uint32_t y, bool *value)
{
	size_t position;

	if (value == NULL || !mask_position(mask, x, y, &position))
		return IO_ERROR_OUT_OF_RANGE;
	*value = st_pixel_bit(mask->bits, position);
	return 0;
}

int
st_pixel_mask_set(struct st_pixel_mask *mask, uint32_t x, uint32_t y, bool value)
{
	size_t position;
	if (!mask_position(mask, x, y, &position))
		return IO_ERROR_OUT_OF_RANGE;
	st_pixel_set_bit(mask->bits, position, value);
	return 0;
}

void
st_pixel_mask_destroy(struct st_pixel_mask *mask)
{
	if (mask == NULL)
		return;
	free(mask->bits);
	free(mask);
}

int
st_pixel_buffer_fill(struct st_pixel_buffer *buffer, uint32_t primary, uint32_t alternate)
{
	size_t count;

	if (buffer == NULL || buffer->primary == NULL
	    || !storage_count(buffer->width, buffer->height, sizeof(uint32_t), &count))
		return IO_ERROR_INVALID_ARGUMENT;
	if (buffer->fast_blink != NULL)
		memset(buffer->fast_blink, 0, packed_bytes(count));
	for (size_t i = 0; i < count; ++i)
		buffer->primary[i] = primary;
	if (buffer->alternate != NULL)
		for (size_t i = 0; i < count; ++i)
			buffer->alternate[i] = alternate;
	return 0;
}

int
st_pixel_buffer_validate_blit(const struct st_pixel_buffer *destination, const struct st_pixel_buffer *source,
    const struct st_pixel_mask *mask, const struct st_pixel_blit *blit, int64_t *right, int64_t *bottom)
{
	uint64_t scaled_width;
	uint64_t scaled_height;
	size_t   ignored;

	if (right == NULL || bottom == NULL || destination == NULL || destination->primary == NULL
	    || source == NULL || source->primary == NULL || blit == NULL || blit->width == 0 || blit->height == 0
	    || blit->scale_x == 0 || blit->scale_y == 0
	    || (blit->flags & ~(ST_PIXEL_BLIT_FLIP_X | ST_PIXEL_BLIT_FLIP_Y)) != 0
	    || !pixel_count(destination->width, destination->height, sizeof(uint32_t), &ignored)
	    || !pixel_count(source->width, source->height, sizeof(uint32_t), &ignored))
		return IO_ERROR_INVALID_ARGUMENT;
	if (blit->source_x >= source->width || blit->source_y >= source->height
	    || blit->width > source->width - blit->source_x || blit->height > source->height - blit->source_y)
		return IO_ERROR_OUT_OF_RANGE;
	if (mask != NULL
	    && (mask->bits == NULL || !pixel_count(mask->width, mask->height, 1, &ignored)
	        || blit->mask_x >= mask->width || blit->mask_y >= mask->height
	        || blit->width > mask->width - blit->mask_x || blit->height > mask->height - blit->mask_y))
		return IO_ERROR_OUT_OF_RANGE;
	scaled_width  = (uint64_t)blit->width * blit->scale_x;
	scaled_height = (uint64_t)blit->height * blit->scale_y;
	if (scaled_width > INT64_MAX || scaled_height > INT64_MAX
	    || (scaled_width > 0 && blit->destination_x > INT64_MAX - (int64_t)scaled_width)
	    || (scaled_height > 0 && blit->destination_y > INT64_MAX - (int64_t)scaled_height))
		return IO_ERROR_OUT_OF_RANGE;
	*right  = (int64_t)blit->destination_x + (int64_t)scaled_width;
	*bottom = (int64_t)blit->destination_y + (int64_t)scaled_height;
	return 0;
}

static bool
mask_set(const struct st_pixel_mask *mask, uint32_t x, uint32_t y)
{
	size_t position = (size_t)y * mask->width + x;

	return st_pixel_bit(mask->bits, position);
}

int
st_pixel_buffer_blit(struct st_pixel_buffer *destination, const struct st_pixel_buffer *source,
    const struct st_pixel_mask *mask, const struct st_pixel_blit *blit)
{
	struct st_pixel_buffer       *copy  = NULL;
	const struct st_pixel_buffer *input = source;
	int64_t                       right;
	int64_t                       bottom;
	int64_t                       left;
	int64_t                       top;
	int                           status;

	status = st_pixel_buffer_validate_blit(destination, source, mask, blit, &right, &bottom);
	if (status != 0)
		return status;
	if (source->fast_blink && !destination->fast_blink)
		status = st_pixel_buffer_ensure_fast_blink(destination);
	else if (source->alternate && !destination->alternate)
		status = st_pixel_buffer_ensure_alternate(destination);
	if (status != 0)
		return status;
	if (destination == source) {
		status = st_pixel_buffer_clone(&copy, source);
		if (status != 0)
			return status;
		input = copy;
	}
	left = blit->destination_x;
	top  = blit->destination_y;
	if (left < 0)
		left = 0;
	if (top < 0)
		top = 0;
	if (right > destination->width)
		right = destination->width;
	if (bottom > destination->height)
		bottom = destination->height;
	for (int64_t y = top; y < bottom; ++y) {
		uint32_t relative_y        = (uint32_t)((y - blit->destination_y) / blit->scale_y);
		uint32_t source_relative_y = relative_y;

		if ((blit->flags & ST_PIXEL_BLIT_FLIP_Y) != 0)
			source_relative_y = blit->height - 1 - relative_y;
		for (int64_t x = left; x < right; ++x) {
			uint32_t relative_x        = (uint32_t)((x - blit->destination_x) / blit->scale_x);
			uint32_t source_relative_x = relative_x;
			size_t   source_position;
			size_t   destination_position;

			if ((blit->flags & ST_PIXEL_BLIT_FLIP_X) != 0)
				source_relative_x = blit->width - 1 - relative_x;
			if (mask != NULL
			    && !mask_set(mask, blit->mask_x + source_relative_x, blit->mask_y + source_relative_y))
				continue;
			source_position      = (size_t)(blit->source_y + source_relative_y) * input->width
			                       + blit->source_x + source_relative_x;
			destination_position = (size_t)y * destination->width + (size_t)x;
			destination->primary[destination_position] = input->primary[source_position];
			if (destination->fast_blink != NULL)
				st_pixel_set_bit(destination->fast_blink, destination_position,
				    st_pixel_bit(input->fast_blink, source_position));
			if (destination->alternate != NULL) {
				const uint32_t *alternate =
				    input->alternate != NULL ? input->alternate : input->primary;

				destination->alternate[destination_position] = alternate[source_position];
			}
		}
	}
	st_pixel_buffer_destroy(copy);
	return 0;
}
