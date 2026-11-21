/* Session-owned, shared mouse-region list. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_MOUSE_H_
#define ST_RIP3_MOUSE_H_

#include "st_rip3_surface.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_mouse;
enum st_rip3_mouse_kind { ST_RIP3_MOUSE_SIMPLE, ST_RIP3_MOUSE_BUTTON };
struct st_rip3_mouse_binding {uint8_t hotkey;bool default_enter;};
struct st_rip3_mouse_visual {
	void *context;
	/* Synchronous redraw; may not mutate/reenter the mouse owner. */
	int (*select)(void *context, bool selected);
	void (*destroy)(void *context);
};
/* Scoped callback value; an inactive context has no numeric fallback. */
struct st_rip3_field_context {bool active;int identity;};
/* Parse one definition-time prefix. Failure leaves both payload views intact
 * and returns -1, which differs from valid identity zero. */
int st_rip3_mouse_identity(const uint8_t **program, size_t *length);
struct st_rip3_mouse_sink {
	void *context;
	void (*invert)(void *context, const struct st_rip3_rect *area);
	/* The program is an independent copy: this callback may delete fields. */
	void (*activate)(void *context, const uint8_t *program, size_t length, bool reset, int identity,
	    bool pointer, int32_t x, int32_t y);
	void (*resource_failure)(void *context);
	void (*render_failure)(void *context);
	void (*changed)(void *context);
};
int st_rip3_mouse_create(struct st_rip3_mouse **result, const struct st_rip3_mouse_sink *sink);
void st_rip3_mouse_destroy(struct st_rip3_mouse *mouse);
/* Half-open, clipped absolute DEVICE bounds. Simple fields and button
 * records share this owner's 128 entries. No drawing buffers or damage map. */
int st_rip3_mouse_define(struct st_rip3_mouse *mouse, enum st_rip3_mouse_kind kind,
	const struct st_rip3_mouse_binding *binding, const struct st_rip3_rect *area,
	bool invert, bool reset, int identity, const uint8_t *program, size_t length,
	const struct st_rip3_mouse_visual *visual);
/* A supplied visual transfers ownership only on successful definition.
 * Its destructor runs after capture cancellation, on deletion/reset/shutdown. */
struct st_rip3_mouse_paint {
	void *context;
	/* Borrowed, synchronous half-open absolute DEVICE rectangle. All selected
	 * instances have already been deleted. Failures do not stop later paints.
	 * The callback must not reenter the mouse owner. */
	int (*apply)(void *context, const struct st_rip3_rect *area, bool pattern);
};
/* Bits 1/2/4 union disjoint contained/crossing/outside classes; 8/16 exclude
 * simple/button fields. Preserve surviving capture across compaction. Delete
 * the whole selection before optional newest-first paint (64 wins 32);
 * 128 paints the selector once, regardless of matches/type exclusions.
 * No region bits or an empty selector is a complete no-op. Unsupported bits
 * fail before mutation. Paint bindings must already be snapshotted. */
int st_rip3_mouse_remove(struct st_rip3_mouse *mouse,
	const struct st_rip3_rect *area, unsigned flags, const struct st_rip3_mouse_paint *paint);
size_t st_rip3_mouse_count(const struct st_rip3_mouse *mouse);
void st_rip3_mouse_kill(struct st_rip3_mouse *mouse);
void st_rip3_mouse_cancel(struct st_rip3_mouse *mouse);
bool st_rip3_mouse_active(const struct st_rip3_mouse *mouse);
/* Capture on left press, invert while inside, commit on release inside and
 * cancel release outside. Captured instances cannot be stolen by appends. */
bool st_rip3_mouse_event(struct st_rip3_mouse *mouse, int32_t x, int32_t y, bool left);
/* One translated CP437 byte per character/down/repeat, never raw physical or
 * noncharacter events. A winning empty/failed action still consumes the key.
 * Cancel pointer capture without altering the sampled physical button state. */
bool st_rip3_mouse_key(struct st_rip3_mouse *mouse, uint8_t byte);
void st_rip3_mouse_hotkeys(struct st_rip3_mouse *mouse, bool enabled);

#endif
