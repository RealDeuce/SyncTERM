/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_FILL_H_
#define ST_RIP3_FILL_H_

#include "st_rip3_draw.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_fill {
	uint8_t rows[8];
	/* Explicit device-pixel phase, relative to the current viewport. Host
	 * pattern anchoring is not inferred from the undocumented wire policy. */
	struct st_rip3_point origin;
};

/* Top-to-bottom rows, MSB left. All eight values are validated before change. */
int st_rip3_fill_custom(struct st_rip3_fill *fill, const unsigned rows[8],
	const struct st_rip3_point *origin);
int st_rip3_fill_solid(struct st_rip3_fill *fill, bool foreground);
/* Device-pixel rectangles; opposing corners are normalized. BAR includes the
 * last row/column and ignores border state. Pattern-off pixels use background.
 * FILLED_RECTANGLE excludes the last row/column of its interior. A non-NULL
 * border pen adds a COPY border, ignoring its mask as specified for this shape.
 * Degenerate filled rectangles draw only that border (when enabled). */
int st_rip3_draw_bar(struct st_rip3_ports *ports,
	const struct st_rip3_rect *area, const struct st_rip3_fill *fill,
	uint8_t foreground, uint8_t background, enum st_rip3_write_mode mode);
int st_rip3_draw_filled_rectangle(struct st_rip3_ports *ports,
	const struct st_rip3_rect *area, const struct st_rip3_fill *fill,
	uint8_t foreground, uint8_t background, const struct st_rip3_pen *border,
	uint8_t border_color, enum st_rip3_write_mode mode);

/* Even-odd fill over one or more closed contours (up to 4096 contours and
 * 4096 vertices per contour in this device-pixel kernel). Coincident edges,
 * overlaps and nested holes retain parity; contour winding is immaterial.
 * Pixel centers use left-inclusive/right-exclusive span coverage. Optional
 * borders preserve the pen pattern and always use COPY. Scratch allocations
 * and validation finish before pixels change; caller inputs are unchanged. */
int st_rip3_draw_filled_polygons(struct st_rip3_ports *ports,
	const struct st_rip3_contour *contours, size_t count,
	const struct st_rip3_fill *fill, uint8_t foreground, uint8_t background,
	const struct st_rip3_pen *border, uint8_t border_color,
	enum st_rip3_write_mode mode);

#endif
