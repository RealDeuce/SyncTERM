/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_pixels.h"

#include "io_error.h"
#include "pixels_internal.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define pixel_count st_pixels_count
int
st_pixel_buffer_create(struct st_pixel_buffer **result, uint32_t width,
	uint32_t height, bool alternate)
{
	struct st_pixel_buffer *buffer;
	size_t count;

	if (result == NULL
	    || !pixel_count(width, height, sizeof(uint32_t), &count))
		return IO_ERROR_INVALID_ARGUMENT;
	buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	buffer->primary = calloc(count, sizeof(buffer->primary[0]));
	if (buffer->primary == NULL) {
		free(buffer);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	if (alternate) {
		buffer->alternate = calloc(count, sizeof(buffer->alternate[0]));
		if (buffer->alternate == NULL) {
			free(buffer->primary);
			free(buffer);
			return IO_ERROR_OUT_OF_MEMORY;
		}
	}
	buffer->width = width;
	buffer->height = height;
	*result = buffer;
	return 0;
}

int
st_rgb_buffer_create(struct st_rgb_buffer **result, uint32_t width,
	uint32_t height)
{
	struct st_rgb_buffer *buffer;
	size_t count;

	if (result == NULL
	    || !pixel_count(width, height, sizeof(uint32_t), &count))
		return IO_ERROR_INVALID_ARGUMENT;
	buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	buffer->pixels = calloc(count, sizeof(buffer->pixels[0]));
	if (buffer->pixels == NULL) {
		free(buffer);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	buffer->width = width;
	buffer->height = height;
	*result = buffer;
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
st_pixel_buffer_clone(struct st_pixel_buffer **result,
	const struct st_pixel_buffer *source)
{
	struct st_pixel_buffer *copy = NULL;
	size_t count;
	int status;

	if (result == NULL || source == NULL || source->primary == NULL
	    || !pixel_count(source->width, source->height, sizeof(uint32_t),
	    &count))
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_pixel_buffer_create(&copy, source->width, source->height,
	    source->alternate != NULL);
	if (status != 0)
		return status;
	memcpy(copy->primary, source->primary,
	    count * sizeof(copy->primary[0]));
	if (source->alternate != NULL)
		memcpy(copy->alternate, source->alternate,
		    count * sizeof(copy->alternate[0]));
	*result = copy;
	return 0;
}

int
st_pixel_buffer_ensure_alternate(struct st_pixel_buffer *buffer)
{
	size_t count;
	uint32_t *alternate;

	if (buffer == NULL || buffer->primary == NULL
	    || !pixel_count(buffer->width, buffer->height, sizeof(uint32_t),
	    &count))
		return IO_ERROR_INVALID_ARGUMENT;
	if (buffer->alternate != NULL)
		return 0;
	alternate = malloc(count * sizeof(alternate[0]));
	if (alternate == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	memcpy(alternate, buffer->primary, count * sizeof(alternate[0]));
	buffer->alternate = alternate;
	return 0;
}

void
st_pixel_buffer_remove_alternate(struct st_pixel_buffer *buffer)
{
	if (buffer == NULL)
		return;
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
st_pixel_mask_create(struct st_pixel_mask **result, uint32_t width,
	uint32_t height)
{
	struct st_pixel_mask *mask;
	size_t count;
	size_t bytes;

	if (result == NULL || !pixel_count(width, height, 1, &count)
	    || count > SIZE_MAX - 7)
		return IO_ERROR_INVALID_ARGUMENT;
	bytes = (count + 7) / 8;
	mask = calloc(1, sizeof(*mask));
	if (mask == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	mask->bits = calloc(bytes, 1);
	if (mask->bits == NULL) {
		free(mask);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	mask->width = width;
	mask->height = height;
	*result = mask;
	return 0;
}

static bool
mask_position(const struct st_pixel_mask *mask, uint32_t x, uint32_t y,
	size_t *position)
{
	if (mask == NULL || mask->bits == NULL || x >= mask->width
	    || y >= mask->height)
		return false;
	*position = (size_t)y * mask->width + x;
	return true;
}

int
st_pixel_mask_get(const struct st_pixel_mask *mask, uint32_t x, uint32_t y,
	bool *value)
{
	size_t position;

	if (value == NULL || !mask_position(mask, x, y, &position))
		return IO_ERROR_OUT_OF_RANGE;
	*value = (mask->bits[position / 8]
	    & (uint8_t)(UINT8_C(0x80) >> (position % 8))) != 0;
	return 0;
}

int
st_pixel_mask_set(struct st_pixel_mask *mask, uint32_t x, uint32_t y,
	bool value)
{
	size_t position;
	uint8_t bit;

	if (!mask_position(mask, x, y, &position))
		return IO_ERROR_OUT_OF_RANGE;
	bit = (uint8_t)(UINT8_C(0x80) >> (position % 8));
	if (value)
		mask->bits[position / 8] |= bit;
	else
		mask->bits[position / 8] &= (uint8_t)~bit;
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
st_pixel_buffer_fill(struct st_pixel_buffer *buffer, uint32_t primary,
	uint32_t alternate)
{
	size_t count;

	if (buffer == NULL || buffer->primary == NULL
	    || !pixel_count(buffer->width, buffer->height, sizeof(uint32_t),
	    &count))
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < count; ++i)
		buffer->primary[i] = primary;
	if (buffer->alternate != NULL)
		for (size_t i = 0; i < count; ++i)
			buffer->alternate[i] = alternate;
	return 0;
}

static int
validate_blit(const struct st_pixel_buffer *destination,
	const struct st_pixel_buffer *source, const struct st_pixel_mask *mask,
	const struct st_pixel_blit *blit, int64_t *right, int64_t *bottom)
{
	uint64_t scaled_width;
	uint64_t scaled_height;
	size_t ignored;

	if (destination == NULL || destination->primary == NULL || source == NULL
	    || source->primary == NULL || blit == NULL || blit->width == 0
	    || blit->height == 0 || blit->scale_x == 0 || blit->scale_y == 0
	    || (blit->flags & ~(ST_PIXEL_BLIT_FLIP_X | ST_PIXEL_BLIT_FLIP_Y))
	    != 0 || !pixel_count(destination->width, destination->height,
	    sizeof(uint32_t), &ignored)
	    || !pixel_count(source->width, source->height, sizeof(uint32_t),
	    &ignored))
		return IO_ERROR_INVALID_ARGUMENT;
	if (blit->source_x >= source->width || blit->source_y >= source->height
	    || blit->width > source->width - blit->source_x
	    || blit->height > source->height - blit->source_y)
		return IO_ERROR_OUT_OF_RANGE;
	if (mask != NULL
	    && (mask->bits == NULL || !pixel_count(mask->width, mask->height, 1,
	    &ignored) || blit->mask_x >= mask->width
	    || blit->mask_y >= mask->height
	    || blit->width > mask->width - blit->mask_x
	    || blit->height > mask->height - blit->mask_y))
		return IO_ERROR_OUT_OF_RANGE;
	scaled_width = (uint64_t)blit->width * blit->scale_x;
	scaled_height = (uint64_t)blit->height * blit->scale_y;
	if (scaled_width > INT64_MAX || scaled_height > INT64_MAX
	    || (scaled_width > 0 && blit->destination_x > INT64_MAX
	    - (int64_t)scaled_width)
	    || (scaled_height > 0 && blit->destination_y > INT64_MAX
	    - (int64_t)scaled_height))
		return IO_ERROR_OUT_OF_RANGE;
	*right = (int64_t)blit->destination_x + (int64_t)scaled_width;
	*bottom = (int64_t)blit->destination_y + (int64_t)scaled_height;
	return 0;
}

static bool
mask_set(const struct st_pixel_mask *mask, uint32_t x, uint32_t y)
{
	size_t position = (size_t)y * mask->width + x;

	return (mask->bits[position / 8]
	    & (uint8_t)(UINT8_C(0x80) >> (position % 8))) != 0;
}

int
st_pixel_buffer_blit(struct st_pixel_buffer *destination,
	const struct st_pixel_buffer *source, const struct st_pixel_mask *mask,
	const struct st_pixel_blit *blit)
{
	struct st_pixel_buffer *copy = NULL;
	const struct st_pixel_buffer *input = source;
	int64_t right;
	int64_t bottom;
	int64_t left;
	int64_t top;
	int status;

	status = validate_blit(destination, source, mask, blit, &right, &bottom);
	if (status != 0)
		return status;
	if (source->alternate != NULL && destination->alternate == NULL) {
		status = st_pixel_buffer_ensure_alternate(destination);
		if (status != 0)
			return status;
	}
	if (destination == source) {
		status = st_pixel_buffer_clone(&copy, source);
		if (status != 0)
			return status;
		input = copy;
	}
	left = blit->destination_x;
	top = blit->destination_y;
	if (left < 0)
		left = 0;
	if (top < 0)
		top = 0;
	if (right > destination->width)
		right = destination->width;
	if (bottom > destination->height)
		bottom = destination->height;
	for (int64_t y = top; y < bottom; ++y) {
		uint32_t relative_y = (uint32_t)((y - blit->destination_y)
		    / blit->scale_y);
		uint32_t source_relative_y = relative_y;

		if ((blit->flags & ST_PIXEL_BLIT_FLIP_Y) != 0)
			source_relative_y = blit->height - 1 - relative_y;
		for (int64_t x = left; x < right; ++x) {
			uint32_t relative_x = (uint32_t)((x
			    - blit->destination_x) / blit->scale_x);
			uint32_t source_relative_x = relative_x;
			size_t source_position;
			size_t destination_position;

			if ((blit->flags & ST_PIXEL_BLIT_FLIP_X) != 0)
				source_relative_x = blit->width - 1 - relative_x;
			if (mask != NULL && !mask_set(mask,
			    blit->mask_x + source_relative_x,
			    blit->mask_y + source_relative_y))
				continue;
			source_position = (size_t)(blit->source_y
			    + source_relative_y) * input->width + blit->source_x
			    + source_relative_x;
			destination_position = (size_t)y * destination->width
			    + (size_t)x;
			destination->primary[destination_position]
			    = input->primary[source_position];
			if (destination->alternate != NULL) {
				const uint32_t *alternate = input->alternate != NULL
				    ? input->alternate : input->primary;

				destination->alternate[destination_position]
				    = alternate[source_position];
			}
		}
	}
	st_pixel_buffer_destroy(copy);
	return 0;
}
