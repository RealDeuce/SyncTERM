/* Private native return-boundary controls; never linked into the application. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_WREN_SCREEN_TEST_CALLS_H
#define ST_WREN_SCREEN_TEST_CALLS_H
#include "wren_host_internal.h"
#include <string.h>
#include "st_wren_screen_test_control.h"

static inline int
test_st_console_size(st_console_t *console, uint32_t *columns,
	uint32_t *rows)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_size") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_size(console, columns, rows);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_cell_pixel_size(st_console_t *console, uint32_t *width,
	uint32_t *height)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_cell_pixel_size") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_cell_pixel_size(console, width, height);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_capabilities(st_console_t *console, uint32_t *capabilities)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_capabilities") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_capabilities(console, capabilities);
	if (status == 0 && st_wren_test_no_pixels) *capabilities &= ~ST_CONSOLE_CAP_PIXELS;
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_get_style(st_console_t *console,
	struct st_console_style *style)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_get_style") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_get_style(console, style);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_set_style(st_console_t *console,
	const struct st_console_style *style)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_set_style") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_set_style(console, style);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_get_position(st_console_t *console, uint32_t *x,
	uint32_t *y)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_get_position") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_get_position(console, x, y);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_set_position(st_console_t *console, uint32_t x, uint32_t y)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_set_position") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_set_position(console, x, y);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_get_window(st_console_t *console,
	struct st_console_rect *window)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_get_window") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_get_window(console, window);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_set_window(st_console_t *console,
	const struct st_console_rect *window)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_set_window") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_set_window(console, window);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_put_codepoint(st_console_t *console, uint32_t codepoint)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_put_codepoint") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_put_codepoint(console, codepoint);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_write_utf8(st_console_t *console, const char *text,
	size_t length)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_write_utf8") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_write_utf8(console, text, length);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_clear_window(st_console_t *console, uint32_t codepoint)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_clear_window") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_clear_window(console, codepoint);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_clear_line(st_console_t *console, uint32_t codepoint)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_clear_line") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_clear_line(console, codepoint);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_delete_lines(st_console_t *console, uint32_t count,
	uint32_t codepoint)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_delete_lines") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_delete_lines(console, count, codepoint);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_insert_lines(st_console_t *console, uint32_t count,
	uint32_t codepoint)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_insert_lines") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_insert_lines(console, count, codepoint);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_scroll(st_console_t *console, int32_t lines,
	uint32_t codepoint)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_scroll") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_scroll(console, lines, codepoint);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_get_palette(st_console_t *console, uint16_t first,
	uint32_t *rgb, uint32_t count)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_get_palette") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_get_palette(console, first, rgb, count);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_set_palette(st_console_t *console, uint16_t first,
	const uint32_t *rgb, uint32_t count)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_set_palette") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_set_palette(console, first, rgb, count);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_get_custom_cursor(st_console_t *console,
	struct st_console_custom_cursor *cursor)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_get_custom_cursor") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_get_custom_cursor(console, cursor);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_get_default_cursor(st_console_t *console,
	struct st_console_custom_cursor *cursor)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_get_default_cursor") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_get_default_cursor(console, cursor);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_set_custom_cursor(st_console_t *console,
	const struct st_console_custom_cursor *cursor)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_set_custom_cursor") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_set_custom_cursor(console, cursor);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_get_video_flags(st_console_t *console, uint32_t *flags)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_get_video_flags") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_get_video_flags(console, flags);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_set_video_flags(st_console_t *console, uint32_t flags)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_set_video_flags") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_set_video_flags(console, flags);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_pixel_size(st_console_t *console, uint32_t *width,
	uint32_t *height)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_pixel_size") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_pixel_size(console, width, height);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_get_pixels(struct st_pixel_buffer **result,
	st_console_t *console, const struct st_console_rect *source,
	bool force_text)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_get_pixels") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_get_pixels(result, console, source, force_text);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_set_pixel(st_console_t *console, uint32_t x, uint32_t y,
	uint32_t colour)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_set_pixel") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_set_pixel(console, x, y, colour);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_blit_pixels(st_console_t *console,
	const struct st_pixel_buffer *source, const struct st_pixel_mask *mask,
	const struct st_pixel_blit *blit)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_blit_pixels") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_blit_pixels(console, source, mask, blit);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_snapshot_create(st_console_snapshot_t **result,
	st_console_t *console)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_snapshot_create") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_snapshot_create(result, console);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_snapshot_restore(st_console_t *console,
	const st_console_snapshot_t *snapshot, bool *restored)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_snapshot_restore") == 0;
	if (restored != NULL) *restored = false;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_snapshot_restore(console, snapshot, restored);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_begin_update(st_console_t *console)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_begin_update") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_begin_update(console);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_end_update(st_console_t *console)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_end_update") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_end_update(console);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_get_clipboard_text(st_console_t *console, char **result)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_get_clipboard_text") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_get_clipboard_text(console, result);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_set_clipboard_text(st_console_t *console, const char *text)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_set_clipboard_text") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_set_clipboard_text(console, text);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_set_pointer_visible(st_console_t *console, bool visible)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_set_pointer_visible") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_set_pointer_visible(console, visible);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_set_pointer_shape(st_console_t *console,
	enum st_console_pointer_shape shape)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_set_pointer_shape") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_set_pointer_shape(console, shape);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_set_title(st_console_t *console, const char *title)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_set_title") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_set_title(console, title);
	return matched && status == 0 ? fault->status : status;
}

static inline int
test_st_console_open_url(st_console_t *console, const char *url)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_open_url") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_open_url(console, url);
	return matched && status == 0 ? fault->status : status;
}
static inline int
test_st_console_read_cells(st_console_t *console,
	const struct st_console_rect *source, struct st_console_cell *cells,
	size_t stride)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_read_cells") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_read_cells(console, source, cells, stride);
	return matched && status == 0 ? fault->status : status;
}
#define st_console_read_cells test_st_console_read_cells
static inline int
test_st_console_write_complete_cells(st_console_t *console,
	const struct st_console_rect *destination,
	const struct st_console_cell *cells, size_t stride)
{
	struct st_wren_screen_fault *fault = &st_wren_test_screen_fault;
	bool matched = fault->name != NULL && strcmp(fault->name, "st_console_write_complete_cells") == 0;
	if (matched) {
		++fault->calls;
		if (!fault->after) return fault->status;
	}
	int status = st_console_write_complete_cells(console, destination, cells, stride);
	return matched && status == 0 ? fault->status : status;
}
#define st_console_write_complete_cells test_st_console_write_complete_cells
#define st_console_size test_st_console_size
#define st_console_cell_pixel_size test_st_console_cell_pixel_size
#define st_console_capabilities test_st_console_capabilities
#define st_console_get_style test_st_console_get_style
#define st_console_set_style test_st_console_set_style
#define st_console_get_position test_st_console_get_position
#define st_console_set_position test_st_console_set_position
#define st_console_get_window test_st_console_get_window
#define st_console_set_window test_st_console_set_window
#define st_console_put_codepoint test_st_console_put_codepoint
#define st_console_write_utf8 test_st_console_write_utf8
#define st_console_clear_window test_st_console_clear_window
#define st_console_clear_line test_st_console_clear_line
#define st_console_delete_lines test_st_console_delete_lines
#define st_console_insert_lines test_st_console_insert_lines
#define st_console_scroll test_st_console_scroll
#define st_console_get_palette test_st_console_get_palette
#define st_console_set_palette test_st_console_set_palette
#define st_console_get_custom_cursor test_st_console_get_custom_cursor
#define st_console_get_default_cursor test_st_console_get_default_cursor
#define st_console_set_custom_cursor test_st_console_set_custom_cursor
#define st_console_get_video_flags test_st_console_get_video_flags
#define st_console_set_video_flags test_st_console_set_video_flags
#define st_console_pixel_size test_st_console_pixel_size
#define st_console_get_pixels test_st_console_get_pixels
#define st_console_set_pixel test_st_console_set_pixel
#define st_console_blit_pixels test_st_console_blit_pixels
#define st_console_snapshot_create test_st_console_snapshot_create
#define st_console_snapshot_restore test_st_console_snapshot_restore
#define st_console_begin_update test_st_console_begin_update
#define st_console_end_update test_st_console_end_update
#define st_console_get_clipboard_text test_st_console_get_clipboard_text
#define st_console_set_clipboard_text test_st_console_set_clipboard_text
#define st_console_set_pointer_visible test_st_console_set_pointer_visible
#define st_console_set_pointer_shape test_st_console_set_pointer_shape
#define st_console_set_title test_st_console_set_title
#define st_console_open_url test_st_console_open_url

#endif
