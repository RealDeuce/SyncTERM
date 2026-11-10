/* Shared composition and scaling for bitmap console backends. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_internal.h"

#include "io_error.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct st_console_bitmap_presenter {
	struct st_rgb_buffer *images[4];
	st_pixel_scaler_t *scalers[4];
	const struct st_rgb_buffer *scaled[4];
	uint64_t scaled_generation[4];
	bool scaled_blocky[4];
	uint32_t palette[UINT16_MAX + 1U];
	uint64_t generation;
	uint64_t uploaded_generation;
	uint64_t damage_generation;
	size_t plane_difference_count;
	uint32_t cell_width;
	uint32_t cell_height;
	uint32_t columns;
	uint32_t rows;
	uint32_t cursor_x;
	uint32_t cursor_y;
	uint32_t cursor_draw_x;
	uint32_t cursor_columns;
	uint32_t cursor_colour;
	uint16_t cursor_index_base;
	uint16_t cursor_index_xor;
	uint32_t output_width;
	uint32_t output_height;
	uint32_t aspect_width;
	uint32_t aspect_height;
	uint32_t epoch;
	uint8_t selected_image;
	uint8_t uploaded_image;
	struct st_console_rect damage;
	struct st_console_rect cursor_rectangle;
	enum st_console_cursor_shape cursor_shape;
	enum st_console_blink_mode blink_mode;
	enum st_console_scaling_mode scaling_mode;
	bool software_scaling_only;
	bool preserve_window_size;
	struct st_console_custom_cursor custom_cursor;
	bool pixel_cursor_enabled;
	bool cursor_custom;
	bool cursor_visible;
	bool cursor_composed;
	bool local_animation_clock;
	bool animation_epoch_pending;
	bool planes_differ;
	bool damage_valid;
	bool damage_full;
	bool blocky_scaling;
	bool initialized;
	bool upload_initialized;
	bool upload_retry;
	bool images_valid;
	bool invalid_colour;
	uint32_t invalid_colour_value;
};

static uint32_t
colour_value(struct st_console_bitmap_presenter *presenter,
	uint32_t colour)
{
	if (ST_CONSOLE_COLOR_IS_RGB(colour))
		return colour & UINT32_C(0xffffff);
	if (colour <= UINT16_MAX)
		return presenter->palette[colour];
	if (!presenter->invalid_colour) {
		presenter->invalid_colour = true;
		presenter->invalid_colour_value = colour;
	}
	return 0;
}

static bool
resolve_rectangle(struct st_console_bitmap_presenter *presenter,
	struct st_rgb_buffer *destination, const st_console_t *console,
	bool alternate, const struct st_console_rect *rectangle)
{
	const uint32_t *plane = alternate && console->pixels->alternate != NULL
	    ? console->pixels->alternate : console->pixels->primary;
	bool changed = false;

	for (uint32_t y = 0; y < rectangle->height; ++y) {
		size_t destination_offset = (size_t)(rectangle->y + y)
		    * destination->width + rectangle->x;
		size_t source_offset = st_console_pixel_position_locked(console,
		    rectangle->x, rectangle->y + y);

		for (uint32_t x = 0; x < rectangle->width; ++x) {
			uint32_t value = colour_value(presenter,
			    plane[source_offset + x]);

			changed = changed
			    || destination->pixels[destination_offset + x] != value;
			destination->pixels[destination_offset + x] = value;
		}
	}
	return changed;
}

static bool
fill_rectangle(struct st_rgb_buffer *buffer,
	const struct st_console_rect *rectangle, uint32_t colour)
{
	bool changed = false;

	for (uint32_t y = 0; y < rectangle->height; ++y) {
		uint32_t *row = &buffer->pixels[(size_t)(rectangle->y + y)
		    * buffer->width + rectangle->x];

		for (uint32_t x = 0; x < rectangle->width; ++x) {
			changed = changed || row[x] != colour;
			row[x] = colour;
		}
	}
	return changed;
}

static bool
compose_index_cursor(struct st_console_bitmap_presenter *presenter,
	struct st_rgb_buffer *destination, const st_console_t *console, bool alternate)
{
	const struct st_console_rect *rect = &presenter->cursor_rectangle;
	const uint32_t *plane = alternate && console->pixels->alternate != NULL
	    ? console->pixels->alternate : console->pixels->primary;
	uint32_t base = presenter->cursor_index_base;
	uint32_t mask = presenter->cursor_index_xor;
	bool changed = false;

	for (uint32_t y = 0; y < rect->height; ++y) {
		size_t source = st_console_pixel_position_locked(console, rect->x, rect->y + y);
		size_t target = (size_t)(rect->y + y) * destination->width + rect->x;
		for (uint32_t x = 0; x < rect->width; ++x) {
			uint32_t index = plane[source + x];
			if (index >= base && index - base <= mask)
				index = base + ((index - base) ^ mask);
			uint32_t rgb = colour_value(presenter, index);
			changed = changed || destination->pixels[target + x] != rgb;
			destination->pixels[target + x] = rgb;
		}
	}
	return changed;
}

static bool
copy_rgb_rectangle(struct st_rgb_buffer *destination,
	const struct st_rgb_buffer *source,
	const struct st_console_rect *rectangle)
{
	bool changed = false;

	for (uint32_t y = 0; y < rectangle->height; ++y) {
		size_t offset = (size_t)(rectangle->y + y) * source->width
		    + rectangle->x;
		size_t bytes = (size_t)rectangle->width
		    * sizeof(source->pixels[0]);

		changed = changed || memcmp(&destination->pixels[offset],
		    &source->pixels[offset], bytes) != 0;
		memcpy(&destination->pixels[offset], &source->pixels[offset], bytes);
	}
	return changed;
}

enum bitmap_image {
	BITMAP_PRIMARY,
	BITMAP_PRIMARY_CURSOR,
	BITMAP_ALTERNATE,
	BITMAP_ALTERNATE_CURSOR,
	BITMAP_IMAGE_COUNT
};

static struct st_console_rect
cursor_rectangle(const struct st_console_bitmap_presenter *presenter)
{
	struct st_console_rect result = {
		.x = presenter->cursor_draw_x * presenter->cell_width,
		.y = presenter->cursor_y * presenter->cell_height,
		.width = presenter->cursor_columns * presenter->cell_width,
		.height = presenter->cell_height,
	};

	if (presenter->cursor_custom) {
		uint32_t start;
		uint32_t end;

		if (presenter->custom_cursor.start > presenter->custom_cursor.end)
			return (struct st_console_rect){0};
		start = (uint32_t)((uint64_t)presenter->custom_cursor.start
		    * presenter->cell_height / presenter->custom_cursor.range);
		end = (uint32_t)(((uint64_t)presenter->custom_cursor.end + 1U)
		    * presenter->cell_height / presenter->custom_cursor.range);
		if (start >= presenter->cell_height)
			return (struct st_console_rect){0};
		if (end > presenter->cell_height)
			end = presenter->cell_height;
		if (end <= start)
			end = start + 1;
		result.y += start;
		result.height = end - start;
	}
	else if (presenter->cursor_shape == ST_CONSOLE_CURSOR_UNDERLINE) {
		uint32_t thickness = presenter->cell_height / 8;

		if (thickness == 0)
			thickness = 1;
		result.y += presenter->cell_height - thickness;
		result.height = thickness;
	}
	else if (presenter->cursor_shape == ST_CONSOLE_CURSOR_BAR) {
		result.width = presenter->cell_width / 8;
		if (result.width == 0)
			result.width = 1;
	}
	return result;
}

static void
animation_state(enum st_console_blink_mode mode, uint32_t elapsed,
	bool *alternate, bool *cursor)
{
	switch (mode) {
	case ST_CONSOLE_BLINK_PRESTEL: {
		uint32_t position = elapsed % 1256U;

		*alternate = position >= 314U;
		*cursor = ((position / 314U) & 1U) != 0;
		break;
	}
	case ST_CONSOLE_BLINK_COMMODORE:
		*alternate = ((elapsed / 333U) & 1U) == 0;
		*cursor = *alternate;
		break;
	case ST_CONSOLE_BLINK_ATARI:
		*alternate = false;
		*cursor = true;
		break;
	case ST_CONSOLE_BLINK_ATARI_ST:
		*alternate = false;
		*cursor = ((elapsed / 500U) & 1U) == 0;
		break;
	case ST_CONSOLE_BLINK_PC:
	default:
		*alternate = ((elapsed / 266U) & 1U) != 0;
		*cursor = ((elapsed / 133U) & 1U) == 0;
		break;
	}
}

int
st_console_bitmap_validate_geometry(const st_console_t *console,
	uint32_t maximum_dimension)
{
	uint32_t width, height;
	int status;

	if (console->pixels == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_console_aspect_correct_size(console->pixels->width,
	    console->pixels->height, console->aspect_width, console->aspect_height,
	    &width, &height);
	if (status != 0)
		return status;
	return width > maximum_dimension || height > maximum_dimension
	    ? IO_ERROR_OUT_OF_RANGE : 0;
}

int
st_console_aspect_correct_size(uint32_t source_width,
	uint32_t source_height, uint32_t aspect_width, uint32_t aspect_height,
	uint32_t *width, uint32_t *height)
{
	uint64_t corrected_width = source_width;
	uint64_t corrected_height = source_height;

	if (width == NULL || height == NULL || source_width == 0
	    || source_height == 0
	    || ((aspect_width == 0) != (aspect_height == 0)))
		return IO_ERROR_INVALID_ARGUMENT;
	if (aspect_width != 0) {
		corrected_height = ((uint64_t)source_width * aspect_height
		    + aspect_width / 2U) / aspect_width;
		if (corrected_height < source_height) {
			corrected_height = source_height;
			corrected_width = ((uint64_t)source_height * aspect_width
			    + aspect_height / 2U) / aspect_height;
		}
	}
	if (corrected_width < source_width)
		corrected_width = source_width;
	if (corrected_height < source_height)
		corrected_height = source_height;
	if (corrected_width > UINT32_MAX || corrected_height > UINT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	*width = (uint32_t)corrected_width;
	*height = (uint32_t)corrected_height;
	return 0;
}

int
st_console_scaled_window_size(uint32_t source_width,
	uint32_t source_height, uint32_t aspect_width, uint32_t aspect_height,
	double scaling, uint32_t *width, uint32_t *height)
{
	double scaled_width;
	double scaled_height;
	uint32_t integer_width;
	uint32_t integer_height;

	if (width == NULL || height == NULL || scaling != scaling)
		return IO_ERROR_INVALID_ARGUMENT;
	if (scaling < 1.0)
		scaling = 1.0;
	scaled_width = source_width * scaling;
	scaled_height = source_height * scaling;
	if (scaled_width > UINT32_MAX || scaled_height > UINT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	integer_width = (uint32_t)scaled_width;
	integer_height = (uint32_t)scaled_height;
	if (integer_width < UINT32_MAX
	    && scaled_width - integer_width >= 0.5)
		++integer_width;
	if (integer_height < UINT32_MAX
	    && scaled_height - integer_height >= 0.5)
		++integer_height;
	return st_console_aspect_correct_size(integer_width, integer_height,
	    aspect_width, aspect_height, width, height);
}

static bool
geometry_width_controls(const struct st_console_bitmap_geometry *geometry)
{
	return geometry->aspect_width == 0
	    || ((uint64_t)geometry->height * geometry->aspect_width
	    + geometry->aspect_height / 2U) / geometry->aspect_height <= geometry->width;
}

int
st_console_snap_geometry_size(const struct st_console_bitmap_geometry *geometry,
	uint32_t current_width, uint32_t current_height, bool larger,
	uint32_t maximum_width, uint32_t maximum_height,
	uint32_t *width, uint32_t *height)
{
	uint32_t minimum_width;
	uint32_t minimum_height;
	uint32_t scale;
	uint32_t candidate_width;
	uint32_t candidate_height;
	uint32_t current;
	uint32_t source;
	uint32_t source_width;
	uint32_t source_height;
	uint32_t aspect_width;
	uint32_t aspect_height;
	int status;

	if (geometry == NULL || current_width == 0
	    || current_height == 0 || width == NULL || height == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	source_width = geometry->width;
	source_height = geometry->height;
	aspect_width = geometry->aspect_width;
	aspect_height = geometry->aspect_height;
	status = st_console_aspect_correct_size(source_width, source_height,
	    aspect_width, aspect_height, &minimum_width, &minimum_height);
	if (status != 0)
		return status;
	/* The controlling raster axis is exact at integer scales. Dividing
	 * both axes by rounded 1x dimensions makes C64's 640x533 report 1x
	 * (533 / 267), preventing the next snap from ever reaching 3x. */
	current = geometry_width_controls(geometry) ? current_width : current_height;
	source = geometry_width_controls(geometry) ? source_width : source_height;
	scale = current / source;
	if (geometry->preserve_window_size && scale == 0) {
		if (!larger) {
			*width = current_width;
			*height = current_height;
			return 0;
		}
		/* The next integer zoom above a preserved fractional size is 1x. */
		scale = 1;
	}
	else {
		if (scale < 1)
			scale = 1;
		if (larger) {
			if (scale == UINT32_MAX)
				return IO_ERROR_OUT_OF_RANGE;
			++scale;
		}
		else if (scale > 1 && current % source == 0)
			--scale;
	}
	status = st_console_scaled_window_size(source_width, source_height,
	    aspect_width, aspect_height, scale, &candidate_width,
	    &candidate_height);
	if (status != 0)
		return status;
	if ((maximum_width != 0 && candidate_width > maximum_width)
	    || (maximum_height != 0 && candidate_height > maximum_height)) {
		*width = current_width;
		*height = current_height;
		return 0;
	}
	*width = candidate_width;
	*height = candidate_height;
	return 0;
}

static long double
window_diagonal_squared(uint32_t width, uint32_t height)
{
	return (long double)width * width + (long double)height * height;
}

int
st_console_mode_change_size(const struct st_console_bitmap_geometry *previous,
	const struct st_console_bitmap_geometry *next,
	uint32_t current_width, uint32_t current_height,
	uint32_t maximum_width, uint32_t maximum_height,
	uint32_t *width, uint32_t *height,
	uint32_t *minimum_width, uint32_t *minimum_height)
{
	uint32_t old_width, old_height, lower_width, lower_height;
	uint32_t upper_width, upper_height;
	uint64_t first, last, quantum;
	double old_scale, delta;
	long double target, lower_score, upper_score;
	int status;

	if (previous == NULL || next == NULL || current_width == 0
	    || current_height == 0 || width == NULL || height == NULL
	    || minimum_width == NULL || minimum_height == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_console_aspect_correct_size(previous->width, previous->height,
	    previous->aspect_width, previous->aspect_height, &old_width, &old_height);
	if (status == 0)
		status = st_console_aspect_correct_size(next->width, next->height,
		    next->aspect_width, next->aspect_height, minimum_width, minimum_height);
	if (status != 0)
		return status;
	if (next->preserve_window_size) {
		*width = current_width;
		*height = current_height;
		*minimum_width = *minimum_height = 1;
		return 0;
	}
	old_scale = geometry_width_controls(previous)
	    ? (double)current_width / previous->width
	    : (double)current_height / previous->height;
	delta = old_scale - (uint64_t)(old_scale + 0.5);
	if (delta < 0)
		delta = -delta;
	/* v1.10a retains integer/fractional scaling preference and chooses
	 * the closest squared window diagonal, not the old pixel multiplier.
	 * Its fractional search terminates at a 2^-15 scale step. */
	quantum = delta < 0.0125 ? 1 : UINT64_C(32768);
	first = quantum;
	last = (uint64_t)UINT32_MAX * quantum;
	if (maximum_width != 0 && maximum_width < *minimum_width)
		maximum_width = *minimum_width;
	if (maximum_height != 0 && maximum_height < *minimum_height)
		maximum_height = *minimum_height;
	target = window_diagonal_squared(current_width, current_height);
	/* Search the scale grid without a loop proportional to the requested
	 * window size. Rounding the aspect-corrected dimensions stays monotone. */
	while (first < last) {
		uint64_t middle = first + (last - first + 1) / 2;
		uint32_t w, h;
		status = st_console_scaled_window_size(next->width, next->height,
		    next->aspect_width, next->aspect_height, (double)middle / quantum, &w, &h);
		if (status == 0 && (maximum_width == 0 || w <= maximum_width)
		    && (maximum_height == 0 || h <= maximum_height)
		    && window_diagonal_squared(w, h) <= target)
			first = middle;
		else
			last = middle - 1;
	}
	status = st_console_scaled_window_size(next->width, next->height,
	    next->aspect_width, next->aspect_height, (double)first / quantum,
	    &lower_width, &lower_height);
	if (status != 0)
		return status;
	lower_score = window_diagonal_squared(lower_width, lower_height) - target;
	if (lower_score < 0)
		lower_score = -lower_score;
	status = st_console_scaled_window_size(next->width, next->height,
	    next->aspect_width, next->aspect_height, (double)(first + 1) / quantum,
	    &upper_width, &upper_height);
	if (status == 0 && (maximum_width == 0 || upper_width <= maximum_width)
	    && (maximum_height == 0 || upper_height <= maximum_height)) {
		upper_score = window_diagonal_squared(upper_width, upper_height) - target;
		if (upper_score < 0)
			upper_score = -upper_score;
		if (upper_score < lower_score) {
			lower_width = upper_width;
			lower_height = upper_height;
		}
	}
	*width = lower_width;
	*height = lower_height;
	return 0;
}

int
st_console_preserve_geometry_scale(
	const struct st_console_bitmap_geometry *previous,
	const struct st_console_bitmap_geometry *next,
	uint32_t current_width, uint32_t current_height,
	uint32_t *width, uint32_t *height,
	uint32_t *minimum_width, uint32_t *minimum_height)
{
	uint32_t old_minimum_width;
	uint32_t old_minimum_height;
	uint32_t fitted_width;
	uint32_t fitted_height;
	uint64_t numerator;
	uint64_t denominator;
	uint64_t scaled_source_width;
	uint64_t scaled_source_height;
	bool width_controls;
	int status;

	if (previous == NULL || next == NULL || width == NULL
	    || height == NULL || minimum_width == NULL || minimum_height == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_console_aspect_correct_size(previous->width,
	    previous->height, previous->aspect_width,
	    previous->aspect_height, &old_minimum_width, &old_minimum_height);
	if (status == 0)
		status = st_console_aspect_correct_size(next->width, next->height,
		    next->aspect_width, next->aspect_height, minimum_width,
		    minimum_height);
	if (status != 0)
		return status;
	if (next->preserve_window_size)
		*minimum_width = *minimum_height = 1;
	if (!next->preserve_window_size && (current_width < old_minimum_width
	    || current_height < old_minimum_height)) {
		*width = *minimum_width;
		*height = *minimum_height;
		return 0;
	}
	if (previous->aspect_width == 0) {
		fitted_width = current_width;
		fitted_height = current_height;
	}
	else {
		uint64_t width_from_height
		    = ((uint64_t)current_height * previous->aspect_width
		    + previous->aspect_height / 2U) / previous->aspect_height;

		fitted_width = current_width;
		fitted_height = current_height;
		if (width_from_height <= current_width)
			fitted_width = (uint32_t)width_from_height;
		else
			fitted_height = (uint32_t)(((uint64_t)current_width
			    * previous->aspect_height + previous->aspect_width / 2U)
			    / previous->aspect_width);
	}
	width_controls = previous->aspect_width == 0
	    ? (uint64_t)fitted_width * previous->height
	    <= (uint64_t)fitted_height * previous->width
	    : ((uint64_t)previous->height * previous->aspect_width
	    + previous->aspect_height / 2U) / previous->aspect_height
	    <= previous->width;
	if (width_controls) {
		numerator = fitted_width;
		denominator = previous->width;
	}
	else {
		numerator = fitted_height;
		denominator = previous->height;
	}
	if (numerator < denominator && !next->preserve_window_size)
		numerator = denominator;
	scaled_source_width = ((uint64_t)next->width * numerator
	    + denominator / 2U) / denominator;
	scaled_source_height = ((uint64_t)next->height * numerator
	    + denominator / 2U) / denominator;
	if (scaled_source_width > UINT32_MAX
	    || scaled_source_height > UINT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	if (scaled_source_width == 0) scaled_source_width = 1;
	if (scaled_source_height == 0) scaled_source_height = 1;
	return st_console_aspect_correct_size((uint32_t)scaled_source_width,
	    (uint32_t)scaled_source_height, next->aspect_width,
	    next->aspect_height, width, height);
}

int
st_console_bitmap_geometry_scaling(
	const struct st_console_bitmap_geometry *geometry,
	uint32_t output_width, uint32_t output_height, double *scaling)
{
	uint32_t minimum_width;
	uint32_t minimum_height;
	uint32_t candidate_width;
	uint32_t candidate_height;
	double width_scaling;
	double height_scaling;
	double result;
	int status;

	if (geometry == NULL || scaling == NULL || output_width == 0
	    || output_height == 0)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_console_aspect_correct_size(geometry->width,
	    geometry->height, geometry->aspect_width, geometry->aspect_height,
	    &minimum_width, &minimum_height);
	if (status != 0)
		return status;
	if (!geometry->preserve_window_size && (output_width < minimum_width || output_height < minimum_height))
		return IO_ERROR_OUT_OF_RANGE;
	width_scaling = (double)output_width / minimum_width;
	height_scaling = (double)output_height / minimum_height;
	if (width_scaling < height_scaling) {
		status = st_console_scaled_window_size(geometry->width,
		    geometry->height, geometry->aspect_width,
		    geometry->aspect_height, height_scaling, &candidate_width,
		    &candidate_height);
		result = status == 0 && candidate_width <= output_width
		    && candidate_height <= output_height
		    ? height_scaling : width_scaling;
	}
	else if (height_scaling < width_scaling) {
		status = st_console_scaled_window_size(geometry->width,
		    geometry->height, geometry->aspect_width,
		    geometry->aspect_height, width_scaling, &candidate_width,
		    &candidate_height);
		result = status == 0 && candidate_width <= output_width
		    && candidate_height <= output_height
		    ? width_scaling : height_scaling;
	}
	else
		result = height_scaling;
	*scaling = result;
	return 0;
}

static struct st_console_rect
fit_rectangle(uint32_t output_width, uint32_t output_height,
	uint32_t source_width, uint32_t source_height, uint32_t aspect_width,
	uint32_t aspect_height, bool allow_downscale)
{
	struct st_console_rect result = {0, 0, output_width, output_height};
	uint32_t minimum_width = source_width;
	uint32_t minimum_height = source_height;
	uint64_t width_ratio = aspect_width == 0 ? source_width : aspect_width;
	uint64_t height_ratio = aspect_height == 0 ? source_height : aspect_height;
	uint64_t width_from_height = ((uint64_t)output_height * width_ratio
	    + height_ratio / 2U) / height_ratio;

	(void)st_console_aspect_correct_size(source_width, source_height,
	    aspect_width, aspect_height, &minimum_width, &minimum_height);
	if (allow_downscale)
		minimum_width = minimum_height = 1;
	if (output_width < minimum_width || output_height < minimum_height) {
		result.width = minimum_width;
		result.height = minimum_height;
		return result;
	}
	if (width_from_height <= output_width)
		result.width = (uint32_t)width_from_height;
	else
		result.height = (uint32_t)(((uint64_t)output_width * height_ratio
		    + width_ratio / 2U) / width_ratio);
	if (result.width < minimum_width)
		result.width = minimum_width;
	if (result.height < minimum_height)
		result.height = minimum_height;
	result.x = (output_width - result.width) / 2;
	result.y = (output_height - result.height) / 2;
	return result;
}

static int
replace_buffers(struct st_console_bitmap_presenter *presenter,
	const struct st_pixel_buffer *pixels)
{
	struct st_rgb_buffer *images[BITMAP_IMAGE_COUNT] = {0};
	int status = 0;

	for (size_t i = 0; i < BITMAP_IMAGE_COUNT && status == 0; ++i)
		status = st_rgb_buffer_create(&images[i], pixels->width,
		    pixels->height);
	if (status != 0) {
		for (size_t i = 0; i < BITMAP_IMAGE_COUNT; ++i)
			st_rgb_buffer_destroy(images[i]);
		return status;
	}
	for (size_t i = 0; i < BITMAP_IMAGE_COUNT; ++i) {
		st_rgb_buffer_destroy(presenter->images[i]);
		presenter->images[i] = images[i];
		presenter->scaled_generation[i] = 0;
	}
	presenter->images_valid = false;
	presenter->damage_full = true;
	presenter->upload_retry = true;
	return 0;
}

int
st_console_bitmap_presenter_create(
	struct st_console_bitmap_presenter **result, const st_console_t *console)
{
	struct st_console_bitmap_presenter *presenter;
	int status;

	if (result == NULL || console == NULL || console->pixels == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	presenter = calloc(1, sizeof(*presenter));
	if (presenter == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	status = replace_buffers(presenter, console->pixels);
	for (size_t i = 0; i < BITMAP_IMAGE_COUNT && status == 0; ++i)
		status = st_pixel_scaler_create(&presenter->scalers[i]);
	if (status != 0) {
		st_console_bitmap_presenter_destroy(presenter);
		return status;
	}
	memcpy(presenter->palette, console->palette, sizeof(presenter->palette));
	presenter->cell_width = console->cell_width;
	presenter->cell_height = console->cell_height;
	presenter->columns = console->columns;
	presenter->rows = console->rows;
	*result = presenter;
	return 0;
}

void
st_console_bitmap_presenter_destroy(
	struct st_console_bitmap_presenter *presenter)
{
	if (presenter == NULL)
		return;
	for (size_t i = 0; i < BITMAP_IMAGE_COUNT; ++i) {
		st_pixel_scaler_destroy(presenter->scalers[i]);
		st_rgb_buffer_destroy(presenter->images[i]);
	}
	free(presenter);
}

void
st_console_bitmap_presenter_set_external_scaling_available(
	struct st_console_bitmap_presenter *presenter, bool available)
{
	presenter->software_scaling_only = !available;
}

void
st_console_bitmap_presenter_set_local_animation_clock(
	struct st_console_bitmap_presenter *presenter, bool local)
{
	if (presenter->local_animation_clock != local) {
		presenter->local_animation_clock = local;
		presenter->animation_epoch_pending = true;
	}
}

static bool
cache_console_state(struct st_console_bitmap_presenter *presenter,
	const st_console_t *console, uint32_t now, bool *cursor_changed)
{
	enum st_console_scaling_mode scaling_mode = presenter->software_scaling_only
	    ? ST_CONSOLE_SCALING_INTERNAL : console->scaling_mode;
	struct st_console_cell cursor_style = {
		.foreground = console->style.foreground,
		.background = console->style.background,
		.flags = console->style.flags,
	};
	struct st_console_cell effective_cursor_style;

	st_console_effective_cell_locked(console, &cursor_style,
	    &effective_cursor_style);
	uint32_t cursor_colour = console->blink_mode == ST_CONSOLE_BLINK_PRESTEL
	    ? ST_CONSOLE_COLOR_RGB_VALUE(255, 255, 255)
	    : effective_cursor_style.foreground;
	uint32_t cursor_draw_x;
	uint32_t cursor_columns;
	bool changed;
	bool old_cursor_composed = presenter->cursor_composed;
	struct st_console_rect old_cursor = presenter->cursor_rectangle;
	uint32_t old_cursor_colour = presenter->cursor_colour;
	uint16_t old_cursor_index_base = presenter->cursor_index_base;
	uint16_t old_cursor_index_xor = presenter->cursor_index_xor;

	st_console_cursor_span_locked(console, &cursor_draw_x, &cursor_columns);
	changed = !presenter->initialized
	    || presenter->aspect_width != console->aspect_width
	    || presenter->aspect_height != console->aspect_height
	    || presenter->scaling_mode != scaling_mode
	    || presenter->preserve_window_size != console->preserve_window_size
	    || presenter->blocky_scaling != console->blocky_scaling;

	(void)st_mutex_lock(&console->display->presentation_mutex);
	bool shared_animation = !presenter->local_animation_clock
	    && console->display->presentation_animation_initialized
	    && console->display->presentation_blink_mode == console->blink_mode;
	if (shared_animation)
		presenter->epoch
		    = (uint32_t)console->display->presentation_animation_epoch;
	else if (!presenter->initialized
	    || presenter->blink_mode != console->blink_mode
	    || presenter->animation_epoch_pending)
		presenter->epoch = now;
	presenter->animation_epoch_pending = false;
	(void)st_mutex_unlock(&console->display->presentation_mutex);
	changed = changed || presenter->blink_mode != console->blink_mode;
	presenter->columns = console->columns;
	presenter->rows = console->rows;
	presenter->cell_width = console->cell_width;
	presenter->cell_height = console->cell_height;
	presenter->cursor_x = console->cursor_x;
	presenter->cursor_y = console->cursor_y;
	presenter->cursor_draw_x = cursor_draw_x;
	presenter->cursor_columns = cursor_columns;
	presenter->cursor_colour = cursor_colour;
	presenter->cursor_visible = console->cursor_visible;
	presenter->cursor_shape = console->cursor_shape;
	presenter->cursor_custom = console->cursor_custom;
	presenter->custom_cursor = console->custom_cursor;
	presenter->blink_mode = console->blink_mode;
	presenter->scaling_mode = scaling_mode;
	presenter->preserve_window_size = console->preserve_window_size;
	presenter->blocky_scaling = console->blocky_scaling;
	presenter->aspect_width = console->aspect_width;
	presenter->aspect_height = console->aspect_height;
	presenter->initialized = true;
	presenter->pixel_cursor_enabled = console->pixel_cursor.enabled;
	presenter->cursor_index_base = 0;
	presenter->cursor_index_xor = 0;
	if (presenter->pixel_cursor_enabled) {
		struct st_console_rect r = console->pixel_cursor.rectangle;
		uint32_t width = console->pixels->width, height = console->pixels->height;
		if (r.x >= width || r.y >= height)
			r = (struct st_console_rect){0};
		else {
			if (r.width > width - r.x) r.width = width - r.x;
			if (r.height > height - r.y) r.height = height - r.y;
		}
		presenter->cursor_rectangle = r;
		presenter->cursor_colour = console->pixel_cursor.colour;
		presenter->cursor_composed = console->pixel_cursor.visible && r.width != 0 && r.height != 0;
	} else {
		presenter->cursor_rectangle = cursor_rectangle(presenter);
		if (presenter->cursor_custom && presenter->custom_cursor.index_xor != 0) {
			presenter->cursor_rectangle.x = presenter->cursor_x * presenter->cell_width;
			presenter->cursor_rectangle.width = presenter->cell_width;
			presenter->cursor_index_base = presenter->custom_cursor.index_base;
			presenter->cursor_index_xor = presenter->custom_cursor.index_xor;
		}
		presenter->cursor_composed = (presenter->cursor_custom
			? presenter->custom_cursor.visible : presenter->cursor_visible)
			&& presenter->cursor_x < presenter->columns
			&& presenter->cursor_y < presenter->rows
			&& presenter->cursor_rectangle.width != 0
			&& presenter->cursor_rectangle.height != 0;
	}
	bool rectangle_changed = !st_console_rects_equal(&old_cursor, &presenter->cursor_rectangle);
	bool bank_changed = old_cursor_index_base != presenter->cursor_index_base
	    || old_cursor_index_xor != presenter->cursor_index_xor;
	if (!shared_animation && presenter->cursor_composed && presenter->cursor_index_xor != 0
	    && (!old_cursor_composed || rectangle_changed || bank_changed))
		presenter->epoch = now;
	*cursor_changed = old_cursor_composed != presenter->cursor_composed
	    || ((old_cursor_composed || presenter->cursor_composed)
	    && (rectangle_changed || bank_changed || old_cursor_colour != presenter->cursor_colour));
	return changed;
}


static void
accumulate_full_damage(struct st_console_bitmap_presenter *presenter)
{
	presenter->damage = (struct st_console_rect){
		0, 0, presenter->images[0]->width, presenter->images[0]->height,
	};
	presenter->damage_valid = true;
	presenter->damage_full = true;
}

static bool
collect_cell_damage(struct st_console_bitmap_presenter *presenter,
	const st_console_t *console)
{
	bool valid = false;
	struct st_console_rect damage = {0};

	for (uint32_t y = 0; y < console->rows; ++y) {
		uint32_t x = 0;
		if (st_console_row_dirty(console, y)) {
			const struct st_console_rect row = {0, y * presenter->cell_height,
			    console->columns * presenter->cell_width, presenter->cell_height};
			st_console_rect_accumulate(&damage, &valid, &row);
			continue;
		}

		while (x < console->columns) {
			uint32_t first;
			struct st_console_rect rectangle;

			while (x < console->columns
			    && (st_console_cell_at(console, (size_t)y * console->columns + x)->flags
			    & ST_CONSOLE_CELL_DIRTY) == 0)
				++x;
			first = x;
			while (x < console->columns
			    && (st_console_cell_at(console, (size_t)y * console->columns + x)->flags
			    & ST_CONSOLE_CELL_DIRTY) != 0)
				++x;
			if (first == x)
				continue;
			rectangle = (struct st_console_rect){
				.x = first * presenter->cell_width,
				.y = y * presenter->cell_height,
				.width = (x - first) * presenter->cell_width,
				.height = presenter->cell_height,
			};
			st_console_rect_accumulate(&damage, &valid, &rectangle);
		}
	}
	if (valid)
		st_console_rect_accumulate(&presenter->damage,
		    &presenter->damage_valid, &damage);
	return valid;
}

static bool
resolve_images(struct st_console_bitmap_presenter *presenter,
	const st_console_t *console, const struct st_console_rect *rectangle)
{
	bool changed = resolve_rectangle(presenter,
	    presenter->images[BITMAP_PRIMARY], console, false, rectangle);

	changed = resolve_rectangle(presenter,
	    presenter->images[BITMAP_ALTERNATE], console, true, rectangle)
	    || changed;
	changed = copy_rgb_rectangle(presenter->images[BITMAP_PRIMARY_CURSOR],
	    presenter->images[BITMAP_PRIMARY], rectangle) || changed;
	changed = copy_rgb_rectangle(presenter->images[BITMAP_ALTERNATE_CURSOR],
	    presenter->images[BITMAP_ALTERNATE], rectangle) || changed;
	if (presenter->cursor_composed && presenter->cursor_index_xor != 0) {
		changed = compose_index_cursor(presenter,
		    presenter->images[BITMAP_PRIMARY_CURSOR], console, false) || changed;
		changed = compose_index_cursor(presenter,
		    presenter->images[BITMAP_ALTERNATE_CURSOR], console, true) || changed;
	} else if (presenter->cursor_composed) {
		changed = fill_rectangle(presenter->images[BITMAP_PRIMARY_CURSOR],
		    &presenter->cursor_rectangle,
		    colour_value(presenter, presenter->cursor_colour)) || changed;
		changed = fill_rectangle(presenter->images[BITMAP_ALTERNATE_CURSOR],
		    &presenter->cursor_rectangle,
		    colour_value(presenter, presenter->cursor_colour)) || changed;
	}
	return changed;
}

static size_t
count_differences(const struct st_rgb_buffer *left,
	const struct st_rgb_buffer *right,
	const struct st_console_rect *rectangle)
{
	size_t count = 0;

	for (uint32_t y = 0; y < rectangle->height; ++y) {
		size_t offset = (size_t)(rectangle->y + y) * left->width
		    + rectangle->x;

		for (uint32_t x = 0; x < rectangle->width; ++x) {
			if (left->pixels[offset + x] != right->pixels[offset + x])
				++count;
		}
	}
	return count;
}

static int
upload_image(struct st_console_bitmap_presenter *presenter,
	uint8_t identity, const struct st_rgb_buffer *pixels,
	const struct st_console_rect *rectangle, st_console_bitmap_upload_t upload,
	void *context)
{
	int status = upload == NULL || rectangle->width == 0 || rectangle->height == 0 ? 0 : upload(context, pixels, rectangle,
	    &presenter->images[identity], presenter->generation);

	if (status == 0) {
		presenter->uploaded_image = identity;
		presenter->uploaded_generation = presenter->generation;
		presenter->upload_initialized = true;
		presenter->upload_retry = false;
	}
	else
		presenter->upload_retry = true;
	return status;
}

int
st_console_bitmap_presenter_frame(
	struct st_console_bitmap_presenter *presenter,
	const st_console_t *console, uint32_t output_width,
	uint32_t output_height, uint32_t now,
	st_console_bitmap_upload_t upload, void *context,
	struct st_console_bitmap_frame *frame)
{
	struct st_console_rect old_cursor;
	struct st_console_rect previous_damage;
	struct st_console_rect destination;
	struct st_console_rect upload_damage;
	const struct st_rgb_buffer *pixels;
	enum st_console_scaling_mode previous_scaling;
	bool cursor_changed = false;
	bool content_changed = false;
	bool images_were_valid;
	bool state_changed = false;
	bool layout_changed;
	bool alternate;
	bool cursor_phase;
	bool use_cursor;
	bool selection_changed;
	bool full_upload;
	bool force_backend_upload = false;
	bool previous_damage_valid;
	bool previous_damage_full;
	uint64_t previous_damage_generation;
	bool new_damage = false;
	bool uploaded = false;
	uint8_t identity;
	int status = 0;

	if (presenter == NULL || frame == NULL || output_width == 0
	    || output_height == 0)
		return IO_ERROR_INVALID_ARGUMENT;
	presenter->invalid_colour = false;
	old_cursor = presenter->cursor_rectangle;
	previous_damage = presenter->damage;
	previous_damage_valid = presenter->damage_valid;
	previous_damage_full = presenter->damage_full;
	previous_damage_generation = presenter->damage_generation;
	previous_scaling = presenter->scaling_mode;
	if (console != NULL) {
		presenter->damage_valid = false;
		presenter->damage_full = false;
		if (console->pixels == NULL)
			return IO_ERROR_INVALID_ARGUMENT;
		force_backend_upload = console->pixel_surface_dirty;
		if (console->pixels->width != presenter->images[0]->width
		    || console->pixels->height != presenter->images[0]->height) {
			status = replace_buffers(presenter, console->pixels);
			if (status != 0)
				return status;
		}
		state_changed = cache_console_state(presenter, console, now,
		    &cursor_changed);
		if (console->palette_dirty) {
			memcpy(presenter->palette, console->palette,
			    sizeof(presenter->palette));
			accumulate_full_damage(presenter);
		}
		if (!presenter->images_valid || console->pixel_surface_dirty)
			accumulate_full_damage(presenter);
		else {
			(void)collect_cell_damage(presenter, console);
			if (cursor_changed) {
				if (old_cursor.width != 0 && old_cursor.height != 0)
					st_console_rect_accumulate(&presenter->damage,
					    &presenter->damage_valid, &old_cursor);
				if (presenter->cursor_rectangle.width != 0
				    && presenter->cursor_rectangle.height != 0)
					st_console_rect_accumulate(&presenter->damage,
					    &presenter->damage_valid,
					    &presenter->cursor_rectangle);
			}
		}
		if (presenter->damage_valid) {
			images_were_valid = presenter->images_valid;
			if (presenter->images_valid)
				presenter->plane_difference_count -= count_differences(
				    presenter->images[BITMAP_PRIMARY],
				    presenter->images[BITMAP_ALTERNATE],
				    &presenter->damage);
			else
				presenter->plane_difference_count = 0;
			content_changed = resolve_images(presenter, console,
			    &presenter->damage) || !images_were_valid || cursor_changed;
			presenter->plane_difference_count += count_differences(
			    presenter->images[BITMAP_PRIMARY],
			    presenter->images[BITMAP_ALTERNATE],
			    &presenter->damage);
			presenter->planes_differ
			    = presenter->plane_difference_count != 0;
			presenter->images_valid = true;
			if (content_changed) {
				++presenter->generation;
				if (presenter->generation == 0)
					++presenter->generation;
				presenter->damage_generation = presenter->generation;
				new_damage = true;
			}
			else {
				presenter->damage = previous_damage;
				presenter->damage_valid = previous_damage_valid;
				presenter->damage_full = previous_damage_full;
				presenter->damage_generation
				    = previous_damage_generation;
			}
		}
	}
	else if (!presenter->initialized || !presenter->images_valid)
		return IO_ERROR_INVALID_ARGUMENT;

	animation_state(presenter->blink_mode, now - presenter->epoch,
	    &alternate, &cursor_phase);
	if (!presenter->planes_differ)
		alternate = false;
	use_cursor = presenter->cursor_composed;
	if (use_cursor && !presenter->pixel_cursor_enabled && (!presenter->cursor_custom
	    || presenter->custom_cursor.blink))
		use_cursor = cursor_phase;
	identity = (uint8_t)(alternate ? BITMAP_ALTERNATE : BITMAP_PRIMARY);
	if (use_cursor)
		++identity;
	selection_changed = !presenter->upload_initialized
	    || identity != presenter->selected_image;
	presenter->selected_image = identity;
	destination = fit_rectangle(output_width, output_height,
	    presenter->images[0]->width, presenter->images[0]->height,
	    presenter->aspect_width, presenter->aspect_height, presenter->preserve_window_size);
	if (!presenter->preserve_window_size && (destination.width < presenter->images[0]->width
	    || destination.height < presenter->images[0]->height))
		return IO_ERROR_INVALID_ARGUMENT;
	layout_changed = presenter->output_width != output_width
	    || presenter->output_height != output_height;
	presenter->output_width = output_width;
	presenter->output_height = output_height;

	pixels = presenter->images[identity];
	upload_damage = presenter->damage;
	full_upload = presenter->upload_retry || !presenter->upload_initialized
	    || presenter->uploaded_image != identity
	    || (presenter->uploaded_generation != presenter->generation
	    && presenter->uploaded_generation + 1 != presenter->generation)
	    || (new_damage && presenter->damage_full)
	    || previous_scaling != presenter->scaling_mode
	    || force_backend_upload;
	if (presenter->scaling_mode == ST_CONSOLE_SCALING_INTERNAL) {
		bool cache_current = presenter->scaled[identity] != NULL
		    && presenter->scaled_generation[identity] == presenter->generation
		    && presenter->scaled[identity]->width == destination.width
		    && presenter->scaled[identity]->height == destination.height
		    && presenter->scaled_blocky[identity]
		    == presenter->blocky_scaling;

		if (!cache_current) {
			bool regional = presenter->scaled[identity] != NULL
			    && presenter->scaled_generation[identity] + 1
			    == presenter->generation && presenter->damage_valid
			    && presenter->damage_generation == presenter->generation
			    && !presenter->damage_full
			    && presenter->scaled[identity]->width == destination.width
			    && presenter->scaled[identity]->height == destination.height
			    && presenter->scaled_blocky[identity]
			    == presenter->blocky_scaling;

			if (regional) {
				struct st_pixel_rect source_damage = {
					presenter->damage.x, presenter->damage.y,
					presenter->damage.width, presenter->damage.height,
				};
				struct st_pixel_rect scaled_damage;

				status = st_pixel_scaler_scale_region(
				    presenter->scalers[identity],
				    &presenter->scaled[identity], pixels,
				    &source_damage, &scaled_damage);
				upload_damage = (struct st_console_rect){
					scaled_damage.x, scaled_damage.y,
					scaled_damage.width, scaled_damage.height,
				};
			}
			else {
				status = st_pixel_scaler_scale(presenter->scalers[identity],
				    &presenter->scaled[identity], pixels,
				    destination.width, destination.height,
				    presenter->blocky_scaling
				    ? ST_PIXEL_SCALING_BLOCKY
				    : ST_PIXEL_SCALING_POINTY);
				full_upload = true;
			}
			if (status == 0)
				presenter->scaled_generation[identity]
				    = presenter->generation;
			if (status == 0)
				presenter->scaled_blocky[identity]
				    = presenter->blocky_scaling;
		}
		pixels = presenter->scaled[identity];
	}
	else if (previous_scaling != presenter->scaling_mode)
		full_upload = true;

	if (status == 0 && (full_upload || new_damage
	    || selection_changed)) {
		if (full_upload || selection_changed) {
			upload_damage = (struct st_console_rect){
				0, 0, pixels->width, pixels->height,
			};
		}
		status = upload_image(presenter, identity, pixels, &upload_damage,
		    upload, context);
		uploaded = status == 0;
	}
	frame->pixels = pixels;
	frame->image_identity = &presenter->images[identity];
	frame->generation = presenter->generation;
	frame->destination = destination;
	frame->source_width = presenter->images[0]->width;
	frame->source_height = presenter->images[0]->height;
	frame->cell_width = presenter->cell_width;
	frame->cell_height = presenter->cell_height;
	frame->scaling_mode = presenter->scaling_mode;
	frame->changed = layout_changed || state_changed || selection_changed
	    || new_damage || uploaded;
	if (presenter->invalid_colour)
		fprintf(stderr, "Invalid colour value: %08" PRIx32 "\n",
		    presenter->invalid_colour_value);
	return status;
}

void
st_console_bitmap_mouse_position(const struct st_console_bitmap_frame *frame,
	int64_t x, int64_t y, struct st_console_event *event)
{
	uint64_t pixel_x;
	uint64_t pixel_y;

	if (event == NULL)
		return;
	event->data.mouse.x = 0;
	event->data.mouse.y = 0;
	event->data.mouse.pixel_x = -1;
	event->data.mouse.pixel_y = -1;
	if (frame == NULL || frame->destination.width == 0
	    || frame->destination.height == 0 || frame->source_width == 0
	    || frame->source_height == 0 || frame->cell_width == 0
	    || frame->cell_height == 0)
		return;
	x -= frame->destination.x;
	y -= frame->destination.y;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if ((uint64_t)x >= frame->destination.width)
		x = frame->destination.width - 1;
	if ((uint64_t)y >= frame->destination.height)
		y = frame->destination.height - 1;
	pixel_x = (uint64_t)x * frame->source_width / frame->destination.width;
	pixel_y = (uint64_t)y * frame->source_height / frame->destination.height;
	if (pixel_x > INT32_MAX)
		pixel_x = INT32_MAX;
	if (pixel_y > INT32_MAX)
		pixel_y = INT32_MAX;
	event->data.mouse.pixel_x = (int32_t)pixel_x;
	event->data.mouse.pixel_y = (int32_t)pixel_y;
	event->data.mouse.x = (int32_t)(pixel_x / frame->cell_width);
	event->data.mouse.y = (int32_t)(pixel_y / frame->cell_height);
}
