/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_BEZIER_H_
#define ST_RIP3_BEZIER_H_

#include <stdbool.h>
#include <stddef.h>

#include "st_rip3_geometry.h"

#define ST_RIP3_BEZIER_LIST_FIELDS "22"

/* Number of coordinate fields after a literal poly-Bézier record type. */
static inline unsigned
st_rip3_bezier_record_coordinates(unsigned type)
{
	static const unsigned counts[] = {2, 6, 6, 2, 2, 6, 6};
	return type < sizeof(counts) / sizeof(*counts) ? counts[type] : 0;
}

/* Converted device controls. Types 0/3/4 supply points[0]; 1/2/5/6 supply
 * P0/P1/P2, with P3 taken from the next record (or first for closed paths). */
struct st_rip3_bezier_record {
	unsigned             type;
	struct st_rip3_point points[3];
};

struct st_rip3_bezier_path {
	struct st_rip3_point *points;
	size_t                count;
};

/* Original eight-fractional-bit, wrapping signed-32 interpolation. The caller
 * supplies room for max(N,2) points. N>=2 writes P0, samples i/N for i=1..N-2,
 * then signed16(P3); the (N-1)/N sample is deliberately absent. N=0/1 writes
 * two scratch endpoints; the drawing consumer still submits only N points.
 * P0 retains its original integer. Interior coordinates floor then signed16.
 * N is 0..4095. Invalid arguments leave output unchanged. */
int st_rip3_bezier_sample(struct st_rip3_point *output, const struct st_rip3_point controls[4], unsigned count);
/* Original z/t/x record expansion, including low16 point/P3 conversion,
 * repeated joins and common-N reduction to at most 4094 submitted points.
 * The 8193-number input budget is independently checked. An open path rejects
 * a terminal cubic. N=1 contributes only P0, without its scratch endpoint.
 * Errors preserve result; destroy successful owned results before reuse. */
int  st_rip3_bezier_expand(struct st_rip3_bezier_path *result, const struct st_rip3_bezier_record *records,
    size_t count, unsigned requested, bool closed);
void st_rip3_bezier_free(struct st_rip3_bezier_path *path);

#endif
