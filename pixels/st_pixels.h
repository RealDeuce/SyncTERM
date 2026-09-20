/* Owned native-colour buffers, RGB images, masks, and pixel scaling. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_PIXELS_H_
#define ST_PIXELS_H_

#include <stdbool.h>
#include <stdint.h>

struct st_pixel_buffer {
	/* Values are palette indexes or ST_PIXEL_COLOR_RGB-tagged RGB. */
	uint32_t *primary;
	uint32_t *alternate;
	uint32_t width;
	uint32_t height;
};

struct st_rgb_buffer {
	/* Pixels are packed 0x00RRGGBB with no tag or palette indexes. */
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
};

#define ST_PIXEL_COLOR_RGB_TAG UINT32_C(0x80000000)
#define ST_PIXEL_COLOR_RGB_VALUE(red, green, blue) \
	(ST_PIXEL_COLOR_RGB_TAG | (((uint32_t)(red) & UINT32_C(0xff)) << 16) \
	    | (((uint32_t)(green) & UINT32_C(0xff)) << 8) \
	    | ((uint32_t)(blue) & UINT32_C(0xff)))
#define ST_PIXEL_COLOR_IS_RGB(colour) \
	(((colour) & ST_PIXEL_COLOR_RGB_TAG) != 0)

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
	int32_t destination_x;
	int32_t destination_y;
	uint32_t scale_x;
	uint32_t scale_y;
	uint32_t mask_x;
	uint32_t mask_y;
	uint32_t flags;
};

/* Results are unchanged on failure. Buffers are initialized to zero. */
int st_pixel_buffer_create(struct st_pixel_buffer **result, uint32_t width,
	uint32_t height, bool alternate);
int st_pixel_buffer_clone(struct st_pixel_buffer **result,
	const struct st_pixel_buffer *source);
int st_pixel_buffer_ensure_alternate(struct st_pixel_buffer *buffer);
void st_pixel_buffer_remove_alternate(struct st_pixel_buffer *buffer);
void st_pixel_buffer_destroy(struct st_pixel_buffer *buffer);
int st_rgb_buffer_create(struct st_rgb_buffer **result, uint32_t width,
	uint32_t height);
void st_rgb_buffer_destroy(struct st_rgb_buffer *buffer);

/* Mask rows are contiguous and bits are MSB-first. */
int st_pixel_mask_create(struct st_pixel_mask **result, uint32_t width,
	uint32_t height);
int st_pixel_mask_get(const struct st_pixel_mask *mask, uint32_t x,
	uint32_t y, bool *value);
int st_pixel_mask_set(struct st_pixel_mask *mask, uint32_t x, uint32_t y,
	bool value);
void st_pixel_mask_destroy(struct st_pixel_mask *mask);

int st_pixel_buffer_fill(struct st_pixel_buffer *buffer, uint32_t primary,
	uint32_t alternate);
/* The returned plane is borrowed until the next scale call or destruction. */
int st_pixel_scaler_create(st_pixel_scaler_t **result);
void st_pixel_scaler_destroy(st_pixel_scaler_t *scaler);
/* Target dimensions must be no smaller than the source dimensions. */
int st_pixel_scaler_scale(st_pixel_scaler_t *scaler,
	const struct st_rgb_buffer **result, const struct st_rgb_buffer *source,
	uint32_t width, uint32_t height,
	enum st_pixel_scaling_policy policy);
/* Updates a previously scaled result after the indicated source damage. */
int st_pixel_scaler_scale_region(st_pixel_scaler_t *scaler,
	const struct st_rgb_buffer **result, const struct st_rgb_buffer *source,
	const struct st_pixel_rect *damage, struct st_pixel_rect *output_damage);
/*
 * Blits clip against the destination. The mask, when present, addresses the
 * same unscaled source pixels and is flipped with them. If source has an
 * alternate plane, destination acquires one before any pixels are changed.
 */
int st_pixel_buffer_blit(struct st_pixel_buffer *destination,
	const struct st_pixel_buffer *source, const struct st_pixel_mask *mask,
	const struct st_pixel_blit *blit);

#endif
