/* Windows BMP containers used by RIP3 icons, hot images and masks. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_BITMAP_H
#define ST_RIP3_BITMAP_H

#include <stddef.h>
#include <stdint.h>

struct st_pixel_buffer;

struct st_rip3_bitmap {
	/* Owned pixels: local palette indexes for 1/4/8-bit images, tagged RGB
	 * for 24-bit images. The file palette is retained independently of the
	 * active RIP drawing palette and transparent-index/mask policy. */
	struct st_pixel_buffer *pixels;
	uint32_t palette[256]; /* untagged 0x00RRGGBB */
	size_t palette_size;
	unsigned bit_depth;
};

/* Full Windows 3.x BMP file header + 40-byte info header; uncompressed
 * bottom-up 1/4/8/24-bit data only. Results remain unchanged on failure.
 * BMM transparency/compositing is a separate, unresolved wire requirement. */
int st_rip3_bitmap_decode(struct st_rip3_bitmap **result,
	const void *data, size_t size);
/* Independent legacy ICN/HIC/MSK memory decoding for the v3 converter. Four
 * top-down planes per row, plane 3 first, MSB-left; WORD dimensions store
 * width/height minus one. Ignore every byte after the required pixel data.
 * The documented BGI buffer bound includes its six-byte overhead.
 *
 * Icons have no palette: the caller must provide sixteen untagged RGB values,
 * which are copied without changing the decoded indexes. Implicit conversion
 * palette choice and filesystem deletion/timestamp policy remain pending #19.
 * This routine does not call/change the frozen 1.54 decoder, infer BMM mask
 * compositing, access files or perform UI/error-popup actions. Status belongs
 * to the owning asset loader/parser; it is not a terminal session error.
 * Results remain unchanged on failure. */
int st_rip3_bitmap_decode_legacy(struct st_rip3_bitmap **result,
	const void *data, size_t size, const uint32_t palette[16]);
/* Encode the primary plane with its local palette, retaining bit depth.
 * Returns malloc-owned bytes; both outputs remain unchanged on failure. */
int st_rip3_bitmap_encode(uint8_t **result, size_t *size,
	const struct st_rip3_bitmap *bitmap);
void st_rip3_bitmap_destroy(struct st_rip3_bitmap *bitmap);

#endif
