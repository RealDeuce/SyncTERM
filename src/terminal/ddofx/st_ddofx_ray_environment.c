/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "filesystem.h"
#include "st_ddofx_ray.h"
#include "st_ddofx_resource.h"
#include "st_ddofx_script.h"
#include "st_file_lock.h"
#include "string_util.h"

/* Loose MAP files contain byte planes, not a dump of the C raycaster.
 * Keep the imported backing so unused atlas tiles and padding survive edits. */
struct rc_environment {
	uint8_t                 profile_map[31850];
	struct rc_saved_profile profiles[RC_PROFILES];
	uint8_t                 plane[11][65534];
	uint8_t                 characteristics[15925];
	uint8_t                 atlas[2][65535];
	uint8_t                 animations[80][138];
	uint8_t                 left[2], bottom[2], map_left[2], map_top[2];
	uint8_t                 x[2], y[2], angle[2], scaled, width[2], height[2];
};

_Static_assert(sizeof(struct rc_environment) == 1141178, "DDO environment file size");

static const uint8_t wall_shape[] = {0, 168, 174, 175, 169};

static void
save_cells(const struct rc_world *w, struct rc_environment *e)
{
	for (int y = 0; y < RC_HEIGHT; ++y) {
		for (int x = 0; x < RC_WIDTH; ++x) {
			const struct rc_cell *c                       = &w->cells[y][x];
			int                   at                      = (y + 20) * 256 + x + 5;
			uint8_t               shape                   = wall_shape[c->wall];
			e->plane[4][at]                               = shape ? c->texture + 23 : c->floor;
			e->plane[4][at + 32768]                       = shape ? shape : c->roof;
			e->characteristics[y * RC_PROFILE_STRIDE + x] = c->characteristic;
			for (int layer = 0; layer < RC_LAYERS; ++layer) {
				const struct rc_object *o = &c->objects[layer];
				for (int side = 0; side < 2; ++side) {
					uint8_t *plane      = e->plane[5 + layer * 2 + side];
					plane[at]           = o->texture + (o->texture >= 8 ? 8 : 0);
					uint8_t orientation = shape;
					if (o->texture)
						orientation =
						    174 + (o->orientation == 3 ? side : o->orientation - 1);
					plane[at + 32768] = orientation;
				}
			}
		}
	}
}

static void
load_cells(struct rc_world *w, const struct rc_environment *e)
{
	for (int y = 0; y < RC_HEIGHT; ++y) {
		for (int x = 0; x < RC_WIDTH; ++x) {
			int             at  = (y + 20) * 256 + x + 5;
			unsigned        low = e->plane[4][at], high = e->plane[4][at + 32768];
			struct rc_cell *c = &w->cells[y][x];
			*c                = (struct rc_cell) {.floor = low >= 1 && low <= 23 ? low : 1,
			    .roof                     = high >= 1 && high <= 23 ? high : 1,
			    .characteristic           = e->characteristics[y * RC_PROFILE_STRIDE + x]};
			for (unsigned shape = 1; shape < sizeof(wall_shape); ++shape) {
				if (wall_shape[shape] == high && low >= 24 && low <= 31) {
					c->wall    = shape;
					c->texture = low - 23;
				}
			}
			for (int layer = 0; layer < RC_LAYERS; ++layer) {
				const uint8_t *plane  = e->plane[5 + layer * 2];
				unsigned       number = plane[at], first = plane[at + 32768];
				unsigned       second = e->plane[6 + layer * 2][at + 32768];
				if (number >= 16 && number <= 23)
					number -= 8;
				else if (!number || number > 7)
					continue;
				if ((first == 174 || first == 175)
				    && (second == first || (first == 174 && second == 175)))
					c->objects[layer] = (struct rc_object) {first == second ? first - 173 : 3,
					    c->characteristic, number};
			}
		}
	}
}

static void
textures(struct rc_world *w, struct rc_environment *e, bool save)
{
	for (int kind = FLCL; kind <= HIOBJ; ++kind) {
		int count = kind == FLCL ? 23 : kind <= HIWALL ? 8 : 15;
		for (int n = 1; n <= count; ++n) {
			bool    object             = kind >= LOOBJ;
			int     tile               = n;
			uint8_t (*pixels)[RC_TILE] = NULL;
			if (object) {
				tile += (n >= 8 ? 8 : 0) + (kind == HIOBJ ? 8 : 0);
				pixels = w->objects[kind - LOOBJ][n - 1];
			}
			else if (kind == FLCL)
				pixels = w->flats[n - 1];
			else {
				tile += kind == LOWALL ? 23 : 31;
				pixels = w->walls[kind - LOWALL][n - 1];
			}
			uint8_t *base = e->atlas[object] + tile / 8 * 8192 + (tile % 8) * RC_TILE;
			for (int y = 0; y < RC_TILE; ++y) {
				uint8_t *row = base + (kind == FLCL ? y : RC_TILE - 1 - y) * 256;
				if (save)
					memcpy(row, pixels[y], RC_TILE);
				else
					memcpy(pixels[y], row, RC_TILE);
			}
			/* The animation slots follow the two atlas tile layouts. */
			uint8_t             *r = e->animations[tile + (object ? 40 : 0)];
			struct rc_animation *a = &w->animations[kind][n - 1];
			if (save) {
				r[1] = (uint8_t)a->step;
				r[3] = (uint8_t)(a->period_us / 10000);
				rc_put_word(r + 12, a->period_us ? a->first + 1 : 0);
				rc_put_word(r + 20, rc_word(r + 12));
				rc_put_word(r + 22, a->period_us ? a->last + 1 : 0);
				rc_put_word(r + 24, a->period_us ? a->current + 1 : 0);
				r[26]  = (uint8_t)a->loop;
				r[137] = a->due != 0;
			}
			else {
				unsigned first = rc_word(r + 20), last = rc_word(r + 22),
					 current = rc_word(r + 24);
				*a               = (struct rc_animation) {0};
				if (first && first <= current && current <= last && last <= SPRITE_COUNT
				    && r[26] <= 3 && r[3]) {
					*a = (struct rc_animation) {.first = first - 1,
					    .last                          = last - 1,
					    .current                       = current - 1,
					    .step                          = r[26] == 3 && r[1] == 255 ? -1 : 1,
					    .loop                          = r[26],
					    .period_us                     = 10000u * r[3],
					    .due                           = r[137] != 0};
					if (a->due)
						w->next_animation = 1;
				}
			}
		}
	}
}

static void
save(struct rc_world *w, struct rc_environment *e)
{
	save_cells(w, e);
	st_ddofx_ray_profiles_save(w, e->profile_map, e->profiles);
	textures(w, e, true);
	if (w->frame_changed) {
		for (unsigned y = 0; y < 160; ++y) {
			memcpy(e->plane[0] + y * 320, w->frame[159 - y], 320);
			memset(e->plane[1] + y * 320, 0, 320);
		}
		if (w->width != 320 || w->height != 160) {
			for (int y = 0; y < w->height; ++y)
				for (int x = 0; x < w->width; ++x)
					e->plane[2][y * w->width + x] =
					    w->frame[(w->height - 1 - y) * 160 / w->height][x * 320 / w->width];
			memset(e->plane[3], 0, (size_t)w->width * w->height);
		}
		w->frame_changed = false;
	}
	rc_put_word(e->left, w->left);
	rc_put_word(e->bottom, w->bottom);
	rc_put_word(e->map_left, w->map_left);
	rc_put_word(e->map_top, w->map_top);
	rc_put_word(e->x, (unsigned)((w->x + 4) * 32));
	rc_put_word(e->y, (unsigned)((w->y + 19) * 32));
	rc_put_word(e->angle, (768 - (142 * w->angle + 50) / 100) % 512);
	e->scaled = w->width != 320 || w->height != 160;
	rc_put_word(e->width, w->width);
	rc_put_word(e->height, w->height);
}

static bool
load(struct st_ddofx_graphics *g, struct rc_environment *e)
{
	struct rc_world        *w      = g->ray;
	struct st_ddofx_surface target = st_ddofx_surface(g, w->owner, VISUAL);
	struct st_ddofx_surface map    = st_ddofx_surface(g, w->map_owner, VISUAL);
	unsigned width  = e->scaled ? rc_word(e->width) : 320;
	unsigned height = e->scaled ? rc_word(e->height) : 160;
	if ((int)rc_word(e->left) >= target.width || (int)rc_word(e->bottom) >= target.height
	    || (rc_word(e->map_left) && (int)rc_word(e->map_left) > map.width - RC_WIDTH)
	    || (rc_word(e->map_top) && (int)rc_word(e->map_top) > map.height - RC_HEIGHT)
	    || rc_word(e->angle) >= 512 || !width || (int)width > target.width || !height
	    || (int)height > target.height || width * height > 64000
	    || !st_ddofx_ray_profiles_load(w, e->profile_map, e->profiles))
		return false;
	load_cells(w, e);
	w->next_animation = 0;
	textures(w, e, false);
	for (unsigned y = 0; y < 160; ++y)
		for (unsigned x = 0; x < 320; ++x) {
			unsigned at          = y * 320 + x;
			uint8_t  overlay     = e->plane[1][at];
			w->frame[159 - y][x] = overlay ? overlay : e->plane[0][at];
		}
	w->left     = rc_word(e->left);
	w->bottom   = rc_word(e->bottom);
	w->width    = width;
	w->height   = height;
	w->map_left = rc_word(e->map_left);
	w->map_top  = rc_word(e->map_top);
	/* The SDK requires the live player coordinates to survive a map change. */
	w->angle = (100 * ((768 - rc_word(e->angle)) % 512) + 71) / 142;
	if (!w->angle)
		w->angle = 360;
	w->frame_changed = false;
	w->dirty         = true;
	return true;
}

bool
st_ddofx_ray_environment_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *verb,
    char *args)
{
	bool writing = !ascii_case_compare(verb, "5SAVE3D");
	if (!writing && ascii_case_compare(verb, "5LOAD3D"))
		return false;
	if (!args)
		return true;
	const char *name = st_ddofx_value(ddo, args);
	if (!st_ddofx_dos_filename(name))
		return true;
	struct rc_world *w = st_ddofx_ray_world(ddo->graphics);
	if (!w)
		return true;
	if (!writing && sink->open_asset) {
		FILE *f = sink->open_asset(sink->context, "BBSTHEME", NULL, name);
		if (f) {
			if (fs_stream_size(f) == (int64_t)sizeof(struct rc_environment)) {
				struct rc_environment *e = malloc(sizeof(*e));
				if (e && fread(e, 1, sizeof(*e), f) == sizeof(*e) && load(ddo->graphics, e)) {
					free(w->environment);
					w->environment = e;
				}
				else
					free(e);
			}
			fclose(f);
		}
	}
	else if (writing && sink->theme_path) {
		if (!w->environment)
			w->environment = calloc(1, sizeof(*w->environment));
		if (!w->environment)
			return true;
		save(w, w->environment);
		char           *directory = sink->theme_path(sink->context, NULL);
		st_file_lock_t *lock      = NULL;
		if (directory && !st_file_lock_acquire(&lock, directory, true)) {
			char *path = sink->theme_path(sink->context, name);
			if (path)
				(void)fs_replace_file(path, w->environment, sizeof(*w->environment));
			free(path);
			st_file_lock_release(lock);
		}
		free(directory);
	}
	return true;
}
