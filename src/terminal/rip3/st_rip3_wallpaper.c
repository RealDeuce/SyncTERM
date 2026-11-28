/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "io_error.h"
#include "st_pixels.h"
#include "st_rip3_bitmap_stamp.h"
#include "st_rip3_ports.h"
#include "st_rip3_wallpaper.h"

static uint32_t
phase(int64_t value, uint32_t period)
{
	int64_t remainder = value % period;
	return (uint32_t)(remainder < 0 ? remainder + period : remainder);
}

static void
tile_row(uint32_t *destination, uint32_t width, const uint32_t *seed, uint32_t period, uint32_t start)
{
	uint32_t tile  = period < width ? period : width;
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
st_rip3_wallpaper(struct st_rip3_ports *ports, const struct st_rip3_bitmap_destination *seed, bool stagger,
    const struct st_rip3_palette *palette)
{
	struct st_rip3_port_info info;
	unsigned                 current;
	int                      status;

	if (ports == NULL || seed == NULL || seed->x0 > seed->x1 || seed->y0 > seed->y1)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rip3_ports_current(ports, &current);
	if (status == 0)
		status = st_rip3_ports_info(ports, current, &info);
	if (status != 0 || !info.viewport_active)
		return status;
	int32_t xmax = info.viewport.x1 - info.viewport.x0, ymax = info.viewport.y1 - info.viewport.y0;
	if (seed->x0 < 0 || seed->y0 < 0 || seed->x0 > xmax || seed->y0 > ymax)
		return IO_ERROR_OUT_OF_RANGE;
	int32_t left = (int32_t)seed->x0, top = (int32_t)seed->y0;
	int32_t right  = seed->x1 > xmax ? xmax : (int32_t)seed->x1;
	int32_t bottom = seed->y1 > ymax ? ymax : (int32_t)seed->y1;
	if (left == 0 && top == 0 && right == xmax && bottom == ymax)
		return 0;
	const struct st_rip3_rect rectangle = {
	    left < info.clip.x0 ? info.clip.x0 : left,
	    top < info.clip.y0 ? info.clip.y0 : top,
	    right > info.clip.x1 ? info.clip.x1 : right,
	    bottom > info.clip.y1 ? info.clip.y1 : bottom,
	};
	if (rectangle.x0 > rectangle.x1 || rectangle.y0 > rectangle.y1)
		return 0;
	uint32_t                period_x = (uint32_t)(right - left) + 1;
	uint32_t                period_y = (uint32_t)(bottom - top) + 1;
	struct st_pixel_buffer *tile     = NULL;
	status = st_rip3_ports_capture_pixels(ports, current, &rectangle, true, true, &tile);
	if (status != 0)
		return status;

	uint32_t width  = (uint32_t)(info.clip.x1 - info.clip.x0) + 1;
	uint32_t height = (uint32_t)(info.clip.y1 - info.clip.y0) + 1;
	/* Publish the screen in one region. Offscreen ports need only one
	 * scratch row because no native display update occurs there. */
	uint32_t              rows   = info.offscreen ? 1 : height;
	size_t                planes = tile->alternate == NULL ? 1 : 2;
	uint32_t             *pixels = NULL;
	struct st_pixel_mask *mask   = NULL;
	if (rows > SIZE_MAX / sizeof(*pixels) / planes / width)
		status = IO_ERROR_OUT_OF_RANGE;
	else if ((pixels = malloc((size_t)width * rows * planes * sizeof(*pixels))) == NULL)
		status = IO_ERROR_OUT_OF_MEMORY;
	else if (tile->width != period_x || tile->height != period_y) {
		status = st_pixel_mask_create(&mask, width, rows);
		if (status == 0)
			memset(pixels, 0, (size_t)width * rows * planes * sizeof(*pixels));
	}
	if (status == 0) {
		struct st_pixel_buffer band = {.width = width,
		    .height                           = rows,
		    .primary                          = pixels,
		    .alternate                        = planes == 2 ? pixels + (size_t)width * rows : NULL};
		if (tile->fast_blink)
			status = st_pixel_buffer_ensure_fast_blink(&band);
		for (uint32_t offset = 0; status == 0 && offset < height; offset += rows) {
			int32_t y = info.clip.y0 + (int32_t)offset;
			if (mask != NULL)
				memset(mask->bits, 0, ((size_t)width * rows + 7) / 8);
			for (uint32_t row = 0; row < rows; row++) {
				int64_t dy = (int64_t)y + row - top, tile_row_number = dy / period_y;
				if (dy < 0 && dy % period_y != 0)
					tile_row_number--;
				int64_t  shift = stagger && tile_row_number % 2 != 0 ? period_x / 2 : 0;
				uint32_t sx    = phase((int64_t)info.clip.x0 - left + shift, period_x);
				int64_t  sy    = (int64_t)top + phase(dy, period_y);
				size_t   to    = (size_t)row * width;
				if (sy < rectangle.y0 || sy > rectangle.y1)
					continue;
				size_t from = (size_t)(sy - rectangle.y0) * tile->width;
				if (mask == NULL && band.fast_blink) {
					for (uint32_t x = 0; x < width; ++x)
						st_pixel_set_bit(band.fast_blink, to + x,
						    st_pixel_bit(tile->fast_blink, from + ((uint64_t)sx + x) % period_x));
				}
				if (mask == NULL) {
					tile_row(band.primary + to, width, tile->primary + from, period_x, sx);
					if (band.alternate != NULL)
						tile_row(band.alternate + to, width, tile->alternate + from,
						    period_x, sx);
				}
				else {
					/* The clipped snapshot retains its position within the
					 * full logical period, including where it has no pixels. */
					for (uint32_t x = 0; x < width; x++) {
						int64_t sample = (int64_t)left + sx;
						if (++sx == period_x)
							sx = 0;
						if (sample < rectangle.x0 || sample > rectangle.x1)
							continue;
						size_t at            = from + (size_t)(sample - rectangle.x0);
						band.primary[to + x] = tile->primary[at];
						if (band.fast_blink)
							st_pixel_set_bit(band.fast_blink, to + x, st_pixel_bit(tile->fast_blink, at));
						if (band.alternate != NULL)
							band.alternate[to + x] = tile->alternate[at];
						(void)st_pixel_mask_set(mask, x, row, true);
					}
				}
			}
			status = st_rip3_ports_write_viewport_pixels(ports, current, info.clip.x0, y, &band, mask,
			    palette);
		}
		free(band.fast_blink);
	}
	st_pixel_mask_destroy(mask);
	free(pixels);
	st_pixel_buffer_destroy(tile);
	return status;
}
