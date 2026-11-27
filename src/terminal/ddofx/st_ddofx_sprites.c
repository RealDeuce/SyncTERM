/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_script.h"
#include "string_util.h"

void
st_ddofx_free_sprites(struct st_ddofx_graphics *g)
{
	for (int i = 0; i < SPRITE_COUNT; ++i)
		free(g->sprites[i].pixels);
	memset(g->sprites, 0, sizeof(g->sprites));
}

int
st_ddofx_sprite_slot(struct st_ddofx_graphics *g, const char *name, bool create)
{
	int vacant = -1;
	if (!*name || strlen(name) > 8)
		return -1;
	for (int i = 0; i < SPRITE_COUNT; ++i) {
		if (g->sprites[i].pixels) {
			if (!ascii_case_compare(g->sprites[i].name, name))
				return i;
		}
		else if (vacant < 0)
			vacant = i;
	}
	return create ? vacant : -1;
}

static bool
page_suffix(const char *suffix, enum page *page)
{
	switch (*suffix) {
		case 0:
			*page = WORK;
			return true;
		case 'B':
			*page = BACKGROUND;
			break;
		case 'H':
			*page = HIDDEN;
			break;
		case 'V':
			*page = VISUAL;
			break;
		default:
			return false;
	}
	return suffix[1] == 0;
}

struct capture {
	uint8_t               *data;
	struct st_console_rect area;
};

static int
capture_visible(void *context, const struct st_pixel_buffer *pixels, const uint32_t *palette,
    struct st_console_rect *changed)
{
	(void)changed;
	struct capture               *c = context;
	const struct st_console_rect *r = &c->area;
	if (pixels->width != WIDTH || pixels->height != HEIGHT)
		return -1;
	for (unsigned row = 0; row < r->height; ++row) {
		const uint32_t *src = pixels->primary + (row + r->y) * WIDTH + r->x;
		for (unsigned x = 0; x < r->width; ++x)
			c->data[row * r->width + x] = st_console_palette_index256(palette, src[x]);
	}
	return 0;
}

bool
st_ddofx_sprite_fits(const struct st_ddofx_graphics *g, unsigned slot, const struct st_ddofx_sprite *s, int32_t x,
    int32_t y)
{
	const struct st_ddofx_graphic_window *w     = &g->graphic_windows[slot];
	int                                   width = slot ? w->width : WIDTH, height = slot ? w->height : HEIGHT;
	return s && s->pixels && x >= 0 && x <= width - s->width && y >= s->height - 1 && y < height;
}

static void
capture_sprite(struct st_ddofx *ddo, char *args, enum page page)
{
	int32_t n[4];
	for (unsigned i = 0; i < 4; ++i)
		if (!st_ddofx_field_number(ddo, &args, &n[i]))
			return;
	if (args == NULL)
		return;
	const char               *name = st_ddofx_value(ddo, args);
	struct st_ddofx_graphics *g    = ddo->graphics;
	int                       slot = st_ddofx_sprite_slot(g, name, true);
	if (slot < 0)
		return;
	struct st_ddofx_sprite *s = &g->sprites[slot];
	free(s->pixels);
	*s = (struct st_ddofx_sprite) {0};
	unsigned                              window = page == BACKGROUND ? 0 : g->alert_graphic_window;
	const struct st_ddofx_graphic_window *w      = &g->graphic_windows[window];
	int width = window ? w->width : WIDTH, height = window ? w->height : HEIGHT;
	if (n[0] < 0 || n[1] <= n[0] || n[1] >= width || n[2] < 0 || n[3] <= n[2] || n[3] >= height)
		return;
	struct st_console_rect r     = {n[0], n[2], n[1] - n[0] + 1, n[3] - n[2] + 1};
	size_t                 count = (size_t)r.width * r.height;
	if (count > 64000)
		return;
	/* Captured images carry 100 trailing bytes in a stored sprite bank. */
	uint8_t *data = calloc(count + 100, 1);
	if (!data)
		return;
	if (!window && page == VISUAL) {
		struct capture c = {data, r};
		if (st_console_modify_pixels_with_palette(g->display, capture_visible, &c) != 0) {
			free(data);
			return;
		}
	}
	else {
		const uint8_t *src =
		    window ? (page == HIDDEN ? w->hidden_page : w->visual) : (uint8_t *)g->page[page];
		for (unsigned row = 0; row < r.height; ++row)
			memcpy(data + row * r.width, src + (r.y + row) * width + r.x, r.width);
	}
	s->active = 1;
	s->allocation = (uint16_t)(count + 100);
	s->pixels = data;
	s->width  = (uint16_t)r.width;
	s->height = (uint16_t)r.height;
	strcpy(s->name, name);
	g->cx = n[0];
	g->cy = n[3];
}

struct sprite_draw {
	const struct st_ddofx_sprite *sprite;
	struct st_console_rect        area;
	int                           src_x, src_y;
	bool                          mirror, opaque;
};

static void
paint(const struct sprite_draw *d, uint8_t *page, unsigned stride, const struct st_pixel_buffer *pixels)
{
	const struct st_console_rect *r = &d->area;
	const struct st_ddofx_sprite *s = d->sprite;
	for (unsigned row = 0; row < r->height; ++row) {
		const uint8_t *src  = s->pixels + (row + d->src_y) * s->width;
		size_t         dest = (row + r->y) * stride + r->x;
		for (unsigned col = 0; col < r->width; ++col) {
			unsigned sx    = col + d->src_x;
			uint8_t  index = src[d->mirror ? s->width - 1 - sx : sx];
			if (index == 0 && !d->opaque)
				continue;
			if (page)
				page[dest + col] = index;
			else
				pixels->primary[dest + col] = pixels->alternate[dest + col] = index;
		}
	}
}

static int
paint_visible(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	const struct sprite_draw *d = context;
	if (pixels->width == WIDTH && pixels->height == HEIGHT) {
		paint(d, NULL, WIDTH, pixels);
		*changed = d->area;
	}
	return 0;
}

void
st_ddofx_draw_sprite(struct st_ddofx_graphics *g, unsigned window, const struct st_ddofx_sprite *s, enum page page,
    char op, int32_t x, int32_t y)
{
	struct st_ddofx_graphic_window *w = &g->graphic_windows[window];
	if (window && !w->visual)
		return;
	int           width = window ? w->width : WIDTH, height = window ? w->height : HEIGHT;
	struct bounds b       = {0, width - 1, 0, height - 1};
	bool          clipped = op == 'F' || op == 'C';
	if (clipped && page == WORK)
		b = window ? w->clip : g->clip;
	int64_t left = x, right = (int64_t)x + s->width - 1, top = (int64_t)y - s->height + 1, bottom = y;
	if (!clipped && (left < b.left || right > b.right || top < b.top || bottom > b.bottom))
		return;
	g->cx = x;
	g->cy = y;
	if (right < b.left || left > b.right || bottom < b.top || top > b.bottom)
		return;
	int x1 = left < b.left ? b.left : (int)left, y1 = top < b.top ? b.top : (int)top;
	int x2 = right > b.right ? b.right : (int)right, y2 = bottom > b.bottom ? b.bottom : (int)bottom;
	struct sprite_draw d = {
	    .sprite = s,
	    .area   = {x1, y1, x2 - x1 + 1, y2 - y1 + 1},
	    .src_x  = (int)(x1 - left),
	    .src_y  = (int)(y1 - top),
	    .mirror = op == 'R' || op == 'F',
	    .opaque = op == 'P'
        };
	if (window)
		paint(&d, page == HIDDEN ? w->hidden_page : w->visual, width, NULL);
	else if (page == VISUAL)
		(void)st_console_modify_pixels(g->display, paint_visible, &d);
	else
		paint(&d, (uint8_t *)g->page[page], WIDTH, NULL);
}

void
st_ddofx_invert_sprite(struct st_ddofx_sprite *s)
{
	for (unsigned y = 0; y < s->height / 2; ++y) {
		uint8_t *top = s->pixels + y * s->width;
		uint8_t *bottom = s->pixels + (s->height - 1 - y) * s->width;
		for (unsigned x = 0; x < s->width; ++x) {
			uint8_t pixel = top[x];
			top[x] = bottom[x];
			bottom[x] = pixel;
		}
	}
}

void
st_ddofx_sprite_command(struct st_ddofx *ddo, const char *verb, char *args)
{
	enum page                 page;
	struct st_ddofx_graphics *g = ddo->graphics;
	if (!strcmp(verb, "2sFF") && args == NULL)
		st_ddofx_free_sprites(g);
	else if (verb[1] == 'S' && page_suffix(verb + 2, &page))
		capture_sprite(ddo, args, page);
	else if (!strcmp(verb, "2sI") && args) {
		int slot = st_ddofx_sprite_slot(g, st_ddofx_value(ddo, args), false);
		if (slot >= 0) {
			st_ddofx_invert_sprite(&g->sprites[slot]);
			g->cx = g->cy = 0;
		}
	}
	else if (verb[1] == 's' && verb[2] && strchr("DCRFP", verb[2]) && page_suffix(verb + 3, &page)) {
		int32_t x, y;
		if (!st_ddofx_field_number(ddo, &args, &x) || !st_ddofx_field_number(ddo, &args, &y) || !args)
			return;
		int slot = st_ddofx_sprite_slot(g, st_ddofx_value(ddo, args), false);
		if (slot >= 0)
			st_ddofx_draw_sprite(g, page == BACKGROUND ? 0 : g->alert_graphic_window,
			    &g->sprites[slot], page, verb[2], x, y);
	}
}
