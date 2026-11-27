/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_controller_event.h"
#include "st_ddofx_script.h"
#include "st_time.h"
#include "string_util.h"

struct stick_config {
	struct st_ddofx_callback fields[9][4];
	bool                     active;
	int                      center_x, center_y;
	uint64_t                 due;
};

struct st_ddofx_joystick {
	struct stick_config slots[2];
};

void
st_ddofx_controller(struct st_ddofx *ddo, const struct st_controller_event *e, bool consumed)
{
	unsigned slot = 0;
	while (slot < 2 && ddo->sticks[slot].device != e->device)
		++slot;
	if (slot == 2 && e->record == ST_PAD_ADD) {
		for (slot = 0; slot < 2 && ddo->sticks[slot].device; ++slot)
			;
		if (slot < 2)
			ddo->sticks[slot].device = e->device;
		return;
	}
	if (slot == 2)
		return;
	struct st_ddofx_stick *s = &ddo->sticks[slot];
	if (e->record == ST_PAD_REMOVE) {
		*s = (struct st_ddofx_stick) {0};
		if (ddo->joystick) {
			ddo->joystick->slots[slot].center_x = 0;
			ddo->joystick->slots[slot].center_y = 0;
		}
		return;
	}
	if (e->record != ST_PAD_VALUE || (consumed && e->value))
		return;

	if (e->type == 3) { /* EV_ABS, already normalized by the controller mapper. */
		switch (e->code) {
			case 0:
				s->x = e->value;
				break;
			case 1:
				s->y = e->value;
				break;
			case 16:
				s->hat_x = e->value;
				break;
			case 17:
				s->hat_y = e->value;
				break;
		}
	}
	else if (e->type == 1) { /* EV_KEY */
		if (e->code >= 544 && e->code <= 547) {
			unsigned bit = 1u << (e->code - 544);
			s->dpad      = (s->dpad & ~bit) | (e->value ? bit : 0);
		}
		/* Preserve independent trigger/thumb and south/east mappings if a
		 * controller exposes both pairs. Either pair supplies the DOS buttons. */
		unsigned bit;
		switch (e->code) {
			case 304:
				bit = 1;
				break;
			case 305:
				bit = 2;
				break;
			case 288:
				bit = 4;
				break;
			case 289:
				bit = 8;
				break;
			default:
				return;
		}
		s->button_inputs = (s->button_inputs & ~bit) | (e->value ? bit : 0);
		s->buttons       = (s->button_inputs | (s->button_inputs >> 2)) & 3;
	}
}

static int
axis(int value, int center)
{
	/* One quarter of the normalized travel from center to either end. */
	int delta = value - center;
	return (delta > 8192) - (delta < -8192);
}

static unsigned
direction(const struct st_ddofx_stick *s, const struct stick_config *c)
{
	int dx = !!(s->dpad & 8) - !!(s->dpad & 4);
	int dy = !!(s->dpad & 2) - !!(s->dpad & 1);
	int x  = dx ? dx : s->hat_x ? s->hat_x : axis(s->x, c->center_x);
	int y  = dy ? dy : s->hat_y ? s->hat_y : axis(s->y, c->center_y);
	return (unsigned)((y + 1) * 3 + x + 1);
}

bool
st_ddofx_joystick_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *body)
{
	bool test      = !ascii_case_ncompare(body, "6TESTJOY,", 9);
	bool calibrate = !ascii_case_ncompare(body, "6CALJOY,", 8);
	if (!test && !calibrate && ascii_case_ncompare(body, "6DEFJOY,", 8))
		return false;
	char   *args = body + (test ? 9 : 8);
	int32_t num;
	if (!st_ddofx_field_number(ddo, &args, &num) || num < 1 || num > 2)
		return true;
	struct st_ddofx_stick *s = &ddo->sticks[num - 1];
	if (test) {
		if (!args)
			st_ddofx_query_reply(ddo, sink, s->device ? "Y" : "N", false);
		return true;
	}
	if (calibrate && (!s->device || args))
		return true;
	if (!ddo->joystick)
		ddo->joystick = calloc(1, sizeof(*ddo->joystick));
	if (!ddo->joystick)
		return true;
	struct stick_config *c = &ddo->joystick->slots[num - 1];
	if (calibrate) {
		c->center_x = s->x;
		c->center_y = s->y;
		return true;
	}
	int32_t buttons;
	if (!st_ddofx_field_number(ddo, &args, &buttons) || !args)
		return true;
	const char *field = st_ddofx_value(ddo, st_ddofx_field(&args));
	if (!ascii_case_compare(field, "CL"))
		*c = (struct stick_config) {.center_x = s->x, .center_y = s->y};
	else if (!ascii_case_compare(field, "AC")) {
		if ((unsigned)buttons <= 1) {
			c->active = buttons != 0;
			c->due    = 0;
		}
	}
	else {
		static const char *const dirs[] = {"LUC", "UP", "RUC", "LF", "", "RT", "LBC", "DN", "RBC"};
		unsigned                 dir;
		if (ascii_to_upper((unsigned char)*field) == 'B' && field[1] >= '1' && field[1] <= '3'
		    && !field[2]) {
			dir     = 4;
			buttons = field[1] - '0';
		}
		else {
			for (dir = 0; dir < 9 && ascii_case_compare(field, dirs[dir]); ++dir)
				;
			if (dir == 9 || dir == 4 || (unsigned)buttons > 3)
				return true;
		}
		const char *reply = st_ddofx_field(&args);
		if (reply && args)
			st_ddofx_callback_define(ddo, &c->fields[dir][buttons], reply, args);
	}
	return true;
}

static const struct st_ddofx_callback *
selected(const struct st_ddofx *ddo, unsigned slot)
{
	const struct stick_config   *c = &ddo->joystick->slots[slot];
	const struct st_ddofx_stick *s = &ddo->sticks[slot];
	if (!c->active || !s->device)
		return NULL;
	const struct st_ddofx_callback *cb = &c->fields[direction(s, c)][s->buttons];
	return (*cb->macro || *cb->reply) && st_ddofx_callback_enabled(ddo, cb) ? cb : NULL;
}

uint32_t
st_ddofx_joystick_wait_ms(const struct st_ddofx *ddo)
{
	uint64_t now;
	if (!ddo->joystick || ddo->controller_paused || ddo->in_event || st_time_monotonic_ms(&now))
		return UINT32_MAX;
	uint32_t wait = UINT32_MAX;
	for (unsigned i = 0; i < 2; ++i) {
		if (!selected(ddo, i))
			continue;
		uint64_t due = ddo->joystick->slots[i].due;
		if (due <= now)
			return 0;
		if (due - now < wait)
			wait = (uint32_t)(due - now);
	}
	return wait;
}

void
st_ddofx_joystick_poll(struct st_ddofx *ddo)
{
	uint64_t now;
	if (!ddo->joystick || ddo->controller_paused || ddo->in_event || st_time_monotonic_ms(&now))
		return;
	for (unsigned i = 0; i < 2; ++i) {
		struct stick_config            *c  = &ddo->joystick->slots[i];
		const struct st_ddofx_callback *cb = selected(ddo, i);
		if (cb && c->due <= now) {
			c->due = now + 275; /* Five DOS timer ticks between held actions. */
			if (st_ddofx_callback_start(ddo, cb, DDO_EVENT))
				return;
		}
	}
}
