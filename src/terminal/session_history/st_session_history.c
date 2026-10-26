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
	size_t capacity;
	size_t lines;
	uint32_t width;
	enum st_screen_mode mode;
	bool has_status;
	st_mutex_t mutex;
};

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
	enum st_screen_mode mode, bool has_status)
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
	history->cells = copy;
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
