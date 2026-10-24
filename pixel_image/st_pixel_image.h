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
/* CTerm APC's PNM framing: one raw-data separator byte, the original
 * four whitespace characters, and an 8-bit PPM maximum. PPM samples
 * still receive the corrected BT.709-to-sRGB conversion. */
int st_pixel_image_decode_cterm_ppm(struct st_pixel_buffer **result,
	const void *data, size_t length);
int st_pixel_image_decode_cterm_pbm(struct st_pixel_mask **result,
	const void *data, size_t length);

/* Build support, without loading libjxl or its CMS library. */
bool st_pixel_image_jxl_compiled(void);
/* Runtime availability, including all required decoder and CMS symbols. */
bool st_pixel_image_jxl_supported(void);
int st_pixel_image_decode_jxl(struct st_pixel_buffer **result,
	const void *data, size_t length);

#endif
