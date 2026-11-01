/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_internal.h"
#include "console_color_rll.h"

#include "io_error.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum row_scale {
	ROW_SCALE_NORMAL,
	ROW_SCALE_TOP,
	ROW_SCALE_BOTTOM,
};

size_t
st_console_pixel_position_locked(const st_console_t *console,
	uint32_t x, uint32_t y)
{
	uint32_t text_width = console->columns * console->cell_width;
	uint32_t text_height = console->rows * console->cell_height;
	uint32_t physical_y = y;

	if (console->full_surface_scroll
	    || (x < text_width && y < text_height)) {
		uint32_t height = console->full_surface_scroll
		    ? console->pixels->height : text_height;

		physical_y = y >= height - console->pixel_top
		    ? y - (height - console->pixel_top)
		    : console->pixel_top + y;
	}
	return (size_t)physical_y * console->pixels->width + x;
}

void
st_console_copy_pixels_locked(const st_console_t *console,
	uint32_t *destination, uint32_t destination_stride, bool alternate,
	const struct st_console_rect *rectangle)
{
	const uint32_t *plane = alternate && console->pixels->alternate != NULL
	    ? console->pixels->alternate : console->pixels->primary;
	uint32_t text_width = console->columns * console->cell_width;
	uint32_t text_height = console->rows * console->cell_height;

	for (uint32_t y = 0; y < rectangle->height; ++y) {
		uint32_t logical_y = rectangle->y + y;
		if (console->full_surface_scroll) {
			size_t from = st_console_pixel_position_locked(console,
			    rectangle->x, logical_y);

			memcpy(&destination[(size_t)y * destination_stride],
			    &plane[from], (size_t)rectangle->width
			    * sizeof(plane[0]));
			continue;
		}
		uint32_t text_part = rectangle->x < text_width
		    && logical_y < text_height
		    ? rectangle->width < text_width - rectangle->x
		    ? rectangle->width : text_width - rectangle->x : 0;
		size_t to = (size_t)y * destination_stride;

		if (text_part != 0) {
			size_t from = st_console_pixel_position_locked(console,
			    rectangle->x, logical_y);

			memcpy(&destination[to], &plane[from],
			    (size_t)text_part * sizeof(plane[0]));
		}
		if (text_part < rectangle->width) {
			size_t from = (size_t)logical_y * console->pixels->width
			    + rectangle->x + text_part;

			memcpy(&destination[to + text_part], &plane[from],
			    (size_t)(rectangle->width - text_part)
			    * sizeof(plane[0]));
		}
	}
}

int
st_console_normalize_raster_locked(st_console_t *console)
{
	uint32_t width;
	uint32_t height;
	size_t count;
	uint32_t *primary;
	uint32_t *alternate;

	if (console->pixels == NULL || console->pixel_top == 0)
		return 0;
	width = console->full_surface_scroll ? console->pixels->width
	    : console->columns * console->cell_width;
	height = console->full_surface_scroll ? console->pixels->height
	    : console->rows * console->cell_height;
	count = (size_t)width * height;
	primary = malloc(count * sizeof(primary[0]));
	alternate = malloc(count * sizeof(alternate[0]));
	if (primary == NULL || alternate == NULL) {
		free(alternate);
		free(primary);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	for (uint32_t y = 0; y < height; ++y) {
		size_t from = st_console_pixel_position_locked(console, 0, y);
		size_t to = (size_t)y * width;

		memcpy(&primary[to], &console->pixels->primary[from],
		    (size_t)width * sizeof(primary[0]));
		memcpy(&alternate[to], &console->pixels->alternate[from],
		    (size_t)width * sizeof(alternate[0]));
	}
	console->pixel_top = 0;
	for (uint32_t y = 0; y < height; ++y) {
		size_t from = (size_t)y * width;
		size_t to = (size_t)y * console->pixels->width;

		memcpy(&console->pixels->primary[to], &primary[from],
		    (size_t)width * sizeof(primary[0]));
		memcpy(&console->pixels->alternate[to], &alternate[from],
		    (size_t)width * sizeof(alternate[0]));
	}
	free(alternate);
	free(primary);
	return 0;
}

int
st_console_set_full_surface_scroll(st_console_t *console, bool enabled)
{
	int status;

	if (console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rwlock_write_lock(&console->state_lock);
	if (status != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (console->pixels == NULL)
		status = IO_ERROR_NOT_SUPPORTED;
	else if (console->full_surface_scroll == enabled)
		status = 0;
	else {
		status = st_console_normalize_raster_locked(console);
		if (status == 0)
			console->full_surface_scroll = enabled;
	}
	(void)st_rwlock_unlock(&console->state_lock);
	return status;
}

struct font_cache {
	const struct st_console_font *font;
	uint16_t identifier;
	bool valid;
};

#define FAST_CELL_COUNT 128U

struct fast_cell {
	const uint8_t *glyph;
	size_t row_stride;
	uint32_t foreground;
	uint32_t background;
	uint32_t flags;
	bool hyperlink;
	bool hidden;
};

static const struct st_console_font *
cached_font(st_console_t *console, struct font_cache *cache,
	uint16_t identifier)
{
	if (!cache->valid || cache->identifier != identifier) {
		cache->font = st_console_font_locked(console, identifier);
		cache->identifier = identifier;
		cache->valid = true;
	}
	return cache->font;
}

static bool
prepare_fast_cell(st_console_t *console, const struct st_console_cell *cell,
	struct font_cache *font_cache, struct fast_cell *state)
{
	struct st_console_cell effective;

	st_console_effective_cell_locked(console, cell, &effective);
	cell = &effective;
	uint16_t glyph_font = st_console_cell_glyph_font(cell);
	const struct st_console_font *font = cached_font(console, font_cache,
	    glyph_font);
	const uint8_t *glyph;
	uint32_t glyph_width;
	size_t row_stride;
	uint32_t foreground = cell->foreground;
	uint32_t background = cell->background;

	if (font == NULL || !st_console_font_glyph(font,
	    cell->glyph, &glyph, &glyph_width, &row_stride)
	    || (console->color_emoji
	    && st_console_font_color_glyph(font, cell->glyph) != NULL)
	    || glyph_width != 8 || font->height != console->cell_height
	    || console->cell_width != 8
	    || (cell->flags & (ST_CONSOLE_CELL_SEPARATED
	    | ST_CONSOLE_CELL_CONTINUATION)) != 0)
		return false;
	if ((cell->flags & ST_CONSOLE_CELL_REVERSE) != 0) {
		uint32_t swap = foreground;

		foreground = background;
		background = swap;
	}
	state->glyph = glyph;
	state->row_stride = row_stride;
	state->foreground = foreground;
	state->background = background;
	state->flags = cell->flags;
	state->hyperlink = cell->hyperlink != 0;
	state->hidden = (cell->flags & ST_CONSOLE_CELL_CONCEAL) != 0
	    && (cell->flags & ST_CONSOLE_CELL_REVEAL) == 0;
	return true;
}

static size_t
render_fast_cells(st_console_t *console, uint32_t x, uint32_t y,
	size_t count, struct font_cache *font_cache)
{
	struct fast_cell states[FAST_CELL_COUNT];
	size_t prepared = 0;

	if (count > FAST_CELL_COUNT)
		count = FAST_CELL_COUNT;
	while (prepared < count) {
		const struct st_console_cell *cell = st_console_cell_at(console,
		    (size_t)y * console->columns + x + prepared);

		if (!prepare_fast_cell(console, cell, font_cache,
		    &states[prepared]))
			break;
		++prepared;
	}
	if (prepared == 0)
		return 0;
	for (uint32_t py = 0; py < console->cell_height; ++py) {
		size_t pixel = st_console_pixel_position_locked(console,
		    x * console->cell_width, y * console->cell_height + py);
		bool last = py + 1 == console->cell_height;

		for (size_t i = 0; i < prepared; ++i) {
			const struct fast_cell *state = &states[i];
			uint8_t bits = state->hidden ? 0
			    : state->glyph[(size_t)py * state->row_stride];
			bool blink = (state->flags & ST_CONSOLE_CELL_BLINK) != 0;

			if ((state->flags & ST_CONSOLE_CELL_BOLD) != 0)
				bits |= bits >> 1;
			if (!state->hidden && last) {
				if ((state->flags & ST_CONSOLE_CELL_UNDERLINE) != 0)
					bits = UINT8_MAX;
				if (state->hyperlink)
					bits |= UINT8_C(0x88);
			}
			for (uint32_t px = 0; px < 8; ++px) {
				bool set = (bits & (UINT8_C(0x80) >> px)) != 0;

				console->pixels->primary[pixel] = set
				    ? state->foreground : state->background;
				console->pixels->alternate[pixel] = set && !blink
				    ? state->foreground : state->background;
				++pixel;
			}
		}
	}
	for (size_t i = 0; i < prepared; ++i) {
		size_t index = (size_t)y * console->columns + x + i;

		(*st_console_drawn_at(console, index)) = (*st_console_cell_at(console, index));
		st_console_drawn_at(console, index)->flags &= ~ST_CONSOLE_CELL_DIRTY;
		st_console_cell_at(console, index)->flags |= ST_CONSOLE_CELL_DIRTY;
	}
	return prepared;
}

static bool
row_bit(const uint8_t *row, uint32_t x)
{
	return (row[x / 8] & (uint8_t)(UINT8_C(0x80) >> (x % 8))) != 0;
}

static uint32_t
composite_rgb(st_console_t *console, const uint8_t *channels, uint32_t alpha,
	uint32_t background)
{
	uint32_t rgb;
	uint32_t result = ST_CONSOLE_COLOR_RGB_TAG;

	if (alpha == 0)
		return background;
	rgb = ST_CONSOLE_COLOR_IS_RGB(background) ? background
	    : background <= UINT16_MAX ? console->palette[background] : 0;
	for (unsigned channel = 0; channel < 3; ++channel) {
		unsigned shift = (2 - channel) * 8;
		uint32_t value = (channels[channel] * alpha
		    + ((rgb >> shift) & 255) * (255 - alpha) + 127) / 255;

		result |= value << shift;
	}
	return result;
}

/* Traverse the packed source by columns, matching its storage order. Keep
 * raw RGBA on the existing row-major path so it remains a useful baseline. */
static void
render_color_rll(st_console_t *console, uint32_t x, uint32_t y,
	const struct st_console_cell *source, const struct st_console_font *font,
	const struct st_console_color_glyph *color, uint32_t glyph_width,
	uint32_t glyph_x, uint32_t first_y, uint32_t y_span, bool expanded,
	bool repeat_last, uint32_t foreground, uint32_t background)
{
	uint32_t width = color->width == 0 ? glyph_width : color->width;
	size_t halfway = (size_t)(width / 2) * font->height;
	struct st_console_color_rll_cursor cursor = {.next = color->rgba + 2};
	struct st_console_color_rll_cursor column_cursor = cursor;
	uint32_t previous_x = UINT32_MAX;
	bool right = false;

	for (uint32_t px = 0; px < console->cell_width; ++px) {
		uint32_t font_x = glyph_x + (expanded
		    ? (px < font->width ? px : font->width - 1)
		    : (uint32_t)((uint64_t)px * font->width / console->cell_width));
		uint32_t color_x = color->width == 0 ? font_x
		    : (uint32_t)((uint64_t)font_x * width / glyph_width);
		bool gap = expanded && px == font->width && !repeat_last;
		uint32_t font_y = first_y, accumulator = 0;
		uint32_t step = y_span / console->cell_height;
		uint32_t remainder = y_span % console->cell_height;

		if (!right && color_x >= width / 2) {
			cursor = (struct st_console_color_rll_cursor){
				.next = color->rgba + st_console_color_rll_half_offset(color->rgba),
				.end = halfway,
			};
			right = true;
		}
		if (color_x == previous_x)
			cursor = column_cursor;
		else
			column_cursor = cursor;
		previous_x = color_x;
		for (uint32_t py = 0; py < console->cell_height; ++py) {
			size_t pixel = st_console_pixel_position_locked(console,
			    x * console->cell_width + px, y * console->cell_height + py);
			uint32_t value = background;

			if (!gap && font_y < font->height) {
				const uint8_t *argb = st_console_color_rll_seek(&cursor,
				    (size_t)color_x * font->height + font_y);
				if (argb != NULL)
					value = composite_rgb(console, argb + 1, argb[0], background);
			}
			if (py + 1 == console->cell_height
			    && (((source->flags & ST_CONSOLE_CELL_UNDERLINE) != 0)
			    || (source->hyperlink != 0 && (px & 3U) == 0)))
				value = foreground;
			console->pixels->primary[pixel] = value;
			console->pixels->alternate[pixel]
			    = (source->flags & ST_CONSOLE_CELL_BLINK) == 0 ? value : background;
			font_y += step;
			accumulator += remainder;
			if (accumulator >= console->cell_height) {
				accumulator -= console->cell_height;
				++font_y;
			}
		}
	}
}

static void
render_cell(st_console_t *console, uint32_t x, uint32_t y,
	enum row_scale row_scale, struct font_cache *font_cache)
{
	size_t index = (size_t)y * console->columns + x;
	const struct st_console_cell *cell = st_console_cell_at(console, index);
	const struct st_console_cell *source = cell;
	struct st_console_cell effective;
	const struct st_console_font *font;
	const uint8_t *glyph = NULL;
	const struct st_console_color_glyph *color = NULL;
	uint32_t glyph_width = 0;
	uint32_t glyph_x = 0;
	size_t glyph_row_stride = 0;
	uint32_t foreground;
	uint32_t background;
	bool draw_glyph = true;
	bool hidden;
	uint16_t glyph_font;

	if (row_scale == ROW_SCALE_BOTTOM) {
		if ((cell->flags & ST_CONSOLE_CELL_TERMINAL_FONT) != 0)
			source = st_console_cell_at(console, (size_t)(y - 1)
			    * console->columns + x);
		if ((source->flags & ST_CONSOLE_CELL_DOUBLE_HEIGHT) == 0)
			draw_glyph = false;
	}
	st_console_effective_cell_locked(console, source, &effective);
	source = &effective;
	glyph_font = st_console_cell_glyph_font(source);
	font = draw_glyph ? cached_font(console, font_cache, glyph_font) : NULL;
	if (font != NULL && !st_console_font_glyph(font,
	    source->glyph, &glyph, &glyph_width,
	    &glyph_row_stride))
		font = NULL;
	if (font != NULL && glyph_width == font->width * 2
	    && (source->flags & ST_CONSOLE_CELL_CONTINUATION) != 0)
		glyph_x = font->width;
	if (console->color_emoji && font != NULL)
		color = st_console_font_color_glyph(font, source->glyph);
	foreground = source->foreground;
	background = source->background;
	if ((source->flags & ST_CONSOLE_CELL_REVERSE) != 0) {
		uint32_t swap = foreground;

		foreground = background;
		background = swap;
	}
	hidden = !draw_glyph || ((source->flags & ST_CONSOLE_CELL_CONCEAL) != 0
	    && (source->flags & ST_CONSOLE_CELL_REVEAL) == 0);
	if (hidden) {
		for (uint32_t py = 0; py < console->cell_height; ++py) {
			size_t pixel = st_console_pixel_position_locked(console,
			    x * console->cell_width, y * console->cell_height + py);

			for (uint32_t px = 0; px < console->cell_width; ++px) {
				console->pixels->primary[pixel + px] = background;
				console->pixels->alternate[pixel + px] = background;
			}
		}
		goto finished;
	}
	if (font == NULL) {
		for (uint32_t py = 0; py < console->cell_height; ++py) {
			size_t pixel = st_console_pixel_position_locked(console,
			    x * console->cell_width, y * console->cell_height + py);

			for (uint32_t px = 0; px < console->cell_width; ++px) {
				bool set = py + 1 == console->cell_height
				    && (((source->flags
				    & ST_CONSOLE_CELL_UNDERLINE) != 0)
				    || (source->hyperlink != 0 && (px & 3U) == 0));

				console->pixels->primary[pixel + px]
				    = set ? foreground : background;
				console->pixels->alternate[pixel + px]
				    = set
				    && (source->flags & ST_CONSOLE_CELL_BLINK) == 0
				    ? foreground : background;
			}
		}
		goto finished;
	}
	if (color == NULL && row_scale == ROW_SCALE_NORMAL && font->width == 8
	    && glyph_width == 8
	    && console->cell_width == 8 && font->height == console->cell_height
	    && (source->flags & ST_CONSOLE_CELL_SEPARATED) == 0) {
		bool blink = (source->flags & ST_CONSOLE_CELL_BLINK) != 0;

		for (uint32_t py = 0; py < console->cell_height; ++py) {
			uint8_t bits = glyph[(size_t)py * glyph_row_stride];
			size_t pixel = st_console_pixel_position_locked(console,
			    x * console->cell_width, y * console->cell_height + py);

			if ((source->flags & ST_CONSOLE_CELL_BOLD) != 0)
				bits |= bits >> 1;
			if (py + 1 == console->cell_height) {
				if ((source->flags & ST_CONSOLE_CELL_UNDERLINE) != 0)
					bits = UINT8_MAX;
				if (source->hyperlink != 0)
					bits |= UINT8_C(0x88);
			}
			for (uint32_t px = 0; px < 8; ++px) {
				bool set = (bits & (UINT8_C(0x80) >> px)) != 0;

				console->pixels->primary[pixel + px]
				    = set ? foreground : background;
				console->pixels->alternate[pixel + px]
				    = set && !blink ? foreground : background;
			}
		}
		goto finished;
	}
	{
		uint32_t font_y;
		uint32_t y_span;
		uint32_t y_accumulator = 0;
		uint32_t y_step;
		uint32_t y_remainder;
		uint32_t x_step = font->width / console->cell_width;
		uint32_t x_remainder = font->width % console->cell_width;
		bool expanded = (console->video_flags & ST_CONSOLE_VIDEO_EXPAND) != 0
		    && font->width < UINT32_MAX
		    && font->width + 1 == console->cell_width;
		bool repeat_last = expanded
		    && (console->video_flags
		    & ST_CONSOLE_VIDEO_LINE_GRAPHICS_EXPAND) != 0
		    && source->glyph >= UINT32_C(0xc0)
		    && source->glyph <= UINT32_C(0xdf);
		bool separated = (source->flags & ST_CONSOLE_CELL_SEPARATED) != 0
		    && source->glyph >= 160;

		if (row_scale == ROW_SCALE_TOP
		    && (source->flags & ST_CONSOLE_CELL_DOUBLE_HEIGHT) != 0) {
			font_y = 0;
			y_span = (font->height + 1) / 2;
		}
		else if (row_scale == ROW_SCALE_BOTTOM) {
			y_span = font->height / 2;
			font_y = font->height - y_span;
		}
		else {
			font_y = 0;
			y_span = font->height;
		}
		if (color != NULL && color->row_stride == 0) {
			render_color_rll(console, x, y, source, font, color, glyph_width,
			    glyph_x, font_y, y_span, expanded, repeat_last, foreground, background);
			goto finished;
		}
		y_step = y_span / console->cell_height;
		y_remainder = y_span % console->cell_height;
		for (uint32_t py = 0; py < console->cell_height; ++py) {
			const uint8_t *font_row = font_y < font->height
			    ? glyph + (size_t)font_y * glyph_row_stride : NULL;
			size_t pixel = st_console_pixel_position_locked(console,
			    x * console->cell_width, y * console->cell_height + py);
			uint32_t font_x = glyph_x;
			uint32_t x_accumulator = 0;
			for (uint32_t px = 0; px < console->cell_width; ++px) {
				bool expansion_gap = expanded && px == font->width
				    && !repeat_last;
				bool set;

				if (expanded)
					font_x = glyph_x + (px < font->width
					    ? px : font->width - 1);
				set = !expansion_gap && font_row != NULL
				    && row_bit(font_row, font_x);

				if (color == NULL && !set && !expansion_gap && font_row != NULL
				    && (source->flags & ST_CONSOLE_CELL_BOLD) != 0
				    && font_x != 0)
					set = row_bit(font_row, font_x - 1);
				if (set && separated) {
					uint32_t nominal_x = (uint32_t)((uint64_t)font_x
					    * 8 / glyph_width);
					uint32_t nominal_y = (uint32_t)((uint64_t)font_y
					    * 20 / font->height);

					if (nominal_x < 2 || nominal_x >= 6
					    || nominal_y == 4 || nominal_y == 5
					    || nominal_y == 12 || nominal_y == 13
					    || nominal_y >= 18)
						set = false;
				}
				if ((source->flags & ST_CONSOLE_CELL_UNDERLINE) != 0
				    && py + 1 == console->cell_height)
					set = true;
				if (source->hyperlink != 0
				    && py + 1 == console->cell_height
				    && (px & 3U) == 0)
					set = true;
				uint32_t value = set ? foreground : background;
				if (color != NULL) {
					bool decoration = py + 1 == console->cell_height
					    && (((source->flags & ST_CONSOLE_CELL_UNDERLINE) != 0)
					    || (source->hyperlink != 0 && (px & 3U) == 0));

					value = background;
					if (!expansion_gap && font_y < font->height) {
						uint32_t color_x = color->width == 0 ? font_x
						    : (uint32_t)((uint64_t)font_x * color->width / glyph_width);
						const uint8_t *rgba = color->rgba
						    + (size_t)font_y * color->row_stride
						    + (size_t)color_x * 4;
						value = composite_rgb(console, rgba, rgba[3], background);
					}
					if (decoration)
						value = foreground;
				}
				console->pixels->primary[pixel + px] = value;
				console->pixels->alternate[pixel + px]
				    = (source->flags & ST_CONSOLE_CELL_BLINK) == 0
				    ? value : background;
				if (!expanded) {
					font_x += x_step;
					x_accumulator += x_remainder;
					if (x_accumulator >= console->cell_width) {
						x_accumulator -= console->cell_width;
						++font_x;
					}
				}
			}
			font_y += y_step;
			y_accumulator += y_remainder;
			if (y_accumulator >= console->cell_height) {
				y_accumulator -= console->cell_height;
				++font_y;
			}
		}
	}
finished:
	(*st_console_drawn_at(console, index)) = *cell;
	st_console_drawn_at(console, index)->flags &= ~ST_CONSOLE_CELL_DIRTY;
}

int
st_console_rasterize_locked(st_console_t *console, bool force)
{
	bool previous_top = false;
	struct font_cache font_cache = {0};

	if (console->pixels == NULL)
		return 0;
	for (uint32_t y = 0; y < console->rows; ++y) {
		enum row_scale scale = ROW_SCALE_NORMAL;
		bool row_changed;
		struct st_console_cell *cells = st_console_cell_at(console,
		    (size_t)y * console->columns);
		struct st_console_cell *drawn = st_console_drawn_at(console,
		    (size_t)y * console->columns);

		if (previous_top) {
			scale = ROW_SCALE_BOTTOM;
			previous_top = false;
		}
		else if (y + 1 < console->rows) {
			for (uint32_t x = 0; x < console->columns; ++x) {
				const struct st_console_cell *cell = &cells[x];

				if ((cell->flags & (ST_CONSOLE_CELL_SAA5050
				    | ST_CONSOLE_CELL_DOUBLE_HEIGHT))
				    == (ST_CONSOLE_CELL_SAA5050
				    | ST_CONSOLE_CELL_DOUBLE_HEIGHT)) {
					scale = ROW_SCALE_TOP;
					previous_top = true;
					break;
				}
			}
		}
		row_changed = console->raster_rows[st_console_row_index(console, y)]
		    != (uint8_t)scale;
		for (uint32_t x = 0; x < console->columns;) {
			const struct st_console_cell *cell = &cells[x];
			uint32_t end;

			if (!force && !row_changed
			    && (drawn[x].flags
			    & ST_CONSOLE_CELL_DIRTY) == 0
			    && st_console_cells_equal(cell, &drawn[x])) {
				++x;
				continue;
			}
			end = x + 1;
			while (end < console->columns) {
				cell = &cells[end];
				if (!force && !row_changed
				    && (drawn[end].flags
				    & ST_CONSOLE_CELL_DIRTY) == 0
				    && st_console_cells_equal(cell,
				    &drawn[end]))
					break;
				++end;
			}
			if (scale == ROW_SCALE_NORMAL) {
				size_t rendered = render_fast_cells(console, x, y,
				    end - x, &font_cache);

				if (rendered != 0) {
					x += (uint32_t)rendered;
					continue;
				}
			}
			render_cell(console, x, y, scale, &font_cache);
			cells[x].flags
			    |= ST_CONSOLE_CELL_DIRTY;
			/* Prestel's lower half is derived from the upper cell. */
			if (scale == ROW_SCALE_TOP && y + 1 < console->rows) {
				size_t below = (size_t)(y + 1) * console->columns + x;

				if ((st_console_cell_at(console, below)->flags
				    & ST_CONSOLE_CELL_TERMINAL_FONT) != 0)
					st_console_drawn_at(console, below)->flags
					    |= ST_CONSOLE_CELL_DIRTY;
			}
			++x;
		}
		console->raster_rows[st_console_row_index(console, y)] = (uint8_t)scale;
	}
	console->raster_current = true;
	return 0;
}

static void
copy_pixel_row_locked(st_console_t *console, uint32_t from, uint32_t to,
	size_t bytes)
{
	size_t source = st_console_pixel_position_locked(console, 0, from);
	size_t target = st_console_pixel_position_locked(console, 0, to);

	memmove(console->pixels->primary + target,
	    console->pixels->primary + source, bytes);
	memmove(console->pixels->alternate + target,
	    console->pixels->alternate + source, bytes);
}

static void
move_raster_locked(st_console_t *console,
	const struct st_console_rect *source, uint32_t destination_x,
	uint32_t destination_y, bool metadata)
{
	bool backwards;
	bool full_scroll_up;
	bool full_scroll_down;
	size_t cell_bytes;
	size_t pixel_bytes;

	if (console->pixels == NULL || source->width == 0 || source->height == 0
	    || (source->x == destination_x && source->y == destination_y))
		return;
	console->raster_current = false;
	backwards = destination_y > source->y;
	cell_bytes = (size_t)source->width * sizeof(console->drawn_cells[0]);
	pixel_bytes = (size_t)source->width * console->cell_width
	    * sizeof(console->pixels->primary[0]);
	full_scroll_up = source->x == 0 && destination_x == 0
	    && source->width == console->columns && destination_y == 0
	    && source->y > 0
	    && source->height == console->rows - source->y;
	full_scroll_down = source->x == 0 && destination_x == 0
	    && source->width == console->columns && source->y == 0
	    && destination_y > 0
	    && source->height == console->rows - destination_y;
	/* Origin rotation is valid only for move-and-clear: a plain move must
	 * retain the old exposed source pixels, including pixel graphics. */
	if (!metadata && (full_scroll_up || full_scroll_down)) {
		uint32_t text_height = console->full_surface_scroll
		    ? console->pixels->height
		    : console->rows * console->cell_height;
		uint32_t shift = (full_scroll_up ? source->y : destination_y)
		    * console->cell_height;

		if (full_scroll_up) {
			console->pixel_top += shift;
			if (console->pixel_top >= text_height)
				console->pixel_top -= text_height;
		}
		else if (console->pixel_top < shift)
			console->pixel_top += text_height - shift;
		else
			console->pixel_top -= shift;
		return;
	}
	if (!metadata && source->x == 0 && destination_x == 0
	    && source->width == console->columns
	    && (!console->full_surface_scroll
	    || (console->pixel_extra_width == 0 && console->pixel_extra_height == 0))) {
		uint32_t height = console->rows * console->cell_height;
		uint32_t shift = (backwards ? destination_y - source->y
		    : source->y - destination_y) * console->cell_height;
		uint32_t first = (backwards ? source->y : destination_y)
		    * console->cell_height;
		uint32_t end = ((backwards ? destination_y : source->y)
		    + source->height) * console->cell_height;

		/* As with the cell ring, only rows outside the scroll region
		 * require copying. Keep status rows and non-text margins fixed. */
		if (!backwards) {
			for (uint32_t y = first; y-- > 0;)
				copy_pixel_row_locked(console, y, (y + shift) % height, pixel_bytes);
			for (uint32_t y = height; y-- > end;)
				copy_pixel_row_locked(console, y, (y + shift) % height, pixel_bytes);
			console->pixel_top = (console->pixel_top + shift) % height;
		} else {
			for (uint32_t y = end; y < height; ++y)
				copy_pixel_row_locked(console, y,
				    (y + height - shift) % height, pixel_bytes);
			for (uint32_t y = 0; y < first; ++y)
				copy_pixel_row_locked(console, y,
				    (y + height - shift) % height, pixel_bytes);
			console->pixel_top = (console->pixel_top + height - shift) % height;
		}
		return;
	}
	for (uint32_t row_index = 0; row_index < source->height; ++row_index) {
		uint32_t row = backwards ? source->height - row_index - 1 : row_index;
		uint32_t source_y = source->y + row;
		uint32_t target_y = destination_y + row;
		size_t source_cell = (size_t)source_y * console->columns + source->x;
		size_t target_cell = (size_t)target_y * console->columns
		    + destination_x;

		if (metadata) {
			memmove(st_console_drawn_at(console, target_cell),
			    st_console_drawn_at(console, source_cell), cell_bytes);
			if (source->x == 0 && destination_x == 0
			    && source->width == console->columns)
				console->raster_rows[st_console_row_index(console, target_y)]
				    = console->raster_rows[st_console_row_index(console, source_y)];
		}
		for (uint32_t py = 0; pixel_bytes != 0
		    && py < console->cell_height; ++py) {
			size_t source_pixel = st_console_pixel_position_locked(console,
			    source->x * console->cell_width,
			    source_y * console->cell_height + py);
			size_t target_pixel = st_console_pixel_position_locked(console,
			    destination_x * console->cell_width,
			    target_y * console->cell_height + py);

			memmove(&console->pixels->primary[target_pixel],
			    &console->pixels->primary[source_pixel], pixel_bytes);
			memmove(&console->pixels->alternate[target_pixel],
			    &console->pixels->alternate[source_pixel], pixel_bytes);
		}
	}
}

void
st_console_move_raster_locked(st_console_t *console,
	const struct st_console_rect *source, uint32_t destination_x,
	uint32_t destination_y)
{
	move_raster_locked(console, source, destination_x, destination_y, true);
}

void
st_console_move_pixels_locked(st_console_t *console,
	const struct st_console_rect *source, uint32_t destination_x,
	uint32_t destination_y)
{
	move_raster_locked(console, source, destination_x, destination_y, false);
}

static bool
pixel_rect_valid(const st_console_t *console,
	const struct st_console_rect *rect)
{
	return console->pixels != NULL && rect != NULL && rect->width != 0
	    && rect->height != 0 && rect->x < console->pixels->width
	    && rect->y < console->pixels->height
	    && rect->width <= console->pixels->width - rect->x
	    && rect->height <= console->pixels->height - rect->y;
}

static bool
pixel_changes_supported(const st_console_t *console)
{
	return console->display->backend_ops == NULL
	    || console->display->backend_ops->presents_pixels;
}

int
st_console_pixel_size(st_console_t *console, uint32_t *width,
	uint32_t *height)
{
	int status;

	if (console == NULL || width == NULL || height == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rwlock_read_lock(&console->state_lock);
	if (status != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (console->pixels == NULL)
		status = IO_ERROR_NOT_SUPPORTED;
	else {
		*width = console->pixels->width;
		*height = console->pixels->height;
		status = 0;
	}
	(void)st_rwlock_unlock(&console->state_lock);
	return status;
}

static void
mark_pixel_cells(st_console_t *console, int64_t left, int64_t top,
	int64_t right, int64_t bottom)
{
	uint32_t first_x;
	uint32_t first_y;
	uint32_t last_x;
	uint32_t last_y;

	if (left < 0)
		left = 0;
	if (top < 0)
		top = 0;
	if (right > console->pixels->width)
		right = console->pixels->width;
	if (bottom > console->pixels->height)
		bottom = console->pixels->height;
	if (left >= right || top >= bottom)
		return;
	first_x = (uint32_t)left / console->cell_width;
	first_y = (uint32_t)top / console->cell_height;
	last_x = ((uint32_t)right - 1) / console->cell_width;
	last_y = ((uint32_t)bottom - 1) / console->cell_height;
	if (first_x >= console->columns || first_y >= console->rows) {
		console->pixel_surface_dirty = true;
		console->changed = true;
		return;
	}
	if (last_x >= console->columns) {
		last_x = console->columns - 1;
		console->pixel_surface_dirty = true;
	}
	if (last_y >= console->rows) {
		last_y = console->rows - 1;
		console->pixel_surface_dirty = true;
	}
	for (uint32_t y = first_y; y <= last_y; ++y) {
		for (uint32_t x = first_x; x <= last_x; ++x) {
			size_t index = (size_t)y * console->columns + x;

			st_console_cell_at(console, index)->flags |= ST_CONSOLE_CELL_PIXEL_GRAPHICS
			    | ST_CONSOLE_CELL_DIRTY;
			st_console_drawn_at(console, index)->flags
			    |= ST_CONSOLE_CELL_PIXEL_GRAPHICS;
		}
	}
	console->changed = true;
}

int
st_console_set_pixel(st_console_t *console, uint32_t x, uint32_t y,
	uint32_t colour)
{
	size_t position;
	int status;

	if (console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rwlock_write_lock(&console->state_lock);
	if (status != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (console->pixels == NULL || !pixel_changes_supported(console))
		status = IO_ERROR_NOT_SUPPORTED;
	else if (x >= console->pixels->width || y >= console->pixels->height)
		status = IO_ERROR_OUT_OF_RANGE;
	else {
		status = console->raster_current ? 0
		    : st_console_rasterize_locked(console, false);
		if (status == 0) {
			position = st_console_pixel_position_locked(console, x, y);
			console->pixels->primary[position] = colour;
			console->pixels->alternate[position] = colour;
			mark_pixel_cells(console, x, y, (int64_t)x + 1,
			    (int64_t)y + 1);
			status = st_console_finish_change_locked(console);
		}
	}
	(void)st_rwlock_unlock(&console->state_lock);
	return status;
}

int
st_console_get_pixels(struct st_pixel_buffer **result, st_console_t *console,
	const struct st_console_rect *source, bool force_text)
{
	struct st_pixel_buffer *copy = NULL;
	int status;

	if (result == NULL || console == NULL || source == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rwlock_write_lock(&console->state_lock);
	if (status != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (console->pixels == NULL)
		status = IO_ERROR_NOT_SUPPORTED;
	else if (!pixel_rect_valid(console, source))
		status = IO_ERROR_OUT_OF_RANGE;
	else {
		status = !force_text && console->raster_current ? 0
		    : st_console_rasterize_locked(console, force_text);
		if (status == 0 && force_text) {
			console->changed = true;
			status = st_console_finish_change_locked(console);
		}
		if (status == 0)
			status = st_pixel_buffer_create(&copy, source->width,
			    source->height, true);
		if (status == 0) {
			st_console_copy_pixels_locked(console, copy->primary,
			    copy->width, false, source);
			st_console_copy_pixels_locked(console, copy->alternate,
			    copy->width, true, source);
		}
	}
	(void)st_rwlock_unlock(&console->state_lock);
	if (status != 0) {
		st_pixel_buffer_destroy(copy);
		return status;
	}
	*result = copy;
	return 0;
}

int
st_console_blit_pixels(st_console_t *console,
	const struct st_pixel_buffer *source, const struct st_pixel_mask *mask,
	const struct st_pixel_blit *blit)
{
	uint64_t width;
	uint64_t height;
	int status;

	if (console == NULL || source == NULL || blit == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rwlock_write_lock(&console->state_lock);
	if (status != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (console->pixels == NULL || !pixel_changes_supported(console))
		status = IO_ERROR_NOT_SUPPORTED;
	else {
		status = console->raster_current ? 0
		    : st_console_rasterize_locked(console, false);
		if (status == 0)
			status = st_console_normalize_raster_locked(console);
		if (status == 0)
			status = st_pixel_buffer_blit(console->pixels, source, mask,
			    blit);
		if (status == 0) {
			width = (uint64_t)blit->width * blit->scale_x;
			height = (uint64_t)blit->height * blit->scale_y;
			mark_pixel_cells(console, blit->destination_x,
			    blit->destination_y,
			    (int64_t)blit->destination_x + (int64_t)width,
			    (int64_t)blit->destination_y + (int64_t)height);
			status = st_console_finish_change_locked(console);
		}
	}
	(void)st_rwlock_unlock(&console->state_lock);
	return status;
}
