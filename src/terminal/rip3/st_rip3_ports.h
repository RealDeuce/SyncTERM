/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_PORTS_H_
#define ST_RIP3_PORTS_H_

#include "st_rip3_geometry.h"
#include "st_rip3_surface.h"

#include <stdbool.h>
#include <stdint.h>

struct st_pixel_buffer;
struct st_rip3_ports;

struct st_rip3_port_info {
	bool defined;
	bool offscreen;
	bool protected;
	bool viewport_active;
	/* Screen bounds are absolute device coordinates; offscreen bounds
	 * start at zero. Viewport coordinates are relative to these bounds. */
	struct st_rip3_rect bounds;
	struct st_rip3_rect viewport;
};

/* Device-pixel port state, owned by one session thread. World translation and
 * wire-field special cases belong to the caller. Screen ports share one
 * surface, so overlapping regions never acquire separate composited layers. */
int st_rip3_ports_create(struct st_rip3_ports **result, enum st_rip3_canvas canvas);
void st_rip3_ports_destroy(struct st_rip3_ports *ports);
int st_rip3_ports_current(const struct st_rip3_ports *ports, unsigned *result);
int st_rip3_ports_info(const struct st_rip3_ports *ports, unsigned number,
	struct st_rip3_port_info *result);
/* Screen bounds are clamped to the device. An entirely external rectangle
 * fails. For offscreen ports x0/y0 are ignored, as specified by DEFINE_PORT.
 * A protected redefinition fails and selects port zero. Other failures leave
 * the table unchanged. Defining does not select/protect/deactivate implicitly. */
int st_rip3_ports_define(struct st_rip3_ports *ports, unsigned number,
	const struct st_rip3_rect *bounds, bool offscreen);
/* Undefined switch targets select port zero and return NOT_FOUND. */
int st_rip3_ports_switch(struct st_rip3_ports *ports, unsigned number);
int st_rip3_ports_protect(struct st_rip3_ports *ports, unsigned number, bool protect);
/* Delete zero means delete all unprotected nonzero ports. A deleted current
 * port falls back to zero. Protected single deletes fail without changes. */
int st_rip3_ports_delete(struct st_rip3_ports *ports, unsigned number);
/* Restore port zero's full active viewport and select zero; normal resets
 * retain protected ports, hard resets discard all nonzero ports. Pixels are
 * retained for the scene reset's separate screen-clear operation. */
int st_rip3_ports_reset(struct st_rip3_ports *ports, bool hard);
/* Configure the current port's viewport in port-relative device coordinates.
 * All-zero coordinates deactivate it; other bounds clamp to the port. */
int st_rip3_ports_viewport(struct st_rip3_ports *ports,
	const struct st_rip3_rect *viewport);
int st_rip3_ports_activate(struct st_rip3_ports *ports, bool active);
/* Drawing coordinates are relative to the current viewport's upper-left.
 * Protection locks configuration but permits all content operations. */
int st_rip3_ports_plot(struct st_rip3_ports *ports, int32_t x, int32_t y,
	uint8_t color, enum st_rip3_write_mode mode);
int st_rip3_ports_fill(struct st_rip3_ports *ports, const struct st_rip3_rect *area,
	uint8_t color, enum st_rip3_write_mode mode);
int st_rip3_ports_erase(struct st_rip3_ports *ports, uint8_t background);
/* Explicitly fitted rectangles in each viewport's local coordinates; NULL
 * means the full viewport. Undefined/inactive source/destination is a no-op. */
int st_rip3_ports_copy(struct st_rip3_ports *ports, unsigned source,
	const struct st_rip3_rect *source_area, unsigned destination,
	const struct st_rip3_rect *destination_area, enum st_rip3_transfer_mode mode);
/* Raw read coordinates are port-relative, independent of viewport activation. */
int st_rip3_ports_read(const struct st_rip3_ports *ports, unsigned number,
	int32_t x, int32_t y, uint8_t *result);
int st_rip3_ports_snapshot(const struct st_rip3_ports *ports,
	const struct st_rip3_palette *palette, struct st_pixel_buffer **result);

#endif
