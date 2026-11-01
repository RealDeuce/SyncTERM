/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#ifndef ST_CONSOLE_COLOR_RLL_H
#define ST_CONSOLE_COLOR_RLL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline bool
st_console_color_rll_stream_valid(const uint8_t *data, size_t size, size_t pixels)
{
	size_t offset = 0;

	while (offset < size && pixels != 0) {
		size_t bytes = data[offset] == 0 ? 2 : 4;
		if (size - offset < bytes)
			return false;
		size_t count = data[offset] == 0 ? (size_t)data[offset + 1] + 1 : 1;
		if (count > pixels)
			return false;
		pixels -= count;
		offset += bytes;
	}
	return pixels == 0 && offset == size;
}

static inline size_t
st_console_color_rll_half_offset(const uint8_t *data)
{
	return (size_t)data[0] | ((size_t)data[1] << 8);
}

static inline bool
st_console_color_rll_valid(const uint8_t *data, size_t size, size_t width, size_t height)
{
	if (size < 2)
		return false;
	size_t offset = st_console_color_rll_half_offset(data);
	return offset >= 2 && offset <= size
	    && st_console_color_rll_stream_valid(data + 2, offset - 2, width / 2 * height)
	    && st_console_color_rll_stream_valid(data + offset, size - offset,
	    (width - width / 2) * height);
}

/* Registration validates both streams. Seek only moves forward; callers save
 * the cursors before a row to replay it when scaling repeats rows. */
struct st_console_color_rll_cursor {
	const uint8_t *next;
	const uint8_t *pixel;
	size_t end;
};

static inline const uint8_t *
st_console_color_rll_seek(struct st_console_color_rll_cursor *cursor, size_t index)
{
	while (index >= cursor->end) {
		const uint8_t *token = cursor->next;
		if (token[0] == 0) {
			cursor->end += (size_t)token[1] + 1;
			cursor->next += 2;
			cursor->pixel = NULL;
		}
		else {
			++cursor->end;
			cursor->next += 4;
			cursor->pixel = token;
		}
	}
	return cursor->pixel;
}

#endif
