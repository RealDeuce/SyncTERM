/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "st_ddofx_ray.h"
#include "st_ddofx_script.h"
#include "st_time.h"
#include "string_util.h"

typedef uint8_t tile[RC_TILE][RC_TILE];

static tile *
texture(struct rc_world *w, int kind, int number)
{
	if (number < 1)
		return NULL;
	switch (kind) {
		case FLCL:
			return number <= 23 ? &w->flats[number - 1] : NULL;
		case LOWALL:
		case HIWALL:
			return number <= 8 ? &w->walls[kind - LOWALL][number - 1] : NULL;
		case LOOBJ:
		case HIOBJ:
			return number <= 15 ? &w->objects[kind - LOOBJ][number - 1] : NULL;
	}
	return NULL;
}

void
st_ddofx_ray_defaults(struct rc_world *w)
{
	w->angle  = 360;
	w->width  = 320;
	w->height = 160;
	w->bottom = 159;
	for (int kind = FLCL; kind <= HIOBJ; ++kind) {
		for (int i = 1; i <= 23; ++i) {
			tile *t = texture(w, kind, i);
			if (!t)
				break;
			for (int y = 0; y < RC_TILE; ++y)
				for (int x = 0; x < RC_TILE; ++x)
					(*t)[y][x] = 16 + 2 * i + ((x / 8 ^ y / 8) & 1) + 8 * kind;
		}
	}
}

/* Cast one screen column through the grid. Distance along the ray is
 * measured perpendicular to the view plane, including at the screen edges. */
struct ray {
	double x, y, dx, dy, forward_x, forward_y;
};

struct intersection {
	double distance;
	int    u;
};

struct object_hit {
	struct intersection hit;
	int                 layer, texture;
};

static bool
plane_hit(const struct ray *r, int cx, int cy, double nx, double ny, double enter, double leave,
    struct intersection *hit)
{
	double denominator = r->dx * nx + r->dy * ny;
	if (fabs(denominator) < 1e-9)
		return false;
	double distance = ((cx + 0.5 - r->x) * nx + (cy + 0.5 - r->y) * ny) / denominator;
	if (distance < enter || distance > leave || distance < 0.001)
		return false;
	double u = 0.5 + (r->x + distance * r->dx - cx - 0.5) * ny - (r->y + distance * r->dy - cy - 0.5) * nx;
	if (u < 0 || u >= 1)
		return false;
	*hit = (struct intersection) {distance, (int)(RC_TILE * u)};
	return true;
}

static bool
wall_hit(const struct ray *r, int cx, int cy, int shape, double enter, double leave, bool x_edge,
    struct intersection *hit)
{
	if (shape == 2 || shape == 3)
		return plane_hit(r, cx, cy, shape == 3, shape == 2, enter, leave, hit);
	double distance = enter;
	double u;
	if (shape == 4) {
		double x = r->x - cx - 0.5, y = r->y - cy - 0.5;
		double a = r->dx * r->dx + r->dy * r->dy, b = x * r->dx + y * r->dy;
		double disc = b * b - a * (x * x + y * y - 0.25);
		if (disc < 0)
			return false;
		distance = (-b - sqrt(disc)) / a;
		if (distance < enter || distance > leave)
			return false;
		u = atan2(y + distance * r->dy, x + distance * r->dx) / 6.283185307179586;
	}
	else
		u = x_edge ? r->y + distance * r->dy : r->x + distance * r->dx;
	*hit = (struct intersection) {fmax(distance, 0.001), (int)((u - floor(u)) * RC_TILE) % RC_TILE};
	return true;
}

static void
draw_column(struct rc_world *w, int column, const struct intersection *hit, int slot, bool object)
{
	double scale = 160.0 / hit->distance;
	int    top   = (int)fmax(0, ceil(79.5 - scale));
	int    end   = (int)fmin(160, ceil(79.5 + scale));
	for (int y = top; y < end; ++y) {
		int v = (int)((y + 0.5 - 80.0 + scale) * RC_TILE / scale);
		if ((unsigned)v >= 2 * RC_TILE)
			continue;
		uint8_t index = object ? w->objects[v < RC_TILE][slot - 1][v % RC_TILE][hit->u]
		                       : w->walls[v < RC_TILE][slot - 1][v % RC_TILE][hit->u];
		if (!object || index)
			w->frame[y][column] = index;
	}
}

static void
render_column(struct rc_world *w, int column, const struct ray *r)
{
	/* One intersection per object layer per visited cell, with at most one
	 * row or column boundary crossed per iteration. */
	struct object_hit objects[(RC_WIDTH + RC_HEIGHT) * RC_LAYERS];
	unsigned          count = 0;
	int               cx = (int)r->x, cy = (int)r->y;
	double            step_x = r->dx ? fabs(1 / r->dx) : DBL_MAX;
	double            step_y = r->dy ? fabs(1 / r->dy) : DBL_MAX;
	double            edge_x = r->dx ? (r->dx > 0 ? cx + 1 - r->x : r->x - cx) * step_x : DBL_MAX;
	double            edge_y = r->dy ? (r->dy > 0 ? cy + 1 - r->y : r->y - cy) * step_y : DBL_MAX;
	double            enter  = 0;
	bool              x_edge = false;
	while (cx >= 1 && cx <= RC_WIDTH && cy >= 1 && cy <= RC_HEIGHT) {
		const struct rc_cell *c     = &w->cells[cy - 1][cx - 1];
		double                leave = fmin(edge_x, edge_y);
		struct intersection   wall;
		bool                  solid = c->wall && wall_hit(r, cx, cy, c->wall, enter, leave, x_edge, &wall);
		for (int i = 0; i < RC_LAYERS; ++i) {
			const struct rc_object *o = &c->objects[i];
			if (!o->texture)
				continue;
			double              nx = o->orientation == 3 ? r->forward_x : o->orientation == 1;
			double              ny = o->orientation == 3 ? r->forward_y : o->orientation == 2;
			struct intersection hit;
			if (plane_hit(r, cx, cy, nx, ny, enter, leave, &hit)) {
				/* An object sharing a wall cell hangs on its visible face. */
				if (solid && hit.distance > wall.distance)
					hit.distance = wall.distance;
				objects[count++] = (struct object_hit) {hit, i, o->texture};
			}
		}
		if (solid) {
			draw_column(w, column, &wall, c->texture, false);
			break;
		}
		enter  = leave;
		x_edge = edge_x < edge_y;
		if (x_edge) {
			cx += r->dx > 0 ? 1 : -1;
			edge_x += step_x;
		}
		else {
			cy += r->dy > 0 ? 1 : -1;
			edge_y += step_y;
		}
	}
	for (int layer = 0; layer < RC_LAYERS; ++layer)
		for (unsigned i = count; i > 0; --i) {
			const struct object_hit *o = &objects[i - 1];
			if (o->layer == layer)
				draw_column(w, column, &o->hit, o->texture, true);
		}
}

static void
render(struct rc_world *w)
{
	double cosine, sine;
	st_ddofx_rotation(w->angle % 360, &cosine, &sine);
	/* X increases westward in the SDK map, and Y increases southward. */
	struct ray r = {.x = w->x, .y = w->y, .forward_x = -sine, .forward_y = -cosine};
	for (int y = 80; y < 160; ++y) {
		double distance = 160.0 / (y + 0.5 - 80.0);
		double dx = -cosine * distance / 160.0, dy = sine * distance / 160.0;
		double x       = r.x + distance * r.forward_x - 159.5 * dx;
		double world_y = r.y + distance * r.forward_y - 159.5 * dy;
		for (int col = 0; col < 320; ++col, x += dx, world_y += dy) {
			int cx = (int)floor(x), cy = (int)floor(world_y);
			if (cx < 1 || cx > RC_WIDTH || cy < 1 || cy > RC_HEIGHT) {
				w->frame[y][col] = w->frame[159 - y][col] = 0;
				continue;
			}
			const struct rc_cell *c = &w->cells[cy - 1][cx - 1];
			int                   u = (int)((x - cx) * RC_TILE), v = (int)((world_y - cy) * RC_TILE);
			w->frame[y][col]       = w->flats[c->floor - 1][v][u];
			w->frame[159 - y][col] = w->flats[c->roof - 1][v][u];
		}
	}
	for (int col = 0; col < 320; ++col) {
		double side = (col - 159.5) / 160.0;
		r.dx        = r.forward_x - side * cosine;
		r.dy        = r.forward_y + side * sine;
		render_column(w, col, &r);
	}
	w->dirty = false;
}

static int
present(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *damage)
{
	struct st_ddofx *ddo = context;
	struct rc_world *w   = ddo->graphics->ray;
	if (pixels->width != WIDTH || pixels->height != HEIGHT)
		return 0;
	int top = w->bottom - w->height + 1;
	for (int y = 0; y < w->height; ++y) {
		size_t         at  = (top + y) * WIDTH + w->left;
		const uint8_t *row = w->frame[y * 160 / w->height];
		for (int x = 0; x < w->width; ++x)
			pixels->primary[at + x] = pixels->alternate[at + x] = row[x * 320 / w->width];
	}
	*damage = (struct st_console_rect) {w->left, top, w->width, w->height};
	st_ddofx_repaint_clocks(ddo, pixels, damage);
	st_ddofx_repaint_marquees(ddo, pixels, damage);
	return 0;
}

static void
copy_sprite(struct rc_world *w, int kind, int number, const struct st_ddofx_sprite *sprite)
{
	if (sprite && sprite->pixels && sprite->width == RC_TILE && sprite->height == RC_TILE) {
		memcpy(texture(w, kind, number), sprite->pixels, sizeof(tile));
		w->dirty = true;
	}
}

static void
animate(struct st_ddofx_graphics *g, uint64_t now)
{
	struct rc_world *w = g->ray;
	w->next_animation  = 0;
	for (int kind = 0; kind <= HIOBJ; ++kind)
		for (int slot = 0; slot < 23; ++slot) {
			struct rc_animation *a = &w->animations[kind][slot];
			if (!a->due)
				continue;
			if (a->due <= now) {
				copy_sprite(w, kind, slot + 1, &g->sprites[a->current]);
				if (!a->loop || a->first == a->last || (a->loop == 2 && a->current == a->last)) {
					a->due = 0;
					continue;
				}
				if (a->loop == 3 && (a->current == a->last || a->current == a->first))
					a->step = a->current == a->first ? 1 : -1;
				a->current += a->step;
				if (a->current > a->last)
					a->current = a->first;
				a->due = now + a->period * g->animator_divisor;
			}
			if (!w->next_animation || a->due < w->next_animation)
				w->next_animation = a->due;
		}
}

uint32_t
st_ddofx_ray_wait_ms(const struct st_ddofx *ddo)
{
	struct rc_world *w = ddo->graphics ? ddo->graphics->ray : NULL;
	uint64_t         now;
	if (!w || ddo->background_paused)
		return UINT32_MAX;
	uint64_t due = st_ddofx_ray_profile_due(ddo);
	if (w->active && w->next_frame < due)
		due = w->next_frame;
	if (w->next_animation && w->next_animation < due)
		due = w->next_animation;
	if (due == UINT64_MAX || st_time_monotonic_ms(&now))
		return UINT32_MAX;
	return due <= now ? 0 : (uint32_t)(due - now);
}

void
st_ddofx_ray_poll(struct st_ddofx *ddo)
{
	if (st_ddofx_ray_wait_ms(ddo))
		return;
	struct st_ddofx_graphics *g = ddo->graphics;
	struct rc_world          *w = g->ray;
	uint64_t                  now;
	if (st_time_monotonic_ms(&now))
		return;
	if (w->next_animation && now >= w->next_animation)
		animate(g, now);
	bool frame = w->active && now >= w->next_frame;
	if (frame)
		st_ddofx_ray_motion_frame(w);
	st_ddofx_ray_profile_poll(ddo, frame, now);
	if (!frame)
		return;
	w->next_frame = now + 55 * g->animator_divisor;
	if (w->left > WIDTH - w->width || w->bottom < w->height - 1)
		return;
	if (w->dirty)
		render(w);
	(void)st_console_modify_pixels(g->display, present, ddo);
}

static void
move(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, int speed, bool marker)
{
	struct rc_world *w = ddo->graphics->ray;
	double           cosine, sine;
	st_ddofx_rotation(w->angle % 360, &cosine, &sine);
	double dx = -sine * speed * (2.5 / 32), dy = -cosine * speed * (2.5 / 32);
	int    old_x = (int)w->x, old_y = (int)w->y;
	/* Check the swept segment so a diagonal step cannot cross a blocked
	 * corner or enter an interactive object before its host callback. */
	for (int i = 1; i <= 32; ++i) {
		int x = (int)(w->x + dx * i / 32), y = (int)(w->y + dy * i / 32);
		if (x <= 1 || x >= RC_WIDTH || y <= 1 || y >= RC_HEIGHT)
			return;
		const struct rc_cell *c = &w->cells[y - 1][x - 1];
		for (int layer = RC_LAYERS - 1; layer >= 0; --layer) {
			const struct rc_object *o = &c->objects[layer];
			if (!o->texture || !o->characteristic)
				continue;
			if (o->characteristic == 2) {
				char reply[24];
				snprintf(reply, sizeof(reply), "\x15%d,%d", x, y);
				w->active = false;
				if (!st_ddofx_capture(ddo, reply + 1, false))
					st_ddofx_query_reply(ddo, sink, reply, false);
			}
			return;
		}
		if (c->wall)
			return;
	}
	w->x += dx;
	w->y += dy;
	w->dirty = true;
	if (marker)
		st_ddofx_ray_marker(ddo->graphics, old_x, old_y);
}

bool
st_ddofx_ray_view_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *verb, char *args)
{
	enum {
		START,
		SCALE,
		ROTATE,
		MOVE,
		TEXTURE,
		OBJECT,
		DEFINE,
		PLAY,
		STOP
	};

	static const char *const names[] = {"5DANIM", "5DSCALE", "5VD", "5VM", "5CTEXTURE", "5COBJECT", "5VANIM",
	    "5PLAYV", "5STOPV"};
	unsigned                 op;
	for (op = 0; op < sizeof(names) / sizeof(*names); ++op)
		if (!ascii_case_compare(verb, names[op]))
			break;
	if (op == sizeof(names) / sizeof(*names))
		return false;
	struct rc_world *w = st_ddofx_ray_world(ddo->graphics);
	if (!w)
		return true;
	int32_t n[3];
	if (op == TEXTURE || op == OBJECT) {
		if (!args)
			return true;
		static const char *const kinds[] = {"FLCL", "LOWALL", "HIWALL", "LOOBJ", "HIOBJ"};
		const char              *type    = st_ddofx_value(ddo, st_ddofx_field(&args));
		int                      kind = op == OBJECT ? LOOBJ : FLCL, last = op == OBJECT ? HIOBJ : HIWALL;
		while (kind <= last && ascii_case_compare(type, kinds[kind]))
			++kind;
		if (kind <= last && st_ddofx_field_number(ddo, &args, n) && args && texture(w, kind, n[0]))
			copy_sprite(w, kind, n[0], st_ddofx_find_sprite(ddo->graphics, st_ddofx_value(ddo, args)));
		return true;
	}
	if (op == DEFINE || op == PLAY || op == STOP) {
		if (!st_ddofx_field_number(ddo, &args, n) || !st_ddofx_field_number(ddo, &args, n + 1)
		    || !texture(w, n[0], n[1]))
			return true;
		struct rc_animation *a = &w->animations[n[0]][n[1] - 1];
		if (op == DEFINE) {
			if (!st_ddofx_field_number(ddo, &args, n + 2) || (unsigned)n[2] > 3)
				return true;
			const char *first = st_ddofx_field(&args);
			const char *last  = st_ddofx_field(&args);
			if (!last || !args)
				return true;
			int     start = st_ddofx_sprite_slot(ddo->graphics, st_ddofx_value(ddo, first), false);
			int     end   = st_ddofx_sprite_slot(ddo->graphics, st_ddofx_value(ddo, last), false);
			int32_t fps;
			if (start >= 0 && end >= start && st_ddofx_numbers(ddo, args, &fps, 1) && fps >= 1
			    && fps <= 100)
				*a = (struct rc_animation) {.first = start,
				    .last                          = end,
				    .loop                          = n[2],
				    .period                        = (1000 + fps - 1) / fps};
		}
		else if (!args && op == STOP)
			a->due = 0;
		else if (!args && a->period) {
			a->current        = a->first;
			a->step           = 1;
			w->next_animation = a->due = 1;
		}
		return true;
	}
	if (!st_ddofx_numbers(ddo, args, n, op == START ? 3 : op == ROTATE ? 1 : 2))
		return true;
	switch (op) {
		case START:
			if (n[0] >= 0 && n[0] < WIDTH && n[1] >= 0 && n[1] < HEIGHT && (unsigned)n[2] <= 1) {
				w->left       = n[0];
				w->bottom     = n[1];
				w->active     = n[2] != 0;
				w->next_frame = 0;
			}
			break;
		case SCALE:
			if (n[0] > 0 && n[0] <= WIDTH && n[1] > 0 && n[1] <= HEIGHT && n[0] * n[1] <= 64000) {
				w->width  = n[0];
				w->height = n[1];
			}
			break;
		case ROTATE:
			if (w->active && n[0] >= -10 && n[0] <= 10) {
				w->angle = (w->angle + n[0] + 359) % 360 + 1;
				w->dirty = true;
			}
			break;
		case MOVE:
			if (w->active && (unsigned)n[0] <= 1 && n[1] >= -10 && n[1] <= 10 && n[1])
				move(ddo, sink, n[1], n[0] != 0);
			break;
	}
	return true;
}
