/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <stdlib.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_script.h"
#include "st_time.h"
#include "string_util.h"

enum {
	ICON_COUNT = 300
};

struct icon {
	int                      x, y, origin_x, origin_y;
	unsigned                 owner, host, animation, pointer;
	bool                     opened, stealth, animated_pointer;
	struct st_ddofx_callback open, close;
};

struct st_ddofx_icons {
	struct icon              items[ICON_COUNT + 1];
	struct st_ddofx_callback drops[ICON_COUNT + 1];
	unsigned                 drag, hover;
	int                      mouse_x, mouse_y, start_x, start_y;
	bool                     moved;
	uint64_t                 pressed;
};

static int
clamp(int value, int low, int high)
{
	if (value < low)
		return low;
	return value > high ? high : value;
}

static void
position(struct st_ddofx *ddo, struct icon *i, int x, int y)
{
	i->x = x;
	i->y = y;
	st_ddofx_place_animation(ddo, i->animation, i->owner, x, y, false);
}

static void
move(struct st_ddofx *ddo, struct icon *i, int x, int y)
{
	const struct st_ddofx_graphic_window *w = &ddo->graphics->graphic_windows[i->owner];
	const struct st_ddofx_sprite         *s = st_ddofx_animation_face(ddo->graphics, i->animation, NULL);
	if (!s || (i->owner && !w->visual))
		return;
	int width = i->owner ? w->width : WIDTH, height = i->owner ? w->height : HEIGHT;
	if (s->width <= width && s->height <= height)
		position(ddo, i, clamp(x, 0, width - s->width), clamp(y, s->height - 1, height - 1));
}

void
st_ddofx_close_icon(struct st_ddofx *ddo, unsigned id)
{
	struct st_ddofx_icons *icons = ddo->graphics->icons;
	if (!icons || !id || id > ICON_COUNT)
		return;
	struct icon *i = &icons->items[id];
	if (!i->opened || (ddo->background_download && !i->stealth))
		return;
	i->opened = false;
	st_ddofx_play_animation(ddo, i->animation);
	st_ddofx_callback_start(ddo, &i->close, DDO_EVENT);
}

bool
st_ddofx_next_icon(struct st_ddofx *ddo, enum st_ddofx_group_kind kind, unsigned id, unsigned *next)
{
	struct st_ddofx_icons *icons = ddo->graphics ? ddo->graphics->icons : NULL;
	if (!icons)
		return false;
	struct icon *i = &icons->items[id];
	if (kind == DDO_ICON_OPEN) {
		if (*next > ICON_COUNT || !i->animation || i->opened || (ddo->background_download && !i->stealth))
			return false;
		while (*next < ICON_COUNT && i->host) {
			struct icon *other = &icons->items[++*next];
			if (other->opened && other->host == i->host) {
				if (ddo->background_download && !other->stealth)
					return false;
				st_ddofx_close_icon(ddo, *next);
				return true;
			}
		}
		*next     = ICON_COUNT + 1;
		i->opened = true;
		if (icons->drag == id || icons->hover == id)
			st_ddofx_icon_cancel(ddo->graphics);
		st_ddofx_stop_animation(ddo, i->animation);
		st_ddofx_callback_start(ddo, &i->open, DDO_EVENT);
		return true;
	}

	unsigned last = kind == DDO_ICON_RESTORE_ALL ? ICON_COUNT : id;
	for (; id + *next <= last; ++*next) {
		i = &icons->items[id + *next];
		if (!i->animation || (i->x == i->origin_x && i->y == i->origin_y))
			continue;
		position(ddo, i, i->x + clamp(i->origin_x - i->x, -30, 30),
		    i->y + clamp(i->origin_y - i->y, -30, 30));
		uint64_t now;
		if (!st_time_monotonic_ms(&now))
			ddo->wait_until = now + 16;
		return true;
	}
	return false;
}

void
st_ddofx_icon_cancel(struct st_ddofx_graphics *g)
{
	if (g->icons)
		g->icons->drag = g->icons->hover = 0;
}

bool
st_ddofx_icon_mouse(struct st_ddofx *ddo, unsigned changed)
{
	struct st_ddofx_graphics          *g     = ddo->graphics;
	struct st_ddofx_icons             *icons = g->icons;
	const struct st_ddofx_mouse_state *m     = &g->mouse;
	if (!icons)
		return false;
	if (icons->drag) {
		unsigned     id = icons->drag;
		struct icon *i  = &icons->items[id];
		int          dx = m->x - icons->mouse_x, dy = m->y - icons->mouse_y;
		if (dx || dy)
			icons->moved = true;
		if (icons->moved)
			move(ddo, i, icons->start_x + dx, icons->start_y + dy);
		if (!(m->buttons & 1)) {
			icons->drag = 0;
			uint64_t now;
			if (icons->moved)
				st_ddofx_callback_start(ddo, &icons->drops[id], DDO_EVENT);
			else if (!st_time_monotonic_ms(&now) && now - icons->pressed <= 500)
				st_ddofx_execute_group(ddo, DDO_ICON_OPEN, id, false);
		}
		return true;
	}
	int x = m->x, y = m->y;
	int owner = m->outside ? -1 : st_ddofx_window_point(g, &x, &y);
	for (unsigned id = ICON_COUNT; owner >= 0 && id; --id) {
		struct icon *i = &icons->items[id];
		if (!i->animation || i->owner != (unsigned)owner || i->opened
		    || (ddo->background_download && !i->stealth))
			continue;
		bool                          visible;
		const struct st_ddofx_sprite *s = st_ddofx_animation_face(g, i->animation, &visible);
		if (!s || !visible || x < i->x || x >= i->x + s->width || y > i->y || y <= i->y - s->height)
			continue;
		if (icons->hover != id && i->pointer)
			st_ddofx_field_pointer(ddo, i->pointer, i->animated_pointer);
		icons->hover = id;
		if ((changed & 1) && m->buttons == 1) {
			icons->drag    = id;
			icons->mouse_x = m->x;
			icons->mouse_y = m->y;
			icons->start_x = i->x;
			icons->start_y = i->y;
			icons->moved   = false;
			st_time_monotonic_ms(&icons->pressed);
			return true;
		}
		return !m->buttons && !changed && i->pointer;
	}
	icons->hover = 0;
	return false;
}

bool
st_ddofx_icon_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *verb, char *args)
{
	enum {
		CREATE,
		POINTER,
		ON_OPEN,
		ON_CLOSE,
		ON_DROP,
		OPEN,
		CLOSE,
		DROP,
		KILL,
		MOVE,
		STATS,
		RESTORE,
		RESTORE_ALL
	};

	static const char *const names[] = {"2ICON", "2MPICON", "2ICONO", "2ICONC", "2ICOND", "2OICON", "2CICON",
	    "2DICON", "2KICON", "2MICON", "2GICON?", "2RICON", "2RAICON"};
	unsigned                 op      = 0;
	while (op <= RESTORE_ALL && ascii_case_compare(verb, names[op]))
		++op;
	if (op > RESTORE_ALL)
		return false;
	struct st_ddofx_graphics *g     = ddo->graphics;
	struct st_ddofx_icons    *icons = g->icons;
	if (op == RESTORE_ALL) {
		if (!args && icons)
			st_ddofx_execute_group(ddo, DDO_ICON_RESTORE_ALL, 1, false);
		return true;
	}
	int32_t id, n[4];
	if (!st_ddofx_field_number(ddo, &args, &id) || id < 0 || id > ICON_COUNT)
		return true;
	if (!id) {
		if (op == CREATE && icons && !args) {
			for (unsigned slot = 1; slot <= ICON_COUNT; ++slot) {
				st_ddofx_freeze_animation(g, icons->items[slot].animation);
				icons->items[slot] = (struct icon) {0};
			}
			st_ddofx_icon_cancel(g);
		}
		return true;
	}
	if (op == CREATE) {
		if (!st_ddofx_numbers(ddo, args, n, 4) || n[2] < 0 || n[2] > GRAPHIC_WINDOWS
		    || !st_ddofx_animation_face(g, n[3], NULL) || n[0] < INT16_MIN || n[0] > INT16_MAX
		    || n[1] < INT16_MIN || n[1] > INT16_MAX)
			return true;
		if (!icons)
			g->icons = icons = calloc(1, sizeof(*icons));
		if (!icons)
			return true;
		struct icon *i = &icons->items[id];
		if (i->animation && i->animation != (unsigned)n[3])
			st_ddofx_stop_animation(ddo, i->animation);
		if (icons->drag == (unsigned)id || icons->hover == (unsigned)id)
			st_ddofx_icon_cancel(g);
		*i                       = (struct icon) {.origin_x = n[0],
		    .x                        = n[0],
		    .origin_y                 = n[1],
		    .y                        = n[1],
		    .owner                    = g->alert_graphic_window,
		    .host                     = n[2],
		    .animation                = n[3],
		    .stealth                  = ddo->define_stealth};
		icons->drops[id].stealth = i->stealth;
		st_ddofx_place_animation(ddo, i->animation, i->owner, i->x, i->y, true);
		return true;
	}
	if (!icons || !icons->items[id].animation)
		return true;
	struct icon *i = &icons->items[id];
	if (op == POINTER) {
		if (id <= 100 && st_ddofx_numbers(ddo, args, n, 2) && (unsigned)n[0] <= 1 && n[1] >= 1
		    && n[1] <= 100) {
			i->pointer          = n[1];
			i->animated_pointer = n[0] != 0;
			if (icons->hover == (unsigned)id)
				icons->hover = 0;
		}
	}
	else if (op >= ON_OPEN && op <= ON_DROP) {
		char *reply = st_ddofx_field(&args);
		if (reply && args) {
			struct st_ddofx_callback *cb = op == ON_OPEN    ? &i->open
			                               : op == ON_CLOSE ? &i->close
			                                                : &icons->drops[id];
			st_ddofx_callback_define(ddo, cb, reply, args);
			cb->macro[50] = cb->reply[50] = 0;
			cb->stealth                   = i->stealth;
		}
	}
	else if (op == MOVE) {
		if (st_ddofx_numbers(ddo, args, n, 2))
			move(ddo, i, n[0], n[1]);
	}
	else if (!args) {
		switch (op) {
			case OPEN:
			case RESTORE:
				st_ddofx_execute_group(ddo, op == OPEN ? DDO_ICON_OPEN : DDO_ICON_RESTORE, id,
				    false);
				break;
			case CLOSE:
				st_ddofx_close_icon(ddo, id);
				break;
			case DROP:
				if (!i->opened)
					st_ddofx_callback_start(ddo, &icons->drops[id], DDO_EVENT);
				break;
			case KILL:
				st_ddofx_stop_animation(ddo, i->animation);
				*i = (struct icon) {0};
				if (icons->drag == (unsigned)id || icons->hover == (unsigned)id)
					st_ddofx_icon_cancel(g);
				break;
			case STATS:
			{
				const struct st_ddofx_sprite *s = st_ddofx_animation_face(g, i->animation, NULL);
				if (s) {
					char result[64];
					snprintf(result, sizeof(result), "%d,%d,%d,%d", i->x, i->x + s->width - 1,
					    i->y - s->height + 1, i->y);
					st_ddofx_query_reply(ddo, sink, result, false);
				}
				break;
			}
		}
	}
	return true;
}
