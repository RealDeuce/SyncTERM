/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_samples.h"
#include "st_ddofx_script.h"
#include "st_time.h"
#include "string_util.h"

enum {
	ANIMATIONS = 400,
	MOTIONS    = 256,
	KNOTS      = 30
};

struct knot {
	int path_frame, sprite_frame, sample, frequency;
};

struct motion {
	int32_t     points[8];
	int         count;
	struct knot knots[KNOTS];
};

struct sound_slot {
	int frame, sample, frequency;
};

struct animation {
	unsigned window;
	struct sound_slot      sounds[19];
	int                    x, y, first, last, frame, direction, loop, draw, fps;
	int                    first_path, last_path, path, path_frame, path_direction, path_loop;
	bool                   sound, queued, shown, advance, completed, frozen;
	uint64_t               due;
	uint8_t               *background;
	size_t                 capacity;
	struct st_console_rect painted;
};

struct st_ddofx_animations {
	struct animation profiles[ANIMATIONS];
	struct motion    paths[MOTIONS + 1];
	unsigned         queue[ANIMATIONS], count;
	uint64_t         due[GRAPHIC_WINDOWS + 1];
};

struct presentation {
	struct st_ddofx *ddo;
	unsigned window, count;
	uint8_t *hidden;
	int width, height;
	struct st_console_rect areas[2 * ANIMATIONS + 1];
};

static void
copy_background(const struct presentation *p, struct animation *a, bool save)
{
	const struct st_console_rect r = a->painted;
	for (unsigned row = 0; row < r.height; ++row) {
		uint8_t *hidden = p->hidden + (r.y + row) * p->width + r.x;
		uint8_t *saved = a->background + row * r.width;
		if (save)
			memcpy(saved, hidden, r.width);
		else
			memcpy(hidden, saved, r.width);
	}
}

static int
publish(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *damage)
{
	const struct presentation *p = context;
	if (pixels->width != WIDTH || pixels->height != HEIGHT)
		return 0;
	struct bounds changed = {WIDTH, -1, HEIGHT, -1};
	for (unsigned i = 0; i < p->count; ++i) {
		struct st_console_rect r = p->areas[i];
		struct bounds area = {r.x, r.x + r.width - 1, r.y, r.y + r.height - 1};
		st_ddofx_publish_page_area(p->ddo, p->window, pixels, area, &changed);
	}
	if (changed.right >= changed.left)
		*damage = (struct st_console_rect){changed.left, changed.top, changed.right - changed.left + 1,
		    changed.bottom - changed.top + 1};
	return 0;
}

static struct st_console_rect
footprint(const struct animation *a, const struct st_ddofx_sprite *s, int width, int height)
{
	int64_t left = a->x, right = left + s->width, bottom = (int64_t)a->y + 1, top = bottom - s->height;
	if (!s->pixels || (!a->draw && (left < 0 || right > width || top < 0 || bottom > height)) || right <= 0
	    || bottom <= 0 || left >= width || top >= height)
		return (struct st_console_rect) {0};
	if (left < 0)
		left = 0;
	if (right > width)
		right = width;
	if (top < 0)
		top = 0;
	if (bottom > height)
		bottom = height;
	return (struct st_console_rect) {left, top, right - left, bottom - top};
}

static void
compose(struct st_ddofx *ddo, unsigned window, const struct st_console_rect *frame, const uint8_t *image)
{
	struct st_ddofx_graphics   *g    = ddo->graphics;
	struct st_ddofx_animations *list = g->animations;
	struct st_ddofx_graphic_window *w = &g->graphic_windows[window];
	if (window && !w->visual)
		return;
	struct presentation p = {.ddo = ddo, .window = window, .width = window ? w->width : WIDTH,
	    .height = window ? w->height : HEIGHT, .hidden = window ? w->hidden_page : (uint8_t *)g->page[HIDDEN]};

	unsigned count = list ? list->count : 0;
	if (frame) {
		if (frame->x >= (unsigned)p.width || frame->y >= (unsigned)p.height)
			return;
		/* A movie frame only requires reassembling the sprite stack if it
		 * changes the background saved underneath a painted sprite. */
		unsigned i;
		for (i = 0; i < count; ++i) {
			const struct animation *a = &list->profiles[list->queue[i]];
			struct st_console_rect r = a->painted;
			if (a->window == window && r.width && r.x < frame->x + frame->width && frame->x < r.x + r.width
			    && r.y < frame->y + frame->height && frame->y < r.y + r.height)
				break;
		}
		if (i == count)
			count = 0;
	}

	/* Undo the old images from front to back. Then every new frame can
	 * save what is underneath it, including lower animations. */
	for (unsigned i = count; i; --i) {
		struct animation *a = &list->profiles[list->queue[i - 1]];
		if (a->window == window && a->painted.width) {
			p.areas[p.count++] = a->painted;
			copy_background(&p, a, false);
			a->painted = (struct st_console_rect) {0};
		}
	}
	if (frame) {
		struct st_console_rect r = *frame;
		if (r.x + r.width > (unsigned)p.width)
			r.width = (unsigned)p.width - r.x;
		if (r.y + r.height > (unsigned)p.height)
			r.height = (unsigned)p.height - r.y;
		for (unsigned y = 0; y < r.height; ++y)
			memcpy(p.hidden + (r.y + y) * p.width + r.x, image + y * frame->width, r.width);
		p.areas[p.count++] = r;
	}
	int32_t cx = g->cx, cy = g->cy;
	for (unsigned i = 0; i < count; ++i) {
		struct animation *a = &list->profiles[list->queue[i]];
		if (a->window != window || !a->shown)
			continue;
		const struct st_ddofx_sprite *s        = &g->sprites[a->frame];
		struct st_console_rect        r        = footprint(a, s, p.width, p.height);
		size_t                        required = (size_t)r.width * r.height;
		if (!required)
			continue;
		if (required > a->capacity) {
			uint8_t *b = realloc(a->background, required);
			if (!b)
				continue;
			a->background = b;
			a->capacity   = required;
		}
		a->painted = r;
		copy_background(&p, a, true);
		st_ddofx_draw_sprite(g, window, s, HIDDEN, "PCF"[a->draw], a->x, a->y);
		p.areas[p.count++] = r;
	}
	g->cx = cx;
	g->cy = cy;
	if (window) {
		for (unsigned i = 0; i < p.count; ++i) {
			struct st_console_rect r = p.areas[i];
			for (unsigned row = 0; row < r.height; ++row) {
				size_t offset = (r.y + row) * p.width + r.x;
				memcpy(w->visual + offset, p.hidden + offset, r.width);
			}
		}
	}
	if (p.count && !w->hidden)
		(void)st_console_modify_pixels(g->display, publish, &p);
}

void
st_ddofx_animation_background(struct st_ddofx *ddo, unsigned window, const struct st_console_rect *frame,
    const uint8_t *image)
{
	compose(ddo, window, frame, image);
}

static void
deadline(struct st_ddofx_animations *list)
{
	memset(list->due, 0, sizeof(list->due));
	for (unsigned i = 0; i < list->count; ++i) {
		const struct animation *a = &list->profiles[list->queue[i]];
		uint64_t *due = &list->due[a->window];
		if (a->due && (!*due || a->due < *due))
			*due = a->due;
	}
}

void
st_ddofx_animation_window_closed(struct st_ddofx_graphics *g, unsigned window)
{
	struct st_ddofx_animations *list = g->animations;
	if (!list)
		return;
	for (unsigned i = 0; i < list->count; ++i) {
		struct animation *a = &list->profiles[list->queue[i]];
		if (a->window != window)
			continue;
		/* Both virtual pages are about to be released. The profile survives,
		 * but its old damage and saved background belong to that allocation. */
		a->painted = (struct st_console_rect){0};
		a->shown = a->advance = false;
		a->due = 1;
	}
	deadline(list);
}

void
st_ddofx_clear_animations(struct st_ddofx_graphics *g)
{
	if (g->animations) {
		for (unsigned i = 0; i < ANIMATIONS; ++i)
			free(g->animations->profiles[i].background);
		free(g->animations);
		g->animations = NULL;
	}
}

static void
stop(struct st_ddofx *ddo, unsigned slot)
{
	struct st_ddofx_animations *list = ddo->graphics->animations;
	struct animation           *a    = &list->profiles[slot];
	if (!a->queued)
		return;
	a->shown = false;
	compose(ddo, a->window, NULL, NULL);
	unsigned i = 0;
	while (list->queue[i] != slot)
		++i;
	--list->count;
	memmove(list->queue + i, list->queue + i + 1, (list->count - i) * sizeof(*list->queue));
	a->queued = a->advance = a->completed = false;
	a->due                                = 0;
	free(a->background);
	a->background = NULL;
	a->capacity   = 0;
	deadline(list);
}

const struct st_ddofx_sprite *
st_ddofx_animation_face(const struct st_ddofx_graphics *g, unsigned id, bool *visible)
{
	if (!g->animations || !id || id > ANIMATIONS)
		return NULL;
	const struct animation *a = &g->animations->profiles[id - 1];
	if (visible)
		*visible = a->queued && a->shown;
	return a->fps && g->sprites[a->first].pixels ? &g->sprites[a->first] : NULL;
}

void
st_ddofx_stop_animation(struct st_ddofx *ddo, unsigned id)
{
	if (ddo->graphics->animations && id && id <= ANIMATIONS)
		stop(ddo, id - 1);
}

void
st_ddofx_play_animation(struct st_ddofx *ddo, unsigned id)
{
	if (!st_ddofx_animation_face(ddo->graphics, id, NULL))
		return;
	struct st_ddofx_animations *list = ddo->graphics->animations;
	struct animation *a = &list->profiles[id - 1];
	if (a->completed) {
		stop(ddo, id - 1);
		a->frame = a->first;
		a->direction = a->path_direction = 1;
		a->path = a->first_path;
		a->path_frame = 1;
	}
	if (a->queued && !a->frozen)
		return;
	if (!a->queued)
		list->queue[list->count++] = id - 1;
	a->frozen = false;
	a->queued = true;
	list->due[a->window] = a->due = 1;
}

void
st_ddofx_freeze_animation(struct st_ddofx_graphics *g, unsigned id)
{
	if (st_ddofx_animation_face(g, id, NULL)) {
		struct animation *a = &g->animations->profiles[id - 1];
		a->frozen = true;
		a->due = 0;
		deadline(g->animations);
	}
}

void
st_ddofx_place_animation(struct st_ddofx *ddo, unsigned id, unsigned owner, int x, int y, bool bind)
{
	if (!st_ddofx_animation_face(ddo->graphics, id, NULL))
		return;
	struct animation *a = &ddo->graphics->animations->profiles[id - 1];
	if (bind) {
		if (a->window != owner && a->shown) {
			a->shown = false;
			compose(ddo, a->window, NULL, NULL);
			a->shown = true;
		}
		a->first_path = a->last_path = a->path = a->path_loop = 0;
		a->window = owner;
		if (a->frozen && a->queued)
			a->due = 1;
		a->frozen = false;
		deadline(ddo->graphics->animations);
	}
	a->x = x;
	a->y = y;
	if (a->shown)
		compose(ddo, a->window, NULL, NULL);
}

static bool
frame_range(struct st_ddofx *ddo, char **args, int *first, int *last)
{
	char *start = st_ddofx_field(args), *end = st_ddofx_field(args);
	if (!end)
		return false;
	struct st_ddofx_graphics *g = ddo->graphics;
	*first                      = st_ddofx_sprite_slot(g, st_ddofx_value(ddo, start), false);
	*last                       = st_ddofx_sprite_slot(g, st_ddofx_value(ddo, end), false);
	if (*first < 0 || *last < *first)
		return false;
	const struct st_ddofx_sprite *s = &g->sprites[*first];
	for (int slot = *first; slot <= *last; ++slot)
		if (!g->sprites[slot].pixels || g->sprites[slot].width != s->width
		    || g->sprites[slot].height != s->height)
			return false;
	return true;
}

static void
define(struct st_ddofx *ddo, int slot, char *args, bool fly)
{
	struct animation *a = &ddo->graphics->animations->profiles[slot];
	int32_t           x, y, loop = 0, fields[6];
	int               first, last;
	if (!st_ddofx_field_number(ddo, &args, &x) || !st_ddofx_field_number(ddo, &args, &y)
	    || (!fly && (!st_ddofx_field_number(ddo, &args, &loop) || (unsigned)loop > 4))
	    || !frame_range(ddo, &args, &first, &last))
		return;
	if (fly) {
		if (!a->queued || a->completed || !st_ddofx_numbers(ddo, args, fields, 1)
		    || (unsigned)fields[0] > 2)
			return;
		a->draw    = fields[0];
		a->advance = false;
		a->due     = 1;
	}
	else {
		if (!st_ddofx_numbers(ddo, args, fields, 6) || (unsigned)fields[0] > MOTIONS
		    || fields[1] < fields[0] || fields[1] > MOTIONS || (unsigned)fields[2] > 3
		    || (unsigned)fields[3] > 2 || fields[4] < 1 || fields[4] > 100 || (unsigned)fields[5] > 1)
			return;
		stop(ddo, slot);
		*a = (struct animation) {.window = ddo->graphics->alert_graphic_window, .first_path = fields[0],
		    .last_path                       = fields[1],
		    .path                            = fields[0],
		    .path_frame                      = 1,
		    .path_direction                  = 1,
		    .path_loop                       = fields[2],
		    .loop                            = loop,
		    .draw                            = fields[3],
		    .fps                             = fields[4],
		    .sound                           = fields[5] != 0};
	}
	a->x     = x;
	a->y     = y;
	a->frame = a->first = first;
	a->last             = last;
	a->direction        = 1;
	if (fly)
		deadline(ddo->graphics->animations);
}

bool
st_ddofx_animation_command(struct st_ddofx *ddo, const char *verb, char *args)
{
	if (!ascii_case_compare(verb, "2COLL")) {
		st_ddofx_collision_command(ddo, args);
		struct st_ddofx_animations *list = ddo->graphics->animations;
		if (list && st_ddofx_collision_active(ddo->graphics)) {
			for (unsigned i = 0; i < list->count; ++i) {
				struct animation *a = &list->profiles[list->queue[i]];
				if (!a->due && !a->frozen)
					a->due = 1;
			}
			deadline(list);
		}
		return true;
	}

	enum {
		DEFINE,
		PLAY,
		STOP,
		FLY,
		SOUND,
		MOTION,
		KNOT
	};

	static const char *const names[] = {"2ANIM", "2PLAYA", "2STOPA", "2AFLY", "2ANIMSOUND", "2MOTION",
	    "2MFRAME"};
	unsigned                 op      = 0;
	while (op < sizeof(names) / sizeof(*names) && ascii_case_compare(verb, names[op]))
		++op;
	if (op == sizeof(names) / sizeof(*names))
		return false;
	int32_t id;
	if (!st_ddofx_field_number(ddo, &args, &id) || id < 0 || id > (op >= MOTION ? MOTIONS : ANIMATIONS))
		return true;
	struct st_ddofx_graphics *g = ddo->graphics;
	if (!g->animations && id && (op == DEFINE || op == MOTION))
		g->animations = calloc(1, sizeof(*g->animations));
	struct st_ddofx_animations *list = g->animations;
	if (!list)
		return true;
	if (!id) {
		if (op == MOTION)
			memset(list->paths, 0, sizeof(list->paths));
		else if (op == DEFINE || op == STOP) {
			for (unsigned i = 0; i < list->count; ++i)
				list->profiles[list->queue[i]].shown = false;
			for (unsigned w = 0; w <= GRAPHIC_WINDOWS; ++w)
				compose(ddo, w, NULL, NULL);
			for (unsigned i = 0; i < ANIMATIONS; ++i)
				free(list->profiles[i].background);
			memset(list->profiles, 0, sizeof(list->profiles));
			list->count = 0;
			memset(list->due, 0, sizeof(list->due));
		}
		return true;
	}
	if (op == MOTION || op == KNOT) {
		int32_t        n[9];
		struct motion *m = &list->paths[id];
		if (op == MOTION && st_ddofx_numbers(ddo, args, n, 9) && n[8] > 0) {
			*m = (struct motion) {
			    .points = {n[0], n[1], n[4], n[5], n[6], n[7], n[2], n[3]},
			    .count  = n[8]
                        };
		}
		else if (op == KNOT && st_ddofx_numbers(ddo, args, n, 5) && n[0] >= 1 && n[0] <= KNOTS && n[1] >= 1
		         && n[1] <= m->count && n[2] >= 0 && n[2] <= SPRITE_COUNT && (unsigned)n[3] <= 100)
			m->knots[n[0] - 1] = (struct knot) {n[1], n[2], n[3], n[4]};
		return true;
	}
	unsigned          slot = (unsigned)id - 1;
	struct animation *a    = &list->profiles[slot];
	if (op == SOUND) {
		int32_t n[4];
		if (a->fps && st_ddofx_numbers(ddo, args, n, 4) && n[0] >= 1 && n[0] <= 19 && n[1] >= 1
		    && n[1] <= 100 && n[2] > 0 && n[3] >= 1 && n[3] <= a->last - a->first + 1)
			a->sounds[n[0] - 1] = (struct sound_slot) {a->first + n[3] - 1, n[1], n[2]};
	}
	else if (op == DEFINE || op == FLY)
		define(ddo, slot, args, op == FLY);
	else if (!args && op == STOP)
		stop(ddo, slot);
	else if (!args && op == PLAY)
		st_ddofx_play_animation(ddo, id);
	return true;
}

static bool
advance_sprite(struct animation *a)
{
	if (!a->loop || a->loop == 4)
		return false;
	if (a->first == a->last)
		return a->sound && (a->loop == 1 || a->loop == 3);
	if (a->frame == a->last) {
		if (a->loop == 2)
			return false;
		if (a->loop == 1) {
			a->frame = a->first;
			return true;
		}
		a->direction = -1;
	}
	if (a->frame == a->first)
		a->direction = 1;
	a->frame += a->direction;
	return true;
}

static bool
advance_path(struct animation *a, const struct motion *paths)
{
	if (!a->path_loop || !a->path || !paths[a->path].count)
		return false;
	if (a->path_direction > 0 && a->path_frame < paths[a->path].count) {
		++a->path_frame;
		return true;
	}
	if (a->path_direction < 0 && a->path_frame > 1) {
		--a->path_frame;
		return true;
	}
	int next = a->path + a->path_direction;
	if (next < a->first_path || next > a->last_path) {
		if (a->path_loop == 2)
			return false;
		if (a->path_loop == 3) {
			a->path_direction = -a->path_direction;
			if (paths[a->path].count > 1) {
				a->path_frame += a->path_direction;
				return true;
			}
			next = a->path + a->path_direction;
			if (next < a->first_path || next > a->last_path)
				return false;
		}
		else
			next = a->first_path;
	}
	if (!paths[next].count)
		return false;
	a->path       = next;
	a->path_frame = a->path_direction > 0 ? 1 : paths[next].count;
	return true;
}

uint32_t
st_ddofx_animation_wait_ms(const struct st_ddofx *ddo)
{
	const struct st_ddofx_animations *list = ddo->graphics ? ddo->graphics->animations : NULL;
	uint64_t                          now;
	if (st_ddofx_collision_pending(ddo))
		return 0;
	if (!list || ddo->background_paused)
		return UINT32_MAX;
	uint64_t due = list->due[0], active = list->due[ddo->graphics->active_graphic_window];
	if (active && (!due || active < due))
		due = active;
	if (!due || st_time_monotonic_ms(&now))
		return UINT32_MAX;
	return now < due ? (uint32_t)(due - now) : 0;
}

static unsigned
period(const struct st_ddofx_graphics *g, const struct animation *a)
{
	return (1000 * g->animator_divisor + a->fps - 1) / a->fps;
}

void
st_ddofx_animation_resolution(struct st_ddofx_graphics *g)
{
	struct st_ddofx_animations *list = g->animations;
	uint64_t                    now;
	if (!list || st_time_monotonic_ms(&now))
		return;
	for (unsigned i = 0; i < list->count; ++i) {
		struct animation *a = &list->profiles[list->queue[i]];
		if (a->advance && a->due)
			a->due = now + period(g, a);
	}
	deadline(list);
}

static void
apply_path_frame(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, struct animation *a,
    const struct motion *m)
{
	if (!m->count)
		return;
	if (a->path_frame > m->count)
		a->path_frame = m->count;
	double t = a->path_frame == 1 && m->count > 1 ? 0 : (double)a->path_frame / m->count;
	a->x     = st_ddofx_bezier_coordinate(m->points, t);
	a->y     = st_ddofx_bezier_coordinate(m->points + 1, t);
	for (unsigned k = 0; k < KNOTS; ++k) {
		const struct knot *knot = &m->knots[k];
		if (knot->path_frame != a->path_frame)
			continue;
		if (knot->sample)
			st_ddofx_animation_sample(ddo, sink, true, knot->sample, knot->frequency);
		if (a->loop == 4 && knot->sprite_frame >= 1 && knot->sprite_frame <= a->last - a->first + 1)
			a->frame = a->first + knot->sprite_frame - 1;
	}
}

void
st_ddofx_animation_poll(struct st_ddofx *ddo, const struct st_ddofx_sink *sink)
{
	if (st_ddofx_animation_wait_ms(ddo))
		return;
	struct st_ddofx_graphics   *g    = ddo->graphics;
	struct st_ddofx_animations *list = g->animations;
	uint64_t                    now;
	if (st_time_monotonic_ms(&now) || !list)
		return;
	bool changed[GRAPHIC_WINDOWS + 1] = {false};
	for (unsigned i = 0; i < list->count; ++i) {
		struct animation *a = &list->profiles[list->queue[i]];
		if ((a->window && a->window != g->active_graphic_window) || !a->due || a->due > now)
			continue;
		bool     sprite = !a->advance || advance_sprite(a);
		bool     path   = !a->advance || advance_path(a, list->paths);
		uint64_t next   = now + period(g, a);
		if (path)
			apply_path_frame(ddo, sink, a, &list->paths[a->path]);
		if (!sprite && !path) {
			a->completed =
			    (a->loop == 2 || (a->path && a->path_loop == 2)) && a->loop != 1 && a->loop != 3;
			st_ddofx_collision_frame(ddo, a->window, list->queue[i], a->x, a->y, &g->sprites[a->frame]);
			a->due = st_ddofx_collision_active(g) ? next : 0;
			continue;
		}
		if (a->sound) {
			for (unsigned j = 0; j < 19; ++j) {
				const struct sound_slot *s = &a->sounds[j];
				if (s->sample && s->frame == a->frame)
					st_ddofx_animation_sample(ddo, sink, false, s->sample, s->frequency);
			}
		}
		a->shown = a->advance = true;
		a->due                = next;
		st_ddofx_collision_frame(ddo, a->window, list->queue[i], a->x, a->y, &g->sprites[a->frame]);
		changed[a->window] = true;
	}
	for (unsigned w = 0; w <= GRAPHIC_WINDOWS; ++w)
		if (changed[w])
			compose(ddo, w, NULL, NULL);
	st_ddofx_collision_poll(ddo);
	deadline(list);
}
