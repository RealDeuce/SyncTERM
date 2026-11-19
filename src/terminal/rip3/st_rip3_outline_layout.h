/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_OUTLINE_LAYOUT_H_
#define ST_RIP3_OUTLINE_LAYOUT_H_

#include "st_rip3_geometry.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_outline_style;

struct st_rip3_outline_layout {
	struct st_rip3_point direction; /* Unit device-axis string direction. */
	struct st_rip3_point baseline; /* Addition to the aligned zero-B origin. */
	struct st_rip3_point shadow; /* Device displacement; zero disables it. */
	unsigned character_turns; /* Counterclockwise quarter turns, independent. */
	unsigned font_orientation; /* Original font-instance mapping: 0,3,2,1. */
	unsigned spacing;
	bool bounding_box_spacing;
};

/* Pure layout services, not parser/session entry points. Resolve the stored y
 * rotations, signed baseline branch table and shadow distance/direction. The
 * baseline is already in device units: flag 0x200 never converts it again.
 * Shadow uses the current canvas/world X ratio on both device axes. Point size
 * zero is a deferred render error. Compatibility bytes and high unused flag
 * bits have no effect. Validation failures leave result unchanged. */
int st_rip3_outline_layout_prepare(struct st_rip3_outline_layout *result,
	const struct st_rip3_outline_style *style, const struct st_rip3_frame *frame);

/* Font-engine advances use 1/1440-inch units. Convert to a selected device DPI
 * with signed integer truncation toward zero, before spacing is added. DPI is
 * explicit: the caller owns which realized metric/axis it queried. Results
 * outside int32_t or DPI outside 1..72*1024 fail without changing result. */
int st_rip3_outline_metric_device(int32_t *result, int32_t advance, unsigned dpi);

/* Apply spacing to already converted device advances and place each byte from
 * an already aligned zero-B device origin. Font selection, metric acquisition,
 * kerning and justification precede this operation. The realized font bbox
 * width is already in device units and must be nonnegative. Each extra gap is
 * trunc((spacing-100)*reference/100), using that bbox or the ordinary advance.
 * Supplied origins retain fractions; the baseline addition is device-space.
 * Positions must fit +/-4*INT32_MAX, matching the outline geometry kernel.
 * total is the adjusted layout advance, NOT the cached whole-string metric
 * used by wire cursor stores. Empty input permits NULL advances/positions.
 * Input/output arrays must not overlap. Preflight precedes every output write;
 * failures preserve positions and total. No allocation or global state. */
int st_rip3_outline_layout_positions(const struct st_rip3_outline_layout *layout,
	const struct st_rip3_text_position *zero_baseline, const int32_t *advances,
	size_t length, int32_t bbox_width, struct st_rip3_text_position *positions,
	int64_t *total);

#endif
