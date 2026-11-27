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
	return st_console_create_clipped_raster_view(result, parent, columns, rows, cell_width, cell_height, x, y,
	    columns, rows);
}

int
st_console_create_clipped_raster_view(st_console_t **result, st_console_t *parent, uint32_t columns, uint32_t rows,
    uint32_t cell_width, uint32_t cell_height, uint64_t x, uint64_t y, uint32_t visible_columns,
    uint32_t visible_rows)
{
	if (result == NULL || parent == NULL || columns == 0 || rows == 0 || cell_width == 0 || cell_height == 0
	    || visible_columns > columns || visible_rows > rows || columns > UINT32_MAX / cell_width
	    || rows > UINT32_MAX / cell_height || (size_t)columns > SIZE_MAX / rows
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
			console->view_capacity_columns = columns;
			console->view_columns  = visible_columns;
			console->view_rows     = visible_rows;
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

int
st_console_place_raster_view(st_console_t *console, uint64_t x, uint64_t y, const struct st_console_viewport *port)
{
	if (!console || !console->raster_parent || !port)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_rwlock_write_lock(st_console_state_lock(console)) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	const struct st_console_rect *source = &port->cells;
	if (source->x > console->columns || source->width > console->columns - source->x
	    || source->y > console->rows || source->height > console->rows - source->y) {
		(void)st_rwlock_unlock(st_console_state_lock(console));
		return IO_ERROR_INVALID_ARGUMENT;
	}
	console->raster_x     = x;
	console->raster_y     = y;
	console->view_columns  = source->width;
	console->view_rows     = source->height;
	console->view_left     = source->x;
	console->view_top      = source->y;
	console->view_hidden   = port->hidden;
	console->view_follow_x = port->follow_x;
	console->view_follow_y = port->follow_y;
	console->view_cursor_x = console->cursor_x;
	console->view_cursor_y = console->cursor_y;
	(void)st_rwlock_unlock(st_console_state_lock(console));
	return 0;
}

int
st_console_raster_view_origin(st_console_t *console, uint32_t *x, uint32_t *y)
{
	if (!console || !console->raster_parent || !x || !y)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_rwlock_read_lock(st_console_state_lock(console)) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	*x = console->view_left;
	*y = console->view_top;
	(void)st_rwlock_unlock(st_console_state_lock(console));
	return 0;
}

int
st_console_set_view_output(st_console_t *console, st_console_view_output *output, void *context)
{
	if (!console || !console->raster_parent)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_rwlock_write_lock(st_console_state_lock(console)) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	console->view_output         = output;
	console->view_output_context = context;
	(void)st_rwlock_unlock(st_console_state_lock(console));
	return 0;
}

int
st_console_redraw_raster_view(st_console_t *console)
{
	if (!console || !console->raster_parent)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_rwlock_write_lock(st_console_state_lock(console)) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	console->view_repaint   = true;
	console->raster_current = false;
	int status              = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(st_console_state_lock(console));
	return status;
}

static uint32_t
reveal(uint32_t first, uint32_t cursor, uint32_t count)
{
	if (cursor < first)
		return cursor;
	if (count && cursor - first >= count)
		return cursor - count + 1;
	return first;
}

void
st_console_follow_view_locked(st_console_t *c, uint32_t x, uint32_t y)
{
	uint32_t left = c->view_left, top = c->view_top;
	if (c->view_follow_x && x != c->view_cursor_x)
		left = reveal(left, x, c->view_columns);
	if (c->view_follow_y && y != c->view_cursor_y)
		top = reveal(top, y, c->view_rows);
	c->view_cursor_x = x;
	c->view_cursor_y = y;
	if (left == c->view_left && top == c->view_top)
		return;
	c->view_left      = left;
	c->view_top       = top;
	c->view_repaint   = true;
	c->raster_current = false;
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
