/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_ARC_H_
#define ST_RIP3_ARC_H_

#include <stdint.h>

enum st_rip3_arc_kind {
	ST_RIP3_ARC_POINT,
	ST_RIP3_ARC_CURVE,
	ST_RIP3_ARC_EMPTY,
};

struct st_rip3_arc_ray {
	int64_t x, y;
};

struct st_rip3_arc {
	struct st_rip3_arc_ray first, last;
	enum st_rip3_arc_kind  kind;
	double                 start, sweep;
};

/* Axis-aligned A/V/I/i/_ endpoint rays, with radii already converted to device
 * counts. Equal integer endpoints retain a point at the raw endpoint;
 * different endpoints on the same ray produce a complete curve. A single
 * center-collapsed ray is empty. For a singular ellipse, nonzero rays use
 * their ordinary bearing and the renderer's degenerate-ellipse policy.
 * These last two policies do not claim native GDI pixel reproduction. */
int st_rip3_arc_axis(struct st_rip3_arc *result, uint64_t radius_x, uint64_t radius_y, unsigned start,
    unsigned end);

#endif
