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
struct st_rip3_clipboard_selection;

/* Logical half-open DEVICE extents relative to viewport origins. Wide values
 * survive projection until logical clipping; physical coverage never changes
 * the remembered pair. These are geometry values, not saved pixel buffers. */
struct st_rip3_copy_extent {int64_t x0,y0,x1,y1;};
struct st_rip3_port_copy_pair {
	bool set;
	struct st_rip3_copy_extent source,destination;
};

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

/* Context-absolute graphics cursor. Wide integer storage preserves converted
 * wire positions until raster clipping; TEXTXY applies its own signed-WORD
 * reply narrowing. The display text encoding is not part of this record. */
struct st_rip3_cursor { int64_t x, y; };

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
	struct st_rip3_cursor cursor;
};

/* Device-pixel port state, owned by one session thread. World translation and
 * wire-field special cases belong to the caller. Screen ports share one
 * surface, so overlapping regions never acquire separate composited layers. */
int st_rip3_ports_create(struct st_rip3_ports **result, enum st_rip3_canvas canvas);
int st_rip3_ports_create_device(struct st_rip3_ports **result,
	uint32_t width, uint32_t height);
void st_rip3_ports_destroy(struct st_rip3_ports *ports);
/* Bind the session's existing selection for PORT snapshots. The borrowed
 * record must outlive the ports owner. There is no second live selection. */
void st_rip3_ports_bind_clipboard(struct st_rip3_ports *,struct st_rip3_clipboard_selection *);
/* Optional native screen enrichment for read-side protocol transfers only.
 * Merge into an owned, physically clipped raw source snapshot; never mutate
 * the scene/selection or reenter RIP. Binding/lifetime is client-owned and is
 * not part of PORT backups. Offscreen sources use their existing storage. */
/* The physical target is the assigned clipboard index, independent of CUR. */
int st_rip3_ports_clipboard_index(const struct st_rip3_ports *);
int st_rip3_ports_bitmap_pixels(const struct st_rip3_ports *, unsigned excluding, uint64_t *result);
void st_rip3_ports_bind_screen_reader(struct st_rip3_ports *,void *context,
	int (*read)(void *,const struct st_rip3_rect *,struct st_pixel_buffer *));
/* Publish a prepared, clipped raw patch after its primary is stored. The
 * frontend may resolve the scratch pixels in place for native presentation. */
void st_rip3_ports_bind_screen_writer(struct st_rip3_ports *,void *context,
	int (*write)(void *,const struct st_rip3_rect *,struct st_pixel_buffer *,const struct st_rip3_palette *));

struct st_rip3_surface_blit_area {
	int64_t x0,y0,x1,y1,destination_x,destination_y;
};
enum st_rip3_surface_blit_mode {
	ST_RIP3_BLIT_KEEP, ST_RIP3_BLIT_SOLID, ST_RIP3_BLIT_PATTERN, ST_RIP3_BLIT_SCROLL_CORNER,
};
struct st_rip3_surface_blit_fill {
	enum st_rip3_surface_blit_mode mode;
	uint32_t ink,background;
	uint8_t pattern[8];
};
/* Normalized inclusive DEVICE coordinates in the backing surface. Uses
 * current-port bounds and preserves its viewport and remembered copy pair.
 * Corner fill is for vertical movement; an identity copy leaves all pixels. */
int st_rip3_ports_blit_surface(struct st_rip3_ports *,const struct st_rip3_surface_blit_area *,
	const struct st_rip3_surface_blit_fill *,const struct st_rip3_palette *);
int st_rip3_ports_current(const struct st_rip3_ports *ports, unsigned *result);
int st_rip3_ports_info(const struct st_rip3_ports *ports, unsigned number,
	struct st_rip3_port_info *result);
/* Nonzero destination identity, or zero for an undefined/invalid port. Viewport,
 * cursor and protection edits retain it; backing/definition replacement does not. */
uint64_t st_rip3_ports_generation(const struct st_rip3_ports *ports, unsigned number);
/* Short captured drawing/capture context. Providers must run outside this call.
 * Restore surviving live records and selection; never resurrect a definition or
 * selected backing replaced by the callback. A stale identity rejects before draw. */
int st_rip3_ports_button_context(struct st_rip3_ports *ports, unsigned number,
	uint64_t generation, const struct st_rip3_port_info *view,
	int (*draw)(void *context), void *context);
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
 * retained for the scene reset's separate screen-clear operation. Port zero's
 * cursor initializes to zero; protected surviving ports retain their cursors. */
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
/* Raw cursor setter for a defined port, independent of protection/activation.
 * Coordinates are finite integers bounded by +/-1024*INT32_MAX. Failure leaves
 * the complete record unchanged. Info supplies the current stored value. */
int st_rip3_ports_cursor(struct st_rip3_ports *ports, unsigned number,
	const struct st_rip3_cursor *position);
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
	ST_RIP3_VIEW_RESET,
	ST_RIP3_PORT_CLEAR_BLACK,
};
/* HEADER table pruning and resident query clearing. */
int st_rip3_ports_header_prune(struct st_rip3_ports *, bool keep_current);
int st_rip3_ports_header_delete_clipboard(struct st_rip3_ports *);
void st_rip3_ports_header_clear_queries(struct st_rip3_ports *);
/* Apply to numbered, defined ports without changing the current selection.
 * Validate every index before changes. Activation, deactivation and geometry
 * changes skip protected ports. Maximize skips inactive viewports and reset
 * activates them; both restore full bounds without painting. Erase uses COPY
 * of the supplied background index across the viewport. Clear-black fills the
 * port's physical bounds with literal black. Both paint operations require an
 * active viewport and allow protected ports, preserving all port metadata. */
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
/* Nonnegative explicit half-open DEVICE rectangles, without WORLD/sentinels.
 * Classify equal sizes before clipping: equal copies trim both sides together,
 * unequal copies fit independently trimmed logical rectangles. The ordinary
 * copy kernel then publishes the successful logical pair on the destination.
 * Pass the active palette for mixed index/RGB composition and presentation. */
int st_rip3_ports_copy_device(struct st_rip3_ports *,unsigned source,
	const struct st_rip3_copy_extent *,unsigned destination,
	const struct st_rip3_copy_extent *,enum st_rip3_transfer_mode,
	const struct st_rip3_palette *);
/* Read optional metadata without selecting/defaulting any port. */
int st_rip3_ports_copy_pair(const struct st_rip3_ports *,unsigned,
	struct st_rip3_port_copy_pair *);
/* PCB reversal into current, COPY mode. NULL uses the bound assigned clipboard
 * only; absent/dangling pointers are no-ops without repair. Explicit sources
 * may include zero/current. Preserve selection, pointer, cursor and image state.
 * Service statuses stay below the existing void parser/session boundary. */
int st_rip3_ports_paste(struct st_rip3_ports *,const unsigned *source);
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
/* PUT_IMAGE reads the clipboard's stored viewport even when inactive.
 * The optional palette is used to present a native screen composite. */
int st_rip3_ports_put_image(struct st_rip3_ports *ports, unsigned source,
	unsigned destination, int32_t x, int32_t y, enum st_rip3_transfer_mode mode,
	const struct st_rip3_palette *palette);
/* Raw read coordinates are port-relative, independent of viewport activation. */
int st_rip3_ports_read(const struct st_rip3_ports *ports, unsigned number,
	int32_t x, int32_t y, uint8_t *result);
/* Read a viewport-relative pixel from the underlying surface, without port-
 * bound clipping or an activation gate. Outside physical pixels return RANGE;
 * they do not wrap or become an invented padded pixel value. */
int st_rip3_ports_read_viewport(const struct st_rip3_ports *ports, unsigned number,
	int32_t x, int32_t y, uint8_t *result);
/* Image-capture read preserving tagged native RGB. Coordinates are port-
 * relative unless viewport is true, with the same physical clipping rules as
 * the corresponding indexed read. No palette mapping or state mutation. */
int st_rip3_ports_read_color(const struct st_rip3_ports *,unsigned number,int32_t x,int32_t y,bool viewport,uint32_t *);
/* Capture the full requested port-relative or viewport-relative rectangle.
 * The caller owns the pixels, including native screen ink when bound.
 * Request the alternate blink phase for a later native screen write.
 * A rectangle outside physical storage fails without trimming the result. */
int st_rip3_ports_capture_pixels(const struct st_rip3_ports *, unsigned number,
	const struct st_rip3_rect *area, bool viewport, bool alternate, struct st_pixel_buffer **result);
/* Write a prepared region inside the active viewport's physical backing.
 * A native writer may resolve the provided pixels through the active palette. */
int st_rip3_ports_write_viewport_pixels(struct st_rip3_ports *, unsigned number,
	int32_t x, int32_t y, struct st_pixel_buffer *, const struct st_rip3_palette *);
int st_rip3_ports_snapshot(const struct st_rip3_ports *ports,
	const struct st_rip3_palette *palette, struct st_pixel_buffer **result);
/* Physical screen backup transport, independent of PORT selection, protection
 * and viewport. Restore copies the unscaled overlap without resizing or
 * changing any definition/generation. The enclosing screen owner handles PAL
 * and optional native ANSI planes. */
int st_rip3_ports_screen_capture(const struct st_rip3_ports *,struct st_pixel_buffer **);
int st_rip3_ports_screen_restore(struct st_rip3_ports *,const struct st_pixel_buffer *);
int st_rip3_ports_screen_damage(struct st_rip3_ports *ports,
	uint8_t *damage, size_t length);
/* Absolute inclusive DEVICE rectangle, clipped only to the physical screen.
 * COPY independent of current PORT, viewport, activation and protection.
 * Record damage even when the stored indexes already equal the paint. */
int st_rip3_ports_screen_fill(struct st_rip3_ports *ports,
	const struct st_rip3_rect *area, uint8_t color);
/* Same physical-screen COPY scope, with an opaque MSB-left 8x8 mask
 * anchored at screen (0,0). Uses existing surface writes/damage storage. */
int st_rip3_ports_screen_pattern(struct st_rip3_ports *ports,
	const struct st_rip3_rect *area, const uint8_t rows[8], uint8_t foreground, uint8_t background);
/* Button clipboard image into the current destination, at viewport-relative
 * DEVICE x/y. Read inactive/protected sources; retain their whole viewport's
 * native dimensions and pad unavailable pixels with index 0. COPY preserves
 * overlapping source pixels before padding. No selection/source mutation. */
int st_rip3_ports_clipboard_blit(struct st_rip3_ports *ports, unsigned source, int32_t x, int32_t y);
/* Same native-size transfer clipped/padded to a captured half-open face. */
int st_rip3_ports_button_clipboard(struct st_rip3_ports *ports,unsigned source,
	const struct st_rip3_rect *face);
/* Selected image inversion within the current drawing viewport. */
int st_rip3_ports_invert_rgb(struct st_rip3_ports *ports, const struct st_rip3_rect *area,
	const struct st_rip3_palette *palette);
/* Redraw a resident button with its captured PORT0 geometry even after later
 * viewport movement/deactivation. Restore live PORT0/current selection on all
 * returns. Callback may draw/read assets, but must not mutate port definitions,
 * selection, cursor or viewport state, or reenter input processing. */
int st_rip3_ports_button_view(struct st_rip3_ports *ports, const struct st_rip3_port_info *view,
	int (*draw)(void *context), void *context);
/* Temporarily intersect the current view with an absolute DEVICE rectangle.
 * Draw relative to the supplied clipped origin. The callback must not change
 * port state or reenter the parser. An empty/inactive view does no drawing. */
int st_rip3_ports_draw_clipped(struct st_rip3_ports *, const struct st_rip3_rect *area,
	int (*draw)(void *, struct st_rip3_point origin), void *context);
/* Outer-reset physical RGB-black clear, independent of PAL/PORT/STYLE. */
int st_rip3_ports_screen_clear_black(struct st_rip3_ports *ports);

/* Modes 1/3 own floating/screen-entry expressions. Floating ignores number;
 * numbered targets must be defined, unprotected screen ports. Storage has no
 * execution or selection effects. Returned views are borrowed until mutation. */
struct st_rip3_resident_query;
int st_rip3_ports_define_query(struct st_rip3_ports *,bool floating,unsigned number,
	const uint8_t *,size_t,bool converted);
const struct st_rip3_resident_query *st_rip3_ports_query(const struct st_rip3_ports *,bool floating,unsigned number);
/* Backup the device port definitions/current selection and owned offscreen
 * pixels. Onscreen pixels belong to the separate screen backup area. Restore
 * prepares all bitmap/query copies and the clipboard selection before replacing
 * any live entry. Saved defined entries replace protected live targets, but
 * protected live ports absent from the snapshot survive. Restore requires the
 * same device canvas. The backup survives the original ports object. Image
 * styles are included. Restore retains each live
 * cursor instead of the saved screen cursor; offscreen cursors are not saved.
 * Floating and numbered query sources are deep-owned. A bound clipboard
 * selection is copied by value; restoring an assigned selection requires a
 * bound target. Complete scene orchestration remains separate. */
int st_rip3_ports_backup_create(struct st_rip3_backup **result);

#endif
