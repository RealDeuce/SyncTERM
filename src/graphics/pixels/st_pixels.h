/* Owned native-colour buffers, RGB images, masks, and pixel scaling. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_PIXELS_H_
#define ST_PIXELS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_pixel_buffer {
	/* Values are palette indexes or ST_PIXEL_COLOR_RGB-tagged RGB. */
	uint32_t *primary;
	uint32_t *alternate;
	uint32_t  width;
	uint32_t  height;
	/* Optional packed rate mask, MSB first, contiguous across rows. NULL means slow. */
	uint8_t  *fast_blink;
};

/* Packed masks use physical pixel offsets, including for borrowed raster views. */
static inline bool
st_pixel_bit(const uint8_t *bits, size_t at)
{
	return bits && (bits[at / 8] & (0x80U >> (at % 8))) != 0;
}

static inline void
st_pixel_set_bit(uint8_t *bits, size_t at, bool value)
{
	uint8_t mask = (uint8_t)(0x80U >> (at % 8));
	if (value)
		bits[at / 8] |= mask;
	else
		bits[at / 8] &= (uint8_t)~mask;
}

/* A binary raster operation follows its changing operand. When both
 * operands blink, a conflict selects the slow clock. */
static inline bool
st_pixel_combined_fast(bool source_blinks, bool source_fast, bool dest_blinks, bool dest_fast)
{
	return (source_blinks || dest_blinks) && (!source_blinks || source_fast) && (!dest_blinks || dest_fast);
}

static inline void
st_pixel_buffer_set_solid(const struct st_pixel_buffer *pixels, size_t at, uint32_t colour)
{
	pixels->primary[at] = colour;
	if (pixels->alternate)
		pixels->alternate[at] = colour;
	if (pixels->fast_blink)
		st_pixel_set_bit(pixels->fast_blink, at, false);
}

void st_pixel_fill_bits(uint8_t *bits, size_t at, size_t count, bool value);
/* A NULL source supplies zero bits. Returns whether the destination changed. */
bool st_pixel_copy_bits(uint8_t *dst, size_t to, const uint8_t *src, size_t from, size_t count);

struct st_rgb_buffer {
	/* Pixels are packed 0x00RRGGBB with no tag or palette indexes. */
	uint32_t *pixels;
	uint32_t  width;
	uint32_t  height;
};

#define ST_PIXEL_COLOR_RGB_TAG UINT32_C(0x80000000)
#define ST_PIXEL_COLOR_RGB_VALUE(red, green, blue)                           \
	(ST_PIXEL_COLOR_RGB_TAG | (((uint32_t)(red) & UINT32_C(0xff)) << 16) \
	    | (((uint32_t)(green) & UINT32_C(0xff)) << 8) | ((uint32_t)(blue) & UINT32_C(0xff)))
#define ST_PIXEL_COLOR_IS_RGB(colour) (((colour) & ST_PIXEL_COLOR_RGB_TAG) != 0)

struct st_pixel_mask {
	uint8_t *bits;
	uint32_t width;
	uint32_t height;
};

typedef struct st_pixel_scaler st_pixel_scaler_t;

enum st_pixel_scaling_policy {
	ST_PIXEL_SCALING_BLOCKY,
	ST_PIXEL_SCALING_POINTY,
};

/* Integer passes applied before Pointy's final fractional interpolation.
 * A uint32_t scale can contain at most 31 factors of two. */
struct st_pointy_plan {
	uint32_t repeat_x, repeat_y;
	uint32_t factors[32];
	size_t   count;
};
struct st_pointy_plan st_pixel_pointy_plan(uint32_t x_multiplier, uint32_t y_multiplier);

struct st_pixel_rect {
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
};

#define ST_PIXEL_BLIT_FLIP_X (UINT32_C(1) << 0)
#define ST_PIXEL_BLIT_FLIP_Y (UINT32_C(1) << 1)

struct st_pixel_blit {
	uint32_t source_x;
	uint32_t source_y;
	uint32_t width;
	uint32_t height;
	int32_t  destination_x;
	int32_t  destination_y;
	uint32_t scale_x;
	uint32_t scale_y;
	uint32_t mask_x;
	uint32_t mask_y;
	uint32_t flags;
};

/* Results are unchanged on failure. Buffers are initialized to zero. */
int  st_pixel_buffer_create(struct st_pixel_buffer **result, uint32_t width, uint32_t height, bool alternate);
int  st_pixel_buffer_clone(struct st_pixel_buffer **result, const struct st_pixel_buffer *source);
int  st_pixel_buffer_ensure_alternate(struct st_pixel_buffer *buffer);
/* Copy between allocated planes, including overlapping spans. When the source
 * has a rate selector the caller must first ensure one on the destination. */
void st_pixel_buffer_copy_span(const struct st_pixel_buffer *destination, size_t to,
    const struct st_pixel_buffer *source, size_t from, size_t count);
int  st_pixel_buffer_ensure_fast_blink(struct st_pixel_buffer *buffer);
void st_pixel_buffer_remove_alternate(struct st_pixel_buffer *buffer);
void st_pixel_buffer_destroy(struct st_pixel_buffer *buffer);
int  st_rgb_buffer_create(struct st_rgb_buffer **result, uint32_t width, uint32_t height);
void st_rgb_buffer_destroy(struct st_rgb_buffer *buffer);

/* Mask rows are contiguous and bits are MSB-first. */
int  st_pixel_mask_create(struct st_pixel_mask **result, uint32_t width, uint32_t height);
int  st_pixel_mask_get(const struct st_pixel_mask *mask, uint32_t x, uint32_t y, bool *value);
int  st_pixel_mask_set(struct st_pixel_mask *mask, uint32_t x, uint32_t y, bool value);
void st_pixel_mask_destroy(struct st_pixel_mask *mask);

int st_pixel_buffer_fill(struct st_pixel_buffer *buffer, uint32_t primary, uint32_t alternate);
/* The returned plane is borrowed until the next scale call or destruction. */
int  st_pixel_scaler_create(st_pixel_scaler_t **result);
void st_pixel_scaler_destroy(st_pixel_scaler_t *scaler);
/* Target dimensions must be nonzero; each axis may enlarge or reduce. */
int st_pixel_scaler_scale(st_pixel_scaler_t *scaler, const struct st_rgb_buffer **result,
    const struct st_rgb_buffer *source, uint32_t width, uint32_t height, enum st_pixel_scaling_policy policy);
/* Updates a previously scaled result after the indicated source damage. */
int st_pixel_scaler_scale_region(st_pixel_scaler_t *scaler, const struct st_rgb_buffer **result,
    const struct st_rgb_buffer *source, const struct st_pixel_rect *damage, struct st_pixel_rect *output_damage);
/*
 * Blits clip against the destination. The mask, when present, addresses the
 * same unscaled source pixels and is flipped with them. If source has an
 * alternate plane, destination acquires one before any pixels are changed.
 * A missing source alternate copies primary into both destination planes.
 * A supplied alternate is copied verbatim, regardless of which source plane
 * was modified; buffers have no per-plane modification tracking.
 */
int st_pixel_buffer_blit(struct st_pixel_buffer *destination, const struct st_pixel_buffer *source,
    const struct st_pixel_mask *mask, const struct st_pixel_blit *blit);
/* Validate before a positioned destination maps the same blit into its shared
 * raster. Successful right/bottom outputs are exclusive destination edges. */
int st_pixel_buffer_validate_blit(const struct st_pixel_buffer *destination, const struct st_pixel_buffer *source,
    const struct st_pixel_mask *mask, const struct st_pixel_blit *blit, int64_t *right, int64_t *bottom);

#endif
