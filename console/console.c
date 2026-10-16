/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_internal.h"

#include "console_config.h"
#include "io_error.h"
#include "st_time.h"
#include "st_unicode_text.h"
#include "st_utf8.h"
#include "st_unicode_width.h"
#include "string_compat.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int present_locked(st_console_t *console, bool forced,
	bool backend_locked);
static void free_console(st_console_t *console);
static void free_retired_consoles(st_console_display_t *display);

_Static_assert(sizeof(struct st_console_cell) == 24,
	"st_console_cell must remain compact");
_Static_assert(offsetof(struct st_console_cell, glyph) == 0
	&& offsetof(struct st_console_cell, foreground) == 4
	&& offsetof(struct st_console_cell, background) == 8
	&& offsetof(struct st_console_cell, flags) == 12
	&& offsetof(struct st_console_cell, font) == 16
	&& offsetof(struct st_console_cell, glyph_font) == 18
	&& offsetof(struct st_console_cell, hyperlink) == 20
	&& offsetof(struct st_console_cell, reserved) == 22
	&& sizeof(struct st_console_cell)
	    == offsetof(struct st_console_cell, reserved)
	    + sizeof(((struct st_console_cell *)0)->reserved),
	"st_console_cell layout must remain stable");

static bool
cell_count(uint32_t columns, uint32_t rows, size_t *result)
{
	if (columns == 0 || rows == 0)
		return false;
	if ((size_t)columns > SIZE_MAX / (size_t)rows)
		return false;
	*result = (size_t)columns * (size_t)rows;
	return *result <= SIZE_MAX / sizeof(struct st_console_cell);
}

static bool
pixel_dimensions(uint32_t columns, uint32_t rows, uint32_t cell_width,
	uint32_t cell_height, uint32_t configured_width,
	uint32_t configured_height, uint32_t *width, uint32_t *height,
	uint32_t *extra_width, uint32_t *extra_height)
{
	uint32_t text_width;
	uint32_t text_height;

	if (cell_width == 0 || cell_height == 0) {
		if (cell_width != 0 || cell_height != 0 || configured_width != 0
		    || configured_height != 0)
			return false;
		*width = 0;
		*height = 0;
		*extra_width = 0;
		*extra_height = 0;
		return true;
	}
	if (columns > UINT32_MAX / cell_width
	    || rows > UINT32_MAX / cell_height)
		return false;
	text_width = columns * cell_width;
	text_height = rows * cell_height;
	if ((configured_width == 0) != (configured_height == 0))
		return false;
	if (configured_width != 0) {
		if (configured_width < text_width || configured_height < text_height)
			return false;
		*width = configured_width;
		*height = configured_height;
	}
	else {
		*width = text_width;
		*height = text_height;
	}
	*extra_width = *width - text_width;
	*extra_height = *height - text_height;
	return true;
}

static uint32_t
default_palette_entry(uint32_t index)
{
	static const uint32_t system[16] = {
		0x000000, 0xa80000, 0x00a800, 0xa85400,
		0x0000a8, 0xa800a8, 0x00a8a8, 0xa8a8a8,
		0x545454, 0xff5454, 0x54ff54, 0xffff54,
		0x5454ff, 0xff54ff, 0x54ffff, 0xffffff,
	};
	static const uint8_t cube[6] = {0, 95, 135, 175, 215, 255};
	static const uint32_t legacy_tail[] = {
		0x000000, 0xffffff, 0x813338, 0x75cec8,
		0x8e3c97, 0x56ac4d, 0x2e2c9b, 0xedf171,
		0x8e5029, 0x553800, 0xc46c71, 0x4a4a4a,
		0x7b7b7b, 0xa9ff9f, 0x706deb, 0xb2b2b2,
		0x005181, 0x60b7e7,
		0xb6b6b6, 0x6d6d6d, 0xff6d6d, 0x6dff6d,
		0xffff6d, 0x6d6dff, 0xff6dff, 0x6dffff,
	};
	uint32_t value;

	if (index < 16)
		return system[index];
	if (index < 232) {
		value = index - 16;
		return ((uint32_t)cube[value / 36] << 16)
		    | ((uint32_t)cube[(value / 6) % 6] << 8)
		    | cube[value % 6];
	}
	if (index < 256) {
		value = 8 + (index - 232) * 10;
		return (value << 16) | (value << 8) | value;
	}
	if (index < 256 + sizeof(legacy_tail) / sizeof(legacy_tail[0]))
		return legacy_tail[index - 256];
	return 0;
}

int
st_console_get_default_palette(uint16_t first, uint32_t *rgb, uint32_t count)
{
	if (rgb == NULL || count > UINT16_MAX + UINT32_C(1) - first)
		return IO_ERROR_INVALID_ARGUMENT;
	for (uint32_t offset = 0; offset < count; ++offset)
		rgb[offset] = default_palette_entry((uint32_t)first + offset);
	return 0;
}

static void
initialize_palette(uint32_t palette[UINT16_MAX + 1U])
{
	(void)st_console_get_default_palette(0, palette,
	    UINT16_MAX + UINT32_C(1));
}

static void
initialize_cells(st_console_t *console, struct st_console_cell *cells,
	size_t count)
{
	struct st_console_cell blank = {
		.glyph = ' ',
		.foreground = 7,
		.background = 0,
		.flags = ST_CONSOLE_CELL_DIRTY,
		.font = 0,
		.glyph_font = 0,
		.hyperlink = 0,
	};
	size_t i;

	for (i = 0; i < count; ++i)
		cells[i] = blank;
	console->style.foreground = blank.foreground;
	console->style.background = blank.background;
	console->style.flags = 0;
	console->style.font = 0;
	console->style.hyperlink = 0;
}

static uint32_t
animation_interval(enum st_console_blink_mode mode)
{
	switch (mode) {
	case ST_CONSOLE_BLINK_PRESTEL:
		return 314;
	case ST_CONSOLE_BLINK_COMMODORE:
		return 333;
	case ST_CONSOLE_BLINK_ATARI_ST:
		return 500;
	case ST_CONSOLE_BLINK_ATARI:
		return 0;
	case ST_CONSOLE_BLINK_PC:
	default:
		return 133;
	}
}

static void
schedule_animation_locked(st_console_display_t *display,
	enum st_console_blink_mode mode, uint64_t now)
{
	uint32_t interval = animation_interval(mode);
	uint64_t elapsed;

	if (!display->presentation_animation_initialized
	    || display->presentation_blink_mode != mode) {
		display->presentation_animation_initialized = true;
		display->presentation_blink_mode = mode;
		display->presentation_animation_epoch = now;
	}
	if (interval == 0) {
		display->presentation_animation_deadline = UINT64_MAX;
		return;
	}
	elapsed = now - display->presentation_animation_epoch;
	display->presentation_animation_deadline
	    = display->presentation_animation_epoch
	    + (elapsed / interval + 1U) * interval;
}

void
st_console_display_reset_animation(st_console_display_t *display,
	enum st_console_blink_mode mode)
{
	uint64_t now;

	if (display->backend_ops == NULL
	    || !display->backend_ops->animated_present
	    || st_time_monotonic_ms(&now) != 0)
		return;
	(void)st_mutex_lock(&display->presentation_mutex);
	if (!display->presentation_animation_initialized
	    || display->presentation_blink_mode != mode) {
		display->presentation_animation_initialized = false;
		schedule_animation_locked(display, mode, now);
		(void)st_condition_signal(&display->presentation_ready);
	}
	(void)st_mutex_unlock(&display->presentation_mutex);
}

static int
presentation_worker(void *argument)
{
	st_console_display_t *display = argument;

	for (;;) {
		st_console_t *console;
		uint64_t now = 0;
		uint64_t next;
		bool animate;
		bool animation_due;
		bool content_due;
		bool repaint;
		int present_status = 0;
		int status;

		(void)st_mutex_lock(&display->presentation_mutex);
		for (;;) {
			struct timespec deadline;
			int64_t delay;

			if (display->presentation_stop)
				break;
			if (st_time_monotonic_ms(&now) != 0) {
				content_due = display->presentation_requested;
				animation_due = false;
				if (content_due)
					break;
				if (st_time_utc_deadline_after_ms(&deadline, 10) == 0)
					(void)st_condition_timedwait(
					    &display->presentation_ready,
					    &display->presentation_mutex, &deadline);
				else
					(void)st_condition_wait(
					    &display->presentation_ready,
					    &display->presentation_mutex);
				continue;
			}
			animate = display->backend_ops->animated_present
			    && display->presentation_animation_initialized;
			animation_due = animate
			    && now >= display->presentation_animation_deadline;
			content_due = display->presentation_requested
			    && now >= display->presentation_content_deadline;
			if (animation_due || content_due)
				break;
			next = UINT64_MAX;
			if (display->presentation_requested)
				next = display->presentation_content_deadline;
			if (animate && display->presentation_animation_deadline < next)
				next = display->presentation_animation_deadline;
			if (next == UINT64_MAX) {
				(void)st_condition_wait(&display->presentation_ready,
				    &display->presentation_mutex);
				continue;
			}
			delay = next > now ? (int64_t)(next - now) : 0;
			if (st_time_utc_deadline_after_ms(&deadline, delay) != 0)
				continue;
			status = st_condition_timedwait(&display->presentation_ready,
			    &display->presentation_mutex, &deadline);
			(void)status;
		}
		if (display->presentation_stop) {
			(void)st_mutex_unlock(&display->presentation_mutex);
			break;
		}
		if (content_due)
			display->presentation_requested = false;
		repaint = content_due && display->repaint_requested;
		if (content_due)
			display->repaint_requested = false;
		console = atomic_load_explicit(&display->active,
		    memory_order_acquire);
		display->presentation_active = console;
		(void)st_mutex_unlock(&display->presentation_mutex);
		if (console != NULL && st_rwlock_write_lock(&console->state_lock)
		    == ST_THREAD_SUCCESS) {
			if (st_time_monotonic_ms(&now) == 0) {
				(void)st_mutex_lock(&display->presentation_mutex);
				schedule_animation_locked(display, console->blink_mode, now);
				(void)st_mutex_unlock(&display->presentation_mutex);
			}
			if (repaint) {
				st_console_mark_all_locked(console);
				console->pixel_surface_dirty = true;
			}
			present_status = st_console_present_locked(console,
			    repaint || animation_due);
			(void)st_rwlock_unlock(&console->state_lock);
		}
		(void)st_mutex_lock(&display->presentation_mutex);
		display->presentation_active = NULL;
		if (present_status != 0 && !display->presentation_requested) {
			if (st_time_monotonic_ms(&now) != 0)
				now = 0;
			display->presentation_requested = true;
			display->presentation_content_deadline = now + 10;
		}
		(void)st_condition_broadcast(&display->presentation_ready);
		(void)st_mutex_unlock(&display->presentation_mutex);
	}
	return 0;
}

int
st_console_start_presentation_worker(st_console_t *console)
{
	st_console_display_t *display = console->display;
	int status;

	if (display->backend_ops == NULL
	    || !display->backend_ops->deferred_present)
		return 0;
	(void)st_mutex_lock(&display->presentation_mutex);
	display->presentation_stop = false;
	display->presentation_requested = true;
	display->presentation_content_deadline = 0;
	display->presentation_animation_initialized = false;
	(void)st_mutex_unlock(&display->presentation_mutex);
	status = st_thread_create(&display->presentation_thread,
	    presentation_worker, display);
	if (status != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	display->presentation_thread_started = true;
	return 0;
}

static void
stop_presentation_worker(st_console_display_t *display)
{
	if (!display->presentation_thread_started)
		return;
	(void)st_mutex_lock(&display->presentation_mutex);
	display->presentation_stop = true;
	(void)st_condition_signal(&display->presentation_ready);
	(void)st_mutex_unlock(&display->presentation_mutex);
	(void)st_thread_join(display->presentation_thread, NULL);
	display->presentation_thread_started = false;
}

void
st_console_display_request_repaint(st_console_display_t *display)
{
	(void)st_mutex_lock(&display->presentation_mutex);
	display->repaint_requested = true;
	if (!display->presentation_requested) {
		uint64_t now = 0;

		(void)st_time_monotonic_ms(&now);
		display->presentation_content_deadline = now + 10;
	}
	display->presentation_requested = true;
	(void)st_condition_signal(&display->presentation_ready);
	(void)st_mutex_unlock(&display->presentation_mutex);
}

static int
request_presentation_locked(st_console_t *console)
{
	st_console_display_t *display = console->display;
	bool wake;

	if (!console->changed || console->update_depth != 0)
		return 0;
	if (atomic_load_explicit(&display->active, memory_order_acquire)
	    != console)
		return 0;
	if (!display->presentation_thread_started)
		return st_console_present_locked(console, false);
	(void)st_mutex_lock(&display->presentation_mutex);
	wake = !display->presentation_requested;
	if (wake) {
		uint64_t now = 0;

		(void)st_time_monotonic_ms(&now);
		display->presentation_content_deadline = now + 10;
	}
	display->presentation_requested = true;
	if (wake)
		(void)st_condition_signal(&display->presentation_ready);
	(void)st_mutex_unlock(&display->presentation_mutex);
	return 0;
}

static int
create_console(st_console_t **result, st_console_display_t *display,
	const struct st_console_config *config)
{
	st_console_t *console;
	size_t count;
	uint32_t pixel_width;
	uint32_t pixel_height;
	uint32_t pixel_extra_width;
	uint32_t pixel_extra_height;
	uint32_t minimum_width;
	uint32_t minimum_height;
	bool configured_default_cursor;
	int status;

	bool initialize_backend;

	if (result == NULL || display == NULL || config == NULL
	    || !cell_count(config->columns, config->rows, &count)
	    || !pixel_dimensions(config->columns, config->rows,
	    config->cell_width, config->cell_height, config->pixel_width,
	    config->pixel_height, &pixel_width, &pixel_height,
	    &pixel_extra_width, &pixel_extra_height)
	    || config->blink_mode < ST_CONSOLE_BLINK_PC
	    || config->blink_mode > ST_CONSOLE_BLINK_ATARI_ST
	    || (config->video_flags & ~ST_CONSOLE_VIDEO_FLAGS) != 0
	    || config->scaling_mode < ST_CONSOLE_SCALING_INTERNAL
	    || config->scaling_mode > ST_CONSOLE_SCALING_EXTERNAL
	    || (config->scaling_policy != ST_PIXEL_SCALING_BLOCKY
	    && config->scaling_policy != ST_PIXEL_SCALING_POINTY)
	    || ((config->aspect_width == 0) != (config->aspect_height == 0)))
		return IO_ERROR_INVALID_ARGUMENT;
	if (pixel_width != 0 && st_console_aspect_correct_size(pixel_width,
	    pixel_height, config->aspect_width, config->aspect_height,
	    &minimum_width, &minimum_height) != 0)
		return IO_ERROR_OUT_OF_RANGE;
	console = calloc(1, sizeof(*console));
	if (console == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	console->cells = malloc(count * sizeof(*console->cells));
	if (console->cells == NULL) {
		free(console);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	status = st_rwlock_init(&console->state_lock);
	if (status != ST_THREAD_SUCCESS) {
		free(console->cells);
		free(console);
		return IO_ERROR_IO;
	}
	status = st_mutex_init(&console->event_mutex, ST_MUTEX_PLAIN);
	if (status != ST_THREAD_SUCCESS) {
		(void)st_rwlock_destroy(&console->state_lock);
		free(console->cells);
		free(console);
		return IO_ERROR_IO;
	}
	status = st_condition_init(&console->event_ready);
	if (status != ST_THREAD_SUCCESS) {
		st_mutex_destroy(&console->event_mutex);
		(void)st_rwlock_destroy(&console->state_lock);
		free(console->cells);
		free(console);
		return IO_ERROR_IO;
	}
	console->columns = config->columns;
	console->display = display;
	console->pointer_visible = true;
	console->pointer_shape = ST_CONSOLE_POINTER_DEFAULT;
	console->rows = config->rows;
	console->cell_width = config->cell_width;
	console->cell_height = config->cell_height;
	console->pixel_extra_width = pixel_extra_width;
	console->pixel_extra_height = pixel_extra_height;
	console->window = (struct st_console_rect){0, 0, config->columns,
	    config->rows};
	console->wrap = config->wrap;
	configured_default_cursor = config->default_cursor.range != 0;
	if (configured_default_cursor)
		console->default_cursor = config->default_cursor;
	else {
		uint32_t height = config->cell_height == 0 ? 1
		    : config->cell_height;
		uint32_t thickness = height > 8 ? 2 : 1;

		console->default_cursor = (struct st_console_custom_cursor){
			.start = height - thickness,
			.end = height - 1,
			.range = height,
			.visible = true,
			.blink = true,
		};
	}
	console->custom_cursor = console->default_cursor;
	if (configured_default_cursor) {
		console->cursor_visible = console->default_cursor.visible;
		console->cursor_shape = console->default_cursor.start == 0
		    && console->default_cursor.end
		    >= console->default_cursor.range - 1
		    ? ST_CONSOLE_CURSOR_BLOCK : ST_CONSOLE_CURSOR_UNDERLINE;
		console->cursor_custom = true;
	}
	else {
		console->cursor_visible = true;
		console->cursor_shape = ST_CONSOLE_CURSOR_BLOCK;
		console->cursor_custom = false;
	}
	console->blink_mode = config->blink_mode;
	console->video_flags = config->video_flags;
	console->scaling_mode = config->scaling_mode;
	console->blocky_scaling
	    = config->scaling_policy == ST_PIXEL_SCALING_BLOCKY;
	console->aspect_width = config->aspect_width;
	console->aspect_height = config->aspect_height;
	if (pixel_width != 0 && !display->backend_initialized) {
		if (display->config.initial_window_width != 0) {
			display->initial_window_width = display->config.initial_window_width
			    < minimum_width ? minimum_width
			    : display->config.initial_window_width;
			display->initial_window_height = display->config.initial_window_height
			    < minimum_height ? minimum_height
			    : display->config.initial_window_height;
		}
		else {
			status = st_console_scaled_window_size(pixel_width, pixel_height,
			    config->aspect_width, config->aspect_height,
			    display->config.initial_scaling, &display->initial_window_width,
			    &display->initial_window_height);
			if (status != 0) {
				st_condition_destroy(&console->event_ready);
				st_mutex_destroy(&console->event_mutex);
				(void)st_rwlock_destroy(&console->state_lock);
				free(console->cells);
				free(console);
				return status;
			}
		}
	}
	console->changed = true;
	initialize_palette(console->palette);
	initialize_cells(console, console->cells, count);
	if (pixel_width != 0) {
		console->drawn_cells = calloc(count, sizeof(console->drawn_cells[0]));
		console->raster_rows = calloc(config->rows,
		    sizeof(console->raster_rows[0]));
		if (console->drawn_cells == NULL || console->raster_rows == NULL)
			status = IO_ERROR_OUT_OF_MEMORY;
		else
			status = st_pixel_buffer_create(&console->pixels, pixel_width,
			    pixel_height, true);
		if (status != 0) {
			free(console->raster_rows);
			free(console->drawn_cells);
			st_condition_destroy(&console->event_ready);
			st_mutex_destroy(&console->event_mutex);
			(void)st_rwlock_destroy(&console->state_lock);
			free(console->cells);
			free(console);
			return status;
		}
	}

	initialize_backend = !display->backend_initialized;
	if (!initialize_backend) {
		status = 0;
		if (display->backend_ops != NULL) {
			if (display->backend_ops->presents_pixels && console->pixels == NULL)
				status = IO_ERROR_NOT_SUPPORTED;
			else if (display->backend_ops->attach != NULL)
				status = display->backend_ops->attach(console);
		}
		if (status != 0) {
			st_condition_destroy(&console->event_ready);
			st_mutex_destroy(&console->event_mutex);
			(void)st_rwlock_destroy(&console->state_lock);
			st_pixel_buffer_destroy(console->pixels);
			free(console->raster_rows);
			free(console->drawn_cells);
			free(console->cells);
			free(console);
			return status;
		}
		console->next_display = display->consoles;
		display->consoles = console;
		++display->console_count;
		*result = console;
		return 0;
	}
	display->backend_failed = false;
	atomic_store_explicit(&display->active, console, memory_order_release);
	if (display->config.backend == ST_CONSOLE_BACKEND_AUTO) {
		status = IO_ERROR_NOT_SUPPORTED;
#if ST_CONSOLE_HAVE_GDI
		if (console->pixels != NULL) {
			console->display->backend = ST_CONSOLE_BACKEND_GDI;
			status = st_console_gdi_init(console);
		}
#endif
#if ST_CONSOLE_HAVE_WIN32
		if (console->pixels == NULL) {
			console->display->backend = ST_CONSOLE_BACKEND_WIN32;
			status = st_console_win32_init(console);
		}
#endif
#if ST_CONSOLE_HAVE_QUARTZ
		if (status != 0 && console->pixels != NULL) {
			console->display->backend = ST_CONSOLE_BACKEND_QUARTZ;
			status = st_console_quartz_init(console);
		}
#endif
#if ST_CONSOLE_HAVE_WAYLAND
		if (status != 0 && console->pixels != NULL) {
			console->display->backend = ST_CONSOLE_BACKEND_WAYLAND;
			status = st_console_wayland_init(console);
		}
#endif
		/* SDL is the portability fallback after native window systems. */
#if ST_CONSOLE_HAVE_X11
		if (status != 0 && console->pixels != NULL) {
			console->display->backend = ST_CONSOLE_BACKEND_X11;
			status = st_console_x11_init(console);
		}
#endif
#if ST_CONSOLE_HAVE_SDL
		if (status != 0 && console->pixels != NULL) {
			console->display->backend = ST_CONSOLE_BACKEND_SDL;
			status = st_console_sdl_init(console);
		}
#endif
		if (status != 0) {
			console->display->backend = ST_CONSOLE_BACKEND_NCURSES;
			status = st_console_ncurses_init(console);
		}
		if (status != 0) {
			console->display->backend = ST_CONSOLE_BACKEND_ANSI;
			status = st_console_ansi_init(console,
			    display->config.ansi_io);
		}
	}
	else if (display->config.backend == ST_CONSOLE_BACKEND_ANSI)
		status = st_console_ansi_init(console, display->config.ansi_io);
	else if (display->config.backend == ST_CONSOLE_BACKEND_NCURSES)
		status = st_console_ncurses_init(console);
	else if (display->config.backend == ST_CONSOLE_BACKEND_WIN32) {
#if ST_CONSOLE_HAVE_WIN32
		status = st_console_win32_init(console);
#else
		status = IO_ERROR_NOT_SUPPORTED;
#endif
	}
	else if (display->config.backend == ST_CONSOLE_BACKEND_SDL) {
#if ST_CONSOLE_HAVE_SDL
		status = st_console_sdl_init(console);
#else
		status = IO_ERROR_NOT_SUPPORTED;
#endif
	}
	else if (display->config.backend == ST_CONSOLE_BACKEND_X11) {
#if ST_CONSOLE_HAVE_X11
		status = st_console_x11_init(console);
#else
		status = IO_ERROR_NOT_SUPPORTED;
#endif
	}
	else if (display->config.backend == ST_CONSOLE_BACKEND_WAYLAND) {
#if ST_CONSOLE_HAVE_WAYLAND
		status = st_console_wayland_init(console);
#else
		status = IO_ERROR_NOT_SUPPORTED;
#endif
	}
	else if (display->config.backend == ST_CONSOLE_BACKEND_GDI) {
#if ST_CONSOLE_HAVE_GDI
		status = st_console_gdi_init(console);
#else
		status = IO_ERROR_NOT_SUPPORTED;
#endif
	}
	else if (display->config.backend == ST_CONSOLE_BACKEND_QUARTZ) {
#if ST_CONSOLE_HAVE_QUARTZ
		status = st_console_quartz_init(console);
#else
		status = IO_ERROR_NOT_SUPPORTED;
#endif
	}
	else if (display->config.backend == ST_CONSOLE_BACKEND_LIBRETRO)
		status = st_console_libretro_init(console);
	else if (display->config.backend == ST_CONSOLE_BACKEND_HEADLESS)
		status = 0;
	else
		status = IO_ERROR_NOT_SUPPORTED;
	if (status == 0)
		status = st_console_start_presentation_worker(console);
	if (status != 0) {
		if (console->display->backend_ops != NULL
		    && console->display->backend_ops->destroy != NULL)
			console->display->backend_ops->destroy(console->display);
		display->backend_ops = NULL;
		display->backend_state = NULL;
		st_condition_destroy(&console->event_ready);
		st_mutex_destroy(&console->event_mutex);
		(void)st_rwlock_destroy(&console->state_lock);
		st_pixel_buffer_destroy(console->pixels);
		free(console->raster_rows);
		free(console->drawn_cells);
		free(console->cells);
		free(console);
		atomic_store_explicit(&display->active, NULL,
		    memory_order_release);
		return status;
	}
	display->backend_initialized = true;
	atomic_store_explicit(&display->input_active, console, memory_order_release);
	console->next_display = display->consoles;
	display->consoles = console;
	++display->console_count;
	*result = console;
	return 0;
}

int
st_console_display_create(st_console_display_t **result,
	const struct st_console_display_config *config)
{
	st_console_display_t *display;
	const struct st_console_icon *icon;
	size_t minimum_stride;

	if (result == NULL || config == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	icon = config->icon;
	if (icon != NULL && (icon->pixels == NULL || icon->width == 0
	    || icon->height == 0))
		return IO_ERROR_INVALID_ARGUMENT;
	minimum_stride = icon == NULL ? 0 : icon->width;
	if (minimum_stride > SIZE_MAX / 4)
		return IO_ERROR_INVALID_ARGUMENT;
	minimum_stride *= 4;
	if ((icon != NULL && (icon->stride < minimum_stride
	    || icon->height > SIZE_MAX / icon->stride))
	    || config->backend < ST_CONSOLE_BACKEND_AUTO
	    || config->backend > ST_CONSOLE_BACKEND_HEADLESS
	    || config->text_presentation < ST_CONSOLE_TEXT_UNICODE
	    || config->text_presentation > ST_CONSOLE_TEXT_ASCII
	    || ((config->initial_window_width == 0)
	    != (config->initial_window_height == 0))
	    || config->initial_scaling != config->initial_scaling)
		return IO_ERROR_INVALID_ARGUMENT;
	display = calloc(1, sizeof(*display));
	if (display == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (st_mutex_init(&display->backend_mutex, ST_MUTEX_PLAIN)
	    != ST_THREAD_SUCCESS) {
		free(display);
		return IO_ERROR_IO;
	}
	if (st_mutex_init(&display->input_mutex, ST_MUTEX_PLAIN)
	    != ST_THREAD_SUCCESS) {
		st_mutex_destroy(&display->backend_mutex);
		free(display);
		return IO_ERROR_IO;
	}
	if (st_mutex_init(&display->presentation_mutex, ST_MUTEX_PLAIN)
	    != ST_THREAD_SUCCESS) {
		st_mutex_destroy(&display->input_mutex);
		st_mutex_destroy(&display->backend_mutex);
		free(display);
		return IO_ERROR_IO;
	}
	if (st_condition_init(&display->presentation_ready) != ST_THREAD_SUCCESS) {
		st_mutex_destroy(&display->presentation_mutex);
		st_mutex_destroy(&display->input_mutex);
		st_mutex_destroy(&display->backend_mutex);
		free(display);
		return IO_ERROR_IO;
	}
	if (st_mutex_init(&display->lifecycle_mutex, ST_MUTEX_PLAIN)
	    != ST_THREAD_SUCCESS) {
		st_condition_destroy(&display->presentation_ready);
		st_mutex_destroy(&display->presentation_mutex);
		st_mutex_destroy(&display->input_mutex);
		st_mutex_destroy(&display->backend_mutex);
		free(display);
		return IO_ERROR_IO;
	}
	display->config = *config;
	if (config->ansi_io != NULL) {
		display->ansi_io = *config->ansi_io;
		display->config.ansi_io = &display->ansi_io;
	}
	display->backend = config->backend;
	*result = display;
	return 0;
}

void
st_console_display_destroy(st_console_display_t *display)
{
	if (display == NULL)
		return;
	if (display->console_count != 0) {
		fprintf(stderr, "st_console_display_destroy: consoles still attached\n");
		return;
	}
	st_mutex_destroy(&display->input_mutex);
	st_mutex_destroy(&display->backend_mutex);
	st_condition_destroy(&display->presentation_ready);
	st_mutex_destroy(&display->presentation_mutex);
	st_mutex_destroy(&display->lifecycle_mutex);
	free(display);
}

int
st_console_create_on_display(st_console_t **result,
	st_console_display_t *display, const struct st_console_config *config)
{
	int status;

	if (display == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&display->lifecycle_mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	status = create_console(result, display, config);
	(void)st_mutex_unlock(&display->lifecycle_mutex);
	return status;
}

int
st_console_create(st_console_t **result,
	const struct st_console_display_config *display_config,
	const struct st_console_config *config)
{
	st_console_display_t *display;
	st_console_t *console;
	int status;

	if (result == NULL || config == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_console_display_create(&display, display_config);
	if (status != 0)
		return status;
	status = create_console(&console, display, config);
	if (status != 0) {
		st_console_display_destroy(display);
		return status;
	}
	console->owns_display = true;
	*result = console;
	return 0;
}

int
st_console_get_backend(st_console_t *console,
	enum st_console_backend *backend)
{
	return console == NULL ? IO_ERROR_INVALID_ARGUMENT
	    : st_console_display_get_backend(console->display, backend);
}

int
st_console_display_get_backend(st_console_display_t *display,
	enum st_console_backend *backend)
{
	if (display == NULL || backend == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&display->lifecycle_mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	*backend = display->backend;
	(void)st_mutex_unlock(&display->lifecycle_mutex);
	return 0;
}

/* Both console state locks and backend_mutex are held by the caller. */
static int
apply_console(st_console_display_t *display, st_console_t *console, bool setup)
{
	int status = 0;

	if (setup && display->backend_ops != NULL
	    && display->backend_ops->activate != NULL)
		status = display->backend_ops->activate(console);
	if (status != 0)
		return status;
	atomic_store_explicit(&display->active, console, memory_order_release);
	if (setup)
		st_console_display_reset_animation(display, console->blink_mode);
	if (setup && display->backend_ops != NULL
	    && display->backend_ops->resize != NULL)
		status = display->backend_ops->resize(console, console->columns,
		    console->rows);
	if (status == 0 && setup && display->backend_ops != NULL
	    && display->backend_ops->palette_changed != NULL)
		display->backend_ops->palette_changed(console, 0, console->palette,
		    UINT16_MAX + 1U);
	if (status == 0 && setup && display->backend_ops != NULL
	    && display->backend_ops->set_title != NULL)
		status = display->backend_ops->set_title(display,
		    console->title == NULL ? "SyncTERM" : console->title);
	if (status == 0 && setup && display->backend_ops != NULL
	    && display->backend_ops->set_pointer_visible != NULL)
		status = display->backend_ops->set_pointer_visible(display,
		    console->pointer_visible);
	if (status == 0 && setup && display->backend_ops != NULL
	    && display->backend_ops->set_pointer_shape != NULL) {
		status = display->backend_ops->set_pointer_shape(display,
		    console->pointer_shape);
		if (status == IO_ERROR_NOT_SUPPORTED)
			status = 0;
	}
	if (status == 0) {
		st_console_mark_all_locked(console);
		status = present_locked(console, true, true);
	}
	return status;
}

/* lifecycle_mutex prevents the implicit previous console from being freed. */
static int
activate_console(st_console_display_t *display, st_console_t *console)
{
	st_console_t *previous;
	int status;

	previous = atomic_load_explicit(&display->active, memory_order_acquire);
	if (st_rwlock_write_lock(&console->state_lock) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (previous != NULL && previous != console
	    && st_rwlock_write_lock(&previous->state_lock) != ST_THREAD_SUCCESS) {
		(void)st_rwlock_unlock(&console->state_lock);
		return IO_ERROR_IO;
	}
	if (st_mutex_lock(&display->backend_mutex) != ST_THREAD_SUCCESS) {
		if (previous != NULL && previous != console)
			(void)st_rwlock_unlock(&previous->state_lock);
		(void)st_rwlock_unlock(&console->state_lock);
		return IO_ERROR_IO;
	}
	bool setup = previous != console || display->backend_failed;

	display->backend_failed = false;
	status = apply_console(display, console, setup);
	if (status != 0) {
		int restored = previous == NULL ? IO_ERROR_IO
		    : apply_console(display, previous, true);

		display->backend_failed = restored != 0;
		atomic_store_explicit(&display->active, previous, memory_order_release);
	}
	else
		atomic_store_explicit(&display->input_active, console, memory_order_release);
	(void)st_mutex_unlock(&display->backend_mutex);
	if (previous != NULL && previous != console)
		(void)st_rwlock_unlock(&previous->state_lock);
	(void)st_rwlock_unlock(&console->state_lock);
	if (status == 0)
		free_retired_consoles(display);
	return status;
}

int
st_console_display_activate(st_console_display_t *display,
	st_console_t *console)
{
	int status;

	if (display == NULL || console == NULL || console->display != display)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&display->lifecycle_mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	status = display->backend_initialized ? activate_console(display, console)
	    : IO_ERROR_INVALID_ARGUMENT;
	(void)st_mutex_unlock(&display->lifecycle_mutex);
	return status;
}

const char *
st_console_backend_name(enum st_console_backend backend)
{
	static const char *const names[] = {
		[ST_CONSOLE_BACKEND_AUTO] = "Autodetect",
		[ST_CONSOLE_BACKEND_ANSI] = "ANSI",
		[ST_CONSOLE_BACKEND_NCURSES] = "Curses",
		[ST_CONSOLE_BACKEND_WIN32] = "Win32 Console",
		[ST_CONSOLE_BACKEND_GDI] = "GDI",
		[ST_CONSOLE_BACKEND_SDL] = "SDL",
		[ST_CONSOLE_BACKEND_X11] = "X11",
		[ST_CONSOLE_BACKEND_WAYLAND] = "Wayland",
		[ST_CONSOLE_BACKEND_QUARTZ] = "Quartz",
		[ST_CONSOLE_BACKEND_LIBRETRO] = "Retro",
		[ST_CONSOLE_BACKEND_HEADLESS] = "Headless",
	};

	if (backend < ST_CONSOLE_BACKEND_AUTO
	    || (size_t)backend >= sizeof(names) / sizeof(names[0]))
		return "Unknown";
	return names[backend];
}

uint32_t
st_console_build_features(void)
{
	uint32_t result = ST_CONSOLE_BUILD_ANSI | ST_CONSOLE_BUILD_LIBRETRO;

#if ST_CONSOLE_HAVE_CURSES_H
	result |= ST_CONSOLE_BUILD_NCURSES;
#endif
#if ST_CONSOLE_HAVE_WIN32
	result |= ST_CONSOLE_BUILD_WIN32;
#endif
#if ST_CONSOLE_HAVE_GDI
	result |= ST_CONSOLE_BUILD_GDI;
#endif
#if ST_CONSOLE_HAVE_SDL
	result |= ST_CONSOLE_BUILD_SDL;
#endif
#if ST_CONSOLE_HAVE_X11
	result |= ST_CONSOLE_BUILD_X11;
#endif
#if ST_CONSOLE_HAVE_XRENDER
	result |= ST_CONSOLE_BUILD_XRENDER;
#endif
#if ST_CONSOLE_HAVE_XRANDR
	result |= ST_CONSOLE_BUILD_XRANDR;
#endif
#if ST_CONSOLE_HAVE_XINERAMA
	result |= ST_CONSOLE_BUILD_XINERAMA;
#endif
#if ST_CONSOLE_HAVE_WAYLAND
	result |= ST_CONSOLE_BUILD_WAYLAND;
#endif
#if ST_CONSOLE_HAVE_QUARTZ
	result |= ST_CONSOLE_BUILD_QUARTZ;
#endif
	return result;
}

void
st_console_destroy(st_console_t *console)
{
	st_console_display_t *display;
	st_console_t *replacement = NULL;
	st_console_t **link;
	bool destroy_display;

	if (console == NULL)
		return;
	display = console->display;
	destroy_display = console->owns_display;
	(void)st_mutex_lock(&display->lifecycle_mutex);
	if (atomic_load_explicit(&display->active, memory_order_acquire)
	    == console) {
		replacement = display->consoles;

		while (replacement == console)
			replacement = replacement->next_display;
		if (replacement != NULL) {
			if (activate_console(display, replacement) != 0) {
				(void)st_mutex_lock(&display->backend_mutex);
				if (display->backend_ops != NULL
				    && display->backend_ops->activate != NULL)
					display->backend_ops->activate(replacement);
				atomic_store_explicit(&display->active, replacement,
				    memory_order_release);
				atomic_store_explicit(&display->input_active, replacement,
				    memory_order_release);
				display->backend_failed = true;
				(void)st_mutex_unlock(&display->backend_mutex);
			}
		}
		else {
			atomic_store_explicit(&display->active, NULL,
			    memory_order_release);
			atomic_store_explicit(&display->input_active, NULL,
			    memory_order_release);
		}
	}
	/* Drain deliveries which selected this console before the switch. */
	(void)st_mutex_lock(&display->input_mutex);
	(void)st_mutex_unlock(&display->input_mutex);
	if (display->presentation_thread_started) {
		(void)st_mutex_lock(&display->presentation_mutex);
		while (display->presentation_active == console)
			(void)st_condition_wait(&display->presentation_ready,
			    &display->presentation_mutex);
		(void)st_mutex_unlock(&display->presentation_mutex);
	}
	if (display->console_count == 1) {
		const struct st_console_backend_ops *ops;

		stop_presentation_worker(display);
		(void)st_mutex_lock(&display->backend_mutex);
		ops = display->backend_ops;
		display->backend_ops = NULL;
		(void)st_mutex_unlock(&display->backend_mutex);
		/* Input threads may still need backend_mutex while they shut down. */
		if (ops != NULL && ops->destroy != NULL)
			ops->destroy(display);
		display->backend_state = NULL;
		display->backend_initialized = false;
		free_retired_consoles(display);
	}
	link = &display->consoles;
	while (*link != NULL && *link != console)
		link = &(*link)->next_display;
	if (*link == console)
		*link = console->next_display;
	--display->console_count;
	if (display->backend_initialized && display->backend_failed) {
		console->next_display = display->retired_consoles;
		display->retired_consoles = console;
	}
	else
		free_console(console);
	(void)st_mutex_unlock(&display->lifecycle_mutex);
	if (destroy_display)
		st_console_display_destroy(display);
}

/* No backend, input, or presentation worker can still reference this object. */
static void
free_console(st_console_t *console)
{
	st_condition_destroy(&console->event_ready);
	st_mutex_destroy(&console->event_mutex);
	(void)st_rwlock_destroy(&console->state_lock);
	free(console->events);
	for (size_t i = 0; i < array_list_count(console->fonts); ++i) {
		struct st_console_font_registration *registration = console->fonts[i];

		if (registration->owned) {
			free((void *)registration->font.bitmap);
			free((void *)registration->font.wide_bitmap);
			free((void *)registration->font.mapping);
		}
		free(registration);
	}
	array_list_free(&console->fonts);
	st_pixel_buffer_destroy(console->pixels);
	free(console->raster_rows);
	free(console->drawn_cells);
	free(console->cells);
	free(console->title);
	free(console);
}

/* lifecycle_mutex is held; a completed activation or shutdown detached them. */
static void
free_retired_consoles(st_console_display_t *display)
{
	while (display->retired_consoles != NULL) {
		st_console_t *console = display->retired_consoles;

		display->retired_consoles = console->next_display;
		free_console(console);
	}
}

st_console_t *
st_console_display_active_console(st_console_t *console)
{
	return console == NULL || console->display == NULL ? NULL
	    : atomic_load_explicit(&console->display->active,
	    memory_order_acquire);
}

int
st_console_display_active(st_console_display_t *display,
	st_console_t **console)
{
	if (display == NULL || console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*console = atomic_load_explicit(&display->input_active, memory_order_acquire);
	return 0;
}

bool
st_console_rect_valid(const st_console_t *console,
	const struct st_console_rect *rect)
{
	return rect != NULL && rect->width != 0 && rect->height != 0
	    && rect->x < console->columns && rect->y < console->rows
	    && rect->width <= console->columns - rect->x
	    && rect->height <= console->rows - rect->y;
}

static bool
cells_equal(const struct st_console_cell *left,
	const struct st_console_cell *right)
{
	return left->glyph == right->glyph
	    && left->foreground == right->foreground
	    && left->background == right->background
	    && ((left->flags ^ right->flags) & ST_CONSOLE_CELL_PUBLIC_FLAGS) == 0
	    && left->font == right->font
	    && left->glyph_font == right->glyph_font
	    && left->hyperlink == right->hyperlink;
}

static void
set_cell_raw_locked(st_console_t *console, size_t index,
	const struct st_console_cell *source)
{
	struct st_console_cell value = *source;

	value.flags &= ST_CONSOLE_CELL_PUBLIC_FLAGS;
	if (cells_equal(&console->cells[index], &value))
		return;
	value.flags |= ST_CONSOLE_CELL_DIRTY;
	console->cells[index] = value;
	console->changed = true;
	console->raster_current = false;
}

static bool
cell_is_wide_locked(st_console_t *console, const struct st_console_cell *cell)
{
	const uint8_t *bitmap;
	uint32_t width;
	size_t row_stride;
	uint16_t glyph_font = st_console_cell_glyph_font(cell);
	const struct st_console_font *font = st_console_font_locked(console,
	    glyph_font);

	return font != NULL && st_console_font_glyph_locked(console,
	    glyph_font, cell->glyph, &bitmap, &width, &row_stride)
	    && width == font->width * 2;
}

static struct st_console_cell
cell_for_storage(const struct st_console_cell *source)
{
	struct st_console_cell value = *source;

	value.flags &= ST_CONSOLE_CELL_PUBLIC_FLAGS;
	value.reserved = 0;
	return value;
}

static bool
pair_matches(const struct st_console_cell *leading,
	const struct st_console_cell *continuation)
{
	return leading->glyph == continuation->glyph
	    && leading->font == continuation->font
	    && leading->glyph_font == continuation->glyph_font
	    && (leading->flags & ST_CONSOLE_CELL_CONTINUATION) == 0
	    && (continuation->flags & ST_CONSOLE_CELL_CONTINUATION) != 0;
}

static bool complete_cell_valid_locked(st_console_t *console,
	const struct st_console_cell *cell);

void
st_console_cursor_span_locked(const st_console_t *console,
	uint32_t *x, uint32_t *columns)
{
	size_t index;

	*x = console->cursor_x;
	*columns = 1;
	if (console->cursor_x >= console->columns
	    || console->cursor_y >= console->rows)
		return;
	index = (size_t)console->cursor_y * console->columns
	    + console->cursor_x;
	if (console->cursor_x > 0
	    && pair_matches(&console->cells[index - 1], &console->cells[index])) {
		--*x;
		*columns = 2;
	}
	else if (console->cursor_x + 1 < console->columns
	    && pair_matches(&console->cells[index], &console->cells[index + 1]))
		*columns = 2;
}

static bool
row_cells_valid_locked(st_console_t *console,
	const struct st_console_cell *cells, uint32_t width)
{
	struct st_console_cell previous = {0};
	bool have_previous = false;

	for (uint32_t x = 0; x < width; ++x) {
		struct st_console_cell value;

		value = cell_for_storage(&cells[x]);
		if (!complete_cell_valid_locked(console, &value))
			return false;

		if (have_previous
		    && (value.flags & ST_CONSOLE_CELL_CONTINUATION) == 0)
			return false;
		if ((value.flags & ST_CONSOLE_CELL_CONTINUATION) != 0) {
			if (!have_previous || !pair_matches(&previous, &value)
			    || !cell_is_wide_locked(console, &previous))
				return false;
			have_previous = false;
			continue;
		}
		if (cell_is_wide_locked(console, &value)) {
			if (x + 1 >= width)
				return false;
			previous = value;
			have_previous = true;
		}
		else
			have_previous = false;
	}
	return !have_previous;
}

static bool
complete_cell_valid_locked(st_console_t *console,
	const struct st_console_cell *cell)
{
	const uint8_t *bitmap;
	uint32_t width;
	size_t row_stride;

	return console->pixels == NULL
	    || st_console_font_glyph_locked(console,
	    st_console_cell_glyph_font(cell), cell->glyph,
	    &bitmap, &width, &row_stride);
}

static bool
complete_row_valid_locked(st_console_t *console,
	const struct st_console_cell *cells, uint32_t width)
{
	bool expect_continuation = false;
	struct st_console_cell previous = {0};

	for (uint32_t x = 0; x < width; ++x) {
		struct st_console_cell value = cells[x];

		value.flags &= ST_CONSOLE_CELL_PUBLIC_FLAGS;
		if (!complete_cell_valid_locked(console, &value))
			return false;
		if ((value.flags & ST_CONSOLE_CELL_CONTINUATION) != 0) {
			if (!expect_continuation || !pair_matches(&previous, &value))
				return false;
			expect_continuation = false;
			continue;
		}
		if (expect_continuation)
			return false;
		if (cell_is_wide_locked(console, &value)) {
			previous = value;
			expect_continuation = true;
		}
		else if (console->pixels == NULL && x + 1 < width
		    && (cells[x + 1].flags & ST_CONSOLE_CELL_CONTINUATION) != 0
		    && pair_matches(&value, &cells[x + 1])) {
			previous = value;
			expect_continuation = true;
		}
	}
	return !expect_continuation;
}

static void
make_clipped_blank_locked(st_console_t *console, struct st_console_cell *cell)
{
	uint8_t columns;

	cell->flags &= ~(ST_CONSOLE_CELL_CONTINUATION
	    | ST_CONSOLE_CELL_PIXEL_GRAPHICS);
	(void)st_console_font_lookup_locked(console, cell->font, ' ',
	    &cell->glyph_font, &cell->glyph, &columns);
}

static void
clear_pair_locked(st_console_t *console, size_t index)
{
	struct st_console_cell blank = st_console_blank_locked(console, ' ');
	size_t count = (size_t)console->columns * console->rows;
	struct st_console_cell *cell = &console->cells[index];

	if ((cell->flags & ST_CONSOLE_CELL_CONTINUATION) != 0
	    && index % console->columns != 0)
		set_cell_raw_locked(console, index - 1, &blank);
	else if (index + 1 < count && index % console->columns + 1
	    < console->columns
	    && (console->cells[index + 1].flags
	    & ST_CONSOLE_CELL_CONTINUATION) != 0)
		set_cell_raw_locked(console, index + 1, &blank);
	set_cell_raw_locked(console, index, &blank);
}

void
st_console_set_cell_locked(st_console_t *console, size_t index,
	const struct st_console_cell *source)
{
	struct st_console_cell value = *source;
	struct st_console_cell continuation;

	value = cell_for_storage(source);
	if (cells_equal(&console->cells[index], &value))
		return;
	clear_pair_locked(console, index);
	set_cell_raw_locked(console, index, &value);
	if ((value.flags & ST_CONSOLE_CELL_CONTINUATION) == 0
	    && cell_is_wide_locked(console, &value)
	    && index % console->columns + 1 < console->columns) {
		continuation = value;
		continuation.flags |= ST_CONSOLE_CELL_CONTINUATION;
		clear_pair_locked(console, index + 1);
		set_cell_raw_locked(console, index + 1, &continuation);
	}
}

void
st_console_mark_all_locked(st_console_t *console)
{
	size_t count = (size_t)console->columns * console->rows;
	size_t i;

	for (i = 0; i < count; ++i)
		console->cells[i].flags |= ST_CONSOLE_CELL_DIRTY;
	console->changed = true;
	console->raster_current = false;
}

static int
present_locked(st_console_t *console, bool forced, bool backend_locked)
{
	st_console_display_t *display = console->display;
	size_t count;
	size_t i;
	int status = 0;

	if (console->update_depth != 0 && !forced)
		return 0;
	if (!console->changed && !forced)
		return 0;
	status = st_console_rasterize_locked(console, false);
	if (status != 0)
		return status;
	if (atomic_load_explicit(&display->active, memory_order_acquire)
	    == console && display->backend_ops != NULL) {
		if (!backend_locked
		    && st_mutex_lock(&display->backend_mutex) != ST_THREAD_SUCCESS)
			return IO_ERROR_IO;
		if (atomic_load_explicit(&display->active, memory_order_acquire)
		    == console)
			status = display->backend_failed ? IO_ERROR_IO
			    : display->backend_ops->present(console);
		if (!backend_locked)
			(void)st_mutex_unlock(&display->backend_mutex);
	}
	if (status != 0)
		return status;
	count = (size_t)console->columns * console->rows;
	for (i = 0; i < count; ++i)
		console->cells[i].flags &= ~ST_CONSOLE_CELL_DIRTY;
	console->changed = false;
	console->pixel_surface_dirty = false;
	console->palette_dirty = false;
	return 0;
}

int
st_console_present_locked(st_console_t *console, bool forced)
{
	return present_locked(console, forced, false);
}

int
st_console_finish_change_locked(st_console_t *console)
{
	return request_presentation_locked(console);
}

int
st_console_size(st_console_t *console, uint32_t *columns, uint32_t *rows)
{
	int status;

	if (console == NULL || columns == NULL || rows == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rwlock_read_lock(&console->state_lock);
	if (status != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	*columns = console->columns;
	*rows = console->rows;
	(void)st_rwlock_unlock(&console->state_lock);
	return 0;
}

int
st_console_cell_pixel_size(st_console_t *console, uint32_t *width,
	uint32_t *height)
{
	int status;

	if (console == NULL || width == NULL || height == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rwlock_read_lock(&console->state_lock);
	if (status != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	*width = console->cell_width;
	*height = console->cell_height;
	(void)st_rwlock_unlock(&console->state_lock);
	return 0;
}

int
st_console_capabilities(st_console_t *console, uint32_t *capabilities)
{
	uint32_t value = 0;
	int status;

	if (console == NULL || capabilities == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rwlock_read_lock(&console->state_lock);
	if (status != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (console->pixels != NULL) {
		value |= ST_CONSOLE_CAP_LOADABLE_FONTS
		    | ST_CONSOLE_CAP_PALETTE_CHANGE
		    | ST_CONSOLE_CAP_CUSTOM_CURSOR
		    | ST_CONSOLE_CAP_EXTENDED_PALETTE
		    | ST_CONSOLE_CAP_BRIGHT_BACKGROUND
		    | ST_CONSOLE_CAP_BLOCKY_SCALING
		    | ST_CONSOLE_CAP_EXTERNAL_SCALING;
		if (console->display->backend_ops == NULL
		    || console->display->backend_ops->presents_pixels)
			value |= ST_CONSOLE_CAP_PIXELS;
	}
	if (console->display->backend == ST_CONSOLE_BACKEND_ANSI
	    || console->display->backend == ST_CONSOLE_BACKEND_NCURSES
	    || console->display->backend == ST_CONSOLE_BACKEND_WIN32)
		value |= ST_CONSOLE_CAP_TEXT_TERMINAL;
	if (console->display->backend == ST_CONSOLE_BACKEND_ANSI
	    || console->display->backend == ST_CONSOLE_BACKEND_NCURSES)
		value |= ST_CONSOLE_CAP_LOCAL_CONTROL_KEYS;
	if (console->display->backend_ops != NULL) {
		if (console->display->backend_ops->capabilities != NULL)
			value |= console->display->backend_ops->capabilities(console);
		if (console->display->backend_ops->set_title != NULL)
			value |= ST_CONSOLE_CAP_WINDOW_TITLE;
		if (console->display->backend_ops->set_clipboard_text != NULL
		    && console->display->backend_ops->get_clipboard_text != NULL)
			value |= ST_CONSOLE_CAP_CLIPBOARD;
		if (console->display->backend_ops->open_url != NULL)
			value |= ST_CONSOLE_CAP_OPEN_URL;
	}
	*capabilities = value;
	(void)st_rwlock_unlock(&console->state_lock);
	return 0;
}

int
st_console_resize(st_console_t *console, uint32_t columns, uint32_t rows)
{
	struct st_console_cell *cells;
	struct st_console_cell *drawn_cells = NULL;
	uint8_t *raster_rows = NULL;
	struct st_pixel_buffer *pixels = NULL;
	struct st_console_cell blank;
	size_t count;
	uint32_t pixel_width;
	uint32_t pixel_height;
	uint32_t ignored_extra_width;
	uint32_t ignored_extra_height;
	uint32_t copy_columns;
	uint32_t copy_rows;
	uint32_t y;
	int result;

	if (console == NULL || !cell_count(columns, rows, &count)
	    || (console->cell_width != 0
	    && (columns > (UINT32_MAX - console->pixel_extra_width)
	    / console->cell_width
	    || rows > (UINT32_MAX - console->pixel_extra_height)
	    / console->cell_height))
	    || !pixel_dimensions(columns, rows, console->cell_width,
	    console->cell_height,
	    console->cell_width == 0 ? 0
	    : columns * console->cell_width + console->pixel_extra_width,
	    console->cell_height == 0 ? 0
	    : rows * console->cell_height + console->pixel_extra_height,
	    &pixel_width, &pixel_height, &ignored_extra_width,
	    &ignored_extra_height))
		return IO_ERROR_INVALID_ARGUMENT;
	cells = malloc(count * sizeof(*cells));
	if (cells == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (pixel_width != 0) {
		drawn_cells = calloc(count, sizeof(drawn_cells[0]));
		raster_rows = calloc(rows, sizeof(raster_rows[0]));
		if (drawn_cells == NULL || raster_rows == NULL) {
			free(raster_rows);
			free(drawn_cells);
			free(cells);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		result = st_pixel_buffer_create(&pixels, pixel_width, pixel_height,
		    true);
		if (result != 0) {
			free(raster_rows);
			free(drawn_cells);
			free(cells);
			return result;
		}
	}
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS) {
		st_pixel_buffer_destroy(pixels);
		free(raster_rows);
		free(drawn_cells);
		free(cells);
		return IO_ERROR_IO;
	}
	if (atomic_load_explicit(&console->display->active,
	    memory_order_acquire) == console && console->display->backend_ops != NULL
	    && console->display->backend_ops->resize != NULL) {
		(void)st_mutex_lock(&console->display->backend_mutex);
		result = atomic_load_explicit(&console->display->active,
		    memory_order_acquire) == console
		    ? (console->display->backend_failed ? IO_ERROR_IO
		    : console->display->backend_ops->resize(console, columns, rows)) : 0;
		(void)st_mutex_unlock(&console->display->backend_mutex);
		if (result != 0) {
			(void)st_rwlock_unlock(&console->state_lock);
			st_pixel_buffer_destroy(pixels);
			free(raster_rows);
			free(drawn_cells);
			free(cells);
			return result;
		}
	}
	blank = st_console_blank_locked(console, ' ');
	blank.flags |= ST_CONSOLE_CELL_DIRTY;
	for (size_t i = 0; i < count; ++i)
		cells[i] = blank;
	copy_columns = columns < console->columns ? columns : console->columns;
	copy_rows = rows < console->rows ? rows : console->rows;
	for (y = 0; y < copy_rows; ++y)
		memcpy(&cells[(size_t)y * columns],
		    &console->cells[(size_t)y * console->columns],
		    (size_t)copy_columns * sizeof(*cells));
	free(console->cells);
	free(console->raster_rows);
	free(console->drawn_cells);
	st_pixel_buffer_destroy(console->pixels);
	console->cells = cells;
	console->raster_rows = raster_rows;
	console->drawn_cells = drawn_cells;
	console->pixels = pixels;
	console->pixel_top = 0;
	console->columns = columns;
	console->rows = rows;
	for (uint32_t y = 0; y < rows; ++y) {
		size_t last = (size_t)(y + 1) * columns - 1;

		if ((console->cells[last].flags & ST_CONSOLE_CELL_CONTINUATION) == 0
		    && cell_is_wide_locked(console, &console->cells[last])) {
			struct st_console_cell blank = st_console_blank_locked(console,
			    ' ');

			set_cell_raw_locked(console, last, &blank);
		}
	}
	console->window = (struct st_console_rect){0, 0, columns, rows};
	console->cursor_x = 0;
	console->cursor_y = 0;
	console->pixel_surface_dirty = pixels != NULL;
	st_console_mark_all_locked(console);
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_regrid_preserve_pixels(st_console_t *console,
	uint32_t columns, uint32_t rows, uint32_t cell_width,
	uint32_t cell_height)
{
	struct st_console_cell *cells;
	struct st_console_cell *drawn_cells;
	uint8_t *raster_rows;
	struct st_console_cell blank;
	size_t count;
	uint32_t pixel_width, pixel_height;
	uint32_t extra_width, extra_height;
	uint32_t old_cell_width, old_cell_height;
	uint32_t old_extra_width, old_extra_height;
	int result;

	if (console == NULL || cell_width == 0 || cell_height == 0
	    || !cell_count(columns, rows, &count))
		return IO_ERROR_INVALID_ARGUMENT;
	cells = malloc(count * sizeof(*cells));
	drawn_cells = malloc(count * sizeof(*drawn_cells));
	raster_rows = calloc(rows, sizeof(*raster_rows));
	if (cells == NULL || drawn_cells == NULL || raster_rows == NULL) {
		free(raster_rows);
		free(drawn_cells);
		free(cells);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS) {
		result = IO_ERROR_IO;
		goto discard;
	}
	if (console->pixels == NULL) {
		result = IO_ERROR_NOT_SUPPORTED;
		goto unlock;
	}
	if (!pixel_dimensions(columns, rows, cell_width, cell_height,
	    console->pixels->width, console->pixels->height,
	    &pixel_width, &pixel_height, &extra_width, &extra_height)) {
		result = IO_ERROR_INVALID_ARGUMENT;
		goto unlock;
	}
	result = st_console_rasterize_locked(console, false);
	if (result == 0)
		result = st_console_normalize_raster_locked(console);
	if (result != 0)
		goto unlock;
	old_cell_width = console->cell_width;
	old_cell_height = console->cell_height;
	old_extra_width = console->pixel_extra_width;
	old_extra_height = console->pixel_extra_height;
	console->cell_width = cell_width;
	console->cell_height = cell_height;
	console->pixel_extra_width = extra_width;
	console->pixel_extra_height = extra_height;
	if (atomic_load_explicit(&console->display->active,
	    memory_order_acquire) == console
	    && console->display->backend_ops != NULL
	    && console->display->backend_ops->resize != NULL) {
		(void)st_mutex_lock(&console->display->backend_mutex);
		result = atomic_load_explicit(&console->display->active,
		    memory_order_acquire) == console
		    ? (console->display->backend_failed ? IO_ERROR_IO
		    : console->display->backend_ops->resize(console,
		    columns, rows)) : 0;
		(void)st_mutex_unlock(&console->display->backend_mutex);
	}
	if (result != 0) {
		console->cell_width = old_cell_width;
		console->cell_height = old_cell_height;
		console->pixel_extra_width = old_extra_width;
		console->pixel_extra_height = old_extra_height;
		goto unlock;
	}
	blank = st_console_blank_locked(console, ' ');
	blank.flags &= ~ST_CONSOLE_CELL_DIRTY;
	blank.flags |= ST_CONSOLE_CELL_PIXEL_GRAPHICS;
	for (size_t i = 0; i < count; i++) {
		cells[i] = blank;
		drawn_cells[i] = blank;
	}
	free(console->cells);
	free(console->drawn_cells);
	free(console->raster_rows);
	console->cells = cells;
	console->drawn_cells = drawn_cells;
	console->raster_rows = raster_rows;
	console->columns = columns;
	console->rows = rows;
	console->window = (struct st_console_rect){0, 0, columns, rows};
	console->cursor_x = 0;
	console->cursor_y = 0;
	console->pixel_surface_dirty = true;
	console->changed = true;
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
unlock:
	(void)st_rwlock_unlock(&console->state_lock);
discard:
	free(raster_rows);
	free(drawn_cells);
	free(cells);
	return result;
}

int
st_console_reconfigure(st_console_t *console,
	const struct st_console_config *config)
{
	struct st_console_cell *cells;
	struct st_console_cell *drawn_cells = NULL;
	uint8_t *raster_rows = NULL;
	struct st_pixel_buffer *pixels = NULL;
	size_t count;
	uint32_t pixel_width;
	uint32_t pixel_height;
	uint32_t pixel_extra_width;
	uint32_t pixel_extra_height;
	uint32_t minimum_width;
	uint32_t minimum_height;
	uint32_t old_cell_width;
	uint32_t old_cell_height;
	uint32_t old_extra_width;
	uint32_t old_extra_height;
	bool configured_cursor;
	int result;

	if (console == NULL || config == NULL
	    || !cell_count(config->columns, config->rows, &count)
	    || !pixel_dimensions(config->columns, config->rows,
	    config->cell_width, config->cell_height, config->pixel_width,
	    config->pixel_height, &pixel_width, &pixel_height,
	    &pixel_extra_width, &pixel_extra_height)
	    || config->blink_mode < ST_CONSOLE_BLINK_PC
	    || config->blink_mode > ST_CONSOLE_BLINK_ATARI_ST
	    || (config->video_flags & ~ST_CONSOLE_VIDEO_FLAGS) != 0
	    || config->scaling_mode < ST_CONSOLE_SCALING_INTERNAL
	    || config->scaling_mode > ST_CONSOLE_SCALING_EXTERNAL
	    || (config->scaling_policy != ST_PIXEL_SCALING_BLOCKY
	    && config->scaling_policy != ST_PIXEL_SCALING_POINTY)
	    || ((config->aspect_width == 0) != (config->aspect_height == 0)))
		return IO_ERROR_INVALID_ARGUMENT;
	if (pixel_width != 0 && st_console_aspect_correct_size(pixel_width,
	    pixel_height, config->aspect_width, config->aspect_height,
	    &minimum_width, &minimum_height) != 0)
		return IO_ERROR_OUT_OF_RANGE;
	cells = malloc(count * sizeof(*cells));
	if (cells == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (pixel_width != 0) {
		drawn_cells = calloc(count, sizeof(*drawn_cells));
		raster_rows = calloc(config->rows, sizeof(*raster_rows));
		if (drawn_cells == NULL || raster_rows == NULL)
			result = IO_ERROR_OUT_OF_MEMORY;
		else
			result = st_pixel_buffer_create(&pixels, pixel_width,
			    pixel_height, true);
		if (result != 0) {
			free(raster_rows);
			free(drawn_cells);
			free(cells);
			return result;
		}
	}
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS) {
		st_pixel_buffer_destroy(pixels);
		free(raster_rows);
		free(drawn_cells);
		free(cells);
		return IO_ERROR_IO;
	}
	old_cell_width = console->cell_width;
	old_cell_height = console->cell_height;
	old_extra_width = console->pixel_extra_width;
	old_extra_height = console->pixel_extra_height;
	console->cell_width = config->cell_width;
	console->cell_height = config->cell_height;
	console->pixel_extra_width = pixel_extra_width;
	console->pixel_extra_height = pixel_extra_height;
	if (atomic_load_explicit(&console->display->active,
	    memory_order_acquire) == console && console->display->backend_ops != NULL
	    && console->display->backend_ops->resize != NULL) {
		(void)st_mutex_lock(&console->display->backend_mutex);
		result = atomic_load_explicit(&console->display->active,
		    memory_order_acquire) == console
		    ? (console->display->backend_failed ? IO_ERROR_IO
		    : console->display->backend_ops->resize(console,
		    config->columns, config->rows)) : 0;
		(void)st_mutex_unlock(&console->display->backend_mutex);
	}
	else
		result = 0;
	if (result != 0) {
		console->cell_width = old_cell_width;
		console->cell_height = old_cell_height;
		console->pixel_extra_width = old_extra_width;
		console->pixel_extra_height = old_extra_height;
		(void)st_rwlock_unlock(&console->state_lock);
		st_pixel_buffer_destroy(pixels);
		free(raster_rows);
		free(drawn_cells);
		free(cells);
		return result;
	}
	free(console->cells);
	free(console->raster_rows);
	free(console->drawn_cells);
	st_pixel_buffer_destroy(console->pixels);
	console->cells = cells;
	console->raster_rows = raster_rows;
	console->drawn_cells = drawn_cells;
	console->pixels = pixels;
	console->columns = config->columns;
	console->rows = config->rows;
	console->pixel_top = 0;
	console->window = (struct st_console_rect){0, 0, config->columns,
	    config->rows};
	console->cursor_x = 0;
	console->cursor_y = 0;
	console->wrap = config->wrap;
	configured_cursor = config->default_cursor.range != 0;
	if (configured_cursor)
		console->default_cursor = config->default_cursor;
	else {
		uint32_t height = config->cell_height == 0 ? 1
		    : config->cell_height;
		uint32_t thickness = height > 8 ? 2 : 1;

		console->default_cursor = (struct st_console_custom_cursor){
			.start = height - thickness,
			.end = height - 1,
			.range = height,
			.visible = true,
			.blink = true,
		};
	}
	console->custom_cursor = console->default_cursor;
	console->cursor_visible = console->default_cursor.visible;
	console->cursor_shape = console->default_cursor.start == 0
	    && console->default_cursor.end >= console->default_cursor.range - 1
	    ? ST_CONSOLE_CURSOR_BLOCK : ST_CONSOLE_CURSOR_UNDERLINE;
	console->cursor_custom = configured_cursor;
	console->blink_mode = config->blink_mode;
	console->video_flags = config->video_flags;
	console->scaling_mode = config->scaling_mode;
	console->blocky_scaling
	    = config->scaling_policy == ST_PIXEL_SCALING_BLOCKY;
	console->aspect_width = config->aspect_width;
	console->aspect_height = config->aspect_height;
	console->pixel_surface_dirty = pixels != NULL;
	console->palette_dirty = false;
	console->changed = true;
	initialize_cells(console, cells, count);
	if (atomic_load_explicit(&console->display->active,
	    memory_order_acquire) == console)
		st_console_display_reset_animation(console->display,
		    console->blink_mode);
	st_console_mark_all_locked(console);
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_write_cells(st_console_t *console,
	const struct st_console_rect *destination,
	const struct st_console_cell *cells, size_t stride)
{
	uint32_t x;
	uint32_t y;
	int result;

	if (console == NULL || destination == NULL || cells == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (!st_console_rect_valid(console, destination)
	    || stride < destination->width) {
		(void)st_rwlock_unlock(&console->state_lock);
		return IO_ERROR_OUT_OF_RANGE;
	}
	for (y = 0; y < destination->height; ++y) {
		if (!row_cells_valid_locked(console, &cells[(size_t)y * stride],
		    destination->width)) {
			(void)st_rwlock_unlock(&console->state_lock);
			return IO_ERROR_INVALID_ARGUMENT;
		}
	}
	for (y = 0; y < destination->height; ++y) {
		for (x = 0; x < destination->width; ++x) {
			size_t target = (size_t)(destination->y + y)
			    * console->columns + destination->x + x;
			st_console_set_cell_locked(console, target,
			    &cells[(size_t)y * stride + x]);
		}
	}
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_write_complete_cells(st_console_t *console,
	const struct st_console_rect *destination,
	const struct st_console_cell *cells, size_t stride)
{
	int result;

	if (console == NULL || destination == NULL || cells == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (!st_console_rect_valid(console, destination)
	    || stride < destination->width) {
		(void)st_rwlock_unlock(&console->state_lock);
		return IO_ERROR_OUT_OF_RANGE;
	}
	for (uint32_t y = 0; y < destination->height; ++y) {
		if (!complete_row_valid_locked(console,
		    &cells[(size_t)y * stride], destination->width)) {
			(void)st_rwlock_unlock(&console->state_lock);
			return IO_ERROR_INVALID_ARGUMENT;
		}
	}
	for (uint32_t y = 0; y < destination->height; ++y) {
		size_t first = (size_t)(destination->y + y) * console->columns
		    + destination->x;
		size_t last = first + destination->width - 1;

		clear_pair_locked(console, first);
		if (last != first)
			clear_pair_locked(console, last);
		for (uint32_t x = 0; x < destination->width; ++x) {
			struct st_console_cell value = cells[(size_t)y * stride + x];

			value.flags &= ST_CONSOLE_CELL_PUBLIC_FLAGS;
			set_cell_raw_locked(console, first + x, &value);
		}
	}
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_read_cells(st_console_t *console,
	const struct st_console_rect *source, struct st_console_cell *cells,
	size_t stride)
{
	uint32_t x;
	uint32_t y;
	int result;

	if (console == NULL || source == NULL || cells == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_read_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (!st_console_rect_valid(console, source) || stride < source->width) {
		(void)st_rwlock_unlock(&console->state_lock);
		return IO_ERROR_OUT_OF_RANGE;
	}
	for (y = 0; y < source->height; ++y) {
		for (x = 0; x < source->width; ++x) {
			cells[(size_t)y * stride + x] = console->cells[
			    (size_t)(source->y + y) * console->columns
			    + source->x + x];
			cells[(size_t)y * stride + x].flags &=
			    ~ST_CONSOLE_CELL_DIRTY;
		}
		if ((cells[(size_t)y * stride].flags
		    & ST_CONSOLE_CELL_CONTINUATION) != 0)
			make_clipped_blank_locked(console,
			    &cells[(size_t)y * stride]);
		if (cell_is_wide_locked(console,
		    &cells[(size_t)y * stride + source->width - 1])
		    && (cells[(size_t)y * stride + source->width - 1].flags
		    & ST_CONSOLE_CELL_CONTINUATION) == 0)
			make_clipped_blank_locked(console,
			    &cells[(size_t)y * stride + source->width - 1]);
		for (x = 0; x < source->width; ++x)
			cells[(size_t)y * stride + x].flags &=
			    ST_CONSOLE_CELL_PUBLIC_FLAGS;
	}
	(void)st_rwlock_unlock(&console->state_lock);
	return 0;
}

int
st_console_fill(st_console_t *console,
	const struct st_console_rect *destination,
	const struct st_console_cell *cell)
{
	uint32_t x;
	uint32_t y;
	int result;

	if (console == NULL || destination == NULL || cell == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (!st_console_rect_valid(console, destination)) {
		(void)st_rwlock_unlock(&console->state_lock);
		return IO_ERROR_OUT_OF_RANGE;
	}
	{
		struct st_console_cell value = cell_for_storage(cell);

		if ((value.flags & ST_CONSOLE_CELL_CONTINUATION) != 0
		    || cell_is_wide_locked(console, &value)
		    || !complete_cell_valid_locked(console, &value)) {
			(void)st_rwlock_unlock(&console->state_lock);
			return IO_ERROR_INVALID_ARGUMENT;
		}
	}
	for (y = 0; y < destination->height; ++y)
		for (x = 0; x < destination->width; ++x)
			st_console_set_cell_locked(console,
			    (size_t)(destination->y + y)
			    * console->columns + destination->x + x, cell);
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_begin_update(st_console_t *console)
{
	int result;

	if (console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (console->update_depth == UINT32_MAX)
		result = IO_ERROR_OUT_OF_RANGE;
	else {
		++console->update_depth;
		result = 0;
	}
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_end_update(st_console_t *console)
{
	int result;

	if (console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (console->update_depth == 0)
		result = IO_ERROR_INVALID_ARGUMENT;
	else {
		--console->update_depth;
		result = st_console_finish_change_locked(console);
	}
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_present(st_console_t *console)
{
	int result;

	if (console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	result = st_console_present_locked(console, true);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_set_palette(st_console_t *console, uint16_t first,
	const uint32_t *rgb, uint32_t count)
{
	uint32_t first_changed = count;
	uint32_t last_changed = 0;
	int result;

	if (console == NULL || rgb == NULL || count == 0
	    || count > UINT32_C(65536) - first)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	for (uint32_t i = 0; i < count; ++i) {
		uint32_t value = rgb[i] & UINT32_C(0xffffff);

		if (console->palette[(uint32_t)first + i] == value)
			continue;
		console->palette[(uint32_t)first + i] = value;
		if (first_changed == count)
			first_changed = i;
		last_changed = i;
	}
	if (first_changed == count) {
		(void)st_rwlock_unlock(&console->state_lock);
		return 0;
	}
	console->palette_dirty = true;
	if (atomic_load_explicit(&console->display->active, memory_order_acquire)
	    == console && console->display->backend_ops != NULL
	    && console->display->backend_ops->palette_changed != NULL
	    && !console->display->backend_failed) {
		uint16_t changed_first = (uint16_t)((uint32_t)first + first_changed);
		uint32_t changed_count = last_changed - first_changed + 1U;

		(void)st_mutex_lock(&console->display->backend_mutex);
		if (atomic_load_explicit(&console->display->active,
		    memory_order_acquire) == console
		    && !console->display->backend_failed)
			console->display->backend_ops->palette_changed(console,
			    changed_first, &console->palette[changed_first],
			    changed_count);
		(void)st_mutex_unlock(&console->display->backend_mutex);
	}
	st_console_mark_all_locked(console);
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_get_palette(st_console_t *console, uint16_t first,
	uint32_t *rgb, uint32_t count)
{
	int result;

	if (console == NULL || rgb == NULL || count == 0
	    || count > UINT32_C(65536) - first)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_read_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	memcpy(rgb, &console->palette[first], (size_t)count * sizeof(*rgb));
	(void)st_rwlock_unlock(&console->state_lock);
	return 0;
}

static int
compare_font_registration(const void *left, const void *right)
{
	const struct st_console_font_registration *left_font = left;
	const struct st_console_font_registration *right_font = right;

	return left_font->identifier < right_font->identifier ? -1
	    : left_font->identifier > right_font->identifier ? 1 : 0;
}

static bool
font_bank_valid(const uint8_t *bitmap, size_t bitmap_size, uint32_t width,
	uint32_t height, uint32_t glyph_count, size_t row_stride,
	size_t glyph_stride)
{
	size_t minimum_row;
	size_t minimum_glyph;
	size_t last_offset;

	if (bitmap == NULL || width == 0 || height == 0 || glyph_count == 0)
		return false;
	minimum_row = (size_t)(width / 8) + (width % 8 != 0 ? 1 : 0);
	if (row_stride < minimum_row || (size_t)height > SIZE_MAX / row_stride)
		return false;
	minimum_glyph = (size_t)height * row_stride;
	if (glyph_stride < minimum_glyph)
		return false;
	if ((size_t)glyph_count - 1 > (SIZE_MAX - minimum_glyph) / glyph_stride)
		return false;
	last_offset = ((size_t)glyph_count - 1) * glyph_stride;
	return bitmap_size >= last_offset + minimum_glyph;
}

static bool
font_cluster_valid(const char *text, size_t text_size)
{
	st_unicode_text_t *graphemes = NULL;
	struct st_unicode_text_item item;
	char *normalized = NULL;
	size_t normalized_size = 0;
	size_t consumed;
	size_t count = 0;
	bool changed;
	bool valid = false;

	if (text == NULL || text_size == 0
	    || st_unicode_text_normalize(&normalized, &normalized_size,
	    text, text_size, &changed) != 0 || changed
	    || normalized_size != text_size
	    || memcmp(normalized, text, text_size) != 0)
		goto finished;
	if (st_unicode_text_create(&graphemes, ST_UNICODE_TEXT_GRAPHEMES) != 0
	    || st_unicode_text_feed(graphemes, text, text_size, &consumed) != 0
	    || consumed != text_size || st_unicode_text_finish(graphemes) != 0)
		goto finished;
	while (st_unicode_text_peek(graphemes, &item) == 0) {
		if (item.kind != ST_UNICODE_TEXT_ITEM_TEXT)
			goto finished;
		++count;
		(void)st_unicode_text_consume(graphemes);
	}
	valid = count == 1;
finished:
	free(normalized);
	st_unicode_text_destroy(&graphemes);
	return valid;
}

static bool
font_valid(const struct st_console_font *font)
{
	uint64_t total;

	if (font == NULL || !font_bank_valid(font->bitmap, font->bitmap_size,
	    font->width, font->height, font->glyph_count, font->row_stride,
	    font->glyph_stride))
		return false;
	if (font->wide_glyph_count != 0
	    && (font->width > UINT32_MAX / 2
	    || !font_bank_valid(font->wide_bitmap, font->wide_bitmap_size,
	    font->width * 2, font->height, font->wide_glyph_count,
	    font->wide_row_stride, font->wide_glyph_stride)
	    ))
		return false;
	total = (uint64_t)font->glyph_count + font->wide_glyph_count;
	if (total > UINT32_MAX || font->fallback_glyph >= font->glyph_count
	    || font->decode_error_glyph >= font->glyph_count
	    || (font->wide_fallback_glyph != ST_CONSOLE_FONT_NO_WIDE_FALLBACK
	    && (font->wide_fallback_glyph < font->glyph_count
	    || font->wide_fallback_glyph >= total)))
		return false;
	if (font->mapping_count != 0 && font->mapping == NULL)
		return false;
	for (size_t i = 0; i < font->mapping_count; ++i) {
		if (font->mapping[i].glyph >= total
		    || font->mapping[i].codepoint > UINT32_C(0x10ffff)
		    || (font->mapping[i].codepoint >= UINT32_C(0xd800)
		    && font->mapping[i].codepoint <= UINT32_C(0xdfff))
		    || (i != 0 && font->mapping[i - 1].codepoint
		    >= font->mapping[i].codepoint))
			return false;
	}
	if (font->cluster_mapping_count != 0 && font->cluster_mapping == NULL)
		return false;
	for (size_t i = 0; i < font->cluster_mapping_count; ++i) {
		if (font->cluster_mapping[i].glyph >= total
		    || !font_cluster_valid(font->cluster_mapping[i].text,
		    font->cluster_mapping[i].text_size))
			return false;
	}
	return true;
}

static bool
find_font(st_console_t *console, uint16_t identifier, size_t *index)
{
	const struct st_console_font_registration key = {
		.identifier = identifier,
	};

	return array_list_sorted_find(console->fonts, &key,
	    compare_font_registration, index);
}

const struct st_console_font *
st_console_font_locked(st_console_t *console, uint16_t identifier)
{
	size_t index;
	struct st_console_font_registration *registration;

	if (!find_font(console, identifier, &index))
		return NULL;
	registration = console->fonts[index];
	atomic_store_explicit(&registration->last_used,
	    atomic_fetch_add_explicit(&console->font_clock, 1,
	    memory_order_relaxed) + 1, memory_order_relaxed);
	return &registration->font;
}

bool
st_console_font_glyph_locked(st_console_t *console, uint16_t identifier,
	uint32_t glyph, const uint8_t **bitmap, uint32_t *width,
	size_t *row_stride)
{
	const struct st_console_font *font = st_console_font_locked(console,
	    identifier);

	if (font == NULL)
		return false;
	if (glyph < font->glyph_count) {
		*bitmap = font->bitmap + (size_t)glyph * font->glyph_stride;
		*width = font->width;
		*row_stride = font->row_stride;
		return true;
	}
	glyph -= font->glyph_count;
	if (glyph >= font->wide_glyph_count)
		return false;
	*bitmap = font->wide_bitmap + (size_t)glyph * font->wide_glyph_stride;
	*width = font->width * 2;
	*row_stride = font->wide_row_stride;
	return true;
}

static bool
font_lookup_exact(const struct st_console_font *font, uint32_t codepoint,
	uint32_t *glyph, uint8_t *columns)
{
	size_t left = 0;
	size_t right = font->mapping_count;

	while (left < right) {
		size_t middle = left + (right - left) / 2;

		if (font->mapping[middle].codepoint < codepoint)
			left = middle + 1;
		else
			right = middle;
	}
	if (left < font->mapping_count
	    && font->mapping[left].codepoint == codepoint)
		*glyph = font->mapping[left].glyph;
	else
		return false;
	*columns = *glyph < font->glyph_count ? 1 : 2;
	return true;
}

static bool
font_lookup_cluster_exact(const struct st_console_font *font,
	const char *text, size_t text_size, uint32_t *glyph)
{
	for (size_t i = 0; i < font->cluster_mapping_count; ++i) {
		const struct st_console_font_cluster_mapping *mapping
		    = &font->cluster_mapping[i];

		if (mapping->text_size == text_size
		    && memcmp(mapping->text, text, text_size) == 0) {
			*glyph = mapping->glyph;
			return true;
		}
	}
	return false;
}

static const struct st_console_font *
fallback_font_locked(st_console_t *console, uint16_t identifier,
	uint16_t *fallback_identifier)
{
	const struct st_console_font *selected = st_console_font_locked(console,
	    identifier);
	const struct st_console_font *font = st_console_font_locked(console, 0);

	*fallback_identifier = identifier;
	if (font == NULL || (selected != NULL
	    && (font->width != selected->width || font->height != selected->height)))
		return selected;
	*fallback_identifier = 0;
	return font;
}

static bool
font_lookup_fallback_locked(st_console_t *console, uint16_t identifier,
	uint8_t columns, uint16_t *glyph_font, uint32_t *glyph)
{
	uint16_t owner;
	const struct st_console_font *font = fallback_font_locked(console,
	    identifier, &owner);

	if (font == NULL || (columns == 2 && font->wide_fallback_glyph
	    == ST_CONSOLE_FONT_NO_WIDE_FALLBACK))
		return false;
	*glyph = columns == 2 ? font->wide_fallback_glyph : font->fallback_glyph;
	*glyph_font = owner;
	return true;
}

bool
st_console_font_lookup_cluster_locked(st_console_t *console,
	uint16_t identifier, const char *text, size_t text_size, uint8_t columns,
	uint16_t *glyph_font, uint32_t *glyph)
{
	const struct st_console_font *font = st_console_font_locked(console,
	    identifier);
	uint16_t fallback_identifier = identifier;
	uint32_t codepoint;
	size_t consumed;
	bool scalar = st_utf8_decode_one(&codepoint, &consumed, text,
	    text_size) == 0 && consumed == text_size;
	uint8_t ignored_columns;

#define LOOKUP_EXACT(candidate) \
	((scalar && font_lookup_exact((candidate), codepoint, glyph, \
	    &ignored_columns)) || font_lookup_cluster_exact((candidate), text, \
	    text_size, glyph))

	if (font != NULL && LOOKUP_EXACT(font)) {
		*glyph_font = identifier;
		return true;
	}
	font = fallback_font_locked(console, identifier, &fallback_identifier);
	if (font == NULL)
		return false;
	if (fallback_identifier == 0 && LOOKUP_EXACT(font)) {
		*glyph_font = 0;
		return true;
	}
#undef LOOKUP_EXACT
	return (columns == 1 || columns == 2)
	    && font_lookup_fallback_locked(console, identifier, columns,
	    glyph_font, glyph);
}

bool
st_console_font_lookup_locked(st_console_t *console, uint16_t identifier,
	uint32_t codepoint, uint16_t *glyph_font, uint32_t *glyph,
	uint8_t *columns)
{
	const struct st_console_font *font;
	char encoded[4];
	ptrdiff_t size = st_utf8_encode_one(encoded, sizeof(encoded), codepoint);

	if (size < 0)
		return false;
	*columns = st_unicode_columns(codepoint);
	if (*columns == 0)
		*columns = 1;
	if (!st_console_font_lookup_cluster_locked(console, identifier,
	    encoded, (size_t)size, *columns, glyph_font, glyph))
		return false;
	font = st_console_font_locked(console, *glyph_font);
	if (font != NULL)
		*columns = *glyph < font->glyph_count ? 1 : 2;
	return true;
}

bool
st_console_font_glyph_text_locked(st_console_t *console,
	uint16_t identifier, uint32_t glyph, char *text, size_t text_size,
	size_t *required)
{
	const struct st_console_font *font = st_console_font_locked(console,
	    identifier);
	const char *selected = NULL;
	size_t selected_size = 0;
	char encoded[4];
	char scalar_selected[4];

	*required = 0;
	if (font == NULL)
		return false;
	for (size_t i = 0; i < font->mapping_count; ++i) {
		ptrdiff_t size;

		if (font->mapping[i].glyph != glyph)
			continue;
		size = st_utf8_encode_one(encoded, sizeof(encoded),
		    font->mapping[i].codepoint);
		if (size < 0)
			continue;
		if (selected == NULL) {
			memcpy(scalar_selected, encoded, (size_t)size);
			selected = scalar_selected;
			selected_size = (size_t)size;
		}
	}
	for (size_t i = 0; i < font->cluster_mapping_count; ++i) {
		const struct st_console_font_cluster_mapping *mapping
		    = &font->cluster_mapping[i];

		if (mapping->glyph != glyph)
			continue;
		if (selected == NULL) {
			selected = mapping->text;
			selected_size = mapping->text_size;
		}
	}
	if (selected == NULL)
		return false;
	*required = selected_size;
	if (text_size >= selected_size)
		memcpy(text, selected, selected_size);
	return true;
}

bool
st_console_cell_codepoint_locked(st_console_t *console,
	const struct st_console_cell *cell, uint32_t *codepoint)
{
	char encoded[4];
	size_t required;
	size_t consumed;

	if (!st_console_font_glyph_text_locked(console, cell->glyph_font,
	    cell->glyph, encoded, sizeof(encoded), &required)
	    || required > sizeof(encoded)
	    || st_utf8_decode_one(codepoint, &consumed, encoded, required) != 0
	    || consumed != required) {
		*codepoint = UINT32_C(0xfffd);
		return false;
	}
	return true;
}

static void
font_registration_free(struct st_console_font_registration *registration)
{
	if (registration == NULL)
		return;
	if (registration->owned) {
		for (size_t i = 0;
		    i < registration->font.cluster_mapping_count; ++i)
			free((void *)registration->font.cluster_mapping[i].text);
		free((void *)registration->font.bitmap);
		free((void *)registration->font.wide_bitmap);
		free((void *)registration->font.mapping);
		free((void *)registration->font.cluster_mapping);
	}
	free(registration);
}

static struct st_console_font_registration *
font_registration_create(const struct st_console_font *font, bool copy)
{
	struct st_console_font_registration *registration = calloc(1,
	    sizeof(*registration));

	if (registration == NULL)
		return NULL;
	registration->font = *font;
	registration->owned = copy;
	if (!copy)
		return registration;
	registration->font.bitmap = NULL;
	registration->font.wide_bitmap = NULL;
	registration->font.mapping = NULL;
	registration->font.cluster_mapping = NULL;
	registration->font.bitmap = malloc(font->bitmap_size);
	if (registration->font.bitmap == NULL)
		goto failed;
	memcpy((void *)registration->font.bitmap, font->bitmap, font->bitmap_size);
	if (font->wide_glyph_count != 0) {
		registration->font.wide_bitmap = malloc(font->wide_bitmap_size);
		if (registration->font.wide_bitmap == NULL)
			goto failed;
		memcpy((void *)registration->font.wide_bitmap, font->wide_bitmap,
		    font->wide_bitmap_size);
	}
	if (font->mapping_count != 0) {
		if (font->mapping_count > SIZE_MAX / sizeof(font->mapping[0]))
			goto failed;
		registration->font.mapping = malloc(font->mapping_count
		    * sizeof(font->mapping[0]));
		if (registration->font.mapping == NULL)
			goto failed;
		memcpy((void *)registration->font.mapping, font->mapping,
		    font->mapping_count * sizeof(font->mapping[0]));
	}
	if (font->cluster_mapping_count != 0) {
		struct st_console_font_cluster_mapping *mapping;

		if (font->cluster_mapping_count > SIZE_MAX / sizeof(mapping[0]))
			goto failed;
		mapping = calloc(font->cluster_mapping_count, sizeof(mapping[0]));
		if (mapping == NULL)
			goto failed;
		registration->font.cluster_mapping = mapping;
		for (size_t i = 0; i < font->cluster_mapping_count; ++i) {
			mapping[i] = font->cluster_mapping[i];
			mapping[i].text = malloc(mapping[i].text_size);
			if (mapping[i].text == NULL)
				goto failed;
			memcpy((void *)mapping[i].text, font->cluster_mapping[i].text,
			    mapping[i].text_size);
		}
	}
	return registration;
failed:
	font_registration_free(registration);
	return NULL;
}

static int
register_font(st_console_t *console, const struct st_console_font *font,
	uint16_t *identifier, bool copy)
{
	struct st_console_font_registration *registration;
	struct st_console_font_registration *evicted = NULL;
	uint16_t assigned;
	size_t index;
	int result;

	if (console == NULL || identifier == NULL || !font_valid(font))
		return IO_ERROR_INVALID_ARGUMENT;
	registration = font_registration_create(font, copy);
	if (registration == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS) {
		font_registration_free(registration);
		return IO_ERROR_IO;
	}
	if (console->next_font_identifier <= UINT16_MAX) {
		assigned = (uint16_t)console->next_font_identifier++;
		index = array_list_count(console->fonts);
	}
	else {
		index = 0;
		for (size_t i = 1; i < array_list_count(console->fonts); ++i) {
			const struct st_console_font_registration *candidate
			    = console->fonts[i];
			const struct st_console_font_registration *oldest
			    = console->fonts[index];

			if (atomic_load_explicit(&candidate->last_used,
			    memory_order_relaxed) < atomic_load_explicit(
			    &oldest->last_used, memory_order_relaxed))
				index = i;
		}
		evicted = console->fonts[index];
		assigned = evicted->identifier;
	}
	registration->identifier = assigned;
	atomic_store_explicit(&registration->last_used,
	    atomic_fetch_add_explicit(&console->font_clock, 1,
	    memory_order_relaxed) + 1, memory_order_relaxed);
	if (evicted != NULL)
		console->fonts[index] = registration;
	else if (!array_list_append(&console->fonts, registration)) {
		--console->next_font_identifier;
		(void)st_rwlock_unlock(&console->state_lock);
		font_registration_free(registration);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	if (evicted != NULL) {
		fprintf(stderr, "SyncTERM: font registry full; replacing "
		    "least-recently-used font in slot %u\n", assigned);
	}
	(void)st_rwlock_unlock(&console->state_lock);
	font_registration_free(evicted);
	*identifier = assigned;
	return 0;
}

int
st_console_register_static_font(st_console_t *console,
	const struct st_console_font *font, uint16_t *identifier)
{
	return register_font(console, font, identifier, false);
}

int
st_console_register_font_copy(st_console_t *console,
	const struct st_console_font *font, uint16_t *identifier)
{
	return register_font(console, font, identifier, true);
}

int
st_console_font_registered(st_console_t *console, uint16_t identifier,
	bool *registered)
{
	size_t index;
	int result;

	if (console == NULL || registered == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_read_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	*registered = find_font(console, identifier, &index);
	(void)st_rwlock_unlock(&console->state_lock);
	return 0;
}

uint16_t
st_console_font_for_flags(const struct st_console_font_selection *selection,
	uint32_t cell_flags)
{
	bool bright;
	bool blink;

	if (selection == NULL)
		return 0;
	bright = selection->bright_selects_font
	    && (cell_flags & ST_CONSOLE_CELL_BRIGHT) != 0;
	blink = selection->blink_selects_font
	    && (cell_flags & ST_CONSOLE_CELL_BLINK) != 0;
	if (bright && blink)
		return selection->bright_blink;
	if (bright)
		return selection->bright;
	if (blink)
		return selection->blink;
	return selection->normal;
}

int
st_console_font_lookup(st_console_t *console, uint16_t identifier,
	uint32_t codepoint, uint16_t *glyph_font, uint32_t *glyph,
	uint8_t *columns)
{
	int result;
	bool found;

	if (console == NULL || glyph_font == NULL || glyph == NULL
	    || columns == NULL
	    || codepoint > UINT32_C(0x10ffff)
	    || (codepoint >= UINT32_C(0xd800) && codepoint <= UINT32_C(0xdfff)))
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_read_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	found = st_console_font_lookup_locked(console, identifier, codepoint,
	    glyph_font, glyph, columns);
	(void)st_rwlock_unlock(&console->state_lock);
	return found ? 0 : IO_ERROR_NOT_FOUND;
}

int
st_console_font_lookup_fallback(st_console_t *console, uint16_t identifier,
	uint8_t columns, uint16_t *glyph_font, uint32_t *glyph)
{
	bool found;

	if (console == NULL || glyph_font == NULL || glyph == NULL
	    || (columns != 1 && columns != 2))
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_rwlock_read_lock(&console->state_lock) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	found = font_lookup_fallback_locked(console, identifier, columns,
	    glyph_font, glyph);
	(void)st_rwlock_unlock(&console->state_lock);
	return found ? 0 : IO_ERROR_NOT_FOUND;
}

int
st_console_font_lookup_cluster(st_console_t *console, uint16_t identifier,
	const char *text, size_t text_size, uint8_t columns,
	uint16_t *glyph_font, uint32_t *glyph)
{
	int result;
	bool found;

	if (console == NULL || (text == NULL && text_size != 0)
	    || text_size == 0 || (columns != 1 && columns != 2)
	    || glyph_font == NULL || glyph == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_read_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	found = st_console_font_lookup_cluster_locked(console, identifier,
	    text, text_size, columns, glyph_font, glyph);
	(void)st_rwlock_unlock(&console->state_lock);
	return found ? 0 : IO_ERROR_NOT_FOUND;
}

int
st_console_font_glyph_text(st_console_t *console, uint16_t identifier,
	uint32_t glyph, char *text, size_t text_size, size_t *required)
{
	size_t needed = 0;
	bool found;
	int result;

	if (console == NULL || required == NULL
	    || (text == NULL && text_size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_read_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	found = st_console_font_glyph_text_locked(console, identifier, glyph,
	    text, text_size, &needed);
	(void)st_rwlock_unlock(&console->state_lock);
	*required = needed;
	if (!found)
		return IO_ERROR_NOT_FOUND;
	return text_size < needed ? IO_ERROR_OUT_OF_RANGE : 0;
}

int
st_console_font_glyph_columns(st_console_t *console, uint16_t identifier,
	uint32_t glyph, uint8_t *columns)
{
	const struct st_console_font *font;
	const uint8_t *bitmap;
	uint32_t width;
	size_t row_stride;
	int result;

	if (console == NULL || columns == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_read_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	font = st_console_font_locked(console, identifier);
	if (font == NULL || !st_console_font_glyph_locked(console, identifier,
	    glyph, &bitmap, &width, &row_stride))
		result = IO_ERROR_NOT_FOUND;
	else {
		*columns = width == font->width * 2 ? 2 : 1;
		result = 0;
	}
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_snapshot_create(st_console_snapshot_t **result,
	st_console_t *console)
{
	st_console_snapshot_t *snapshot;
	size_t count;
	int status;

	if (result == NULL || console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rwlock_write_lock(&console->state_lock);
	if (status != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	/* A snapshot is a presentation boundary, including inside a batch. */
	status = st_console_rasterize_locked(console, false);
	if (status != 0) {
		(void)st_rwlock_unlock(&console->state_lock);
		return status;
	}
	status = st_console_present_locked(console, true);
	if (status != 0) {
		(void)st_rwlock_unlock(&console->state_lock);
		return status;
	}
	count = (size_t)console->columns * console->rows;
	snapshot = calloc(1, sizeof(*snapshot));
	if (snapshot == NULL) {
		(void)st_rwlock_unlock(&console->state_lock);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	snapshot->cells = malloc(count * sizeof(snapshot->cells[0]));
	if (snapshot->cells == NULL) {
		free(snapshot);
		(void)st_rwlock_unlock(&console->state_lock);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	if (console->pixels != NULL) {
		status = st_pixel_buffer_clone(&snapshot->pixels, console->pixels);
		if (status != 0) {
			free(snapshot->cells);
			free(snapshot);
			(void)st_rwlock_unlock(&console->state_lock);
			return status;
		}
	}
	snapshot->columns = console->columns;
	snapshot->owner = console;
	snapshot->rows = console->rows;
	memcpy(snapshot->palette, console->palette, sizeof(snapshot->palette));
	snapshot->style = console->style;
	snapshot->window = console->window;
	snapshot->cursor_x = console->cursor_x;
	snapshot->cursor_y = console->cursor_y;
	snapshot->wrap = console->wrap;
	snapshot->cursor_visible = console->cursor_visible;
	snapshot->cursor_shape = console->cursor_shape;
	snapshot->cursor_custom = console->cursor_custom;
	snapshot->custom_cursor = console->custom_cursor;
	snapshot->blink_mode = console->blink_mode;
	snapshot->video_flags = console->video_flags;
	snapshot->blocky_scaling = console->blocky_scaling;
	snapshot->pixel_top = console->pixel_top;
	snapshot->full_surface_scroll = console->full_surface_scroll;
	memcpy(snapshot->cells, console->cells,
	    count * sizeof(snapshot->cells[0]));
	for (size_t i = 0; i < count; ++i)
		snapshot->cells[i].flags &= ~ST_CONSOLE_CELL_DIRTY;
	(void)st_rwlock_unlock(&console->state_lock);
	*result = snapshot;
	return 0;
}

int
st_console_snapshot_restore(st_console_t *console,
	const st_console_snapshot_t *snapshot)
{
	uint32_t first_changed = UINT32_MAX;
	uint32_t last_changed = 0;
	size_t count;
	int result;

	if (console == NULL || snapshot == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (snapshot->owner != console
	    || snapshot->columns != console->columns
	    || snapshot->rows != console->rows
	    || (snapshot->pixels == NULL) != (console->pixels == NULL)
	    || (snapshot->pixels != NULL
	    && (snapshot->pixels->width != console->pixels->width
	    || snapshot->pixels->height != console->pixels->height))) {
		(void)st_rwlock_unlock(&console->state_lock);
		return IO_ERROR_OUT_OF_RANGE;
	}
	count = (size_t)console->columns * console->rows;
	for (uint32_t i = 0; i <= UINT16_MAX; ++i) {
		if (console->palette[i] == snapshot->palette[i])
			continue;
		if (first_changed == UINT32_MAX)
			first_changed = i;
		last_changed = i;
	}
	memcpy(console->palette, snapshot->palette, sizeof(console->palette));
	if (first_changed != UINT32_MAX)
		console->palette_dirty = true;
	if (first_changed != UINT32_MAX
	    && console->display->backend_ops != NULL
	    && console->display->backend_ops->palette_changed != NULL
	    && !console->display->backend_failed) {
		(void)st_mutex_lock(&console->display->backend_mutex);
		if (atomic_load_explicit(&console->display->active,
		    memory_order_acquire) == console)
			console->display->backend_ops->palette_changed(console,
			    (uint16_t)first_changed, &console->palette[first_changed],
			    last_changed - first_changed + 1U);
		(void)st_mutex_unlock(&console->display->backend_mutex);
	}
	console->style = snapshot->style;
	console->window = snapshot->window;
	console->cursor_x = snapshot->cursor_x;
	console->cursor_y = snapshot->cursor_y;
	console->wrap = snapshot->wrap;
	console->cursor_visible = snapshot->cursor_visible;
	console->cursor_shape = snapshot->cursor_shape;
	console->cursor_custom = snapshot->cursor_custom;
	console->custom_cursor = snapshot->custom_cursor;
	console->blink_mode = snapshot->blink_mode;
	console->video_flags = snapshot->video_flags;
	console->blocky_scaling = snapshot->blocky_scaling;
	memcpy(console->cells, snapshot->cells,
	    count * sizeof(console->cells[0]));
	if (console->pixels != NULL) {
		size_t pixels_count = (size_t)console->pixels->width
		    * console->pixels->height;

		memcpy(console->pixels->primary, snapshot->pixels->primary,
		    pixels_count * sizeof(console->pixels->primary[0]));
		memcpy(console->pixels->alternate, snapshot->pixels->alternate,
		    pixels_count * sizeof(console->pixels->alternate[0]));
		memcpy(console->drawn_cells, snapshot->cells,
		    count * sizeof(console->drawn_cells[0]));
		console->pixel_top = snapshot->pixel_top;
		console->full_surface_scroll = snapshot->full_surface_scroll;
		console->pixel_surface_dirty = true;
	}
	st_console_mark_all_locked(console);
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

void
st_console_snapshot_destroy(st_console_snapshot_t *snapshot)
{
	if (snapshot == NULL)
		return;
	st_pixel_buffer_destroy(snapshot->pixels);
	free(snapshot->cells);
	free(snapshot);
}

static int
event_take_locked(st_console_t *console, struct st_console_event *event,
	const struct timespec *deadline, bool polling)
{
	int result = ST_THREAD_SUCCESS;

	while (console->event_count == 0) {
		if (polling)
			return IO_ERROR_WOULD_BLOCK;
		if (deadline == NULL)
			result = st_condition_wait(&console->event_ready,
			    &console->event_mutex);
		else
			result = st_condition_timedwait(&console->event_ready,
			    &console->event_mutex, deadline);
		if (result == ST_THREAD_TIMED_OUT)
			return IO_ERROR_TIMED_OUT;
		if (result != ST_THREAD_SUCCESS)
			return IO_ERROR_IO;
	}
	*event = console->events[console->event_head];
	console->event_head = (console->event_head + 1) % console->event_capacity;
	--console->event_count;
	return 0;
}

static int
reserve_events_locked(st_console_t *console, size_t additional)
{
	struct st_console_event *events;
	size_t required;
	size_t capacity;

	if (additional > SIZE_MAX - console->event_count)
		return IO_ERROR_OUT_OF_MEMORY;
	required = console->event_count + additional;
	if (required <= console->event_capacity)
		return 0;
	capacity = console->event_capacity == 0 ? 32 : console->event_capacity;
	while (capacity < required) {
		if (capacity > SIZE_MAX / 2)
			return IO_ERROR_OUT_OF_MEMORY;
		capacity *= 2;
	}
	if (capacity > SIZE_MAX / sizeof(*events))
		return IO_ERROR_OUT_OF_MEMORY;
	events = malloc(capacity * sizeof(*events));
	if (events == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (size_t i = 0; i < console->event_count; ++i)
		events[i] = console->events[(console->event_head + i)
		    % console->event_capacity];
	free(console->events);
	console->events = events;
	console->event_capacity = capacity;
	console->event_head = 0;
	return 0;
}

static void
append_event_locked(st_console_t *console,
	const struct st_console_event *event)
{
	size_t tail = (console->event_head + console->event_count)
	    % console->event_capacity;

	console->events[tail] = *event;
	++console->event_count;
}

static bool
physical_key_down(const st_console_display_t *display, uint16_t evdev)
{
	return (display->physical_keys[evdev / 8]
	    & (uint8_t)(UINT8_C(1) << (evdev % 8))) != 0;
}

static void
set_physical_key(st_console_display_t *display, uint16_t evdev, bool pressed)
{
	uint8_t mask = (uint8_t)(UINT8_C(1) << (evdev % 8));

	if (pressed)
		display->physical_keys[evdev / 8] |= mask;
	else
		display->physical_keys[evdev / 8] &= (uint8_t)~mask;
}

static bool
event_valid(const struct st_console_event *event)
{
	if (event == NULL || event->type < ST_CONSOLE_EVENT_KEY_CODE
	    || event->type > ST_CONSOLE_EVENT_CLOSE)
		return false;
	switch (event->type) {
	case ST_CONSOLE_EVENT_TEXT:
		return st_unicode_scalar_valid(event->data.text.codepoint);
	case ST_CONSOLE_EVENT_MOUSE:
		return event->data.mouse.action >= ST_CONSOLE_MOUSE_MOVE
		    && event->data.mouse.action <= ST_CONSOLE_MOUSE_WHEEL;
	case ST_CONSOLE_EVENT_RESIZE:
		return event->data.resize.columns != 0
		    && event->data.resize.rows != 0;
	case ST_CONSOLE_EVENT_KEY_CODE:
	case ST_CONSOLE_EVENT_PHYSICAL_KEY:
	case ST_CONSOLE_EVENT_DOORWAY_KEY:
	case ST_CONSOLE_EVENT_FOCUS:
	case ST_CONSOLE_EVENT_CLOSE:
		return true;
	}
	return false;
}

bool
st_console_control_key(uint32_t character, uint16_t *key)
{
	uint16_t result;

	if (character >= 'a' && character <= 'z')
		character -= 'a' - 'A';
	if (character >= '@' && character <= '_')
		result = (uint16_t)(character - '@');
	else if (character == ' ')
		result = 0;
	else
		return false;
	if (key != NULL)
		*key = result;
	return true;
}

static int
post_event(st_console_display_t *display, st_console_t *console,
	const struct st_console_event *event)
{
	struct st_console_event release = {
		.type = ST_CONSOLE_EVENT_PHYSICAL_KEY,
		.data.physical_key = {.pressed = false},
	};
	size_t release_count = 0;
	size_t added = 1;
	int result;

	if (display == NULL || !event_valid(event))
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_mutex_lock(&display->input_mutex);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (console == NULL)
		console = atomic_load_explicit(&display->input_active, memory_order_acquire);
	if (console == NULL) {
		if (event->type == ST_CONSOLE_EVENT_PHYSICAL_KEY)
			set_physical_key(display, event->data.physical_key.evdev,
			    event->data.physical_key.pressed);
		(void)st_mutex_unlock(&display->input_mutex);
		return 0;
	}
	result = st_mutex_lock(&console->event_mutex);
	if (result != ST_THREAD_SUCCESS) {
		(void)st_mutex_unlock(&display->input_mutex);
		return IO_ERROR_IO;
	}
	if (event->type == ST_CONSOLE_EVENT_FOCUS
	    && !event->data.focus.focused) {
		for (unsigned int key = 0; key <= UINT16_MAX; ++key)
			if (physical_key_down(display, (uint16_t)key))
				++release_count;
	}
	result = reserve_events_locked(console, release_count + 1);
	if (result != 0) {
		(void)st_mutex_unlock(&console->event_mutex);
		(void)st_mutex_unlock(&display->input_mutex);
		return result;
	}
	if (release_count != 0) {
		for (unsigned int key = 0; key <= UINT16_MAX; ++key) {
			if (!physical_key_down(display, (uint16_t)key))
				continue;
			release.data.physical_key.evdev = (uint16_t)key;
			append_event_locked(console, &release);
			set_physical_key(display, (uint16_t)key, false);
		}
		added += release_count;
	}
	if (event->type == ST_CONSOLE_EVENT_PHYSICAL_KEY)
		set_physical_key(display, event->data.physical_key.evdev,
		    event->data.physical_key.pressed);
	append_event_locked(console, event);
	result = ST_THREAD_SUCCESS;
	for (size_t i = 0; i < added && result == ST_THREAD_SUCCESS; ++i)
		result = st_condition_signal(&console->event_ready);
	(void)st_mutex_unlock(&console->event_mutex);
	(void)st_mutex_unlock(&display->input_mutex);
	return result == ST_THREAD_SUCCESS ? 0 : IO_ERROR_IO;
}

int
st_console_display_post_event(st_console_display_t *display,
	const struct st_console_event *event)
{
	return post_event(display, NULL, event);
}

int
st_console_post_event(st_console_t *console,
	const struct st_console_event *event)
{
	return console == NULL ? IO_ERROR_INVALID_ARGUMENT
	    : post_event(console->display, console, event);
}

int
st_console_physical_key_state(st_console_t *console, uint16_t evdev,
	bool *pressed)
{
	return console == NULL ? IO_ERROR_INVALID_ARGUMENT
	    : st_console_display_physical_key_state(console->display, evdev,
	    pressed);
}

int
st_console_synthesize_physical_key(st_console_t *console, uint16_t evdev,
	bool pressed)
{
	struct st_console_event event = {
		.type = ST_CONSOLE_EVENT_PHYSICAL_KEY,
		.data.physical_key = {
			.evdev = evdev,
			.pressed = pressed,
		},
	};
	st_console_display_t *display;
	int result;

	if (console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	display = console->display;
	result = st_mutex_lock(&display->input_mutex);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (physical_key_down(display, evdev) == pressed) {
		(void)st_mutex_unlock(&display->input_mutex);
		return 0;
	}
	result = st_mutex_lock(&console->event_mutex);
	if (result != ST_THREAD_SUCCESS) {
		(void)st_mutex_unlock(&display->input_mutex);
		return IO_ERROR_IO;
	}
	result = reserve_events_locked(console, 1);
	if (result == 0) {
		append_event_locked(console, &event);
		set_physical_key(display, evdev, pressed);
		result = st_condition_signal(&console->event_ready)
		    == ST_THREAD_SUCCESS ? 0 : IO_ERROR_IO;
	}
	(void)st_mutex_unlock(&console->event_mutex);
	(void)st_mutex_unlock(&display->input_mutex);
	return result;
}

int
st_console_display_physical_key_state(st_console_display_t *display,
	uint16_t evdev, bool *pressed)
{
	int result;

	if (display == NULL || pressed == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_mutex_lock(&display->input_mutex);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	*pressed = physical_key_down(display, evdev);
	(void)st_mutex_unlock(&display->input_mutex);
	return 0;
}

static int
event_take(st_console_t *console, struct st_console_event *event,
	const struct timespec *deadline, bool polling)
{
	int result;

	if (console == NULL || event == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_mutex_lock(&console->event_mutex);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	result = event_take_locked(console, event, deadline, polling);
	(void)st_mutex_unlock(&console->event_mutex);
	return result;
}

int
st_console_event_try(st_console_t *console, struct st_console_event *event)
{
	return event_take(console, event, NULL, true);
}

int
st_console_event_wait(st_console_t *console, struct st_console_event *event)
{
	return event_take(console, event, NULL, false);
}

int
st_console_event_timedwait(st_console_t *console,
	struct st_console_event *event, const struct timespec *deadline)
{
	if (deadline == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return event_take(console, event, deadline, false);
}

int
st_console_events_clear(st_console_t *console)
{
	int result;

	if (console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_mutex_lock(&console->event_mutex);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	console->event_head = 0;
	console->event_count = 0;
	(void)st_mutex_unlock(&console->event_mutex);
	return 0;
}

int
st_console_set_title(st_console_t *console, const char *title)
{
	char *copy;
	size_t copy_size;
	int result;

	if (console == NULL || title == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_unicode_text_normalize(&copy, &copy_size, title,
	    strlen(title), NULL);
	if (result != 0)
		return result;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS) {
		free(copy);
		return IO_ERROR_IO;
	}
	free(console->title);
	console->title = copy;
	result = console->display->backend_ops == NULL
	    || console->display->backend_ops->set_title == NULL
	    ? IO_ERROR_NOT_SUPPORTED : 0;
	if (result == 0) {
		(void)st_mutex_lock(&console->display->backend_mutex);
		if (atomic_load_explicit(&console->display->active,
		    memory_order_acquire) == console)
			result = console->display->backend_failed ? IO_ERROR_IO
			    : console->display->backend_ops->set_title(console->display, copy);
		(void)st_mutex_unlock(&console->display->backend_mutex);
	}
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_set_clipboard_text(st_console_t *console, const char *text)
{
	return console == NULL ? IO_ERROR_INVALID_ARGUMENT
	    : st_console_display_set_clipboard_text(console->display, text);
}

int
st_console_get_clipboard_text(st_console_t *console, char **result)
{
	return console == NULL ? IO_ERROR_INVALID_ARGUMENT
	    : st_console_display_get_clipboard_text(console->display, result);
}

void
st_console_free_text(char *text)
{
	free(text);
}

int
st_console_open_url(st_console_t *console, const char *url)
{
	return console == NULL ? IO_ERROR_INVALID_ARGUMENT
	    : st_console_display_open_url(console->display, url);
}

int
st_console_set_pointer_visible(st_console_t *console, bool visible)
{
	int result;

	if (console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	console->pointer_visible = visible;
	result = console->display->backend_ops == NULL
	    || console->display->backend_ops->set_pointer_visible == NULL
	    ? IO_ERROR_NOT_SUPPORTED : 0;
	if (result == 0) {
		(void)st_mutex_lock(&console->display->backend_mutex);
		if (atomic_load_explicit(&console->display->active,
		    memory_order_acquire) == console)
			result = console->display->backend_failed ? IO_ERROR_IO
			    : console->display->backend_ops->set_pointer_visible(console->display, visible);
		(void)st_mutex_unlock(&console->display->backend_mutex);
	}
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_set_pointer_shape(st_console_t *console,
	enum st_console_pointer_shape shape)
{
	int result;

	if (console == NULL || shape < ST_CONSOLE_POINTER_DEFAULT
	    || shape > ST_CONSOLE_POINTER_LINK)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	console->pointer_shape = shape;
	result = console->display->backend_ops == NULL
	    || console->display->backend_ops->set_pointer_shape == NULL
	    ? IO_ERROR_NOT_SUPPORTED : 0;
	if (result == 0) {
		(void)st_mutex_lock(&console->display->backend_mutex);
		if (atomic_load_explicit(&console->display->active,
		    memory_order_acquire) == console)
			result = console->display->backend_failed ? IO_ERROR_IO
			    : console->display->backend_ops->set_pointer_shape(
			    console->display, shape);
		(void)st_mutex_unlock(&console->display->backend_mutex);
	}
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_set_fullscreen(st_console_t *console, bool fullscreen)
{
	return console == NULL ? IO_ERROR_INVALID_ARGUMENT
	    : st_console_display_set_fullscreen(console->display, fullscreen);
}

static int
display_host_call(st_console_display_t *display, const char *text,
	bool enabled, int operation, char **text_result)
{
	int result;

	if (display == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&display->lifecycle_mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (st_mutex_lock(&display->backend_mutex) != ST_THREAD_SUCCESS) {
		(void)st_mutex_unlock(&display->lifecycle_mutex);
		return IO_ERROR_IO;
	}
	if (display->backend_ops == NULL)
		result = IO_ERROR_NOT_SUPPORTED;
	else if (display->backend_failed)
		result = IO_ERROR_IO;
	else if (operation == 0)
		result = display->backend_ops->set_clipboard_text == NULL
		    ? IO_ERROR_NOT_SUPPORTED
		    : display->backend_ops->set_clipboard_text(display, text);
	else if (operation == 1)
		result = display->backend_ops->get_clipboard_text == NULL
		    ? IO_ERROR_NOT_SUPPORTED
		    : display->backend_ops->get_clipboard_text(display, text_result);
	else if (operation == 2)
		result = display->backend_ops->open_url == NULL
		    ? IO_ERROR_NOT_SUPPORTED
		    : display->backend_ops->open_url(display, text);
	else
		result = display->backend_ops->set_fullscreen == NULL
		    ? IO_ERROR_NOT_SUPPORTED
		    : display->backend_ops->set_fullscreen(display, enabled);
	(void)st_mutex_unlock(&display->backend_mutex);
	(void)st_mutex_unlock(&display->lifecycle_mutex);
	return result;
}

int
st_console_display_set_clipboard_text(st_console_display_t *display,
	const char *text)
{
	char *normalized;
	size_t normalized_size;
	int result;

	if (text == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_unicode_text_normalize(&normalized, &normalized_size, text,
	    strlen(text), NULL);
	if (result != 0)
		return result;
	result = display_host_call(display, normalized, false, 0, NULL);
	free(normalized);
	return result;
}

int
st_console_display_get_clipboard_text(st_console_display_t *display,
	char **result)
{
	char *raw = NULL;
	char *normalized;
	size_t normalized_size;
	int status;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = display_host_call(display, NULL, false, 1, &raw);
	if (status != 0)
		return status;
	if (raw == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_unicode_text_normalize(&normalized, &normalized_size, raw,
	    strlen(raw), NULL);
	free(raw);
	if (status != 0)
		return status;
	*result = normalized;
	return 0;
}

int
st_console_display_open_url(st_console_display_t *display, const char *url)
{
	char *normalized;
	size_t normalized_size;
	int result;

	if (url == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_unicode_text_normalize(&normalized, &normalized_size, url,
	    strlen(url), NULL);
	if (result != 0)
		return result;
	result = display_host_call(display, normalized, false, 2, NULL);
	free(normalized);
	return result;
}

int
st_console_display_set_fullscreen(st_console_display_t *display,
	bool fullscreen)
{
	return display_host_call(display, NULL, fullscreen, 3, NULL);
}

int
st_console_display_get_scaling(st_console_display_t *display, double *result)
{
	double scaling;
	int status;

	if (display == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&display->lifecycle_mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (st_mutex_lock(&display->backend_mutex) != ST_THREAD_SUCCESS) {
		(void)st_mutex_unlock(&display->lifecycle_mutex);
		return IO_ERROR_IO;
	}
	if (display->backend_ops == NULL
	    || display->backend_ops->get_scaling == NULL)
		status = IO_ERROR_NOT_SUPPORTED;
	else if (display->backend_failed)
		status = IO_ERROR_IO;
	else
		status = display->backend_ops->get_scaling(display, &scaling);
	(void)st_mutex_unlock(&display->backend_mutex);
	(void)st_mutex_unlock(&display->lifecycle_mutex);
	if (status == 0)
		*result = scaling;
	return status;
}
