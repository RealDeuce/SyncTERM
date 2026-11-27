/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_script.h"
#include "st_time.h"
#include "string_util.h"

enum {
	REGION_COUNT = 300,
	REPEAT_MS    = 50
};

struct region {
	struct bounds            area;
	unsigned                 buttons;
	bool                     on_release;
	struct st_ddofx_callback callback;
};

struct st_ddofx_regions {
	struct region slots[REGION_COUNT];
	unsigned      count, next, waiting;
	int           x, y;
	unsigned      buttons;
	uint64_t      repeat_due;
	bool          repeating;
};

static unsigned
button_mask(uint32_t buttons)
{
	return ((buttons & ST_CONSOLE_MOUSE_BUTTON_LEFT) ? 1u : 0u)
	       | ((buttons & ST_CONSOLE_MOUSE_BUTTON_RIGHT) ? 2u : 0u)
	       | ((buttons & ST_CONSOLE_MOUSE_BUTTON_MIDDLE) ? 4u : 0u);
}

static void
define_region(struct st_ddofx *ddo, char *args, bool on_release)
{
	struct st_ddofx_mouse_state *mouse = &ddo->graphics->mouse;
	int32_t                      slot, x1, x2, y1, y2, buttons;
	if (!st_ddofx_field_number(ddo, &args, &slot))
		return;
	if (!slot) {
		free(mouse->regions);
		mouse->regions = NULL;
		return;
	}
	if (slot < 1 || slot > REGION_COUNT || !st_ddofx_field_number(ddo, &args, &x1)
	    || !st_ddofx_field_number(ddo, &args, &x2) || !st_ddofx_field_number(ddo, &args, &y1)
	    || !st_ddofx_field_number(ddo, &args, &y2) || !st_ddofx_field_number(ddo, &args, &buttons) || x1 < 0
	    || x1 >= x2 || x2 >= WIDTH || y1 < 0 || y1 >= y2 || y2 >= HEIGHT || buttons < 1 || buttons > 7)
		return;
	char *reply = st_ddofx_field(&args);
	if (!reply || !args)
		return;
	if (!mouse->regions)
		mouse->regions = calloc(1, sizeof(*mouse->regions));
	if (!mouse->regions)
		return;
	struct region *r = &mouse->regions->slots[slot - 1];
	r->area          = (struct bounds) {x1, x2, y1, y2};
	r->buttons       = (unsigned)buttons;
	r->on_release    = on_release;
	st_ddofx_callback_define(ddo, &r->callback, reply, args);
	if ((unsigned)slot > mouse->regions->count)
		mouse->regions->count = (unsigned)slot;
	if (mouse->buttons && !mouse->ignore_hold && !mouse->regions->waiting && !ddo->in_mouse)
		mouse->regions->repeat_due = 1;
}

void
st_ddofx_mouse_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *args)
{
	struct st_ddofx_mouse_state *mouse = &ddo->graphics->mouse;
	char                        *verb  = st_ddofx_field(&args);
	char                         result[64];
	if (!ascii_case_compare(verb, "MACT") && !args)
		st_ddofx_query_reply(ddo, sink, sink->mouse_disabled ? "N" : "Y", false);
	else if (!ascii_case_compare(verb, "MVIS")) {
		int32_t visible;
		if (!args || !st_ddofx_number(ddo, args, &visible) || (visible != 0 && visible != 1))
			return;
		if (!mouse->visibility_changed
		    && st_console_get_pointer_visible(ddo->graphics->display, &mouse->previous_visibility) != 0)
			return;
		mouse->visibility_changed = true;
		(void)st_console_set_pointer_visible(ddo->graphics->display, visible != 0);
	}
	else if (!ascii_case_compare(verb, "MINF") && !args) {
		snprintf(result, sizeof(result), "%d,%d,%u", mouse->x, mouse->y, mouse->buttons);
		st_ddofx_query_reply(ddo, sink, result, false);
	}
	else if (!ascii_case_compare(verb, "MIOLD")) {
		int32_t button;
		if (!args || !st_ddofx_number(ddo, args, &button) || !button || button < -3 || button > 3)
			return;
		unsigned i = (unsigned)abs((int)button) - 1, release = button < 0;
		snprintf(result, sizeof(result), "%u,%d,%d", mouse->history[i][release].count,
		    mouse->history[i][release].x, mouse->history[i][release].y);
		mouse->history[i][release].count = 0;
		st_ddofx_query_reply(ddo, sink, result, false);
	}
	else if (!ascii_case_compare(verb, "MREG") || !ascii_case_compare(verb, "MREGN"))
		define_region(ddo, args, !ascii_case_compare(verb, "MREGN"));
}

void
st_ddofx_resume_mouse(struct st_ddofx *ddo)
{
	ddo->in_mouse = false;
	if (!ddo->graphics || !ddo->graphics->mouse.regions)
		return;
	struct st_ddofx_mouse_state *mouse = &ddo->graphics->mouse;
	struct st_ddofx_regions     *table = mouse->regions;
	/* A macro can redefine a later slot. Keep the initiating sample, but
	 * read the region when its turn comes, after the previous macro. */
	while (table->buttons && table->next < table->count) {
		struct region *r = &table->slots[table->next++];
		if (r->buttons != table->buttons || table->x < r->area.left || table->x > r->area.right
		    || table->y < r->area.top || table->y > r->area.bottom)
			continue;
		if (r->on_release && mouse->buttons) {
			table->waiting = table->next;
			return;
		}
		if (!r->on_release)
			table->repeating = true;
		if (st_ddofx_callback_start(ddo, &r->callback, DDO_MOUSE)) {
			ddo->in_mouse = true;
			return;
		}
	}
	uint64_t now;
	if (table->repeating && mouse->buttons && st_time_monotonic_ms(&now) == 0)
		table->repeat_due = now + REPEAT_MS;
}

static void
scan(struct st_ddofx *ddo)
{
	struct st_ddofx_mouse_state *mouse = &ddo->graphics->mouse;
	struct st_ddofx_regions     *table = mouse->regions;
	table->x                           = mouse->x;
	table->y                           = mouse->y;
	table->buttons                     = mouse->buttons;
	table->next                        = 0;
	table->repeating                   = false;
	table->repeat_due                  = 0;
	st_ddofx_resume_mouse(ddo);
}

void
st_ddofx_mouse(struct st_ddofx *ddo, const struct st_console_event *event)
{
	struct st_ddofx_mouse_state *mouse   = &ddo->graphics->mouse;
	unsigned                     buttons = button_mask(event->data.mouse.buttons);
	unsigned                     changed = mouse->buttons ^ buttons;
	int                          x = event->data.mouse.pixel_x, y = event->data.mouse.pixel_y;
	bool                         moved = mouse->x != x || mouse->y != y;
	mouse->x                           = x;
	mouse->y                           = y;
	mouse->buttons                     = buttons;
	if (mouse->ignore_hold) {
		mouse->ignore_hold = buttons != 0;
		return;
	}
	for (unsigned i = 0; i < 3; ++i) {
		unsigned bit = 1u << i;
		if (!(changed & bit))
			continue;
		unsigned release = !(buttons & bit);
		if (mouse->history[i][release].count < UINT_MAX)
			++mouse->history[i][release].count;
		mouse->history[i][release].x = x;
		mouse->history[i][release].y = y;
	}
	struct st_ddofx_regions *table = mouse->regions;
	if (!table || (!moved && !changed))
		return;
	if (table->waiting) {
		if (buttons)
			return;
		table->next    = table->waiting - 1;
		table->waiting = 0;
		st_ddofx_resume_mouse(ddo);
	}
	else
		scan(ddo);
}

void
st_ddofx_mouse_cancel(struct st_ddofx *ddo, uint32_t buttons)
{
	if (!ddo->graphics)
		return;
	struct st_ddofx_mouse_state *mouse = &ddo->graphics->mouse;
	mouse->buttons                     = button_mask(buttons);
	mouse->ignore_hold                 = mouse->buttons != 0;
	if (mouse->regions) {
		mouse->regions->repeat_due = 0;
		mouse->regions->buttons    = 0;
		mouse->regions->waiting    = 0;
		mouse->regions->repeating  = false;
	}
}

uint32_t
st_ddofx_mouse_wait_ms(const struct st_ddofx *ddo)
{
	if (!ddo->graphics || !ddo->graphics->mouse.regions || ddo->in_key || ddo->in_event || ddo->fade_due)
		return UINT32_MAX;
	uint64_t due = ddo->graphics->mouse.regions->repeat_due, now;
	if (!due || st_time_monotonic_ms(&now) != 0)
		return UINT32_MAX;
	return now < due ? (uint32_t)(due - now) : 0;
}

void
st_ddofx_mouse_poll(struct st_ddofx *ddo)
{
	if (st_ddofx_mouse_wait_ms(ddo) == 0)
		scan(ddo);
}
