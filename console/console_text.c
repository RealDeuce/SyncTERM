/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_internal.h"

#include "io_error.h"
#include "st_unicode_text.h"
#include "st_unicode_width.h"
#include "st_utf8.h"

#include <stdlib.h>
#include <string.h>

static struct st_console_cell
style_cell_locked(const st_console_t *console, uint32_t codepoint,
	uint8_t *columns)
{
	struct st_console_cell cell;
	uint32_t glyph = codepoint;
	uint16_t glyph_font = console->style.font;
	uint8_t resolved_columns = st_unicode_columns(codepoint);

	(void)st_console_font_lookup_locked((st_console_t *)console,
	    console->style.font, codepoint, &glyph_font, &glyph,
	    &resolved_columns);
	cell = (struct st_console_cell){
		.glyph = glyph,
		.foreground = console->style.foreground,
		.background = console->style.background,
		.flags = console->style.flags & ST_CONSOLE_CELL_PUBLIC_FLAGS,
		.font = console->style.font,
		.glyph_font = glyph_font,
		.hyperlink = console->style.hyperlink,
	};
	if (columns != NULL)
		*columns = resolved_columns;
	return cell;
}

static struct st_console_cell
style_cluster_locked(const st_console_t *console, const char *text,
	size_t text_size, uint8_t columns)
{
	struct st_console_cell cell = {
		.foreground = console->style.foreground,
		.background = console->style.background,
		.flags = console->style.flags & ST_CONSOLE_CELL_PUBLIC_FLAGS,
		.font = console->style.font,
		.glyph_font = console->style.font,
		.hyperlink = console->style.hyperlink,
	};

	(void)st_console_font_lookup_cluster_locked((st_console_t *)console,
	    cell.font, text, text_size, columns, &cell.glyph_font, &cell.glyph);
	return cell;
}

struct st_console_cell
st_console_blank_locked(const st_console_t *console, uint32_t codepoint)
{
	return style_cell_locked(console, codepoint, NULL);
}

static bool
fill_codepoint_valid_locked(const st_console_t *console, uint32_t codepoint)
{
	uint8_t columns;

	if (!st_unicode_scalar_valid(codepoint))
		return false;
	(void)style_cell_locked(console, codepoint, &columns);
	return columns == 1;
}

int
st_console_set_style(st_console_t *console,
	const struct st_console_style *style)
{
	int result;

	if (console == NULL || style == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	console->style = *style;
	console->style.flags &= ST_CONSOLE_CELL_PUBLIC_FLAGS;
	(void)st_rwlock_unlock(&console->state_lock);
	return 0;
}

int
st_console_get_style(st_console_t *console, struct st_console_style *style)
{
	int result;

	if (console == NULL || style == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_read_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	*style = console->style;
	(void)st_rwlock_unlock(&console->state_lock);
	return 0;
}

static bool
position_in_window(const st_console_t *console, uint32_t x, uint32_t y)
{
	return x >= console->window.x && y >= console->window.y
	    && x - console->window.x < console->window.width
	    && y - console->window.y < console->window.height;
}

int
st_console_set_position(st_console_t *console, uint32_t x, uint32_t y)
{
	int result;

	if (console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (!position_in_window(console, x, y))
		result = IO_ERROR_OUT_OF_RANGE;
	else {
		console->cursor_x = x;
		console->cursor_y = y;
		console->changed = true;
		result = st_console_finish_change_locked(console);
	}
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_set_cursor(st_console_t *console, bool visible,
	enum st_console_cursor_shape shape)
{
	struct st_console_custom_cursor cursor;
	int result;

	if (console == NULL || shape < ST_CONSOLE_CURSOR_BLOCK
	    || shape > ST_CONSOLE_CURSOR_BAR)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	cursor = console->default_cursor;
	cursor.visible = visible;
	if (shape == ST_CONSOLE_CURSOR_BLOCK) {
		cursor.start = 0;
		cursor.end = cursor.range - 1;
	}
	if (console->cursor_visible != visible || console->cursor_shape != shape
	    || console->cursor_custom != (shape != ST_CONSOLE_CURSOR_BAR)
	    || (shape != ST_CONSOLE_CURSOR_BAR
	    && (console->custom_cursor.start != cursor.start
	    || console->custom_cursor.end != cursor.end
	    || console->custom_cursor.range != cursor.range
	    || console->custom_cursor.visible != cursor.visible
	    || console->custom_cursor.blink != cursor.blink))) {
		console->cursor_visible = visible;
		console->cursor_shape = shape;
		console->cursor_custom = shape != ST_CONSOLE_CURSOR_BAR;
		if (console->cursor_custom)
			console->custom_cursor = cursor;
		console->changed = true;
	}
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_get_default_cursor(st_console_t *console,
	struct st_console_custom_cursor *cursor)
{
	int result;

	if (console == NULL || cursor == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_read_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	*cursor = console->default_cursor;
	(void)st_rwlock_unlock(&console->state_lock);
	return 0;
}

int
st_console_set_default_cursor(st_console_t *console,
	const struct st_console_custom_cursor *cursor)
{
	int result;

	if (console == NULL || cursor == NULL || cursor->range == 0
	    || cursor->start >= cursor->range || cursor->end >= cursor->range
	    || cursor->start > cursor->end)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	console->default_cursor = *cursor;
	console->custom_cursor = *cursor;
	console->cursor_visible = cursor->visible;
	console->cursor_shape = cursor->start == 0
	    && cursor->end == cursor->range - 1
	    ? ST_CONSOLE_CURSOR_BLOCK : ST_CONSOLE_CURSOR_BAR;
	console->cursor_custom = true;
	console->changed = true;
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_get_cursor(st_console_t *console, bool *visible,
	enum st_console_cursor_shape *shape)
{
	int result;

	if (console == NULL || visible == NULL || shape == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_read_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	*visible = console->cursor_visible;
	*shape = console->cursor_shape;
	(void)st_rwlock_unlock(&console->state_lock);
	return 0;
}

int
st_console_set_custom_cursor(st_console_t *console,
	const struct st_console_custom_cursor *cursor)
{
	int result;

	if (console == NULL || cursor == NULL || cursor->range == 0)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (!console->cursor_custom
	    || console->custom_cursor.start != cursor->start
	    || console->custom_cursor.end != cursor->end
	    || console->custom_cursor.range != cursor->range
	    || console->custom_cursor.visible != cursor->visible
	    || console->custom_cursor.blink != cursor->blink) {
		console->custom_cursor = *cursor;
		console->cursor_custom = true;
		console->cursor_visible = cursor->visible;
		console->cursor_shape = cursor->start == 0
		    ? ST_CONSOLE_CURSOR_BLOCK : ST_CONSOLE_CURSOR_UNDERLINE;
		console->changed = true;
	}
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_get_custom_cursor(st_console_t *console,
	struct st_console_custom_cursor *cursor)
{
	int result;

	if (console == NULL || cursor == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_read_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (console->cursor_custom)
		*cursor = console->custom_cursor;
	else {
		*cursor = console->default_cursor;
		cursor->visible = console->cursor_visible;
		if (console->cursor_shape == ST_CONSOLE_CURSOR_BLOCK) {
			cursor->start = 0;
			cursor->end = cursor->range - 1;
		}
	}
	(void)st_rwlock_unlock(&console->state_lock);
	return 0;
}

int
st_console_set_blink_mode(st_console_t *console,
	enum st_console_blink_mode mode)
{
	int result;

	if (console == NULL || mode < ST_CONSOLE_BLINK_PC
	    || mode > ST_CONSOLE_BLINK_ATARI_ST)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (console->blink_mode != mode) {
		console->blink_mode = mode;
		console->changed = true;
		if (atomic_load_explicit(&console->display->active,
		    memory_order_acquire) == console)
			st_console_display_reset_animation(console->display, mode);
	}
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_get_blink_mode(st_console_t *console,
	enum st_console_blink_mode *mode)
{
	int result;

	if (console == NULL || mode == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_read_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	*mode = console->blink_mode;
	(void)st_rwlock_unlock(&console->state_lock);
	return 0;
}

int
st_console_set_video_flags(st_console_t *console, uint32_t flags)
{
	const uint32_t presentation_flags = ST_CONSOLE_VIDEO_NO_BRIGHT
	    | ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND | ST_CONSOLE_VIDEO_NO_BLINK
	    | ST_CONSOLE_VIDEO_LINE_GRAPHICS_EXPAND;
	int result;

	if (console == NULL || (flags & ~ST_CONSOLE_VIDEO_FLAGS) != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (((flags ^ console->video_flags) & ST_CONSOLE_VIDEO_EXPAND) != 0) {
		(void)st_rwlock_unlock(&console->state_lock);
		return IO_ERROR_INVALID_ARGUMENT;
	}
	if (flags != console->video_flags) {
		uint32_t changed = flags ^ console->video_flags;

		console->video_flags = flags;
		if ((changed & presentation_flags) != 0) {
			size_t count = (size_t)console->columns * console->rows;

			st_console_mark_all_locked(console);
			for (size_t i = 0; console->drawn_cells != NULL && i < count;
			    ++i)
				st_console_drawn_at(console, i)->flags
				    |= ST_CONSOLE_CELL_DIRTY;
		}
	}
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_get_video_flags(st_console_t *console, uint32_t *flags)
{
	int result;

	if (console == NULL || flags == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_read_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	*flags = console->video_flags;
	(void)st_rwlock_unlock(&console->state_lock);
	return 0;
}

int
st_console_set_scaling_mode(st_console_t *console,
	enum st_console_scaling_mode mode)
{
	int result;

	if (console == NULL || mode < ST_CONSOLE_SCALING_INTERNAL
	    || mode > ST_CONSOLE_SCALING_EXTERNAL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (console->scaling_mode != mode) {
		console->scaling_mode = mode;
		console->changed = true;
	}
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_get_scaling_mode(st_console_t *console,
	enum st_console_scaling_mode *mode)
{
	int result;

	if (console == NULL || mode == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_read_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	*mode = console->scaling_mode;
	(void)st_rwlock_unlock(&console->state_lock);
	return 0;
}

int
st_console_set_blocky_scaling(st_console_t *console, bool blocky)
{
	int result;

	if (console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (console->blocky_scaling != blocky) {
		console->blocky_scaling = blocky;
		console->changed = true;
	}
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_get_blocky_scaling(st_console_t *console, bool *blocky)
{
	int result;

	if (console == NULL || blocky == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_read_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	*blocky = console->blocky_scaling;
	(void)st_rwlock_unlock(&console->state_lock);
	return 0;
}

int
st_console_set_aspect_ratio(st_console_t *console, uint32_t width,
	uint32_t height)
{
	uint32_t old_width;
	uint32_t old_height;
	uint32_t checked_width;
	uint32_t checked_height;
	int result;

	if (console == NULL || ((width == 0) != (height == 0)))
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (console->pixels != NULL
	    && st_console_aspect_correct_size(console->pixels->width,
	    console->pixels->height, width, height, &checked_width,
	    &checked_height) != 0) {
		(void)st_rwlock_unlock(&console->state_lock);
		return IO_ERROR_OUT_OF_RANGE;
	}
	if (console->aspect_width != width || console->aspect_height != height) {
		old_width = console->aspect_width;
		old_height = console->aspect_height;
		console->aspect_width = width;
		console->aspect_height = height;
		result = 0;
		if (console->pixels != NULL
		    && st_console_display_active_console(console) == console
		    && console->display->backend_ops != NULL
		    && console->display->backend_ops->resize != NULL) {
			(void)st_mutex_lock(&console->display->backend_mutex);
			if (st_console_display_active_console(console) == console)
				result = console->display->backend_failed ? IO_ERROR_IO
				    : console->display->backend_ops->resize(console,
				    console->columns, console->rows);
			(void)st_mutex_unlock(&console->display->backend_mutex);
		}
		if (result != 0) {
			console->aspect_width = old_width;
			console->aspect_height = old_height;
		}
		else
			console->changed = true;
	}
	else
		result = 0;
	if (result == 0)
		result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_get_aspect_ratio(st_console_t *console, uint32_t *width,
	uint32_t *height)
{
	int result;

	if (console == NULL || width == NULL || height == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_read_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	*width = console->aspect_width;
	*height = console->aspect_height;
	(void)st_rwlock_unlock(&console->state_lock);
	return 0;
}

int
st_console_get_position(st_console_t *console, uint32_t *x, uint32_t *y)
{
	int result;

	if (console == NULL || x == NULL || y == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_read_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	*x = console->cursor_x;
	*y = console->cursor_y;
	(void)st_rwlock_unlock(&console->state_lock);
	return 0;
}

int
st_console_set_window(st_console_t *console,
	const struct st_console_rect *window)
{
	int result;

	if (console == NULL || window == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (!st_console_rect_valid(console, window))
		result = IO_ERROR_OUT_OF_RANGE;
	else {
		console->window = *window;
		console->cursor_x = window->x;
		console->cursor_y = window->y;
		console->changed = true;
		result = st_console_finish_change_locked(console);
	}
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_get_window(st_console_t *console, struct st_console_rect *window)
{
	int result;

	if (console == NULL || window == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_read_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	*window = console->window;
	(void)st_rwlock_unlock(&console->state_lock);
	return 0;
}

int
st_console_set_wrap(st_console_t *console, bool wrap)
{
	int result;

	if (console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	console->wrap = wrap;
	(void)st_rwlock_unlock(&console->state_lock);
	return 0;
}

static void
fill_line_locked(st_console_t *console, uint32_t y, uint32_t codepoint)
{
	struct st_console_cell blank = st_console_blank_locked(console, codepoint);
	uint32_t x;

	for (x = 0; x < console->window.width; ++x) {
		size_t index = (size_t)y * console->columns
		    + console->window.x + x;

		st_console_set_cell_locked(console, index, &blank);
		/* The pixel ring can expose graphics even when this cell's
		 * contents already equal the fill cell. */
		if (console->drawn_cells != NULL)
			st_console_drawn_at(console, index)->flags |= ST_CONSOLE_CELL_DIRTY;
	}
}

static void
clear_full_surface_scroll_strip_locked(st_console_t *console,
	uint32_t first_row, uint32_t row_count, uint32_t background)
{
	uint32_t text_width;
	uint32_t strip_width;

	if (!console->full_surface_scroll || console->pixels == NULL)
		return;
	text_width = console->columns * console->cell_width;
	if (text_width >= console->pixels->width)
		return;
	strip_width = console->pixels->width - text_width;
	for (uint32_t y = first_row * console->cell_height;
	    y < (first_row + row_count) * console->cell_height; ++y) {
		size_t position = st_console_pixel_position_locked(console,
		    text_width, y);

		for (uint32_t x = 0; x < strip_width; ++x) {
			console->pixels->primary[position + x] = background;
			console->pixels->alternate[position + x] = background;
		}
	}
	console->pixel_surface_dirty = true;
	console->changed = true;
}

static void
clear_window_boundary_pairs_locked(st_console_t *console, uint32_t top,
	uint32_t height)
{
	struct st_console_cell blank;
	uint32_t left = console->window.x;
	uint32_t right = left + console->window.width;

	if ((left == 0 && right == console->columns) || height == 0)
		return;
	blank = st_console_blank_locked(console, ' ');
	for (uint32_t y = top; y < top + height; ++y) {
		size_t row = (size_t)y * console->columns;

		if (left != 0 && (st_console_cell_at(console, row + left)->flags
		    & ST_CONSOLE_CELL_CONTINUATION) != 0)
			st_console_set_cell_locked(console, row + left, &blank);
		if (right != console->columns && (st_console_cell_at(console, row + right)->flags
		    & ST_CONSOLE_CELL_CONTINUATION) != 0)
			st_console_set_cell_locked(console, row + right - 1, &blank);
	}
}

static void
move_row_block_locked(st_console_t *console, uint32_t source_y,
	uint32_t destination_y, uint32_t height)
{
	struct st_console_rect source = {
		.x = console->window.x,
		.y = source_y,
		.width = console->window.width,
		.height = height,
	};
	bool backwards = destination_y > source_y;
	size_t bytes = (size_t)source.width * sizeof(console->cells[0]);
	uint32_t first;
	uint32_t last;

	if (height == 0 || source_y == destination_y)
		return;
	first = source_y < destination_y ? source_y : destination_y;
	last = source_y > destination_y ? source_y : destination_y;
	clear_window_boundary_pairs_locked(console, first,
	    last - first + height);
	st_console_move_raster_locked(console, &source, source.x, destination_y);
	for (uint32_t row_index = 0; row_index < height; ++row_index) {
		uint32_t row = backwards ? height - row_index - 1 : row_index;
		size_t from = (size_t)(source_y + row) * console->columns
		    + source.x;
		size_t to = (size_t)(destination_y + row) * console->columns
		    + source.x;

		memmove(st_console_cell_at(console, to), st_console_cell_at(console, from), bytes);
		for (uint32_t x = 0; x < source.width; ++x)
			st_console_cell_at(console, to + x)->flags |= ST_CONSOLE_CELL_DIRTY;
	}
	console->changed = true;
}

static bool move_full_width_rows_locked(st_console_t *console,
	const struct st_console_rect *source, uint32_t destination_x,
	uint32_t destination_y, const struct st_console_cell *fill);

static void
scroll_locked(st_console_t *console, int32_t lines, uint32_t codepoint)
{
	uint32_t amount;
	uint32_t row;
	uint32_t top = console->window.y;
	uint32_t bottom = top + console->window.height;

	if (lines == 0)
		return;
	if (lines > 0)
		amount = (uint32_t)lines;
	else
		amount = (uint32_t)(-(int64_t)lines);
	if (amount >= console->window.height) {
		for (row = top; row < bottom; ++row)
			fill_line_locked(console, row, codepoint);
		return;
	}
	if (console->window.x == 0 && console->window.width == console->columns) {
		const struct st_console_rect source = {0,
		    lines > 0 ? top + amount : top, console->columns,
		    console->window.height - amount};
		struct st_console_cell blank = st_console_blank_locked(console, codepoint);

		if (move_full_width_rows_locked(console, &source, 0,
		    lines > 0 ? top : top + amount, &blank))
			return;
	}
	if (lines > 0) {
		move_row_block_locked(console, top + amount, top,
		    console->window.height - amount);
		row = bottom - amount;
		for (; row < bottom; ++row)
			fill_line_locked(console, row, codepoint);
	}
	else {
		move_row_block_locked(console, top, top + amount,
		    console->window.height - amount);
		for (row = top; row < top + amount; ++row)
			fill_line_locked(console, row, codepoint);
	}
	if (top == 0 && bottom == console->rows
	    && console->window.x == 0
	    && console->window.width == console->columns) {
		struct st_console_cell blank = st_console_blank_locked(console,
		    codepoint);

		clear_full_surface_scroll_strip_locked(console,
		    lines > 0 ? bottom - amount : top, amount,
		    blank.background);
	}
}

static void
line_feed_locked(st_console_t *console)
{
	uint32_t bottom = console->window.y + console->window.height;

	if (console->cursor_y + 1 < bottom)
		++console->cursor_y;
	else
		scroll_locked(console, 1, ' ');
}

static void
put_codepoint_locked(st_console_t *console, uint32_t codepoint)
{
	struct st_console_cell cell;
	uint8_t columns;
	uint32_t right = console->window.x + console->window.width;

	switch (codepoint) {
	case '\r':
		console->cursor_x = console->window.x;
		return;
	case '\n':
		line_feed_locked(console);
		return;
	case '\b':
		if (console->cursor_x > console->window.x) {
			struct st_console_cell blank;

			--console->cursor_x;
			blank = st_console_blank_locked(console, ' ');
			st_console_set_cell_locked(console,
			    (size_t)console->cursor_y * console->columns
			    + console->cursor_x, &blank);
		}
		return;
	case '\t': {
		uint32_t relative = console->cursor_x - console->window.x;
		uint32_t spaces = 8 - (relative % 8);

		while (spaces-- != 0)
			put_codepoint_locked(console, ' ');
		return;
	}
	default:
		break;
	}
	if (st_unicode_columns(codepoint) == 0
	    && !(st_unicode_nonspacing_mark(codepoint)
	    && !st_unicode_default_ignorable(codepoint)))
		return;
	cell = style_cell_locked(console, codepoint, &columns);
	if (columns == 2 && console->cursor_x + 1 >= right) {
		if (!console->wrap) {
			console->cursor_x = right - 1;
			return;
		}
		console->cursor_x = console->window.x;
		line_feed_locked(console);
	}
	if (columns > right - console->cursor_x)
		return;
	st_console_set_cell_locked(console,
	    (size_t)console->cursor_y * console->columns + console->cursor_x,
	    &cell);
	if (columns < right - console->cursor_x)
		console->cursor_x += columns;
	else if (console->wrap) {
		console->cursor_x = console->window.x;
		line_feed_locked(console);
	}
	else
		console->cursor_x = right - 1;
}

static void
put_cluster_locked(st_console_t *console, const char *text, size_t text_size,
	uint8_t columns)
{
	struct st_console_cell cell;
	uint32_t right = console->window.x + console->window.width;

	if (columns == 0)
		return;
	cell = style_cluster_locked(console, text, text_size, columns);
	{
		const struct st_console_font *font = st_console_font_locked(console,
		    cell.glyph_font);
		if (font != NULL)
			columns = cell.glyph < font->glyph_count ? 1 : 2;
	}
	if (columns == 2 && console->cursor_x + 1 >= right) {
		if (!console->wrap) {
			console->cursor_x = right - 1;
			return;
		}
		console->cursor_x = console->window.x;
		line_feed_locked(console);
	}
	if (columns > right - console->cursor_x)
		return;
	st_console_set_cell_locked(console,
	    (size_t)console->cursor_y * console->columns + console->cursor_x,
	    &cell);
	if (columns < right - console->cursor_x)
		console->cursor_x += columns;
	else if (console->wrap) {
		console->cursor_x = console->window.x;
		line_feed_locked(console);
	}
	else
		console->cursor_x = right - 1;
}

int
st_console_put_codepoint(st_console_t *console, uint32_t codepoint)
{
	int result;

	if (console == NULL || !st_unicode_scalar_valid(codepoint))
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	put_codepoint_locked(console, codepoint);
	console->changed = true;
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_put_decode_error(st_console_t *console)
{
	const struct st_console_font *font;
	struct st_console_cell cell;
	uint32_t right;
	int result;

	if (console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	font = st_console_font_locked(console, 0);
	if (font == NULL) {
		(void)st_rwlock_unlock(&console->state_lock);
		return IO_ERROR_NOT_FOUND;
	}
	cell = (struct st_console_cell){
		.glyph = font->decode_error_glyph,
		.foreground = console->style.foreground,
		.background = console->style.background,
		.flags = console->style.flags & ST_CONSOLE_CELL_PUBLIC_FLAGS,
		.font = console->style.font,
		.glyph_font = 0,
		.hyperlink = console->style.hyperlink,
	};
	right = console->window.x + console->window.width;
	st_console_set_cell_locked(console,
	    (size_t)console->cursor_y * console->columns + console->cursor_x,
	    &cell);
	if (console->cursor_x + 1 < right)
		++console->cursor_x;
	else if (console->wrap) {
		console->cursor_x = console->window.x;
		line_feed_locked(console);
	}
	console->changed = true;
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_write_utf8(st_console_t *console, const char *text, size_t length)
{
	st_unicode_text_t *unicode = NULL;
	struct st_unicode_text_item item;
	size_t consumed = 0;
	int result;

	if (console == NULL || (text == NULL && length != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_unicode_text_create(&unicode, ST_UNICODE_TEXT_GRAPHEMES);
	if (result == 0)
		result = st_unicode_text_feed(unicode, text, length, &consumed);
	if (result == 0 && consumed != length)
		result = IO_ERROR_INVALID_ENCODING;
	if (result == 0)
		result = st_unicode_text_finish(unicode);
	if (result == 0 && (st_unicode_text_state(unicode)
	    & ST_UNICODE_TEXT_SAW_DECODE_ERROR) != 0)
		result = IO_ERROR_INVALID_ENCODING;
	if (result != 0) {
		st_unicode_text_destroy(&unicode);
		return result;
	}
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS) {
		st_unicode_text_destroy(&unicode);
		return IO_ERROR_IO;
	}
	while (st_unicode_text_peek(unicode, &item) == 0) {
		uint32_t codepoint;

		/* UAX #29 groups CRLF into one zero-column grapheme; both
		 * controls still act on the text window rather than being drawn. */
		if (item.size == 2 && item.bytes[0] == '\r' && item.bytes[1] == '\n') {
			put_codepoint_locked(console, '\r');
			put_codepoint_locked(console, '\n');
		}
		else if (st_utf8_decode_one(&codepoint, &consumed, item.bytes,
		    item.size) == 0 && consumed == item.size)
			put_codepoint_locked(console, codepoint);
		else
			put_cluster_locked(console, (const char *)item.bytes,
			    item.size, item.columns);
		(void)st_unicode_text_consume(unicode);
	}
	if (length != 0)
		console->changed = true;
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	st_unicode_text_destroy(&unicode);
	return result;
}

int
st_console_clear_window(st_console_t *console, uint32_t codepoint)
{
	uint32_t y;
	int result;

	if (console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (!fill_codepoint_valid_locked(console, codepoint)) {
		(void)st_rwlock_unlock(&console->state_lock);
		return IO_ERROR_INVALID_ARGUMENT;
	}
	for (y = console->window.y;
	    y < console->window.y + console->window.height; ++y)
		fill_line_locked(console, y, codepoint);
	if (console->cursor_x != console->window.x
	    || console->cursor_y != console->window.y)
		console->changed = true;
	console->cursor_x = console->window.x;
	console->cursor_y = console->window.y;
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_clear_line(st_console_t *console, uint32_t codepoint)
{
	struct st_console_cell blank;
	uint32_t right;
	int result;

	if (console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (!fill_codepoint_valid_locked(console, codepoint)) {
		(void)st_rwlock_unlock(&console->state_lock);
		return IO_ERROR_INVALID_ARGUMENT;
	}
	blank = st_console_blank_locked(console, codepoint);
	right = console->window.x + console->window.width;
	for (uint32_t x = console->cursor_x; x < right; ++x)
		st_console_set_cell_locked(console,
		    (size_t)console->cursor_y * console->columns + x, &blank);
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

static bool
destination_valid(const st_console_t *console,
	const struct st_console_rect *source, uint32_t x, uint32_t y)
{
	return st_console_rect_valid(console, source) && x < console->columns
	    && y < console->rows && source->width <= console->columns - x
	    && source->height <= console->rows - y;
}

static bool
inside(uint32_t x, uint32_t y, uint32_t left, uint32_t top,
	uint32_t width, uint32_t height)
{
	return x >= left && y >= top && x - left < width && y - top < height;
}

static bool
text_cell_wide_locked(st_console_t *console,
	const struct st_console_cell *cell)
{
	uint16_t glyph_font = st_console_cell_glyph_font(cell);
	const struct st_console_font *font = st_console_font_locked(console,
	    glyph_font);
	return font != NULL && cell->glyph >= font->glyph_count
	    && cell->glyph - font->glyph_count < font->wide_glyph_count;
}

static void
normalize_saved_boundaries_locked(st_console_t *console,
	struct st_console_cell *cells, uint32_t width, uint32_t height)
{
	struct st_console_cell blank = st_console_blank_locked(console, ' ');

	for (uint32_t y = 0; y < height; ++y) {
		struct st_console_cell *row = &cells[(size_t)y * width];

		if ((row[0].flags & ST_CONSOLE_CELL_CONTINUATION) != 0)
			row[0] = blank;
		if ((row[width - 1].flags & ST_CONSOLE_CELL_CONTINUATION) == 0
		    && text_cell_wide_locked(console, &row[width - 1]))
			row[width - 1] = blank;
	}
}

static void
copy_row_storage_locked(st_console_t *console, uint32_t from, uint32_t to)
{
	size_t bytes = (size_t)console->columns * sizeof(console->cells[0]);

	memmove(st_console_cell_at(console, (size_t)to * console->columns),
	    st_console_cell_at(console, (size_t)from * console->columns), bytes);
	if (console->drawn_cells != NULL) {
		memmove(st_console_drawn_at(console, (size_t)to * console->columns),
		    st_console_drawn_at(console, (size_t)from * console->columns), bytes);
		console->raster_rows[st_console_row_index(console, to)]
		    = console->raster_rows[st_console_row_index(console, from)];
	}
}

static bool
rotate_rows_locked(st_console_t *console, const struct st_console_rect *source,
	uint32_t destination_y)
{
	bool up = destination_y < source->y;
	uint32_t shift = up ? source->y - destination_y : destination_y - source->y;
	uint32_t first = up ? destination_y : source->y;
	uint32_t end = (up ? source->y : destination_y) + source->height;

	if (shift > source->height || source->height < console->rows / 2)
		return false;
	/* Move pixels, but not metadata: the latter follows the cell origin.
	 * Outside rows need only a small correction copy (e.g. the status row).
	 * Copy in dependency order before rotating, so no scratch buffer or
	 * per-scroll allocation is needed even when the physical rows wrap. */
	st_console_move_pixels_locked(console, source, 0, destination_y);
	if (up) {
		for (uint32_t y = first; y-- > 0;)
			copy_row_storage_locked(console, y, (y + shift) % console->rows);
		for (uint32_t y = console->rows; y-- > end;)
			copy_row_storage_locked(console, y, (y + shift) % console->rows);
		console->cell_top = (console->cell_top + shift) % console->rows;
	} else {
		for (uint32_t y = end; y < console->rows; ++y)
			copy_row_storage_locked(console, y,
			    (y + console->rows - shift) % console->rows);
		for (uint32_t y = 0; y < first; ++y)
			copy_row_storage_locked(console, y,
			    (y + console->rows - shift) % console->rows);
		console->cell_top = (console->cell_top + console->rows - shift)
		    % console->rows;
	}
	return true;
}

static bool
move_full_width_rows_locked(st_console_t *console,
	const struct st_console_rect *source, uint32_t destination_x,
	uint32_t destination_y, const struct st_console_cell *fill)
{
	uint32_t exposed;
	uint32_t fill_y;

	if (source->x != 0 || destination_x != 0
	    || source->width != console->columns)
		return false;
	if (source->y == destination_y)
		return true;
	if (fill == NULL || !rotate_rows_locked(console, source, destination_y)) {
		bool backwards = destination_y > source->y;
		size_t bytes = (size_t)console->columns * sizeof(console->cells[0]);

		st_console_move_raster_locked(console, source, destination_x, destination_y);
		for (uint32_t i = 0; i < source->height; ++i) {
			uint32_t row = backwards ? source->height - i - 1 : i;
			memmove(st_console_cell_at(console,
			    (size_t)(destination_y + row) * console->columns),
			    st_console_cell_at(console,
			    (size_t)(source->y + row) * console->columns), bytes);
		}
	}
	st_console_dirty_rows(console, destination_y, destination_y + source->height);
	if (fill == NULL)
		return true;
	if (destination_y < source->y) {
		exposed = source->y - destination_y;
		if (exposed > source->height)
			exposed = source->height;
		fill_y = source->y + source->height - exposed;
	}
	else {
		exposed = destination_y - source->y;
		if (exposed > source->height)
			exposed = source->height;
		fill_y = source->y;
	}
	for (uint32_t y = fill_y; y < fill_y + exposed; ++y) {
		for (uint32_t x = 0; x < console->columns; ++x) {
			size_t index = (size_t)y * console->columns + x;

			st_console_set_cell_locked(console, index, fill);
			if (console->drawn_cells != NULL)
				st_console_drawn_at(console, index)->flags
				    |= ST_CONSOLE_CELL_DIRTY;
		}
	}
	if ((destination_y == 0 && source->y > 0
	    && source->height == console->rows - source->y)
	    || (source->y == 0 && destination_y > 0
	    && source->height == console->rows - destination_y))
		clear_full_surface_scroll_strip_locked(console, fill_y,
		    exposed, fill->background);
	return true;
}

static int
move_cells(st_console_t *console, const struct st_console_rect *source,
	uint32_t destination_x, uint32_t destination_y,
	const struct st_console_cell *fill)
{
	struct st_console_cell *saved;
	size_t count;
	int result;

	if (console == NULL || source == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (!destination_valid(console, source, destination_x, destination_y)) {
		(void)st_rwlock_unlock(&console->state_lock);
		return IO_ERROR_OUT_OF_RANGE;
	}
	if (fill != NULL) {
		struct st_console_cell value = *fill;

		value.flags &= ST_CONSOLE_CELL_PUBLIC_FLAGS;
		if ((value.flags & ST_CONSOLE_CELL_CONTINUATION) != 0
		    || text_cell_wide_locked(console, &value)) {
			(void)st_rwlock_unlock(&console->state_lock);
			return IO_ERROR_INVALID_ARGUMENT;
		}
	}
	if (move_full_width_rows_locked(console, source, destination_x,
	    destination_y, fill)) {
		result = st_console_finish_change_locked(console);
		(void)st_rwlock_unlock(&console->state_lock);
		return result;
	}
	count = (size_t)source->width * source->height;
	saved = malloc(count * sizeof(saved[0]));
	if (saved == NULL) {
		(void)st_rwlock_unlock(&console->state_lock);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	for (uint32_t y = 0; y < source->height; ++y)
		memcpy(&saved[(size_t)y * source->width],
		    st_console_cell_at(console, (size_t)(source->y + y) * console->columns
		    + source->x), (size_t)source->width * sizeof(saved[0]));
	normalize_saved_boundaries_locked(console, saved, source->width,
	    source->height);
	/* Keep the resolved raster in step with the cells.  The destination
	 * writes below still validate wide-cell boundaries and mark damage, but
	 * cells which survived the move then compare equal to drawn_cells and do
	 * not need to be rendered again. */
	st_console_move_raster_locked(console, source, destination_x,
	    destination_y);
	for (uint32_t y = 0; y < source->height; ++y) {
		for (uint32_t x = 0; x < source->width; ++x) {
			st_console_set_cell_locked(console,
			    (size_t)(destination_y + y) * console->columns
			    + destination_x + x,
			    &saved[(size_t)y * source->width + x]);
		}
	}
	if (fill != NULL) {
		for (uint32_t y = 0; y < source->height; ++y) {
			for (uint32_t x = 0; x < source->width; ++x) {
				uint32_t screen_x = source->x + x;
				uint32_t screen_y = source->y + y;

				if (!inside(screen_x, screen_y, destination_x,
				    destination_y, source->width, source->height)) {
					size_t index = (size_t)screen_y
					    * console->columns + screen_x;

					st_console_set_cell_locked(console, index, fill);
					if (console->drawn_cells != NULL)
						st_console_drawn_at(console, index)->flags
						    |= ST_CONSOLE_CELL_DIRTY;
				}
			}
		}
	}
	free(saved);
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_move_cells(st_console_t *console,
	const struct st_console_rect *source, uint32_t destination_x,
	uint32_t destination_y)
{
	return move_cells(console, source, destination_x, destination_y, NULL);
}

int
st_console_move_cells_clear(st_console_t *console,
	const struct st_console_rect *source, uint32_t destination_x,
	uint32_t destination_y, const struct st_console_cell *fill)
{
	if (fill == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return move_cells(console, source, destination_x, destination_y, fill);
}

int
st_console_scroll(st_console_t *console, int32_t lines, uint32_t codepoint)
{
	int result;

	if (console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (!fill_codepoint_valid_locked(console, codepoint)) {
		(void)st_rwlock_unlock(&console->state_lock);
		return IO_ERROR_INVALID_ARGUMENT;
	}
	scroll_locked(console, lines, codepoint);
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

static void
move_lines_locked(st_console_t *console, uint32_t count, bool insert,
	uint32_t codepoint)
{
	uint32_t bottom = console->window.y + console->window.height;
	uint32_t remaining = bottom - console->cursor_y;
	uint32_t row;

	if (count > remaining)
		count = remaining;
	if (insert) {
		move_row_block_locked(console, console->cursor_y,
		    console->cursor_y + count, remaining - count);
		for (row = console->cursor_y;
		    row < console->cursor_y + count; ++row)
			fill_line_locked(console, row, codepoint);
	}
	else {
		move_row_block_locked(console, console->cursor_y + count,
		    console->cursor_y, remaining - count);
		row = bottom - count;
		for (; row < bottom; ++row)
			fill_line_locked(console, row, codepoint);
	}
}

static int
change_lines(st_console_t *console, uint32_t count, uint32_t codepoint,
	bool insert)
{
	int result;

	if (console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (!fill_codepoint_valid_locked(console, codepoint)) {
		(void)st_rwlock_unlock(&console->state_lock);
		return IO_ERROR_INVALID_ARGUMENT;
	}
	move_lines_locked(console, count, insert, codepoint);
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
	return result;
}

int
st_console_insert_lines(st_console_t *console, uint32_t count,
	uint32_t codepoint)
{
	return change_lines(console, count, codepoint, true);
}

int
st_console_delete_lines(st_console_t *console, uint32_t count,
	uint32_t codepoint)
{
	return change_lines(console, count, codepoint, false);
}
