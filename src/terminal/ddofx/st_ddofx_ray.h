/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_DDOFX_RAY_H_
#define ST_DDOFX_RAY_H_

#include "st_ddofx_graphics_internal.h"

enum {
	RC_WIDTH          = 244,
	RC_HEIGHT         = 64,
	RC_LAYERS         = 3,
	RC_TILE           = 32,
	RC_PROFILES       = 3600,
	RC_PROFILE_STRIDE = 245
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
	uint8_t          wall, texture, floor, roof, characteristic;
	struct rc_object objects[RC_LAYERS];
};

struct rc_animation {
	int      first, last, current, step, loop;
	/* The live scheduler uses milliseconds and MAP files use whole centiseconds. */
	unsigned period_us;
	uint64_t due;
};

struct rc_world {
	struct rc_environment *environment;
	struct rc_recording   *motion;
	struct rc_profiles    *profiles;
	struct rc_cell         cells[RC_HEIGHT][RC_WIDTH];
	unsigned               layer, owner, map_owner;
	double                 x, y;
	int                    angle;
	uint32_t               seed;
	bool                   overview;
	int                    map_left, map_top;
	bool                   active, dirty, frame_changed;
	uint64_t               next_frame, next_animation;
	struct rc_animation    animations[5][23];
	int                    left, bottom, width, height;
	uint8_t flats[23][RC_TILE][RC_TILE], walls[2][8][RC_TILE][RC_TILE], objects[2][15][RC_TILE][RC_TILE];
	uint8_t frame[160][320];
	uint8_t scaled_frame[64000];
};

struct rc_saved_profile {
	uint8_t defined, action, key_high[2], key_low[2], mouse, joystick, direction;
	uint8_t reply[26], macro[26], stealth, chain[2];
};

static inline unsigned
rc_word(const uint8_t p[2])
{
	return p[0] + 256u * p[1];
}

static inline void
rc_put_word(uint8_t p[2], unsigned value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
}

bool st_ddofx_ray_environment_command(struct st_ddofx *, const struct st_ddofx_sink *, const char *, char *);
bool st_ddofx_ray_profiles_load(struct rc_world *, const uint8_t *, const struct rc_saved_profile *);
void st_ddofx_ray_profiles_save(const struct rc_world *, uint8_t *, struct rc_saved_profile *);
struct rc_world *st_ddofx_ray_world(struct st_ddofx_graphics *);
bool     st_ddofx_ray_record_command(struct st_ddofx *, const struct st_ddofx_sink *, const char *, char *);
void     st_ddofx_ray_motion_frame(struct rc_world *);
bool     st_ddofx_ray_profile_command(struct st_ddofx *, const char *, char *);
uint64_t st_ddofx_ray_profile_due(const struct st_ddofx *);
void     st_ddofx_ray_profile_poll(struct st_ddofx *, bool frame, uint64_t now);
void     st_ddofx_ray_defaults(struct rc_world *);
bool     st_ddofx_ray_view_command(struct st_ddofx *, const struct st_ddofx_sink *, const char *, char *);
void     st_ddofx_ray_marker(struct st_ddofx_graphics *, int old_x, int old_y);

#endif
