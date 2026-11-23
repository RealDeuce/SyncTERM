/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_surface.h"

#include "io_error.h"
#include "st_pixels.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct st_rip3_surface {
	/* Index values, plus palette-independent tagged RGB from reset/native ink.
	 * COPY/clone retain that value; indexed logical writes replace it. */
	uint32_t *pixels;
	uint8_t *damage;
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
	    || (uint64_t)width * height > SIZE_MAX / sizeof(*surface->pixels))
		return IO_ERROR_OUT_OF_RANGE;
	surface = calloc(1, sizeof(*surface));
	if (surface == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	surface->pixels = calloc((size_t)width * height, sizeof(*surface->pixels));
	if (surface->pixels == NULL) {
		free(surface);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	surface->width = width;
	surface->height = height;
	*result = surface;
	return 0;
}

int
st_rip3_surface_clone(struct st_rip3_surface **result,
    const struct st_rip3_surface *surface)
{
	struct st_rip3_surface *copy = NULL;
	int status;

	if (result == NULL || surface == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rip3_surface_create(&copy, surface->width, surface->height);
	if (status != 0)
		return status;
	memcpy(copy->pixels, surface->pixels, (size_t)surface->width * surface->height * sizeof(*surface->pixels));
	*result = copy;
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
	*result = (uint8_t)surface->pixels[(size_t)y * surface->width + (uint32_t)x];
	return 0;
}

static uint32_t
write_pixel(uint32_t destination, uint32_t source, enum st_rip3_write_mode mode)
{
	switch (mode) {
		case ST_RIP3_XOR: return (uint8_t)(destination ^ source);
		case ST_RIP3_OR: return (uint8_t)(destination | source);
		case ST_RIP3_AND: return (uint8_t)(destination & source);
		case ST_RIP3_NOT: return (uint8_t)~destination;
		default: return source;
	}
}

int
st_rip3_surface_damage(struct st_rip3_surface *surface, uint8_t *damage, size_t length)
{
	if (surface==NULL || (damage!=NULL && length<(size_t)surface->width*surface->height))
		return IO_ERROR_INVALID_ARGUMENT;
	surface->damage=damage;
	return 0;
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
st_rip3_surface_clear_black(struct st_rip3_surface *surface, const struct st_rip3_rect *area)
{
	if (surface==NULL || !rect_valid(area)) return IO_ERROR_INVALID_ARGUMENT;
	const struct st_rip3_rect bounds={0,0,(int32_t)surface->width-1,(int32_t)surface->height-1};
	struct st_rip3_rect visible=area==NULL ? bounds : *area;
	if (!intersect(&visible,&bounds)) return 0;
	size_t width=(size_t)(visible.x1-visible.x0)+1;
	for (int32_t y=visible.y0;y<=visible.y1;y++) {
		size_t offset=(size_t)y*surface->width+(uint32_t)visible.x0;
		uint32_t *row=surface->pixels+offset;
		for (size_t x=0;x<width;x++) row[x]=ST_PIXEL_COLOR_RGB_TAG;
		if (surface->damage!=NULL) memset(surface->damage+offset,1,width);
	}
	return 0;
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
			if (surface->damage!=NULL) surface->damage[offset]=1;
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

int
st_rip3_surface_invert_rgb(struct st_rip3_surface *surface,
    const struct st_rip3_rect *clip,const struct st_rip3_rect *area,const struct st_rip3_palette *palette)
{
	if (surface==NULL || area==NULL || palette==NULL || !rect_valid(area) || !rect_valid(clip)) return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i=0;i<256;i++) if (palette->rgb[i]>0xffffff) return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_rect visible=*area,bounds={0,0,(int32_t)surface->width-1,(int32_t)surface->height-1};
	if (!intersect(&visible,&bounds) || (clip!=NULL && !intersect(&visible,clip))) return 0;
	uint8_t inverse[256];
	for (size_t i=0;i<256;i++) (void)st_rip3_palette_color(palette,true,palette->rgb[i]^0xffffff,&inverse[i]);
	for (int32_t y=visible.y0;y<=visible.y1;y++) for (int32_t x=visible.x0;x<=visible.x1;x++) {
		size_t at=(size_t)y*surface->width+(uint32_t)x;uint32_t pixel=surface->pixels[at];
		uint8_t color;
		if (ST_PIXEL_COLOR_IS_RGB(pixel)) (void)st_rip3_palette_color(palette,true,(pixel&0xffffff)^0xffffff,&color);
		else color=inverse[pixel];
		surface->pixels[at]=color;
		if (surface->damage!=NULL) surface->damage[at]=1;
	}
	return 0;
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
	uint32_t *saved = NULL;

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
		saved = malloc(sw * sh * sizeof(*saved));
		if (saved == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		for (size_t y = 0; y < sh; y++)
			memcpy(saved + y * sw,
			    source->pixels + ((size_t)src.y0 + y) * source->width
			    + (uint32_t)src.x0, sw * sizeof(*saved));
	}
	for (size_t y = 0; y < dh; y++) {
		size_t sy = (size_t)(((uint64_t)y * 2 + 1) * sh / (2 * (uint64_t)dh));

		for (size_t x = 0; x < dw; x++) {
			size_t sx = (size_t)(((uint64_t)x * 2 + 1) * sw / (2 * (uint64_t)dw));
			size_t offset = ((size_t)dst.y0 + y) * destination->width
			    + (uint32_t)dst.x0 + x;
			uint32_t pixel = saved != NULL ? saved[sy * sw + sx]
			    : source->pixels[((size_t)src.y0 + sy) * source->width
			    + (uint32_t)src.x0 + sx];

			if (mode == ST_RIP3_TRANSFER_INVERT)
				destination->pixels[offset] = (uint8_t)~pixel;
			else
				destination->pixels[offset] = write_pixel(
				    destination->pixels[offset], pixel,
				    (enum st_rip3_write_mode)mode);
			if (destination->damage!=NULL) destination->damage[offset]=1;
		}
	}
	free(saved);
	return 0;
}

int
st_rip3_surface_copy_projected(struct st_rip3_surface *destination,
    const struct st_rip3_rect *destination_area,
    const struct st_rip3_surface *source,
    const struct st_rip3_rect *source_area, enum st_rip3_transfer_mode mode)
{
	struct st_rip3_rect src, dst;
	uint32_t *saved = NULL;
	if (source == NULL || destination == NULL || source_area == NULL || destination_area == NULL
	    || !rect_valid(source_area) || !rect_valid(destination_area)
	    || source_area->x0 < 0 || source_area->y0 < 0
	    || destination_area->x0 < 0 || destination_area->y0 < 0
	    || mode < ST_RIP3_TRANSFER_COPY || mode > ST_RIP3_TRANSFER_INVERT)
		return IO_ERROR_INVALID_ARGUMENT;
	src = *source_area; dst = *destination_area;
	if (!surface_clip(source,&src,NULL) || !surface_clip(destination,&dst,NULL)) return 0;
	uint64_t sw = (uint64_t)source_area->x1-source_area->x0+1;
	uint64_t sh = (uint64_t)source_area->y1-source_area->y0+1;
	uint64_t dw = (uint64_t)destination_area->x1-destination_area->x0+1;
	uint64_t dh = (uint64_t)destination_area->y1-destination_area->y0+1;
	size_t saved_width = (size_t)(src.x1-src.x0)+1;
	if (source == destination) {
		size_t saved_height = (size_t)(src.y1-src.y0)+1;
		saved = malloc(saved_width*saved_height*sizeof(*saved));
		if (saved == NULL) return IO_ERROR_OUT_OF_MEMORY;
		for (size_t y = 0; y < saved_height; y++)
			memcpy(saved+y*saved_width,source->pixels+((size_t)src.y0+y)*source->width+(uint32_t)src.x0,saved_width*sizeof(*saved));
	}
	/* Nonnegative int32 endpoints bound every span to 2^31, so the doubled
	 * center-sampling products fit uint64_t. Clip output without refitting.
	 * Missing physical source pixels do not acquire invented padding values. */
	for (int32_t y = dst.y0; y <= dst.y1; y++) {
		uint64_t sy = (uint64_t)source_area->y0+((2*(uint64_t)(y-destination_area->y0)+1)*sh/(2*dh));
		if (sy < (uint32_t)src.y0 || sy > (uint32_t)src.y1) continue;
		for (int32_t x = dst.x0; x <= dst.x1; x++) {
			uint64_t sx = (uint64_t)source_area->x0+((2*(uint64_t)(x-destination_area->x0)+1)*sw/(2*dw));
			if (sx < (uint32_t)src.x0 || sx > (uint32_t)src.x1) continue;
			uint32_t pixel = saved != NULL ? saved[(size_t)(sy-src.y0)*saved_width+(size_t)(sx-src.x0)]
			    : source->pixels[(size_t)sy*source->width+(size_t)sx];
			size_t offset = (size_t)y*destination->width+(uint32_t)x;
			destination->pixels[offset] = mode == ST_RIP3_TRANSFER_INVERT ? (uint8_t)~pixel
			    : write_pixel(destination->pixels[offset],pixel,(enum st_rip3_write_mode)mode);
			if (destination->damage!=NULL) destination->damage[offset]=1;
		}
	}
	free(saved);
	return 0;
}

int
st_rip3_surface_blit(struct st_rip3_surface *destination,
    const struct st_rip3_rect *destination_clip,
    int32_t destination_x, int32_t destination_y,
    const struct st_rip3_surface *source,
    const struct st_rip3_rect *source_clip,
    const struct st_rip3_rect *source_area,
    enum st_rip3_transfer_mode mode)
{
	struct st_rip3_rect src;
	struct st_rip3_rect dst;
	int64_t dx, dy, x0, y0, x1, y1;

	if (destination == NULL || source == NULL || source_area == NULL
	    || !rect_valid(source_area) || !rect_valid(source_clip)
	    || !rect_valid(destination_clip)
	    || mode < ST_RIP3_TRANSFER_COPY || mode > ST_RIP3_TRANSFER_INVERT)
		return IO_ERROR_INVALID_ARGUMENT;
	src = *source_area;
	dst = (struct st_rip3_rect){0, 0, (int32_t)destination->width - 1,
	    (int32_t)destination->height - 1};
	if (!surface_clip(source, &src, source_clip)
	    || !surface_clip(destination, &dst, destination_clip))
		return 0;
	/* Preserve the ORIGINAL origin correspondence, even after source clipping.
	 * The difference of two int32_t coordinates and all inverse clip bounds
	 * fit int64_t; no requested width/height needs to be allocated or narrowed. */
	dx = (int64_t)destination_x - source_area->x0;
	dy = (int64_t)destination_y - source_area->y0;
	x0 = src.x0;
	y0 = src.y0;
	x1 = src.x1;
	y1 = src.y1;
	if (x0 < dst.x0 - dx) x0 = dst.x0 - dx;
	if (y0 < dst.y0 - dy) y0 = dst.y0 - dy;
	if (x1 > dst.x1 - dx) x1 = dst.x1 - dx;
	if (y1 > dst.y1 - dy) y1 = dst.y1 - dy;
	if (x0 > x1 || y0 > y1)
		return 0;
	src = (struct st_rip3_rect){(int32_t)x0, (int32_t)y0,
	    (int32_t)x1, (int32_t)y1};
	dst = (struct st_rip3_rect){(int32_t)(x0 + dx), (int32_t)(y0 + dy),
	    (int32_t)(x1 + dx), (int32_t)(y1 + dy)};
	/* Both rectangles are now visible and have identical dimensions: the fit
	 * service performs an identity mapping with its existing owned snapshot. */
	return st_rip3_surface_copy(destination, NULL, &dst, source, NULL,
	    &src, mode);
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
		snapshot->primary[i] = ST_PIXEL_COLOR_IS_RGB(surface->pixels[i]) ? surface->pixels[i]
		    : ST_PIXEL_COLOR_RGB_TAG | palette->rgb[surface->pixels[i]];
	*result = snapshot;
	return 0;
}


int
st_rip3_surface_capture_raw(const struct st_rip3_surface *surface,struct st_pixel_buffer **result)
{
	if (surface==NULL || result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_pixel_buffer *copy=NULL;
	int status=st_pixel_buffer_create(&copy,surface->width,surface->height,false);
	if (status!=0) return status;
	memcpy(copy->primary,surface->pixels,(size_t)surface->width*surface->height*sizeof(*surface->pixels));
	*result=copy;return 0;
}
int
st_rip3_surface_restore_raw(struct st_rip3_surface *surface,const struct st_pixel_buffer *source)
{
	if (surface==NULL || source==NULL || source->primary==NULL || source->width==0 || source->height==0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (source->height>SIZE_MAX/sizeof(*source->primary)/source->width) return IO_ERROR_OUT_OF_RANGE;
	uint32_t width=source->width<surface->width ? source->width : surface->width;
	uint32_t height=source->height<surface->height ? source->height : surface->height;
	for (uint32_t y=0;y<height;y++) for (uint32_t x=0;x<width;x++) {
		uint32_t value=source->primary[(size_t)y*source->width+x];
		if (!ST_PIXEL_COLOR_IS_RGB(value) && value>255) return IO_ERROR_OUT_OF_RANGE;
	}
	for (uint32_t y=0;y<height;y++) {
		memcpy(surface->pixels+(size_t)y*surface->width,source->primary+(size_t)y*source->width,(size_t)width*sizeof(*source->primary));
		if (surface->damage!=NULL) memset(surface->damage+(size_t)y*surface->width,1,width);
	}
	return 0;
}

int
st_rip3_surface_capture_area_raw(const struct st_rip3_surface *surface,
    const struct st_rip3_rect *area,struct st_pixel_buffer **result)
{
	if (surface==NULL || area==NULL || !rect_valid(area) || result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_rect visible=*area;
	if (!surface_clip(surface,&visible,NULL)) {*result=NULL;return 0;}
	struct st_pixel_buffer *copy=NULL;
	int status=st_pixel_buffer_create(&copy,(uint32_t)(visible.x1-visible.x0)+1,(uint32_t)(visible.y1-visible.y0)+1,false);
	if (status!=0) return status;
	for (uint32_t y=0;y<copy->height;y++)
		memcpy(copy->primary+(size_t)y*copy->width,
		    surface->pixels+((size_t)visible.y0+y)*surface->width+(uint32_t)visible.x0,
		    (size_t)copy->width*sizeof(*copy->primary));
	*result=copy;return 0;
}

int
st_rip3_surface_copy_from_raw(struct st_rip3_surface *destination,const struct st_rip3_rect *to,
    const struct st_pixel_buffer *source,const struct st_rip3_rect *from,enum st_rip3_transfer_mode mode)
{
	if (source==NULL || source->primary==NULL || source->width==0 || source->height==0
	    || source->width>INT32_MAX || source->height>INT32_MAX) return IO_ERROR_INVALID_ARGUMENT;
	if (source->height>SIZE_MAX/sizeof(*source->primary)/source->width) return IO_ERROR_OUT_OF_RANGE;
	for (size_t i=0;i<(size_t)source->width*source->height;i++)
		if (!ST_PIXEL_COLOR_IS_RGB(source->primary[i]) && source->primary[i]>255) return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_surface borrowed={.pixels=source->primary,.width=source->width,.height=source->height};
	return st_rip3_surface_copy_projected(destination,to,&borrowed,from,mode);
}

int
st_rip3_surface_read_color(const struct st_rip3_surface *surface,int32_t x,int32_t y,uint32_t *result)
{
	if (surface==NULL || result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (x<0 || y<0 || (uint32_t)x>=surface->width || (uint32_t)y>=surface->height) return IO_ERROR_OUT_OF_RANGE;
	*result=surface->pixels[(size_t)y*surface->width+(uint32_t)x];return 0;
}
