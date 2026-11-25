/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_bitmap_stamp.h"
#include "st_rip3_ports.h"
#include "st_pixels.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdlib.h>

int
st_rip3_bitmap_translate_key(const struct st_rip3_palette *palette,
    unsigned direct_index_limit, uint32_t color, uint8_t *result)
{
	if (palette == NULL || result == NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (color > 255 || (direct_index_limit != 2 && direct_index_limit != 16 && direct_index_limit != 236))
		return IO_ERROR_OUT_OF_RANGE;
	for (unsigned i = 0; i < 256; i++) if (palette->rgb[i] > 0xffffff) return IO_ERROR_OUT_OF_RANGE;
	uint8_t best = (uint8_t)color;
	if (color >= direct_index_limit) {
		uint32_t rgb = palette->rgb[color], distance = UINT32_MAX;
		for (unsigned i = 0; i < 256; i++) {
			int32_t r = (int32_t)((rgb>>16)&255)-(int32_t)((palette->rgb[i]>>16)&255);
			int32_t g = (int32_t)((rgb>>8)&255)-(int32_t)((palette->rgb[i]>>8)&255);
			int32_t b = (int32_t)(rgb&255)-(int32_t)(palette->rgb[i]&255);
			uint32_t next = (uint32_t)(r*r+g*g+b*b);
			if (next <= distance) { distance = next; best = (uint8_t)i; }
			if (next == 0) break;
		}
	}
	*result = best; return 0;
}

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
	if (depth != 1 && depth != 4 && depth != 8 && depth != 16 && depth != 24 && depth != 32) return IO_ERROR_NOT_SUPPORTED;
	if (bitmap->pixels->width > INT32_MAX || bitmap->pixels->height > INT32_MAX
	    || bitmap->pixels->height > SIZE_MAX/sizeof(uint32_t)/bitmap->pixels->width)
		return IO_ERROR_OUT_OF_RANGE;
	if ((depth > 8 && bitmap->palette_size != 0)
	    || (depth <= 8 && (bitmap->palette_size == 0 || bitmap->palette_size > (1u << depth))))
		return IO_ERROR_INVALID_ARGUMENT;
	if (palette_adopted && depth > 8) return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < 256; i++)
		if (palette->rgb[i] > 0xffffff) return IO_ERROR_OUT_OF_RANGE;
	for (size_t i = 0; i < bitmap->palette_size; i++) {
		if (bitmap->palette[i] > 0xffffff) return IO_ERROR_OUT_OF_RANGE;
		if (palette_adopted && bitmap->palette[i] != palette->rgb[i]) return IO_ERROR_INVALID_ARGUMENT;
	}
	for (size_t i = 0; i < (size_t)bitmap->pixels->width*bitmap->pixels->height; i++) {
		uint32_t pixel = bitmap->pixels->primary[i];
		if (depth > 8) {
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

struct dither_color {
	uint8_t nearest, other;
	uint32_t numerator, denominator;
};

static struct dither_color
dither_prepare(const struct st_rip3_palette *palette, uint32_t rgb)
{
	struct dither_color result = {.denominator=1};
	(void)st_rip3_palette_color(palette,true,rgb,&result.nearest);
	result.other = result.nearest;
	uint32_t base = palette->rgb[result.nearest];
	if (base == rgb) return result;
	int32_t channels[3], delta[3];
	for (unsigned c = 0; c < 3; c++) {
		unsigned shift = 16-c*8;
		channels[c] = (int32_t)((base>>shift)&255);
		delta[c] = (int32_t)((rgb>>shift)&255)-channels[c];
	}
	/* Choose the segment from the nearest colour which best approximates
	 * the requested RGB. Projection weight dot/length^2 determines the mix.
	 * Nearest-colour selection implies 0 < dot <= length^2/2. Compare exact
	 * squared-error improvements without division; products are < 2^53 for
	 * three 8-bit channels. Equal improvements retain the first index. */
	for (unsigned i = 0; i < 256; i++) {
		int32_t dot = 0;
		uint32_t squared = 0;
		for (unsigned c = 0; c < 3; c++) {
			int32_t direction = (int32_t)((palette->rgb[i]>>(16-c*8))&255)-channels[c];
			dot += delta[c]*direction; squared += (uint32_t)(direction*direction);
		}
		if (dot > 0 && (uint64_t)dot*(uint32_t)dot*result.denominator
		    > (uint64_t)result.numerator*result.numerator*squared) {
			result.other = (uint8_t)i; result.numerator = (uint32_t)dot; result.denominator = squared;
		}
	}
	return result;
}

static uint8_t
dither_pixel(const struct dither_color *color, uint64_t x, uint64_t y)
{
	static const uint8_t order[64] = {
		0,48,12,60,3,51,15,63,32,16,44,28,35,19,47,31,
		8,56,4,52,11,59,7,55,40,24,36,20,43,27,39,23,
		2,50,14,62,1,49,13,61,34,18,46,30,33,17,45,29,
		10,58,6,54,9,57,5,53,42,26,38,22,41,25,37,21,
	};
	unsigned threshold = 2u*order[(y&7)*8+(x&7)]+1u;
	return (uint64_t)color->numerator*128 > (uint64_t)color->denominator*threshold
	    ? color->other : color->nearest;
}

struct bitmap_paint {
	struct st_rip3_ports *ports;
	const struct prepared_pixel *pixels;
	struct st_rip3_rect visible;
	enum st_rip3_write_mode mode;
};

static int
paint_bitmap(void *context)
{
	const struct bitmap_paint *paint = context;
	const struct prepared_pixel *pixel = paint->pixels;
	for (int64_t y = paint->visible.y0; y <= paint->visible.y1; ++y) {
		for (int64_t x = paint->visible.x0; x <= paint->visible.x1; ++x, ++pixel) {
			if (pixel->opaque) {
				int status = st_rip3_ports_plot(paint->ports, (int32_t)x, (int32_t)y,
				    pixel->color, paint->mode);
				if (status != 0)
					return status;
			}
		}
	}
	return 0;
}

static int
draw_bitmap(struct st_rip3_ports *ports,
    const struct st_rip3_bitmap *bitmap, const struct st_rip3_bitmap_destination *destination,
    const struct st_rip3_palette *palette, enum st_rip3_bitmap_colors colors,
    enum st_rip3_transfer_mode mode,
    const uint8_t *transparent_color, const struct st_rip3_rect *confined_clip)
{
	const int64_t limit = INT64_C(1024)*INT32_MAX;
	struct st_rip3_rect visible;
	uint8_t mapped[256];
	struct dither_color dithered[256];
	int status;

	if (ports == NULL || destination == NULL || mode < ST_RIP3_TRANSFER_COPY || mode > ST_RIP3_TRANSFER_INVERT
	    || colors < ST_RIP3_BITMAP_MATCH || colors > ST_RIP3_BITMAP_DITHER
	    || destination->x0 > destination->x1 || destination->y0 > destination->y1)
		return IO_ERROR_INVALID_ARGUMENT;
	if (destination->x0 < -limit || destination->x1 > limit || destination->y0 < -limit || destination->y1 > limit)
		return IO_ERROR_OUT_OF_RANGE;
	if (confined_clip == NULL) {
		struct st_rip3_port_info info;
		unsigned current;
		status = st_rip3_ports_current(ports,&current);
		if (status == 0) status = st_rip3_ports_info(ports,current,&info);
		if (status != 0 || !info.viewport_active) return status;
		visible = (struct st_rip3_rect){0,0,info.clip.x1,info.clip.y1};
	}
	else visible = *confined_clip;
	/* The shipped color-key mask creator rejects RGB555 before staging or
	 * drawing. Ordinary 16-bit loads still reach the common RGB painter. */
	if (transparent_color != NULL && bitmap != NULL && bitmap->bit_depth == 16) return IO_ERROR_NOT_SUPPORTED;
	if (visible.x0 > visible.x1 || visible.y0 > visible.y1 || destination->x0 > visible.x1
	    || destination->y0 > visible.y1 || destination->x1 < visible.x0 || destination->y1 < visible.y0)
		return 0;
	if (destination->x0 > visible.x0) visible.x0 = (int32_t)destination->x0;
	if (destination->y0 > visible.y0) visible.y0 = (int32_t)destination->y0;
	if (destination->x1 < visible.x1) visible.x1 = (int32_t)destination->x1;
	if (destination->y1 < visible.y1) visible.y1 = (int32_t)destination->y1;
	status = validate(bitmap,palette,colors == ST_RIP3_BITMAP_ADOPTED);
	if (status != 0) return status;
	uint32_t width = (uint32_t)(visible.x1-visible.x0)+1, height = (uint32_t)(visible.y1-visible.y0)+1;
	if (height > SIZE_MAX/sizeof(struct prepared_pixel)/width) return IO_ERROR_OUT_OF_RANGE;
	struct prepared_pixel *pixels = malloc((size_t)width*height*sizeof(*pixels));
	if (pixels == NULL) return IO_ERROR_OUT_OF_MEMORY;
	for (size_t i = 0; i < bitmap->palette_size; i++) {
		/* The internal selection mask sees nearest output-palette indexes,
		 * before dithering and even when the draw preserves adopted indexes. */
		(void)st_rip3_palette_color(palette,true,bitmap->palette[i],&mapped[i]);
		if (colors == ST_RIP3_BITMAP_DITHER) dithered[i] = dither_prepare(palette,bitmap->palette[i]);
	}
	uint64_t span_x = (uint64_t)(destination->x1-destination->x0)+1;
	uint64_t span_y = (uint64_t)(destination->y1-destination->y0)+1;
	for (uint32_t y = 0; y < height; y++) {
		uint32_t sy = sample((uint64_t)((int64_t)visible.y0+y-destination->y0),span_y,bitmap->pixels->height);
		for (uint32_t x = 0; x < width; x++) {
			uint32_t sx = sample((uint64_t)((int64_t)visible.x0+x-destination->x0),span_x,bitmap->pixels->width);
			uint32_t code = bitmap->pixels->primary[(size_t)sy*bitmap->pixels->width+sx];
			struct prepared_pixel *pixel = &pixels[(size_t)y*width+x];
			uint64_t phase_x = (uint64_t)((int64_t)visible.x0+x-destination->x0);
			uint64_t phase_y = (uint64_t)((int64_t)visible.y0+y-destination->y0);
			if (bitmap->bit_depth > 8) {
				if (colors == ST_RIP3_BITMAP_DITHER) {
					struct dither_color color = dither_prepare(palette,code & ~ST_PIXEL_COLOR_RGB_TAG);
					pixel->color = dither_pixel(&color,phase_x,phase_y);
				}
				else (void)st_rip3_palette_color(palette,true,code & ~ST_PIXEL_COLOR_RGB_TAG,&pixel->color);
				/* 24-bit compares exact source RGB; 32-bit's shipped
				 * classifier never selects ordinary byte-valued RGB keys. */
				pixel->opaque = transparent_color == NULL || bitmap->bit_depth == 32
				    || (code & ~ST_PIXEL_COLOR_RGB_TAG) != palette->rgb[*transparent_color];
			}
			else {
				pixel->color = colors == ST_RIP3_BITMAP_DITHER
				    ? dither_pixel(&dithered[code],phase_x,phase_y)
				    : colors == ST_RIP3_BITMAP_ADOPTED ? (uint8_t)code : mapped[code];
				pixel->opaque = transparent_color == NULL || (bitmap->bit_depth == 1
				    ? code != (*transparent_color != 0) : mapped[code] != *transparent_color);
			}
			if (mode == ST_RIP3_TRANSFER_INVERT) pixel->color = (uint8_t)~pixel->color;
		}
	}
	enum st_rip3_write_mode write_mode = mode == ST_RIP3_TRANSFER_INVERT ? ST_RIP3_COPY : (enum st_rip3_write_mode)mode;

	struct bitmap_paint paint = {ports, pixels, visible, write_mode};
	status = st_rip3_ports_draw(ports, write_mode, paint_bitmap, &paint);
	free(pixels);
	return status;
}

int
st_rip3_draw_bitmap(struct st_rip3_ports *ports,
    const struct st_rip3_bitmap *bitmap, const struct st_rip3_bitmap_destination *destination,
    const struct st_rip3_palette *palette, enum st_rip3_bitmap_colors colors,
    enum st_rip3_transfer_mode mode, const uint8_t *transparent_color)
{
	return draw_bitmap(ports,bitmap,destination,palette,colors,mode,transparent_color,NULL);
}

int
st_rip3_draw_bitmap_playback(struct st_rip3_ports *ports, const struct st_rip3_port_info *port,
    const struct st_rip3_bitmap *bitmap, const struct st_rip3_palette *palette)
{
	struct st_rip3_rect clip = port->clip;
	const struct st_rip3_rect view = port->viewport;
	const struct st_rip3_rect bounds = port->bounds;
	int64_t left = -(int64_t)view.x0, top = -(int64_t)view.y0;
	int64_t right = (int64_t)bounds.x1 - bounds.x0 - view.x0;
	int64_t bottom = (int64_t)bounds.y1 - bounds.y0 - view.y0;
	if (left > clip.x1 || top > clip.y1 || right < clip.x0 || bottom < clip.y0) return 0;
	if (left > clip.x0) clip.x0 = (int32_t)left;
	if (top > clip.y0) clip.y0 = (int32_t)top;
	if (right < clip.x1) clip.x1 = (int32_t)right;
	if (bottom < clip.y1) clip.y1 = (int32_t)bottom;
	struct st_rip3_bitmap_destination target = {0,0,(int64_t)view.x1-view.x0,(int64_t)view.y1-view.y0};
	if (!port->image.automatic) {
		const struct st_rip3_rect area = port->image.area;
		int64_t x = (int64_t)bounds.x0 + view.x0, y = (int64_t)bounds.y0 + view.y0;
		/* The rectangle is already in surface coordinates. Translate to
		 * the stamp kernel's viewport origin and consume its half-open edges. */
		target = (struct st_rip3_bitmap_destination){area.x0-x,area.y0-y,area.x1-x-1,area.y1-y-1};
	}
	if (target.x0 > target.x1 || target.y0 > target.y1) return 0;
	return draw_bitmap(ports,bitmap,&target,palette,ST_RIP3_BITMAP_DITHER,ST_RIP3_TRANSFER_COPY,NULL,&clip);
}
