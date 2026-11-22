/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_CLIPBOARD_H_
#define ST_RIP3_CLIPBOARD_H_

#include <stdbool.h>
#include <stdint.h>

struct st_rip3_ports;

/* Borrowed session-owned selection. Captures allocate slots 1..35. An explicitly
 * assigned zero is retained but cannot be replaced by GET_IMAGE. Read-only
 * consumers do not change either field. The bound PORT owner captures/restores
 * this record by value as part of its table backup. */
struct st_rip3_clipboard_selection {
	bool set;
	unsigned port;
};

/* Viewport-relative DEVICE rectangle as passed to the shared capture helper.
 * Right/bottom are exclusive source edges. The helper's extra allocation unit
 * is intentional: [1,1,4,4) copies 3x3 into a 4x4 bitmap. Wire 1C supplies its
 * own first far-edge increment before calling. Values must be within
 * +/-1024*INT32_MAX; reversed edges reject without changes. */
struct st_rip3_clipboard_area {
	int64_t left, top, right, bottom;
};

/* Assigned target first, else first unused 1..35. Protection/slot-zero rejection
 * and no-free-slot failure retain all state. An unprotected assigned target is
 * deleted before budget/allocation; failure then clears selection and leaves
 * that target undefined. Deleting current selects zero, which remains current
 * and becomes the source read after destination creation. Copy/trim failure
 * retains the new clear allocation and reports success, as in the shared driver
 * helper. Other port definitions/pixels are retained.
 *
 * pixel_budget is an explicit client policy, measured against all currently
 * defined offscreen device width*height counts after target deletion. No display-
 * sized default is inferred. Low-WORD allocation counts are preserved; wrapped
 * zero counts remain unsupported before mutation pending their allocation case.
 * This is a state/storage service status; the void wire parser consumes it. */
int st_rip3_clipboard_capture(struct st_rip3_ports *ports,
	struct st_rip3_clipboard_selection *selection,
	const struct st_rip3_clipboard_area *area, uint64_t pixel_budget);

/* Button auto-stamp uses the same allocation/failure ownership with an exact
 * half-open logical footprint and COPY padding at the original source offset.
 * The button renderer checks the documented same-destination guard first. */
int st_rip3_clipboard_capture_button(struct st_rip3_ports *ports,
	struct st_rip3_clipboard_selection *selection,
	const struct st_rip3_clipboard_area *area, uint64_t pixel_budget);

/* LOAD_BITMAP's explicit post-draw duplicate route. Zero uses shared capture;
 * >35 and same-current nonzero selectors skip duplication. A different defined
 * target is deleted before allocation, clearing a matching clipboard pointer.
 * This route does not assign its replacement as the clipboard. Protection
 * returns PERMISSION_DENIED without deletion/selection change. Creation failure
 * leaves the target undefined; the caller may still perform wallpaper. Copy
 * failures are ignored after allocation. Ordinary extents have no extra unit. */
int st_rip3_clipboard_duplicate(struct st_rip3_ports *ports,
	struct st_rip3_clipboard_selection *selection, unsigned number,
	const struct st_rip3_clipboard_area *area, uint64_t pixel_budget);

#endif
