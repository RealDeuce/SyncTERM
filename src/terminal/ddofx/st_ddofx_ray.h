/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_DDOFX_RAY_H_
#define ST_DDOFX_RAY_H_

#include "st_ddofx_graphics_internal.h"

enum {
	RC_WIDTH  = 244,
	RC_HEIGHT = 64,
	RC_LAYERS = 3,
	RC_TILE   = 32
};

enum rc_texture_kind {
	FLCL,
	LOWALL,
	HIWALL,
	LOOBJ,
	HIOBJ
};

struct rc_object {
	uint8_t orientation, characteristic, texture;
};

struct rc_cell {
	uint8_t          wall, texture, floor, roof;
	struct rc_object objects[RC_LAYERS];
};

struct rc_animation {
	int      first, last, current, step, loop, period;
	uint64_t due;
};

struct rc_world {
	struct rc_cell      cells[RC_HEIGHT][RC_WIDTH];
	unsigned            layer;
	double              x, y;
	int                 angle;
	uint32_t            seed;
	bool                overview;
	int                 map_left, map_top;
	bool                active, dirty;
	uint64_t            next_frame, next_animation;
	struct rc_animation animations[5][23];
	int                 left, bottom, width, height;
	uint8_t flats[23][RC_TILE][RC_TILE], walls[2][8][RC_TILE][RC_TILE], objects[2][15][RC_TILE][RC_TILE];
	uint8_t frame[160][320];
};

struct rc_world *st_ddofx_ray_world(struct st_ddofx_graphics *);
void             st_ddofx_ray_defaults(struct rc_world *);
bool             st_ddofx_ray_view_command(struct st_ddofx *, const struct st_ddofx_sink *, const char *, char *);
void             st_ddofx_ray_marker(struct st_ddofx_graphics *, int old_x, int old_y);

#endif
