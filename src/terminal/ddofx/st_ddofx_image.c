/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_script.h"
#include "string_util.h"

enum operation {
	SCALE,
	ROTATE,
	OUTLINE,
	ALIAS
};

struct image_op {
	struct st_ddofx_graphics     *g;
	enum operation                op;
	enum page                     src, dst;
	struct bounds                 box, clip, damage;
	int32_t                       n[10];
	const struct st_pixel_buffer *screen;
	const uint32_t               *palette;
};

static uint8_t
read_index(const struct image_op *im, int x, int y)
{
	if (im->src == VISUAL)
		return st_console_palette_index256(im->palette, im->screen->primary[y * WIDTH + x]);
	return im->g->page[im->src][y][x];
}

static void
write_index(struct image_op *im, int x, int y, uint8_t colour)
{
	if (x < im->clip.left || x > im->clip.right || y < im->clip.top || y > im->clip.bottom)
		return;
	if (im->dst != VISUAL) {
		im->g->page[im->dst][y][x] = colour;
		return;
	}
	size_t offset               = (size_t)y * WIDTH + x;
	im->screen->primary[offset] = im->screen->alternate[offset] = colour;
	if (im->damage.left > x)
		im->damage.left = x;
	if (im->damage.right < x)
		im->damage.right = x;
	if (im->damage.top > y)
		im->damage.top = y;
	if (im->damage.bottom < y)
		im->damage.bottom = y;
}

static int
sample(int dst, int source_len, int dest_len)
{
	if (source_len >= dest_len)
		return dst * source_len / dest_len;
	return ((dst + 1) * source_len - 1) / dest_len;
}

static void
scale(struct image_op *im)
{
	struct bounds b = im->box;
	int           w = b.right - b.left + 1, h = b.bottom - b.top + 1, nw = im->n[4], nh = im->n[5];
	if (nw < 1 || nw > WIDTH - b.left || nh < 1 || nh > b.bottom + 1)
		return;

	uint8_t *source = malloc((size_t)w * h);
	if (!source)
		return;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			source[y * w + x] = read_index(im, b.left + x, b.bottom - y);
			write_index(im, b.left + x, b.bottom - y, 0);
		}
	/* The lower-left pixel anchors both sampling axes. */
	for (int y = 0; y < nh; ++y) {
		const uint8_t *row = source + sample(y, h, nh) * w;
		for (int x = 0; x < nw; ++x)
			write_index(im, b.left + x, b.bottom - y, row[sample(x, w, nw)]);
	}
	free(source);
}

void
st_ddofx_rotation(int angle, double *cosine, double *sine)
{
	if (angle % 90 == 0) {
		static const int quarter[] = {0, 1, 0, -1, 0};
		*sine                      = quarter[angle / 90];
		*cosine                    = quarter[angle / 90 + 1];
	}
	else {
		double radians = angle * (3.14159265358979323846 / 180.0);
		*cosine        = cos(radians);
		*sine          = sin(radians);
	}
}

static void
rotate(struct image_op *im)
{
	struct bounds b     = im->box;
	int           angle = (unsigned)im->n[4] <= 359 ? im->n[4] : 0;
	double        ox    = im->n[5] == -1 ? b.left + nearbyint((b.right - b.left) * 0.5) : im->n[5];
	double        oy    = im->n[6] == -1 ? b.top + nearbyint((b.bottom - b.top) * 0.5) : im->n[6];
	double        dx = im->n[8] == -1 ? ox : im->n[8], dy = im->n[9] == -1 ? oy : im->n[9];
	double        cs, sn;
	st_ddofx_rotation(angle, &cs, &sn);
	for (int x = b.left; x <= b.right; ++x)
		for (int y = b.top; y <= b.bottom; ++y) {
			uint8_t index = read_index(im, x, y);
			if (index == im->n[7])
				continue;
			double u = cs * (x - ox) - sn * (y - oy), v = sn * (x - ox) + cs * (y - oy);
			double tx = dx + round(u), ty = dy + round(v);
			if (tx < 0 || tx >= WIDTH || ty < 0 || ty >= HEIGHT)
				continue;
			write_index(im, (int)tx, (int)ty, index);
			/* At oblique angles the second sample closes gaps between pixels. */
			write_index(im, (int)(dx + trunc(u)), (int)(dy + trunc(v)), index);
		}
}

static void
outline(struct image_op *im)
{
	struct bounds b = im->box;
	for (int y = b.top; y <= b.bottom; ++y)
		for (int x = b.left; x <= b.right; ++x) {
			if (read_index(im, x, y) != im->n[4])
				continue;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx) {
					int xx = x + dx, yy = y + dy;
					if (xx >= b.left && xx <= b.right && yy >= b.top && yy <= b.bottom
					    && read_index(im, xx, yy) != im->n[4])
						write_index(im, xx, yy, 255);
				}
		}
	for (int y = b.top; y <= b.bottom; ++y)
		for (int x = b.left; x <= b.right; ++x)
			if (read_index(im, x, y) == 255)
				write_index(im, x, y, (uint8_t)im->n[5]);
}

struct rgb {
	int r, g, b;
};

static void
smooth(struct image_op *im)
{
	struct rgb dac[256];
	for (unsigned i = 0; i < 256; ++i) {
		uint32_t rgb = im->palette[i];
		dac[i]       = (struct rgb) {(rgb >> 18) & 63, (rgb >> 10) & 63, (rgb >> 2) & 63};
	}
	struct bounds b = im->box;
	for (int x = b.left; x <= b.right; ++x)
		for (int y = b.top; y <= b.bottom; ++y) {
			uint8_t index = read_index(im, x, y);
			if (im->n[4] >= 0 && im->n[4] != index)
				continue;
			/* A run through either axis is already a smooth edge. */
			if (((x ? read_index(im, x - 1, y) : 0) == index
			        && (x + 1 < WIDTH ? read_index(im, x + 1, y) : 0) == index)
			    || ((y ? read_index(im, x, y - 1) : 0) == index
			        && (y + 1 < HEIGHT ? read_index(im, x, y + 1) : 0) == index))
				continue;
			struct rgb center = dac[index], avg = center;
			int        count = 1;
			for (int dx = -1; dx <= 1; dx += 2)
				for (int dy = -1; dy <= 1; dy += 2) {
					if ((unsigned)(x + dx) >= WIDTH || (unsigned)(y + dy) >= HEIGHT)
						continue;
					struct rgb p = dac[read_index(im, x + dx, y + dy)];
					avg.r += p.r;
					avg.g += p.g;
					avg.b += p.b;
					++count;
				}
			avg.r /= count;
			avg.g /= count;
			avg.b /= count;
			int quality = im->n[5];
			if (abs(center.r - avg.r) < quality && abs(center.g - avg.g) < quality
			    && abs(center.b - avg.b) < quality)
				continue;
			int best = 256, nearest = 0;
			for (int i = 0; i < 256; ++i) {
				int distance =
				    (abs(dac[i].r - avg.r) + abs(dac[i].g - avg.g) + abs(dac[i].b - avg.b)) / 3;
				if (distance < best) {
					best    = distance;
					nearest = i;
					if (!distance)
						break;
				}
			}
			write_index(im, x, y, (uint8_t)nearest);
		}
}

static int
apply(void *context, const struct st_pixel_buffer *pixels, const uint32_t *palette,
    struct st_console_rect *changed)
{
	struct image_op *im = context;
	if (pixels && (pixels->width != WIDTH || pixels->height != HEIGHT))
		return 0;
	im->screen  = pixels;
	im->palette = palette;
	switch (im->op) {
		case SCALE:
			scale(im);
			break;
		case ROTATE:
			rotate(im);
			break;
		case OUTLINE:
			outline(im);
			break;
		case ALIAS:
			smooth(im);
			break;
	}
	struct bounds d = im->damage;
	if (changed && d.right >= d.left)
		*changed = (struct st_console_rect) {d.left, d.top, d.right - d.left + 1, d.bottom - d.top + 1};
	return 0;
}

static bool
page_field(struct st_ddofx *ddo, char **args, enum page *page)
{
	const char *name = st_ddofx_field(args);
	if (!name)
		return false;
	name = st_ddofx_value(ddo, name);
	return strlen(name) == 2 && st_ddofx_page_name(name, page);
}

void
st_ddofx_gadget_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *body)
{
	char           *args = body;
	const char     *verb = st_ddofx_field(&args);
	struct image_op im   = {
	    .g      = ddo->graphics,
	    .damage = {WIDTH, -1, HEIGHT, -1}
        };
	if (st_ddofx_text_field_command(ddo, sink, verb, args) || st_ddofx_choice_command(ddo, verb, args) || st_ddofx_marquee_command(ddo, sink, verb, args)
	    || st_ddofx_clock_command(ddo, sink, verb, args) || st_ddofx_movie_command(ddo, sink, verb, args))
		return;
	if (!ascii_case_compare(verb, "5CPUSHB")) {
		st_ddofx_define_button(ddo, args);
		return;
	}
	if (!ascii_case_compare(verb, "5SCALE"))
		im.op = SCALE;
	else if (!ascii_case_compare(verb, "5ROTATE"))
		im.op = ROTATE;
	else if (!ascii_case_compare(verb, "5ALIAS"))
		im.op = ALIAS;
	else if (!ascii_case_compare(verb, "5OUTLN"))
		im.op = OUTLINE;
	else {
		st_ddofx_grid_command(ddo, verb, args);
		return;
	}
	if (!page_field(ddo, &args, &im.src))
		return;
	im.dst = im.src;
	if (im.op == ROTATE && (!page_field(ddo, &args, &im.dst) || im.src == im.dst))
		return;
	if (!st_ddofx_numbers(ddo, args, im.n, im.op == ROTATE ? 10 : 6))
		return;
	im.box = (struct bounds) {im.n[0], im.n[1], im.n[2], im.n[3]};
	if (im.box.left < 0 || im.box.right >= WIDTH || im.box.left > im.box.right || im.box.top < 0
	    || im.box.bottom >= HEIGHT || im.box.top > im.box.bottom)
		return;
	if ((im.op == ROTATE && (unsigned)im.n[7] > 255)
	    || (im.op == ALIAS && (im.n[4] < -1 || im.n[4] > 255 || (unsigned)im.n[5] > 255))
	    || (im.op == OUTLINE && ((unsigned)im.n[4] > 254 || (unsigned)im.n[5] > 255)))
		return;
	im.clip = im.dst == WORK ? im.g->clip : (struct bounds) {0, WIDTH - 1, 0, HEIGHT - 1};
	if (im.src == VISUAL || im.dst == VISUAL)
		(void)st_console_modify_pixels_with_palette(im.g->display, apply, &im);
	else
		(void)apply(&im, NULL, im.g->palette, NULL);
}
