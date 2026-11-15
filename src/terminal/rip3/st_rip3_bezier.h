/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_BEZIER_H_
#define ST_RIP3_BEZIER_H_

#include "st_rip3_fill.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Device geometry, after world translation. Smooth/jointed differ only in
 * authoring constraints; both supply two controls and an endpoint. A line
 * supplies its endpoint in points[0]. No source geometry is modified. */
enum st_rip3_bezier_type {
	ST_RIP3_BEZIER_LINE,
	ST_RIP3_BEZIER_SMOOTH,
	ST_RIP3_BEZIER_JOINTED,
};

struct st_rip3_bezier_segment {
	enum st_rip3_bezier_type type;
	struct st_rip3_point points[3];
};

struct st_rip3_bezier_path {
	struct st_rip3_point *points;
	size_t count;
};

/* Owned, uniformly subdivided cubic Bernstein path. This kernel takes an
 * explicit positive subdivision count (1..4095), and limits the total expanded
 * path to 4096 vertices, including the start. Samples t=i/subdivisions use
 * double arithmetic and round to the nearest device-pixel address (half ties
 * toward positive infinity); endpoints are exact. Errors leave result unchanged.
 * Destroy successful results before reuse. Wire count/range/recovery and final
 * block rules are separate policies pending documentation clarification #15. */
int st_rip3_bezier_build(struct st_rip3_bezier_path *result,
	const struct st_rip3_point *start,
	const struct st_rip3_bezier_segment *segments, size_t count,
	size_t subdivisions);
void st_rip3_bezier_free(struct st_rip3_bezier_path *path);

/* Strokes preserve line pattern/phase and write mode across the entire path.
 * A closed path adds its final edge even when the input endpoint differs from
 * the start. Filled paths close implicitly, use even-odd interiors and optional
 * patterned COPY borders. Both allocate/validate before changing pixels. */
int st_rip3_draw_bezier_path(struct st_rip3_ports *ports,
	const struct st_rip3_point *start,
	const struct st_rip3_bezier_segment *segments, size_t count,
	size_t subdivisions, bool closed, const struct st_rip3_pen *pen,
	uint8_t foreground, uint8_t background, enum st_rip3_write_mode mode);
int st_rip3_draw_filled_bezier_path(struct st_rip3_ports *ports,
	const struct st_rip3_point *start,
	const struct st_rip3_bezier_segment *segments, size_t count,
	size_t subdivisions, const struct st_rip3_fill *fill,
	uint8_t foreground, uint8_t background, const struct st_rip3_pen *border,
	uint8_t border_color, enum st_rip3_write_mode mode);

#endif
