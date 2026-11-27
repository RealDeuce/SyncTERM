/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_script.h"

enum {
	PROFILE_COUNT = 3000
};

struct profile {
	int32_t x, y, buffer;
	int     first, last, frame, step;
	bool    active, mirror, oscillate;
};

struct st_ddofx_simple {
	struct profile profiles[PROFILE_COUNT];
	unsigned       animations, count;
};

static void
define(struct st_ddofx *ddo, bool animated, char *args)
{
	struct st_ddofx_graphics *g    = ddo->graphics;
	struct profile            next = {.active = true};
	int32_t                   slot = 0, loop = 1, mode;
	if (animated && (!st_ddofx_field_number(ddo, &args, &slot) || slot < 1 || slot > PROFILE_COUNT))
		return;
	if (!st_ddofx_field_number(ddo, &args, &next.x) || !st_ddofx_field_number(ddo, &args, &next.y)
	    || !st_ddofx_field_number(ddo, &args, &next.buffer) || next.buffer < 0)
		return;
	char *start = st_ddofx_field(&args), *end = animated ? st_ddofx_field(&args) : start;
	if (!start || !end || (animated && !st_ddofx_field_number(ddo, &args, &loop))
	    || !st_ddofx_field_number(ddo, &args, &mode) || args || (mode != 1 && mode != 2)
	    || (loop != 1 && loop != 2))
		return;
	const char *name = st_ddofx_value(ddo, start);
	if (animated && !strcmp(name, "NONE"))
		next.active = false;
	else {
		next.first = st_ddofx_sprite_slot(g, name, false);
		next.last  = animated ? st_ddofx_sprite_slot(g, st_ddofx_value(ddo, end), false) : next.first;
		if (next.first < 0 || next.last < 0)
			return;
		next.frame     = next.first;
		next.step      = next.last >= next.first ? 1 : -1;
		next.oscillate = loop == 2;
		next.mirror    = mode == 2;
	}
	if (!g->simple)
		g->simple = calloc(1, sizeof(*g->simple));
	struct st_ddofx_simple *stack = g->simple;
	if (!stack)
		return;
	unsigned index = animated ? (unsigned)slot - 1 : stack->count;
	/* Animation slots form a sparse prefix. Keep appended static entries
	 * in drawing order when the host redefines one of those animations. */
	if (index >= PROFILE_COUNT || (animated && index >= stack->animations && stack->count > stack->animations))
		return;
	stack->profiles[index] = next;
	if (stack->count <= index)
		stack->count = index + 1;
	if (animated && stack->animations <= index)
		stack->animations = index + 1;
}

static bool
repair_area(const struct profile *p, const struct st_ddofx_sprite *s, struct bounds *bounds)
{
	int64_t left = (int64_t)p->x - p->buffer, right = (int64_t)p->x + s->width - 1 + p->buffer;
	int64_t top = (int64_t)p->y - s->height + 1 - p->buffer, bottom = (int64_t)p->y + p->buffer;
	if (right < 0 || bottom < 0 || left >= WIDTH || top >= HEIGHT)
		return false;
	*bounds = (struct bounds) {left < 0 ? 0 : (int)left, right >= WIDTH ? WIDTH - 1 : (int)right,
	    top < 0 ? 0 : (int)top, bottom >= HEIGHT ? HEIGHT - 1 : (int)bottom};
	return true;
}

static void
advance(struct profile *p)
{
	if (!p->active || p->first == p->last)
		return;
	int low = p->first < p->last ? p->first : p->last, high = p->first < p->last ? p->last : p->first;
	int next = p->frame + p->step;
	if (next >= low && next <= high)
		p->frame = next;
	else if (p->oscillate) {
		p->step = -p->step;
		p->frame += p->step;
	}
	else
		p->frame = p->first;
}

static int
publish(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	struct st_ddofx_graphics *g = context;
	if (pixels->width != WIDTH || pixels->height != HEIGHT)
		return 0;
	struct bounds damage = {WIDTH, -1, HEIGHT, -1};
	for (unsigned i = 0; i < g->simple->count; ++i) {
		const struct profile         *p = &g->simple->profiles[i];
		const struct st_ddofx_sprite *s = &g->sprites[p->frame];
		struct bounds                 r;
		if (!p->active || !s->pixels || !repair_area(p, s, &r))
			continue;
		for (int y = r.top; y <= r.bottom; ++y) {
			size_t row = (size_t)y * WIDTH;
			for (int x = r.left; x <= r.right; ++x)
				pixels->primary[row + x] = pixels->alternate[row + x] = g->page[WORK][y][x];
		}
		if (r.left < damage.left)
			damage.left = r.left;
		if (r.right > damage.right)
			damage.right = r.right;
		if (r.top < damage.top)
			damage.top = r.top;
		if (r.bottom > damage.bottom)
			damage.bottom = r.bottom;
	}
	if (damage.right >= 0)
		*changed = (struct st_console_rect) {damage.left, damage.top, damage.right - damage.left + 1,
		    damage.bottom - damage.top + 1};
	return 0;
}

static void
draw_stack(struct st_ddofx_graphics *g)
{
	struct st_ddofx_simple *stack = g->simple;
	if (!stack || !stack->count)
		return;
	/* Restore every background first so an overlapping repair cannot erase
	 * an earlier sprite. Publish only after the entire frame is composed. */
	for (unsigned i = 0; i < stack->count; ++i) {
		const struct profile         *p = &stack->profiles[i];
		const struct st_ddofx_sprite *s = &g->sprites[p->frame];
		struct bounds                 r;
		if (!p->active || !s->pixels || !repair_area(p, s, &r))
			continue;
		for (int y = r.top; y <= r.bottom; ++y)
			memcpy(&g->page[WORK][y][r.left], &g->page[BACKGROUND][y][r.left],
			    (size_t)(r.right - r.left + 1));
	}
	for (unsigned i = 0; i < stack->count; ++i) {
		const struct profile         *p = &stack->profiles[i];
		const struct st_ddofx_sprite *s = &g->sprites[p->frame];
		if (p->active && s->pixels)
			st_ddofx_draw_sprite(g, 0, s, WORK, p->mirror ? 'F' : 'C', p->x, p->y);
	}
	(void)st_console_modify_pixels(g->display, publish, g);
	for (unsigned i = 0; i < stack->animations; ++i)
		advance(&stack->profiles[i]);
}

void
st_ddofx_simple_command(struct st_ddofx *ddo, const char *verb, char *args)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	if (!strcmp(verb, "2SS") || !strcmp(verb, "2SSA"))
		define(ddo, verb[3] == 'A', args);
	else if (!args && g->simple) {
		if (!strcmp(verb, "2ACSS")) {
			free(g->simple);
			g->simple = NULL;
		}
		else if (!strcmp(verb, "2CSS")) {
			memset(g->simple->profiles + g->simple->animations, 0,
			    (g->simple->count - g->simple->animations) * sizeof(*g->simple->profiles));
			g->simple->count = g->simple->animations;
		}
		else if (!strcmp(verb, "2DSS"))
			draw_stack(g);
	}
}
