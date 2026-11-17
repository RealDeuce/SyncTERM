/* Windows BMP containers used by RIP3 icons, hot images and masks. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_BITMAP_H
#define ST_RIP3_BITMAP_H

#include <stddef.h>
#include <stdint.h>

struct st_pixel_buffer;
struct st_rip3_ports;
struct st_rip3_rect;
struct st_rip3_palette;

struct st_rip3_bitmap {
	/* Owned pixels: local palette indexes for 1/4/8-bit images, tagged RGB
	 * for 16/24/32-bit images. The file palette is retained independently of the
	 * active RIP drawing palette and transparent-index/mask policy. */
	struct st_pixel_buffer *pixels;
	uint32_t palette[256]; /* untagged 0x00RRGGBB */
	size_t palette_size;
	unsigned bit_depth;
};

/* Full Windows 3.x BMP file header + 40-byte info header; uncompressed
 * bottom-up 1/4/8/16/24/32-bit data only. RGB555 uses floor(v*255/31)
 * component expansion; bit15 and 32-bit high bytes carry no alpha. Results
 * remain unchanged on failure. BMM uses this ordinary decoder; depth-specific
 * color-key selection and explicit put modes belong to the command/renderer. */
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
 * RGB555 components quantize to the nearest 5-bit value; unused high bits/
 * bytes and row padding are zero. Returns malloc-owned bytes; both outputs
 * remain unchanged on failure. */
int st_rip3_bitmap_encode(uint8_t **result, size_t *size,
	const struct st_rip3_bitmap *bitmap);
/* Capture a named port's active viewport as an owned 8-bit bitmap with the
 * explicit resolved 256-entry palette. Device area is viewport-relative and
 * clipped, or NULL for the full viewport; no world/sentinel policy is inferred.
 * Undefined/empty returns NOT_FOUND, inactive returns NOT_SUPPORTED, and every
 * failure preserves result. Protection/current selection/configuration/pixels
 * remain unchanged. One session thread owns the capture and source state. */
int st_rip3_bitmap_capture(struct st_rip3_bitmap **result,
	const struct st_rip3_ports *ports, unsigned number,
	const struct st_rip3_rect *area, const struct st_rip3_palette *palette);
/* Raw port-relative DEVICE capture, clipped to the backing bounds, or NULL
 * for the whole port. Rectangles have inclusive endpoints as in the device
 * kernel. Stored viewport bounds/activation do not restrict this read.
 * Ownership, palette and unchanged-state guarantees match viewport capture.
 * Command-specific half-open bounds and source-kind policy stay in the caller. */
int st_rip3_bitmap_capture_port(struct st_rip3_bitmap **result,
	const struct st_rip3_ports *ports, unsigned number,
	const struct st_rip3_rect *area, const struct st_rip3_palette *palette);
void st_rip3_bitmap_destroy(struct st_rip3_bitmap *bitmap);

#endif
