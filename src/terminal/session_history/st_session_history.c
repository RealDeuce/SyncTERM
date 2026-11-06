/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_session_history.h"

#include "io_error.h"
#include "st_threads.h"

#include <stdlib.h>
#include <string.h>

struct st_session_history {
	struct st_console_cell *cells;
	struct st_pixel_buffer **pixels;
	size_t capacity;
	size_t lines;
	uint32_t width;
	enum st_screen_mode mode;
	bool has_status;
	st_mutex_t mutex;
};

void
st_history_free_pixels(struct st_pixel_buffer **pixels, size_t lines)
{
	if (pixels == NULL)
		return;
	for (size_t row = 0; row < lines; ++row)
		st_pixel_buffer_destroy(pixels[row]);
	free(pixels);
}

int
st_history_capture_pixels(struct st_pixel_buffer **result, st_console_t *console,
	const struct st_console_cell *cells, uint32_t columns,
	const struct st_console_rect *pixels)
{
	if (result == NULL || console == NULL || cells == NULL || columns == 0 || pixels == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	bool graphics = false;
	for (uint32_t x = 0; x < columns; ++x)
		graphics |= (cells[x].flags & ST_CONSOLE_CELL_PIXEL_GRAPHICS) != 0;
	if (!graphics) {
		*result = NULL;
		return 0;
	}
	struct st_pixel_buffer *copy = NULL;
	int status = st_console_get_pixels(&copy, console, pixels, false);
	if (status != 0)
		return status;
	size_t count = (size_t)copy->width * copy->height;
	uint32_t maximum = 0;
	bool indexed = false;
	for (size_t i = 0; i < count; ++i) {
		uint32_t colours[2] = {copy->primary[i], copy->alternate[i]};
		for (unsigned plane = 0; plane < 2; ++plane) {
			if (ST_PIXEL_COLOR_IS_RGB(colours[plane]))
				continue;
			indexed = true;
			if (colours[plane] > maximum)
				maximum = colours[plane];
		}
	}
	uint32_t *palette = NULL;
	if (indexed) {
		if (maximum > UINT16_MAX)
			status = IO_ERROR_INVALID_ARGUMENT;
		else {
			palette = malloc(((size_t)maximum + 1) * sizeof(*palette));
			status = palette == NULL ? IO_ERROR_OUT_OF_MEMORY
				: st_console_get_palette(console, 0, palette, (size_t)maximum + 1);
		}
	}
	if (status == 0) {
		for (size_t i = 0; i < count; ++i) {
			if (!ST_PIXEL_COLOR_IS_RGB(copy->primary[i]))
				copy->primary[i] = ST_PIXEL_COLOR_RGB_TAG | palette[copy->primary[i]];
			if (!ST_PIXEL_COLOR_IS_RGB(copy->alternate[i]))
				copy->alternate[i] = ST_PIXEL_COLOR_RGB_TAG | palette[copy->alternate[i]];
		}
		if (memcmp(copy->primary, copy->alternate, count * sizeof(*copy->primary)) == 0)
			st_pixel_buffer_remove_alternate(copy);
	}
	free(palette);
	if (status != 0) {
		st_pixel_buffer_destroy(copy);
		return status;
	}
	*result = copy;
	return 0;
}

int
st_history_resize_pixels(struct st_pixel_buffer **result,
	const struct st_pixel_buffer *source, uint32_t source_columns,
	uint32_t columns, uint32_t cell_width, uint32_t cell_height)
{
	if (result == NULL || source == NULL || source_columns == 0 || columns == 0 ||
		cell_width == 0 || cell_height == 0 || source->width % source_columns != 0 ||
		source->width / source_columns == 0 || columns > UINT32_MAX / cell_width)
		return IO_ERROR_INVALID_ARGUMENT;
	struct st_pixel_buffer *copy = NULL;
	int status = st_pixel_buffer_create(&copy, columns * cell_width, cell_height,
		source->alternate != NULL);
	if (status != 0)
		return status;
	st_pixel_buffer_fill(copy, ST_PIXEL_COLOR_RGB_TAG, ST_PIXEL_COLOR_RGB_TAG);
	uint32_t old_width = source->width / source_columns;
	uint32_t retained = columns < source_columns ? columns : source_columns;
	for (uint32_t y = 0; y < cell_height; ++y) {
		size_t sy = (size_t)((uint64_t)y * source->height / cell_height) * source->width;
		for (uint32_t x = 0; x < retained * cell_width; ++x) {
			size_t from = sy + (size_t)((uint64_t)x * old_width / cell_width);
			size_t to = (size_t)y * copy->width + x;
			copy->primary[to] = source->primary[from];
			if (copy->alternate != NULL)
				copy->alternate[to] = source->alternate[from];
		}
	}
	*result = copy;
	return 0;
}

static bool
cell_count(size_t lines, uint32_t width, size_t *result)
{
	if (width != 0 && lines > SIZE_MAX / width)
		return false;
	*result = lines * width;
	return *result <= SIZE_MAX / sizeof(struct st_console_cell);
}

int
st_session_history_create(st_session_history_t **result, size_t line_capacity)
{
	st_session_history_t *history;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	history = calloc(1, sizeof(*history));
	if (history == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (st_mutex_init(&history->mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS) {
		free(history);
		return IO_ERROR_IO;
	}
	history->capacity = line_capacity;
	*result = history;
	return 0;
}

void
st_session_history_destroy(st_session_history_t *history)
{
	if (history == NULL)
		return;
	free(history->cells);
	st_history_free_pixels(history->pixels, history->lines);
	st_mutex_destroy(&history->mutex);
	free(history);
}

int
st_session_history_set_capacity(st_session_history_t *history,
	size_t line_capacity)
{
	size_t retained;
	size_t count;

	if (history == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&history->mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	retained = history->lines < line_capacity ? history->lines : line_capacity;
	if (!cell_count(retained, history->width, &count)) {
		(void)st_mutex_unlock(&history->mutex);
		return IO_ERROR_OUT_OF_RANGE;
	}
	if (count != 0 && retained != history->lines)
		memmove(history->cells,
		    history->cells + (history->lines - retained) * history->width,
		    count * sizeof(*history->cells));
	if (history->pixels != NULL) {
		for (size_t row = 0; row < history->lines - retained; ++row)
			st_pixel_buffer_destroy(history->pixels[row]);
		memmove(history->pixels, history->pixels + history->lines - retained,
			retained * sizeof(*history->pixels));
		if (retained == 0) {
			free(history->pixels);
			history->pixels = NULL;
		}
	}
	if (retained == 0) {
		free(history->cells);
		history->cells = NULL;
	}
	history->capacity = line_capacity;
	history->lines = retained;
	if (retained == 0)
		history->width = 0;
	(void)st_mutex_unlock(&history->mutex);
	return 0;
}

size_t
st_session_history_capacity(st_session_history_t *history)
{
	size_t result = 0;

	if (history != NULL && st_mutex_lock(&history->mutex) == ST_THREAD_SUCCESS) {
		result = history->capacity;
		(void)st_mutex_unlock(&history->mutex);
	}
	return result;
}

int
st_session_history_publish(st_session_history_t *history,
	const struct st_console_cell *cells, uint32_t width, size_t lines,
	enum st_screen_mode mode, bool has_status, struct st_pixel_buffer **pixels)
{
	struct st_console_cell *copy = NULL;
	size_t retained;
	size_t count;

	if (history == NULL || width == 0 || (lines != 0 && cells == NULL))
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&history->mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	retained = lines < history->capacity ? lines : history->capacity;
	if (!cell_count(retained, width, &count)) {
		(void)st_mutex_unlock(&history->mutex);
		return IO_ERROR_OUT_OF_RANGE;
	}
	if (count != 0) {
		copy = malloc(count * sizeof(*copy));
		if (copy == NULL) {
			(void)st_mutex_unlock(&history->mutex);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		memcpy(copy, cells + (lines - retained) * width,
		    count * sizeof(*copy));
	}
	free(history->cells);
	st_history_free_pixels(history->pixels, history->lines);
	if (pixels != NULL) {
		for (size_t row = 0; row < lines - retained; ++row)
			st_pixel_buffer_destroy(pixels[row]);
		memmove(pixels, pixels + lines - retained, retained * sizeof(*pixels));
		if (retained == 0) {
			free(pixels);
			pixels = NULL;
		}
	}
	history->cells = copy;
	history->pixels = pixels;
	history->width = retained == 0 ? 0 : width;
	history->lines = retained;
	history->mode = mode;
	history->has_status = has_status;
	(void)st_mutex_unlock(&history->mutex);
	return 0;
}

void
st_session_history_clear(st_session_history_t *history)
{
	if (history == NULL || st_mutex_lock(&history->mutex) != ST_THREAD_SUCCESS)
		return;
	free(history->cells);
	st_history_free_pixels(history->pixels, history->lines);
	history->pixels = NULL;
	history->cells = NULL;
	history->width = 0;
	history->lines = 0;
	(void)st_mutex_unlock(&history->mutex);
}

int
st_session_history_copy(st_session_history_t *history,
	struct st_console_cell **cells, uint32_t *width, size_t *lines,
	enum st_screen_mode *mode, bool *has_status)
{
	struct st_console_cell *copy;
	size_t count;

	if (history == NULL || cells == NULL || width == NULL || lines == NULL
	    || mode == NULL || has_status == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&history->mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (history->lines == 0) {
		(void)st_mutex_unlock(&history->mutex);
		return IO_ERROR_NOT_FOUND;
	}
	if (!cell_count(history->lines, history->width, &count)) {
		(void)st_mutex_unlock(&history->mutex);
		return IO_ERROR_OUT_OF_RANGE;
	}
	copy = malloc(count * sizeof(*copy));
	if (copy == NULL) {
		(void)st_mutex_unlock(&history->mutex);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	memcpy(copy, history->cells, count * sizeof(*copy));
	*cells = copy;
	*width = history->width;
	*lines = history->lines;
	*mode = history->mode;
	*has_status = history->has_status;
	(void)st_mutex_unlock(&history->mutex);
	return 0;
}

int
st_session_history_copy_pixels(st_session_history_t *history, size_t row,
	struct st_pixel_buffer **result)
{
	if (history == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&history->mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	int status = 0;
	if (row >= history->lines)
		status = IO_ERROR_OUT_OF_RANGE;
	else if (history->pixels == NULL || history->pixels[row] == NULL)
		*result = NULL;
	else
		status = st_pixel_buffer_clone(result, history->pixels[row]);
	(void)st_mutex_unlock(&history->mutex);
	return status;
}

int
st_session_history_info(st_session_history_t *history, uint32_t *width,
	size_t *lines, enum st_screen_mode *mode, bool *has_status)
{
	if (history == NULL || width == NULL || lines == NULL || mode == NULL
	    || has_status == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&history->mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (history->lines == 0) {
		(void)st_mutex_unlock(&history->mutex);
		return IO_ERROR_NOT_FOUND;
	}
	*width = history->width;
	*lines = history->lines;
	*mode = history->mode;
	*has_status = history->has_status;
	(void)st_mutex_unlock(&history->mutex);
	return 0;
}
