/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_script.h"

enum {
	COLLISIONS = 300,
	ANIMATIONS = 400,
	HIT_WORDS  = (ANIMATIONS + 63) / 64
};

struct collision {
	struct bounds            box;
	unsigned                 animation, variable;
	bool                     active, once;
	struct st_ddofx_callback callback;
	/* A macro can wait or redefine a box. Keep at most one outstanding
	 * notification per (box, animation) while script execution is busy. */
	uint64_t hits[HIT_WORDS];
};

struct st_ddofx_collisions {
	struct collision boxes[COLLISIONS];
	unsigned         next, pending, active;
};

bool
st_ddofx_collision_active(const struct st_ddofx_graphics *g)
{
	return g->collisions && g->collisions->active;
}

void
st_ddofx_collision_command(struct st_ddofx *ddo, char *args)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	int32_t                   id, n[7];
	if (!st_ddofx_field_number(ddo, &args, &id) || (unsigned)id > COLLISIONS)
		return;
	if (!id) {
		free(g->collisions);
		g->collisions = NULL;
		return;
	}
	for (unsigned i = 0; i < 7; ++i)
		if (!st_ddofx_field_number(ddo, &args, &n[i]))
			return;
	const char *reply = st_ddofx_field(&args);
	if (!reply || !args || n[0] > n[1] || n[2] > n[3] || (unsigned)n[4] > 1 || (unsigned)n[5] > 1000
	    || (unsigned)n[6] > ANIMATIONS)
		return;
	if (!g->collisions)
		g->collisions = calloc(1, sizeof(*g->collisions));
	if (!g->collisions)
		return;
	struct collision *c = &g->collisions->boxes[id - 1];
	if (!c->active)
		++g->collisions->active;
	for (unsigned i = 0; i < HIT_WORDS; ++i)
		for (uint64_t bits = c->hits[i]; bits; bits &= bits - 1)
			--g->collisions->pending;
	*c = (struct collision) {
	    .box       = {n[0], n[1], n[2], n[3]},
	    .once      = n[4],
	    .variable  = n[5],
	    .animation = n[6],
	    .active    = true
        };
	st_ddofx_callback_define(ddo, &c->callback, reply, args);
}

void
st_ddofx_collision_frame(struct st_ddofx *ddo, unsigned slot, int x, int y, const struct st_ddofx_sprite *sprite)
{
	struct st_ddofx_collisions *list = ddo->graphics->collisions;
	if (!list || !list->active || !sprite->pixels)
		return;
	int64_t right = (int64_t)x + sprite->width - 1, top = (int64_t)y - sprite->height + 1;
	for (unsigned i = 0; i < COLLISIONS; ++i) {
		struct collision *c = &list->boxes[i];
		if (!c->active || (c->animation && c->animation != slot + 1) || x > c->box.right
		    || right < c->box.left || y < c->box.top || top > c->box.bottom
		    || !st_ddofx_callback_enabled(ddo, &c->callback))
			continue;
		uint64_t bit = UINT64_C(1) << (slot % 64);
		if (!(c->hits[slot / 64] & bit)) {
			c->hits[slot / 64] |= bit;
			++list->pending;
		}
		if (c->once) {
			c->active = false;
			--list->active;
		}
	}
}

bool
st_ddofx_collision_pending(const struct st_ddofx *ddo)
{
	const struct st_ddofx_collisions *list = ddo->graphics ? ddo->graphics->collisions : NULL;
	return list && list->pending && !ddo->in_event && !ddo->background_paused;
}

void
st_ddofx_collision_poll(struct st_ddofx *ddo)
{
	if (!st_ddofx_collision_pending(ddo))
		return;
	struct st_ddofx_collisions *list = ddo->graphics->collisions;
	for (unsigned offset = 0; offset < COLLISIONS; ++offset) {
		unsigned          i = (list->next + offset) % COLLISIONS;
		struct collision *c = &list->boxes[i];
		for (unsigned j = 0; j < HIT_WORDS; ++j) {
			if (!c->hits[j])
				continue;
			unsigned bit = 0;
			while (!(c->hits[j] & (UINT64_C(1) << bit)))
				++bit;
			c->hits[j] &= ~(UINT64_C(1) << bit);
			--list->pending;
			list->next = (i + 1) % COLLISIONS;
			if (st_ddofx_callback_start(ddo, &c->callback, DDO_EVENT) && c->variable)
				st_ddofx_set_numeric_variable(ddo, c->variable, j * 64 + bit + 1);
			return;
		}
	}
}
