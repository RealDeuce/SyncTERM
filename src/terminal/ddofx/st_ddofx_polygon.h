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
	unsigned    owner, morph_target, morph_speed;
	bool        playing, wire;
};

struct polygon_page {
	uint64_t      due;
	uint8_t      *bits;
	bool          second_mask;
	struct bounds area;
};

struct polygon_scene {
	struct polygon_object *object[POLYGON_SLOTS];
	struct polygon_page    page[GRAPHIC_WINDOWS + 1];
	struct projected_face *projected;
	size_t                 capacity;
};

void st_ddofx_render_polygons(struct st_ddofx *, unsigned owner);
#endif
