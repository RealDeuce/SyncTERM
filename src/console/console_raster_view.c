/* Positioned native text grids sharing a display console's raster. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "console_internal.h"
#include "io_error.h"

int
st_console_create_raster_view(st_console_t **result, st_console_t *parent, uint32_t columns, uint32_t rows,
    uint32_t cell_width, uint32_t cell_height, uint64_t x, uint64_t y)
{
	if (result == NULL || parent == NULL || columns == 0 || rows == 0 || cell_width == 0 || cell_height == 0
	    || columns > UINT32_MAX / cell_width || rows > UINT32_MAX / cell_height
	    || (size_t)columns > SIZE_MAX / rows
	    || (size_t)columns * rows > SIZE_MAX / sizeof(struct st_console_cell))
		return IO_ERROR_INVALID_ARGUMENT;
	if (parent->raster_parent != NULL)
		return IO_ERROR_NOT_SUPPORTED;
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	/* Zero cell dimensions construct only the existing logical cell storage. */
	const struct st_console_config geometry = {.columns = columns, .rows = rows};
	st_console_t                  *console  = NULL;
	int                            status   = st_console_create(&console, &display, &geometry);
	if (status != 0)
		return status;
	size_t                  count       = (size_t)columns * rows;
	struct st_pixel_buffer *pixels      = calloc(1, sizeof(*pixels));
	struct st_console_cell *drawn       = calloc(count, sizeof(*drawn));
	uint8_t                *raster_rows = calloc(rows, sizeof(*raster_rows));
	console->view_text_batch            = calloc(count, sizeof(*console->view_text_batch));
	if (pixels == NULL || drawn == NULL || raster_rows == NULL || console->view_text_batch == NULL) {
		free(pixels);
		free(drawn);
		free(raster_rows);
		st_console_destroy(console);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	if (st_rwlock_write_lock(st_console_state_lock(parent)) != ST_THREAD_SUCCESS)
		status = IO_ERROR_IO;
	else {
		if (parent->pixels == NULL || parent->pixels->alternate == NULL)
			status = IO_ERROR_NOT_SUPPORTED;
		else {
			pixels->width          = columns * cell_width;
			pixels->height         = rows * cell_height;
			pixels->primary        = parent->pixels->primary;
			pixels->alternate      = parent->pixels->alternate;
			console->pixels        = pixels;
			console->drawn_cells   = drawn;
			console->raster_rows   = raster_rows;
			console->cell_width    = cell_width;
			console->cell_height   = cell_height;
			console->raster_parent = parent;
			console->raster_x      = x;
			console->raster_y      = y;
			console->default_cursor =
			    (struct st_console_custom_cursor) {.start = cell_height > 8 ? cell_height - 2
			                                                                : cell_height - 1,
				.end                                  = cell_height - 1,
				.range                                = cell_height,
				.visible                              = true,
				.blink                                = true};
			console->custom_cursor = console->default_cursor;
			memcpy(console->palette, parent->palette, sizeof(console->palette));
			++parent->raster_children;
		}
		(void)st_rwlock_unlock(st_console_state_lock(parent));
	}
	if (status != 0) {
		free(pixels);
		free(drawn);
		free(raster_rows);
		st_console_destroy(console);
		return status;
	}
	*result = console;
	return 0;
}

static int
adopt(st_console_t *console, bool transient, bool parent_palette, uint32_t width, uint32_t height)
{
	if (console == NULL || console->raster_parent == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_rwlock_write_lock(st_console_state_lock(console)) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	st_console_t *parent = console->raster_parent;
	if (width && (width > UINT32_MAX / console->columns || height > UINT32_MAX / console->rows)) {
		(void)st_rwlock_unlock(st_console_state_lock(console));
		return IO_ERROR_INVALID_ARGUMENT;
	}
	int           status = parent->raster_current ? 0 : st_console_rasterize_locked(parent, false);
	if (status != 0) {
		(void)st_rwlock_unlock(st_console_state_lock(console));
		return status;
	}
	/* Initial setup and any unpresented text are hidden by adoption. */
	if (!console->raster_enabled || !console->raster_current) {
		size_t count = (size_t)console->columns * console->rows;
		for (size_t i = 0; i < count; ++i) {
			console->cells[i].flags &= ~ST_CONSOLE_CELL_DIRTY;
			console->drawn_cells[i] = console->cells[i];
		}
	}
	if (++console->view_batch == 0) {
		memset(console->view_text_batch, 0,
		    (size_t)console->columns * console->rows * sizeof(*console->view_text_batch));
		console->view_batch = 1;
	}
	if (width) {
		console->cell_width = width;
		console->cell_height = height;
		console->pixels->width = console->columns * width;
		console->pixels->height = console->rows * height;
	}
	console->raster_current      = true;
	console->raster_enabled      = true;
	console->raster_transient    = transient;
	console->raster_parent_palette = parent_palette;
	console->changed             = false;
	console->dirty_row_end       = 0;
	console->pixel_surface_dirty = false;
	(void)st_rwlock_unlock(st_console_state_lock(console));
	return 0;
}

int
st_console_raster_view_adopt(st_console_t *console)
{
	return adopt(console, false, false, 0, 0);
}

int
st_console_raster_view_adopt_transient(st_console_t *console)
{
	return adopt(console, true, false, 0, 0);
}

int
st_console_raster_view_adopt_indexed(st_console_t *console)
{
	return adopt(console, false, true, 0, 0);
}

int
st_console_raster_view_adopt_indexed_size(st_console_t *console, uint32_t width, uint32_t height)
{
	if (!width || !height)
		return IO_ERROR_INVALID_ARGUMENT;
	return adopt(console, false, true, width, height);
}
