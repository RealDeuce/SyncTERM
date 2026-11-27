/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_script.h"
#include "st_time.h"
#include "string_util.h"

/* Sprite references are one-based so a zero-initialized cell is empty. */
struct tile {
	int      background, first, last;
	unsigned phase;
};

struct st_ddofx_grid {
	int          x, y, columns, rows, view_w, view_h, tile_w, tile_h;
	int          left, top;
	uint64_t     due;
	unsigned     frame;
	bool         animated, drawn;
	uint8_t     *underlay;
	struct tile *visible;
	struct tile  cells[];
};

enum {
	GRID_INTERVAL_MS = 200
};

struct grid_update {
	const struct st_ddofx_graphics *graphics;
	bool                            animation_only;
};

static int
publish(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	const struct grid_update       *update = context;
	const struct st_ddofx_graphics *g      = update->graphics;
	const struct st_ddofx_grid     *grid   = g->grid;
	if (pixels->width != WIDTH || pixels->height != HEIGHT)
		return 0;
	for (int row = 0; row < grid->view_h; ++row) {
		for (int col = 0; col < grid->view_w; ++col) {
			const struct tile *tile = &grid->visible[row * grid->view_w + col];
			if (update->animation_only && tile->first == tile->last)
				continue;
			int x = grid->x + col * grid->tile_w, y = grid->y + row * grid->tile_h;
			for (int i = 0; i < grid->tile_h; ++i) {
				size_t offset = (size_t)(y + i) * WIDTH + x;
				for (int j = 0; j < grid->tile_w; ++j)
					pixels->primary[offset + j] = pixels->alternate[offset + j] =
					    g->page[HIDDEN][y + i][x + j];
			}
		}
	}
	*changed =
	    (struct st_console_rect) {grid->x, grid->y, grid->view_w * grid->tile_w, grid->view_h * grid->tile_h};
	return 0;
}

void
st_ddofx_grid_destroy(struct st_ddofx_graphics *g, bool erase)
{
	struct st_ddofx_grid *grid = g->grid;
	if (!grid)
		return;
	if (erase && grid->drawn) {
		int width = grid->view_w * grid->tile_w;
		for (int y = 0; y < grid->view_h * grid->tile_h; ++y)
			memcpy(&g->page[HIDDEN][grid->y + y][grid->x], grid->underlay + y * width, width);
		(void)st_console_modify_pixels(g->display, publish, &(struct grid_update) {g, false});
	}
	free(grid->underlay);
	free(grid);
	g->grid = NULL;
}

static void
define(struct st_ddofx *ddo, char *args)
{
	int32_t n[8];
	if (!st_ddofx_numbers(ddo, args, n, 8))
		return;
	int x = n[0], y = n[1], cols = n[2], rows = n[3], vw = n[4], vh = n[5], tw = n[6], th = n[7];
	if ((unsigned)x >= WIDTH || (unsigned)y >= HEIGHT || cols < 1 || cols > 150 || rows < 1 || rows > 150
	    || vw < 1 || vw > cols || vh < 1 || vh > rows || tw < 1 || tw > (WIDTH - x) / vw || th < 1
	    || th > (HEIGHT - y) / vh)
		return;
	struct st_ddofx_grid *grid = calloc(1, sizeof(*grid) + (cols * rows + vw * vh) * sizeof(struct tile));
	if (!grid)
		return;
	grid->underlay = malloc((size_t)vw * tw * vh * th);
	if (!grid->underlay) {
		free(grid);
		return;
	}
	grid->x       = x;
	grid->y       = y;
	grid->columns = cols;
	grid->rows    = rows;
	grid->view_w  = vw;
	grid->view_h  = vh;
	grid->tile_w  = tw;
	grid->tile_h  = th;
	grid->visible = grid->cells + cols * rows;
	st_ddofx_grid_destroy(ddo->graphics, true);
	ddo->graphics->grid = grid;
}

static bool
fits(const struct st_ddofx_graphics *g, int slot)
{
	const struct st_ddofx_sprite *s = &g->sprites[slot];
	return s->pixels && s->width == g->grid->tile_w && s->height == g->grid->tile_h;
}

static struct tile *
cell(struct st_ddofx_grid *grid, int64_t x, int64_t y)
{
	if (x < 1 || x > grid->columns || y < 1 || y > grid->rows)
		return NULL;
	return &grid->cells[(y - 1) * grid->columns + x - 1];
}

static void
assign(struct st_ddofx *ddo, char *args, bool fill)
{
	struct st_ddofx_graphics *g     = ddo->graphics;
	struct st_ddofx_grid     *grid  = g->grid;
	struct tile              *tile  = grid->cells;
	int                       count = grid->columns * grid->rows;
	if (!fill) {
		int32_t x, y;
		if (!st_ddofx_field_number(ddo, &args, &x) || !st_ddofx_field_number(ddo, &args, &y))
			return;
		tile = cell(grid, x, y);
		if (!tile)
			return;
		count = 1;
	}
	char   *a = st_ddofx_field(&args), *b = st_ddofx_field(&args);
	int32_t layer;
	if (!a || !b || !st_ddofx_numbers(ddo, args, &layer, 1) || (layer != 1 && layer != 2))
		return;
	int first = st_ddofx_sprite_slot(g, st_ddofx_value(ddo, a), false);
	int last  = st_ddofx_sprite_slot(g, st_ddofx_value(ddo, b), false);
	if (first < 0 || last < first || (layer == 1 && first != last))
		return;
	for (int i = first; i <= last; ++i)
		if (!fits(g, i))
			return;
	for (int i = 0; i < count; ++i) {
		if (layer == 1)
			tile[i].background = first + 1;
		else {
			tile[i].first = first + 1;
			tile[i].last  = last + 1;
			tile[i].phase = grid->frame;
		}
	}
}

static void
draw(struct st_ddofx_graphics *g, bool tick)
{
	struct st_ddofx_grid *grid    = g->grid;
	int                   width   = grid->view_w * grid->tile_w;
	int32_t               saved_x = g->cx, saved_y = g->cy;
	if (!grid->drawn)
		for (int y = 0; y < grid->view_h * grid->tile_h; ++y)
			memcpy(grid->underlay + y * width, &g->page[HIDDEN][grid->y + y][grid->x], width);
	grid->drawn    = true;
	grid->animated = false;
	for (int row = 0; row < grid->view_h; ++row) {
		struct tile *tiles = grid->visible + row * grid->view_w;
		if (!tick)
			memcpy(tiles, cell(grid, grid->left + 1, grid->top + row + 1),
			    grid->view_w * sizeof(*tiles));
		for (int col = 0; col < grid->view_w; ++col) {
			const struct tile *t        = &tiles[col];
			bool               animated = t->first != t->last;
			grid->animated |= animated;
			if (tick && !animated)
				continue;
			int dx = col * grid->tile_w, dy = row * grid->tile_h;
			int x = grid->x + dx, y = grid->y + dy, bottom = y + grid->tile_h - 1;
			if (t->background && fits(g, t->background - 1))
				st_ddofx_draw_sprite(g, 0, &g->sprites[t->background - 1], HIDDEN, 'P', x, bottom);
			else
				for (int i = 0; i < grid->tile_h; ++i)
					memcpy(&g->page[HIDDEN][y + i][x], grid->underlay + (dy + i) * width + dx,
					    grid->tile_w);
			if (t->first) {
				unsigned slot = t->first - 1 + (grid->frame - t->phase) % (t->last - t->first + 1);
				if (fits(g, slot))
					st_ddofx_draw_sprite(g, 0, &g->sprites[slot], HIDDEN, 'D', x, bottom);
			}
		}
	}
	g->cx = saved_x;
	g->cy = saved_y;
	(void)st_console_modify_pixels(g->display, publish, &(struct grid_update) {g, tick});
	if (grid->animated && !grid->due) {
		uint64_t now;
		if (st_time_monotonic_ms(&now) == 0)
			grid->due = now + GRID_INTERVAL_MS;
	}
	if (!grid->animated)
		grid->due = 0;
}

uint32_t
st_ddofx_grid_wait_ms(const struct st_ddofx *ddo)
{
	const struct st_ddofx_grid *grid = ddo->graphics ? ddo->graphics->grid : NULL;
	uint64_t                    now;
	if (!grid || !grid->due || ddo->background_paused || st_time_monotonic_ms(&now) != 0)
		return UINT32_MAX;
	return grid->due > now ? (uint32_t)(grid->due - now) : 0;
}

void
st_ddofx_grid_poll(struct st_ddofx *ddo)
{
	if (st_ddofx_grid_wait_ms(ddo) == 0) {
		struct st_ddofx_grid *grid = ddo->graphics->grid;
		++grid->frame;
		grid->due = 0;
		draw(ddo->graphics, true);
	}
}

void
st_ddofx_grid_command(struct st_ddofx *ddo, const char *op, char *args)
{
	struct st_ddofx_graphics *g    = ddo->graphics;
	struct st_ddofx_grid     *grid = g->grid;
	if (!ascii_case_compare(op, "5DEFGRID"))
		define(ddo, args);
	else if (!grid)
		return;
	else if (!ascii_case_compare(op, "5KILGRID") && !args)
		st_ddofx_grid_destroy(g, true);
	else if (!ascii_case_compare(op, "5DRWGRID") && !args)
		draw(g, false);
	else if (!ascii_case_compare(op, "5CHGGRID") || !ascii_case_compare(op, "5FILGRID"))
		assign(ddo, args, !ascii_case_compare(op, "5FILGRID"));
	else if (!ascii_case_compare(op, "5POSGRID") || !ascii_case_compare(op, "5MOVGRID")) {
		int32_t n[2];
		if (!st_ddofx_numbers(ddo, args, n, 2))
			return;
		bool    relative = !ascii_case_compare(op, "5MOVGRID");
		int64_t left     = (int64_t)n[0] + (relative ? grid->left : -1);
		int64_t top      = (int64_t)n[1] + (relative ? grid->top : -1);
		if (left < 0 || top < 0 || left > grid->columns - grid->view_w || top > grid->rows - grid->view_h)
			return;
		grid->left = (int)left;
		grid->top  = (int)top;
		draw(g, false);
	}
	else if (!ascii_case_compare(op, "5POSTILE") || !ascii_case_compare(op, "5MOVTILE")
	         || !ascii_case_compare(op, "5KILTILE")) {
		bool    kill = !ascii_case_compare(op, "5KILTILE"), relative = !ascii_case_compare(op, "5MOVTILE");
		int32_t n[4];
		if (!st_ddofx_numbers(ddo, args, n, kill ? 2 : 4))
			return;
		struct tile *src = cell(grid, n[0], n[1]);
		if (!src || !src->first)
			return;
		if (!kill) {
			struct tile *dst = cell(grid, (int64_t)n[2] + (relative ? n[0] : 0),
			    (int64_t)n[3] + (relative ? n[1] : 0));
			if (!dst || dst == src)
				return;
			dst->first = src->first;
			dst->last  = src->last;
			dst->phase = src->phase;
		}
		src->first = src->last = 0;
		draw(g, false);
	}
}
