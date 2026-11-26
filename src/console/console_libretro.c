/* Pull-based bitmap backend for a libretro run loop. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "console_internal.h"
#include "io_error.h"
#include "st_time.h"

struct libretro_state {
	uint32_t                            source_width, source_height;
	struct st_console_bitmap_presenter *presenter;
	struct st_console_bitmap_frame      frame;
	uint32_t                           *pixels;
	uint32_t                            width;
	uint32_t                            height;
};

static uint32_t
milliseconds_now(void)
{
	uint64_t now = 0;

	(void)st_time_monotonic_ms(&now);
	return (uint32_t)now;
}

static int
retain_pixels(void *context, const struct st_rgb_buffer *pixels, const struct st_console_rect *rectangle,
    const void *image_identity, uint64_t generation)
{
	struct libretro_state *state = context;
	size_t                 count;

	(void)image_identity;
	(void)generation;

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
		state->width  = pixels->width;
		state->height = pixels->height;
		memcpy(state->pixels, pixels->pixels, count * sizeof(*state->pixels));
		return 0;
	}
	for (uint32_t y = 0; y < rectangle->height; ++y) {
		size_t offset = (size_t)(rectangle->y + y) * pixels->width + rectangle->x;

		memcpy(&state->pixels[offset], &pixels->pixels[offset],
		    (size_t)rectangle->width * sizeof(*state->pixels));
	}
	return 0;
}

static int
libretro_prepare_frame(st_console_t *console)
{
	struct libretro_state *state = console->display->backend_state;
	state->source_width          = console->pixels->width;
	state->source_height         = console->pixels->height;
	return st_console_bitmap_prepare_frame(state->presenter, console, milliseconds_now());
}

static int
libretro_show_frame(st_console_display_t *display)
{
	struct libretro_state *state = display->backend_state;
	return st_console_bitmap_presenter_frame(state->presenter, NULL, state->source_width, state->source_height,
	    milliseconds_now(), retain_pixels, state, &state->frame);
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
libretro_destroy(st_console_display_t *display)
{
	struct libretro_state *state = display->backend_state;

	st_console_bitmap_presenter_destroy(state->presenter);
	free(state->pixels);
	free(state);
	display->backend_state = NULL;
}

static const struct st_console_backend_ops libretro_ops = {
    .presents_pixels = true,
    .pull_present    = true,
    .prepare_frame   = libretro_prepare_frame,
    .show_frame      = libretro_show_frame,
    .resize          = libretro_resize,
    .destroy         = libretro_destroy,
};

int
st_console_libretro_init(st_console_t *console)
{
	struct libretro_state *state;
	int                    status;

	if (console->pixels == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	state = calloc(1, sizeof(*state));
	if (state == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	status = st_console_bitmap_presenter_create(&state->presenter, console);
	if (status != 0) {
		free(state);
		return status;
	}
	console->display->backend_state = state;
	console->display->backend_ops   = &libretro_ops;
	status                          = libretro_prepare_frame(console);
	if (status == 0)
		status = libretro_show_frame(console->display);
	if (status != 0) {
		libretro_destroy(console->display);
		console->display->backend_ops = NULL;
	}
	return status;
}

static int
libretro_state_lock(st_console_display_t *display, struct libretro_state **result)
{
	if (display == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&display->backend_mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (display->backend_ops != &libretro_ops) {
		(void)st_mutex_unlock(&display->backend_mutex);
		return IO_ERROR_NOT_SUPPORTED;
	}
	*result = display->backend_state;
	return 0;
}

int
st_console_display_libretro_map_pointer(st_console_display_t *display, int32_t x, int32_t y,
    struct st_console_event *event)
{
	struct libretro_state *state;
	int                    status;
	if (event == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = libretro_state_lock(display, &state);
	if (status != 0)
		return status;
	st_console_bitmap_mouse_position(&state->frame, x, y, event);
	(void)st_mutex_unlock(&display->backend_mutex);
	return 0;
}

int
st_console_display_libretro_frame_size(st_console_display_t *display, uint32_t *width, uint32_t *height)
{
	struct libretro_state *state;
	int                    status;

	if (width == NULL || height == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_console_pull_frame(display);
	if (status != 0)
		return status;
	status = libretro_state_lock(display, &state);
	if (status != 0)
		return status;
	*width  = state->width;
	*height = state->height;
	(void)st_mutex_unlock(&display->backend_mutex);
	return 0;
}

int
st_console_display_libretro_frame_copy(st_console_display_t *display, uint32_t *destination,
    size_t destination_stride, size_t destination_rows)
{
	struct libretro_state *state;
	int                    status;

	if (destination == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_console_pull_frame(display);
	if (status != 0)
		return status;
	status = libretro_state_lock(display, &state);
	if (status != 0)
		return status;
	status = st_console_bitmap_presenter_frame(state->presenter, NULL, state->width, state->height,
	    milliseconds_now(), retain_pixels, state, &state->frame);
	if (destination_stride < state->width || destination_rows < state->height
	    || (state->height != 0 && destination_stride > SIZE_MAX / sizeof(*destination) / state->height))
		status = status == 0 ? IO_ERROR_INVALID_ARGUMENT : status;
	else if (status == 0) {
		for (uint32_t y = 0; y < state->height; ++y)
			memcpy(&destination[(size_t)y * destination_stride],
			    &state->pixels[(size_t)y * state->width], (size_t)state->width * sizeof(*destination));
	}
	(void)st_mutex_unlock(&display->backend_mutex);
	return status;
}
