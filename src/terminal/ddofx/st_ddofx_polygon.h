/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_DDOFX_POLYGON_H_
#define ST_DDOFX_POLYGON_H_

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_mesh.h"

enum {
	POLYGON_SLOTS = 2000
};

struct polygon_object {
	struct mesh geometry;
	double      position[3], distance;
	int         angle[3], increment[3];
	unsigned    morph_target, morph_speed;
	bool        playing, wire;
};

struct polygon_scene {
	struct polygon_object *object[POLYGON_SLOTS];
	uint64_t               due;
	struct projected_face *projected;
	size_t                 capacity;
	uint8_t                coverage[HEIGHT][WIDTH / 8], damage[HEIGHT][WIDTH / 8];
	struct bounds          area;
};

void st_ddofx_render_polygons(struct st_ddofx_graphics *);
#endif
