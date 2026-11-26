/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_DRAW_H_
#define ST_RIP3_DRAW_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "st_rip3_geometry.h"
#include "st_rip3_surface.h"

struct st_rip3_ports;

struct st_rip3_pen {
	uint16_t pattern;
	uint32_t thickness;
	bool     draw_background;
};

struct st_rip3_contour {
	const struct st_rip3_point *points;
	size_t                      count;
};

/* Style 0..3 selects the documented built-in mask; style 4 uses custom.
 * A zero custom mask is invalid. Thickness is a positive device-pixel count.
 * Line-style wire decoding and world-unit policy are handled by the caller. */
int st_rip3_pen_init(struct st_rip3_pen *pen);
int st_rip3_pen_set(struct st_rip3_pen *pen, bool draw_background, unsigned style, uint16_t custom,
    uint32_t thickness);
/* Device-pixel line, relative to the current port's viewport. Coverage is a
 * geometric round-capped stroke and each destination pixel is written once.
 * Even widths use a half-pixel offset toward positive y (positive x for a
 * vertical stroke), giving a stable choice of side in either direction.
 * Mask phase starts at the first endpoint's MSB and advances by major-axis
 * pixel steps; clipping retains that phase. Gaps optionally draw background.
 * Inactive viewports are no-ops. Invalid input is rejected before drawing. */
int st_rip3_draw_line(struct st_rip3_ports *ports, const struct st_rip3_point *first,
    const struct st_rip3_point *last, const struct st_rip3_pen *pen, uint32_t foreground, uint32_t background,
    enum st_rip3_write_mode mode);

/* Open/closed stroked path, 1..4096 vertices. Continuous endpoint-cap coverage
 * forms one visit; leaving and reentering a pixel's stroke area applies the
 * operation again. Duplicate vertices and ordinary closed seams add no visit.
 * Pattern phase continues across segments; foreground wins within one joined
 * visit. One vertex draws a point; a two-vertex polygon is a single line.
 * Allocation failure preserves pixels. Inputs remain borrowed/unmodified. */
int st_rip3_draw_path(struct st_rip3_ports *ports, const struct st_rip3_point *points, size_t count, bool closed,
    const struct st_rip3_pen *pen, uint32_t foreground, uint32_t background, enum st_rip3_write_mode mode);

/* An unfilled rectangle, accepting either pair of opposing corners. Degenerate
 * rectangles are a single line/point; corners and joins apply write mode once. */
int st_rip3_draw_rectangle(struct st_rip3_ports *ports, const struct st_rip3_rect *area,
    const struct st_rip3_pen *pen, uint32_t foreground, uint32_t background, enum st_rip3_write_mode mode);

#endif
