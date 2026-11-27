/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_polygon.h"

struct screen_point {
	double x, y;
};

struct projected_face {
	struct screen_point p[4]; /* The near plane can split one edge pair. */
	size_t              count, index;
	double              depth;
	uint8_t             colour;
};

static void
extend(struct bounds *b, int left, int right, int y)
{
	if (left < b->left)
		b->left = left;
	if (right > b->right)
		b->right = right;
	if (y < b->top)
		b->top = y;
	if (y > b->bottom)
		b->bottom = y;
}

static void
plot(struct st_ddofx_graphics *g, int x, int y, uint8_t colour)
{
	struct polygon_scene *s = g->polygons;
	g->page[WORK][y][x]     = colour;
	s->coverage[y][x / 8] |= (uint8_t)(1u << (x % 8));
	extend(&s->area, x, x, y);
}

static void
edge(struct st_ddofx_graphics *g, struct screen_point a, struct screen_point b, uint8_t colour)
{
	bool   steep = fabs(b.y - a.y) > fabs(b.x - a.x);
	double ax = steep ? a.y : a.x, bx = steep ? b.y : b.x;
	double ay = steep ? a.x : a.y, by = steep ? b.x : b.y;
	double lo = fmax(0, fmin(ax, bx)), hi = fmin(steep ? HEIGHT - 1 : WIDTH - 1, fmax(ax, bx));
	if (lo > hi)
		return;
	for (int i = (int)ceil(lo); i <= (int)floor(hi); ++i) {
		double y = round(ax == bx ? ay : ay + (i - ax) / (bx - ax) * (by - ay));
		if (y >= 0 && y < (steep ? WIDTH : HEIGHT))
			plot(g, steep ? (int)y : i, steep ? i : (int)y, colour);
	}
}

static void
paint(struct st_ddofx_graphics *g, const struct projected_face *f, bool wire, uint8_t colour)
{
	double ymin = HEIGHT, ymax = -1;
	for (size_t i = 0; i < f->count; ++i) {
		edge(g, f->p[i], f->p[(i + 1) % f->count], colour);
		ymin = fmin(ymin, f->p[i].y);
		ymax = fmax(ymax, f->p[i].y);
	}
	if (wire || ymin > HEIGHT - 1 || ymax < 0)
		return;
	int bottom = (int)fmin(ymax, HEIGHT - 1);
	for (int y = (int)ceil(fmax(ymin, 0)); y <= bottom; ++y) {
		double xmin = WIDTH, xmax = -1;
		for (size_t i = 0; i < f->count; ++i) {
			struct screen_point a = f->p[i], b = f->p[(i + 1) % f->count];
			if ((a.y > y) == (b.y > y))
				continue;
			double x = a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x);
			xmin     = fmin(xmin, x);
			xmax     = fmax(xmax, x);
		}
		if (xmin > WIDTH - 1 || xmax < 0)
			continue;
		int left = (int)ceil(fmax(0, xmin)), right = (int)fmin(WIDTH - 1, xmax);
		if (left > right)
			continue;
		struct polygon_scene *s = g->polygons;
		memset(&g->page[WORK][y][left], colour, (size_t)(right - left + 1));
		extend(&s->area, left, right, y);
		unsigned first = 0xffu << (left % 8), last = 0xffu >> (7 - right % 8);
		uint8_t *row = s->coverage[y];
		if (left / 8 == right / 8)
			row[left / 8] |= (uint8_t)(first & last);
		else {
			row[left / 8] |= (uint8_t)first;
			row[right / 8] |= (uint8_t)last;
			memset(row + left / 8 + 1, 0xff, (size_t)(right / 8 - left / 8 - 1));
		}
	}
}

struct rotation {
	double sy, cy, sr, cr, sp, cp;
};

static struct mesh_point
transform(struct mesh_point p, const struct polygon_object *o, const struct rotation *r)
{
	double tx = r->cy * p.x - r->sy * p.z, tz = r->sy * p.x + r->cy * p.z;
	double x = r->cr * tx - r->sr * p.y, ty = r->cr * p.y + r->sr * tx;
	return (struct mesh_point) {x + o->position[0], r->sp * tz + r->cp * ty + o->position[1],
	    r->cp * tz - r->sp * ty + o->position[2]};
}

static struct mesh_point
interpolate(struct mesh_point a, struct mesh_point b, double t)
{
	return (struct mesh_point) {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

static void
project(struct projected_face *f, struct mesh_point p, double distance)
{
	f->p[f->count++] =
	    (struct screen_point) {nearbyint(distance * p.x / p.z), nearbyint(distance * p.y / p.z)};
}

static void
prepare(struct projected_face *f, const struct polygon_object *o, const struct rotation *r, size_t index)
{
	const struct mesh      *m    = &o->geometry;
	const struct mesh_face *face = &m->face[index];
	*f                           = (struct projected_face) {.index = index, .colour = face->colour};
	struct mesh_point p[3];
	for (unsigned j = 0; j < 3; ++j) {
		p[j] = transform(m->point[face->vertex[j]], o, r);
		f->depth += p[j].z;
	}
	/* Visible space is negative Z. Clip before dividing so a triangle
	 * crossing the camera never projects through infinity. */
	for (unsigned j = 0; j < 3; ++j) {
		struct mesh_point prev = p[(j + 2) % 3], cur = p[j];
		if ((cur.z <= -1) != (prev.z <= -1)) {
			struct mesh_point cut = interpolate(prev, cur, (-1 - prev.z) / (cur.z - prev.z));
			cut.z                 = -1;
			project(f, cut, o->distance);
		}
		if (cur.z <= -1)
			project(f, cur, o->distance);
	}
}

static int
depth_order(const void *va, const void *vb)
{
	const struct projected_face *a = va, *b = vb;
	if (a->depth != b->depth)
		return a->depth > b->depth ? 1 : -1;
	return (a->index > b->index) - (a->index < b->index);
}

static void
draw_object(struct st_ddofx_graphics *g, const struct polygon_object *o)
{
	struct polygon_scene *s     = g->polygons;
	size_t                count = o->geometry.faces;
	if (!count)
		return;
	if (count > s->capacity) {
		struct projected_face *p = realloc(s->projected, count * sizeof(*p));
		if (!p)
			return;
		s->projected = p;
		s->capacity  = count;
	}
	const double    radians = 0.01745329251994329577;
	double          yaw = radians * o->angle[0], roll = radians * o->angle[1], pitch = radians * o->angle[2];
	struct rotation r = {sin(yaw), cos(yaw), sin(roll), cos(roll), sin(pitch), cos(pitch)};
	for (size_t i = 0; i < count; ++i)
		prepare(&s->projected[i], o, &r, i);
	qsort(s->projected, count, sizeof(*s->projected), depth_order);
	for (size_t i = 0; i < count; ++i) {
		const struct projected_face *f     = &s->projected[i];
		int                          shade = (int)fmin(15, nearbyint(16.0 * (i + 1) / count));
		if (f->count >= 3)
			paint(g, f, o->wire, (uint8_t)(f->colour - shade));
	}
}

static int
present(void *ctx, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	struct st_ddofx_graphics *g = ctx;
	struct polygon_scene     *s = g->polygons;
	struct bounds             b = s->area;
	if (pixels->width != WIDTH || pixels->height != HEIGHT || b.left > b.right)
		return 0;
	for (int y = b.top; y <= b.bottom; ++y)
		for (int i = b.left / 8; i <= b.right / 8; ++i) {
			unsigned bits = s->damage[y][i] | s->coverage[y][i];
			for (int x = i * 8; bits; ++x, bits >>= 1)
				if (bits & 1)
					pixels->primary[y * WIDTH + x] = pixels->alternate[y * WIDTH + x] =
					    g->page[WORK][y][x];
		}
	*changed = (struct st_console_rect) {b.left, b.top, b.right - b.left + 1, b.bottom - b.top + 1};
	return 0;
}

void
st_ddofx_render_polygons(struct st_ddofx_graphics *g)
{
	struct polygon_scene *s   = g->polygons;
	struct bounds         old = s->area;
	memcpy(s->damage, s->coverage, sizeof(s->damage));
	for (int y = old.top; y <= old.bottom; ++y)
		for (int i = old.left / 8; i <= old.right / 8; ++i) {
			unsigned bits     = s->coverage[y][i];
			s->coverage[y][i] = 0;
			for (int x = i * 8; bits; ++x, bits >>= 1)
				if (bits & 1)
					g->page[WORK][y][x] = g->page[BACKGROUND][y][x];
		}
	s->area = (struct bounds) {WIDTH, -1, HEIGHT, -1};
	for (unsigned i = 0; i < POLYGON_SLOTS; ++i)
		if (s->object[i] && s->object[i]->playing)
			draw_object(g, s->object[i]);
	struct bounds current = s->area;
	if (old.left < current.left)
		s->area.left = old.left;
	if (old.right > current.right)
		s->area.right = old.right;
	if (old.top < current.top)
		s->area.top = old.top;
	if (old.bottom > current.bottom)
		s->area.bottom = old.bottom;
	if (s->area.left <= s->area.right)
		(void)st_console_modify_pixels(g->display, present, g);
	s->area = current;
}
