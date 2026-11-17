/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_WALLPAPER_H_
#define ST_RIP3_WALLPAPER_H_

#include <stdbool.h>

struct st_rip3_ports;
struct st_rip3_bitmap_destination;

/* Repeat the composited seed through the live current viewport with COPY.
 * Input uses inclusive viewport-relative DEVICE edges. Trim far edges to the
 * viewport before snapshotting; the near origin must be inside it. Alternate
 * rows shift left by floor(trimmed seed width/2), measured from the seed row
 * in both directions. No scaling, palette, key or source ROP is repeated.
 * Inactive viewports are no-ops. Failed validation/staging changes no pixels;
 * success changes content only, including in protected ports. The owning void
 * parser consumes this service status. No file or clipboard policy occurs. */
int st_rip3_wallpaper(struct st_rip3_ports *ports,
	const struct st_rip3_bitmap_destination *seed, bool stagger);

#endif
