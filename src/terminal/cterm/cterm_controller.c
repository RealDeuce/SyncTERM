/* Connected controller state after Wren and graphics input filtering. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <inttypes.h>
#include <stdlib.h>

#include "cterm.h"
#include "cterm_internal.h"

struct control {
	unsigned type, code;
	int      minimum, maximum, value;
	bool     known, changed;
};

struct st_cterm_pad {
	struct st_cterm_pad *next;
	uint32_t             local, wire;
	unsigned             device_class, source;
	struct control      *controls;
	size_t               count;
	bool                 complete;
};

static unsigned
parameter(unsigned type, int value)
{
	if (type == 1)
		return (unsigned)value;
	return (unsigned)(value < 0 ? -2 * (int64_t)value - 1 : 2 * (int64_t)value);
}

static void
values(st_cterm_t *t, struct st_cterm_pad *p, bool initial)
{
	bool frame = initial;
	for (size_t i = 0; i < p->count; i++) {
		struct control *c = p->controls + i;
		if (c->known && (initial || c->changed)) {
			cterm_respond_printf(t, "\x1b[=3;%" PRIu32 ";%u;%u;%uJ", p->wire, c->type, c->code,
			    parameter(c->type, c->value));
			frame = true;
		}
		c->changed = false;
	}
	if (frame)
		cterm_respond_printf(t, "\x1b[=4;%" PRIu32 "J", p->wire);
}

static void
announce(st_cterm_t *t, struct st_cterm_pad *p)
{
	if (!p->complete || !t->pads->next_id)
		return;
	p->wire = t->pads->next_id++;
	cterm_respond_printf(t, "\x1b[=1;%" PRIu32 ";%u;%uJ", p->wire, p->device_class, p->source);
	for (size_t i = 0; i < p->count; i++) {
		struct control *c = p->controls + i;
		cterm_respond_printf(t, "\x1b[=2;%" PRIu32 ";%u;%u;%u;%uJ", p->wire, c->type, c->code,
		    parameter(c->type, c->minimum), parameter(c->type, c->maximum));
	}
	values(t, p, true);
}

bool
cterm_pads_available(const st_cterm_t *t)
{
	return t->pads && (!t->pads->available || t->pads->available(t->pads->context));
}

void
cterm_pad_mode(st_cterm_t *t, bool enabled)
{
	struct st_cterm_pads *s = t->pads;
	if (!s || s->enabled == enabled)
		return;
	if (enabled) {
		if (!cterm_pads_available(t))
			return;
		if (!s->paused && s->sample)
			s->sample(s->context);
	}
	s->enabled = enabled;
	for (struct st_cterm_pad *p = s->devices; p; p = p->next) {
		p->wire = 0;
		if (enabled && !s->paused)
			announce(t, p);
	}
	if (enabled)
		cterm_respond(t, "\x1b[=6J", 5);
}

void
st_cterm_pad_pause(st_cterm_t *t, bool paused)
{
	struct st_cterm_pads *s = t ? t->pads : NULL;
	if (!s || s->paused == paused)
		return;
	s->paused = paused;
	for (struct st_cterm_pad *p = s->devices; p; p = p->next) {
		if (paused && p->wire) {
			cterm_respond_printf(t, "\x1b[=5;%" PRIu32 "J", p->wire);
			p->wire = 0;
		}
		else if (!paused && s->enabled)
			announce(t, p);
	}
}

void
st_cterm_pad_event(st_cterm_t *t, const struct st_controller_event *e, bool consumed)
{
	struct st_cterm_pads *s = t ? t->pads : NULL;
	if (!s)
		return;
	struct st_cterm_pad **link = &s->devices;
	while (*link && (*link)->local != e->device)
		link = &(*link)->next;
	struct st_cterm_pad *p = *link;
	if (e->record == ST_PAD_ADD) {
		if (p)
			return;
		*link = p = calloc(1, sizeof(*p));
		if (!p)
			return;
		p->local        = e->device;
		p->device_class = e->device_class;
		p->source       = e->source;
		return;
	}
	if (!p)
		return;
	switch (e->record) {
		case ST_PAD_CAP:
		{
			if (p->complete)
				return;
			struct control *controls = realloc(p->controls, (p->count + 1) * sizeof(*controls));
			if (!controls)
				return;
			p->controls          = controls;
			controls[p->count++] = (struct control) {.type = e->type,
			    .code                                      = e->code,
			    .minimum                                   = e->minimum,
			    .maximum                                   = e->maximum};
			break;
		}
		case ST_PAD_VALUE:
			for (size_t i = 0; i < p->count; i++) {
				struct control *c = p->controls + i;
				if (c->type != e->type || c->code != e->code)
					continue;
				/* A consumed release still neutralizes a control previously delivered
				 * as held. A consumed press cannot leak into the next inventory. */
				if (consumed && (!c->known || e->value != 0))
					break;
				c->changed |= !c->known || c->value != e->value;
				c->value = e->value;
				c->known = true;
				break;
			}
			break;
		case ST_PAD_FRAME:
			p->complete = true;
			if (s->enabled && !s->paused) {
				if (p->wire)
					values(t, p, false);
				else
					announce(t, p);
			}
			break;
		case ST_PAD_REMOVE:
			if (p->wire)
				cterm_respond_printf(t, "\x1b[=5;%" PRIu32 "J", p->wire);
			*link = p->next;
			free(p->controls);
			free(p);
			break;
		default:
			break;
	}
}

void
st_cterm_pads_clear(struct st_cterm_pads *s)
{
	while (s->devices) {
		struct st_cterm_pad *p = s->devices;
		s->devices             = p->next;
		free(p->controls);
		free(p);
	}
}
