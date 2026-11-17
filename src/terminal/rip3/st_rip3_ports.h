/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_PORTS_H_
#define ST_RIP3_PORTS_H_

#include "st_rip3_geometry.h"
#include "st_rip3_surface.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_pixel_buffer;
struct st_rip3_ports;
struct st_rip3_backup;
struct st_rip3_surface;

struct st_rip3_image_style {
	/* Stored DEVICE boundaries relative to the viewport, without clipping.
	 * All -1 is the default viewport-at-display marker; raw all-zero is the
	 * separate internal inhibit sentinel. Wire all-zero input stores 0,0,1,1.
	 * JPEG presentation consumes its own far-edge units before fitting. */
	bool automatic;
	bool enabled;
	struct st_rip3_rect area;
	unsigned flags;
};

struct st_rip3_port_info {
	bool defined;
	bool offscreen;
	bool protected;
	bool viewport_active;
	/* Screen bounds are absolute device coordinates; offscreen bounds
	 * start at zero. Viewport coordinates are relative to these bounds, but
	 * wire definitions can be negative or extend beyond the port. */
	struct st_rip3_rect bounds;
	struct st_rip3_rect viewport;
	/* Derived viewport-relative intersection with the underlying physical
	 * surface, independent of activation. Empty is 0,0,-1,-1. This limits
	 * raster work without changing the stored origin, extent or fitting. */
	struct st_rip3_rect clip;
	struct st_rip3_image_style image;
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
 * fails. All endpoints here are inclusive; the wire producer converts extent
 * boundaries before calling. For offscreen ports x0/y0 are ignored.
 * A protected redefinition fails and selects port zero. Other failures leave
 * the table unchanged (client policy, not a protocol rollback guarantee).
 * Defining does not select/protect/deactivate implicitly. */
int st_rip3_ports_define(struct st_rip3_ports *ports, unsigned number,
	const struct st_rip3_rect *bounds, bool offscreen);
/* Install an already-prepared owned bitmap as a nonzero offscreen port, with
 * a full active viewport and automatic image style. No allocation or current
 * selection change occurs. Success consumes *bitmap and sets it to NULL;
 * failures retain both the table and caller ownership. Protected targets are
 * rejected without DEFINE_PORT's wire-level fallback selection. The caller
 * must resolve implicit replacement/clipboard/protection policy first. */
int st_rip3_ports_install_bitmap(struct st_rip3_ports *ports, unsigned number,
	struct st_rip3_surface **bitmap);
/* Undefined switch targets select port zero and return NOT_FOUND. */
int st_rip3_ports_switch(struct st_rip3_ports *ports, unsigned number);
/* Original wire/CUR selector: create an undefined target as a default full-
 * screen port, then select it. Creation failure selects zero and returns its
 * status without changing the failed entry. Defined protected/inactive ports
 * select normally. Invalid indexes fail before changing selection. This does
 * not apply protection flags or activate ENV references. */
int st_rip3_ports_select(struct st_rip3_ports *ports, unsigned number);
int st_rip3_ports_protect(struct st_rip3_ports *ports, unsigned number, bool protect);
/* Delete zero means delete all unprotected nonzero ports. A deleted current
 * port falls back to zero. Protected single deletes fail without changes. */
int st_rip3_ports_delete(struct st_rip3_ports *ports, unsigned number);
/* Restore port zero's full active viewport and select zero; normal resets
 * retain protected ports, hard resets discard all nonzero ports. Reset image
 * state for the old current port (even if protected) and for zero; other
 * protected survivors retain their records. Pixels are
 * retained for the scene reset's separate screen-clear operation. */
int st_rip3_ports_reset(struct st_rip3_ports *ports, bool hard);
/* Configure the current port's viewport in port-relative device coordinates.
 * Bounds clamp to the port. All-zero device bounds define one active pixel;
 * wire-level projected all-zero deactivation belongs to the command decoder.
 * Use st_rip3_ports_activate to deactivate while retaining the rectangle. */
int st_rip3_ports_viewport(struct st_rip3_ports *ports,
	const struct st_rip3_rect *viewport);
/* Wire producer's absolute DEVICE rectangle (offscreen origin is zero).
 * Inclusive endpoints, already normalized, must be nonnegative. Store it
 * without port/surface intersection, activate, and retain image state. This
 * differs from the bounded port-relative device setter above. */
int st_rip3_ports_viewport_device(struct st_rip3_ports *ports,
	const struct st_rip3_rect *viewport);
int st_rip3_ports_activate(struct st_rip3_ports *ports, bool active);
/* Replace one defined port's raw image record without selecting it. NULL
 * means all-minus-one viewport fallback; raw zero inhibits explicit display.
 * Preserve unclipped boundaries and accept flags 0..63. Wire setters may edit
 * protected/inactive ports; macros apply their own gates before this service.
 * All input validation precedes mutation. No drawing or file policy occurs. */
int st_rip3_ports_image_style(struct st_rip3_ports *ports, unsigned number,
	const struct st_rip3_rect *area, unsigned flags);
enum st_rip3_view_operation {
	ST_RIP3_VIEW_ACTIVATE,
	ST_RIP3_VIEW_DEACTIVATE,
	ST_RIP3_VIEW_MAXIMIZE,
	ST_RIP3_VIEW_ERASE,
};
/* Apply to numbered ports without changing the current selection. Validate
 * every index before changes; repeated indexes are idempotent. Undefined
 * ports are ignored. Activation/deactivation skip protected ports; maximize
 * also skips inactive viewports and fills the underlying port's full bounds.
 * Erase skips inactive viewports, permits protected content and uses COPY
 * with the explicitly resolved current background index. No definitions,
 * protection, current selection or pixels outside erased viewports change. */
int st_rip3_ports_operate_views(struct st_rip3_ports *ports,
	const unsigned *numbers, size_t count, enum st_rip3_view_operation operation,
	uint8_t background);
/* Drawing coordinates are relative to the current viewport's upper-left.
 * Protection locks configuration but permits all content operations. */
int st_rip3_ports_plot(struct st_rip3_ports *ports, int32_t x, int32_t y,
	uint8_t color, enum st_rip3_write_mode mode);
int st_rip3_ports_fill(struct st_rip3_ports *ports, const struct st_rip3_rect *area,
	uint8_t color, enum st_rip3_write_mode mode);
int st_rip3_ports_erase(struct st_rip3_ports *ports, uint8_t background);
/* Explicitly fitted rectangles in each stored viewport's local coordinates;
 * NULL means the full viewport. Physical clipping preserves the original
 * sampling rather than fitting again. Undefined/inactive operands are no-ops. */
int st_rip3_ports_copy(struct st_rip3_ports *ports, unsigned source,
	const struct st_rip3_rect *source_area, unsigned destination,
	const struct st_rip3_rect *destination_area, enum st_rip3_transfer_mode mode);
/* Unscaled viewport-relative transfer. NULL source_area means its full viewport.
 * Source clipping shifts the destination by the same number of pixels;
 * destination clipping trims the matching source pixels. Shared screen ports
 * read original pixels on overlap. Undefined/inactive operands are no-ops;
 * protection and current selection are retained, and uncovered source remains.
 * Wire commands that bypass viewports or erase uncovered areas need their own
 * explicit policy; this service does neither. */
int st_rip3_ports_blit(struct st_rip3_ports *ports, unsigned source,
	const struct st_rip3_rect *source_area, unsigned destination,
	int32_t destination_x, int32_t destination_y,
	enum st_rip3_transfer_mode mode);
/* Raw read coordinates are port-relative, independent of viewport activation. */
int st_rip3_ports_read(const struct st_rip3_ports *ports, unsigned number,
	int32_t x, int32_t y, uint8_t *result);
/* Read a viewport-relative pixel from the underlying surface, without port-
 * bound clipping or an activation gate. Outside physical pixels return RANGE;
 * they do not wrap or become an invented padded pixel value. */
int st_rip3_ports_read_viewport(const struct st_rip3_ports *ports, unsigned number,
	int32_t x, int32_t y, uint8_t *result);
int st_rip3_ports_snapshot(const struct st_rip3_ports *ports,
	const struct st_rip3_palette *palette, struct st_pixel_buffer **result);
/* Owned raw index-plane snapshots for the screen backup component, independent
 * of all port/viewport definitions. Restore requires matching device dimensions
 * and clones before committing; it neither takes ownership of nor changes the
 * saved surface. It bypasses viewport clipping/activation and drawing modes.
 * Palette and UI state belong to the enclosing screen backup transaction. */
int st_rip3_ports_screen_clone(const struct st_rip3_ports *ports,
	struct st_rip3_surface **result);
int st_rip3_ports_screen_restore(struct st_rip3_ports *ports,
	const struct st_rip3_surface *snapshot);

/* Backup the device port definitions/current selection and owned offscreen
 * pixels. Onscreen pixels belong to the separate screen backup area. Restore
 * prepares all bitmap copies before replacing any live entry, bypasses live
 * protection, and requires the same device canvas. The backup survives the
 * original ports object. Image styles are included. Query/cursor/clipboard
 * metadata is not implemented
 * in this device table yet and must be added to its snapshot with that state. */
int st_rip3_ports_backup_create(struct st_rip3_backup **result);

#endif
