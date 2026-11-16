/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_bitmap_stamp.h"
#include "st_rip3_ports.h"
#include "st_pixels.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdlib.h>

static uint32_t
sample(uint64_t position, uint64_t span, uint32_t source)
{
	uint64_t numerator = position*2+1, denominator = span*2;
	if (numerator <= UINT64_MAX/source)
		return (uint32_t)(numerator*source/denominator);
	/* Exact fractional multiplication for the rare >64-bit product. Every
	 * remainder step is below 3*denominator (<2^45 for valid geometry), and
	 * the final quotient is below source. No compiler integer extensions or
	 * floating-point rounding influence extreme clipped sample selection. */
	uint64_t quotient = 0, remainder = 0;
	for (unsigned bit = 31; bit != 0; bit--) {
		quotient *= 2; remainder *= 2;
		if ((source & (UINT32_C(1) << (bit-1))) != 0) remainder += numerator;
		quotient += remainder/denominator; remainder %= denominator;
	}
	return (uint32_t)quotient;
}

static int
validate(const struct st_rip3_bitmap *bitmap, const struct st_rip3_palette *palette,
    bool palette_adopted)
{
	if (bitmap == NULL || palette == NULL || bitmap->pixels == NULL
	    || bitmap->pixels->primary == NULL || bitmap->pixels->width == 0 || bitmap->pixels->height == 0)
		return IO_ERROR_INVALID_ARGUMENT;
	unsigned depth = bitmap->bit_depth;
	if (depth != 1 && depth != 4 && depth != 8 && depth != 24) return IO_ERROR_NOT_SUPPORTED;
	if (bitmap->pixels->width > INT32_MAX || bitmap->pixels->height > INT32_MAX
	    || bitmap->pixels->height > SIZE_MAX/sizeof(uint32_t)/bitmap->pixels->width)
		return IO_ERROR_OUT_OF_RANGE;
	if ((depth == 24 && bitmap->palette_size != 0)
	    || (depth != 24 && (bitmap->palette_size == 0 || bitmap->palette_size > (1u << depth))))
		return IO_ERROR_INVALID_ARGUMENT;
	if (palette_adopted && depth == 24) return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < 256; i++)
		if (palette->rgb[i] > 0xffffff) return IO_ERROR_OUT_OF_RANGE;
	for (size_t i = 0; i < bitmap->palette_size; i++) {
		if (bitmap->palette[i] > 0xffffff) return IO_ERROR_OUT_OF_RANGE;
		if (palette_adopted && bitmap->palette[i] != palette->rgb[i]) return IO_ERROR_INVALID_ARGUMENT;
	}
	for (size_t i = 0; i < (size_t)bitmap->pixels->width*bitmap->pixels->height; i++) {
		uint32_t pixel = bitmap->pixels->primary[i];
		if (depth == 24) {
			if (!ST_PIXEL_COLOR_IS_RGB(pixel) || (pixel & ~ST_PIXEL_COLOR_RGB_TAG) > 0xffffff)
				return IO_ERROR_INVALID_FORMAT;
		}
		else if (pixel >= bitmap->palette_size) return IO_ERROR_INVALID_FORMAT;
	}
	return 0;
}

struct prepared_pixel {
	uint8_t color;
	bool opaque;
};

int
st_rip3_draw_bitmap(struct st_rip3_ports *ports,
    const struct st_rip3_bitmap *bitmap, const struct st_rip3_bitmap_destination *destination,
    const struct st_rip3_palette *palette, bool palette_adopted,
    enum st_rip3_transfer_mode mode,
    const uint8_t *transparent_color)
{
	const int64_t limit = INT64_C(1024)*INT32_MAX;
	struct st_rip3_port_info info;
	struct st_rip3_rect visible;
	unsigned current;
	uint8_t mapped[256];
	int status;

	if (ports == NULL || destination == NULL || mode < ST_RIP3_TRANSFER_COPY || mode > ST_RIP3_TRANSFER_INVERT
	    || destination->x0 > destination->x1 || destination->y0 > destination->y1)
		return IO_ERROR_INVALID_ARGUMENT;
	if (destination->x0 < -limit || destination->x1 > limit || destination->y0 < -limit || destination->y1 > limit)
		return IO_ERROR_OUT_OF_RANGE;
	status = st_rip3_ports_current(ports,&current);
	if (status == 0) status = st_rip3_ports_info(ports,current,&info);
	if (status != 0 || !info.viewport_active) return status;
	visible = (struct st_rip3_rect){0,0,info.viewport.x1-info.viewport.x0,info.viewport.y1-info.viewport.y0};
	if (destination->x0 > visible.x1 || destination->y0 > visible.y1 || destination->x1 < 0 || destination->y1 < 0)
		return 0;
	if (destination->x0 > visible.x0) visible.x0 = (int32_t)destination->x0;
	if (destination->y0 > visible.y0) visible.y0 = (int32_t)destination->y0;
	if (destination->x1 < visible.x1) visible.x1 = (int32_t)destination->x1;
	if (destination->y1 < visible.y1) visible.y1 = (int32_t)destination->y1;
	status = validate(bitmap,palette,palette_adopted);
	if (status != 0) return status;
	uint32_t width = (uint32_t)(visible.x1-visible.x0)+1, height = (uint32_t)(visible.y1-visible.y0)+1;
	if (height > SIZE_MAX/sizeof(struct prepared_pixel)/width) return IO_ERROR_OUT_OF_RANGE;
	struct prepared_pixel *pixels = malloc((size_t)width*height*sizeof(*pixels));
	if (pixels == NULL) return IO_ERROR_OUT_OF_MEMORY;
	for (size_t i = 0; i < bitmap->palette_size; i++) {
		if (palette_adopted) mapped[i] = (uint8_t)i;
		else (void)st_rip3_palette_color(palette,true,bitmap->palette[i],&mapped[i]);
	}
	uint64_t span_x = (uint64_t)(destination->x1-destination->x0)+1;
	uint64_t span_y = (uint64_t)(destination->y1-destination->y0)+1;
	for (uint32_t y = 0; y < height; y++) {
		uint32_t sy = sample((uint64_t)((int64_t)visible.y0+y-destination->y0),span_y,bitmap->pixels->height);
		for (uint32_t x = 0; x < width; x++) {
			uint32_t sx = sample((uint64_t)((int64_t)visible.x0+x-destination->x0),span_x,bitmap->pixels->width);
			uint32_t code = bitmap->pixels->primary[(size_t)sy*bitmap->pixels->width+sx];
			struct prepared_pixel *pixel = &pixels[(size_t)y*width+x];
			if (bitmap->bit_depth == 24) {
				(void)st_rip3_palette_color(palette,true,code & ~ST_PIXEL_COLOR_RGB_TAG,&pixel->color);
				pixel->opaque = transparent_color == NULL || pixel->color != *transparent_color;
			}
			else {
				pixel->color = mapped[code];
				pixel->opaque = transparent_color == NULL || code != *transparent_color;
			}
			if (mode == ST_RIP3_TRANSFER_INVERT) pixel->color = (uint8_t)~pixel->color;
		}
	}
	enum st_rip3_write_mode write_mode = mode == ST_RIP3_TRANSFER_INVERT ? ST_RIP3_COPY : (enum st_rip3_write_mode)mode;
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		const struct prepared_pixel *pixel = &pixels[(size_t)y*width+x];
		if (pixel->opaque)
			(void)st_rip3_ports_plot(ports,visible.x0+(int32_t)x,visible.y0+(int32_t)y,pixel->color,write_mode);
	}
	free(pixels);
	return 0;
}
