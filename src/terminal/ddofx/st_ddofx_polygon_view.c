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

struct polygon_raster {
	struct st_ddofx *ddo;
	unsigned         owner;
	int              width, height, mask_stride;
	uint8_t         *canvas, *coverage, *previous;
	struct bounds    area;
};

static void
plot(struct polygon_raster *r, int x, int y, uint8_t colour)
{
	r->canvas[(size_t)y * r->width + x] = colour;
	r->coverage[(size_t)y * r->mask_stride + x / 8] |= (uint8_t)(1u << (x % 8));
	extend(&r->area, x, x, y);
}

static void
edge(struct polygon_raster *r, struct screen_point a, struct screen_point b, uint8_t colour)
{
	bool   steep = fabs(b.y - a.y) > fabs(b.x - a.x);
	double ax = steep ? a.y : a.x, bx = steep ? b.y : b.x;
	double ay = steep ? a.x : a.y, by = steep ? b.x : b.y;
	double lo = fmax(0, fmin(ax, bx)), hi = fmin(steep ? r->height - 1 : r->width - 1, fmax(ax, bx));
	if (lo > hi)
		return;
	for (int i = (int)ceil(lo); i <= (int)floor(hi); ++i) {
		double y = round(ax == bx ? ay : ay + (i - ax) / (bx - ax) * (by - ay));
		if (y >= 0 && y < (steep ? r->width : r->height))
			plot(r, steep ? (int)y : i, steep ? i : (int)y, colour);
	}
}

static void
paint(struct polygon_raster *r, const struct projected_face *f, bool wire, uint8_t colour)
{
	double ymin = r->height, ymax = -1;
	for (size_t i = 0; i < f->count; ++i) {
		edge(r, f->p[i], f->p[(i + 1) % f->count], colour);
		ymin = fmin(ymin, f->p[i].y);
		ymax = fmax(ymax, f->p[i].y);
	}
	if (wire || ymin > r->height - 1 || ymax < 0)
		return;
	int bottom = (int)fmin(ymax, r->height - 1);
	for (int y = (int)ceil(fmax(ymin, 0)); y <= bottom; ++y) {
		double xmin = r->width, xmax = -1;
		for (size_t i = 0; i < f->count; ++i) {
			struct screen_point a = f->p[i], b = f->p[(i + 1) % f->count];
			if ((a.y > y) == (b.y > y))
				continue;
			double x = a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x);
			xmin     = fmin(xmin, x);
			xmax     = fmax(xmax, x);
		}
		if (xmin > r->width - 1 || xmax < 0)
			continue;
		int left = (int)ceil(fmax(0, xmin)), right = (int)fmin(r->width - 1, xmax);
		if (left > right)
			continue;
		memset(r->canvas + (size_t)y * r->width + left, colour, (size_t)(right - left + 1));
		extend(&r->area, left, right, y);
		unsigned first = 0xffu << (left % 8), last = 0xffu >> (7 - right % 8);
		uint8_t *row = r->coverage + (size_t)y * r->mask_stride;
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
draw_object(struct polygon_raster *raster, const struct polygon_object *o)
{
	struct polygon_scene *s     = raster->ddo->graphics->polygons;
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
			paint(raster, f, o->wire, (uint8_t)(f->colour - shade));
	}
}

static int
present(void *ctx, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	const struct polygon_raster *r      = ctx;
	struct bounds                damage = {WIDTH, -1, HEIGHT, -1};
	if (pixels->width != WIDTH || pixels->height != HEIGHT)
		return 0;
	struct bounds view = r->area;
	if (r->owner) {
		const struct st_ddofx_graphic_window *w        = &r->ddo->graphics->graphic_windows[r->owner];
		struct bounds                         interior = st_ddofx_window_interior(w);
		int                                   right    = w->x + interior.right - interior.left;
		int                                   bottom   = w->y + interior.bottom - interior.top;
		if (view.left < w->x)
			view.left = w->x;
		if (view.top < w->y)
			view.top = w->y;
		if (view.right > right)
			view.right = right;
		if (view.bottom > bottom)
			view.bottom = bottom;
	}
	for (int y = view.top; y <= view.bottom; ++y) {
		const uint8_t *old     = r->previous + (size_t)y * r->mask_stride;
		const uint8_t *current = r->coverage + (size_t)y * r->mask_stride;
		int            start   = -1;
		for (int x = view.left; x <= view.right + 1; ++x) {
			bool painted = x <= view.right && ((old[x / 8] | current[x / 8]) & (1u << (x % 8)));
			if (painted && start < 0)
				start = x;
			if (!painted && start >= 0) {
				st_ddofx_publish_page_area(r->ddo, r->owner, WORK, pixels,
				    (struct bounds) {start, x - 1, y, y}, &damage);
				start = -1;
			}
		}
	}
	if (damage.left <= damage.right)
		*changed = (struct st_console_rect) {damage.left, damage.top, damage.right - damage.left + 1,
		    damage.bottom - damage.top + 1};
	return 0;
}

void
st_ddofx_render_polygons(struct st_ddofx *ddo, unsigned owner)
{
	struct st_ddofx_graphics *g    = ddo->graphics;
	struct polygon_scene     *s    = g->polygons;
	struct polygon_page      *page = &s->page[owner];
	if (owner && !g->graphic_windows[owner].visual)
		return;
	struct st_ddofx_surface canvas     = st_ddofx_surface(g, owner, owner ? VISUAL : WORK);
	struct st_ddofx_surface background = st_ddofx_surface(g, owner, owner ? HIDDEN : BACKGROUND);
	struct polygon_raster   r          = {
	    .ddo         = ddo,
	    .owner       = owner,
	    .width       = canvas.width,
	    .height      = canvas.height,
	    .mask_stride = (canvas.width + 7) / 8,
	    .canvas      = canvas.data,
	    .area        = {canvas.width, -1, canvas.height, -1}
        };
	size_t mask_size = (size_t)r.mask_stride * r.height;
	if (!page->bits) {
		page->bits = calloc(2, mask_size);
		if (!page->bits)
			return;
		page->area = r.area;
	}
	/* Alternate masks so the old footprint stays available for publication. */
	r.previous        = page->bits + (page->second_mask ? mask_size : 0);
	page->second_mask = !page->second_mask;
	r.coverage        = page->bits + (page->second_mask ? mask_size : 0);
	struct bounds old = page->area;
	for (int y = old.top; y <= old.bottom; ++y) {
		size_t pos = (size_t)y * r.mask_stride;
		for (int i = old.left / 8; i <= old.right / 8; ++i) {
			unsigned bits = r.previous[pos + i];
			for (int x = 8 * i; bits; ++x, bits >>= 1) {
				if (bits & 1) {
					size_t at    = (size_t)y * r.width + x;
					r.canvas[at] = background.data[at];
				}
			}
		}
	}
	for (unsigned i = 0; i < POLYGON_SLOTS; ++i) {
		const struct polygon_object *o = s->object[i];
		if (o && o->playing && o->owner == owner)
			draw_object(&r, o);
	}
	page->area = r.area;
	if (old.left <= old.right) {
		extend(&r.area, old.left, old.right, old.top);
		extend(&r.area, old.left, old.right, old.bottom);
	}
	if (r.area.left <= r.area.right && (!owner || !g->graphic_windows[owner].hidden))
		(void)st_console_modify_pixels(g->display, present, &r);
	for (int y = old.top; y <= old.bottom; ++y)
		memset(r.previous + (size_t)y * r.mask_stride + old.left / 8, 0, old.right / 8 - old.left / 8 + 1);
}
