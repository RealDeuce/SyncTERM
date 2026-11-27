/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_ray.h"
#include "st_ddofx_resource.h"
#include "st_ddofx_script.h"
#include "st_time.h"
#include "string_util.h"

static bool
in_map(int x, int y)
{
	return x >= 1 && x <= RC_WIDTH && y >= 1 && y <= RC_HEIGHT;
}

static bool
interior(int x, int y)
{
	return x > 1 && x < RC_WIDTH && y > 1 && y < RC_HEIGHT;
}

static void
clear_map(struct rc_world *w, int floor, int roof, int wall, bool solid)
{
	for (int y = 1; y <= RC_HEIGHT; ++y) {
		for (int x = 1; x <= RC_WIDTH; ++x) {
			struct rc_cell *c = &w->cells[y - 1][x - 1];
			*c                = (struct rc_cell) {.floor = floor, .roof = roof};
			if (solid || !interior(x, y)) {
				c->wall    = 1;
				c->texture = wall;
			}
		}
	}
}

struct maze_point {
	int x, y;
};

static struct maze_point
create_maze(struct rc_world *w, int wall, int floor, int roof, uint32_t seed)
{
	enum {
		COLS = RC_WIDTH / 4,
		ROWS = RC_HEIGHT / 4
	};

	bool                           open[ROWS][COLS] = {{false}};
	struct maze_point              path[ROWS * COLS], end = {1, 1};
	static const struct maze_point step[] = {
	    {0,  -1},
            {0,  1 },
            {1,  0 },
            {-1, 0 }
        };
	if (!seed) {
		uint64_t now = 0;
		(void)st_time_monotonic_ms(&now);
		w->seed = (w->seed + (uint32_t)now) * UINT32_C(1664525) + UINT32_C(1013904223);
		seed    = w->seed;
	}
	clear_map(w, floor, roof, wall, true);
	int depth = 1, longest = 1;
	path[0]    = end;
	open[1][1] = true;
	while (depth) {
		struct maze_point p = path[depth - 1];
		int               available[4], count = 0;
		for (int i = 0; i < 4; ++i) {
			int x = p.x + 2 * step[i].x, y = p.y + 2 * step[i].y;
			if (x > 0 && y > 0 && x < COLS - 1 && y < ROWS - 1 && !open[y][x])
				available[count++] = i;
		}
		if (!count) {
			--depth;
			continue;
		}
		seed                       = seed * UINT32_C(0x08088405) + 1;
		struct maze_point d        = step[available[((uint64_t)seed * count) >> 32]];
		open[p.y + d.y][p.x + d.x] = true;
		p.x += 2 * d.x;
		p.y += 2 * d.y;
		open[p.y][p.x] = true;
		path[depth++]  = p;
		if (depth > longest) {
			longest = depth;
			end     = p;
		}
	}
	for (int y = 0; y < RC_HEIGHT; ++y)
		for (int x = 0; x < RC_WIDTH; ++x)
			if (open[y / 4][x / 4]) {
				w->cells[y][x].wall    = 0;
				w->cells[y][x].texture = 0;
			}
	w->x = w->y = 5.5;
	w->dirty    = true;
	return (struct maze_point) {4 * end.x + 1, 4 * end.y + 1};
}

struct rc_world *
st_ddofx_ray_world(struct st_ddofx_graphics *g)
{
	if (!g->ray) {
		g->ray = calloc(1, sizeof(*g->ray));
		if (g->ray) {
			st_ddofx_ray_defaults(g->ray);
			(void)create_maze(g->ray, 1, 1, 1, 0);
		}
	}
	return g->ray;
}

static char *
map_line(char **next)
{
	char *line = *next;
	char *end  = line + strcspn(line, "\r\n");
	*next      = end;
	if (*end) {
		bool crlf = end[0] == '\r' && end[1] == '\n';
		*end      = 0;
		*next += crlf ? 2 : 1;
	}
	return line;
}

static char *
map_row(char **next)
{
	char  *line   = map_line(next);
	size_t length = strlen(line);
	return line + (length < 4 ? length : 4);
}

static void
read_map(struct rc_world *w, char *text)
{
	char *floor[RC_HEIGHT - 2];
	for (int i = 0; i < 9; ++i)
		(void)map_line(&text);
	for (int i = 0; i < RC_HEIGHT - 2; ++i)
		floor[i] = map_row(&text);
	for (int i = 0; i < 8; ++i)
		(void)map_line(&text);

	for (int y = 0; y < RC_HEIGHT - 2; ++y) {
		char  *roof   = map_row(&text);
		size_t length = strlen(roof);
		if (length != strlen(floor[y]))
			continue;
		for (size_t i = 0; i < length && i < RC_WIDTH - 2; ++i) {
			struct rc_cell   *c   = &w->cells[y + 1][RC_WIDTH - 2 - i];
			char              low = floor[y][i], high = roof[i];
			static const char shapes[] = "#-|0";
			const char       *shape    = strchr(shapes, high);
			if (shape) {
				c->wall    = (uint8_t)(1 + shape - shapes);
				c->texture = low >= '1' && low <= '8' ? low - '0' : 1;
			}
			else if (low >= 'a' && low <= 'w' && high >= 'a' && high <= 'w') {
				c->floor = low - 'a' + 1;
				c->roof  = high - 'a' + 1;
				c->wall = c->texture = 0;
			}
		}
	}
	w->dirty = true;
}

static void
load_map(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *member)
{
	if (!ddo->resource_prefix[0] || !sink->open_asset)
		return;
	char container[sizeof(ddo->resource_prefix) + 1];
	snprintf(container, sizeof(container), "%sM", ddo->resource_prefix);
	FILE *file = sink->open_asset(sink->context, "BBSTHEME", NULL, container);
	if (!file)
		return;
	uint32_t length;
	if (st_ddofx_resource_find(file, member, -1, &length, NULL) >= 0 && length && length <= 64000) {
		char *text = malloc(length + 1);
		if (text && fread(text, 1, length, file) == length) {
			text[length]                = 0;
			text[strcspn(text, "\x1a")] = 0;
			read_map(ddo->graphics->ray, text);
		}
		free(text);
	}
	fclose(file);
}

enum overview_action {
	DRAW,
	MARK,
	ANNOTATE
};

struct overview {
	struct rc_world     *w;
	enum overview_action action;
	int                  x, y, colour, other;
};

static void
xor_marker(const struct st_pixel_buffer *pixels, const uint32_t *palette, struct rc_world *w, int x, int y)
{
	size_t at             = (w->map_top + y - 1) * WIDTH + w->map_left + RC_WIDTH - x;
	pixels->primary[at]   = st_console_palette_index256(palette, pixels->primary[at]) ^ 15;
	pixels->alternate[at] = st_console_palette_index256(palette, pixels->alternate[at]) ^ 15;
}

static int
paint_map(void *context, const struct st_pixel_buffer *pixels, const uint32_t *palette,
    struct st_console_rect *damage)
{
	struct overview *o = context;
	struct rc_world *w = o->w;
	if (pixels->width != WIDTH || pixels->height != HEIGHT)
		return 0;
	*damage = (struct st_console_rect) {w->map_left, w->map_top, RC_WIDTH, RC_HEIGHT};
	if (o->action == MARK)
		xor_marker(pixels, palette, w, o->x, o->y);
	else {
		for (int y = 1; y <= RC_HEIGHT; ++y) {
			for (int x = 1; x <= RC_WIDTH; ++x) {
				int colour = o->colour;
				if (o->action == DRAW)
					colour = w->cells[y - 1][x - 1].wall ? o->colour : o->other;
				else {
					int dx = x - o->x, dy = y - o->y;
					if (dx * dx + dy * dy > (int64_t)o->other * o->other)
						continue;
				}
				size_t at           = (w->map_top + y - 1) * WIDTH + w->map_left + RC_WIDTH - x;
				pixels->primary[at] = pixels->alternate[at] = colour;
			}
		}
	}
	if (o->action != ANNOTATE)
		xor_marker(pixels, palette, w, (int)w->x, (int)w->y);
	return 0;
}

void
st_ddofx_ray_marker(struct st_ddofx_graphics *g, int old_x, int old_y)
{
	if (!g->ray->overview || (old_x == (int)g->ray->x && old_y == (int)g->ray->y))
		return;
	struct overview o = {.w = g->ray, .action = MARK, .x = old_x, .y = old_y};
	(void)st_console_modify_pixels_with_palette(g->display, paint_map, &o);
}

bool
st_ddofx_ray_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *verb, char *args)
{
	enum {
		CLEAR,
		CHANGE,
		OBJECT,
		LAYER,
		QUERY,
		MAZE,
		LOAD,
		DUMP,
		ANNOTATION,
		XPOS,
		YPOS,
		ANGLE,
		GETX,
		GETY,
		GETANGLE
	};

	static const struct {
		const char *name;
		int         count;
	} commands[] = {
	    {"5CLSMAP",  1 },
            {"5CHMAP",   4 },
            {"5CHOBJ",   5 },
            {"5DLYR",    1 },
            {"5GETMAP",  2 },
            {"5CMAZE",   4 },
	    {"5LOADMAP", -1},
            {"52DMAP",   4 },
            {"52DMMAP",  4 },
            {"5SETXPOS", 2 },
            {"5SETYPOS", 2 },
            {"5SETANGL", 1 },
	    {"5GETXPOS", 0 },
            {"5GETYPOS", 0 },
            {"5GETANGL", 0 }
        };

	unsigned op;
	for (op = 0; op < sizeof(commands) / sizeof(*commands); ++op)
		if (!ascii_case_compare(verb, commands[op].name))
			break;
	if (op == sizeof(commands) / sizeof(*commands))
		return st_ddofx_ray_record_command(ddo, sink, verb, args) || st_ddofx_ray_view_command(ddo, sink, verb, args);
	int32_t n[5];
	int     count = commands[op].count;
	if (count == -1 ? !args : !st_ddofx_numbers(ddo, args, n, (unsigned)count))
		return true;
	struct rc_world *w = st_ddofx_ray_world(ddo->graphics);
	if (!w)
		return true;
	char reply[64];
	switch (op) {
		case CLEAR:
			if (n[0] >= 1 && n[0] <= 23) {
				clear_map(w, n[0], n[0], 1, false);
				w->dirty = true;
			}
			break;
		case LAYER:
			if (n[0] >= 1 && n[0] <= RC_LAYERS)
				w->layer = n[0] - 1;
			break;
		case CHANGE:
		case OBJECT:
		{
			if (!interior(n[1], n[2]))
				break;
			struct rc_cell *c = &w->cells[n[2] - 1][n[1] - 1];
			if (op == OBJECT) {
				if (n[0] >= 1 && n[0] <= 3 && (unsigned)n[3] <= 2 && (unsigned)n[4] <= 15)
					c->objects[w->layer] = (struct rc_object) {n[4] ? n[0] : 0, n[3], n[4]};
			}
			else if (n[0] >= 1 && n[0] <= 4 && n[3] >= 1 && n[3] <= 8) {
				c->wall    = n[0];
				c->texture = n[3];
			}
			else if ((n[0] == 5 || n[0] == 6) && n[3] >= 1 && n[3] <= 23) {
				c->wall = c->texture = 0;
				if (n[0] == 5)
					c->floor = n[3];
				else
					c->roof = n[3];
			}
			w->dirty = true;
			break;
		}
		case QUERY:
		{
			if (!in_map(n[0], n[1]))
				break;
			const struct rc_cell   *c = &w->cells[n[1] - 1][n[0] - 1];
			const struct rc_object *o = &c->objects[w->layer];
			snprintf(reply, sizeof(reply), "%u,%u,%u,%u,%u,%u", c->wall, c->texture,
			    c->wall ? 0 : c->floor, c->wall ? 0 : c->roof, o->orientation, o->texture);
			st_ddofx_query_reply(ddo, sink, reply, false);
			break;
		}
		case MAZE:
			if (n[0] >= 1 && n[0] <= 7 && n[1] >= 1 && n[1] <= 20 && n[2] >= 1 && n[2] <= 20
			    && n[3] >= 0 && n[3] <= 2000000000) {
				struct maze_point end = create_maze(w, n[0], n[1], n[2], (uint32_t)n[3]);
				snprintf(reply, sizeof(reply), "%d,%d", end.x, end.y);
				st_ddofx_query_reply(ddo, sink, reply, false);
			}
			break;
		case LOAD:
			load_map(ddo, sink, st_ddofx_value(ddo, args));
			break;
		case DUMP:
		case ANNOTATION:
		{
			struct overview o = {.w = w, .x = n[0], .y = n[1], .colour = n[2], .other = n[3]};
			if ((unsigned)o.colour > 255)
				break;
			if (op == DUMP) {
				if ((unsigned)n[0] > 395 || (unsigned)n[1] > 412 || (unsigned)n[3] > 255)
					break;
				w->map_left = n[0];
				w->map_top  = n[1];
				w->overview = true;
				o.action    = DRAW;
			}
			else {
				if (!w->overview || !in_map(n[0], n[1]) || n[3] < 0)
					break;
				o.action = ANNOTATE;
				o.other  = n[3] <= 1 ? 0 : n[3];
			}
			(void)st_console_modify_pixels_with_palette(ddo->graphics->display, paint_map, &o);
			break;
		}
		case XPOS:
		case YPOS:
		{
			if (n[0] < 2 || n[0] >= (op == XPOS ? RC_WIDTH : RC_HEIGHT) || (unsigned)n[1] > 1)
				break;
			int old_x = (int)w->x, old_y = (int)w->y;
			if (op == XPOS)
				w->x = n[0] + 0.5;
			else
				w->y = n[0] + 0.5;
			if (n[1])
				st_ddofx_ray_marker(ddo->graphics, old_x, old_y);
			w->dirty = true;
			break;
		}
		case ANGLE:
			if (n[0] >= 1 && n[0] <= 360) {
				w->angle = n[0];
				w->dirty = true;
			}
			break;
		case GETX:
		case GETY:
			snprintf(reply, sizeof(reply), "%d", (int)(op == GETX ? w->x : w->y));
			st_ddofx_query_reply(ddo, sink, reply, true);
			break;
		case GETANGLE:
		{
			static const char *const dir[] = {"NORTH", "NORTH EAST", "EAST", "SOUTH EAST", "SOUTH",
			    "SOUTH WEST", "WEST", "NORTH WEST"};
			snprintf(reply, sizeof(reply), "%d,%s", w->angle, dir[(w->angle + 22) / 45 % 8]);
			st_ddofx_query_reply(ddo, sink, reply, false);
			break;
		}
	}
	return true;
}
