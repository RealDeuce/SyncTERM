/* Device placement of the resolution-independent RIP3 text-window owner. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_TEXT_WINDOW_H_
#define ST_RIP3_TEXT_WINDOW_H_

#include "st_rip3_geometry.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_text_cursor_position {
	bool set_column,set_row;
	uint64_t column,row;
};

/* Borrowed single-owner text-window services for host control variables.
 * Numbers are decimal TW slots 0..35. Cursor operations skip undefined/inactive
 * targets, permit protected definitions and retain current selection.
 * Unspecified axes retain the chosen target's coordinate. Clamp supplied
 * coordinates against its full dimensions before narrowing, independently
 * of ANSI origin/margins and graphics PORT state.
 * These statuses stay below the parser's nonfatal recovery boundary. */
struct st_rip3_text_window_operations {
	void *context;
	int (*current)(void *context, unsigned *number);
	int (*activate)(void *context, const unsigned *numbers, size_t count, bool active);
	int (*position)(void *context, unsigned number, const struct st_rip3_text_cursor_position *position);
	int (*cursor)(void *context, const unsigned *numbers, size_t count, bool visible);
};

struct st_rip3_text_window_layout {
	/* Ordered, range-clamped inclusive cell coordinates for ordinary w. */
	uint32_t x0,y0,x1,y1;
	unsigned font;
	bool wrap;
	/* Absolute DEVICE placement; independent of PORT, viewport and WORLD.
	 * The physical canvas clips any far-edge cell overhang. */
	uint32_t x,y,columns,rows,cell_width,cell_height;
};

/* Geometry service below the parser's void recovery boundary. These are
 * non-deactivation w layouts: the all-zero tuple's gate operation belongs
 * to the owner and must retain its prior geometry/cursor. Reversed endpoint
 * pairs are ordered and every coordinate clamps to the nominal font grid
 * before computing extents and placement. Failure preserves
 * result. No font resource, ANSI parser, text rendering or table mutation. */
int st_rip3_text_window_normal(struct st_rip3_text_window_layout *result,
	const struct st_rip3_frame *device, uint32_t x0, uint32_t y0,
	uint32_t x1, uint32_t y1, unsigned wrap, unsigned font);
/* The ordinary w identity excludes wrap. Repeating an identical definition
 * updates wrap without relocating the current ANSI cursor. */
bool st_rip3_text_window_same(const struct st_rip3_text_window_layout *left,
	const struct st_rip3_text_window_layout *right);

#endif
