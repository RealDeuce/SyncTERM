/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
/* Private stroke coverage shared by path and filled-polygon rendering. */
#ifndef ST_RIP3_DRAW_INTERNAL_H_
#define ST_RIP3_DRAW_INTERNAL_H_

#include "st_rip3_draw.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_stroke_mask {
	struct st_rip3_rect bounds;
	size_t width;
	size_t height;
	/* 0 = transparent, 1 = pen background, 2 = pen foreground. */
	uint8_t *coverage;
};

int st_rip3_contours_validate(const struct st_rip3_contour *contours,
	size_t count, size_t *vertices);
/* Produces owned coverage without drawing. Each contour starts mask phase zero;
 * segments within a contour carry phase. Empty/inactive coverage succeeds. */
int st_rip3_stroke_collect(struct st_rip3_ports *ports,
	const struct st_rip3_contour *contours, size_t count, bool closed,
	const struct st_rip3_pen *pen, struct st_rip3_stroke_mask *result);
int st_rip3_stroke_apply(struct st_rip3_ports *ports,
	const struct st_rip3_stroke_mask *mask, uint8_t foreground,
	uint8_t background, enum st_rip3_write_mode mode);
void st_rip3_stroke_free(struct st_rip3_stroke_mask *mask);

#endif
