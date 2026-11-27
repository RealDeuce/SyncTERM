/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_resource.h"
#include "st_ddofx_script.h"
#include "st_time.h"
#include "string_util.h"

enum {
	POINTERS = 100,
	FRAME_MS = 110
};

struct pointer {
	struct st_console_indexed_pointer image;
	int                               hot_x, hot_y;
	bool                              loaded;
};

struct profile {
	int first, last, frame, direction, loop;
};

struct st_ddofx_pointers {
	struct pointer slots[POINTERS], current;
	struct profile profiles[POINTERS];
	unsigned       animation;
	uint64_t       due;
};

static int
word(const uint8_t *bytes)
{
	unsigned u = bytes[0] | ((unsigned)bytes[1] << 8);
	return u < 32768 ? (int)u : (int)u - 65536;
}

static bool
load(const struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *name, struct pointer *out)
{
	if (!*ddo->resource_prefix || !sink->open_asset)
		return false;
	char container[sizeof(ddo->resource_prefix) + 1];
	snprintf(container, sizeof(container), "%sM", ddo->resource_prefix);
	FILE *file = sink->open_asset(sink->context, "BBSTHEME", NULL, container);
	if (!file)
		return false;
	uint32_t n;
	uint8_t  raw[516];
	bool     ok = st_ddofx_resource_find(file, name, -1, &n, NULL) >= 0 && (n == 68 || n == sizeof(raw))
	              && fread(raw, 1, n, file) == n;
	fclose(file);
	if (!ok)
		return false;
	out->hot_x = word(raw);
	out->hot_y = word(raw + 2);
	if (n == 516) {
		memcpy(out->image.and_mask, raw + 4, 256);
		memcpy(out->image.xor_mask, raw + 260, 256);
	}
	else {
		for (unsigned y = 0; y < 16; ++y) {
			unsigned keep = (unsigned)word(raw + 4 + y * 2), toggle = (unsigned)word(raw + 36 + y * 2);
			for (unsigned x = 0; x < 16; ++x) {
				unsigned bit              = 0x8000u >> x;
				out->image.and_mask[y][x] = keep & bit ? 255 : 0;
				out->image.xor_mask[y][x] = toggle & bit ? 15 : 0;
			}
		}
	}
	out->loaded = true;
	return true;
}

static void
default_arrow(struct pointer *p)
{
	/* White VGA arrow with a black outline and transparent surroundings. */
	*p = (struct pointer) {.loaded = true};
	memset(p->image.and_mask, 255, sizeof(p->image.and_mask));
	for (unsigned y = 0; y < 14; ++y) {
		for (unsigned x = 0; x <= y / 2; ++x) {
			p->image.and_mask[y][x] = 0;
			p->image.xor_mask[y][x] = (x && x < y / 2 && y < 13) ? 15 : 0;
		}
	}
}

/* A software arrow can be positioned independently of the window pointer. */
bool
st_ddofx_ensure_pointer(struct st_ddofx_mouse_state *m)
{
	if (!m->pointers)
		m->pointers = calloc(1, sizeof(*m->pointers));
	if (!m->pointers)
		return false;
	if (!m->pointers->current.loaded)
		default_arrow(&m->pointers->current);
	return true;
}

void
st_ddofx_pointer_refresh(struct st_ddofx *ddo)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	if (!g)
		return;
	struct st_ddofx_mouse_state *m = &g->mouse;
	const struct pointer        *p = m->pointers ? &m->pointers->current : NULL;
	if ((!p || !p->loaded) && !m->visibility_changed)
		return;
	if (!m->visibility_changed) {
		if (st_console_get_pointer_visible(g->display, &m->previous_visibility) != 0)
			return;
		m->visibility_changed = true;
	}
	bool custom = p && p->loaded && !m->outside && !ddo->pointer_paused && m->x >= 0 && m->y >= 0
	              && m->x < WIDTH && m->y < HEIGHT;
	bool host   = ddo->pointer_paused ? m->previous_visibility : !custom && !m->hidden;
	bool host_visible;
	if (st_console_get_pointer_visible(g->display, &host_visible) == 0 && host_visible != host)
		(void)st_console_set_pointer_visible(g->display, host);
	if (custom && !m->hidden)
		(void)st_console_set_indexed_pointer(g->display, m->x - p->hot_x, m->y - p->hot_y, &p->image);
	else
		(void)st_console_set_indexed_pointer(g->display, 0, 0, NULL);
}

void
st_ddofx_pointer_pause(struct st_ddofx *ddo, bool pause)
{
	if (ddo->pointer_paused != pause) {
		ddo->pointer_paused = pause;
		if (pause && ddo->graphics)
			ddo->graphics->mouse.host_known = false;
		st_ddofx_pointer_refresh(ddo);
	}
}

static void
select_slot(struct st_ddofx *ddo, unsigned slot)
{
	struct st_ddofx_pointers *p = ddo->graphics->mouse.pointers;
	if (slot && slot <= POINTERS && p->slots[slot - 1].loaded) {
		p->current = p->slots[slot - 1];
		st_ddofx_pointer_refresh(ddo);
	}
}

void
st_ddofx_field_pointer(struct st_ddofx *ddo, unsigned slot, bool animated)
{
	struct st_ddofx_pointers *p = ddo->graphics->mouse.pointers;
	if (!p || !slot || slot > POINTERS)
		return;
	if (!animated) {
		p->animation = 0;
		select_slot(ddo, slot);
	}
	else if (p->profiles[slot - 1].loop) {
		p->animation = slot;
		p->due       = 0;
	}
}

void
st_ddofx_pointer_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *verb, char *args)
{
	struct st_ddofx_mouse_state *m = &ddo->graphics->mouse;
	if (!m->pointers
	    && (!ascii_case_compare(verb, "MPL") || !ascii_case_compare(verb, "MPT")
	        || !ascii_case_compare(verb, "AMP")))
		m->pointers = calloc(1, sizeof(*m->pointers));
	struct st_ddofx_pointers *p = m->pointers;
	if (!p)
		return;
	if (!ascii_case_compare(verb, "DAM") && !args) {
		p->animation = 0;
		return;
	}
	if (!args)
		return;
	if (!ascii_case_compare(verb, "MPT")) {
		if (!ascii_case_compare(args, "DEFAULT")) {
			default_arrow(&p->current);
			st_ddofx_mouse_defaults(m);
		}
		else if (!load(ddo, sink, args, &p->current))
			return;
		st_ddofx_pointer_refresh(ddo);
		return;
	}
	int32_t slot;
	if (!st_ddofx_field_number(ddo, &args, &slot) || slot < 0 || slot > POINTERS)
		return;
	if (!ascii_case_compare(verb, "MPL")) {
		if (!slot)
			memset(p->slots, 0, sizeof(p->slots));
		else if (args)
			p->slots[slot - 1].loaded = load(ddo, sink, args, &p->slots[slot - 1]);
	}
	else if (!ascii_case_compare(verb, "SMP") && !args)
		select_slot(ddo, (unsigned)slot);
	else if (!ascii_case_compare(verb, "AMP")) {
		if (!slot) {
			memset(p->profiles, 0, sizeof(p->profiles));
			p->animation = 0;
			return;
		}
		int32_t v[3];
		if (!st_ddofx_numbers(ddo, args, v, 3) || v[0] < 1 || v[1] < v[0] || v[1] > POINTERS || v[2] < 1
		    || v[2] > 3)
			return;
		p->profiles[slot - 1] =
		    (struct profile) {.first = v[0], .last = v[1], .frame = v[0], .direction = 1, .loop = v[2]};
		if (p->animation == (unsigned)slot)
			p->animation = 0;
	}
	else if (!ascii_case_compare(verb, "AAM") && !args)
		st_ddofx_field_pointer(ddo, (unsigned)slot, true);
}

uint32_t
st_ddofx_pointer_wait_ms(const struct st_ddofx *ddo)
{
	if (!ddo->graphics || !ddo->graphics->mouse.pointers || ddo->background_paused || ddo->pointer_paused)
		return UINT32_MAX;
	const struct st_ddofx_pointers *p = ddo->graphics->mouse.pointers;
	uint64_t                        now;
	if (!p->animation || st_time_monotonic_ms(&now) != 0)
		return UINT32_MAX;
	return p->due > now ? (uint32_t)(p->due - now) : 0;
}

void
st_ddofx_pointer_poll(struct st_ddofx *ddo)
{
	if (st_ddofx_pointer_wait_ms(ddo))
		return;
	struct st_ddofx_pointers *p    = ddo->graphics->mouse.pointers;
	struct profile           *anim = &p->profiles[p->animation - 1];
	uint64_t                  now;
	if (st_time_monotonic_ms(&now) != 0)
		return;
	p->due = now + FRAME_MS;
	select_slot(ddo, (unsigned)anim->frame);
	anim->frame += anim->direction;
	if (anim->frame < anim->first || anim->frame > anim->last) {
		if (anim->loop == 1) {
			anim->frame  = anim->first;
			p->animation = 0;
		}
		else if (anim->loop == 2)
			anim->frame = anim->first;
		else {
			anim->direction = -anim->direction;
			anim->frame += anim->direction;
		}
	}
}
