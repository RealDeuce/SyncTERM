/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_script.h"
#include "string_util.h"

enum {
	BUTTON_COUNT = 300
};

struct push_button {
	char                     up[9], down[9];
	int                      x, y;
	uint16_t                 key;
	unsigned                 mask;
	bool                     pass;
	struct st_ddofx_callback callback;
};

struct st_ddofx_buttons {
	struct push_button slots[BUTTON_COUNT];
	unsigned           count;
};

unsigned
st_ddofx_button_count(const struct st_ddofx *ddo)
{
	return ddo->graphics && ddo->graphics->buttons ? ddo->graphics->buttons->count : 0;
}

static struct st_ddofx_sprite *
sprite(struct st_ddofx_graphics *g, const char *name)
{
	int i = st_ddofx_sprite_slot(g, name, false);
	return i >= 0 ? &g->sprites[i] : NULL;
}

static bool
fits(const struct st_ddofx_sprite *s, int32_t x, int32_t y)
{
	return s && x >= 0 && x <= WIDTH - s->width && y >= s->height - 1 && y < HEIGHT;
}

void
st_ddofx_define_button(struct st_ddofx *ddo, char *args)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	int32_t                   slot;
	if (!st_ddofx_field_number(ddo, &args, &slot) || slot < 0 || slot > BUTTON_COUNT)
		return;
	if (!slot) {
		free(g->buttons);
		g->buttons = NULL;
		return;
	}
	const char *up_name = st_ddofx_field(&args), *down_name = st_ddofx_field(&args);
	if (!up_name || !down_name)
		return;
	struct st_ddofx_sprite *up = sprite(g, up_name), *down = sprite(g, down_name);
	if (!up || !down) {
		if (g->buttons)
			g->buttons->slots[slot - 1] = (struct push_button) {0};
		return;
	}
	int32_t v[6];
	for (unsigned i = 0; i < 6; ++i)
		if (!st_ddofx_field_number(ddo, &args, &v[i]))
			return;
	char *reply = st_ddofx_field(&args);
	if (!args || !reply || !fits(up, v[0], v[1]) || !fits(down, v[0], v[1]) || (unsigned)v[2] > 255
	    || (unsigned)v[3] > 255 || (v[4] != 0 && v[4] != 1) || v[5] < 1 || v[5] > 7)
		return;
	if (!g->buttons)
		g->buttons = calloc(1, sizeof(*g->buttons));
	if (!g->buttons)
		return;
	struct push_button *b = &g->buttons->slots[slot - 1];
	memcpy(b->up, up->name, sizeof(b->up));
	memcpy(b->down, down->name, sizeof(b->down));
	b->x    = v[0];
	b->y    = v[1];
	b->key  = (uint16_t)((v[2] << 8) | v[3]);
	b->pass = v[4] != 0;
	b->mask = (unsigned)v[5];
	st_ddofx_callback_define(ddo, &b->callback, reply, args);
	b->callback.macro[95] = b->callback.reply[95] = 0;
	if ((unsigned)slot > g->buttons->count)
		g->buttons->count = (unsigned)slot;
	st_ddofx_draw_sprite(g, up, WORK, 'D', b->x, b->y);
	if (g->mouse.buttons && !g->mouse.scan.waiting && !g->mouse.ignore_hold)
		g->mouse.scan.repeat_due = 1;
}

bool
st_ddofx_button_pass(const struct st_ddofx *ddo, unsigned i)
{
	return i < st_ddofx_button_count(ddo) && ddo->graphics->buttons->slots[i].pass;
}

void
st_ddofx_button_up(struct st_ddofx *ddo, unsigned i)
{
	if (i >= st_ddofx_button_count(ddo))
		return;
	struct push_button     *b = &ddo->graphics->buttons->slots[i];
	struct st_ddofx_sprite *s = sprite(ddo->graphics, b->up);
	if (fits(s, b->x, b->y))
		st_ddofx_draw_sprite(ddo->graphics, s, VISUAL, 'D', b->x, b->y);
}

static bool
press(struct st_ddofx *ddo, unsigned i, enum st_ddofx_callback_kind kind, int x, int y)
{
	struct push_button     *b  = &ddo->graphics->buttons->slots[i];
	struct st_ddofx_sprite *up = sprite(ddo->graphics, b->up), *down = sprite(ddo->graphics, b->down);
	if (!fits(up, b->x, b->y) || !fits(down, b->x, b->y))
		return false;
	if (kind == DDO_MOUSE && (x < b->x || x >= b->x + up->width || y > b->y || y <= b->y - up->height))
		return false;
	if (!st_ddofx_button_callback(ddo, &b->callback, kind, i))
		return false;
	st_ddofx_draw_sprite(ddo->graphics, down, VISUAL, 'D', b->x, b->y);
	return true;
}

bool
st_ddofx_button_key(struct st_ddofx *ddo, unsigned i, uint16_t word)
{
	return word && i < st_ddofx_button_count(ddo) && ddo->graphics->buttons->slots[i].key == word
	       && press(ddo, i, DDO_KEY, 0, 0);
}

bool
st_ddofx_button_mouse(struct st_ddofx *ddo, unsigned i, int x, int y, unsigned buttons)
{
	return i < st_ddofx_button_count(ddo) && ddo->graphics->buttons->slots[i].mask == buttons
	       && press(ddo, i, DDO_MOUSE, x, y);
}
