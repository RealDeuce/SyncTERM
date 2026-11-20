/* Device placement of the resolution-independent RIP3 text-window owner. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_TEXT_WINDOW_H_
#define ST_RIP3_TEXT_WINDOW_H_

#include "st_rip3_geometry.h"
#include <stdbool.h>
#include <stdint.h>

struct st_rip3_text_window_layout {
	/* Original inclusive character-cell coordinates for ordinary w. */
	uint32_t x0,y0,x1,y1;
	unsigned font;
	bool wrap;
	/* Absolute DEVICE placement; independent of PORT, viewport and WORLD.
	 * The physical canvas clips any far-edge cell overhang. */
	uint32_t x,y,columns,rows,cell_width,cell_height;
};

/* Geometry service below the parser's void recovery boundary. These are
 * non-deactivation w layouts: the all-zero tuple's gate operation belongs
 * to the owner and must retain its prior geometry/cursor. Failure preserves
 * result. No font resource, ANSI parser, text rendering or table mutation. */
int st_rip3_text_window_normal(struct st_rip3_text_window_layout *result,
	const struct st_rip3_frame *device, uint32_t x0, uint32_t y0,
	uint32_t x1, uint32_t y1, unsigned wrap, unsigned font);
/* The ordinary w identity excludes wrap. Repeating an identical definition
 * updates wrap without relocating the current ANSI cursor. */
bool st_rip3_text_window_same(const struct st_rip3_text_window_layout *left,
	const struct st_rip3_text_window_layout *right);

#endif
