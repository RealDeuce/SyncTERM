/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_BITMAP_STAMP_H_
#define ST_RIP3_BITMAP_STAMP_H_

#include "st_rip3_bitmap.h"
#include "st_rip3_surface.h"
#include <stdbool.h>
#include <stdint.h>

struct st_rip3_ports;

/* Inclusive device rectangle relative to the current viewport. Wide endpoints
 * retain protocol projection until clipping/sampling: each must fit within
 * +/-1024*INT32_MAX. This is placement geometry, not a wire-field structure. */
struct st_rip3_bitmap_destination {
	int64_t x0, y0, x1, y1;
};

/* Stamp a decoded BMP without image-style fitting, clearing or state edits.
 * Nearest source pixel centers are computed against the original requested
 * destination, then clipped to the viewport. Each covered pixel is applied
 * once; mode 4 inverts the mapped SOURCE, independently of destination NOT.
 * Indexed transparency compares original source indexes; RGB transparency
 * compares the resolved destination-palette index, before transfer inversion.
 * NULL transparent_color means opaque. Source/destination palettes remain
 * unchanged. palette_adopted requires an indexed source whose palette exactly
 * matches the corresponding prefix of the supplied palette; it preserves raw
 * source indexes, including equal-RGB entries. Otherwise nearest-color mapping
 * applies. This kernel does not commit palettes, dither or duplicate ports.
 *
 * Disabled image style does not disable stamping. Inactive/empty viewport
 * coverage succeeds without dereferencing bitmap/palette. Otherwise complete
 * validation and all scratch allocation precede drawing; protection allows
 * contents to change. One session thread owns source, ports and invocation.
 * Status belongs to the owning parser, never a terminal/session error path. */
int st_rip3_draw_bitmap(struct st_rip3_ports *ports,
	const struct st_rip3_bitmap *bitmap,
	const struct st_rip3_bitmap_destination *destination,
	const struct st_rip3_palette *palette, bool palette_adopted,
	enum st_rip3_transfer_mode mode,
	const uint8_t *transparent_color);

#endif
