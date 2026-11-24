/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_SKEW_H_
#define ST_RIP3_SKEW_H_

#include "st_rip3_draw_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum st_rip3_skew_kind {
	ST_RIP3_SKEW_ARC,
	ST_RIP3_SKEW_CHORD,
	ST_RIP3_SKEW_PIE,
	ST_RIP3_SKEW_OVAL,
};
struct st_rip3_skew_path {
	struct st_rip3_vector *points;
	size_t count;
	unsigned start,end;
};

/* Order-dependent original span policy; raw angles are 0..4095. */
int st_rip3_skew_span(unsigned *start, unsigned *end);
/* One-degree integer contour, retaining repeated samples and turns. Inputs
 * are already WORLD-converted device counts. Two Q14 floor stages reproduce
 * ordinary original table arithmetic; wide products deliberately avoid native
 * overflow. ARC has S..E; CHORD appends first; PIE appends center and first;
 * OVAL has exactly 360 samples and leaves closure to Polygon. Owned output is
 * unchanged on error. Input centers/radii are bounded by 512*INT32_MAX, covering
 * the complete supported five-digit wire projection; rotation is 0..4095. */
int st_rip3_skew_build(struct st_rip3_skew_path *result,
	int64_t x, int64_t y, uint64_t rx, uint64_t ry,
	unsigned rotation, unsigned start, unsigned end,
	enum st_rip3_skew_kind kind);
void st_rip3_skew_free(struct st_rip3_skew_path *path);
int st_rip3_marker_build(struct st_rip3_skew_path *result,
	unsigned marker, int64_t x, int64_t y, uint64_t width, uint64_t height,
	unsigned rotation);
/* Device center, two conjugate-radius endpoints, then two arc ray points. */
int st_rip3_affine_build(struct st_rip3_skew_path *result, bool *closed,
	const struct st_rip3_vector control[5], enum st_rip3_skew_kind kind);

#endif
