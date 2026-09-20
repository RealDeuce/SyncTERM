/* Decode external image data into SyncTERM pixel buffers and masks. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_PIXEL_IMAGE_H_
#define ST_PIXEL_IMAGE_H_

#include <stdbool.h>
#include <stddef.h>

struct st_pixel_buffer;
struct st_pixel_mask;

/* Results are unchanged on failure. */
int st_pixel_image_decode_ppm(struct st_pixel_buffer **result,
	const void *data, size_t length);
int st_pixel_image_decode_pbm(struct st_pixel_mask **result,
	const void *data, size_t length);

bool st_pixel_image_jxl_supported(void);
int st_pixel_image_decode_jxl(struct st_pixel_buffer **result,
	const void *data, size_t length);

#endif
