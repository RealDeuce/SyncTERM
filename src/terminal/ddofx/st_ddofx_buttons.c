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
	unsigned                 mask, window;
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

struct st_ddofx_sprite *
st_ddofx_find_sprite(struct st_ddofx_graphics *g, const char *name)
{
	int i = st_ddofx_sprite_slot(g, name, false);
	return i >= 0 ? &g->sprites[i] : NULL;
}

static void
paint_button(struct st_ddofx *ddo, const struct push_button *button, const struct st_ddofx_sprite *sprite,
    enum page page)
{
	st_ddofx_draw_sprite(ddo->graphics, button->window, sprite, page, 'D', button->x, button->y);
	if (button->window) {
		struct bounds area = {button->x, button->x + sprite->width - 1, button->y - sprite->height + 1,
		    button->y};
		st_ddofx_refresh_window_area(ddo, button->window, area);
	}
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
	struct st_ddofx_sprite *up = st_ddofx_find_sprite(g, up_name), *down = st_ddofx_find_sprite(g, down_name);
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
	if (!args || !reply || !st_ddofx_sprite_fits(g, g->alert_graphic_window, up, v[0], v[1])
	    || !st_ddofx_sprite_fits(g, g->alert_graphic_window, down, v[0], v[1]) || (unsigned)v[2] > 255
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
	b->window = g->alert_graphic_window;
	b->pass = v[4] != 0;
	b->mask = (unsigned)v[5];
	st_ddofx_callback_define(ddo, &b->callback, reply, args, 95);

	if ((unsigned)slot > g->buttons->count)
		g->buttons->count = (unsigned)slot;
	paint_button(ddo, b, up, WORK);
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
	struct st_ddofx_sprite *s = st_ddofx_find_sprite(ddo->graphics, b->up);
	if (st_ddofx_sprite_fits(ddo->graphics, b->window, s, b->x, b->y))
		paint_button(ddo, b, s, VISUAL);
}

static bool
press(struct st_ddofx *ddo, unsigned i, enum st_ddofx_callback_kind kind, int x, int y)
{
	struct push_button     *b  = &ddo->graphics->buttons->slots[i];
	struct st_ddofx_sprite *up   = st_ddofx_find_sprite(ddo->graphics, b->up),
			       *down = st_ddofx_find_sprite(ddo->graphics, b->down);
	if (!st_ddofx_sprite_fits(ddo->graphics, b->window, up, b->x, b->y)
	    || !st_ddofx_sprite_fits(ddo->graphics, b->window, down, b->x, b->y))
		return false;
	if (kind == DDO_MOUSE && (x < b->x || x >= b->x + up->width || y > b->y || y <= b->y - up->height))
		return false;
	if (!st_ddofx_button_callback(ddo, &b->callback, kind, i))
		return false;
	paint_button(ddo, b, down, VISUAL);
	return true;
}

bool
st_ddofx_button_key(struct st_ddofx *ddo, unsigned i, uint16_t word)
{
	return word && i < st_ddofx_button_count(ddo) && ddo->graphics->buttons->slots[i].key == word
	       && press(ddo, i, DDO_KEY, 0, 0);
}

bool
st_ddofx_button_mouse(struct st_ddofx *ddo, unsigned i, int owner, int x, int y, unsigned buttons)
{
	return i < st_ddofx_button_count(ddo) && (int)ddo->graphics->buttons->slots[i].window == owner
	       && ddo->graphics->buttons->slots[i].mask == buttons && press(ddo, i, DDO_MOUSE, x, y);
}
