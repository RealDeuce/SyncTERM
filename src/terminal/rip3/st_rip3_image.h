/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_IMAGE_H_
#define ST_RIP3_IMAGE_H_

#include "st_rip3_surface.h"

#include <stddef.h>
#include <stdint.h>

struct st_pixel_buffer;
struct st_rip3_ports;

/* Render an already-decoded image through the current port's image style.
 * Input is tagged RGB (JPEG/24-bit BMP) or indexes into an explicit independent
 * source palette (indexed BMP). Only the primary plane is used. Active drawing
 * palette entries are never changed; colors use its nearest RGB entry.
 *
 * Device-pixel policy: clamp area edges to the viewport, nearest source pixel
 * centers, and flag 1 fits/centers aspect ratio while retaining margins.
 * Integer fit rounds down with a minimum one-pixel dimension; odd extra margins
 * go right or below. This is geometric/spec rendering, not RIPtel pixel reproduction.
 * Flag 4 skips the initial black COPY clear of the fitted rectangle. Image
 * pixels always use COPY, independently of vector write mode/background.
 * Physical clipping retains the original fit/sample coordinates and stages only
 * visible pixels. Source pixels/palettes and visible fit staging are prepared before
 * initial painting. Flag 16 then tiles the stored image-area rectangle with
 * COPY, including retained margins and the extra far row/column beyond the
 * reduced JPEG target. Flag 32 shifts alternate rows by half that tile width;
 * without 16 it does nothing. Wallpaper staging failure retains the initial
 * draw. A raw default marker fails rectangle reconciliation after drawing.
 *
 * Inactive viewport/internal inhibit state is a successful no-op, without
 * dereferencing image/palette. Protected ports permit drawing. Flag 8 is
 * ignored for RGB images; indexed palette commit is not implemented and is
 * rejected before drawing. Flag 2 is stored without JPEG file deletion.
 * Wire all-zero style remains enabled: initial drawing can use physical-size
 * fallback while its stored 1x1 wallpaper seed repeats the top-left pixel.
 * World/style production and whole-scene reset belong to the owning layer.
 * No file access, macro expansion or session-error propagation happens here.
 */
int st_rip3_draw_image(struct st_rip3_ports *ports,
	const struct st_pixel_buffer *image, const uint32_t *source_palette,
	size_t source_palette_size, const struct st_rip3_palette *palette);

#endif
