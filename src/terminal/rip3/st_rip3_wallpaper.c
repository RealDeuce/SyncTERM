/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_wallpaper.h"
#include "st_rip3_bitmap_stamp.h"
#include "st_rip3_ports.h"
#include "st_pixels.h"
#include "io_error.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t
phase(int64_t value, uint32_t period)
{
	int64_t remainder = value%period;
	return (uint32_t)(remainder < 0 ? remainder+period : remainder);
}

static void
tile_row(uint32_t *destination, uint32_t width,
    const uint32_t *seed, uint32_t period, uint32_t start)
{
	uint32_t tile = period < width ? period : width;
	uint32_t first = period - start < tile ? period - start : tile;
	memcpy(destination, seed + start, (size_t)first * sizeof(*destination));
	if (first < tile)
		memcpy(destination + first, seed, (size_t)(tile - first) * sizeof(*destination));
	/* Copy runs from the row already built, including for one-pixel seeds. */
	for (uint32_t filled = tile; filled < width;) {
		uint32_t count = filled < width - filled ? filled : width - filled;
		memcpy(destination + filled, destination, (size_t)count * sizeof(*destination));
		filled += count;
	}
}

int
st_rip3_wallpaper(struct st_rip3_ports *ports,
    const struct st_rip3_bitmap_destination *seed, bool stagger,
    const struct st_rip3_palette *palette)
{
	struct st_rip3_port_info info;
	unsigned current;
	int status;

	if (ports == NULL || seed == NULL || seed->x0 > seed->x1 || seed->y0 > seed->y1) return IO_ERROR_INVALID_ARGUMENT;
	status = st_rip3_ports_current(ports,&current);
	if (status == 0) status = st_rip3_ports_info(ports,current,&info);
	if (status != 0 || !info.viewport_active) return status;
	int32_t xmax = info.viewport.x1-info.viewport.x0, ymax = info.viewport.y1-info.viewport.y0;
	if (seed->x0 < 0 || seed->y0 < 0 || seed->x0 > xmax || seed->y0 > ymax) return IO_ERROR_OUT_OF_RANGE;
	int32_t left = (int32_t)seed->x0, top = (int32_t)seed->y0;
	int32_t right = seed->x1 > xmax ? xmax : (int32_t)seed->x1;
	int32_t bottom = seed->y1 > ymax ? ymax : (int32_t)seed->y1;
	if (left == 0 && top == 0 && right == xmax && bottom == ymax) return 0;
	if (info.clip.x1 < 0 || info.clip.y1 < 0) return 0;
	/* A seed crossing the physical bitmap still needs original-client pixel
	 * policy reconciliation. Do not shrink its period or invent padding. */
	if (right > info.clip.x1 || bottom > info.clip.y1) return IO_ERROR_NOT_SUPPORTED;
	const struct st_rip3_rect rectangle = {left, top, right, bottom};
	struct st_pixel_buffer *tile = NULL;
	status = st_rip3_ports_capture_pixels(ports, current, &rectangle, true, true, &tile);
	if (status != 0)
		return status;

	uint32_t width = (uint32_t)(info.clip.x1-info.clip.x0)+1;
	uint32_t height = (uint32_t)(info.clip.y1-info.clip.y0)+1;
	/* Publish the screen in one region. Offscreen ports need only one
	 * scratch row because no native display update occurs there. */
	uint32_t rows = info.offscreen ? 1 : height;
	size_t planes = tile->alternate == NULL ? 1 : 2;
	uint32_t *pixels = NULL;
	if (rows > SIZE_MAX / sizeof(*pixels) / planes / width)
		status = IO_ERROR_OUT_OF_RANGE;
	else if ((pixels = malloc((size_t)width * rows * planes * sizeof(*pixels))) == NULL)
		status = IO_ERROR_OUT_OF_MEMORY;
	else {
		struct st_pixel_buffer band = {.width=width, .height=rows, .primary=pixels,
		    .alternate=planes == 2 ? pixels + (size_t)width * rows : NULL};
		for (uint32_t offset = 0; status == 0 && offset < height; offset += rows) {
			int32_t y = info.clip.y0 + (int32_t)offset;
			for (uint32_t row = 0; row < rows; row++) {
				int64_t dy = (int64_t)y + row - top, tile_row_number = dy / tile->height;
				if (dy < 0 && dy % tile->height != 0)
					tile_row_number--;
				int64_t shift = stagger && tile_row_number % 2 != 0 ? tile->width / 2 : 0;
				uint32_t sx = phase((int64_t)info.clip.x0 - left + shift, tile->width);
				size_t from = (size_t)phase(dy, tile->height) * tile->width;
				size_t to = (size_t)row * width;
				tile_row(band.primary + to, width, tile->primary + from, tile->width, sx);
				if (band.alternate != NULL)
					tile_row(band.alternate + to, width, tile->alternate + from, tile->width, sx);
			}
			status = st_rip3_ports_write_viewport_pixels(ports, current, info.clip.x0, y, &band, palette);
		}
	}
	free(pixels);
	st_pixel_buffer_destroy(tile);
	return status;
}
