/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_DDOFX_MESH_H_
#define ST_DDOFX_MESH_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct mesh_point {
	double x, y, z;
};

struct mesh_face {
	uint16_t vertex[3];
	uint8_t  colour;
};

struct mesh {
	struct mesh_point *point;
	struct mesh_face  *face;
	size_t             points, faces, point_capacity, face_capacity;
};

void st_ddofx_mesh_free(struct mesh *);
bool st_ddofx_mesh_append(struct mesh *, const struct mesh *);
bool st_ddofx_mesh_triangle(struct mesh *, const int32_t coordinates[10]);
bool st_ddofx_mesh_morph(struct mesh *, const struct mesh *, unsigned);
bool st_ddofx_mesh_read(struct mesh *, FILE *, uint32_t length, double divisor, uint8_t colour);

#endif
