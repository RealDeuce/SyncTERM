/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_POLYGON_INTERNAL_H_
#define ST_RIP3_POLYGON_INTERNAL_H_

#include "st_rip3_draw_internal.h"
#include "st_rip3_fill.h"

/* One closed integral device path, used by projected filled-polygon commands.
 * Finite integral coordinates up to +/-1024*INT32_MAX remain unsaturated.
 * Pixel centers use even-odd parity, with left-inclusive/right-exclusive spans.
 * Self-crossings, repeated/subdivided edges and reversed winding retain parity.
 * A supplied border preserves the resolved pen pattern and uses COPY after the
 * fill. All validation/allocations precede painting. Wire count/border/default
 * policy belongs to the decoder; this kernel accepts 1..4096 vertices. */
/* One shared even-odd interior and border coverage over multiple contours.
 * The aggregate device-kernel vertex budget remains 4096. */
int st_rip3_polygon_fill_contours(struct st_rip3_ports *ports, const struct st_rip3_vector_contour *contours,
    size_t count, const struct st_rip3_fill *fill, uint32_t foreground, uint32_t background,
    const struct st_rip3_pen *border, uint32_t border_color, enum st_rip3_write_mode mode);
int st_rip3_polygon_fill(struct st_rip3_ports *ports, const struct st_rip3_vector *points, size_t count,
    const struct st_rip3_fill *fill, uint32_t foreground, uint32_t background, const struct st_rip3_pen *border,
    uint32_t border_color, enum st_rip3_write_mode mode);
/* Flag-1 marker. Current W applies to both contributions. An opaque pen
 * takes precedence over fill in overlapping coverage. */
int st_rip3_polygon_marker_fill(struct st_rip3_ports *ports, const struct st_rip3_vector *points, size_t count,
    const struct st_rip3_fill *fill, uint32_t foreground, uint32_t background, const struct st_rip3_pen *pen,
    uint32_t draw_color, enum st_rip3_write_mode mode);

#endif
