/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_script.h"
#include "string_util.h"

struct drawing {
	struct st_ddofx_graphics *graphics;
	uint8_t *pixels;
	int stride;
	struct bounds screen;
	struct bounds *clip;
};

static bool
inside(const struct bounds *b, int64_t x, int64_t y)
{
	return x >= b->left && x <= b->right && y >= b->top && y <= b->bottom;
}

static void
plot(const struct drawing *d, const struct bounds *b, int64_t x, int64_t y, bool xor)
{
	if (inside(b, x, y)) {
		uint8_t *p = &d->pixels[y * d->stride + x];
		*p         = xor ? *p ^ d->graphics->colour : d->graphics->colour;
	}
}

/* Step only the visible part of the major axis. Its distance from the
 * original endpoint determines both rounding and the phase of the dashes. */
static void
line(const struct drawing *d, const struct bounds *b, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
    uint16_t pattern, bool xor, bool skip_first)
{
	int64_t  dx = (int64_t)x2 - x1, dy = (int64_t)y2 - y1;
	uint64_t ax = (uint64_t)llabs(dx), ay = (uint64_t)llabs(dy);
	bool     horizontal = ax >= ay;
	uint64_t major = horizontal ? ax : ay, minor = horizontal ? ay : ax;
	int64_t  origin = horizontal ? x1 : y1;
	int64_t  last   = horizontal ? x2 : y2;
	int64_t  start = origin < last ? origin : last, end = origin < last ? last : origin;
	int      low = horizontal ? b->left : b->top, high = horizontal ? b->right : b->bottom;
	if (start < low)
		start = low;
	if (end > high)
		end = high;
	for (int64_t a = start; a <= end; ++a) {
		uint64_t step = (uint64_t)llabs(a - origin);
		if ((skip_first && step == 0) || !(pattern & (0x8000u >> (step % 16))))
			continue;
		int64_t offset = major ? (int64_t)((step * minor + major / 2) / major) : 0;
		int64_t x      = horizontal ? a : x1 + (dx < 0 ? -offset : offset);
		int64_t y      = horizontal ? y1 + (dy < 0 ? -offset : offset) : a;
		plot(d, b, x, y, xor);
	}
}

static void
cursor_or_line(struct st_ddofx *ddo, const struct drawing *d, const char *op, char *args)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	int32_t                   nx, ny;
	if (!st_ddofx_field_number(ddo, &args, &nx) || !st_ddofx_field_number(ddo, &args, &ny))
		return;
	uint16_t pattern = UINT16_MAX;
	bool     dashed  = !strcmp(op, "DRDASH") || !strcmp(op, "DRDSHRL");
	if (dashed) {
		const char *p = args ? st_ddofx_value(ddo, args) : "";
		if (strlen(p) != 16 || strspn(p, "01") != 16)
			return;
		pattern = 0;
		for (unsigned i = 0; i < 16; ++i)
			pattern = (uint16_t)((pattern << 1) | (p[i] == '1'));
	}
	else if (args != NULL)
		return;
	if (!strcmp(op, "DRAWREL") || !strcmp(op, "MVREL") || !strcmp(op, "DRDSHRL")) {
		int64_t x = (int64_t)g->cx + nx, y = (int64_t)g->cy + ny;
		if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX)
			return;
		nx = (int32_t)x;
		ny = (int32_t)y;
	}
	if (op[0] != 'M')
		line(d, d->clip, g->cx, g->cy, nx, ny, pattern, !strcmp(op, "DRAWX"), false);
	g->cx = nx;
	g->cy = ny;
}

static void
rectangle(const struct drawing *d, const char *op, const int32_t n[4])
{
	struct st_ddofx_graphics *g = d->graphics;
	struct bounds box = {n[0], n[1], n[2], n[3]};
	if (box.left < 0 || box.left >= box.right || box.right > d->screen.right || box.top < 0 || box.top >= box.bottom
	    || box.bottom > d->screen.bottom)
		return;
	if (!strcmp(op, "CLIP")) {
		*d->clip = box;
		return;
	}
	const struct bounds *clip = !strcmp(op, "RECT") ? &d->screen : d->clip;
	int                  x1   = box.left < clip->left ? clip->left : box.left;
	int                  x2   = box.right > clip->right ? clip->right : box.right;
	int                  y1   = box.top < clip->top ? clip->top : box.top;
	int                  y2   = box.bottom > clip->bottom ? clip->bottom : box.bottom;
	if (x1 > x2)
		return;
	bool outlined = op[0] == 'D', xor = !strcmp(op, "DRWBXX");
	for (int y = y1; y <= y2; ++y) {
		if (!outlined) {
			memset(d->pixels + y * d->stride + x1, g->colour, (size_t)(x2 - x1 + 1));
			continue;
		}
		for (int x = x1; x <= x2; ++x) {
			if (y - box.top < g->horizontal_thickness || box.bottom - y < g->horizontal_thickness
			    || x - box.left < g->vertical_thickness || box.right - x < g->vertical_thickness)
				plot(d, clip, x, y, xor);
		}
	}
}

static void
ellipse(const struct drawing *d, int32_t rx, int32_t ry, bool filled)
{
	struct st_ddofx_graphics *g = d->graphics;
	if (rx <= 0 || ry <= 0)
		return;
	const struct bounds *b = d->clip;
	for (int y = b->top; y <= b->bottom; ++y) {
		int64_t dy = (int64_t)y - g->cy;
		if (llabs(dy) > ry)
			continue;
		double  t      = (double)dy / ry;
		int64_t offset = (int64_t)(rx * sqrt(1 - t * t) + 0.5);
		int64_t left = g->cx - offset, right = g->cx + offset;
		if (filled) {
			if (left < b->left)
				left = b->left;
			if (right > b->right)
				right = b->right;
			if (left <= right)
				memset(d->pixels + y * d->stride + left, g->colour, (size_t)(right - left + 1));
		}
		else {
			plot(d, b, left, y, false);
			plot(d, b, right, y, false);
		}
	}
	if (filled)
		return;
	/* Sample the horizontal axis as well so shallow arcs stay connected. */
	for (int x = b->left; x <= b->right; ++x) {
		int64_t dx = (int64_t)x - g->cx;
		if (llabs(dx) > rx)
			continue;
		double  t      = (double)dx / rx;
		int64_t offset = (int64_t)(ry * sqrt(1 - t * t) + 0.5);
		plot(d, b, x, g->cy - offset, false);
		plot(d, b, x, g->cy + offset, false);
	}
}

void
st_ddofx_flood(uint8_t *data, int stride, const struct bounds *b, int32_t x, int32_t y, uint8_t colour)
{
	if (!inside(b, x, y))
		return;
	uint8_t original = data[y * stride + x];
	if (original == colour)
		return;
	size_t    capacity = (size_t)(b->right - b->left + 1) * (b->bottom - b->top + 1);
	uint32_t *todo     = malloc(capacity * sizeof(*todo));
	if (!todo)
		return;
	data[y * stride + x] = colour;
	todo[0]              = (uint32_t)(y * stride + x);
	size_t count        = 1;
	while (count) {
		uint32_t         pixel = todo[--count];
		int              px = (int)(pixel % stride), py = (int)(pixel / stride);
		static const int offsets[4][2] = {
		    {-1, 0 },
                    {1,  0 },
                    {0,  -1},
                    {0,  1 }
                };
		for (unsigned i = 0; i < 4; ++i) {
			int nx = px + offsets[i][0], ny = py + offsets[i][1];
			if (inside(b, nx, ny) && data[ny * stride + nx] == original) {
				/* Mark at insertion so each pixel occupies at most one slot. */
				data[ny * stride + nx] = colour;
				todo[count++]          = (uint32_t)(ny * stride + nx);
			}
		}
	}
	free(todo);
}

int32_t
st_ddofx_bezier_coordinate(const int32_t *p, double t)
{
	double s     = 1 - t;
	double value = s * s * s * p[0] + 3 * s * s * t * p[2] + 3 * s * t * t * p[4] + t * t * t * p[6];
	if (value >= INT32_MAX)
		return INT32_MAX;
	if (value <= INT32_MIN)
		return INT32_MIN;
	return (int32_t)round(value);
}

static void
bezier(const struct drawing *d, const int32_t *n, bool xor)
{
	struct st_ddofx_graphics *g = d->graphics;
	if (n[8] <= 0)
		return;
	/* The SDK's resolution is a DOS integer. */
	int     steps = n[8] > INT16_MAX ? INT16_MAX : n[8];
	int32_t x = n[0], y = n[1];
	for (int i = 1; i <= steps; ++i) {
		double  t  = (double)i / steps;
		int32_t nx = st_ddofx_bezier_coordinate(n, t), ny = st_ddofx_bezier_coordinate(n + 1, t);
		line(d, &d->screen, x, y, nx, ny, UINT16_MAX, xor, i != 1);
		x = nx;
		y = ny;
	}
	g->cx = n[6];
	g->cy = n[7];
}

void
st_ddofx_draw(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *verb, char *args)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	struct st_ddofx_graphic_window *w = &g->graphic_windows[g->alert_graphic_window];
	struct drawing target = {g, (uint8_t *)g->page[WORK], WIDTH, {0, WIDTH - 1, 0, HEIGHT - 1}, &g->clip};
	if (w->visual)
		target = (struct drawing){g, w->visual, w->width, {0, w->width - 1, 0, w->height - 1}, &w->clip};
	char                      op[8];
	size_t                    len = strlen(verb);
	if (len >= sizeof(op))
		return;
	for (size_t i = 0; i <= len; ++i)
		op[i] = (char)ascii_to_upper((unsigned char)verb[i]);
	int32_t n[9];
	if (!strcmp(op, "P") || !strcmp(op, "PX") || !strcmp(op, "G")) {
		if (!st_ddofx_numbers(ddo, args, n, 2) || !inside(&target.screen, n[0], n[1]))
			return;
		if (op[0] == 'G') {
			char reply[4];
			snprintf(reply, sizeof(reply), "%u", (unsigned)target.pixels[n[1] * target.stride + n[0]]);
			st_ddofx_query_reply(ddo, sink, reply, true);
		}
		else
			plot(&target, target.clip, n[0], n[1], op[1] != 0);
	}
	else if ((!strcmp(op, "FILL") || !strcmp(op, "ERASE")) && args == NULL)
		memset(target.pixels, op[0] == 'F' ? g->colour : 0, (size_t)target.stride * (target.screen.bottom + 1));
	else if (!strcmp(op, "M") || !strcmp(op, "MVREL") || !strcmp(op, "DRAW") || !strcmp(op, "DRAWX")
	         || !strcmp(op, "DRAWREL") || !strcmp(op, "DRDASH") || !strcmp(op, "DRDSHRL"))
		cursor_or_line(ddo, &target, op, args);
	else if ((!strcmp(op, "CLIP") || !strcmp(op, "RECT") || !strcmp(op, "CLPRCT") || !strcmp(op, "DRWBOX")
	             || !strcmp(op, "DRWBXX"))
	         && st_ddofx_numbers(ddo, args, n, 4))
		rectangle(&target, op, n);
	else if (!strcmp(op, "BOXLNS") && st_ddofx_numbers(ddo, args, n, 2) && n[0] >= 0 && n[1] >= 0) {
		g->horizontal_thickness = n[0];
		g->vertical_thickness   = n[1];
	}
	else if ((!strcmp(op, "CIRC") || !strcmp(op, "CIRCF")) && st_ddofx_numbers(ddo, args, n, 1))
		ellipse(&target, n[0], n[0], op[len - 1] == 'F');
	else if ((!strcmp(op, "ELIPS") || !strcmp(op, "ELIPSF")) && st_ddofx_numbers(ddo, args, n, 2))
		ellipse(&target, n[0], n[1], op[len - 1] == 'F');
	else if ((!strcmp(op, "PAINT") || !strcmp(op, "FLOOD")) && st_ddofx_numbers(ddo, args, n, 2))
		st_ddofx_flood(target.pixels, target.stride, op[0] == 'P' ? &target.screen : target.clip, n[0], n[1],
		    g->colour);
	else if ((!strcmp(op, "BCURV") || !strcmp(op, "BCURVX")) && st_ddofx_numbers(ddo, args, n, 9))
		bezier(&target, n, op[len - 1] == 'X');
}
