/* Shared composition and scaling for bitmap console backends. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_internal.h"

#include "io_error.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct st_console_bitmap_presenter {
	struct st_pixel_buffer *source;
	struct st_pixel_buffer *native;
	st_pixel_scaler_t *scaler;
	const struct st_pixel_buffer *scaled;
	uint8_t *blink_cells;
	size_t blink_count;
	size_t cell_count;
	uint32_t cell_width;
	uint32_t cell_height;
	uint32_t columns;
	uint32_t rows;
	uint32_t cursor_x;
	uint32_t cursor_y;
	uint32_t cursor_colour;
	uint32_t output_width;
	uint32_t output_height;
	uint32_t aspect_width;
	uint32_t aspect_height;
	uint32_t epoch;
	struct st_console_rect cursor_rectangle;
	enum st_console_cursor_shape cursor_shape;
	enum st_console_blink_mode blink_mode;
	enum st_console_scaling_mode scaling_mode;
	struct st_console_custom_cursor custom_cursor;
	bool cursor_custom;
	bool cursor_visible;
	bool cursor_drawn;
	bool alternate;
	bool blocky_scaling;
	bool initialized;
	bool force_upload;
};

static uint32_t
colour_value(const st_console_t *console, uint32_t colour)
{
	if (ST_CONSOLE_COLOR_IS_RGB(colour))
		return colour & UINT32_C(0xffffff);
	return console->palette[colour & UINT32_C(0xff)];
}

static void
copy_rectangle(struct st_pixel_buffer *destination,
	const struct st_pixel_buffer *source, bool alternate,
	const struct st_console_rect *rectangle)
{
	const uint32_t *plane = alternate && source->alternate != NULL
	    ? source->alternate : source->primary;

	for (uint32_t y = 0; y < rectangle->height; ++y) {
		size_t offset = (size_t)(rectangle->y + y) * source->width
		    + rectangle->x;

		memcpy(&destination->primary[offset], &plane[offset],
		    (size_t)rectangle->width * sizeof(plane[0]));
	}
}

static void
copy_console_rectangle(struct st_pixel_buffer *destination,
	const st_console_t *console, bool alternate,
	const struct st_console_rect *rectangle)
{
	uint32_t *plane = alternate && destination->alternate != NULL
	    ? destination->alternate : destination->primary;
	size_t offset = (size_t)rectangle->y * destination->width
	    + rectangle->x;

	st_console_copy_pixels_locked(console, &plane[offset],
	    destination->width, alternate, rectangle);
}

static void
fill_rectangle(struct st_pixel_buffer *buffer,
	const struct st_console_rect *rectangle, uint32_t colour)
{
	for (uint32_t y = 0; y < rectangle->height; ++y) {
		uint32_t *row = &buffer->primary[(size_t)(rectangle->y + y)
		    * buffer->width + rectangle->x];

		for (uint32_t x = 0; x < rectangle->width; ++x)
			row[x] = colour;
	}
}

static bool
same_custom_cursor(const struct st_console_custom_cursor *left,
	const struct st_console_custom_cursor *right)
{
	return left->start == right->start && left->end == right->end
	    && left->range == right->range && left->visible == right->visible
	    && left->blink == right->blink;
}

static struct st_console_rect
cursor_rectangle(const struct st_console_bitmap_presenter *presenter)
{
	struct st_console_rect result = {
		.x = presenter->cursor_x * presenter->cell_width,
		.y = presenter->cursor_y * presenter->cell_height,
		.width = presenter->cell_width,
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
		*cursor = !*alternate;
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

int
st_console_snap_window_size(st_console_t *console,
	uint32_t current_width, uint32_t current_height, bool larger,
	uint32_t maximum_width, uint32_t maximum_height,
	uint32_t *width, uint32_t *height)
{
	uint32_t minimum_width;
	uint32_t minimum_height;
	uint32_t scale;
	uint32_t candidate_width;
	uint32_t candidate_height;
	uint64_t width_scale;
	uint64_t height_scale;
	uint32_t source_width;
	uint32_t source_height;
	uint32_t aspect_width;
	uint32_t aspect_height;
	int status;

	if (console == NULL || current_width == 0
	    || current_height == 0 || width == NULL || height == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_rwlock_read_lock(&console->state_lock) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (console->pixels == NULL) {
		(void)st_rwlock_unlock(&console->state_lock);
		return IO_ERROR_INVALID_ARGUMENT;
	}
	source_width = console->pixels->width;
	source_height = console->pixels->height;
	aspect_width = console->aspect_width;
	aspect_height = console->aspect_height;
	(void)st_rwlock_unlock(&console->state_lock);
	status = st_console_aspect_correct_size(source_width, source_height,
	    aspect_width, aspect_height, &minimum_width, &minimum_height);
	if (status != 0)
		return status;
	width_scale = current_width / minimum_width;
	height_scale = current_height / minimum_height;
	scale = (uint32_t)(width_scale < height_scale
	    ? width_scale : height_scale);
	if (scale < 1)
		scale = 1;
	if (larger) {
		if (scale == UINT32_MAX)
			return IO_ERROR_OUT_OF_RANGE;
		++scale;
	}
	else {
		status = st_console_scaled_window_size(source_width,
		    source_height, aspect_width, aspect_height, scale,
		    &candidate_width, &candidate_height);
		if (status != 0)
			return status;
		if (scale > 1 && candidate_width >= current_width
		    && candidate_height >= current_height)
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

int
st_console_preserve_window_scale(const st_console_t *console,
	uint32_t source_width, uint32_t source_height, uint32_t current_width,
	uint32_t current_height, uint32_t *width, uint32_t *height,
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

	if (console == NULL || console->pixels == NULL || width == NULL
	    || height == NULL || minimum_width == NULL || minimum_height == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_console_aspect_correct_size(console->pixels->width,
	    console->pixels->height, console->aspect_width,
	    console->aspect_height, &old_minimum_width, &old_minimum_height);
	if (status == 0)
		status = st_console_aspect_correct_size(source_width, source_height,
		    console->aspect_width, console->aspect_height, minimum_width,
		    minimum_height);
	if (status != 0)
		return status;
	if (current_width < old_minimum_width
	    || current_height < old_minimum_height) {
		*width = *minimum_width;
		*height = *minimum_height;
		return 0;
	}
	if (console->aspect_width == 0) {
		fitted_width = current_width;
		fitted_height = current_height;
	}
	else {
		uint64_t width_from_height
		    = ((uint64_t)current_height * console->aspect_width
		    + console->aspect_height / 2U) / console->aspect_height;

		fitted_width = current_width;
		fitted_height = current_height;
		if (width_from_height <= current_width)
			fitted_width = (uint32_t)width_from_height;
		else
			fitted_height = (uint32_t)(((uint64_t)current_width
			    * console->aspect_height + console->aspect_width / 2U)
			    / console->aspect_width);
	}
	width_controls = console->aspect_width == 0
	    ? (uint64_t)fitted_width * console->pixels->height
	    <= (uint64_t)fitted_height * console->pixels->width
	    : ((uint64_t)console->pixels->height * console->aspect_width
	    + console->aspect_height / 2U) / console->aspect_height
	    <= console->pixels->width;
	if (width_controls) {
		numerator = fitted_width;
		denominator = console->pixels->width;
	}
	else {
		numerator = fitted_height;
		denominator = console->pixels->height;
	}
	if (numerator < denominator)
		numerator = denominator;
	scaled_source_width = ((uint64_t)source_width * numerator
	    + denominator / 2U) / denominator;
	scaled_source_height = ((uint64_t)source_height * numerator
	    + denominator / 2U) / denominator;
	if (scaled_source_width > UINT32_MAX
	    || scaled_source_height > UINT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	return st_console_aspect_correct_size((uint32_t)scaled_source_width,
	    (uint32_t)scaled_source_height, console->aspect_width,
	    console->aspect_height, width, height);
}

static struct st_console_rect
fit_rectangle(uint32_t output_width, uint32_t output_height,
	uint32_t source_width, uint32_t source_height, uint32_t aspect_width,
	uint32_t aspect_height)
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
	struct st_pixel_buffer *source = NULL;
	struct st_pixel_buffer *native = NULL;
	int status;

	status = st_pixel_buffer_clone(&source, pixels);
	if (status == 0)
		status = st_pixel_buffer_create(&native, pixels->width,
		    pixels->height, false);
	if (status != 0) {
		st_pixel_buffer_destroy(native);
		st_pixel_buffer_destroy(source);
		return status;
	}
	st_pixel_buffer_destroy(presenter->native);
	st_pixel_buffer_destroy(presenter->source);
	presenter->native = native;
	presenter->source = source;
	presenter->force_upload = true;
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
	if (status == 0)
		status = st_pixel_scaler_create(&presenter->scaler);
	if (status != 0) {
		st_console_bitmap_presenter_destroy(presenter);
		return status;
	}
	presenter->cell_count = (size_t)console->columns * console->rows;
	presenter->blink_cells = calloc(presenter->cell_count,
	    sizeof(presenter->blink_cells[0]));
	if (presenter->blink_cells == NULL) {
		st_console_bitmap_presenter_destroy(presenter);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	for (size_t i = 0; i < presenter->cell_count; ++i) {
		presenter->blink_cells[i] = (console->cells[i].flags
		    & ST_CONSOLE_CELL_BLINK) != 0;
		presenter->blink_count += presenter->blink_cells[i];
	}
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
	st_pixel_scaler_destroy(presenter->scaler);
	st_pixel_buffer_destroy(presenter->native);
	st_pixel_buffer_destroy(presenter->source);
	free(presenter->blink_cells);
	free(presenter);
}

static bool
cache_console_state(struct st_console_bitmap_presenter *presenter,
	const st_console_t *console, uint32_t now)
{
	uint32_t cursor_colour = console->blink_mode == ST_CONSOLE_BLINK_PRESTEL
	    ? UINT32_C(0xffffff)
	    : colour_value(console, console->style.foreground);
	bool changed = !presenter->initialized
	    || presenter->columns != console->columns
	    || presenter->rows != console->rows
	    || presenter->cursor_x != console->cursor_x
	    || presenter->cursor_y != console->cursor_y
	    || presenter->cursor_colour != cursor_colour
	    || presenter->cursor_visible != console->cursor_visible
	    || presenter->cursor_shape != console->cursor_shape
	    || presenter->cursor_custom != console->cursor_custom
	    || !same_custom_cursor(&presenter->custom_cursor,
	    &console->custom_cursor)
	    || presenter->aspect_width != console->aspect_width
	    || presenter->aspect_height != console->aspect_height
	    || presenter->scaling_mode != console->scaling_mode;
	changed = changed
	    || presenter->blocky_scaling != console->blocky_scaling;

	if (!presenter->initialized || presenter->blink_mode != console->blink_mode)
		presenter->epoch = now;
	changed = changed || presenter->blink_mode != console->blink_mode;
	presenter->columns = console->columns;
	presenter->rows = console->rows;
	presenter->cursor_x = console->cursor_x;
	presenter->cursor_y = console->cursor_y;
	presenter->cursor_colour = cursor_colour;
	presenter->cursor_visible = console->cursor_visible;
	presenter->cursor_shape = console->cursor_shape;
	presenter->cursor_custom = console->cursor_custom;
	presenter->custom_cursor = console->custom_cursor;
	presenter->blink_mode = console->blink_mode;
	presenter->scaling_mode = console->scaling_mode;
	presenter->blocky_scaling = console->blocky_scaling;
	presenter->aspect_width = console->aspect_width;
	presenter->aspect_height = console->aspect_height;
	presenter->initialized = true;
	return changed;
}

static int
reset_blink_cells(struct st_console_bitmap_presenter *presenter,
	const st_console_t *console)
{
	size_t count = (size_t)console->columns * console->rows;
	uint8_t *cells = calloc(count, sizeof(cells[0]));

	if (cells == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	presenter->blink_count = 0;
	for (size_t i = 0; i < count; ++i) {
		cells[i] = (console->cells[i].flags
		    & ST_CONSOLE_CELL_BLINK) != 0;
		presenter->blink_count += cells[i];
	}
	free(presenter->blink_cells);
	presenter->blink_cells = cells;
	presenter->cell_count = count;
	return 0;
}

static void
refresh_blink_cells(struct st_console_bitmap_presenter *presenter,
	const st_console_t *console)
{
	presenter->blink_count = 0;
	for (size_t i = 0; i < presenter->cell_count; ++i) {
		presenter->blink_cells[i] = (console->cells[i].flags
		    & ST_CONSOLE_CELL_BLINK) != 0;
		presenter->blink_count += presenter->blink_cells[i];
	}
}

static int
upload_rectangle(struct st_console_bitmap_presenter *presenter,
	st_console_bitmap_upload_t upload, void *context,
	const struct st_console_rect *rectangle)
{
	int status = upload == NULL ? 0
	    : upload(context, presenter->native, rectangle);

	if (status != 0)
		presenter->force_upload = true;
	return status;
}

struct scaled_upload {
	struct st_console_bitmap_presenter *presenter;
	st_console_bitmap_upload_t upload;
	void *context;
};

static int
upload_scaled_rectangle(void *context, const struct st_pixel_buffer *pixels,
	const struct st_console_rect *rectangle)
{
	struct scaled_upload *scaled_upload = context;
	struct st_pixel_rect damage = {
		.x = rectangle->x,
		.y = rectangle->y,
		.width = rectangle->width,
		.height = rectangle->height,
	};
	struct st_pixel_rect scaled_damage;
	struct st_console_rect upload_rectangle;
	int status;

	status = st_pixel_scaler_scale_region(scaled_upload->presenter->scaler,
	    &scaled_upload->presenter->scaled, pixels->primary, pixels->width,
	    pixels->height, &damage, &scaled_damage);
	if (status != 0)
		return status;
	upload_rectangle = (struct st_console_rect){
		.x = scaled_damage.x,
		.y = scaled_damage.y,
		.width = scaled_damage.width,
		.height = scaled_damage.height,
	};
	status = scaled_upload->upload == NULL ? 0
	    : scaled_upload->upload(scaled_upload->context,
	    scaled_upload->presenter->scaled, &upload_rectangle);
	if (status != 0)
		scaled_upload->presenter->force_upload = true;
	return status;
}

static int
compose_full(struct st_console_bitmap_presenter *presenter,
	bool draw_cursor, st_console_bitmap_upload_t upload, void *context)
{
	struct st_console_rect full = {0, 0, presenter->source->width,
	    presenter->source->height};

	copy_rectangle(presenter->native, presenter->source,
	    presenter->alternate, &full);
	if (draw_cursor)
		fill_rectangle(presenter->native, &presenter->cursor_rectangle,
		    presenter->cursor_colour);
	return upload_rectangle(presenter, upload, context, &full);
}

static int
sync_dirty_cells(struct st_console_bitmap_presenter *presenter,
	const st_console_t *console, bool draw_cursor,
	st_console_bitmap_upload_t upload, void *context, bool *changed)
{
	int status = 0;

	for (uint32_t y = 0; y < console->rows && status == 0; ++y) {
		uint32_t x = 0;

		while (x < console->columns) {
			uint32_t first;
			struct st_console_rect rectangle;

			while (x < console->columns
			    && (console->cells[(size_t)y * console->columns + x].flags
			    & ST_CONSOLE_CELL_DIRTY) == 0)
				++x;
			first = x;
			while (x < console->columns
			    && (console->cells[(size_t)y * console->columns + x].flags
			    & ST_CONSOLE_CELL_DIRTY) != 0)
				++x;
			if (first == x)
				continue;
			for (uint32_t column = first; column < x; ++column) {
				size_t index = (size_t)y * console->columns + column;
				uint8_t blink = (console->cells[index].flags
				    & ST_CONSOLE_CELL_BLINK) != 0;

				if (blink != presenter->blink_cells[index]) {
					if (blink)
						++presenter->blink_count;
					else
						--presenter->blink_count;
					presenter->blink_cells[index] = blink;
				}
			}
			rectangle = (struct st_console_rect){
				.x = first * presenter->cell_width,
				.y = y * presenter->cell_height,
				.width = (x - first) * presenter->cell_width,
				.height = presenter->cell_height,
			};
			copy_console_rectangle(presenter->source, console, false,
			    &rectangle);
			if (presenter->source->alternate != NULL)
				copy_console_rectangle(presenter->source, console, true,
				    &rectangle);
			copy_rectangle(presenter->native, presenter->source,
			    presenter->alternate, &rectangle);
			if (draw_cursor)
				fill_rectangle(presenter->native,
				    &presenter->cursor_rectangle,
				    presenter->cursor_colour);
			status = upload_rectangle(presenter, upload, context,
			    &rectangle);
			*changed = true;
		}
	}
	return status;
}

static int
sync_blink_cells(struct st_console_bitmap_presenter *presenter,
	bool draw_cursor, st_console_bitmap_upload_t upload, void *context,
	bool *changed)
{
	int status = 0;

	for (uint32_t y = 0; y < presenter->rows && status == 0; ++y) {
		uint32_t x = 0;

		while (x < presenter->columns) {
			uint32_t first;
			struct st_console_rect rectangle;

			while (x < presenter->columns
			    && presenter->blink_cells[(size_t)y
			    * presenter->columns + x] == 0)
				++x;
			first = x;
			while (x < presenter->columns
			    && presenter->blink_cells[(size_t)y
			    * presenter->columns + x] != 0)
				++x;
			if (first == x)
				continue;
			rectangle = (struct st_console_rect){
				.x = first * presenter->cell_width,
				.y = y * presenter->cell_height,
				.width = (x - first) * presenter->cell_width,
				.height = presenter->cell_height,
			};
			copy_rectangle(presenter->native, presenter->source,
			    presenter->alternate, &rectangle);
			if (draw_cursor)
				fill_rectangle(presenter->native,
				    &presenter->cursor_rectangle,
				    presenter->cursor_colour);
			status = upload_rectangle(presenter, upload, context,
			    &rectangle);
			*changed = true;
		}
	}
	return status;
}

static int
upload_cursor_change(struct st_console_bitmap_presenter *presenter,
	struct st_console_rect old_cursor, bool old_drawn, bool draw_cursor,
	st_console_bitmap_upload_t upload, void *context)
{
	int status = 0;

	if (old_drawn && old_cursor.width != 0 && old_cursor.height != 0) {
		copy_rectangle(presenter->native, presenter->source,
		    presenter->alternate, &old_cursor);
		status = upload_rectangle(presenter, upload, context, &old_cursor);
	}
	if (status == 0 && draw_cursor && presenter->cursor_rectangle.width != 0
	    && presenter->cursor_rectangle.height != 0) {
		fill_rectangle(presenter->native, &presenter->cursor_rectangle,
		    presenter->cursor_colour);
		status = upload_rectangle(presenter, upload, context,
		    &presenter->cursor_rectangle);
	}
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
	struct st_console_rect destination;
	bool old_cursor_drawn;
	bool alternate;
	bool cursor_phase;
	bool draw_cursor;
	bool state_changed = false;
	bool phase_changed;
	bool layout_changed;
	bool changed = false;
	enum st_console_scaling_mode previous_scaling;
	bool previous_blocky;
	int status = 0;

	if (presenter == NULL || frame == NULL || output_width == 0
	    || output_height == 0)
		return IO_ERROR_INVALID_ARGUMENT;
	old_cursor = presenter->cursor_rectangle;
	old_cursor_drawn = presenter->cursor_drawn;
	previous_scaling = presenter->scaling_mode;
	previous_blocky = presenter->blocky_scaling;
	if (console != NULL) {
		bool dimensions_changed = console->columns != presenter->columns
		    || console->rows != presenter->rows;

		if (console->pixels == NULL)
			return IO_ERROR_INVALID_ARGUMENT;
		if (console->pixels->width != presenter->source->width
		    || console->pixels->height != presenter->source->height) {
			status = replace_buffers(presenter, console->pixels);
			if (status != 0)
				return status;
		}
		if (dimensions_changed) {
			status = reset_blink_cells(presenter, console);
			if (status != 0)
				return status;
		}
		state_changed = cache_console_state(presenter, console, now);
		if (console->pixel_surface_dirty)
			presenter->force_upload = true;
		if (previous_scaling != presenter->scaling_mode)
			presenter->force_upload = true;
		if (presenter->force_upload) {
			struct st_console_rect full = {0, 0,
			    presenter->source->width, presenter->source->height};

			copy_console_rectangle(presenter->source, console, false,
			    &full);
			if (presenter->source->alternate != NULL)
				copy_console_rectangle(presenter->source, console, true,
				    &full);
			refresh_blink_cells(presenter, console);
		}
	}
	else if (!presenter->initialized)
		return IO_ERROR_INVALID_ARGUMENT;
	animation_state(presenter->blink_mode, now - presenter->epoch,
	    &alternate, &cursor_phase);
	if (presenter->blink_count == 0)
		alternate = false;
	draw_cursor = presenter->cursor_custom
	    ? presenter->custom_cursor.visible
	    : presenter->cursor_visible;
	if ((presenter->cursor_custom && presenter->custom_cursor.blink)
	    || !presenter->cursor_custom)
		draw_cursor = draw_cursor && cursor_phase;
	presenter->cursor_rectangle = cursor_rectangle(presenter);
	draw_cursor = draw_cursor && presenter->cursor_x < presenter->columns
	    && presenter->cursor_y < presenter->rows
	    && presenter->cursor_rectangle.width != 0
	    && presenter->cursor_rectangle.height != 0;
	phase_changed = alternate != presenter->alternate;
	destination = fit_rectangle(output_width, output_height,
	    presenter->source->width, presenter->source->height,
	    presenter->aspect_width, presenter->aspect_height);
	layout_changed = presenter->output_width != output_width
	    || presenter->output_height != output_height;
	presenter->output_width = output_width;
	presenter->output_height = output_height;
	presenter->alternate = alternate;

	if (presenter->scaling_mode == ST_CONSOLE_SCALING_EXTERNAL) {
		bool cursor_changed = old_cursor_drawn != draw_cursor
		    || !st_console_rects_equal(&old_cursor,
		    &presenter->cursor_rectangle)
		    || state_changed;

		if (presenter->force_upload) {
			status = compose_full(presenter, draw_cursor, upload, context);
			changed = true;
		}
		else if (console != NULL)
			status = sync_dirty_cells(presenter, console, draw_cursor,
			    upload, context, &changed);
		if (status == 0 && !presenter->force_upload && phase_changed)
			status = sync_blink_cells(presenter, draw_cursor, upload,
			    context, &changed);
		if (status == 0 && !presenter->force_upload
		    && cursor_changed) {
			status = upload_cursor_change(presenter, old_cursor,
			    old_cursor_drawn, draw_cursor, upload, context);
			changed = true;
		}
		if (status == 0)
			presenter->force_upload = false;
		frame->pixels = presenter->native;
	}
	else {
		struct scaled_upload scaled_upload = {
			.presenter = presenter,
			.upload = upload,
			.context = context,
		};
		bool full = presenter->force_upload || previous_blocky
		    != presenter->blocky_scaling || layout_changed
		    || presenter->scaled == NULL
		    || presenter->scaled->width != destination.width
		    || presenter->scaled->height != destination.height;
		bool cursor_changed = old_cursor_drawn != draw_cursor
		    || !st_console_rects_equal(&old_cursor,
		    &presenter->cursor_rectangle)
		    || state_changed;

		if (full) {
			if (console != NULL && !presenter->force_upload)
				status = sync_dirty_cells(presenter, console, false,
				    NULL, NULL, &changed);
			if (status != 0)
				presenter->force_upload = true;
		}
		else if (console != NULL)
			status = sync_dirty_cells(presenter, console, draw_cursor,
			    upload_scaled_rectangle, &scaled_upload, &changed);
		if (!full && status == 0 && phase_changed)
			status = sync_blink_cells(presenter, draw_cursor,
			    upload_scaled_rectangle, &scaled_upload, &changed);
		if (!full && status == 0 && cursor_changed) {
			status = upload_cursor_change(presenter, old_cursor,
			    old_cursor_drawn, draw_cursor, upload_scaled_rectangle,
			    &scaled_upload);
			changed = true;
		}
		if (status == 0 && full) {
			status = compose_full(presenter, draw_cursor, NULL, NULL);
			if (status == 0)
				status = st_pixel_scaler_scale(presenter->scaler,
				    &presenter->scaled, presenter->native->primary,
				    presenter->native->width, presenter->native->height,
				    destination.width, destination.height,
				    presenter->blocky_scaling
				    ? ST_PIXEL_SCALING_BLOCKY
				    : ST_PIXEL_SCALING_POINTY);
			if (status == 0) {
				struct st_console_rect full = {0, 0,
				    presenter->scaled->width,
				    presenter->scaled->height};

				status = upload == NULL ? 0
				    : upload(context, presenter->scaled, &full);
				if (status != 0)
					presenter->force_upload = true;
			}
			changed = true;
		}
		if (status == 0)
			presenter->force_upload = false;
		else
			presenter->force_upload = true;
		frame->pixels = presenter->scaled;
	}
	presenter->cursor_drawn = draw_cursor;
	frame->destination = destination;
	frame->scaling_mode = presenter->scaling_mode;
	frame->changed = changed || layout_changed || state_changed;
	return status;
}
