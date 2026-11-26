/* Borrowed local device/query state for ordinary RIP3 data references. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_LOCAL_VARIABLES_H_
#define ST_RIP3_LOCAL_VARIABLES_H_

#include <stdbool.h>

#include "st_rip3_geometry.h"

enum st_rip3_modifier {
	ST_RIP3_MODIFIER_SHIFT = 1,
	ST_RIP3_MODIFIER_CTRL  = 2
};

/* The text-window owner supplies its resolved DEVICE cell grid. Coordinates
 * here are not another WORLD frame. Nonzero cell sizes and row/column counts
 * are required; XY clamps its computed zero-based cell to this grid. */
struct st_rip3_query_cells {
	struct st_rip3_point origin;
	unsigned             cell_width, cell_height, columns, rows;
};

struct st_rip3_pointer_buttons {
	bool left, middle, right;
};

struct st_rip3_backspace {
	bool nondestructive;
	bool send_del;
};

/* One interpreter thread borrows these bindings and their backing values.
 * Keep binding pointers stable while an enclosing query is pending; values
 * may change between processing calls. Read each reference when reached,
 * including after a delay. No keyboard injection, mouse sampling or device
 * discovery is performed by the protocol code. */
struct st_rip3_local_queries {
	void *context;
	/* Nonblocking current modifier snapshot; status is absorbed below the void
	 * parser boundary. Do not reenter or mutate the parser from this callback. */
	int (*modifiers)(void *context, unsigned *down);
	/* Both operations address the active connection's ANSI keyboard mode. */
	bool (*doorway_mode)(void *context);
	void (*set_doorway_mode)(void *context, bool enabled);
	/* Captured coordinates of this host/query event, not a fresh live-pointer
	 * request. An absent sample supplies DEVICE (0,0), independently of MSTAT. */
	const struct st_rip3_point           *mouse_position;
	const struct st_rip3_pointer_buttons *mouse_buttons;
	/* Independent backend presence check, even before the first event. */
	int (*mouse_present)(void *context, bool *present);
	/* Destination device capacity, independently of current palette/ENV mode. */
	const unsigned                   *colors;
	bool                              text_window_query;
	const struct st_rip3_query_cells *text_window;
};

#endif
