/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_internal.h"

#include "io_error.h"
#include "st_utf8.h"

#include <stdlib.h>
#include <string.h>

struct st_console_cell
st_console_blank_locked(const st_console_t *console, uint32_t codepoint)
{
	struct st_console_cell cell;
	uint32_t glyph = codepoint;
	uint16_t glyph_font = console->style.font;
	uint8_t columns;

	(void)st_console_font_lookup_locked((st_console_t *)console,
	    console->style.font, codepoint, &glyph_font, &glyph, &columns);
	cell = (struct st_console_cell){
		.codepoint = codepoint,
		.glyph = glyph,
		.foreground = console->style.foreground,
		.background = console->style.background,
		.flags = console->style.flags & ST_CONSOLE_CELL_PUBLIC_FLAGS,
		.font = console->style.font,
		.hyperlink = console->style.hyperlink,
	};
	cell.flags &= ~ST_CONSOLE_CELL_FALLBACK_FONT;
	if (glyph_font != cell.font)
		cell.flags |= ST_CONSOLE_CELL_FALLBACK_FONT;
	return cell;
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
	console->style.flags &= ST_CONSOLE_CELL_PUBLIC_FLAGS
	    & ~ST_CONSOLE_CELL_FALLBACK_FONT;
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
	cursor.blink = true;
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
		cursor->blink = true;
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

	for (x = 0; x < console->window.width; ++x)
		st_console_set_cell_locked(console,
		    (size_t)y * console->columns + console->window.x + x, &blank);
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

		if (left != 0 && (console->cells[row + left].flags
		    & ST_CONSOLE_CELL_CONTINUATION) != 0)
			st_console_set_cell_locked(console, row + left, &blank);
		if (right != console->columns && (console->cells[row + right].flags
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

		memmove(&console->cells[to], &console->cells[from], bytes);
		for (uint32_t x = 0; x < source.width; ++x)
			console->cells[to + x].flags |= ST_CONSOLE_CELL_DIRTY;
	}
	console->changed = true;
}

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
	uint16_t glyph_font;
	uint8_t columns = 1;
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
	cell = st_console_blank_locked(console, codepoint);
	glyph_font = console->style.font;
	(void)st_console_font_lookup_locked(console, cell.font, codepoint,
	    &glyph_font, &cell.glyph, &columns);
	cell.flags &= ~ST_CONSOLE_CELL_FALLBACK_FONT;
	if (glyph_font != cell.font)
		cell.flags |= ST_CONSOLE_CELL_FALLBACK_FONT;
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

	if (console == NULL)
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
st_console_write_utf8(st_console_t *console, const char *text, size_t length)
{
	const unsigned char *bytes = (const unsigned char *)text;
	size_t offset = 0;
	uint32_t codepoint;
	int result;

	if (console == NULL || (text == NULL && length != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	while (offset < length) {
		size_t consumed;

		result = st_utf8_decode_one(&codepoint, &consumed, bytes + offset,
		    length - offset);
		if (result != 0)
			return result;
		offset += consumed;
	}
	result = st_rwlock_write_lock(&console->state_lock);
	if (result != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	offset = 0;
	while (offset < length) {
		size_t consumed;

		(void)st_utf8_decode_one(&codepoint, &consumed, bytes + offset,
		    length - offset);
		offset += consumed;
		put_codepoint_locked(console, codepoint);
	}
	if (length != 0)
		console->changed = true;
	result = st_console_finish_change_locked(console);
	(void)st_rwlock_unlock(&console->state_lock);
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
	const uint8_t *bitmap;
	uint32_t width;
	size_t row_stride;

	return font != NULL && st_console_font_glyph_locked(console,
	    glyph_font, cell->glyph, &bitmap, &width, &row_stride)
	    && width == font->width * 2;
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
		uint16_t glyph_font = value.font;
		uint8_t columns;

		value.flags &= ST_CONSOLE_CELL_PUBLIC_FLAGS;
		if ((value.flags & ST_CONSOLE_CELL_EXPLICIT_GLYPH) == 0) {
			value.flags &= ~ST_CONSOLE_CELL_FALLBACK_FONT;
			(void)st_console_font_lookup_locked(console, value.font,
			    value.codepoint, &glyph_font, &value.glyph,
			    &columns);
			if (glyph_font != value.font)
				value.flags |= ST_CONSOLE_CELL_FALLBACK_FONT;
		}
		if ((value.flags & ST_CONSOLE_CELL_CONTINUATION) != 0
		    || text_cell_wide_locked(console, &value)) {
			(void)st_rwlock_unlock(&console->state_lock);
			return IO_ERROR_INVALID_ARGUMENT;
		}
	}
	count = (size_t)source->width * source->height;
	saved = malloc(count * sizeof(saved[0]));
	if (saved == NULL) {
		(void)st_rwlock_unlock(&console->state_lock);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	for (uint32_t y = 0; y < source->height; ++y)
		memcpy(&saved[(size_t)y * source->width],
		    &console->cells[(size_t)(source->y + y) * console->columns
		    + source->x], (size_t)source->width * sizeof(saved[0]));
	normalize_saved_boundaries_locked(console, saved, source->width,
	    source->height);
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
				    destination_y, source->width, source->height))
					st_console_set_cell_locked(console,
					    (size_t)screen_y * console->columns
					    + screen_x, fill);
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
