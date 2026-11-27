/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "filesystem.h"
#include "st_ddofx_mesh.h"

enum {
	MESH_LIMIT = 10000
};

void
st_ddofx_mesh_free(struct mesh *m)
{
	free(m->point);
	free(m->face);
	*m = (struct mesh) {0};
}

static size_t
capacity(size_t old, size_t need)
{
	size_t n = old ? 2 * old : 8;
	if (n < need)
		n = need;
	return n < MESH_LIMIT ? n : MESH_LIMIT;
}

static bool
reserve(struct mesh *m, size_t points, size_t faces)
{
	if (points > MESH_LIMIT || faces > MESH_LIMIT)
		return false;
	if (points > m->point_capacity) {
		size_t             n = capacity(m->point_capacity, points);
		struct mesh_point *p = realloc(m->point, n * sizeof(*p));
		if (!p)
			return false;
		m->point          = p;
		m->point_capacity = n;
	}
	if (faces > m->face_capacity) {
		size_t            n = capacity(m->face_capacity, faces);
		struct mesh_face *p = realloc(m->face, n * sizeof(*p));
		if (!p)
			return false;
		m->face          = p;
		m->face_capacity = n;
	}
	return true;
}

bool
st_ddofx_mesh_append(struct mesh *dst, const struct mesh *src)
{
	size_t np = src->points, nf = src->faces;
	if (!reserve(dst, dst->points + np, dst->faces + nf))
		return false;
	if (np)
		memcpy(dst->point + dst->points, src->point, np * sizeof(*src->point));
	for (size_t i = 0; i < nf; ++i) {
		struct mesh_face f = src->face[i];
		for (unsigned j = 0; j < 3; ++j)
			f.vertex[j] += (uint16_t)dst->points;
		dst->face[dst->faces + i] = f;
	}
	dst->points += np;
	dst->faces += nf;
	return true;
}

bool
st_ddofx_mesh_triangle(struct mesh *m, const int32_t coord[10])
{
	struct mesh_face f        = {.colour = (uint8_t)coord[9]};
	size_t           original = m->points;
	if (!reserve(m, m->points, m->faces + 1))
		return false;
	for (unsigned i = 0; i < 3; ++i) {
		struct mesh_point p = {coord[i * 3], coord[i * 3 + 1], coord[i * 3 + 2]};
		size_t            j = 0;
		while (j < m->points && (m->point[j].x != p.x || m->point[j].y != p.y || m->point[j].z != p.z))
			++j;
		if (j == m->points) {
			if (!reserve(m, j + 1, m->faces + 1)) {
				m->points = original;
				return false;
			}
			m->point[m->points++] = p;
		}
		f.vertex[i] = (uint16_t)j;
	}
	m->face[m->faces++] = f;
	return true;
}

static bool
advance(double *value, double target, unsigned speed)
{
	if (*value < target)
		*value = fmin(*value + speed, target);
	else if (*value > target)
		*value = fmax(*value - speed, target);
	return *value == target;
}

bool
st_ddofx_mesh_morph(struct mesh *dst, const struct mesh *src, unsigned speed)
{
	if (dst == src)
		return true;
	if (!reserve(dst, src->points, src->faces))
		return false;
	/* New vertices start at the origin. Use the target topology so even
	 * meshes with different numbers of faces and points remain drawable. */
	for (size_t i = dst->points; i < src->points; ++i)
		dst->point[i] = (struct mesh_point) {0};
	for (size_t i = dst->faces; i < src->faces; ++i)
		dst->face[i].colour = 0;
	dst->points       = src->points;
	dst->faces        = src->faces;
	bool     complete = true;
	unsigned colours  = speed * 2;
	for (size_t i = 0; i < dst->faces; ++i) {
		memcpy(dst->face[i].vertex, src->face[i].vertex, sizeof(dst->face[i].vertex));
		if (dst->face[i].colour != src->face[i].colour) {
			if (colours) {
				dst->face[i].colour = src->face[i].colour;
				--colours;
			}
			else
				complete = false;
		}
	}
	for (size_t i = 0; i < dst->points; ++i) {
		struct mesh_point       *p = &dst->point[i];
		const struct mesh_point *t = &src->point[i];
		complete &= advance(&p->x, t->x, speed);
		complete &= advance(&p->y, t->y, speed);
		complete &= advance(&p->z, t->z, speed);
	}
	return complete;
}

static unsigned
le16(const uint8_t *p)
{
	return p[0] | (unsigned)p[1] << 8;
}

static uint32_t
le32(const uint8_t *p)
{
	return le16(p) | (uint32_t)le16(p + 2) << 16;
}

/* Each nesting level can enter only the next defined 3DS container. A
 * member length, then the containing chunk length, bounds every read. */
struct reader {
	FILE        *file;
	struct mesh *mesh;
	double       divisor;
	uint8_t      colour;
};

static bool
coordinate(uint32_t bits, double divisor, double *out)
{
	unsigned exponent = (bits >> 23) & 255;
	if (exponent == 255)
		return false;
	uint32_t significand = bits & 0x7fffff;
	if (exponent)
		significand |= 0x800000;
	double v = ldexp((double)significand, (exponent ? (int)exponent : 1) - 150) / divisor;
	*out     = nearbyint(bits >> 31 ? -v : v);
	return isfinite(*out) && fabs(*out) <= INT32_MAX;
}

static bool
children(struct reader *r, unsigned parent, uint32_t remaining)
{
	struct mesh *m           = r->mesh;
	size_t       first_point = m->points, first_face = m->faces;
	while (remaining) {
		uint8_t header[6];
		if (remaining < sizeof(header) || fread(header, 1, sizeof(header), r->file) != sizeof(header))
			return false;
		unsigned id   = le16(header);
		uint32_t size = le32(header + 2);
		if (size < 6 || size > remaining)
			return false;
		remaining -= size;
		size -= 6;
		if ((parent == 0x4d4d && id == 0x3d3d) || (parent == 0x3d3d && id == 0x4000)
		    || (parent == 0x4000 && id == 0x4100)) {
			if (id == 0x4000) {
				int c;
				do {
					if (!size || (c = fgetc(r->file)) == EOF)
						return false;
					--size;
				} while (c != 0);
			}
			if (!children(r, id, size))
				return false;
			continue;
		}
		if (parent == 0x4100 && (id == 0x4110 || id == 0x4120)) {
			uint8_t data[12];
			if (size < 2 || fread(data, 1, 2, r->file) != 2)
				return false;
			unsigned count = le16(data), stride = id == 0x4110 ? 12 : 8;
			if (size - 2 < count * stride
			    || !reserve(m, m->points + (id == 0x4110 ? count : 0),
				m->faces + (id == 0x4120 ? count : 0)))
				return false;
			size -= 2 + count * stride;
			for (unsigned i = 0; i < count; ++i) {
				if (fread(data, 1, stride, r->file) != stride)
					return false;
				if (id == 0x4110) {
					struct mesh_point *p = &m->point[m->points++];
					if (!coordinate(le32(data), r->divisor, &p->x)
					    || !coordinate(le32(data + 4), r->divisor, &p->y)
					    || !coordinate(le32(data + 8), r->divisor, &p->z))
						return false;
				}
				else {
					struct mesh_face *f = &m->face[m->faces++];
					for (unsigned j = 0; j < 3; ++j) {
						size_t index = first_point + le16(data + 2 * j);
						if (index >= MESH_LIMIT)
							return false;
						f->vertex[j] = (uint16_t)index;
					}
					f->colour = r->colour;
				}
			}
		}
		if (size) {
			int64_t offset = fs_stream_position(r->file);
			if (offset < 0 || offset > INT64_MAX - size || fs_stream_seek(r->file, offset + size))
				return false;
		}
	}
	if (parent == 0x4100)
		for (size_t i = first_face; i < m->faces; ++i)
			for (unsigned j = 0; j < 3; ++j)
				if (m->face[i].vertex[j] >= m->points)
					return false;
	return true;
}

bool
st_ddofx_mesh_read(struct mesh *m, FILE *file, uint32_t length, double divisor, uint8_t colour)
{
	uint8_t header[6];
	if (length < 6 || fread(header, 1, 6, file) != 6 || le16(header) != 0x4d4d || le32(header + 2) > length
	    || le32(header + 2) < 6)
		return false;
	struct reader r = {file, m, divisor ? divisor : 16.0, colour};
	return children(&r, 0x4d4d, le32(header + 2) - 6);
}
