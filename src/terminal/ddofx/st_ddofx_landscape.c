/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_script.h"
#include "st_time.h"
#include "string_util.h"

enum {
	MAP          = 255,
	VIEW_W       = 320,
	VIEW_H       = 200,
	WATER_OFFSET = 137
};

enum setting {
	X,
	Y,
	ALTITUDE,
	DIRECTION,
	SPEED,
	DETAIL,
	SETTINGS
};

struct st_ddofx_landscape {
	unsigned owner;
	int      camera[SETTINGS];
	int      land_x, land_top, cloud_x, cloud_top;
	uint8_t  sky;
	bool     clouds, ready, active;
	uint64_t due;
	uint32_t random_seed;
	uint8_t  height[256][256];
	uint8_t  frame[VIEW_H][VIEW_W];
};

static int
wrap(int64_t n, int period)
{
	int r = (int)(n % period);
	return r < 0 ? r + period : r;
}

static struct st_ddofx_landscape *
landscape(struct st_ddofx_graphics *g)
{
	if (!g->landscape) {
		g->landscape = calloc(1, sizeof(*g->landscape));
		if (g->landscape) {
			g->landscape->camera[ALTITUDE] = 100;
			g->landscape->sky              = 2;
		}
	}
	return g->landscape;
}

static uint8_t
sample(const struct st_ddofx_landscape *l, int x, int y)
{
	return l->height[(uint8_t)y][(uint8_t)x];
}

static uint32_t
random_word(uint32_t *seed)
{
	*seed = *seed * UINT32_C(1664525) + UINT32_C(1013904223);
	return *seed;
}

static uint8_t
displace(uint32_t *seed, unsigned sum, unsigned step)
{
	int offset = (int)(((uint64_t)random_word(seed) * (step + 1)) >> 32) - (int)step / 2;
	int value  = (int)(sum / 4) + offset;
	return value < 0 ? 0 : value > WATER_OFFSET ? WATER_OFFSET : (uint8_t)value;
}

static void
generate(struct st_ddofx_landscape *l, uint32_t seed)
{
	if (!seed) {
		if (!l->random_seed) {
			uint64_t now = 0;
			(void)st_time_monotonic_ms(&now);
			l->random_seed = (uint32_t)now;
		}
		seed = random_word(&l->random_seed) | UINT32_C(0x80000000);
	}

	/* Diamond-square subdivision on a periodic grid gives continuous terrain
	 * across the camera's wrap at 255. Each pass reads only coarser samples. */
	l->height[0][0] = (uint8_t)(random_word(&seed) >> 25);
	for (int step = 256; step >= 2; step /= 2) {
		int half = step / 2;
		for (int y = half; y < 256; y += step) {
			for (int x = half; x < 256; x += step) {
				unsigned sum    = sample(l, x - half, y - half) + sample(l, x + half, y - half)
				                  + sample(l, x - half, y + half) + sample(l, x + half, y + half);
				l->height[y][x] = displace(&seed, sum, step);
			}
		}
		for (int y = 0; y < 256; y += half) {
			for (int x = y % step ? 0 : half; x < 256; x += step) {
				unsigned sum    = sample(l, x - half, y) + sample(l, x + half, y)
				                  + sample(l, x, y - half) + sample(l, x, y + half);
				l->height[y][x] = displace(&seed, sum, step);
			}
		}
	}
	l->ready = true;
}

static uint8_t
terrain_colour(uint8_t height)
{
	return height >= 1 && height <= 15 ? height + WATER_OFFSET : height;
}

struct map_io {
	struct st_ddofx_landscape *landscape;
	struct st_ddofx_surface    target;
	int                        left, bottom;
	bool                      acquire;
};

static int
map_io(void *context, const struct st_pixel_buffer *pixels, const uint32_t *palette,
    struct st_console_rect *damage)
{
	struct map_io             *io = context;
	struct st_ddofx_landscape     *l      = io->landscape;
	const struct st_ddofx_surface *target = &io->target;
	if (pixels && (pixels->width != WIDTH || pixels->height != HEIGHT))
		return 0;

	for (int y = 0; y < MAP; ++y) {
		int sy = io->bottom - y;
		for (int x = 0; x < MAP; ++x) {
			int sx = io->left + x;
			size_t at = (size_t)sy * target->width + sx;
			if (io->acquire) {
				uint8_t colour = pixels ? st_console_palette_index256(palette, pixels->primary[at])
				                        : target->data[at];
				l->height[y][x] = colour > WATER_OFFSET && colour <= WATER_OFFSET + 15
				                      ? colour - WATER_OFFSET
				                      : colour;
			}
			else if (pixels)
				st_pixel_buffer_set_solid(pixels, at, terrain_colour(l->height[y][x]));
			else if (sx >= target->clip.left && sx <= target->clip.right && sy >= target->clip.top
			         && sy <= target->clip.bottom)
				target->data[at] = terrain_colour(l->height[y][x]);
		}
		if (io->acquire)
			l->height[y][MAP] = l->height[y][0];
	}
	if (io->acquire) {
		/* The SDK exposes 256 camera positions but imports 255 pixels per axis. */
		memcpy(l->height[MAP], l->height[0], sizeof(l->height[0]));
		l->ready = true;
	}
	else if (damage)
		*damage = (struct st_console_rect) {io->left, io->bottom - MAP + 1, MAP, MAP};
	return 0;
}

static void
render(struct st_ddofx_landscape *l)
{
	double cosine, sine;
	st_ddofx_rotation(l->camera[DIRECTION], &cosine, &sine);
	int x = l->camera[X] = wrap(l->camera[X] + lround(l->camera[SPEED] * sine), 256);
	int y = l->camera[Y] = wrap(l->camera[Y] + lround(l->camera[SPEED] * cosine), 256);
	int eye              = sample(l, x, y) + l->camera[ALTITUDE];
	int depth            = 55 + 10 * l->camera[DETAIL];
	memset(l->frame, l->sky, sizeof(l->frame));

	for (int column = 0; column < VIEW_W; ++column) {
		double side = (column - VIEW_W / 2) / 180.0;
		double dx = sine + side * cosine, dy = cosine - side * sine;
		int    bottom = VIEW_H;
		/* Project near terrain first. Farther samples can only reveal pixels
		 * above the highest span drawn in this column. */
		for (int distance = 1; distance <= depth && bottom; ++distance) {
			uint8_t h       = sample(l, x + (int)floor(distance * dx), y + (int)floor(distance * dy));
			int     surface = h < 16 ? 16 : h;
			int     top     = VIEW_H / 2 + (eye - surface) * 30 / distance;
			if (top < 0)
				top = 0;
			while (bottom > top)
				l->frame[--bottom][column] = terrain_colour(h);
		}
	}
}

static uint8_t
cloud_colour(uint8_t colour, uint8_t sky)
{
	if (colour == sky)
		return 255;
	if (colour > WATER_OFFSET)
		colour -= WATER_OFFSET;
	return 121 + colour / 9;
}

static void
present(struct st_ddofx *ddo, struct st_ddofx_landscape *l)
{
	struct st_ddofx_surface target = st_ddofx_surface(ddo->graphics, l->owner, VISUAL);
	struct st_console_rect  land   = {l->land_x, l->land_top, VIEW_W, VIEW_H};
	uint8_t                *frame  = (uint8_t *)l->frame;
	if (l->land_x <= target.width - VIEW_W && l->land_top <= target.height - VIEW_H)
		st_ddofx_animation_background(ddo, l->owner, &land, frame, -1);
	if (!l->clouds || l->cloud_x > target.width - VIEW_W || l->cloud_top > target.height - VIEW_H)
		return;

	/* The clouds are the terrain reflected about both axes. The next
	 * terrain render replaces the complete frame before it is used again. */
	for (size_t i = 0; i < sizeof(l->frame) / 2; ++i) {
		size_t  opposite = sizeof(l->frame) - 1 - i;
		uint8_t colour   = frame[i];
		frame[i]         = cloud_colour(frame[opposite], l->sky);
		frame[opposite]  = cloud_colour(colour, l->sky);
	}
	struct st_console_rect clouds = {l->cloud_x, l->cloud_top, VIEW_W, VIEW_H};
	st_ddofx_animation_background(ddo, l->owner, &clouds, frame, 255);
}

uint32_t
st_ddofx_landscape_wait_ms(const struct st_ddofx *ddo)
{
	const struct st_ddofx_landscape *l = ddo->graphics ? ddo->graphics->landscape : NULL;
	uint64_t                         now;
	if (!l || !l->active || !l->ready || ddo->background_paused
	    || (l->owner && l->owner != ddo->graphics->active_graphic_window) || st_time_monotonic_ms(&now))
		return UINT32_MAX;
	return now < l->due ? (uint32_t)(l->due - now) : 0;
}

void
st_ddofx_landscape_poll(struct st_ddofx *ddo)
{
	if (st_ddofx_landscape_wait_ms(ddo))
		return;
	struct st_ddofx_graphics *g = ddo->graphics;
	uint64_t                  now;
	if (st_time_monotonic_ms(&now))
		return;
	render(g->landscape);
	present(ddo, g->landscape);
	g->landscape->due = now + 55 * g->animator_divisor;
}

static bool
camera_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *verb, char *args)
{
	static const struct {
		const char *set, *modify, *query;
		int         max, period;
	} commands[SETTINGS] = {
	    [X]         = {"5LCX",   "5LMX",   "5LGX", 255, 256},
	    [Y]         = {"5LCY",   "5LMY",   "5LGY", 255, 256},
	    [ALTITUDE]  = {"5LCZ",   "5LMZ",   "5LGZ", 100, 0  },
	    [DIRECTION] = {"5LCDIR", "5LMDIR", "5LGD", 360, 360},
	    [SPEED]     = {"5LCSPD", "5LMSPD", NULL,   50,  0  },
	    [DETAIL]    = {"5LCD",   "5LMD",   NULL,   20,  0  },
	};

	for (int i = 0; i < SETTINGS; ++i) {
		bool query    = commands[i].query && !ascii_case_compare(verb, commands[i].query);
		bool relative = !ascii_case_compare(verb, commands[i].modify);
		if (!query && !relative && ascii_case_compare(verb, commands[i].set))
			continue;
		struct st_ddofx_landscape *l = landscape(ddo->graphics);
		if (!l)
			return true;
		if (query) {
			if (!args) {
				char reply[12];
				snprintf(reply, sizeof(reply), "%d", l->camera[i]);
				st_ddofx_query_reply(ddo, sink, reply, true);
			}
			return true;
		}
		int32_t n;
		if (st_ddofx_numbers(ddo, args, &n, 1) && (relative || (n >= 0 && n <= commands[i].max))) {
			int64_t value = relative ? (int64_t)l->camera[i] + n : n;
			l->camera[i]  = commands[i].period        ? wrap(value, commands[i].period)
			                : value < 0               ? 0
			                : value > commands[i].max ? commands[i].max
			                                          : (int)value;
		}
		return true;
	}
	return false;
}

bool
st_ddofx_landscape_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *verb, char *args)
{
	enum {
		CREATE,
		SKY,
		CLOUDS,
		LAND_POS,
		CLOUD_POS,
		DRAW_VP,
		DRAW_WP,
		GET_VP,
		GET_WP,
		ANIMATE
	};

	static const char *const names[] = {"5CLAND", "5LSKY", "5LCLOUD", "5LLPOS", "5LCPOS", "5LDMAP", "5LDMAPWP",
	    "5LGMAP", "5LGMAPWP", "5LANIM"};
	if (ascii_case_ncompare(verb, "5L", 2) && ascii_case_compare(verb, "5CLAND"))
		return false;
	for (unsigned i = 0; i < sizeof(names) / sizeof(*names); ++i) {
		if (ascii_case_compare(verb, names[i]))
			continue;
		bool    position = i >= LAND_POS && i <= GET_WP;
		int32_t n[2];
		if (!st_ddofx_numbers(ddo, args, n, position ? 2 : 1))
			return true;
		struct st_ddofx_landscape *l = landscape(ddo->graphics);
		if (!l)
			return true;
		switch (i) {
			case CREATE:
				if (n[0] >= 0 && n[0] <= 2000000000) {
					l->owner = ddo->graphics->alert_graphic_window;
					generate(l, (uint32_t)n[0]);
				}
				break;
			case SKY:
				if ((unsigned)n[0] <= 255)
					l->sky = (uint8_t)n[0];
				break;
			case CLOUDS:
				l->clouds = n[0] == 1;
				break;
			case ANIMATE:
				l->active = n[0] == 1;
				l->due    = 0;
				break;
			case LAND_POS:
			case CLOUD_POS:
			{
				struct st_ddofx_surface target = st_ddofx_surface(ddo->graphics, l->owner, VISUAL);
				if (n[0] >= 0 && n[0] <= target.width - VIEW_W && n[1] >= VIEW_H - 1
				    && n[1] < target.height) {
					if (i == LAND_POS) {
						l->land_x   = n[0];
						l->land_top = n[1] - VIEW_H + 1;
					}
					else {
						l->cloud_x   = n[0];
						l->cloud_top = n[1] - VIEW_H + 1;
					}
				}
				break;
			}
			default:
			{
				enum page     page = i == GET_WP || i == DRAW_WP ? WORK : VISUAL;
				struct map_io io   = {l,
				    st_ddofx_surface(ddo->graphics, ddo->graphics->alert_graphic_window, page),
				    n[0], n[1], i == GET_WP || i == GET_VP};
				if ((!io.acquire && !l->ready) || n[0] < 0 || n[0] > io.target.width - MAP
				    || n[1] < MAP - 1 || n[1] >= io.target.height)
					break;
				if (io.target.data)
					(void)map_io(&io, NULL, NULL, NULL);
				else
					(void)st_console_modify_pixels_with_palette(ddo->graphics->display, map_io,
					    &io);
				break;
			}
		}
		return true;
	}
	return camera_command(ddo, sink, verb, args);
}
