/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_surface.h"

#include "io_error.h"
#include "st_pixels.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct st_rip3_surface {
	uint8_t *pixels;
	uint32_t width;
	uint32_t height;
};

static unsigned
channel(unsigned dac)
{
	return (dac * 255 + 31) / 63;
}

static uint32_t
rgb(unsigned r, unsigned g, unsigned b)
{
	return (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

int
st_rip3_palette_defaults(struct st_rip3_palette *palette)
{
	static const uint32_t desktop[] = {
		0x000000, 0x0000aa, 0x00aa00, 0x00aaaa,
		0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
		0x555555, 0x5555ff, 0x55ff55, 0x55ffff,
		0xff5555, 0xff55ff, 0xffff55, 0xffffff,
	};
	static const unsigned gray[] = {
		0, 4, 8, 13, 17, 21, 25, 29, 34, 38, 42, 46, 50, 55, 59, 63,
	};
	static const unsigned red[] = {0, 10, 21, 31, 42, 52, 63};
	static const unsigned green[] = {0, 9, 18, 27, 36, 45, 54, 63};
	static const unsigned blue[] = {0, 21, 42, 63};

	if (palette == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < 16; i++) {
		unsigned level = channel(gray[i]);

		palette->rgb[i] = desktop[i];
		palette->rgb[16 + i] = rgb(level, level, level);
	}
	for (size_t r = 0; r < 7; r++) {
		for (size_t g = 0; g < 8; g++) {
			for (size_t b = 0; b < 4; b++)
				palette->rgb[32 + b + g * 4 + r * 32] =
				    rgb(channel(red[r]), channel(green[g]),
				    channel(blue[b]));
		}
	}
	return 0;
}

int
st_rip3_palette_desktop(struct st_rip3_palette *palette, size_t start,
    const unsigned *master_colors, size_t count)
{
	unsigned master_copy[16];

	if (palette == NULL || (master_colors == NULL && count != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (start > 16 || count > 16 - start)
		return IO_ERROR_OUT_OF_RANGE;
	for (size_t i = 0; i < count; i++) {
		if (master_colors[i] > 63)
			return IO_ERROR_OUT_OF_RANGE;
	}
	for (size_t i = 0; i < count; i++)
		master_copy[i] = master_colors[i];
	for (size_t i = 0; i < count; i++) {
		unsigned color = master_copy[i];
		unsigned r = (((color >> 2) & 1) * 2 + ((color >> 5) & 1)) * 85;
		unsigned g = (((color >> 1) & 1) * 2 + ((color >> 4) & 1)) * 85;
		unsigned b = ((color & 1) * 2 + ((color >> 3) & 1)) * 85;

		palette->rgb[start + i] = rgb(r, g, b);
	}
	return 0;
}

int
st_rip3_palette_rgb(struct st_rip3_palette *palette, size_t start,
    unsigned bits, const uint32_t *colors, size_t count)
{
	if (palette == NULL || (colors == NULL && count != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (bits != 8)
		return IO_ERROR_NOT_SUPPORTED;
	if (start > 256 || count > 256 - start)
		return IO_ERROR_OUT_OF_RANGE;
	for (size_t i = 0; i < count; i++) {
		if (colors[i] > UINT32_C(0xffffff))
			return IO_ERROR_OUT_OF_RANGE;
	}
	if (count != 0)
		memmove(palette->rgb + start, colors, count * sizeof(*colors));
	return 0;
}

int
st_rip3_palette_color(const struct st_rip3_palette *palette, bool direct,
    uint32_t color, uint8_t *result)
{
	unsigned best_distance = UINT_MAX;
	uint8_t best = 0;

	if (palette == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!direct) {
		if (color > 255)
			return IO_ERROR_OUT_OF_RANGE;
		*result = (uint8_t)color;
		return 0;
	}
	if (color > UINT32_C(0xffffff))
		return IO_ERROR_OUT_OF_RANGE;
	for (size_t i = 0; i < 256; i++) {
		if (palette->rgb[i] > UINT32_C(0xffffff))
			return IO_ERROR_OUT_OF_RANGE;
	}
	for (size_t i = 0; i < 256; i++) {
		int r = (int)((color >> 16) & 255) - (int)((palette->rgb[i] >> 16) & 255);
		int g = (int)((color >> 8) & 255) - (int)((palette->rgb[i] >> 8) & 255);
		int b = (int)(color & 255) - (int)(palette->rgb[i] & 255);
		unsigned distance = (unsigned)(r * r + g * g + b * b);

		if (distance < best_distance) {
			best_distance = distance;
			best = (uint8_t)i;
		}
	}
	*result = best;
	return 0;
}

int
st_rip3_surface_create(struct st_rip3_surface **result,
    uint32_t width, uint32_t height)
{
	struct st_rip3_surface *surface;

	if (result == NULL || width == 0 || height == 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (width > INT32_MAX || height > INT32_MAX
	    || (uint64_t)width * height > SIZE_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	surface = calloc(1, sizeof(*surface));
	if (surface == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	surface->pixels = calloc((size_t)width * height, 1);
	if (surface->pixels == NULL) {
		free(surface);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	surface->width = width;
	surface->height = height;
	*result = surface;
	return 0;
}

void
st_rip3_surface_destroy(struct st_rip3_surface *surface)
{
	if (surface == NULL)
		return;
	free(surface->pixels);
	free(surface);
}

int
st_rip3_surface_size(const struct st_rip3_surface *surface,
    uint32_t *width, uint32_t *height)
{
	if (surface == NULL || width == NULL || height == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*width = surface->width;
	*height = surface->height;
	return 0;
}

int
st_rip3_surface_get(const struct st_rip3_surface *surface,
    int32_t x, int32_t y, uint8_t *result)
{
	if (surface == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (x < 0 || y < 0 || (uint32_t)x >= surface->width
	    || (uint32_t)y >= surface->height)
		return IO_ERROR_OUT_OF_RANGE;
	*result = surface->pixels[(size_t)y * surface->width + (uint32_t)x];
	return 0;
}

static uint8_t
write_pixel(uint8_t destination, uint8_t source, enum st_rip3_write_mode mode)
{
	switch (mode) {
		case ST_RIP3_XOR: return destination ^ source;
		case ST_RIP3_OR: return destination | source;
		case ST_RIP3_AND: return destination & source;
		case ST_RIP3_NOT: return (uint8_t)~destination;
		default: return source;
	}
}

static bool
rect_valid(const struct st_rip3_rect *rect)
{
	return rect == NULL || (rect->x0 <= rect->x1 && rect->y0 <= rect->y1);
}

static bool
intersect(struct st_rip3_rect *area, const struct st_rip3_rect *clip)
{
	if (area->x0 < clip->x0)
		area->x0 = clip->x0;
	if (area->y0 < clip->y0)
		area->y0 = clip->y0;
	if (area->x1 > clip->x1)
		area->x1 = clip->x1;
	if (area->y1 > clip->y1)
		area->y1 = clip->y1;
	return rect_valid(area);
}

int
st_rip3_surface_fill(struct st_rip3_surface *surface,
    const struct st_rip3_rect *clip, const struct st_rip3_rect *area,
    uint8_t color, enum st_rip3_write_mode mode)
{
	struct st_rip3_rect bounds;
	struct st_rip3_rect visible;

	if (surface == NULL || area == NULL || !rect_valid(area)
	    || !rect_valid(clip) || mode < ST_RIP3_COPY || mode > ST_RIP3_NOT)
		return IO_ERROR_INVALID_ARGUMENT;
	visible = *area;
	bounds = (struct st_rip3_rect){0, 0, (int32_t)surface->width - 1,
	    (int32_t)surface->height - 1};
	if (!intersect(&visible, &bounds)
	    || (clip != NULL && !intersect(&visible, clip)))
		return 0;
	for (int32_t y = visible.y0; y <= visible.y1; y++) {
		for (int32_t x = visible.x0; x <= visible.x1; x++) {
			size_t offset = (size_t)y * surface->width + (uint32_t)x;

			surface->pixels[offset] = write_pixel(surface->pixels[offset],
			    color, mode);
		}
	}
	return 0;
}

int
st_rip3_surface_plot(struct st_rip3_surface *surface,
    const struct st_rip3_rect *clip, int32_t x, int32_t y, uint8_t color,
    enum st_rip3_write_mode mode)
{
	struct st_rip3_rect point = {x, y, x, y};

	return st_rip3_surface_fill(surface, clip, &point, color, mode);
}

static bool
surface_clip(const struct st_rip3_surface *surface,
    struct st_rip3_rect *area, const struct st_rip3_rect *clip)
{
	struct st_rip3_rect bounds = {0, 0, (int32_t)surface->width - 1,
	    (int32_t)surface->height - 1};

	return intersect(area, &bounds) && (clip == NULL || intersect(area, clip));
}

int
st_rip3_surface_copy(struct st_rip3_surface *destination,
    const struct st_rip3_rect *destination_clip,
    const struct st_rip3_rect *destination_area,
    const struct st_rip3_surface *source,
    const struct st_rip3_rect *source_clip,
    const struct st_rip3_rect *source_area,
    enum st_rip3_transfer_mode mode)
{
	struct st_rip3_rect src;
	struct st_rip3_rect dst;
	size_t sw, sh, dw, dh;
	uint8_t *saved = NULL;

	if (destination == NULL || source == NULL || destination_area == NULL
	    || source_area == NULL || !rect_valid(source_area)
	    || !rect_valid(destination_area) || !rect_valid(source_clip)
	    || !rect_valid(destination_clip)
	    || mode < ST_RIP3_TRANSFER_COPY || mode > ST_RIP3_TRANSFER_INVERT)
		return IO_ERROR_INVALID_ARGUMENT;
	src = *source_area;
	dst = *destination_area;
	if (!surface_clip(source, &src, source_clip)
	    || !surface_clip(destination, &dst, destination_clip))
		return 0;
	/* Clipped dimensions cannot overflow their surface's allocated extent. */
	sw = (size_t)(src.x1 - src.x0) + 1;
	sh = (size_t)(src.y1 - src.y0) + 1;
	dw = (size_t)(dst.x1 - dst.x0) + 1;
	dh = (size_t)(dst.y1 - dst.y0) + 1;
	if (source == destination) {
		saved = malloc(sw * sh);
		if (saved == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		for (size_t y = 0; y < sh; y++)
			memcpy(saved + y * sw,
			    source->pixels + ((size_t)src.y0 + y) * source->width
			    + (uint32_t)src.x0, sw);
	}
	for (size_t y = 0; y < dh; y++) {
		size_t sy = (size_t)(((uint64_t)y * 2 + 1) * sh / (2 * (uint64_t)dh));

		for (size_t x = 0; x < dw; x++) {
			size_t sx = (size_t)(((uint64_t)x * 2 + 1) * sw / (2 * (uint64_t)dw));
			size_t offset = ((size_t)dst.y0 + y) * destination->width
			    + (uint32_t)dst.x0 + x;
			uint8_t pixel = saved != NULL ? saved[sy * sw + sx]
			    : source->pixels[((size_t)src.y0 + sy) * source->width
			    + (uint32_t)src.x0 + sx];

			if (mode == ST_RIP3_TRANSFER_INVERT)
				destination->pixels[offset] = (uint8_t)~pixel;
			else
				destination->pixels[offset] = write_pixel(
				    destination->pixels[offset], pixel,
				    (enum st_rip3_write_mode)mode);
		}
	}
	free(saved);
	return 0;
}

int
st_rip3_surface_snapshot(const struct st_rip3_surface *surface,
    const struct st_rip3_palette *palette, struct st_pixel_buffer **result)
{
	struct st_pixel_buffer *snapshot;
	int status;

	if (surface == NULL || palette == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < 256; i++) {
		if (palette->rgb[i] > UINT32_C(0xffffff))
			return IO_ERROR_OUT_OF_RANGE;
	}
	status = st_pixel_buffer_create(&snapshot, surface->width, surface->height,
	    false);
	if (status != 0)
		return status;
	for (size_t i = 0; i < (size_t)surface->width * surface->height; i++)
		snapshot->primary[i] = ST_PIXEL_COLOR_RGB_TAG
		    | palette->rgb[surface->pixels[i]];
	*result = snapshot;
	return 0;
}
