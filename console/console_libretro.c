/* Pull-based bitmap backend for a libretro run loop. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_internal.h"

#include "io_error.h"
#include "st_time.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

struct libretro_state {
	st_mutex_t mutex;
	struct st_console_bitmap_presenter *presenter;
	struct st_console_bitmap_frame frame;
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
};

static uint32_t
milliseconds_now(void)
{
	uint64_t now = 0;

	(void)st_time_monotonic_ms(&now);
	return (uint32_t)now;
}

static int
retain_pixels(void *context, const struct st_pixel_buffer *pixels,
	const struct st_console_rect *rectangle)
{
	struct libretro_state *state = context;
	size_t count;

	if (pixels->height != 0 && pixels->width > SIZE_MAX / pixels->height)
		return IO_ERROR_OUT_OF_RANGE;
	count = (size_t)pixels->width * pixels->height;
	if (pixels->width != state->width || pixels->height != state->height) {
		uint32_t *replacement;

		if (count > SIZE_MAX / sizeof(*replacement))
			return IO_ERROR_OUT_OF_RANGE;
		replacement = malloc(count * sizeof(*replacement));
		if (replacement == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		free(state->pixels);
		state->pixels = replacement;
		state->width = pixels->width;
		state->height = pixels->height;
		memcpy(state->pixels, pixels->primary,
		    count * sizeof(*state->pixels));
		return 0;
	}
	for (uint32_t y = 0; y < rectangle->height; ++y) {
		size_t offset = (size_t)(rectangle->y + y) * pixels->width
		    + rectangle->x;

		memcpy(&state->pixels[offset], &pixels->primary[offset],
		    (size_t)rectangle->width * sizeof(*state->pixels));
	}
	return 0;
}

static int
libretro_present(st_console_t *console)
{
	struct libretro_state *state = console->backend_state;
	int status = st_mutex_lock(&state->mutex);

	if (status != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	status = st_console_bitmap_presenter_frame(state->presenter, console,
	    console->pixels->width, console->pixels->height, milliseconds_now(),
	    retain_pixels, state, &state->frame);
	(void)st_mutex_unlock(&state->mutex);
	return status;
}

static int
libretro_resize(st_console_t *console, uint32_t columns, uint32_t rows)
{
	(void)console;
	(void)columns;
	(void)rows;
	return 0;
}

static void
libretro_destroy(st_console_t *console)
{
	struct libretro_state *state = console->backend_state;

	st_console_bitmap_presenter_destroy(state->presenter);
	st_mutex_destroy(&state->mutex);
	free(state->pixels);
	free(state);
	console->backend_state = NULL;
}

static const struct st_console_backend_ops libretro_ops = {
	.presents_pixels = true,
	.present = libretro_present,
	.resize = libretro_resize,
	.destroy = libretro_destroy,
};

int
st_console_libretro_init(st_console_t *console)
{
	struct libretro_state *state;
	int status;

	if (console->pixels == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	state = calloc(1, sizeof(*state));
	if (state == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (st_mutex_init(&state->mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS) {
		free(state);
		return IO_ERROR_IO;
	}
	status = st_console_bitmap_presenter_create(&state->presenter, console);
	if (status != 0) {
		st_mutex_destroy(&state->mutex);
		free(state);
		return status;
	}
	console->backend_state = state;
	console->backend_ops = &libretro_ops;
	status = libretro_present(console);
	if (status != 0) {
		libretro_destroy(console);
		console->backend_ops = NULL;
	}
	return status;
}

static int
libretro_state_lock(st_console_t *console, struct libretro_state **result)
{
	if (console == NULL || console->backend_ops != &libretro_ops)
		return IO_ERROR_NOT_SUPPORTED;
	*result = console->backend_state;
	return st_mutex_lock(&(*result)->mutex) == ST_THREAD_SUCCESS
	    ? 0 : IO_ERROR_IO;
}

int
st_console_libretro_frame_size(st_console_t *console, uint32_t *width,
	uint32_t *height)
{
	struct libretro_state *state;
	int status;

	if (width == NULL || height == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = libretro_state_lock(console, &state);
	if (status != 0)
		return status;
	*width = state->width;
	*height = state->height;
	(void)st_mutex_unlock(&state->mutex);
	return 0;
}

int
st_console_libretro_frame_copy(st_console_t *console,
	uint32_t *destination, size_t destination_stride)
{
	struct libretro_state *state;
	int status;

	if (destination == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = libretro_state_lock(console, &state);
	if (status != 0)
		return status;
	if (destination_stride < state->width)
		status = IO_ERROR_INVALID_ARGUMENT;
	else {
		for (uint32_t y = 0; y < state->height; ++y)
			memcpy(&destination[(size_t)y * destination_stride],
			    &state->pixels[(size_t)y * state->width],
			    (size_t)state->width * sizeof(*destination));
	}
	(void)st_mutex_unlock(&state->mutex);
	return status;
}
