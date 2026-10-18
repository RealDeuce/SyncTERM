/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * RIP/BGI raster arithmetic retained from the legacy renderer.
 */

#include "cterm_rip_raster.h"
#include "cterm_rip_asset.h"
#include "cterm_rip_fonts.h"
#include "cterm_rip_icon_file.h"
#include "cterm_rip_icons.h"

#include "filesystem.h"
#include "io_error.h"
#include "string_compat.h"
#include "st_pixels.h"
#include "st_utf8.h"

#include "string_util.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint16_t      ega_palette[64][3] = {
	{0, 0, 0},
	{0, 0, 0xaaaa},
	{0, 0xaaaa, 0},
	{0, 0xaaaa, 0xaaaa},
	{0xaaaa, 0, 0},
	{0xaaaa, 0, 0xaaaa},
	{0xaaaa, 0xaaaa, 0},
	{0xaaaa, 0xaaaa, 0xaaaa},
	{0, 0, 0x5555},
	{0, 0, 0xffff},
	{0, 0xaaaa, 0x5555},            // 10
	{0, 0xaaaa, 0xffff},
	{0xaaaa, 0, 0x5555},
	{0xaaaa, 0, 0xffff},
	{0xaaaa, 0xaaaa, 0x5555},
	{0xaaaa, 0xaaaa, 0xffff},
	{0, 0x5555, 0},
	{0, 0x5555, 0xaaaa},
	{0, 0xffff, 0},
	{0, 0xffff, 0xaaaa},
	{0xaaaa, 0x5555, 0},            // 20
	{0xaaaa, 0x5555, 0xaaaa},
	{0xaaaa, 0xffff, 0},
	{0xaaaa, 0xffff, 0xaaaa},
	{0, 0x5555, 0x5555},
	{0, 0x5555, 0xffff},
	{0, 0xffff, 0x5555},
	{0, 0xffff, 0xffff},
	{0xaaaa, 0x5555, 0x5555},
	{0xaaaa, 0x5555, 0xffff},
	{0xaaaa, 0xffff, 0x5555},       // 30
	{0xaaaa, 0xffff, 0xffff},
	{0x5555, 0, 0},
	{0x5555, 0, 0xaaaa},
	{0x5555, 0xaaaa, 0},
	{0x5555, 0xaaaa, 0xaaaa},
	{0xffff, 0, 0},
	{0xffff, 0, 0xaaaa},
	{0xffff, 0xaaaa, 0},
	{0xffff, 0xaaaa, 0xaaaa},
	{0x5555, 0, 0x5555},            // 40
	{0x5555, 0, 0xffff},
	{0x5555, 0xaaaa, 0x5555},
	{0x5555, 0xaaaa, 0xffff},
	{0xffff, 0, 0x5555},
	{0xffff, 0, 0xffff},
	{0xffff, 0xaaaa, 0x5555},
	{0xffff, 0xaaaa, 0xffff},
	{0x5555, 0x5555, 0},
	{0x5555, 0x5555, 0xaaaa},
	{0x5555, 0xffff, 0},            // 50
	{0x5555, 0xffff, 0xaaaa},
	{0xffff, 0x5555, 0},
	{0xffff, 0x5555, 0xaaaa},
	{0xffff, 0xffff, 0},
	{0xffff, 0xffff, 0xaaaa},
	{0x5555, 0x5555, 0x5555},
	{0x5555, 0x5555, 0xffff},
	{0x5555, 0xffff, 0x5555},
	{0x5555, 0xffff, 0xffff},
	{0xffff, 0x5555, 0x5555},       // 60
	{0xffff, 0x5555, 0xffff},
	{0xffff, 0xffff, 0x5555},
	{0xffff, 0xffff, 0xffff}
};

static const uint8_t       default_mapped[256][3] = {
	{0, 0, 0}, {0, 0, 42}, {0, 42, 0}, {0, 42, 42}, {42, 0, 0}, {42, 0, 42},
	{42, 21, 0}, {42, 42, 42}, {21, 21, 21}, {21, 21, 63}, {21, 63, 21}, {21, 63, 63},
	{63, 21, 21}, {63, 21, 63}, {63, 63, 21}, {63, 63, 63}, {0, 0, 0}, {4, 4, 4},
	{8, 8, 8}, {13, 13, 13}, {17, 17, 17}, {21, 21, 21}, {25, 25, 25}, {29, 29, 29},
	{34, 34, 34}, {38, 38, 38}, {42, 42, 42}, {46, 46, 46}, {50, 50, 50}, {55, 55, 55},
	{59, 59, 59}, {63, 63, 63}, {0, 0, 0}, {0, 0, 21}, {0, 0, 42}, {0, 0, 63},
	{0, 9, 0}, {0, 9, 21}, {0, 9, 42}, {0, 9, 63}, {0, 18, 0}, {0, 18, 21},
	{0, 18, 42}, {0, 18, 63}, {0, 27, 0}, {0, 27, 21}, {0, 27, 42}, {0, 27, 63},
	{0, 36, 0}, {0, 36, 21}, {0, 36, 42}, {0, 36, 63}, {0, 45, 0}, {0, 45, 21},
	{0, 45, 42}, {0, 45, 63}, {0, 54, 0}, {0, 54, 21}, {0, 54, 42}, {0, 54, 63},
	{0, 63, 0}, {0, 63, 21}, {0, 63, 42}, {0, 63, 63}, {10, 0, 0}, {10, 0, 21},
	{10, 0, 42}, {10, 0, 63}, {10, 9, 0}, {10, 9, 21}, {10, 9, 42}, {10, 9, 63},
	{10, 18, 0}, {10, 18, 21}, {10, 18, 42}, {10, 18, 63}, {10, 27, 0}, {10, 27, 21},
	{10, 27, 42}, {10, 27, 63}, {10, 36, 0}, {10, 36, 21}, {10, 36, 42}, {10, 36, 63},
	{10, 45, 0}, {10, 45, 21}, {10, 45, 42}, {10, 45, 63}, {10, 54, 0}, {10, 54, 21},
	{10, 54, 42}, {10, 54, 63}, {10, 63, 0}, {10, 63, 21}, {10, 63, 42}, {10, 63, 63},
	{21, 0, 0}, {21, 0, 21}, {21, 0, 42}, {21, 0, 63}, {21, 9, 0}, {21, 9, 21},
	{21, 9, 42}, {21, 9, 63}, {21, 18, 0}, {21, 18, 21}, {21, 18, 42}, {21, 18, 63},
	{21, 27, 0}, {21, 27, 21}, {21, 27, 42}, {21, 27, 63}, {21, 36, 0}, {21, 36, 21},
	{21, 36, 42}, {21, 36, 63}, {21, 45, 0}, {21, 45, 21}, {21, 45, 42}, {21, 45, 63},
	{21, 54, 0}, {21, 54, 21}, {21, 54, 42}, {21, 54, 63}, {21, 63, 0}, {21, 63, 21},
	{21, 63, 42}, {21, 63, 63}, {31, 0, 0}, {31, 0, 21}, {31, 0, 42}, {31, 0, 63},
	{31, 9, 0}, {31, 9, 21}, {31, 9, 42}, {31, 9, 63}, {31, 18, 0}, {31, 18, 21},
	{31, 18, 42}, {31, 18, 63}, {31, 27, 0}, {31, 27, 21}, {31, 27, 42}, {31, 27, 63},
	{31, 36, 0}, {31, 36, 21}, {31, 36, 42}, {31, 36, 63}, {31, 45, 0}, {31, 45, 21},
	{31, 45, 42}, {31, 45, 63}, {31, 54, 0}, {31, 54, 21}, {31, 54, 42}, {31, 54, 63},
	{31, 63, 0}, {31, 63, 21}, {31, 63, 42}, {31, 63, 63}, {42, 0, 0}, {42, 0, 21},
	{42, 0, 42}, {42, 0, 63}, {42, 9, 0}, {42, 9, 21}, {42, 9, 42}, {42, 9, 63},
	{42, 18, 0}, {42, 18, 21}, {42, 18, 42}, {42, 18, 63}, {42, 27, 0}, {42, 27, 21},
	{42, 27, 42}, {42, 27, 63}, {42, 36, 0}, {42, 36, 21}, {42, 36, 42}, {42, 36, 63},
	{42, 45, 0}, {42, 45, 21}, {42, 45, 42}, {42, 45, 63}, {42, 54, 0}, {42, 54, 21},
	{42, 54, 42}, {42, 54, 63}, {42, 63, 0}, {42, 63, 21}, {42, 63, 42}, {42, 63, 63},
	{52, 0, 0}, {52, 0, 21}, {52, 0, 42}, {52, 0, 63}, {52, 9, 0}, {52, 9, 21},
	{52, 9, 42}, {52, 9, 63}, {52, 18, 0}, {52, 18, 21}, {52, 18, 42}, {52, 18, 63},
	{52, 27, 0}, {52, 27, 21}, {52, 27, 42}, {52, 27, 63}, {52, 36, 0}, {52, 36, 21},
	{52, 36, 42}, {52, 36, 63}, {52, 45, 0}, {52, 45, 21}, {52, 45, 42}, {52, 45, 63},
	{52, 54, 0}, {52, 54, 21}, {52, 54, 42}, {52, 54, 63}, {52, 63, 0}, {52, 63, 21},
	{52, 63, 42}, {52, 63, 63}, {63, 0, 0}, {63, 0, 21}, {63, 0, 42}, {63, 0, 63},
	{63, 9, 0}, {63, 9, 21}, {63, 9, 42}, {63, 9, 63}, {63, 18, 0}, {63, 18, 21},
	{63, 18, 42}, {63, 18, 63}, {63, 27, 0}, {63, 27, 21}, {63, 27, 42}, {63, 27, 63},
	{63, 36, 0}, {63, 36, 21}, {63, 36, 42}, {63, 36, 63}, {63, 45, 0}, {63, 45, 21},
	{63, 45, 42}, {63, 45, 63}, {63, 54, 0}, {63, 54, 21}, {63, 54, 42}, {63, 54, 63},
	{63, 63, 0}, {63, 63, 21}, {63, 63, 42}, {63, 63, 63}
};

static const uint8_t initial_ega_palette[16] = {
	0, 1, 2, 3, 4, 5, 20, 7, 56, 57, 58, 59, 60, 61, 62, 63
};

/* Keep the legacy table verbatim, including its documented slash direction. */
static const uint8_t rip_fill_patterns[12][8] = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
	{0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00},
	{0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80},
	{0xe0, 0xc1, 0x83, 0x07, 0x0e, 0x1c, 0x38, 0x70},
	{0xf0, 0x78, 0x3c, 0x1e, 0x0f, 0x87, 0xc3, 0xe1},
	{0xa5, 0xd2, 0x69, 0xb4, 0x5a, 0x2d, 0x96, 0x4b},
	{0xff, 0x88, 0x88, 0x88, 0xff, 0x88, 0x88, 0x88},
	{0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81},
	{0xcc, 0x33, 0xcc, 0x33, 0xcc, 0x33, 0xcc, 0x33},
	{0x80, 0x00, 0x08, 0x00, 0x80, 0x00, 0x08, 0x00},
	{0x88, 0x00, 0x22, 0x00, 0x88, 0x00, 0x22, 0x00},
};

static const uint32_t initial_colour_indexes[16] = {
	0x10, 0x14, 0x12, 0x16, 0x11, 0x15, 0x13, 0x17,
	0x18, 0x1c, 0x1a, 0x1e, 0x19, 0x1d, 0x1b, 0x1f
};

static uint32_t
map_colour(const struct cterm_rip_raster *rip, int colour)
{
	colour &= 0x80ffffff;
	if (colour < 16)
		return rip->palette[colour];
	if (colour < 256)
		return ST_PIXEL_COLOR_RGB_TAG
		    | ((uint32_t)((default_mapped[colour][0] << 2)
		        | (default_mapped[colour][0] & 3)) << 16)
		    | ((uint32_t)((default_mapped[colour][1] << 2)
		        | (default_mapped[colour][1] & 3)) << 8)
		    | (uint32_t)((default_mapped[colour][2] << 2)
		        | (default_mapped[colour][2] & 3));
	return 0;
}

static int
pixel_colour(const struct cterm_rip_raster *rip, uint32_t pixel)
{
	pixel &= 0x80ffffff;
	for (int colour = 0; colour < 16; colour++) {
		if (rip->palette[colour] == pixel)
			return colour;
	}
	for (int colour = 0; colour < 256; colour++) {
		if (map_colour(rip, colour) == pixel)
			return colour;
	}
	return 0;
}

int
cterm_rip_raster_init(struct cterm_rip_raster *rip, st_console_t *console)
{
	uint32_t width, height;
	int status;

	if (rip == NULL || console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_console_pixel_size(console, &width, &height);
	if (status != 0)
		return status;
	/* The legacy renderer uses a 640x350 RIP canvas at the upper left of
	 * a larger text screen too (notably 640x400 80x25). */
	if (width < 640 || height < 350)
		return IO_ERROR_NOT_SUPPORTED;
	memset(rip, 0, sizeof(*rip));
	rip->console = console;
	rip->text_codepage = ST_CODEPAGE_CP437;
	rip->screen_width = width;
	rip->screen_height = height;
	rip->x_dim = 640;
	rip->y_dim = 350;
	rip->x_max = 640;
	rip->y_max = 350;
	rip->viewport.ex = 639;
	rip->viewport.ey = 349;
	rip->colour = 1;
	rip->line_pattern = 0xffff;
	rip->line_width = 1;
	/* Legacy init leaves borders off; RIP_RESET_WINDOWS enables them. */
	rip->borders = false;
	rip->version = 1;
	rip->font_size = 1;
	rip->text_region.sx = -1;
	rip->text_region.sy = -1;
	rip->text_region.ex = -1;
	rip->text_region.ey = -1;
	rip->fill_colour = 7;
	rip->fill_style = 1;
	memcpy(rip->fill_pattern, rip_fill_patterns[1], 8);
	memcpy(rip->palette, initial_colour_indexes, sizeof(rip->palette));
	memcpy(rip->master_palette, initial_ega_palette,
	    sizeof(rip->master_palette));
	for (size_t i = 0; i < 16; i++) {
		const uint16_t *rgb16 = ega_palette[rip->master_palette[i]];
		const uint32_t rgb = ((uint32_t)(rgb16[0] >> 8) << 16)
		    | ((uint32_t)(rgb16[1] >> 8) << 8) | (rgb16[2] >> 8);

		status = st_console_set_palette(console, (uint16_t)rip->palette[i],
		    &rgb, 1);
		if (status != 0)
			return status;
	}
	return 0;
}

void
cterm_rip_raster_destroy(struct cterm_rip_raster *rip)
{
	if (rip == NULL)
		return;
	st_pixel_buffer_destroy(rip->clipboard);
	rip->clipboard = NULL;
	st_pixel_buffer_destroy(rip->saved_clipboard.pixels);
	rip->saved_clipboard.pixels = NULL;
	for (size_t i = 0; i < sizeof(rip->screen_saves)
	    / sizeof(rip->screen_saves[0]); i++) {
		st_pixel_buffer_destroy(rip->screen_saves[i].pixels);
		rip->screen_saves[i].pixels = NULL;
	}
	free(rip->poly_fill_marks);
	rip->poly_fill_marks = NULL;
}

int
cterm_rip_raster_save_screen(struct cterm_rip_raster *rip, int slot)
{
	struct st_pixel_buffer *pixels = NULL;
	struct cterm_rip_saved_screen *saved;
	struct st_console_rect rect;
	int status;

	if (rip == NULL || slot < 0 || slot >= 11)
		return IO_ERROR_INVALID_ARGUMENT;
	if (rip->x_max <= 0 || rip->y_max <= 0)
		return IO_ERROR_OUT_OF_RANGE;
	rect = (struct st_console_rect){
		.x = 0, .y = 0,
		.width = (uint32_t)rip->x_max,
		.height = (uint32_t)rip->y_max,
	};
	status = st_console_get_pixels(&pixels, rip->console, &rect, false);
	if (status != 0)
		return status;
	saved = &rip->screen_saves[slot];
	st_pixel_buffer_destroy(saved->pixels);
	saved->pixels = pixels;
	saved->sx = rip->viewport.sx;
	saved->sy = rip->viewport.sy;
	saved->ex = rip->viewport.ex;
	saved->ey = rip->viewport.ey;
	return 0;
}

int
cterm_rip_raster_restore_screen(struct cterm_rip_raster *rip,
	int slot, bool clear_after)
{
	struct cterm_rip_saved_screen *saved;
	struct st_pixel_blit blit;
	int status;

	if (rip == NULL || slot < 0 || slot >= 11)
		return IO_ERROR_INVALID_ARGUMENT;
	saved = &rip->screen_saves[slot];
	if (saved->pixels == NULL)
		return 0;
	blit = (struct st_pixel_blit){
		.width = saved->pixels->width,
		.height = saved->pixels->height,
		.scale_x = 1,
		.scale_y = 1,
	};
	status = st_console_blit_pixels(rip->console, saved->pixels, NULL,
	    &blit);
	if (status != 0)
		return status;
	rip->viewport.sx = saved->sx;
	rip->viewport.sy = saved->sy;
	rip->viewport.ex = saved->ex;
	rip->viewport.ey = saved->ey;
	if (clear_after) {
		st_pixel_buffer_destroy(saved->pixels);
		saved->pixels = NULL;
	}
	return 0;
}

int
cterm_rip_raster_save_clipboard(struct cterm_rip_raster *rip)
{
	struct st_pixel_buffer *copy = NULL;
	int status;

	if (rip == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (rip->clipboard != NULL) {
		status = st_pixel_buffer_clone(&copy, rip->clipboard);
		if (status != 0)
			return status;
	}
	st_pixel_buffer_destroy(rip->saved_clipboard.pixels);
	rip->saved_clipboard.pixels = copy;
	rip->saved_clipboard.x = rip->clipx;
	rip->saved_clipboard.y = rip->clipy;
	return 0;
}

int
cterm_rip_raster_restore_clipboard(struct cterm_rip_raster *rip)
{
	struct st_pixel_buffer *copy = NULL;
	int status;

	if (rip == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (rip->saved_clipboard.pixels != NULL) {
		status = st_pixel_buffer_clone(&copy,
		    rip->saved_clipboard.pixels);
		if (status != 0)
			return status;
	}
	st_pixel_buffer_destroy(rip->clipboard);
	rip->clipboard = copy;
	rip->clipx = rip->saved_clipboard.x;
	rip->clipy = rip->saved_clipboard.y;
	return 0;
}

int
cterm_rip_raster_paste_clipboard(struct cterm_rip_raster *rip)
{
	struct st_pixel_blit blit;

	if (rip == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (rip->clipboard == NULL)
		return 0;
	blit = (struct st_pixel_blit){
		.width = rip->clipboard->width,
		.height = rip->clipboard->height,
		.destination_x = rip->clipx,
		.destination_y = rip->clipy,
		.scale_x = 1,
		.scale_y = 1,
	};
	return st_console_blit_pixels(rip->console, rip->clipboard, NULL,
	    &blit);
}

static void
set_master_palette(struct cterm_rip_raster *rip)
{
	for (size_t i = 0; i < 16 && rip->error == 0; i++) {
		const uint16_t *rgb16 = ega_palette[rip->master_palette[i]];
		uint32_t rgb = ((uint32_t)(rgb16[0] >> 8) << 16)
		    | ((uint32_t)(rgb16[1] >> 8) << 8) | (rgb16[2] >> 8);

		rip->error = st_console_set_palette(rip->console,
		    (uint16_t)rip->palette[i], &rgb, 1);
	}
}

static void
refresh_screen_size(struct cterm_rip_raster *rip)
{
	uint32_t width, height;
	int status = st_console_pixel_size(rip->console, &width, &height);

	if (status != 0)
		rip->error = status;
	else {
		rip->screen_width = width;
		rip->screen_height = height;
	}
}

static bool
no_viewport(const struct cterm_rip_raster *rip)
{
	return rip->viewport.sx == 0 && rip->viewport.sy == 0
	    && rip->viewport.ex == 0 && rip->viewport.ey == 0;
}

/* RIPterm rounds each virtual coordinate independently.  Cached in-range
 * entries are clamped, while its out-of-range fallback is not. */
static int
map_axis(int value, int dimension, int maximum)
{
	int64_t numerator = (int64_t)value * maximum;
	int result;

	if (numerator >= 0)
		result = (int)((numerator + dimension / 2) / dimension);
	else
		result = -(int)((-numerator + dimension / 2) / dimension);
	if (value >= 0 && value < dimension && result >= maximum)
		result = maximum - 1;
	return result;
}

static int
map_x(const struct cterm_rip_raster *rip, int x)
{
	return map_axis(x, rip->x_dim, rip->x_max);
}

static int
map_y(const struct cterm_rip_raster *rip, int y)
{
	return map_axis(y, rip->y_dim, rip->y_max);
}

int
cterm_rip_raster_map_x(const struct cterm_rip_raster *rip, int x)
{
	return rip == NULL ? -1 : map_x(rip, x);
}

int
cterm_rip_raster_map_y(const struct cterm_rip_raster *rip, int y)
{
	return rip == NULL ? -1 : map_y(rip, y);
}

static int
unmap_axis(int value, int dimension, int maximum)
{
	int64_t numerator;

	if (value < 0)
		return -1;
	if (value >= dimension)
		value = dimension - 1;
	numerator = (int64_t)value * dimension;
	return (int)((numerator + maximum / 2) / maximum);
}

int
cterm_rip_raster_unmap_x(const struct cterm_rip_raster *rip, int x)
{
	return rip == NULL ? -1 : unmap_axis(x, rip->x_dim, rip->x_max);
}

int
cterm_rip_raster_unmap_y(const struct cterm_rip_raster *rip, int y)
{
	return rip == NULL ? -1 : unmap_axis(y, rip->y_dim, rip->y_max);
}

static void
set_pixel(struct cterm_rip_raster *rip, int x, int y, uint32_t colour)
{
	int actual_x = rip->viewport.sx + x;
	int actual_y = rip->viewport.sy + y;
	int mapped_x, mapped_y;

	if (rip->error != 0 || x < 0 || y < 0
	    || x > rip->viewport.ex - rip->viewport.sx
	    || y > rip->viewport.ey - rip->viewport.sy)
		return;
	mapped_x = map_x(rip, actual_x);
	mapped_y = map_y(rip, actual_y);
	if (mapped_x < 0 || mapped_y < 0
	    || (uint32_t)mapped_x >= rip->screen_width
	    || (uint32_t)mapped_y >= rip->screen_height)
		return;
	if (rip->poly_fill_marks != NULL && mapped_x < rip->x_max
	    && mapped_y < rip->y_max)
		rip->poly_fill_marks[(size_t)mapped_y * (size_t)rip->x_max
		    + (size_t)mapped_x]
		    |= 0x40;
	rip->error = st_console_set_pixel(rip->console, (uint32_t)mapped_x,
	    (uint32_t)mapped_y, colour);
}

static void
draw_pixel(struct cterm_rip_raster *rip, int x, int y)
{
	int colour = rip->colour;

	if (rip->error != 0 || x < 0 || y < 0
	    || x > rip->viewport.ex - rip->viewport.sx
	    || y > rip->viewport.ey - rip->viewport.sy)
		return;
	if (rip->xor_mode) {
		struct st_pixel_buffer *pixel = NULL;
		/* The legacy XOR read is unscaled; rip_setpixel maps the write. */
		const struct st_console_rect rect = {
			.x = (uint32_t)(rip->viewport.sx + x),
			.y = (uint32_t)(rip->viewport.sy + y),
			.width = 1, .height = 1,
		};
		if (rect.x >= rip->screen_width
		    || rect.y >= rip->screen_height)
			return;

		rip->error = st_console_get_pixels(&pixel, rip->console,
		    &rect, false);
		if (rip->error != 0)
			return;
		colour ^= pixel_colour(rip, pixel->primary[0]);
		st_pixel_buffer_destroy(pixel);
	}
	set_pixel(rip, x, y, map_colour(rip, colour));
}

static void draw_line_to(struct cterm_rip_raster *rip, int target_x,
    int target_y, int *ppos);

static bool
cs_clip_line(struct cterm_rip_raster *rip, int *x1p, int *y1p, int *x2p, int *y2p)
{
	int x1 = *x1p, y1 = *y1p;
	int x2 = *x2p, y2 = *y2p;
	int sx = rip->viewport.sx;
	int sy = rip->viewport.sy;
	int ex = rip->viewport.ex;
	int ey = rip->viewport.ey;

	for (;;) {
		int oc1 = 0, oc2 = 0;
		if (x1 < sx) oc1 |= 1;
		else if (x1 > ex) oc1 |= 2;
		if (y1 < sy) oc1 |= 4;
		else if (y1 > ey) oc1 |= 8;
		if (x2 < sx) oc2 |= 1;
		else if (x2 > ex) oc2 |= 2;
		if (y2 < sy) oc2 |= 4;
		else if (y2 > ey) oc2 |= 8;

		if ((oc1 | oc2) == 0) {
			// Both endpoints inside.
			*x1p = x1; *y1p = y1;
			*x2p = x2; *y2p = y2;
			return true;
		}
		if (oc1 & oc2) {
			// Both endpoints outside the same edge.
			return false;
		}

		// Always clip the endpoint currently outside.  Swap if needed
		// so pt1 is the outside one.
		bool swapped = false;
		if (oc1 == 0) {
			int t;
			t = x1; x1 = x2; x2 = t;
			t = y1; y1 = y2; y2 = t;
			t = oc1; oc1 = oc2; oc2 = t;
			swapped = true;
		}

		int dx = x2 - x1;
		int dy = y2 - y1;

		// Clip against one violated edge per iteration.  Checked in
		// outcode bit order (left, right, top, bottom) as BGI does.
		// dx!=0 is guaranteed for x clips (otherwise both endpoints
		// would share the same x outcode bit and we'd have early-
		// exited on oc1 & oc2).  Same reasoning for dy!=0 on y clips.
		if (oc1 & 1) {
			// x < left
			y1 += (sx - x1) * dy / dx;
			x1 = sx;
		}
		else if (oc1 & 2) {
			// x > right
			y1 += (ex - x1) * dy / dx;
			x1 = ex;
		}
		else if (oc1 & 4) {
			// y < top
			x1 += (sy - y1) * dx / dy;
			y1 = sy;
		}
		else if (oc1 & 8) {
			// y > bottom
			x1 += (ey - y1) * dx / dy;
			y1 = ey;
		}

		if (swapped) {
			int t;
			t = x1; x1 = x2; x2 = t;
			t = y1; y1 = y2; y2 = t;
		}
	}
}

static void
draw_line(struct cterm_rip_raster *rip, int x1, int y1, int x2, int y2)
{
	int ppos = 0;
	int saved_x = rip->x, saved_y = rip->y;

	// For standalone lines, the first endpoint goes into the
	// Bresenham's starting position (when y values are equal).
	// draw_line_to puts its target there, so pass x1,y1 as
	// target and x2,y2 as source (via CP).
	rip->x = x2;
	rip->y = y2;
	draw_line_to(rip, x1, y1, &ppos);
	rip->x = saved_x;
	rip->y = saved_y;
}

/*
 * Draw from current position to (target_x, target_y), updating CP.
 * Pattern position is passed in/out via *ppos.
 */
static void
draw_line_to(struct cterm_rip_raster *rip, int target_x, int target_y, int *ppos)
{
	// The Bresenham swaps endpoints to start from the upper point
	// (lower y).  When y values are equal, no swap — drawing starts
	// from x1,y1.  For lineto, that must be the target so that the
	// pattern phase matches.
	int  x1 = target_x, y1 = target_y;
	int  x2 = rip->x, y2 = rip->y;
	int  orig_x1 = x1, orig_y1 = y1;
	int  orig_x2 = x2, orig_y2 = y2;
	int  ldx = x2 - x1;
	int  ldy = y2 - y1;

	rip->x = target_x;
	rip->y = target_y;

	if (no_viewport(rip))
		return;

	// Cohen-Sutherland clip the center line.  The center is only
	// drawn when visible; thick offsets below are always processed
	// using the original (pre-clip) endpoints and independently CS-
	// clipped by their recursive draw_line_to calls.
	bool center_visible = cs_clip_line(rip, &x1, &y1, &x2, &y2);

	if (center_visible) {
		int *minc, *mins, *mino, *mind;
		int *maxc, *maxs, *maxe, *maxo, *maxd;
		int  x, y;
		int  swap = 0;

		int  dx = abs(x2 - x1);
		int  dy = abs(y2 - y1);

		if (y2 < y1)
			swap = 1;

		if (swap) {
			x = x2; y = y2;
			x2 = x1; y2 = y1;
			x1 = x; y1 = y;
		}

		int xoff = (x2 < x1) ? -1 : 1;
		int yoff = (y2 < y1) ? -1 : 1;

		if (dx >= dy) {
			minc = &y; mins = &y1; mino = &yoff; mind = &dy;
			maxc = &x; maxs = &x1; maxe = &x2; maxo = &xoff; maxd = &dx;
		}
		else {
			minc = &x; mins = &x1; mino = &xoff; mind = &dx;
			maxc = &y; maxs = &y1; maxe = &y2; maxo = &yoff; maxd = &dy;
		}

		int de = 2 * *mind - *maxd;
		int pp = *ppos;

		*minc = *mins;

		for ((*maxc) = *maxs; (((*maxs) < (*maxe)) ? ((*maxc) <= (*maxe)) : ((*maxc) >= (*maxe))); (*maxc) += *maxo) {
			if (rip->line_pattern & (1 << pp))
				draw_pixel(rip, x, y);
			if (de >= 0) {
				*minc = (*minc) + *mino;
				de = de - ((2 * *maxd) & INT_MAX);
			}
			de = de + ((2 * *mind) & INT_MAX);
			pp = (pp + 1) & 0x0F;
			if (*maxs == *maxe)
				break;
		}
		*ppos = pp;
	}

	if (rip->line_width == 3) {
		rip->line_width = 1;
		/*
		 * Offset direction and base endpoints come from the
		 * original (pre-clip) line.  The recursive call will
		 * CS-clip each offset line independently, matching BGI.
		 */
		int orig_adx = abs(ldx);
		int orig_ady = abs(ldy);
		int save_x = rip->x, save_y = rip->y;
		for (int side = -1; side <= 1; side += 2) {
			int ox1, oy1, ox2, oy2;
			if (orig_adx >= orig_ady) {
				ox1 = orig_x1; oy1 = orig_y1 + side;
				ox2 = orig_x2; oy2 = orig_y2 + side;
			}
			else {
				ox1 = orig_x1 + side; oy1 = orig_y1;
				ox2 = orig_x2 + side; oy2 = orig_y2;
			}
			int pp = 0;
			rip->x = ox2;
			rip->y = oy2;
			draw_line_to(rip, ox1, oy1, &pp);
		}
		rip->x = save_x;
		rip->y = save_y;
		rip->line_width = 3;
	}
}

/*
 * Parameterized line draw: explicit color, pattern, and width.
 * Companion to draw_line(rip, ) which uses rip->color/rip->line_pattern/rip->line_width.
 */
static void
set_line(struct cterm_rip_raster *rip, int x1, int y1, int x2, int y2, uint32_t color, uint16_t pat, int width)
{
	int  orig_x1 = x1, orig_y1 = y1;
	int  orig_x2 = x2, orig_y2 = y2;
	int  ldx = x2 - x1;
	int  ldy = y2 - y1;

	if (no_viewport(rip))
		return;

	// Cohen-Sutherland clip the center line.  Thick offsets below
	// are computed from the original (pre-clip) endpoints and drawn
	// via recursive set_line calls, which will CS-clip them.
	bool center_visible = cs_clip_line(rip, &x1, &y1, &x2, &y2);

	if (center_visible) {
		int *minc, *mins, *mino, *mind;
		int *maxc, *maxs, *maxe, *maxo, *maxd;
		int  x, y;
		int  swap = 0;
		int  dx = abs(x2 - x1);
		int  dy = abs(y2 - y1);

		// Swap endpoints
		if (y2 < y1)
			swap = 1;

		if (swap) {
			x = x2;
			y = y2;
			x2 = x1;
			y2 = y1;
			x1 = x;
			y1 = y;
		}

		int xoff;

		if (x2 < x1)
			xoff = -1;
		else
			xoff = 1;

		int yoff;

		if (y2 < y1)
			yoff = -1;
		else
			yoff = 1;

		if (dx >= dy) {
			minc = &y;
			mins = &y1;
			mino = &yoff;
			mind = &dy;

			maxc = &x;
			maxs = &x1;
			maxe = &x2;
			maxo = &xoff;
			maxd = &dx;
		}
		else {
			minc = &x;
			mins = &x1;
			mino = &xoff;
			mind = &dx;

			maxc = &y;
			maxs = &y1;
			maxe = &y2;
			maxo = &yoff;
			maxd = &dy;
		}

		int de = 2 * *mind - *maxd;
		int ppos = 0;

		*minc = *mins;

		for ((*maxc) = *maxs; (((*maxs) < (*maxe)) ? ((*maxc) <= (*maxe)) : ((*maxc) >= (*maxe))); (*maxc) += *maxo) {
			if (pat & (1 << ppos))
				set_pixel(rip, x, y, color);
			if (de >= 0) {
				*minc = (*minc) + *mino;
				de = de - ((2 * *maxd) & INT_MAX);
			}
			de = de + ((2 * *mind) & INT_MAX);
			ppos++;
			if (ppos == 16)
				ppos = 0;
			if (*maxs == *maxe)
				break;
		}
	}

	if (width == 3) {
		/*
		 * Offset direction and base endpoints come from the
		 * original (pre-clip) line.  Recursive set_line will
		 * CS-clip each offset independently.
		 */
		int orig_adx = abs(ldx);
		int orig_ady = abs(ldy);
		for (int side = -1; side <= 1; side += 2) {
			int ox1, oy1, ox2, oy2;
			if (orig_adx >= orig_ady) {
				ox1 = orig_x1; oy1 = orig_y1 + side;
				ox2 = orig_x2; oy2 = orig_y2 + side;
			}
			else {
				ox1 = orig_x1 + side; oy1 = orig_y1;
				ox2 = orig_x2 + side; oy2 = orig_y2;
			}
			set_line(rip, ox1, oy1, ox2, oy2, color, pat, 1);
		}
	}
}
void
cterm_rip_raster_line(struct cterm_rip_raster *rip, int x1, int y1,
    int x2, int y2)
{
	if (rip != NULL)
		draw_line(rip, x1, y1, x2, y2);
}

void
cterm_rip_raster_set_line(struct cterm_rip_raster *rip, int x1, int y1,
    int x2, int y2, uint32_t colour, uint16_t pattern, int width)
{
	if (rip != NULL)
		set_line(rip, x1, y1, x2, y2, colour, pattern, width);
}

void
cterm_rip_raster_fill_style(struct cterm_rip_raster *rip, int pattern,
    int colour)
{
	if (rip == NULL || pattern < 0 || pattern >= 12
	    || colour < 0 || colour > 255)
		return;
	rip->fill_colour = colour & 15;
	rip->fill_style = pattern;
	memcpy(rip->fill_pattern, rip_fill_patterns[pattern], 8);
}

void
cterm_rip_raster_bar(struct cterm_rip_raster *rip, int x1, int y1,
    int x2, int y2)
{
	struct st_pixel_buffer *pixels = NULL;
	struct st_pixel_blit blit;
	int left, top, right, bottom;
	uint32_t width, height;
	int status;

	if (rip == NULL)
		return;
	if (x1 > x2) {
		int temp = x1;
		x1 = x2;
		x2 = temp;
	}
	if (y1 > y2) {
		int temp = y1;
		y1 = y2;
		y2 = temp;
	}
	left = x1 + rip->viewport.sx;
	top = y1 + rip->viewport.sy;
	right = x2 + rip->viewport.sx;
	bottom = y2 + rip->viewport.sy;
	if (left < 0) left = 0;
	if (top < 0) top = 0;
	if (right > rip->viewport.ex) right = rip->viewport.ex;
	if (bottom > rip->viewport.ey) bottom = rip->viewport.ey;
	if (left > right || top > bottom || rip->error != 0)
		return;
	if (rip->x_max != rip->x_dim || rip->y_max != rip->y_dim) {
		for (int y = top; y <= bottom && rip->error == 0; y++) {
			for (int x = left; x <= right && rip->error == 0; x++) {
				int mapped_x = map_x(rip, x);
				int mapped_y = map_y(rip, y);
				int colour;

				if (mapped_x < 0 || mapped_y < 0
				    || (uint32_t)mapped_x >= rip->screen_width
				    || (uint32_t)mapped_y >= rip->screen_height)
					continue;
				colour = (rip->fill_pattern[mapped_y & 7]
				    & (0x80 >> (mapped_x & 7)))
				    ? rip->fill_colour : 0;
				rip->error = st_console_set_pixel(rip->console,
				    (uint32_t)mapped_x, (uint32_t)mapped_y,
				    map_colour(rip, colour));
			}
		}
		return;
	}
	width = (uint32_t)(right - left + 1);
	height = (uint32_t)(bottom - top + 1);
	status = st_pixel_buffer_create(&pixels, width, height, false);
	if (status != 0) {
		rip->error = status;
		return;
	}
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			int actual_x = left + (int)x;
			int actual_y = top + (int)y;
			int colour = (rip->fill_pattern[actual_y & 7]
			    & (0x80 >> (actual_x & 7))) ? rip->fill_colour : 0;

			pixels->primary[(size_t)y * width + x] = map_colour(rip, colour);
		}
	}
	blit = (struct st_pixel_blit){
		.width = width,
		.height = height,
		.destination_x = left,
		.destination_y = top,
		.scale_x = 1,
		.scale_y = 1,
	};
	rip->error = st_console_blit_pixels(rip->console, pixels, NULL, &blit);
	st_pixel_buffer_destroy(pixels);
}

static void
capture_image(struct cterm_rip_raster *rip, int x1, int y1,
    int x2, int y2)
{
	struct st_pixel_buffer *captured = NULL;
	struct st_console_rect rect;
	int temp;

	if (x1 > x2) {
		temp = x1;
		x1 = x2;
		x2 = temp;
	}
	if (y1 > y2) {
		temp = y1;
		y1 = y2;
		y2 = temp;
	}
	/* The BGI clipboard format has a 16-bit size limit. */
	if ((((size_t)(x2 - x1 + 8) / 8) * 4
	    * (size_t)(y2 - y1 + 1) + 6) > 65535) {
		rip->colour = 7;
		cterm_rip_raster_fill_style(rip, 1, 8);
		rip->line_width = 1;
		rip->line_style = 0;
		rip->line_pattern = 0xffff;
		rip->xor_mode = false;
		return;
	}
	rect = (struct st_console_rect){
		.x = (uint32_t)(x1 + rip->viewport.sx),
		.y = (uint32_t)(y1 + rip->viewport.sy),
		.width = (uint32_t)(x2 - x1 + 1),
		.height = (uint32_t)(y2 - y1 + 1),
	};
	st_pixel_buffer_destroy(rip->clipboard);
	rip->clipboard = NULL;
	rip->clipx = x1 + rip->viewport.sx;
	rip->clipy = y1 + rip->viewport.sy;
	if (rect.x >= rip->screen_width || rect.y >= rip->screen_height
	    || rect.width > rip->screen_width - rect.x
	    || rect.height > rip->screen_height - rect.y)
		return;
	rip->error = st_console_get_pixels(&captured, rip->console,
	    &rect, false);
	if (rip->error == 0)
		rip->clipboard = captured;
}

static void
paste_buffer(struct cterm_rip_raster *rip,
    struct st_pixel_buffer *source, int x, int y, int mode)
{
	struct st_pixel_buffer *destination = NULL;
	struct st_console_rect rect;
	struct st_pixel_blit blit;
	uint32_t width, height;
	int actual_x, actual_y;

	if (source == NULL || mode < 0 || mode > 4 || rip->error != 0)
		return;
	actual_x = x + rip->viewport.sx;
	actual_y = y + rip->viewport.sy;
	if (actual_x < 0 || actual_y < 0
	    || actual_x + (int)source->width - 1 > rip->viewport.ex
	    || actual_x >= rip->x_dim || actual_y >= rip->y_dim)
		return;
	width = source->width;
	height = source->height;
	if (width > (uint32_t)(rip->x_dim - actual_x))
		width = (uint32_t)(rip->x_dim - actual_x);
	if (height > (uint32_t)(rip->y_dim - actual_y))
		height = (uint32_t)(rip->y_dim - actual_y);
	/* The old bitmap backend ignores a blit extending off-screen. */
	if ((uint32_t)actual_x >= rip->screen_width
	    || (uint32_t)actual_y >= rip->screen_height
	    || width > rip->screen_width - (uint32_t)actual_x
	    || height > rip->screen_height - (uint32_t)actual_y)
		return;
	blit = (struct st_pixel_blit){
		.width = width, .height = height,
		.destination_x = actual_x, .destination_y = actual_y,
		.scale_x = 1, .scale_y = 1,
	};
	if (mode == 0) {
		rip->error = st_console_blit_pixels(rip->console, source,
		    NULL, &blit);
		return;
	}
	rect = (struct st_console_rect){
		.x = (uint32_t)actual_x, .y = (uint32_t)actual_y,
		.width = width, .height = height,
	};
	rip->error = st_console_get_pixels(&destination, rip->console,
	    &rect, false);
	if (rip->error != 0)
		return;
	for (uint32_t row = 0; row < height; row++) {
		for (uint32_t col = 0; col < width; col++) {
			size_t target = (size_t)row * width + col;
			size_t origin = (size_t)row * source->width + col;
			int image = pixel_colour(rip, source->primary[origin]);
			int screen = pixel_colour(rip, destination->primary[target]);
			int alternate = pixel_colour(rip,
			    destination->alternate[target]);
			int combined;
			int combined_alternate;

			switch (mode) {
			case 1:
				combined = screen ^ image;
				combined_alternate = alternate ^ image;
				break;
			case 2:
				combined = screen | image;
				combined_alternate = alternate | image;
				break;
			case 3:
				combined = screen & image;
				combined_alternate = alternate & image;
				break;
			default:
				combined = image ^ 15;
				combined_alternate = combined;
				break;
			}
			destination->primary[target] = map_colour(rip,
			    combined & 15);
			destination->alternate[target] = map_colour(rip,
			    combined_alternate & 15);
		}
	}
	rip->error = st_console_blit_pixels(rip->console, destination,
	    NULL, &blit);
	st_pixel_buffer_destroy(destination);
}

static void
paste_image(struct cterm_rip_raster *rip, int x, int y, int mode)
{
	paste_buffer(rip, rip->clipboard, x, y, mode);
}

int
cterm_rip_raster_invert_rectangle(struct cterm_rip_raster *rip,
    int x1, int y1, int x2, int y2)
{
	struct st_pixel_buffer *pixels = NULL;
	struct st_console_rect rect;
	struct st_pixel_blit blit;
	int status;

	if (rip == NULL || x1 < 0 || y1 < 0 || x2 < x1 || y2 < y1
	    || (uint32_t)x2 >= rip->screen_width
	    || (uint32_t)y2 >= rip->screen_height)
		return IO_ERROR_INVALID_ARGUMENT;
	rect = (struct st_console_rect){
		.x = (uint32_t)x1, .y = (uint32_t)y1,
		.width = (uint32_t)(x2 - x1 + 1),
		.height = (uint32_t)(y2 - y1 + 1),
	};
	status = st_console_get_pixels(&pixels, rip->console, &rect, false);
	if (status != 0)
		return status;
	for (size_t pixel = 0; pixel < (size_t)rect.width * rect.height;
	    pixel++) {
		uint32_t *planes[2] = {
			&pixels->primary[pixel], &pixels->alternate[pixel]
		};

		for (size_t plane = 0; plane < 2; plane++) {
			uint32_t colour = *planes[plane] & UINT32_C(0x80ffffff);

			for (int index = 0; index < 16; index++) {
				if (colour == (map_colour(rip, index)
				    & UINT32_C(0x80ffffff))) {
					*planes[plane] = map_colour(rip, index ^ 15);
					break;
				}
			}
		}
	}
	blit = (struct st_pixel_blit){
		.width = rect.width, .height = rect.height,
		.destination_x = x1, .destination_y = y1,
		.scale_x = 1, .scale_y = 1,
	};
	status = st_console_blit_pixels(rip->console, pixels, NULL, &blit);
	st_pixel_buffer_destroy(pixels);
	return status;
}

static bool
icon_name_equal(const char *name, const char *builtin)
{
	size_t index = 0;

	while (name[index] != '\0' && builtin[index] != '\0') {
		char left = name[index];
		char right = builtin[index];

		if (left >= 'a' && left <= 'z') left -= 'a' - 'A';
		if (right >= 'a' && right <= 'z') right -= 'a' - 'A';
		if (left != right)
			return false;
		index++;
	}
	if (name[index] == '\0' && builtin[index] == '\0')
		return true;
	return name[index] == '\0' && strchr(name, '.') == NULL
	    && strcmp(builtin + index, ".ICN") == 0;
}

static int rip_text_width(struct cterm_rip_raster *rip, const char *text);
static void draw_rip_text(struct cterm_rip_raster *rip, const char *text);

static void
draw_missing_icon(struct cterm_rip_raster *rip, int x, int y,
    bool to_clipboard, const char *filename, int *out_width,
    int *out_height)
{
	char label[280];
	const char unavailable[] = "not available";
	int saved_font = rip->font_number;
	int saved_size = rip->font_size;
	int saved_vertical = rip->font_vertical;
	int saved_colour = rip->colour;
	uint16_t saved_pattern = rip->line_pattern;
	int label_width, unavailable_width, width;
	int x2, y2;

	if (strchr(filename, '.') == NULL)
		(void)snprintf(label, sizeof(label), "Icon %s.ICN", filename);
	else
		(void)snprintf(label, sizeof(label), "Icon %s", filename);
	for (char *name = label + 5; *name != '\0'; name++)
		*name = (char)ascii_to_upper((unsigned char)*name);
	rip->font_number = 2;
	rip->font_size = 4;
	rip->font_vertical = 0;
	rip->colour = 12;
	label_width = rip_text_width(rip, label);
	unavailable_width = rip_text_width(rip, unavailable);
	width = label_width > unavailable_width
	    ? label_width : unavailable_width;
	x2 = x + width + 24;
	y2 = y + 37;
	if (out_width != NULL)
		*out_width = x2 - x + 1;
	if (out_height != NULL)
		*out_height = y2 - y + 1;
	rip->line_pattern = 0xcccc;
	rip->line_width = 1;
	set_line(rip, x, y, x2, y, map_colour(rip, 12), 0xcccc, 1);
	set_line(rip, x2, y, x2, y2, map_colour(rip, 12), 0xcccc, 1);
	set_line(rip, x2, y2, x, y2, map_colour(rip, 12), 0xcccc, 1);
	set_line(rip, x, y2, x, y, map_colour(rip, 12), 0xcccc, 1);
	rip->x = x + (x2 - x + 1 - label_width) / 2;
	rip->y = y + 6;
	draw_rip_text(rip, label);
	rip->x = x + (x2 - x + 1 - unavailable_width) / 2;
	rip->y = y + 20;
	draw_rip_text(rip, unavailable);
	if (to_clipboard) {
		int left = x + rip->viewport.sx;
		int top = y + rip->viewport.sy;
		int right = x2 + rip->viewport.sx;
		int bottom = y2 + rip->viewport.sy;

		st_pixel_buffer_destroy(rip->clipboard);
		rip->clipboard = NULL;
		rip->clipx = left;
		rip->clipy = top;
		if (left >= 0 && top >= 0
		    && (uint32_t)right < rip->screen_width
		    && (uint32_t)bottom < rip->screen_height) {
			const struct st_console_rect rect = {
				.x = (uint32_t)left, .y = (uint32_t)top,
				.width = (uint32_t)(right - left + 1),
				.height = (uint32_t)(bottom - top + 1),
			};
			struct st_pixel_buffer *captured = NULL;
			int status = st_console_get_pixels(&captured, rip->console,
			    &rect, false);

			if (status == 0)
				rip->clipboard = captured;
			else if (status == IO_ERROR_OUT_OF_MEMORY)
				rip->error = status;
		}
	}
	rip->colour = saved_colour;
	rip->font_number = saved_font;
	rip->font_size = saved_size;
	rip->font_vertical = saved_vertical;
	rip->line_pattern = saved_pattern;
}

static struct st_pixel_buffer *
load_file_icon(struct cterm_rip_raster *rip, const char *filename)
{
	struct st_pixel_buffer *pixels = NULL;
	uint8_t header[4], *bytes = NULL, *colours = NULL;
	uint32_t width, height;
	size_t row_bytes, file_size;
	FILE *stream = cterm_rip_asset_open_read(rip->rip_directory,
	    filename, true);

	if (stream == NULL)
		return NULL;
	if (fread(header, 1, sizeof(header), stream) != sizeof(header))
		goto finished;
	width = (uint32_t)header[0] + ((uint32_t)header[1] << 8) + 1U;
	height = (uint32_t)header[2] + ((uint32_t)header[3] << 8) + 1U;
	row_bytes = (width + 7U) / 8U;
	if (height > (65535U - 6U) / (4U * row_bytes))
		goto finished;
	file_size = 4U + 4U * row_bytes * height;
	bytes = malloc(file_size);
	if (bytes == NULL) {
		rip->error = IO_ERROR_OUT_OF_MEMORY;
		goto finished;
	}
	memcpy(bytes, header, sizeof(header));
	if (fread(bytes + 4, 1, file_size - 4, stream) != file_size - 4)
		goto finished;
	{
		int status = cterm_rip_icon_decode(&colours, &width, &height,
		    bytes, file_size);

		if (status == IO_ERROR_OUT_OF_MEMORY)
			rip->error = status;
		if (status != 0)
			goto finished;
	}
	rip->error = st_pixel_buffer_create(&pixels, width, height, false);
	if (rip->error != 0)
		goto finished;
	for (size_t i = 0; i < (size_t)width * height; i++)
		pixels->primary[i] = map_colour(rip, colours[i]);
finished:
	if (stream != NULL)
		fclose(stream);
	free(colours);
	free(bytes);
	return pixels;
}

static void
write_icon(struct cterm_rip_raster *rip, const char *filename)
{
	struct fs_info info;
	uint8_t *colours = NULL, *bytes = NULL;
	char *path = NULL;
	size_t count, size;
	int status;

	if (rip->clipboard == NULL || rip->rip_directory == NULL)
		return;
	path = cterm_rip_asset_path(rip->rip_directory, filename, false);
	if (path == NULL)
		return;
	status = fs_get_link_info(&info, rip->rip_directory);
	if (status == IO_ERROR_NOT_FOUND) {
		status = fs_create_directories(rip->rip_directory);
		if (status == 0)
			status = fs_get_link_info(&info, rip->rip_directory);
	}
	if (status != 0 || info.kind != FS_ENTRY_DIRECTORY)
		goto finished;
	status = fs_get_link_info(&info, path);
	if (status != IO_ERROR_NOT_FOUND
	    && (status != 0 || info.kind != FS_ENTRY_FILE))
		goto finished;
	count = (size_t)rip->clipboard->width * rip->clipboard->height;
	colours = malloc(count);
	if (colours == NULL) {
		rip->error = IO_ERROR_OUT_OF_MEMORY;
		goto finished;
	}
	for (size_t i = 0; i < count; i++)
		colours[i] = (uint8_t)pixel_colour(rip,
		    rip->clipboard->primary[i]);
	status = cterm_rip_icon_encode(&bytes, &size, colours,
	    rip->clipboard->width, rip->clipboard->height);
	if (status == IO_ERROR_OUT_OF_MEMORY)
		rip->error = status;
	if (status == 0)
		(void)fs_replace_file(path, bytes, size);
finished:
	free(bytes);
	free(colours);
	free(path);
}

static bool
load_icon(struct cterm_rip_raster *rip, int x, int y,
    int mode, bool to_clipboard, const char *filename,
    int *out_width, int *out_height)
{
	const struct builtin_icon *icon = NULL;
	struct st_pixel_buffer *pixels;

	if (out_width != NULL)
		*out_width = 0;
	if (out_height != NULL)
		*out_height = 0;
	if (mode < 0 || mode > 4 || filename[0] == '\0'
	    || strstr(filename, "..") != NULL
	    || strchr(filename, '/') != NULL
	    || strchr(filename, '\\') != NULL)
		return false;
	pixels = load_file_icon(rip, filename);
	if (rip->error != 0)
		return false;
	if (pixels != NULL)
		goto stamp;
	for (size_t i = 0; i < builtin_icon_count; i++) {
		if (icon_name_equal(filename, builtin_icons[i].filename)) {
			icon = &builtin_icons[i];
			break;
		}
	}
	if (icon == NULL) {
		draw_missing_icon(rip, x, y, to_clipboard, filename,
		    out_width, out_height);
		return false;
	}
	if (x + rip->viewport.sx + icon->width - 1
	    > rip->viewport.ex || y + rip->viewport.sy + icon->height - 1
	    > rip->viewport.ey)
		return false;
	rip->error = st_pixel_buffer_create(&pixels,
	    icon->width, icon->height, false);
	if (rip->error != 0)
		return false;
	for (size_t i = 0; i < (size_t)icon->width * icon->height; i++) {
		uint8_t byte = icon->data[i / 2];
		int colour = (i & 1) != 0 ? byte & 15 : byte >> 4;

		pixels->primary[i] = map_colour(rip, colour);
	}
stamp:
	if (x + rip->viewport.sx + (int)pixels->width - 1
	    > rip->viewport.ex || y + rip->viewport.sy + (int)pixels->height - 1
	    > rip->viewport.ey) {
		st_pixel_buffer_destroy(pixels);
		return false;
	}
	if (out_width != NULL)
		*out_width = (int)pixels->width;
	if (out_height != NULL)
		*out_height = (int)pixels->height;
	paste_buffer(rip, pixels, x, y, mode);
	if (to_clipboard) {
		st_pixel_buffer_destroy(rip->clipboard);
		rip->clipboard = pixels;
		rip->clipx = x + rip->viewport.sx;
		rip->clipy = y + rip->viewport.sy;
	}
	else
		st_pixel_buffer_destroy(pixels);
	return true;
}

static void
erase_view(struct cterm_rip_raster *rip)
{
	struct st_pixel_buffer *pixels = NULL;
	struct st_pixel_blit blit;
	uint32_t width, height;

	if (no_viewport(rip) || rip->error != 0)
		return;
	if (rip->x_max != rip->x_dim || rip->y_max != rip->y_dim) {
		for (int y = 0; y <= rip->viewport.ey - rip->viewport.sy
		    && rip->error == 0; y++) {
			for (int x = 0; x <= rip->viewport.ex - rip->viewport.sx
			    && rip->error == 0; x++)
				set_pixel(rip, x, y, map_colour(rip, 0));
		}
		return;
	}
	width = (uint32_t)(rip->viewport.ex - rip->viewport.sx + 1);
	height = (uint32_t)(rip->viewport.ey - rip->viewport.sy + 1);
	rip->error = st_pixel_buffer_create(&pixels, width, height, false);
	if (rip->error != 0)
		return;
	rip->error = st_pixel_buffer_fill(pixels, map_colour(rip, 0), 0);
	if (rip->error == 0) {
		blit = (struct st_pixel_blit){
			.width = width, .height = height,
			.destination_x = rip->viewport.sx,
			.destination_y = rip->viewport.sy,
			.scale_x = 1, .scale_y = 1,
		};
		rip->error = st_console_blit_pixels(rip->console, pixels,
		    NULL, &blit);
	}
	st_pixel_buffer_destroy(pixels);
}

int
cterm_rip_raster_reset_windows(struct cterm_rip_raster *rip)
{
	if (rip == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	refresh_screen_size(rip);
	if (rip->error != 0)
		return rip->error;
	rip->x_dim = 640;
	rip->y_dim = 350;
	rip->x_max = rip->screen_width < 640 ? (int)rip->screen_width : 640;
	rip->y_max = rip->screen_height < 350 ? (int)rip->screen_height : 350;
	rip->viewport.sx = 0;
	rip->viewport.sy = 0;
	rip->viewport.ex = 639;
	rip->viewport.ey = 349;
	rip->borders = true;
	memcpy(rip->master_palette, initial_ega_palette,
	    sizeof(rip->master_palette));
	set_master_palette(rip);
	erase_view(rip);
	return rip->error;
}

void
cterm_rip_raster_rectangle(struct cterm_rip_raster *rip,
    int x1, int y1, int x2, int y2)
{
	if (rip == NULL)
		return;
	draw_line(rip, x1, y1, x2, y1);
	draw_line(rip, x2, y1, x2, y2);
	draw_line(rip, x2, y2, x1, y2);
	draw_line(rip, x1, y2, x1, y1);
}

struct rip_point {
	int x;
	int y;
};

#define RIP_POLY_SEPARATOR INT_MIN

static bool
poly_separator(struct rip_point point)
{
	return point.x == RIP_POLY_SEPARATOR;
}

/* Preserve BGI's degenerate-vertex and sub-polygon closing rules. */
static size_t
collect_vertices(const struct rip_point *raw, int count,
    struct rip_point *vertices, bool close_poly)
{
	size_t used = 0;
	int vertex_count = 0;
	int first = 0;

	for (int i = 0; i < count; i++) {
		bool match = raw[i].x == raw[first].x
		    && raw[i].y == raw[first].y;

		if (vertex_count == 0) {
			vertices[used++] = raw[i];
			vertex_count = 1;
			first = i;
		}
		else if (match && vertex_count == 1)
			continue;
		else if (match) {
			vertices[used++] = raw[i];
			vertices[used++] = (struct rip_point){RIP_POLY_SEPARATOR, 0};
			vertex_count = 0;
		}
		else {
			vertices[used++] = raw[i];
			vertex_count++;
		}
	}
	if (close_poly)
		vertices[used++] = raw[0];
	vertices[used++] = (struct rip_point){RIP_POLY_SEPARATOR, 0};
	return used;
}

static void
draw_poly_outline(struct cterm_rip_raster *rip,
    const struct rip_point *vertices, size_t count)
{
	for (size_t i = 0; i + 1 < count && rip->error == 0; i++) {
		if (poly_separator(vertices[i])
		    || poly_separator(vertices[i + 1]))
			continue;
		draw_line(rip, vertices[i].x, vertices[i].y,
		    vertices[i + 1].x, vertices[i + 1].y);
	}
}

static void
fill_pixel(struct cterm_rip_raster *rip, struct st_pixel_buffer *pixels,
    struct st_pixel_mask *mask, int x, int y)
{
	int actual_x = x + rip->viewport.sx;
	int actual_y = y + rip->viewport.sy;
	int mapped_x, mapped_y;
	int colour;

	if (x < 0 || y < 0 || actual_x < 0 || actual_y < 0
	    || actual_x > rip->viewport.ex || actual_y > rip->viewport.ey)
		return;
	mapped_x = map_x(rip, actual_x);
	mapped_y = map_y(rip, actual_y);
	if (mapped_x < 0 || mapped_y < 0
	    || (uint32_t)mapped_x >= pixels->width
	    || (uint32_t)mapped_y >= pixels->height)
		return;
	colour = (rip->fill_pattern[mapped_y & 7]
	    & (0x80 >> (mapped_x & 7))) ? rip->fill_colour : 0;
	pixels->primary[(size_t)mapped_y * pixels->width + (size_t)mapped_x]
	    = map_colour(rip, colour);
	rip->error = st_pixel_mask_set(mask, (uint32_t)mapped_x,
	    (uint32_t)mapped_y, true);
}

static void
scanline_poly_fill(struct cterm_rip_raster *rip,
    const struct rip_point *vertices, size_t count)
{
	int min_y = INT_MAX;
	int max_y = INT_MIN;
	int *intersections;
	struct st_pixel_buffer *pixels = NULL;
	struct st_pixel_mask *mask = NULL;
	const struct st_pixel_blit blit = {
		.width = (uint32_t)rip->x_max,
		.height = (uint32_t)rip->y_max,
		.scale_x = 1, .scale_y = 1,
	};

	if (rip->fill_style == 0 || count < 2)
		return;
	for (size_t i = 0; i < count; i++) {
		if (poly_separator(vertices[i]))
			continue;
		if (vertices[i].y < min_y) min_y = vertices[i].y;
		if (vertices[i].y > max_y) max_y = vertices[i].y;
	}
	if (min_y > max_y)
		return;
	intersections = malloc(count * sizeof(*intersections));
	if (intersections == NULL) {
		rip->error = IO_ERROR_OUT_OF_MEMORY;
		return;
	}
	rip->error = st_pixel_buffer_create(&pixels,
	    (uint32_t)rip->x_max, (uint32_t)rip->y_max, false);
	if (rip->error == 0)
		rip->error = st_pixel_mask_create(&mask,
		    (uint32_t)rip->x_max, (uint32_t)rip->y_max);
	if (rip->error != 0) {
		st_pixel_buffer_destroy(pixels);
		st_pixel_mask_destroy(mask);
		free(intersections);
		return;
	}
	for (int y = min_y; y <= max_y && rip->error == 0; y++) {
		int found = 0;

		for (size_t i = 0; i + 1 < count; i++) {
			int x0, x1, y0, y1, ix, j;

			if (poly_separator(vertices[i])
			    || poly_separator(vertices[i + 1]))
				continue;
			x0 = vertices[i].x;
			y0 = vertices[i].y;
			x1 = vertices[i + 1].x;
			y1 = vertices[i + 1].y;
			if (y1 < y0) {
				int temp = y0;
				y0 = y1;
				y1 = temp;
				temp = x0;
				x0 = x1;
				x1 = temp;
			}
			if (y0 == y1 || y < y0 || y >= y1)
				continue;
			ix = x0 + (long)(y - y0) * (x1 - x0) / (y1 - y0);
			j = found;
			while (j > 0 && intersections[j - 1] > ix) {
				intersections[j] = intersections[j - 1];
				j--;
			}
			intersections[j] = ix;
			found++;
		}
		for (int i = 0; i + 1 < found && rip->error == 0; i += 2) {
			for (int x = intersections[i]; x <= intersections[i + 1]
			    && rip->error == 0; x++)
				fill_pixel(rip, pixels, mask, x, y);
		}
	}
	if (rip->error == 0)
		rip->error = st_console_blit_pixels(rip->console, pixels,
		    mask, &blit);
	st_pixel_mask_destroy(mask);
	st_pixel_buffer_destroy(pixels);
	free(intersections);
}

static void
draw_polygon(struct cterm_rip_raster *rip, const struct rip_point *points,
    int count, bool fill)
{
	struct rip_point *raw;
	struct rip_point *vertices;
	size_t used;

	if (no_viewport(rip) || count < 2 || count > 512 || rip->error != 0)
		return;
	raw = malloc((size_t)(count + 1) * sizeof(*raw));
	vertices = malloc((size_t)(count * 2 + 6) * sizeof(*vertices));
	if (raw == NULL || vertices == NULL) {
		free(raw);
		free(vertices);
		rip->error = IO_ERROR_OUT_OF_MEMORY;
		return;
	}
	memcpy(raw, points, (size_t)count * sizeof(*raw));
	raw[count] = raw[0];
	used = collect_vertices(raw, count + 1, vertices, fill);
	if (fill) {
		scanline_poly_fill(rip, vertices, used);
		if (rip->colour != 0)
			draw_poly_outline(rip, vertices, used);
	}
	else
		draw_poly_outline(rip, vertices, used);
	free(vertices);
	free(raw);
}

/* BGI's quadrant metric and Q1.15 trigonometry, retained as integer math. */
static int
bgi_metric(int ax_val, int bx_val)
{
	bx_val = -bx_val;
	if (ax_val >= 0) {
		if (bx_val >= 0)
			return bx_val - ax_val;
		return ax_val + 6000 + bx_val;
	}
	if (bx_val >= 0)
		return -ax_val + 2000 - bx_val;
	return ax_val + 4000 - bx_val;
}

static const int16_t bgi_sin_table[91] = {
	0x0000, 0x023B, 0x0477, 0x06B2, 0x08ED, 0x0B27, 0x0D61, 0x0F99, 0x11D0, 0x1406,
	0x163A, 0x186C, 0x1A9C, 0x1CCB, 0x1EF7, 0x2120, 0x2348, 0x256C, 0x278D, 0x29AC,
	0x2BC7, 0x2DDF, 0x2FF3, 0x3203, 0x340F, 0x3618, 0x381C, 0x3A1C, 0x3C17, 0x3E0E,
	0x4000, 0x41EC, 0x43D4, 0x45B6, 0x4793, 0x496A, 0x4B3C, 0x4D08, 0x4ECD, 0x508D,
	0x5246, 0x53F9, 0x55A6, 0x574B, 0x58EA, 0x5A82, 0x5C13, 0x5D9C, 0x5F1F, 0x609A,
	0x620D, 0x6379, 0x64DD, 0x6639, 0x678D, 0x68D9, 0x6A1D, 0x6B59, 0x6C8C, 0x6DB7,
	0x6ED9, 0x6FF3, 0x7104, 0x720C, 0x730B, 0x7401, 0x74EF, 0x75D3, 0x76AD, 0x777F,
	0x7847, 0x7906, 0x79BC, 0x7A68, 0x7B0A, 0x7BA3, 0x7C32, 0x7CB8, 0x7D33, 0x7DA5,
	0x7E0E, 0x7E6C, 0x7EC1, 0x7F0B, 0x7F4C, 0x7F83, 0x7FB0, 0x7FD3, 0x7FEC, 0x7FFB,
	(int16_t)0x8000,
};

static int32_t
bgi_sin32(int angle_deg)
{
	int negative = 0;
	int32_t value;

	angle_deg %= 360;
	if (angle_deg < 0)
		angle_deg += 360;
	if (angle_deg > 180) {
		angle_deg -= 180;
		negative = 1;
	}
	if (angle_deg > 90)
		angle_deg = 180 - angle_deg;
	value = (int32_t)(uint16_t)bgi_sin_table[angle_deg] << 1;
	return negative ? -value : value;
}

static int32_t
bgi_cos32(int angle_deg)
{
	return bgi_sin32(angle_deg + 90);
}

static int16_t
bgi_trig_mul(int32_t trig32, int16_t radius)
{
	uint16_t lo = (uint16_t)(trig32 & 0xffff);
	uint16_t hi = (uint16_t)((trig32 >> 16) & 0xffff);
	uint32_t first = (uint32_t)hi * (uint16_t)radius;
	uint32_t second = (uint32_t)lo * (uint16_t)radius;

	return (int16_t)((first & 0xffff) + (second >> 16));
}

static int
bgi_arc_limit(int a, int b, int angle_deg)
{
	int16_t sx = bgi_trig_mul(bgi_cos32(angle_deg), (int16_t)a);
	int16_t sy = -bgi_trig_mul(bgi_sin32(angle_deg), (int16_t)b);

	return bgi_metric(sx, sy);
}

static bool
bgi_arc_in_range(int metric, int start_limit, int end_limit, bool inverse)
{
	if (!inverse)
		return metric >= start_limit && metric <= end_limit;
	return metric >= start_limit || metric <= end_limit;
}

struct arc_points {
	struct rip_point *points;
	size_t count;
	size_t capacity;
};

static void
arc_add(struct cterm_rip_raster *rip, struct arc_points *arc, int x, int y)
{
	struct rip_point *grown;
	size_t capacity;

	if (arc == NULL || rip->error != 0)
		return;
	if (arc->count == arc->capacity) {
		capacity = arc->capacity == 0 ? 64 : arc->capacity * 2;
		if (capacity > SIZE_MAX / sizeof(*grown)) {
			rip->error = IO_ERROR_OUT_OF_MEMORY;
			return;
		}
		grown = realloc(arc->points, capacity * sizeof(*grown));
		if (grown == NULL) {
			rip->error = IO_ERROR_OUT_OF_MEMORY;
			return;
		}
		arc->points = grown;
		arc->capacity = capacity;
	}
	arc->points[arc->count++] = (struct rip_point){x, y};
}

static void
ellipse_plot(struct cterm_rip_raster *rip, struct st_pixel_buffer *pixels,
    struct st_pixel_mask *mask, int xc, int yc, int x, int y,
    int start_limit, int end_limit, bool fill, bool inverse,
    uint32_t colour, int pass, struct arc_points *arc)
{
	if (rip->error != 0)
		return;
	if (pass == 0) {
		if (fill) {
			for (int fx = xc - x; fx <= xc + x && rip->error == 0;
			    fx++) {
				if (y == 0)
					fill_pixel(rip, pixels, mask, fx, yc);
				else {
					fill_pixel(rip, pixels, mask, fx, yc + y);
					fill_pixel(rip, pixels, mask, fx, yc - y);
				}
			}
		}
		return;
	}
	if (arc != NULL) {
		if (bgi_arc_in_range(y - x, start_limit, end_limit, inverse))
			arc_add(rip, arc, xc + x, yc - y);
		if (bgi_arc_in_range(x - y + 2000, start_limit, end_limit, inverse))
			arc_add(rip, arc, xc - x, yc - y);
		if (bgi_arc_in_range(y - x + 4000, start_limit, end_limit, inverse))
			arc_add(rip, arc, xc - x, yc + y);
		if (bgi_arc_in_range(x - y + 6000, start_limit, end_limit, inverse))
			arc_add(rip, arc, xc + x, yc + y);
	}
	if (!rip->borders || rip->line_width == 3)
		return;
	if (bgi_arc_in_range(y - x, start_limit, end_limit, inverse))
		set_pixel(rip, xc + x, yc - y, colour);
	if (bgi_arc_in_range(x - y + 2000, start_limit, end_limit, inverse))
		set_pixel(rip, xc - x, yc - y, colour);
	if (bgi_arc_in_range(y - x + 4000, start_limit, end_limit, inverse))
		set_pixel(rip, xc - x, yc + y, colour);
	if (bgi_arc_in_range(x - y + 6000, start_limit, end_limit, inverse))
		set_pixel(rip, xc + x, yc + y, colour);
}

static void
full_ellipse(struct cterm_rip_raster *rip, int xc, int yc,
    int start_angle, int end_angle, int radius_x, int radius_y,
    bool fill, uint32_t colour, struct arc_points *arc)
{
	bool thick_sweep = rip->line_width == 3 && rip->borders && !fill;
	int original_x = radius_x;
	int original_y = radius_y;
	int start_limit, end_limit, x, y;
	bool inverse;
	long bigger, square100, step, step_const, temp, error;
	long delta, increment, accumulated, threshold;
	struct st_pixel_buffer *pixels = NULL;
	struct st_pixel_mask *mask = NULL;
	const struct st_pixel_blit fill_blit = {
		.width = (uint32_t)rip->x_max,
		.height = (uint32_t)rip->y_max,
		.scale_x = 1, .scale_y = 1,
	};

	if (rip->error != 0 || no_viewport(rip))
		return;
	if (start_angle % 360 == end_angle % 360
	    && !(start_angle == 0 && end_angle == 360) && !thick_sweep)
		return;
	if (radius_x == 0) radius_x = 1;
	if (radius_y == 0) radius_y = 1;
	start_limit = bgi_arc_limit(radius_x, radius_y, start_angle);
	end_limit = bgi_arc_limit(radius_x, radius_y, end_angle);
	inverse = end_limit <= start_limit;
	bigger = radius_x > radius_y ? radius_x : radius_y;
	square100 = bigger * bigger * 100;
	step = square100 / radius_x / radius_x;
	step_const = square100 / radius_y / radius_y;
	temp = step_const * radius_y;
	if (fill) {
		rip->error = st_pixel_buffer_create(&pixels,
		    (uint32_t)rip->x_max, (uint32_t)rip->y_max, false);
		if (rip->error == 0)
			rip->error = st_pixel_mask_create(&mask,
			    (uint32_t)rip->x_max, (uint32_t)rip->y_max);
		if (rip->error != 0)
			goto cleanup;
	}
	for (int pass = 0; pass < 2 && rip->error == 0; pass++) {
		if (pass == 0 && !fill)
			continue;
		if (pass == 1 && thick_sweep)
			continue;
		x = 0;
		y = radius_y;
		error = temp * radius_y - square100;
		delta = 2 * temp;
		increment = 0;
		accumulated = 0;
		threshold = 0;

#define update_incr() (increment = accumulated + step, threshold = delta - step_const)
#define do_inc_x() (x++, error += increment, accumulated = increment + step)
#define do_dec_y() (y--, error -= threshold, delta = threshold - step_const)
#define plot_4() ellipse_plot(rip, pixels, mask, xc, yc, x, y, \
	start_limit, end_limit, fill, inverse, colour, pass, arc)
		plot_4();
		update_incr();
		if (2 * error + 2 * increment >= threshold)
			do_dec_y();
		do_inc_x();
		while (accumulated < delta && rip->error == 0) {
			plot_4();
			update_incr();
			if (2 * error + 2 * increment >= threshold)
				do_dec_y();
			do_inc_x();
		}
		while (y >= 0 && rip->error == 0) {
			plot_4();
			update_incr();
			if (increment / 2 + error <= threshold)
				do_inc_x();
			do_dec_y();
		}
#undef update_incr
#undef do_inc_x
#undef do_dec_y
#undef plot_4
		if (pass == 0 && rip->error == 0)
			rip->error = st_console_blit_pixels(rip->console,
			    pixels, mask, &fill_blit);
	}
	if (rip->line_width == 3 && rip->borders && rip->error == 0) {
		int sweep_end = end_angle;
		uint16_t saved_pattern = rip->line_pattern;
		int previous_x, previous_y;
		bool normal_drawn = false;
		bool duplicate_drawn = false;

		if (start_angle >= end_angle)
			sweep_end = end_angle + 360;
		rip->line_pattern = 0xffff;
		previous_x = xc + bgi_trig_mul(bgi_cos32(start_angle),
		    (int16_t)original_x);
		previous_y = yc - bgi_trig_mul(bgi_sin32(start_angle),
		    (int16_t)original_y);
		for (int degree = start_angle + 1;
		    degree <= sweep_end && rip->error == 0; degree++) {
			int current_x = xc + bgi_trig_mul(bgi_cos32(degree),
				(int16_t)original_x);
			int current_y = yc - bgi_trig_mul(bgi_sin32(degree),
				(int16_t)original_y);

			if (current_x != previous_x || current_y != previous_y) {
				draw_line(rip, previous_x, previous_y,
				    current_x, current_y);
				previous_x = current_x;
				previous_y = current_y;
				normal_drawn = true;
				duplicate_drawn = false;
			}
			else if (normal_drawn && !duplicate_drawn) {
				draw_line(rip, current_x, current_y,
				    current_x, current_y);
				duplicate_drawn = true;
			}
		}
		rip->line_pattern = saved_pattern;
		if (rip->version == 1 && rip->line_style == 4)
			rip->line_pattern = 0x8000;
	}
cleanup:
	st_pixel_mask_destroy(mask);
	st_pixel_buffer_destroy(pixels);
}

static void
draw_pie(struct cterm_rip_raster *rip, int xc, int yc,
    int start_angle, int end_angle, int radius_x, int radius_y,
    bool circular)
{
	struct arc_points arc = {0};
	uint32_t colour = map_colour(rip, rip->colour);

	if (no_viewport(rip)
	    || (!circular && radius_x == 0 && radius_y == 0))
		return;
	if (circular && start_angle > end_angle) {
		int temp = start_angle;
		start_angle = end_angle;
		end_angle = temp;
	}
	for (int sub_start = start_angle;
	    sub_start < end_angle && rip->error == 0; ) {
		int boundary = ((sub_start / 90) + 1) * 90;
		int sub_end = boundary < end_angle ? boundary : end_angle;

		if (sub_end - sub_start >= 2) {
			bool borders = rip->borders;
			size_t count;

			arc.count = 0;
			rip->borders = false;
			full_ellipse(rip, xc, yc, sub_start, sub_end,
			    radius_x, radius_y, false, colour, &arc);
			rip->borders = borders;
			count = arc.count;
			if (count != 0 && rip->error == 0) {
				arc_add(rip, &arc, xc, yc);
				arc_add(rip, &arc, arc.points[0].x,
				    arc.points[0].y);
				if (rip->error == 0)
					scanline_poly_fill(rip, arc.points,
					    arc.count);
				arc.count = count;
				if (borders) {
					for (size_t i = 1; i < count
					    && rip->error == 0; i++)
						draw_line(rip,
						    arc.points[i - 1].x,
						    arc.points[i - 1].y,
						    arc.points[i].x,
						    arc.points[i].y);
				}
			}
		}
		sub_start = sub_end;
	}
	if (rip->borders && rip->error == 0) {
		int ex = xc + bgi_trig_mul(bgi_cos32(start_angle),
		    (int16_t)radius_x);
		int ey = yc - bgi_trig_mul(bgi_sin32(start_angle),
		    (int16_t)radius_y);

		draw_line(rip, xc, yc, ex, ey);
		ex = xc + bgi_trig_mul(bgi_cos32(end_angle),
		    (int16_t)radius_x);
		ey = yc - bgi_trig_mul(bgi_sin32(end_angle),
		    (int16_t)radius_y);
		draw_line(rip, xc, yc, ex, ey);
	}
	free(arc.points);
}

static void
draw_bezier(struct cterm_rip_raster *rip, const int *points, int segments)
{
	double step = 1.0 / segments;
	int pattern_position = 0;

	rip->x = points[0];
	rip->y = points[1];
	draw_line_to(rip, points[0], points[1], &pattern_position);
	for (double t = step; t <= 1.0 && rip->error == 0; t += step) {
		double squared = t * t;
		double cubed = t * squared;
		double remainder = 1.0 - t;
		double remainder_squared = remainder * remainder;
		double remainder_cubed = remainder_squared * remainder;
		double second = 3.0 * t * remainder_squared;
		double third = 3.0 * squared * remainder;
		int x = remainder_cubed * points[0] + second * points[2]
		    + third * points[4] + cubed * points[6];
		int y = remainder_cubed * points[1] + second * points[3]
		    + third * points[5] + cubed * points[7];

		pattern_position = 0;
		draw_line_to(rip, x, y, &pattern_position);
	}
	pattern_position = 0;
	draw_line_to(rip, points[6], points[7], &pattern_position);
}

struct flood_seed {
	int x;
	int y;
	int direction;
};

struct flood_state {
	struct cterm_rip_raster *rip;
	struct st_pixel_buffer *pixels;
	uint32_t edge;
	uint32_t foreground;
	uint32_t background;
	struct flood_seed seeds[492];
	size_t seed_count;
	bool found_adjacent;
	int previous_y;
	int previous_left;
	int previous_right;
};

static bool
flood_push(struct flood_state *state, int x, int y, int direction)
{
	if (state->seed_count == sizeof(state->seeds) / sizeof(state->seeds[0]))
		return false;
	state->seeds[state->seed_count++] = (struct flood_seed){x, y, direction};
	return direction != 0;
}

static bool
flood_scan_adjacent(struct flood_state *state, int left_edge,
    int right_edge, int y, int direction)
{
	struct cterm_rip_raster *rip = state->rip;
	struct st_pixel_buffer *pixels = state->pixels;
	uint8_t pattern_row = rip->fill_pattern[(rip->viewport.sy + y) & 7];
	bool in_span = false;
	bool found_directional = false;
	int scan_left = left_edge == 0 ? 1 : left_edge;
	int scan_right = right_edge == (int)pixels->width - 1
	    ? (int)pixels->width - 2 : right_edge;
	int x = scan_left;

	do {
		uint32_t pixel = pixels->primary[(size_t)y * pixels->width + x]
		    & 0x80ffffff;

		if (pixel == state->edge) {
			in_span = false;
			goto advance;
		}
		if (in_span)
			goto advance;
		if (pattern_row == 0 || state->foreground == state->background) {
			in_span = true;
			if (flood_push(state, x, y, direction))
				found_directional = true;
			goto advance;
		}
		if (pixel == ((pattern_row & (0x80 >>
		    ((rip->viewport.sx + x) & 7))) != 0
		    ? state->foreground : state->background))
			goto advance;
		in_span = true;
		flood_push(state, x, y, 0);
advance:
		if (x >= scan_right)
			break;
		x++;
	} while (1);
	return found_directional;
}

static void
flood_process_seed(struct flood_state *state, struct flood_seed seed)
{
	struct cterm_rip_raster *rip = state->rip;
	struct st_pixel_buffer *pixels = state->pixels;
	int left_edge, right_edge;
	int x, pattern_x, pattern_y;
	bool push_up = true, push_down = true;

	if (seed.x < 0 || seed.y < 0 || seed.x >= (int)pixels->width
	    || seed.y >= (int)pixels->height)
		return;
	if ((pixels->primary[(size_t)seed.y * pixels->width + seed.x]
	    & 0x80ffffff) == state->edge)
		return;
	pattern_y = (rip->viewport.sy + seed.y) & 7;
	x = seed.x;
	pattern_x = (rip->viewport.sx + x) & 7;
	while (x < (int)pixels->width) {
		uint32_t *pixel = &pixels->primary[(size_t)seed.y
		    * pixels->width + x];

		if ((*pixel & 0x80ffffff) == state->edge)
			break;
		*pixel = rip->fill_pattern[pattern_y] & (0x80 >> pattern_x)
		    ? state->foreground : state->background;
		x++;
		pattern_x = (pattern_x + 1) & 7;
	}
	right_edge = x - 1;
	x = seed.x - 1;
	pattern_x = (rip->viewport.sx + x) & 7;
	while (x >= 0) {
		uint32_t *pixel = &pixels->primary[(size_t)seed.y
		    * pixels->width + x];

		if ((*pixel & 0x80ffffff) == state->edge)
			break;
		*pixel = rip->fill_pattern[pattern_y] & (0x80 >> pattern_x)
		    ? state->foreground : state->background;
		x--;
		pattern_x = (pattern_x - 1) & 7;
	}
	left_edge = x + 1;
	if (seed.direction != 0) {
		if (seed.direction > 0)
			push_up = false;
		else
			push_down = false;
	}
	else if (!state->found_adjacent
	    && left_edge >= state->previous_left
	    && right_edge <= state->previous_right) {
		if (state->previous_y + 1 == seed.y)
			push_up = false;
		else if (state->previous_y - 1 == seed.y)
			push_down = false;
	}
	if (push_up && seed.y > 0
	    && flood_scan_adjacent(state, left_edge, right_edge,
	        seed.y - 1, -1))
		state->found_adjacent = true;
	if (push_down && seed.y < (int)pixels->height - 1
	    && flood_scan_adjacent(state, left_edge, right_edge,
	        seed.y + 1, 1))
		state->found_adjacent = true;
	state->previous_y = seed.y;
	state->previous_left = left_edge;
	state->previous_right = right_edge;
}

static void
flood_fill(struct cterm_rip_raster *rip, int x, int y, int border)
{
	struct flood_state *state;
	struct st_pixel_buffer *pixels = NULL;
	struct st_console_rect rect;
	struct st_pixel_blit blit;
	int bottom;

	if (rip->error != 0 || no_viewport(rip)
	    || x > rip->viewport.ex - rip->viewport.sx
	    || y > rip->viewport.ey - rip->viewport.sy
	    || border > 255)
		return;
	bottom = rip->viewport.ey;

	if ((uint32_t)bottom >= rip->screen_height)
		bottom = (int)rip->screen_height - 1;
	if (rip->viewport.sx < 0 || rip->viewport.sy < 0
	    || (uint32_t)rip->viewport.ex >= rip->screen_width
	    || bottom < rip->viewport.sy)
		return;
	rect = (struct st_console_rect){
		.x = rip->viewport.sx, .y = rip->viewport.sy,
		.width = rip->viewport.ex - rip->viewport.sx + 1,
		.height = bottom - rip->viewport.sy + 1,
	};
	rip->error = st_console_get_pixels(&pixels, rip->console, &rect, false);
	if (rip->error != 0)
		return;
	state = calloc(1, sizeof(*state));
	if (state == NULL) {
		rip->error = IO_ERROR_OUT_OF_MEMORY;
		st_pixel_buffer_destroy(pixels);
		return;
	}
	state->rip = rip;
	state->pixels = pixels;
	state->edge = map_colour(rip, border);
	state->foreground = map_colour(rip, rip->fill_colour);
	state->background = map_colour(rip, 0);
	state->previous_y = y;
	state->previous_right = (int)pixels->width - 1;
	flood_process_seed(state, (struct flood_seed){x, y, 0});
	while (state->seed_count != 0) {
		struct flood_seed next = state->seeds[--state->seed_count];

		flood_process_seed(state, next);
	}
	blit = (struct st_pixel_blit){
		.width = pixels->width, .height = pixels->height,
		.destination_x = rip->viewport.sx,
		.destination_y = rip->viewport.sy,
		.scale_x = 1, .scale_y = 1,
	};
	rip->error = st_console_blit_pixels(rip->console, pixels, NULL, &blit);
	free(state);
	st_pixel_buffer_destroy(pixels);
}

static const int stroke_mults[11] = {1, 3, 2, 3, 1, 4, 5, 2, 5, 3, 4};
static const int stroke_divs[11] = {1, 5, 3, 4, 1, 3, 3, 1, 2, 1, 1};

static int
stroke_char_width(struct cterm_rip_raster *rip, unsigned char ch)
{
	size_t size;
	const uint8_t *font = cterm_rip_font_data(rip->font_number, &size);
	int first, count;

	if (font == NULL || size < 0x90)
		return 0;
	first = font[0x84];
	count = font[0x81] | (font[0x82] << 8);
	if (ch < first || ch - first >= count
	    || (size_t)(0x90 + 2 * count + ch - first) >= size)
		return 0;
	return font[0x90 + 2 * count + ch - first];
}

static void
draw_rip_char(struct cterm_rip_raster *rip, unsigned char ch)
{
	size_t size;
	const uint8_t *font;
	int x, y, start_x, start_y;

	if (rip->error != 0 || no_viewport(rip))
		return;
	font = cterm_rip_font_data(rip->font_number, &size);
	if (font == NULL)
		return;
	if (rip->font_number == 0) {
		int magnitude = rip->font_size;

		if (!rip->font_vertical) {
			int extent = 8 * magnitude - 2;
			int width = rip->viewport.ex - rip->viewport.sx;
			int height = rip->viewport.ey - rip->viewport.sy;

			if (rip->x < 0 || rip->x >= width - extent
			    || rip->y < 0 || rip->y >= height - extent) {
				rip->x += 8 * magnitude;
				return;
			}
		}
		for (y = 0; y < 8; y++) {
			uint8_t bits = font[(size_t)ch * 8 + y];

			for (x = 0; x < 8; x++) {
				if (!(bits & (0x80 >> x)))
					continue;
				for (int dy = 0; dy < magnitude; dy++) {
					for (int dx = 0; dx < magnitude; dx++) {
						if (rip->font_vertical)
							set_pixel(rip,
							    rip->x + y * magnitude + dy
							    - 8 * magnitude,
							    rip->y - x * magnitude - dx - 1,
							    map_colour(rip, rip->colour));
						else
							set_pixel(rip,
							    rip->x + x * magnitude + dx,
							    rip->y + y * magnitude + dy,
							    map_colour(rip, rip->colour));
					}
				}
			}
		}
		if (rip->font_vertical)
			rip->y += 8 * magnitude;
		else
			rip->x += 8 * magnitude;
		return;
	}
	if (size < 0x90)
		return;
	int first = font[0x84];
	int count = font[0x81] | (font[0x82] << 8);
	if (ch < first || ch - first >= count
	    || (size_t)(0x90 + 2 * (ch - first) + 1) >= size)
		return;
	int glyph_offset = 0x80 + font[0x85] + (font[0x86] << 8)
	    + font[0x90 + 2 * (ch - first)]
	    + (font[0x91 + 2 * (ch - first)] << 8);
	int magnitude = rip->font_size;
	int mult = stroke_mults[magnitude];
	int div = stroke_divs[magnitude];
	int height = ((int)(int8_t)font[0x88] - (int)(int8_t)font[0x8a])
	    * mult / div;
	int saved_width = rip->line_width;
	uint16_t saved_pattern = rip->line_pattern;

	start_x = rip->x;
	start_y = rip->y;
	rip->line_width = 1;
	rip->line_pattern = 0xffff;
	while (glyph_offset >= 0 && (size_t)glyph_offset + 1 < size
	    && rip->error == 0) {
		uint8_t raw_x = font[glyph_offset++];
		uint8_t raw_y = font[glyph_offset++];
		int opcode = ((raw_x >> 6) & 2) | ((raw_y >> 7) & 1);
		int scaled_x = (int8_t)((raw_x & 0x7f)
		    | ((raw_x & 0x40) ? 0x80 : 0)) * mult / div;
		int scaled_y = (int8_t)((raw_y & 0x7f)
		    | ((raw_y & 0x40) ? 0x80 : 0)) * mult / div;
		int target_x = start_x + (rip->font_vertical
		    ? height - scaled_y : scaled_x);
		int target_y = start_y + (rip->font_vertical
		    ? -scaled_x : height - scaled_y);

		if (opcode == 0)
			break;
		if (opcode == 3)
			draw_line(rip, rip->x, rip->y, target_x, target_y);
		if (opcode == 2 || opcode == 3) {
			rip->x = target_x;
			rip->y = target_y;
		}
	}
	if (rip->font_vertical)
		rip->x = start_x + height;
	else
		rip->y = start_y;
	rip->line_width = saved_width;
	rip->line_pattern = saved_pattern;
}

static void
font_text_bytes(struct cterm_rip_raster *rip, const char *text,
    char **result)
{
	size_t size = strlen(text);
	char *bytes = malloc(size + 1);
	size_t output = 0;

	*result = NULL;
	if (bytes == NULL) {
		rip->error = IO_ERROR_OUT_OF_MEMORY;
		return;
	}
	for (size_t input = 0; input < size;) {
		uint32_t scalar;
		size_t consumed;
		uint8_t glyph;

		if (st_utf8_decode_one(&scalar, &consumed, text + input,
		    size - input) != 0) {
			scalar = '?';
			consumed = 1;
		}
		if (!st_codepage_from_unicode(&glyph, rip->text_codepage,
		    scalar))
			glyph = '?';
		bytes[output++] = (char)glyph;
		input += consumed;
	}
	bytes[output] = '\0';
	*result = bytes;
}

static void
draw_rip_text(struct cterm_rip_raster *rip, const char *text)
{
	char *converted = NULL;

	if (rip->unicode_text) {
		font_text_bytes(rip, text, &converted);
		if (converted == NULL)
			return;
		text = converted;
	}
	if (rip->font_vertical) {
		int start_x = rip->x;
		int start_y = rip->y;
		int width = 0;
		int cursor_y;

		for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
			if (*p == '\n')
				continue;
			width += rip->font_number == 0 ? 8 * rip->font_size
			    : stroke_char_width(rip, *p);
		}
		if (rip->font_number != 0)
			width = width * stroke_mults[rip->font_size]
			    / stroke_divs[rip->font_size];
		cursor_y = rip->y + width;
		for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
			if (*p == '\n')
				continue;
			rip->x = start_x;
			rip->y = cursor_y;
			draw_rip_char(rip, *p);
			cursor_y -= rip->font_number == 0 ? 8 * rip->font_size
			    : stroke_char_width(rip, *p)
			    * stroke_mults[rip->font_size]
			    / stroke_divs[rip->font_size];
		}
		rip->y = start_y;
		rip->x = start_x + width;
	}
	else {
		for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
			if (*p != '\n')
				draw_rip_char(rip, *p);
		}
	}
	free(converted);
}

static int
rip_text_width(struct cterm_rip_raster *rip, const char *text)
{
	char *converted = NULL;
	int width = 0;

	if (rip->unicode_text) {
		font_text_bytes(rip, text, &converted);
		if (converted == NULL)
			return 0;
		text = converted;
	}

	for (const unsigned char *p = (const unsigned char *)text; *p; p++)
		width += rip->font_number == 0 ? 8 * rip->font_size
		    : stroke_char_width(rip, *p);
	if (rip->font_number != 0)
		width = width * stroke_mults[rip->font_size]
		    / stroke_divs[rip->font_size];
	free(converted);
	return width;
}

static int
rip_font_height(struct cterm_rip_raster *rip)
{
	size_t size;
	const uint8_t *font;

	if (rip->font_number == 0)
		return 8 * rip->font_size;
	font = cterm_rip_font_data(rip->font_number, &size);
	if (font == NULL || size <= 0x8a)
		return 0;
	return ((int)(int8_t)font[0x88] - (int)(int8_t)font[0x8a])
	    * stroke_mults[rip->font_size]
	    / stroke_divs[rip->font_size];
}

static void
draw_region_text(struct cterm_rip_raster *rip, int justify,
    const char *text)
{
	int old_x = rip->x;
	int old_y = rip->y;
	int words = 0;
	bool in_word = false;

	rip->x = rip->text_region.sx + rip->text_region.xpos
	    - rip->viewport.sx;
	rip->y = rip->text_region.sy + rip->text_region.ypos
	    - rip->viewport.sy;
	if (justify != 0) {
		for (const char *p = text; *p; p++) {
			if (*p == ' ')
				in_word = false;
			else if (!in_word) {
				in_word = true;
				words++;
			}
		}
	}
	if (justify == 0 || words < 2)
		draw_rip_text(rip, text);
	else {
		int region_width = rip->text_region.ex
		    - rip->text_region.sx + 1;
		int slack = region_width - rip_text_width(rip, text) + 1;
		double step = words > 2 ? (double)slack / (words - 1)
		    : (double)slack;
		int accumulated = 0;
		char *copy = strdup(text);
		char *current;
		char *boundary;

		if (copy == NULL) {
			rip->error = IO_ERROR_OUT_OF_MEMORY;
			goto restore;
		}
		current = copy;
		boundary = strchr(current, ' ');
		for (int word = 0; word < words - 1
		    && rip->error == 0; word++) {
			int start_x = rip->x;
			int gap = (int)step;
			int ideal = (int)(step * (word + 1));

			if (boundary != NULL)
				*boundary = '\0';
			draw_rip_text(rip, current);
			rip->x = start_x + rip_text_width(rip, current);
			accumulated += gap;
			if (ideal > accumulated) {
				gap += ideal - accumulated;
				accumulated = ideal;
			}
			rip->x += gap;
			if (boundary != NULL)
				*boundary = ' ';
			current = boundary;
			if (current != NULL) {
				char *next = current + 1;

				while (*next == ' ')
					next++;
				boundary = strchr(next, ' ');
			}
		}
		if (current != NULL && rip->error == 0)
			draw_rip_text(rip, current);
		free(copy);
	}
	rip->text_region.ypos += rip->text_region.line_height + 3;
restore:
	rip->x = old_x;
	rip->y = old_y;
}

static int
parse_mega(const char *text, int field_width, int *consumed)
{
	int value = 0;
	int i;
	int digit;

	for (i = 0; i < field_width; i++) {
		if (text[i] >= '0' && text[i] <= '9')
			digit = text[i] - '0';
		else if (text[i] >= 'A' && text[i] <= 'Z')
			digit = text[i] - 'A' + 10;
		else if (text[i] >= 'a' && text[i] <= 'z')
			digit = text[i] - 'a' + 10;
		else if (text[i] == '-')
			digit = 0;
		else if (i > 0 && (text[i] == '|' || text[i] == '\0'))
			break;
		else {
			*consumed = 0;
			return -1;
		}
		if (value > (INT_MAX - digit) / 36) {
			*consumed = 0;
			return -1;
		}
		value = value * 36 + digit;
	}
	*consumed = i;
	return value;
}

static int
parse_fields(const char *arguments, const uint8_t *widths, size_t count,
    int *values)
{
	int parsed = 0;

	for (size_t i = 0; i < count; i++) {
		int consumed = 0;
		int value = parse_mega(arguments, widths[i], &consumed);

		if (consumed == 0)
			break;
		values[i] = value;
		arguments += consumed;
		parsed++;
	}
	return parsed;
}

/* RIPterm's filled poly-Bézier uses four raster scans, then two gap passes.
 * A vertex-based polygon fill changes its pixels around thin curves. */
static void
fill_poly_bezier(struct cterm_rip_raster *rip)
{
	uint8_t *marks = rip->poly_fill_marks;
	int width = rip->x_max;
	int height = rip->y_max;
	struct st_pixel_buffer *pixels = NULL;
	struct st_pixel_mask *mask = NULL;
	const struct st_pixel_blit blit = {
		.width = (uint32_t)width, .height = (uint32_t)height,
		.scale_x = 1, .scale_y = 1,
	};
	bool in_line, fill;
	size_t pixel;

	for (int y = 0; y < height; y++) {
		in_line = fill = false;
		for (int x = 0; x < width; x++) {
			pixel = (size_t)y * (size_t)width + (size_t)x;
			if (in_line) {
				if (!(marks[pixel] & 0x40)) {
					fill = !fill;
					in_line = false;
				}
			}
			else if (marks[pixel] & 0x40)
				in_line = true;
			if (fill && !in_line)
				marks[pixel] |= 0x20;
		}
	}
	for (int y = height - 1; y >= 0; y--) {
		in_line = fill = false;
		for (int x = width - 1; x >= 0; x--) {
			pixel = (size_t)y * (size_t)width + (size_t)x;
			if (in_line) {
				if (!(marks[pixel] & 0x40)) {
					fill = !fill;
					in_line = false;
				}
			}
			else if (marks[pixel] & 0x40)
				in_line = true;
			if (fill && !in_line)
				marks[pixel] |= 0x10;
		}
	}
	for (int x = 0; x < width; x++) {
		in_line = fill = false;
		for (int y = 0; y < height; y++) {
			pixel = (size_t)y * (size_t)width + (size_t)x;
			if (in_line) {
				if (!(marks[pixel] & 0x40)) {
					fill = !fill;
					in_line = false;
				}
			}
			else if (marks[pixel] & 0x40)
				in_line = true;
			if (fill && !in_line)
				marks[pixel] |= 0x08;
		}
	}
	for (int x = width - 1; x >= 0; x--) {
		in_line = fill = false;
		for (int y = height - 1; y >= 0; y--) {
			pixel = (size_t)y * (size_t)width + (size_t)x;
			if (in_line) {
				if (!(marks[pixel] & 0x40)) {
					fill = !fill;
					in_line = false;
				}
			}
			else if (marks[pixel] & 0x40)
				in_line = true;
			if (fill && !in_line)
				marks[pixel] |= 0x04;
		}
	}
	for (int y = 0; y < height; y++) {
		for (int x = 1; x < width - 1; x++) {
			pixel = (size_t)y * (size_t)width + (size_t)x;
			if ((marks[pixel] & 0x3c) != 0
			    && (marks[pixel] & 0x3c) != 0x3c
			    && (marks[pixel - 1] == 0x3c
			    || marks[pixel + 1] == 0x3c))
				marks[pixel] |= 0x3c;
		}
	}
	for (int x = 0; x < width; x++) {
		for (int y = 1; y < height - 1; y++) {
			pixel = (size_t)y * (size_t)width + (size_t)x;
			if ((marks[pixel] & 0x3c) != 0
			    && (marks[pixel] & 0x3c) != 0x3c
			    && (marks[pixel - (size_t)width] == 0x3c
			    || marks[pixel + (size_t)width] == 0x3c))
				marks[pixel] |= 0x3c;
		}
	}
	rip->error = st_pixel_buffer_create(&pixels,
	    (uint32_t)width, (uint32_t)height, false);
	if (rip->error == 0)
		rip->error = st_pixel_mask_create(&mask,
		    (uint32_t)width, (uint32_t)height);
	if (rip->error == 0) {
		for (int y = 0; y < height && rip->error == 0; y++) {
			for (int x = 0; x < width && rip->error == 0; x++) {
				pixel = (size_t)y * (size_t)width + (size_t)x;
				if (marks[pixel] != 0x3c)
					continue;
				int colour = (rip->fill_pattern[y & 7]
				    & (0x80 >> (x & 7))) ? rip->fill_colour : 0;

				pixels->primary[pixel] = map_colour(rip, colour);
				rip->error = st_pixel_mask_set(mask, (uint32_t)x,
				    (uint32_t)y, true);
			}
		}
		if (rip->error == 0)
			rip->error = st_console_blit_pixels(rip->console,
			    pixels, mask, &blit);
	}
	st_pixel_mask_destroy(mask);
	st_pixel_buffer_destroy(pixels);
}

static bool
poly_number(const char *arguments, size_t length, size_t offset,
    int width, int *value)
{
	int consumed = 0;

	if (offset > length || (size_t)width > length - offset)
		return false;
	*value = parse_mega(arguments + offset, width, &consumed);
	return *value >= 0 && consumed == width;
}

static void
poly_bezier(struct cterm_rip_raster *rip, const char *arguments,
    bool filled, bool closed)
{
	const size_t length = strlen(arguments);
	size_t offset = 4;
	int number, segments, start_x, start_y, last_x, last_y;
	bool last_was_bezier = false;
	bool completed = false;
	uint32_t colour = map_colour(rip, rip->colour);

	if (!poly_number(arguments, length, 0, 2, &number)
	    || !poly_number(arguments, length, 2, 2, &segments)
	    || segments <= 0
	    || !poly_number(arguments, length, 5, 2, &start_x)
	    || !poly_number(arguments, length, 7, 2, &start_y))
		return;
	last_x = start_x;
	last_y = start_y;
	if (filled) {
		rip->poly_fill_marks = calloc((size_t)rip->x_max
		    * (size_t)rip->y_max, 1);
		if (rip->poly_fill_marks == NULL) {
			rip->error = IO_ERROR_OUT_OF_MEMORY;
			return;
		}
	}
	for (int index = 0; index < number && rip->error == 0; index++) {
		int type, point[8];

		if (!poly_number(arguments, length, offset, 1, &type))
			goto done;
		offset++;
		switch (type) {
		case 0:
			if (!poly_number(arguments, length, offset, 2, &point[0])
			    || !poly_number(arguments, length, offset + 2, 2,
			    &point[1]))
				goto done;
			offset += 4;
			if (!last_was_bezier)
				set_line(rip, last_x, last_y, point[0], point[1],
				    colour, rip->line_pattern, rip->line_width);
			last_x = point[0];
			last_y = point[1];
			last_was_bezier = false;
			break;
		case 1: case 2: case 5: case 6: {
			for (size_t i = 0; i < 6; i++) {
				if (!poly_number(arguments, length, offset + i * 2,
				    2, &point[i]))
					goto done;
			}
			offset += 12;
			if (offset == length) {
				point[6] = start_x;
				point[7] = start_y;
			}
			else if (!poly_number(arguments, length, offset + 1, 2,
			    &point[6])
			    || !poly_number(arguments, length, offset + 3, 2,
			    &point[7]))
				goto done;
			/* Legacy rip_bezier() ignores its tagged foreground
			 * argument.  Curved pixels therefore do not become fill
			 * boundaries in RIPterm's four-pass fill. */
			uint8_t *marks = rip->poly_fill_marks;

			rip->poly_fill_marks = NULL;
			draw_bezier(rip, point, segments);
			rip->poly_fill_marks = marks;
			last_x = point[6];
			last_y = point[7];
			last_was_bezier = true;
			break;
		}
		default:
			goto done;
		}
	}
	completed = rip->error == 0;
	if (completed && closed && (last_x != start_x || last_y != start_y))
		set_line(rip, last_x, last_y, start_x, start_y,
		    colour, rip->line_pattern, rip->line_width);
done:
	if (completed && filled && rip->error == 0)
		fill_poly_bezier(rip);
	free(rip->poly_fill_marks);
	rip->poly_fill_marks = NULL;
}

static void
button_chisel_inset(int height, int *xinset, int *yinset)
{
	static const struct {
		int limit, x, y;
	} insets[] = {
		{12, 1, 1}, {25, 3, 2}, {40, 4, 3},
		{75, 6, 5}, {150, 7, 5}, {200, 8, 6},
		{250, 10, 7}, {300, 11, 8}, {INT_MAX, 13, 9},
	};

	for (size_t i = 0; i < sizeof(insets) / sizeof(insets[0]); i++) {
		if (height < insets[i].limit) {
			*xinset = insets[i].x;
			*yinset = insets[i].y;
			return;
		}
	}
	*xinset = insets[sizeof(insets) / sizeof(insets[0]) - 1].x;
	*yinset = insets[sizeof(insets) / sizeof(insets[0]) - 1].y;
}

static void
button_line(struct cterm_rip_raster *rip, int x1, int y1,
    int x2, int y2, uint32_t colour)
{
	set_line(rip, x1, y1, x2, y2, colour, 0xffff, 1);
}

static void
draw_plain_button(struct cterm_rip_raster *rip, int x1, int y1,
    int x2, int y2, bool inverted, const char *icon,
    int *actual_x2, int *actual_y2)
{
	const struct cterm_rip_button_style *style = &rip->button_style;
	int bx1 = x1 + rip->viewport.sx;
	int by1 = y1 + rip->viewport.sy;
	int bx2 = x2 + rip->viewport.sx;
	int by2 = y2 + rip->viewport.sy;
	int original_x1, original_y1, original_x2, original_y2;
	int bevel_size = style->bevel_size;
	int bevel_start = 1;
	int xinset, yinset;
	uint32_t ch, cs, cc;
	int saved_fill_colour = rip->fill_colour;
	int saved_fill_style = rip->fill_style;
	uint8_t saved_fill_pattern[8];

	if (style->type == 2 && style->width != 0 && style->height != 0) {
		bx2 = bx1 + style->width;
		by2 = by1 + style->height;
	}
	original_x1 = bx1;
	original_y1 = by1;
	original_x2 = bx2;
	original_y2 = by2;
	if (bx2 == bx1) {
		bx1--;
		bx2++;
	}
	if (by2 == by1) {
		by1--;
		by2++;
	}
	if ((style->flags & 512) != 0 && bevel_size == 0) {
		bevel_size = 1;
		bevel_start = 0;
	}
	if (style->type == 0) {
		int icon_width = 0, icon_height = 0;
		bool loaded = false;

		if (icon != NULL) {
			if (inverted && (style->flags & 4096) != 0) {
				char hot_name[256];
				const char *dot = strrchr(icon, '.');

				if (dot != NULL)
					(void)snprintf(hot_name, sizeof(hot_name),
					    "%.*s.HIC", (int)(dot - icon), icon);
				else
					(void)snprintf(hot_name, sizeof(hot_name),
					    "%s.HIC", icon);
				loaded = load_icon(rip, x1, y1, 0, false,
				    hot_name, &icon_width, &icon_height);
			}
			if (!loaded)
				(void)load_icon(rip, x1, y1,
				    inverted ? 4 : 0, false, icon,
				    &icon_width, &icon_height);
		}
		bx1 = original_x1;
		by1 = original_y1;
		bx2 = bx1 + icon_width;
		by2 = by1 + icon_height;
		original_x2 = bx2;
		original_y2 = by2;
	}
	else if (style->type == 1 && rip->clipboard != NULL) {
		bx1 = original_x1;
		by1 = original_y1;
		bx2 = bx1 + (int)rip->clipboard->width;
		by2 = by1 + (int)rip->clipboard->height;
		original_x2 = bx2;
		original_y2 = by2;
		paste_buffer(rip, rip->clipboard, bx1, by1,
		    inverted ? 4 : 0);
	}
	if (actual_x2 != NULL)
		*actual_x2 = original_x2;
	if (actual_y2 != NULL)
		*actual_y2 = original_y2;
	ch = map_colour(rip, inverted ? 15 ^ style->highlight
	    : style->highlight);
	cs = map_colour(rip, inverted ? 15 ^ style->shadow
	    : style->shadow);
	cc = map_colour(rip, inverted ? 15 ^ style->corner
	    : style->corner);
	if (style->type == 2) {
		memcpy(saved_fill_pattern, rip->fill_pattern,
		    sizeof(saved_fill_pattern));
		rip->fill_colour = inverted ? 15 ^ style->surface
		    : style->surface;
		rip->fill_style = 1;
		memset(rip->fill_pattern, 0xff, sizeof(rip->fill_pattern));
		if (bx2 > bx1 && by2 > by1)
			cterm_rip_raster_bar(rip, bx1, by1,
			    bx2 - 1, by2 - 1);
		rip->fill_colour = saved_fill_colour;
		rip->fill_style = saved_fill_style;
		memcpy(rip->fill_pattern, saved_fill_pattern,
		    sizeof(rip->fill_pattern));
	}
	if ((style->flags & 8) != 0) {
		button_chisel_inset(by2 - by1, &xinset, &yinset);
		button_line(rip, bx1 + xinset + 1, by1 + yinset + 1,
		    bx2 - xinset - 1, by1 + yinset + 1, ch);
		button_line(rip, bx2 - xinset - 1, by1 + yinset + 1,
		    bx2 - xinset - 1, by2 - yinset - 1, ch);
		button_line(rip, bx2 - xinset - 1, by2 - yinset - 1,
		    bx1 + xinset + 1, by2 - yinset - 1, ch);
		button_line(rip, bx1 + xinset + 1, by2 - yinset - 1,
		    bx1 + xinset + 1, by1 + yinset + 1, ch);
		button_line(rip, bx1 + xinset, by1 + yinset,
		    bx2 - xinset - 1, by1 + yinset, cs);
		button_line(rip, bx1 + xinset, by1 + yinset,
		    bx1 + xinset, by2 - yinset - 1, cs);
		int iiy = by2 - 2 - yinset;
		int iix = bx2 - xinset - 2;

		if (iiy > by1 + yinset + 1 && iiy < by2 - yinset - 1) {
			button_line(rip, bx1 + xinset + 2, iiy,
			    bx2 - xinset - 2, iiy, cs);
			button_line(rip, iix, iiy, iix,
			    by1 + yinset + 2, cs);
		}
		else if (iiy <= by1) {
			button_line(rip, iix, iiy, bx1 + xinset, iiy, cs);
			button_line(rip, iix, iiy, iix, by1 + yinset, cs);
		}
	}
	if ((style->flags & 32768) != 0) {
		int sx = original_x2 - original_x1 < 2 ? 1 : 0;
		int sy = original_y2 - original_y1 < 2 ? 1 : 0;

		button_line(rip, original_x1 + sx, original_y1,
		    original_x2 - 1 - sx, original_y1, cs);
		button_line(rip, original_x1, original_y1 + sy,
		    original_x1, original_y2 - 1 - sy, cs);
		button_line(rip, original_x2 - 1, original_y2 - 1 - sy,
		    original_x2 - 1, original_y1 + sy, ch);
		button_line(rip, original_x2 - 1 - sx, original_y2 - 1,
		    original_x1 + sx, original_y2 - 1, ch);
		if (original_x2 - original_x1 >= 3
		    && original_y2 - original_y1 >= 3) {
			set_pixel(rip, original_x1, original_y1, cc);
			set_pixel(rip, original_x2 - 1, original_y1, cc);
			set_pixel(rip, original_x2 - 1, original_y2 - 1, cc);
			set_pixel(rip, original_x1, original_y2 - 1, cc);
		}
	}
	if ((style->flags & 512) != 0) {
		for (int i = bevel_start; i <= bevel_size; i++) {
			button_line(rip, original_x1 - i, original_y1 - i,
			    original_x2 - 1 + i, original_y1 - i, ch);
			button_line(rip, original_x1 - i, original_y1 - i,
			    original_x1 - i, original_y2 - 1 + i, ch);
			button_line(rip, original_x2 - 1 + i, original_y2 - 1 + i,
			    original_x2 - 1 + i, original_y1 - i, cs);
			button_line(rip, original_x2 - 1 + i, original_y2 - 1 + i,
			    original_x1 - i, original_y2 - 1 + i, cs);
			set_pixel(rip, original_x1 - i, original_y1 - i, cc);
			set_pixel(rip, original_x2 - 1 + i, original_y1 - i, cc);
			set_pixel(rip, original_x1 - i, original_y2 - 1 + i, cc);
			set_pixel(rip, original_x2 - 1 + i,
			    original_y2 - 1 + i, cc);
		}
	}
	if ((style->flags & 16) != 0) {
		uint32_t r_cs = map_colour(rip, style->shadow);
		uint32_t r_ch = map_colour(rip, style->highlight);
		uint32_t r_cc = map_colour(rip, style->corner);
		uint32_t r_bg = map_colour(rip, 0);
		int rx = original_x1, ry = original_y1;
		int rw = original_x2 - original_x1;
		int rh = original_y2 - original_y1;

		if ((style->flags & 512) != 0) {
			rw += 2 * bevel_size;
			rh += 2 * bevel_size;
			rx -= bevel_size;
			ry -= bevel_size;
		}
		rw += 4; rh += 4; rx -= 2; ry -= 2;
		button_line(rip, rx, ry, rx + rw - 1, ry, r_cs);
		button_line(rip, rx, ry, rx, ry + rh - 1, r_cs);
		button_line(rip, rx + rw - 1, ry,
		    rx + rw - 1, ry + rh - 1, r_ch);
		button_line(rip, rx, ry + rh - 1,
		    rx + rw - 1, ry + rh - 1, r_ch);
		set_pixel(rip, rx, ry, r_cc);
		set_pixel(rip, rx + rw - 1, ry, r_cc);
		set_pixel(rip, rx, ry + rh - 1, r_cc);
		set_pixel(rip, rx + rw - 1, ry + rh - 1, r_cc);
		button_line(rip, rx + 1, ry + 1, rx + rw - 2, ry + 1, r_bg);
		button_line(rip, rx + 1, ry + 1, rx + 1, ry + rh - 2, r_bg);
		button_line(rip, rx + rw - 2, ry + 1,
		    rx + rw - 2, ry + rh - 2, r_bg);
		button_line(rip, rx + 1, ry + rh - 2,
		    rx + rw - 2, ry + rh - 2, r_bg);
	}
}

static int
button_hcenter_x(const struct cterm_rip_button_style *style,
    int raw_x, int raw_width, int ext_x, int ext_width, int text_width,
    int x2)
{
	int difference = raw_width - text_width;

	if ((style->flags2 & 8) != 0 && (style->flags & 512) != 0)
		return ext_x + ((style->flags & 8) != 0 ? 20 : 10);
	if ((style->flags2 & 16) != 0 && (style->flags & 512) != 0)
		return ext_x + ext_width
		    - ((style->flags & 8) != 0 ? 20 : 10) - text_width;
	if ((style->flags2 & 8) != 0)
		return raw_x;
	if ((style->flags2 & 16) != 0)
		return x2 - text_width;
	return raw_x + ((difference - (difference < 0 ? -1 : 0)) >> 1);
}

static void
capture_button_stamp(struct cterm_rip_raster *rip,
    const struct cterm_rip_button_draw *button)
{
	int bx1 = button->x1, by1 = button->y1;
	int bx2 = button->x2, by2 = button->y2;
	int bevel_size = button->style.bevel_size;
	struct st_console_rect rect;
	struct st_pixel_buffer *captured = NULL;

	if (button->style.type == 2
	    && button->style.width != 0 && button->style.height != 0) {
		bx2 = bx1 + button->style.width;
		by2 = by1 + button->style.height;
	}
	if (bx1 == bx2) {
		bx1--;
		bx2++;
	}
	if (by1 == by2) {
		by1--;
		by2++;
	}
	if ((button->style.flags & 512) != 0 && bevel_size == 0)
		bevel_size = 1;
	if (button->style.type == 1 && rip->clipboard != NULL) {
		bx1 = button->x1;
		by1 = button->y1;
		bx2 = bx1 + (int)rip->clipboard->width;
		by2 = by1 + (int)rip->clipboard->height;
	}
	st_pixel_buffer_destroy(rip->clipboard);
	rip->clipboard = NULL;
	rip->clipx = bx1;
	rip->clipy = by1;
	bx1 -= bevel_size;
	by1 -= bevel_size;
	bx2 += bevel_size;
	by2 += bevel_size;
	if (bx1 < 0 || by1 < 0
	    || (uint32_t)bx2 >= rip->screen_width
	    || (uint32_t)by2 >= rip->screen_height)
		return;
	rect = (struct st_console_rect){
		.x = (uint32_t)bx1,
		.y = (uint32_t)by1,
		.width = (uint32_t)(bx2 - bx1 + 1),
		.height = (uint32_t)(by2 - by1 + 1),
	};
	rip->error = st_console_get_pixels(&captured, rip->console,
	    &rect, false);
	if (rip->error == 0)
		rip->clipboard = captured;
}

static void
draw_button_label(struct cterm_rip_raster *rip,
    int x1, int y1, int x2, int y2, int hotkey,
    bool inverted, const char *label)
{
	const struct cterm_rip_button_style *style = &rip->button_style;
	const struct cterm_rip_font_metric *metric =
	    cterm_rip_font_button_metric(rip->font_number, rip->font_size);
	int bx1 = x1 + rip->viewport.sx;
	int by1 = y1 + rip->viewport.sy;
	int bx2 = x2 + rip->viewport.sx;
	int by2 = y2 + rip->viewport.sy;
	int bevel = (style->flags & 512) != 0
	    ? (style->bevel_size == 0 ? 1 : style->bevel_size) : 0;
	int recess = (style->flags & 16) != 0 ? 2 : 0;
	int extension = bevel + recess;
	int width, height, descender, text_x, si, text_y;
	int old_x = rip->x, old_y = rip->y, old_colour = rip->colour;
	int hotkey_position = -1;
	bool has_descender = false;

	if (metric == NULL || label == NULL || label[0] == '\0')
		return;
	if (style->type == 2 && style->width != 0 && style->height != 0) {
		bx2 = bx1 + style->width;
		by2 = by1 + style->height;
	}
	else if (style->type == 1 && rip->clipboard != NULL) {
		bx2 = bx1 + (int)rip->clipboard->width;
		by2 = by1 + (int)rip->clipboard->height;
	}
	width = rip_text_width(rip, label);
	height = metric->base - metric->top + 1;
	descender = metric->drop - metric->base;
	for (const unsigned char *p = (const unsigned char *)label; *p; p++) {
		if (cterm_rip_button_char_descends(*p))
			has_descender = true;
	}
	if (has_descender && (style->flags & 8192) != 0)
		height += descender;
	int ext_x = bx1 - extension;
	int ext_y = by1 - extension;
	int ext_x2 = bx2 - 1 + extension;
	int ext_y2 = by2 - 1 + extension;
	int ext_width = bx2 - bx1 + 2 * bevel;
	int ext_height = by2 - by1 + 2 * bevel;
	int y_difference = ext_height - height;

	switch (style->orientation) {
	case 0:
		text_x = button_hcenter_x(style, bx1, bx2 - bx1,
		    ext_x, ext_width, width, bx2);
		si = ext_y - 5 - height;
		if (has_descender && (style->flags & 8192) != 0)
			si -= descender;
		break;
	case 1:
		text_x = ext_x - width - 8;
		si = ext_y + ((y_difference
		    - (y_difference < 0 ? -1 : 0)) >> 1) + 2;
		if ((style->flags & 2048) != 0)
			si--;
		if (has_descender)
			si += (descender - (descender < 0 ? -1 : 0)) >> 1;
		break;
	case 2:
		text_x = button_hcenter_x(style, bx1, bx2 - bx1,
		    ext_x, ext_width, width, bx2);
		si = ext_y + ((y_difference
		    - (y_difference < 0 ? -1 : 0)) >> 1) + 1;
		break;
	case 3:
		text_x = ext_x2 + 8;
		si = ext_y + ((y_difference
		    - (y_difference < 0 ? -1 : 0)) >> 1) + 2;
		if ((style->flags & 2048) != 0)
			si--;
		if (has_descender)
			si += (descender - (descender < 0 ? -1 : 0)) >> 1;
		break;
	default:
		text_x = button_hcenter_x(style, bx1, bx2 - bx1,
		    ext_x, ext_width, width, bx2);
		si = ext_y2 + 3;
		break;
	}
	if ((style->flags & 16) != 0)
		si += 2;
	if (style->type != 1 || !has_descender)
		si--;
	text_y = si - metric->top;
	if ((style->flags & 32) != 0) {
		rip->x = text_x + 1 - rip->viewport.sx;
		rip->y = text_y + 1 - rip->viewport.sy;
		rip->colour = inverted ? 15 ^ style->shadow_label
		    : style->shadow_label;
		/* v1.10a draws the shadow one glyph at a time, without the
		 * vertical text layout performed for the foreground label. */
		for (const unsigned char *p = (const unsigned char *)label;
		    *p != '\0'; p++)
			draw_rip_char(rip, *p);
	}
	if ((style->flags & 2048) != 0 || (style->flags2 & 2) != 0) {
		for (int i = 0; label[i] != '\0'; i++) {
			if (ascii_to_upper((unsigned char)label[i])
			    == ascii_to_upper((unsigned char)hotkey)) {
				hotkey_position = i;
				break;
			}
		}
	}
	rip->x = text_x - rip->viewport.sx;
	rip->y = text_y - rip->viewport.sy;
	rip->colour = inverted ? 15 ^ style->foreground : style->foreground;
	if (hotkey_position >= 0 && (style->flags2 & 2) != 0) {
		char *copy = strdup(label);

		if (copy == NULL) {
			rip->error = IO_ERROR_OUT_OF_MEMORY;
			goto restore;
		}
		copy[hotkey_position] = '\0';
		int prefix_width = rip_text_width(rip, copy);
		char hotkey_text[2] = {label[hotkey_position], '\0'};
		int hotkey_width = rip_text_width(rip, hotkey_text);

		for (int i = 0; i < hotkey_position; i++)
			draw_rip_char(rip, (unsigned char)label[i]);
		rip->x = text_x + prefix_width - rip->viewport.sx;
		rip->colour = inverted ? 15 ^ style->underline
		    : style->underline;
		draw_rip_char(rip, (unsigned char)label[hotkey_position]);
		rip->x = text_x + prefix_width + hotkey_width
		    - rip->viewport.sx;
		rip->y = text_y - rip->viewport.sy;
		rip->colour = inverted ? 15 ^ style->foreground
		    : style->foreground;
		for (const unsigned char *p = (const unsigned char *)label
		    + hotkey_position + 1; *p != '\0'; p++)
			draw_rip_char(rip, *p);
		free(copy);
	}
	else
		draw_rip_text(rip, label);
	if (hotkey_position >= 0 && (style->flags & 2048) != 0) {
		char *copy = strdup(label);

		if (copy == NULL) {
			rip->error = IO_ERROR_OUT_OF_MEMORY;
			goto restore;
		}
		copy[hotkey_position] = '\0';
		char hotkey_text[2] = {label[hotkey_position], '\0'};
		int prefix_width = rip_text_width(rip, copy);
		int hotkey_width = rip_text_width(rip, hotkey_text);
		int bottom = cterm_rip_button_char_descends(
		    (uint8_t)label[hotkey_position]) ? metric->drop : metric->base;
		int underline_y = text_y + bottom + 3
		    + ((style->flags & 32) != 0);

		button_line(rip, text_x + prefix_width, underline_y,
		    text_x + prefix_width + hotkey_width - 2,
		    underline_y,
		    map_colour(rip, inverted ? 15 ^ style->underline
		    : style->underline));
		free(copy);
	}
restore:
	rip->x = old_x;
	rip->y = old_y;
	rip->colour = old_colour;
}

int
cterm_rip_raster_draw_button(struct cterm_rip_raster *rip,
    struct cterm_rip_button_draw *button, bool inverted)
{
	struct cterm_rip_button_style old_style;
	char *converted_label = NULL;
	const char *label;
	bool unicode_text;
	int old_font_number, old_font_size, old_font_vertical;
	int x1, y1, x2, y2;

	if (rip == NULL || button == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (button->style.type < 0 || button->style.type > 2)
		return IO_ERROR_NOT_SUPPORTED;
	label = button->label;
	unicode_text = rip->unicode_text;
	if (unicode_text && label != NULL) {
		font_text_bytes(rip, label, &converted_label);
		if (converted_label == NULL)
			return rip->error;
		label = converted_label;
	}
	old_style = rip->button_style;
	old_font_number = rip->font_number;
	old_font_size = rip->font_size;
	old_font_vertical = rip->font_vertical;
	rip->button_style = button->style;
	rip->font_number = button->font_number;
	rip->font_size = button->font_size;
	rip->font_vertical = button->font_vertical;
	x1 = button->x1 - rip->viewport.sx;
	y1 = button->y1 - rip->viewport.sy;
	x2 = button->x2 - rip->viewport.sx;
	y2 = button->y2 - rip->viewport.sy;
	draw_plain_button(rip, x1, y1, x2, y2, inverted,
	    button->icon, &button->x2, &button->y2);
	if ((button->style.flags & 64) != 0 && rip->error == 0)
		capture_button_stamp(rip, button);
	x2 = button->x2 - rip->viewport.sx;
	y2 = button->y2 - rip->viewport.sy;
	rip->unicode_text = false;
	draw_button_label(rip, x1, y1, x2, y2, button->hotkey,
	    inverted, label);
	rip->unicode_text = unicode_text;
	rip->button_style = old_style;
	rip->font_number = old_font_number;
	rip->font_size = old_font_size;
	rip->font_vertical = old_font_vertical;
	free(converted_label);
	return rip->error;
}

int
cterm_rip_raster_command(struct cterm_rip_raster *rip,
    int level, int sublevel, uint8_t code, const char *arguments,
    bool *handled)
{
	static const uint16_t line_patterns[4] = {
		0xffff, 0xcccc, 0xfc78, 0xf8f8
	};
	static const uint8_t four_coords[4] = {2, 2, 2, 2};
	static const uint8_t style_fields[3] = {2, 4, 2};
	static const uint8_t fill_fields[2] = {2, 2};
	static const uint8_t colour_field[1] = {2};
	static const uint8_t write_mode_field[1] = {1};
	static const uint8_t border_field[1] = {2};
	static const uint8_t point_fields[2] = {2, 2};
	static const uint8_t custom_fill_fields[9] = {
		2, 2, 2, 2, 2, 2, 2, 2, 2
	};
	static const uint8_t circle_fields[3] = {2, 2, 2};
	static const uint8_t oval_fields[6] = {2, 2, 2, 2, 2, 2};
	static const uint8_t arc_fields[5] = {2, 2, 2, 2, 2};
	static const uint8_t flood_fields[3] = {2, 2, 2};
	static const uint8_t capture_fields[5] = {2, 2, 2, 2, 1};
	static const uint8_t paste_fields[4] = {2, 2, 2, 1};
	static const uint8_t icon_fields[5] = {2, 2, 2, 1, 2};
	static const uint8_t region_fields[6] = {2, 2, 2, 2, 1, 1};
	static const uint8_t font_fields[5] = {2, 2, 2, 1, 1};
	static const uint8_t palette_fields[16] = {
		2, 2, 2, 2, 2, 2, 2, 2,
		2, 2, 2, 2, 2, 2, 2, 2
	};
	static const uint8_t bezier_fields[9] = {
		2, 2, 2, 2, 2, 2, 2, 2, 2
	};
	static const uint8_t button_style_fields[15] = {
		2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 6
	};
	static const uint8_t button_fields[7] = {
		2, 2, 2, 2, 2, 1, 1
	};
	int fields[6];
	int button_style[15];
	int button[7];
	int custom_fill[9];
	int bezier[9];
	int palette_values[16];
	int count;

	if (rip == NULL || arguments == NULL || handled == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (level == 1 && sublevel == 0 && code == 'G') {
		/* The legacy RIP_COPY_REGION case does not draw or change state. */
		*handled = true;
		return rip->error;
	}
	if (level == 1 && sublevel == 0 && code == 'W') {
		*handled = true;
		if (arguments[0] != '\0' && arguments[1] != '\0')
			write_icon(rip, arguments + 1);
		return rip->error;
	}
	if (level == 1 && sublevel == 0 && code == 'B') {
		int type_bits;

		*handled = true;
		count = parse_fields(arguments, button_style_fields, 15,
		    button_style);
		if (count != 15 || button_style[2] > 4)
			return rip->error;
		type_bits = button_style[3] & (1 | 128 | 256);
		if ((type_bits & (type_bits - 1)) != 0)
			return rip->error;
		for (size_t i = 5; i <= 10; i++) {
			if (button_style[i] > 255)
				return rip->error;
		}
		if (button_style[12] > 255 || button_style[13] > 255)
			return rip->error;
		struct cterm_rip_button_style *style = &rip->button_style;

		style->width = button_style[0];
		style->height = button_style[1];
		style->orientation = button_style[2];
		style->flags = button_style[3];
		style->bevel_size = button_style[4];
		style->foreground = button_style[5];
		style->shadow_label = button_style[6];
		style->highlight = button_style[7];
		style->shadow = button_style[8];
		style->surface = button_style[9];
		style->group = button_style[10];
		style->flags2 = button_style[11];
		style->underline = button_style[12];
		style->corner = button_style[13];
		if (type_bits != 0)
			style->type = type_bits == 1 ? 1
			    : type_bits == 128 ? 0 : 2;
		return rip->error;
	}
	if (level == 1 && sublevel == 0 && code == 'U') {
		const char *text;
		const char *label_start;
		const char *command_start = NULL;
		char *icon = NULL;
		char *label = NULL;
		bool selected_command = false;
		const struct cterm_rip_button_style *style = &rip->button_style;

		*handled = false;
		if (strlen(arguments) < 12)
			return rip->error;
		text = arguments + 12;
		if (style->type < 0 || style->type > 2)
			return rip->error;
		count = parse_fields(arguments, button_fields, 7, button);
		if (count != 7) {
			*handled = true;
			return rip->error;
		}
		label_start = strstr(text, "<>");
		if (label_start != NULL && label_start != text) {
			icon = strndup(text, (size_t)(label_start - text));
			if (icon == NULL)
				return IO_ERROR_OUT_OF_MEMORY;
		}
		else if (label_start == NULL && text[0] != '\0') {
			icon = strdup(text);
			if (icon == NULL)
				return IO_ERROR_OUT_OF_MEMORY;
		}
		if (label_start != NULL) {
			label_start += 2;
			command_start = strstr(label_start, "<>");
			selected_command = command_start != NULL
			    && (button[5] & 1) != 0
			    && command_start[2] != '\0';
			if (command_start == NULL)
				command_start = label_start + strlen(label_start);
			if (command_start != label_start) {
				size_t size = (size_t)(command_start - label_start);

				label = malloc(size + 1);
				if (label == NULL) {
					free(icon);
					return IO_ERROR_OUT_OF_MEMORY;
				}
				memcpy(label, label_start, size);
				label[size] = '\0';
			}
		}
		*handled = (style->flags & 1024) == 0
		    && !selected_command;
		rip->last_button_style = *style;
		struct cterm_rip_button_draw drawing = {
			.style = *style,
			.x1 = button[0] + rip->viewport.sx,
			.y1 = button[1] + rip->viewport.sy,
			.x2 = button[2] + rip->viewport.sx,
			.y2 = button[3] + rip->viewport.sy,
			.hotkey = button[4],
			.font_number = rip->font_number,
			.font_size = rip->font_size,
			.font_vertical = rip->font_vertical,
			.icon = icon,
			.label = label,
		};
		cterm_rip_raster_draw_button(rip, &drawing,
		    (button[5] & 1) != 0);
		rip->last_button_x2 = drawing.x2;
		rip->last_button_y2 = drawing.y2;
		if ((drawing.style.flags & 64) != 0) {
			rip->button_style.type = 1;
			rip->button_style.flags &= ~(8 | 64 | 256 | 512 | 32768);
			rip->button_style.flags |= 1;
		}
		free(icon);
		free(label);
		return rip->error;
	}
	if (level == 1 && sublevel == 0
	    && (code == 'C' || code == 'P' || code == 'I'
	        || code == 'T' || code == 't' || code == 'E')) {
		*handled = true;
		if (code == 'C') {
			count = parse_fields(arguments, capture_fields, 5,
			    fields);
			if (count == 5)
				capture_image(rip, fields[0], fields[1],
				    fields[2], fields[3]);
		}
		else if (code == 'P') {
			count = parse_fields(arguments, paste_fields, 4,
			    fields);
			if (count == 4)
				paste_image(rip, fields[0], fields[1],
				    fields[2]);
		}
		else if (code == 'I') {
			count = parse_fields(arguments, icon_fields, 5,
			    fields);
			if (count == 5 && strlen(arguments) >= 9)
				(void)load_icon(rip, fields[0], fields[1],
				    fields[2], fields[3] != 0,
				    arguments + 9, NULL, NULL);
		}
		else if (code == 'T') {
			count = parse_fields(arguments, region_fields, 6,
			    fields);
			if (count == 6) {
				rip->text_region.sx = fields[0];
				rip->text_region.sy = fields[1];
				rip->text_region.ex = fields[2];
				rip->text_region.ey = fields[3];
				rip->text_region.xpos = 0;
				rip->text_region.ypos = 0;
				rip->text_region.line_height = rip_font_height(rip);
			}
		}
		else if (code == 't') {
			int consumed = 0;
			int justify = parse_mega(arguments, 1, &consumed);

			if (consumed == 1)
				draw_region_text(rip, justify, arguments + 1);
		}
		else {
			rip->text_region.sx = -1;
			rip->text_region.sy = -1;
			rip->text_region.ex = -1;
			rip->text_region.ey = -1;
			rip->text_region.xpos = 0;
			rip->text_region.ypos = 0;
		}
		return rip->error;
	}
	*handled = level == 0 && sublevel == 0
	    && (code == 'S' || code == 'B' || code == 'c'
	        || code == 'f'
	        || code == '=' || code == 'L' || code == 'R'
	        || code == 'W' || code == 'X'
	        || code == 'P' || code == 'p' || code == 'l'
	        || code == 'm' || code == 's' || code == 'v'
	        || code == 'E' || code == 'C' || code == 'O'
	        || code == 'V' || code == 'o' || code == 'A'
	        || code == 'I' || code == 'i'
	        || code == 'N' || code == 'Z' || code == 'F'
	        || code == 'Q' || code == 'a' || code == 'Y'
	        || code == '@' || code == 'T' || code == 'K'
	        || code == 'j' || code == 't' || code == 'z'
	        || code == 'x');
	if (!*handled)
		return 0;
	switch (code) {
	case 'f':
		count = parse_fields(arguments, point_fields, 2, fields);
		if (count == 2 && fields[0] > 0 && fields[1] > 0) {
			refresh_screen_size(rip);
			if (rip->error != 0)
				break;
			rip->x_dim = fields[0];
			rip->y_dim = fields[1];
			rip->x_max = rip->screen_width < (uint32_t)fields[0]
			    ? (int)rip->screen_width : fields[0];
			rip->y_max = rip->screen_height < (uint32_t)fields[1]
			    ? (int)rip->screen_height : fields[1];
			rip->viewport.sx = 0;
			rip->viewport.sy = 0;
			rip->viewport.ex = fields[0] - 1;
			rip->viewport.ey = fields[1] - 1;
		}
		break;
	case 't':
	case 'z':
	case 'x':
		poly_bezier(rip, arguments, code == 'x', code != 't');
		break;
	case 'E':
		erase_view(rip);
		break;
	case 'N':
		count = parse_fields(arguments, border_field, 1, fields);
		if (count == 1)
			rip->borders = fields[0] != 0;
		break;
	case 'C':
		count = parse_fields(arguments, circle_fields, 3, fields);
		if (count == 3)
			full_ellipse(rip, fields[0], fields[1], 0, 360,
			    fields[2], fields[2] * 7750 / 10000, false,
			    map_colour(rip, rip->colour), NULL);
		break;
	case 'O':
	case 'V':
		count = parse_fields(arguments, oval_fields, 6, fields);
		if (count == 6)
			full_ellipse(rip, fields[0], fields[1], fields[2],
			    fields[3], fields[4], fields[5], false,
			    map_colour(rip, rip->colour), NULL);
		break;
	case 'o':
		count = parse_fields(arguments, four_coords, 4, fields);
		if (count == 4)
			full_ellipse(rip, fields[0], fields[1], 0, 360,
			    fields[2], fields[3], true,
			    map_colour(rip, rip->colour), NULL);
		break;
	case 'A':
		count = parse_fields(arguments, arc_fields, 5, fields);
		if (count == 5)
			full_ellipse(rip, fields[0], fields[1], fields[2],
			    fields[3], fields[4], fields[4] * 7750 / 10000,
			    false, map_colour(rip, rip->colour), NULL);
		break;
	case 'I':
		count = parse_fields(arguments, arc_fields, 5, fields);
		if (count == 5)
			draw_pie(rip, fields[0], fields[1], fields[2],
			    fields[3], fields[4],
			    fields[4] * 7750 / 10000, true);
		break;
	case 'i':
		count = parse_fields(arguments, oval_fields, 6, fields);
		if (count == 6)
			draw_pie(rip, fields[0], fields[1], fields[2],
			    fields[3], fields[4], fields[5], false);
		break;
	case 'Z':
		count = parse_fields(arguments, bezier_fields, 9, bezier);
		if (count == 9) {
			if (bezier[8] <= 0)
				bezier[8] = 0x3fc;
			draw_bezier(rip, bezier, bezier[8]);
		}
		break;
	case 'F':
		count = parse_fields(arguments, flood_fields, 3, fields);
		if (count == 3)
			flood_fill(rip, fields[0], fields[1], fields[2]);
		break;
	case 'Q':
		count = parse_fields(arguments, palette_fields, 16,
		    palette_values);
		if (count == 16) {
			for (int i = 0; i < 16; i++) {
				if (palette_values[i] > 63)
					break;
				rip->master_palette[i] = (uint8_t)palette_values[i];
			}
			set_master_palette(rip);
		}
		break;
	case 'a':
		count = parse_fields(arguments, point_fields, 2, fields);
		if (count == 2 && fields[0] < 16 && fields[1] <= 63) {
			rip->master_palette[fields[0]] = (uint8_t)fields[1];
			set_master_palette(rip);
		}
		break;
	case 'Y':
		count = parse_fields(arguments, font_fields, 5, fields);
		if (count == 5) {
			if (fields[0] <= 10)
				rip->font_number = fields[0];
			rip->font_vertical = fields[1];
			if (fields[2] > 0 && fields[2] <= 10)
				rip->font_size = fields[2];
		}
		break;
	case '@':
		count = parse_fields(arguments, point_fields, 2, fields);
		if (count == 2 && strlen(arguments) >= 4) {
			rip->x = fields[0];
			rip->y = fields[1];
			draw_rip_text(rip, arguments[4] == '\0' ? " " : arguments + 4);
		}
		break;
	case 'T':
		draw_rip_text(rip, arguments[0] == '\0' ? " " : arguments);
		break;
	case 'S':
		count = parse_fields(arguments, fill_fields, 2, fields);
		if (count == 2 && fields[0] <= 11 && fields[1] <= 255)
			cterm_rip_raster_fill_style(rip, fields[0], fields[1]);
		break;
	case 'c':
		count = parse_fields(arguments, colour_field, 1, fields);
		if (count == 1 && fields[0] <= 255)
			rip->colour = fields[0] & 15;
		break;
	case 'W':
		count = parse_fields(arguments, write_mode_field, 1, fields);
		if (count == 1 && fields[0] <= 1)
			rip->xor_mode = fields[0] == 1;
		break;
	case 'm':
		count = parse_fields(arguments, point_fields, 2, fields);
		if (count == 2
		    && fields[0] <= rip->viewport.ex - rip->viewport.sx + 1
		    && fields[1] <= rip->viewport.ey - rip->viewport.sy + 1) {
			rip->x = fields[0];
			rip->y = fields[1];
		}
		break;
	case 's':
		count = parse_fields(arguments, custom_fill_fields, 9,
		    custom_fill);
		if (count == 9) {
			for (int i = 0; i < 8; i++)
				rip->fill_pattern[i] = custom_fill[i] & 0xff;
			if (custom_fill[8] <= 255) {
				rip->fill_colour = custom_fill[8];
				rip->fill_style = -1;
			}
		}
		break;
	case 'v':
		count = parse_fields(arguments, four_coords, 4, fields);
		if (count == 4) {
			bool disabled = fields[0] == 0 && fields[1] == 0
			    && fields[2] == 0 && fields[3] == 0;

			if (!disabled) {
				if (fields[2] >= rip->x_dim)
					fields[2] = rip->x_dim - 1;
				if (fields[3] >= rip->y_dim)
					fields[3] = rip->y_dim - 1;
				if (fields[0] >= fields[2]
				    || fields[1] >= fields[3])
					break;
			}
			rip->viewport.sx = fields[0];
			rip->viewport.sy = fields[1];
			rip->viewport.ex = fields[2];
			rip->viewport.ey = fields[3];
		}
		break;
	case 'X':
		count = parse_fields(arguments, point_fields, 2, fields);
		if (count == 2 && !no_viewport(rip))
			set_pixel(rip, fields[0], fields[1],
			    map_colour(rip, rip->colour));
		break;
	case 'j':
		count = parse_fields(arguments, point_fields, 2, fields);
		if (count == 2 && !no_viewport(rip))
			draw_line(rip, fields[0], fields[1], fields[0], fields[1]);
		break;
	case '=':
		count = parse_fields(arguments, style_fields, 3, fields);
		if (count == 3 && fields[0] <= 4 && fields[2] <= 3) {
			rip->line_pattern = fields[0] < 4
			    ? line_patterns[fields[0]] : (uint16_t)fields[1];
			rip->line_width = fields[2];
			rip->line_style = fields[0];
		}
		break;
	case 'B':
	case 'K':
	case 'L':
	case 'R':
		count = parse_fields(arguments, four_coords, 4, fields);
		if (count == 4) {
			if (code == 'B')
				cterm_rip_raster_bar(rip, fields[0], fields[1],
				    fields[2], fields[3]);
			else if (code == 'K' && !no_viewport(rip)) {
				if (fields[0] > fields[2]) {
					int temp = fields[0];
					fields[0] = fields[2];
					fields[2] = temp;
				}
				if (fields[1] > fields[3]) {
					int temp = fields[1];
					fields[1] = fields[3];
					fields[3] = temp;
				}
				cterm_rip_raster_bar(rip, fields[0], fields[1],
				    fields[2], fields[3]);
				if (rip->borders)
					cterm_rip_raster_rectangle(rip, fields[0],
					    fields[1], fields[2], fields[3]);
			}
			else if (code == 'L')
				cterm_rip_raster_line(rip, fields[0], fields[1],
				    fields[2], fields[3]);
			else
				cterm_rip_raster_rectangle(rip, fields[0], fields[1],
				    fields[2], fields[3]);
		}
		break;
	case 'P':
	case 'p':
	case 'l': {
		struct rip_point points[512];
		const char *cursor = arguments;
		int consumed = 0;
		int vertices = parse_mega(cursor, 2, &consumed);
		bool valid = true;

		if (consumed == 0 || vertices < 2 || vertices > 512)
			break;
		cursor += consumed;
		for (int i = 0; i < vertices; i++) {
			points[i].x = parse_mega(cursor, 2, &consumed);
			if (consumed == 0) {
				valid = false;
				break;
			}
			cursor += consumed;
			points[i].y = parse_mega(cursor, 2, &consumed);
			if (consumed == 0) {
				valid = false;
				break;
			}
			cursor += consumed;
		}
		/* The legacy descriptor parses every remaining base-36 field,
		 * but the drawing command uses only the declared vertices. */
		size_t extra_fields = 0;
		while (valid && *cursor != '\0') {
			if (extra_fields >= 1025U - (1U + 2U * (size_t)vertices)
			    || parse_mega(cursor, 2, &consumed) < 0
			    || consumed == 0) {
				valid = false;
				break;
			}
			cursor += consumed;
			++extra_fields;
		}
		if (valid && (extra_fields & 1U) == 0) {
			if (code == 'l') {
				for (int i = 1; i < vertices; i++)
					draw_line(rip, points[i - 1].x,
					    points[i - 1].y, points[i].x,
					    points[i].y);
			}
			else
				draw_polygon(rip, points, vertices,
				    code == 'p');
		}
		break;
	}
	default:
		break;
	}
	return rip->error;
}
