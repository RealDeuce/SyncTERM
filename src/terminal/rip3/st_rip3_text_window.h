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

struct st_rip3_text_window_info { bool defined,protected,active,extended; };
struct st_rip3_text_window_snapshot;

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
	/* CUR first use creates fresh defaults; wire 2T has a separate clone path. */
	int (*select)(void *context, unsigned number);
	int (*info)(void *context, unsigned number, struct st_rip3_text_window_info *result);
	int (*snapshot)(void *context, unsigned number, struct st_rip3_text_window_snapshot *result);
	int (*protect)(void *context, unsigned number, bool protect);
	int (*activate)(void *context, const unsigned *numbers, size_t count, bool active);
	/* Maximize an active unprotected definition without changing selection or painting. */
	int (*maximize)(void *context, unsigned number);
	int (*position)(void *context, unsigned number, const struct st_rip3_text_cursor_position *position);
	int (*cursor)(void *context, const unsigned *numbers, size_t count, bool visible);
	/* ETW: whole target bounding rectangle, graphics palette index 0. */
	int (*erase)(void *context, const unsigned *numbers, size_t count);
	/* TWERASEEOL: display row from cursor inclusive, target ANSI background. */
	int (*erase_eol)(void *context, const unsigned *numbers, size_t count);
	/* Validate the complete selector/permission set before helper calls. Live
	 * definition copies retain selection, carry protection except at zero,
	 * and own independent queries. Later allocation failure is not rollback. */
	int (*copy_entries)(void *context,unsigned source,const unsigned *destinations,size_t count);
	/* One validated live RESET target. Zero gets forced fresh defaults;
	 * unprotected nonzero definitions are deleted. No pixels or floating query
	 * change. Failure preserves this target; the dispatcher continues later
	 * selectors and resolves CUR from live selection each time. */
	int (*reset_entry)(void *context,unsigned number);
	void (*clear_query)(void *context, bool floating, unsigned number);
	/* Whole TW snapshots own logical definitions/native metadata, never screen pixels
	 * or connection parser input. Saved objects release without the live owner. */
	int (*backup_capture)(void *context,void **result);
	int (*backup_restore)(void *context,const void *saved);
	void (*backup_destroy)(void *saved);
};

struct st_rip3_text_window_layout {
	/* Ordered, range-clamped inclusive cell coordinates for ordinary w. */
	uint32_t x0,y0,x1,y1;
	unsigned font;
	bool wrap;
	bool extended;
	unsigned flags;
	/* Absolute DEVICE placement; independent of PORT, viewport and WORLD.
	 * The physical canvas clips any far-edge cell overhang. */
	uint64_t x,y,bound_x,bound_y,bound_width,bound_height;
	uint32_t columns,rows,cell_width,cell_height;
};

struct st_rip3_text_cell { uint32_t width,height; };
/* Read-only native observation. Cursor coordinates are one-based within the
 * full window, independently of ANSI origin/margins. Layout is unclipped.
 * The client's unredefined startup TTY has no established MicroANSI font. */
struct st_rip3_text_window_snapshot {
	struct st_rip3_text_window_layout layout;
	uint32_t column,row;
	bool cursor_visible,font_known;
};
struct st_rip3_rect;
struct st_rip3_backup;
int st_rip3_text_window_backup_create(struct st_rip3_backup **result);

/* Geometry service below the parser's void recovery boundary. These are
 * non-deactivation w layouts: the all-zero tuple's gate operation belongs
 * to the owner and must retain its prior geometry/cursor. Reversed endpoint
 * pairs are ordered and every coordinate clamps to the nominal font grid
 * before computing extents and placement. Failure preserves
 * result. No font resource, ANSI parser, text rendering or table mutation. */
int st_rip3_text_window_normal(struct st_rip3_text_window_layout *result,
	const struct st_rip3_frame *device, uint32_t x0, uint32_t y0,
	uint32_t x1, uint32_t y1, unsigned wrap, unsigned font);
/* Grow or shrink to the retained font's nominal grid at current cell metrics. */
int st_rip3_text_window_maximize(struct st_rip3_text_window_layout *layout);
/* The ordinary w identity excludes wrap. Repeating an identical definition
 * updates wrap without relocating the current ANSI cursor. */
bool st_rip3_text_window_same(const struct st_rip3_text_window_layout *left,
	const struct st_rip3_text_window_layout *right);
/* Extended b geometry: unsigned absolute WORLD endpoints, exclusive far
 * edges, directed first-fit selection using the actual loaded cell metrics.
 * Retain unclipped wide bounds, extend undersized axes at their far edges and
 * center the display with odd spare pixels at right/bottom. No state changes. */
int st_rip3_text_window_extended(struct st_rip3_text_window_layout *result,
	const struct st_rip3_frame *device, const struct st_rip3_text_cell cells[5],
	uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
	uint32_t columns, uint32_t rows, unsigned font, unsigned flags);
/* Clip the stored bounding rectangle only to the physical raster. Empty is
 * 0,0,-1,-1; narrowing follows clipping. Neither WORLD nor PORT is applied. */
int st_rip3_text_window_bounds(const struct st_rip3_text_window_layout *layout,
	uint32_t width, uint32_t height, struct st_rip3_rect *result);

#endif
