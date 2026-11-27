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
	unsigned                 buttons, window;
	bool                     on_release;
	struct st_ddofx_callback callback;
};

struct st_ddofx_regions {
	struct region slots[REGION_COUNT];
	unsigned      count;
};

struct field {
	struct bounds            area;
	unsigned                 pointer, window;
	bool                     defined, animated;
	struct st_ddofx_callback callback;
};

struct st_ddofx_fields {
	struct field slots[REGION_COUNT];
	unsigned     last, running;
	bool         dirty;
};

void
st_ddofx_mouse_defaults(struct st_ddofx_mouse_state *m)
{
	m->limits     = (struct bounds) {0, WIDTH - 1, 0, HEIGHT - 1};
	m->rate_x     = 8;
	m->rate_y     = 16;
	m->fraction_x = 0;
	m->fraction_y = 0;
}

static int
clamp(int value, int low, int high)
{
	return value < low ? low : value > high ? high : value;
}

static void
move_to(struct st_ddofx *ddo, int x, int y)
{
	struct st_ddofx_mouse_state *m = &ddo->graphics->mouse;
	x                              = clamp(x, m->limits.left, m->limits.right);
	y                              = clamp(y, m->limits.top, m->limits.bottom);
	bool moved                     = x != m->x || y != m->y;
	m->x                           = x;
	m->y                           = y;
	m->fraction_x                  = 0;
	m->fraction_y                  = 0;
	m->controlled                  = true;
	if (moved && m->fields)
		m->fields->dirty = true;
	if (moved && (m->regions || st_ddofx_control_count(ddo)) && m->buttons && !m->scan.waiting
	    && !m->ignore_hold)
		m->scan.repeat_due = 1;
	st_ddofx_pointer_refresh(ddo);
}

static bool
read_area(struct st_ddofx *ddo, char **args, struct bounds *area, unsigned slot)
{
	const struct st_ddofx_graphic_window *w = &ddo->graphics->graphic_windows[slot];
	int width = slot ? w->width : WIDTH, height = slot ? w->height : HEIGHT;
	int32_t x1, x2, y1, y2;
	if (!st_ddofx_field_number(ddo, args, &x1) || !st_ddofx_field_number(ddo, args, &x2)
	    || !st_ddofx_field_number(ddo, args, &y1) || !st_ddofx_field_number(ddo, args, &y2) || x1 < 0
	    || x1 >= x2 || x2 >= width || y1 < 0 || y1 >= y2 || y2 >= height)
		return false;
	*area = (struct bounds) {x1, x2, y1, y2};
	return true;
}

static bool
inside(const struct bounds *area, int x, int y)
{
	return x >= area->left && x <= area->right && y >= area->top && y <= area->bottom;
}

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
	int32_t                      slot, buttons;
	struct bounds                area;
	if (!st_ddofx_field_number(ddo, &args, &slot))
		return;
	if (!slot) {
		free(mouse->regions);
		mouse->regions = NULL;
		return;
	}
	if (slot < 1 || slot > REGION_COUNT || !read_area(ddo, &args, &area, ddo->graphics->alert_graphic_window)
	    || !st_ddofx_field_number(ddo, &args, &buttons) || buttons < 1 || buttons > 7)
		return;
	char *reply = st_ddofx_field(&args);
	if (!reply || !args)
		return;
	if (!mouse->regions)
		mouse->regions = calloc(1, sizeof(*mouse->regions));
	if (!mouse->regions)
		return;
	struct region *r = &mouse->regions->slots[slot - 1];
	r->window        = ddo->graphics->alert_graphic_window;
	r->area          = area;
	r->buttons       = (unsigned)buttons;
	r->on_release    = on_release;
	st_ddofx_callback_define(ddo, &r->callback, reply, args, 100);
	if ((unsigned)slot > mouse->regions->count)
		mouse->regions->count = (unsigned)slot;
	if (mouse->buttons && !mouse->ignore_hold && !mouse->scan.waiting && !ddo->in_mouse)
		mouse->scan.repeat_due = 1;
}

static void
define_field(struct st_ddofx *ddo, char *args)
{
	struct st_ddofx_mouse_state *m = &ddo->graphics->mouse;
	int32_t                      slot, pointer, animated;
	if (!st_ddofx_field_number(ddo, &args, &slot))
		return;
	if (!slot) {
		free(m->fields);
		m->fields = NULL;
		return;
	}
	struct field f = {.defined = true, .window = ddo->graphics->alert_graphic_window};
	if (slot < 1 || slot > REGION_COUNT || !read_area(ddo, &args, &f.area, f.window)
	    || !st_ddofx_field_number(ddo, &args, &pointer) || pointer < 0 || pointer > 100
	    || !st_ddofx_field_number(ddo, &args, &animated) || (animated != 0 && animated != 1))
		return;
	char *reply = st_ddofx_field(&args);
	if (!reply || !args)
		return;
	if (!m->fields)
		m->fields = calloc(1, sizeof(*m->fields));
	if (!m->fields)
		return;
	f.pointer  = (unsigned)pointer;
	f.animated = animated != 0;
	st_ddofx_callback_define(ddo, &f.callback, reply, args, 100);
	m->fields->slots[slot - 1] = f;
	m->fields->dirty           = true;
}

void
st_ddofx_mouse_view_changed(struct st_ddofx_graphics *g)
{
	if (g->mouse.fields)
		g->mouse.fields->dirty = true;
}

static bool
field_ready(const struct st_ddofx *ddo)
{
	if (!ddo->graphics || ddo->pointer_paused || ddo->in_event || ddo->in_mouse || ddo->fade_due)
		return false;
	const struct st_ddofx_mouse_state *m = &ddo->graphics->mouse;
	return m->fields && m->fields->dirty && !m->outside && !m->ignore_hold && !m->scan.waiting;
}

static void
hover(struct st_ddofx *ddo)
{
	if (!field_ready(ddo))
		return;
	struct st_ddofx_mouse_state *m      = &ddo->graphics->mouse;
	struct st_ddofx_fields      *fields = m->fields;
	fields->dirty                       = false;
	int x = m->x, y = m->y;
	int owner = st_ddofx_window_point(ddo->graphics, &x, &y);
	if (owner < 0 || ddo->graphics->frame_drag.slot)
		return;
	for (unsigned i = REGION_COUNT; i; --i) {
		const struct field *f = &fields->slots[i - 1];
		if (!f->defined || f->window != (unsigned)owner || !st_ddofx_callback_enabled(ddo, &f->callback)
		    || !inside(&f->area, x, y))
			continue;
		if (fields->last != i && st_ddofx_callback_start(ddo, &f->callback, DDO_FIELD)) {
			st_ddofx_sleep_arm(ddo);
			fields->last    = i;
			fields->running = i;
			ddo->in_mouse   = true;
		}
		break;
	}
	/* Leaving every field retains the last field ID until another wins. */
}

void
st_ddofx_finish_field(struct st_ddofx *ddo)
{
	ddo->in_mouse = false;
	if (!ddo->graphics || !ddo->graphics->mouse.fields)
		return;
	struct st_ddofx_fields *fields = ddo->graphics->mouse.fields;
	if (fields->running) {
		const struct field *f = &fields->slots[fields->running - 1];
		fields->running       = 0;
		if (f->defined)
			st_ddofx_field_pointer(ddo, f->pointer, f->animated);
	}
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
		mouse->hidden             = !visible;
		st_ddofx_pointer_refresh(ddo);
	}
	else if (!ascii_case_compare(verb, "MMOV")) {
		int32_t v[2];
		if (st_ddofx_numbers(ddo, args, v, 2) && v[0] >= 0 && v[0] < WIDTH && v[1] >= 0 && v[1] < HEIGHT
		    && st_ddofx_ensure_pointer(mouse)) {
			mouse->keep_position = !mouse->host_known;
			move_to(ddo, v[0], v[1]);
		}
	}
	else if (!ascii_case_compare(verb, "MLIM")) {
		struct bounds limits;
		if (read_area(ddo, &args, &limits, 0) && !args && st_ddofx_ensure_pointer(mouse)) {
			mouse->limits = limits;
			move_to(ddo, mouse->x, mouse->y);
		}
	}
	else if (!ascii_case_compare(verb, "MSPD")) {
		int32_t v[2];
		if (st_ddofx_numbers(ddo, args, v, 2) && v[0] > 0 && v[1] > 0 && st_ddofx_ensure_pointer(mouse)) {
			mouse->rate_x = v[0];
			mouse->rate_y = v[1];
			move_to(ddo, mouse->x, mouse->y);
		}
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
	else if (!ascii_case_compare(verb, "MFLD"))
		define_field(ddo, args);
	else if (!ascii_case_compare(verb, "MFSWP") && mouse->fields) {
		int32_t slots[2];
		if (st_ddofx_numbers(ddo, args, slots, 2) && slots[0] >= 1 && slots[0] <= REGION_COUNT
		    && slots[1] >= 1 && slots[1] <= REGION_COUNT && slots[0] != slots[1]) {
			struct field *a      = &mouse->fields->slots[slots[0] - 1],
				     *b      = &mouse->fields->slots[slots[1] - 1];
			struct field tmp     = *a;
			*a                   = *b;
			*b                   = tmp;
			mouse->fields->dirty = true;
		}
	}
	else if (!ascii_case_compare(verb, "MFFRS") && !args && mouse->fields) {
		mouse->fields->last  = 0;
		mouse->fields->dirty = true;
	}
	else
		st_ddofx_pointer_command(ddo, sink, verb, args);
}

void
st_ddofx_resume_mouse(struct st_ddofx *ddo)
{
	ddo->in_mouse = false;
	if (!ddo->graphics)
		return;
	struct st_ddofx_mouse_state *mouse = &ddo->graphics->mouse;
	struct st_ddofx_mouse_scan  *scan  = &mouse->scan;
	if (scan->waiting && mouse->buttons)
		return;
	scan->waiting = false;
	/* A callback may edit later slots. Preserve the initiating sample
	 * across waits and read each control when its turn comes. */
	int x = scan->x, y = scan->y;
	int owner = st_ddofx_window_point(ddo->graphics, &x, &y);
	unsigned regions = mouse->regions ? mouse->regions->count : 0;
	while (scan->buttons && (scan->next < regions || scan->next < st_ddofx_control_count(ddo))) {
		unsigned slot = scan->next;
		while (scan->stage < 2) {
			enum st_ddofx_group_kind kind = scan->stage++ == 0 ? DDO_RADIO : DDO_CHECKBOX;
			if (scan->buttons == 1 && st_ddofx_choice_mouse(ddo, kind, slot, owner, x, y)
			    && mouse->buttons) {
				scan->waiting = true;
				return;
			}
		}
		if (scan->stage == 2) {
			++scan->stage;
			if (st_ddofx_button_mouse(ddo, slot, owner, x, y, scan->buttons)) {
				scan->waiting = mouse->buttons != 0;
				ddo->in_mouse = true;
				return;
			}
		}
		struct region *r   = slot < regions ? &mouse->regions->slots[slot] : NULL;
		bool hit = r && owner >= 0 && r->window == (unsigned)owner && r->buttons == scan->buttons
		    && st_ddofx_callback_enabled(ddo, &r->callback) && inside(&r->area, x, y);
		if (hit && r->on_release && mouse->buttons) {
			scan->waiting = true;
			return;
		}
		++scan->next;
		scan->stage = 0;
		if (!hit)
			continue;
		if (!r->on_release)
			scan->repeating = true;
		if (st_ddofx_callback_start(ddo, &r->callback, DDO_MOUSE)) {
			ddo->in_mouse = true;
			return;
		}
	}
	uint64_t now;
	if (scan->repeating && mouse->buttons && st_time_monotonic_ms(&now) == 0)
		scan->repeat_due = now + REPEAT_MS;
	hover(ddo);
}

static void
scan(struct st_ddofx *ddo)
{
	struct st_ddofx_mouse_state *mouse = &ddo->graphics->mouse;
	mouse->scan                        = (struct st_ddofx_mouse_scan) {.x = mouse->x,
	    .y                                         = mouse->y,
	    .buttons                                   = mouse->outside ? 0 : mouse->buttons};
	st_ddofx_resume_mouse(ddo);
}

/* Console samples are absolute raster pixels. Integrate their differences
 * at the requested mickey rate. Carry the remainder between small moves;
 * discard it against a stop so reversing direction responds immediately. */
static int
advance(int value, int delta, int baseline, int rate, int *fraction, int low, int high)
{
	int64_t units    = (int64_t)delta * baseline + *fraction;
	int64_t position = value + units / rate;
	*fraction        = (int)(units % rate);
	if (position < low || (position == low && *fraction < 0)) {
		*fraction = 0;
		return low;
	}
	if (position > high || (position == high && *fraction > 0)) {
		*fraction = 0;
		return high;
	}
	return (int)position;
}

static bool
sample_position(struct st_ddofx_mouse_state *m, int x, int y)
{
	bool outside = x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT;
	bool changed = m->outside != outside;
	m->outside   = outside;
	if (outside) {
		m->host_known = false;
		return changed;
	}

	int old_x = m->x, old_y = m->y;
	if (!m->controlled || (!m->host_known && !m->keep_position)) {
		m->x          = clamp(x, m->limits.left, m->limits.right);
		m->y          = clamp(y, m->limits.top, m->limits.bottom);
		m->fraction_x = 0;
		m->fraction_y = 0;
	}
	else if (m->host_known) {
		m->x = advance(m->x, x - m->host_x, 8, m->rate_x, &m->fraction_x, m->limits.left, m->limits.right);
		m->y =
		    advance(m->y, y - m->host_y, 16, m->rate_y, &m->fraction_y, m->limits.top, m->limits.bottom);
	}
	m->host_x        = x;
	m->host_y        = y;
	m->host_known    = true;
	m->keep_position = false;
	return changed || m->x != old_x || m->y != old_y;
}

void
st_ddofx_mouse(struct st_ddofx *ddo, const struct st_console_event *event)
{
	struct st_ddofx_mouse_state *mouse   = &ddo->graphics->mouse;
	unsigned                     buttons = button_mask(event->data.mouse.buttons);
	unsigned                     changed = mouse->buttons ^ buttons;
	if (ddo->sleeper.warning) {
		st_ddofx_mouse_cancel(ddo, event->data.mouse.buttons);
		return;
	}
	if (changed & buttons)
		st_ddofx_sleep_arm(ddo);
	bool moved     = sample_position(mouse, event->data.mouse.pixel_x, event->data.mouse.pixel_y);
	mouse->buttons = buttons;
	if (moved && mouse->pointers)
		st_ddofx_pointer_refresh(ddo);
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
		mouse->history[i][release].x = mouse->x;
		mouse->history[i][release].y = mouse->y;
	}
	if (st_ddofx_graphic_window_mouse(ddo, changed) || st_ddofx_icon_mouse(ddo, changed)) {
		mouse->scan.repeat_due = 0;
		return;
	}
	if ((changed & 1) && buttons == 1 && !mouse->outside) {
		int x = mouse->x, y = mouse->y;
		int owner = st_ddofx_window_point(ddo->graphics, &x, &y);
		st_ddofx_list_click(ddo, owner, x, y);
		if (st_ddofx_text_field_mouse(ddo, owner, x, y)) {
			mouse->ignore_hold = true;
			return;
		}
	}
	if (moved && mouse->fields)
		mouse->fields->dirty = true;
	if (!mouse->regions && !st_ddofx_control_count(ddo) && !mouse->scan.waiting) {
		hover(ddo);
		return;
	}
	if (!moved && !changed)
		return;
	if (mouse->scan.waiting)
		st_ddofx_resume_mouse(ddo);
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
	st_ddofx_icon_cancel(ddo->graphics);
	ddo->graphics->frame_drag = (struct st_ddofx_frame_drag){0};
	mouse->scan                        = (struct st_ddofx_mouse_scan) {0};
}

void
st_ddofx_background_download(struct st_ddofx *ddo, bool active)
{
	ddo->background_download = active;
	if (ddo->graphics) {
		struct st_ddofx_mouse_state *m = &ddo->graphics->mouse;
		m->ignore_hold = m->buttons != 0;
		st_ddofx_icon_cancel(ddo->graphics);
		ddo->graphics->frame_drag = (struct st_ddofx_frame_drag){0};
		m->scan = (struct st_ddofx_mouse_scan){0};
		if (m->fields)
			m->fields->dirty = true;
	}
}

static uint32_t
repeat_wait_ms(const struct st_ddofx *ddo)
{
	if (!ddo->graphics || ddo->in_key || ddo->in_event || ddo->fade_due)
		return UINT32_MAX;
	uint64_t due = ddo->graphics->mouse.scan.repeat_due, now;
	if (ddo->graphics->frame_drag.repeat_due)
		due = ddo->graphics->frame_drag.repeat_due;
	if (!due || st_time_monotonic_ms(&now) != 0)
		return UINT32_MAX;
	return now < due ? (uint32_t)(due - now) : 0;
}

uint32_t
st_ddofx_mouse_wait_ms(const struct st_ddofx *ddo)
{
	return field_ready(ddo) ? 0 : repeat_wait_ms(ddo);
}

void
st_ddofx_mouse_poll(struct st_ddofx *ddo)
{
	if (repeat_wait_ms(ddo) == 0) {
		if (ddo->graphics->frame_drag.repeat_due)
			st_ddofx_repeat_window_arrow(ddo);
		else
			scan(ddo);
	}
	hover(ddo);
}
