/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
/* Private stroke coverage shared by paths, filled polygons and curves. */
#ifndef ST_RIP3_DRAW_INTERNAL_H_
#define ST_RIP3_DRAW_INTERNAL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "st_rip3_draw.h"

struct st_rip3_stroke_mask {
	struct st_rip3_rect bounds;
	size_t              width;
	size_t              height;
	/* 0 = transparent, 1 = pen background, 2 = pen foreground. */
	uint8_t *coverage;
};

struct st_rip3_vector {
	double x;
	double y;
};

struct st_rip3_vector_contour {
	const struct st_rip3_vector *points;
	size_t                       count;
};

struct st_rip3_vector_run {
	size_t first;
	size_t count;
};

/* Solid generated curves, retaining subpixel vertices. Runs are open; a
 * closed curve supplies its first point again at the end. */
int st_rip3_stroke_vectors(struct st_rip3_ports *ports, const struct st_rip3_vector *vertices, size_t vertex_count,
    const struct st_rip3_vector_run *runs, size_t run_count, uint32_t thickness,
    struct st_rip3_stroke_mask *result);

int st_rip3_contours_validate(const struct st_rip3_contour *contours, size_t count, size_t *vertices);
/* Produces owned coverage without drawing. Each contour starts mask phase zero;
 * segments within a contour carry phase. Empty/inactive coverage succeeds. */
int st_rip3_stroke_collect(struct st_rip3_ports *ports, const struct st_rip3_contour *contours, size_t count,
    bool closed, const struct st_rip3_pen *pen, struct st_rip3_stroke_mask *result);
/* Union coverage for a projected path with an explicit pen.
 * Integer world projection can exceed int32_t; finite coordinates up to
 * 1024*INT32_MAX are retained until visible coverage is collected. Phase
 * continues across edges and each destination pixel is applied once. */
/* Multiple projected contours share one coverage allocation. Pattern phase
 * restarts for each contour; foreground wins at overlapping borders. */
int st_rip3_stroke_vector_contours(struct st_rip3_ports *ports, const struct st_rip3_vector_contour *contours,
    size_t count, bool closed, const struct st_rip3_pen *pen, struct st_rip3_stroke_mask *result);
int st_rip3_stroke_path_vectors(struct st_rip3_ports *ports, const struct st_rip3_vector *vertices, size_t count,
    bool closed, const struct st_rip3_pen *pen, struct st_rip3_stroke_mask *result);

/* Standalone traversal: preserve distinct visits and compose their operands
 * before painting. Consecutive coverage joins through overlapping endpoint
 * caps; duplicate vertices do not add visits. Closed seams join cyclically.
 * Foreground wins within one joined component, while distinct visits retain
 * their order/operation. Validation/allocation failure preserves the scene. */
int st_rip3_stroke_visits(struct st_rip3_ports *ports, const struct st_rip3_vector *points, size_t count,
    bool closed, const struct st_rip3_pen *pen, uint32_t foreground, uint32_t background,
    enum st_rip3_write_mode mode);

int st_rip3_stroke_apply(struct st_rip3_ports *ports, const struct st_rip3_stroke_mask *mask, uint32_t foreground,
    uint32_t background, enum st_rip3_write_mode mode);
/* Prepared text masks: foreground wins over shadow; apply the write mode once
 * per covered pixel. Both masks use their own already-clipped bounds. */
int  st_rip3_stroke_shadow_apply(struct st_rip3_ports *ports, const struct st_rip3_stroke_mask *ink,
    const struct st_rip3_stroke_mask *shadow, uint32_t foreground, uint32_t background,
    enum st_rip3_write_mode mode);
void st_rip3_stroke_free(struct st_rip3_stroke_mask *mask);

#endif
