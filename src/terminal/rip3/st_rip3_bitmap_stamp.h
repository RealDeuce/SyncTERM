/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_BITMAP_STAMP_H_
#define ST_RIP3_BITMAP_STAMP_H_

#include "st_rip3_bitmap.h"
#include "st_rip3_surface.h"
#include <stdint.h>

struct st_rip3_ports;
struct st_rip3_port_info;

/* Inclusive device rectangle relative to the current viewport. Wide endpoints
 * retain protocol projection until clipping/sampling: each must fit within
 * +/-1024*INT32_MAX. This is placement geometry, not a wire-field structure. */
struct st_rip3_bitmap_destination {
	int64_t x0, y0, x1, y1;
};

enum st_rip3_bitmap_colors {
	ST_RIP3_BITMAP_MATCH,
	ST_RIP3_BITMAP_ADOPTED,
	ST_RIP3_BITMAP_DITHER,
};

/* Translate a wire key through the current DRAWING palette before image-palette
 * adoption. Supported initialized direct-index limits are 2, 16 and 236. Below
 * the limit the index is unchanged; otherwise the first exact RGB match wins,
 * or the last equal-distance minimum. Failure retains result. This does not
 * select a palette or infer a device profile from ENV RGB input precision. */
int st_rip3_bitmap_translate_key(const struct st_rip3_palette *palette,
	unsigned direct_index_limit, uint32_t color, uint8_t *result);

/* Stamp a decoded BMP without image-style fitting or clearing.
 * Nearest source pixel centers are computed against the original requested
 * destination, then clipped to the viewport. Each covered pixel is applied
 * once; mode 4 inverts the mapped SOURCE, independently of destination NOT.
 * transparent_color is an already translated output-palette key, not a file
 * index. A 1-bit source compares raw bits to zero/nonzero key; 4/8-bit sources
 * compare nearest output-palette indexes before dithering/transfer inversion.
 * 24-bit sources compare exact source RGB to the output palette's key entry.
 * The shipped 16-bit keyed path rejects before drawing; 32-bit keys never
 * select transparent pixels. NULL transparent_color means opaque. Both palettes
 * remain unchanged. ADOPTED requires an indexed source whose palette exactly
 * matches the corresponding prefix of the supplied palette; it preserves raw
 * draw indexes, including equal-RGB entries; key classification still uses
 * the separate nearest-index map. MATCH selects nearest RGB.
 * DITHER uses ordered 8x8 two-colour mixing, anchored to the original requested
 * rectangle before clipping. Exact palette matches remain exact. The algorithm
 * is client policy, not historical RIPtel pixel reproduction. This kernel
 * does not commit palettes or duplicate ports. The command layer performs
 * duplication after the initial paint using the shared clipboard service.
 *
 * Disabled image style does not disable stamping. Inactive viewports succeed
 * before bitmap access. A far edge one below the near edge denotes an empty axis.
 * Empty coverage succeeds after the keyed-16-bit rejection
 * check. Complete validation and scratch allocation precede drawing; protection allows
 * contents to change. One session thread owns source, ports and invocation.
 * Status belongs to the owning parser, never a terminal/session error path. */
int st_rip3_draw_bitmap(struct st_rip3_ports *ports,
	const struct st_rip3_bitmap *bitmap,
	const struct st_rip3_bitmap_destination *destination,
	const struct st_rip3_palette *palette, enum st_rip3_bitmap_colors colors,
	enum st_rip3_transfer_mode mode,
	const uint8_t *transparent_color);

/* Present an HCL bitmap into the captured active current port. The caller
 * checks the playback gates before loading the file. Uses the stored surface
 * rectangle or the viewport fallback, clipping to the port as well as the
 * viewport and backing surface. COPY and dither are independent of style flags. */
int st_rip3_draw_bitmap_playback(struct st_rip3_ports *ports, const struct st_rip3_port_info *port,
	const struct st_rip3_bitmap *bitmap, const struct st_rip3_palette *palette);

#endif
