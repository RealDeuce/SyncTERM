/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_polygon.h"
#include "st_ddofx_resource.h"
#include "st_ddofx_script.h"
#include "st_time.h"
#include "string_util.h"
#include "value_convert.h"

static void
destroy(struct polygon_object *o)
{
	if (o) {
		st_ddofx_mesh_free(&o->geometry);
		free(o);
	}
}

void
st_ddofx_polygon_window_closed(struct st_ddofx_graphics *g, unsigned owner)
{
	if (g->polygons) {
		struct polygon_page *p = &g->polygons->page[owner];
		free(p->bits);
		p->bits = NULL;
		p->area = (struct bounds) {0, -1, 0, -1};
	}
}

void
st_ddofx_clear_polygons(struct st_ddofx_graphics *g)
{
	if (g->polygons) {
		for (unsigned i = 0; i < POLYGON_SLOTS; ++i)
			destroy(g->polygons->object[i]);
		for (unsigned owner = 0; owner <= GRAPHIC_WINDOWS; ++owner)
			free(g->polygons->page[owner].bits);
		free(g->polygons->projected);
		free(g->polygons);
		g->polygons = NULL;
	}
}

static bool
load(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, struct mesh *m, const char *member, double divisor,
    uint8_t colour)
{
	char container[sizeof(ddo->resource_prefix) + 1];
	if (!member || !st_ddofx_dos_filename(member))
		return false;
	snprintf(container, sizeof(container), "%sM", ddo->resource_prefix);
	FILE *file = sink->open_asset(sink->context, "BBSTHEME", NULL, container);
	if (!file)
		return false;
	uint32_t length;
	bool     ok = st_ddofx_resource_find(file, member, -1, &length, NULL) >= 0
	              && st_ddofx_mesh_read(m, file, length, divisor, colour);
	fclose(file);
	return ok;
}

static struct polygon_object *
initialize(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *args, bool imported)
{
	int32_t camera[7], colour = 0;
	double  divisor = 1;
	if (imported) {
		char *field = st_ddofx_field(&args);
		if (!field || !value_parse_double(st_ddofx_value(ddo, field), &divisor)
		    || !st_ddofx_field_number(ddo, &args, &colour) || (unsigned)colour > 255)
			return NULL;
	}
	for (unsigned i = 0; i < 7; ++i)
		if (!st_ddofx_field_number(ddo, &args, &camera[i]))
			return NULL;
	if (imported != (args != NULL))
		return NULL;
	struct polygon_object *o = calloc(1, sizeof(*o));
	if (!o)
		return NULL;
	if (imported && !load(ddo, sink, &o->geometry, st_ddofx_asset_name(ddo, args), divisor, (uint8_t)colour)) {
		destroy(o);
		return NULL;
	}
	const int defaults[] = {-175, -100, -1030};
	for (unsigned i = 0; i < 3; ++i) {
		o->position[i] = camera[i] ? camera[i] : defaults[i];
		o->angle[i]    = camera[i + 3] % 360;
	}
	o->owner    = ddo->graphics->alert_graphic_window;
	o->distance = camera[6] ? camera[6] : 1800;
	return o;
}

static void
copy_geometry(struct mesh *dst, const struct mesh *src)
{
	struct mesh copy = {0};
	if (st_ddofx_mesh_append(&copy, src)) {
		st_ddofx_mesh_free(dst);
		*dst = copy;
	}
	else
		st_ddofx_mesh_free(&copy);
}

void
st_ddofx_polygon_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *verb, char *args)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	struct polygon_scene     *s = g->polygons;
	int32_t                   id, n[10];
	if (!st_ddofx_field_number(ddo, &args, &id) || id < 0 || id > POLYGON_SLOTS)
		return;
	bool imported = !ascii_case_compare(verb, "INIT");
	if (imported || !ascii_case_compare(verb, "INITL")) {
		if (!id) {
			if (s) {
				for (unsigned i = 0; i < POLYGON_SLOTS; ++i) {
					destroy(s->object[i]);
					s->object[i] = NULL;
				}
				for (unsigned owner = 0; owner <= GRAPHIC_WINDOWS; ++owner)
					if (s->page[owner].bits)
						st_ddofx_render_polygons(ddo, owner);
				st_ddofx_clear_polygons(g);
			}
			return;
		}
		struct polygon_object *o = initialize(ddo, sink, args, imported);
		if (!o)
			return;
		if (!s) {
			s = g->polygons = calloc(1, sizeof(*s));
			if (!s) {
				destroy(o);
				return;
			}
			for (unsigned owner = 0; owner <= GRAPHIC_WINDOWS; ++owner)
				s->page[owner].due = UINT64_MAX;
		}
		struct polygon_object *old = s->object[id - 1];
		s->object[id - 1] = o;
		if (old && old->playing)
			st_ddofx_render_polygons(ddo, old->owner);
		destroy(old);
		return;
	}
	struct polygon_object *o = id && s ? s->object[id - 1] : NULL;
	if (!o)
		return;
	if (!ascii_case_compare(verb, "DEL") && !args) {
		s->object[id - 1] = NULL;
		if (o->playing)
			st_ddofx_render_polygons(ddo, o->owner);
		destroy(o);
	}
	else if (!ascii_case_compare(verb, "FACE") && st_ddofx_numbers(ddo, args, n, 10)
	         && (unsigned)n[9] <= 255) {
		(void)st_ddofx_mesh_triangle(&o->geometry, n);
	}
	else if (!ascii_case_compare(verb, "PAINT") && st_ddofx_numbers(ddo, args, n, 3) && n[0] >= 1
	         && n[1] >= n[0] && (size_t)n[1] <= o->geometry.faces && (unsigned)n[2] <= 255) {
		for (int32_t i = n[0] - 1; i < n[1]; ++i)
			o->geometry.face[i].colour = (uint8_t)n[2];
	}
	else if (!ascii_case_compare(verb, "PLAY") && st_ddofx_numbers(ddo, args, n, 1) && (unsigned)n[0] <= 1) {
		bool stopped = o->playing && !n[0];
		o->playing   = n[0] != 0;
		if (stopped)
			st_ddofx_render_polygons(ddo, o->owner);
		else if (o->playing)
			s->page[o->owner].due = 0;
	}
	else if (!ascii_case_compare(verb, "WIRE") && st_ddofx_numbers(ddo, args, n, 1) && (unsigned)n[0] <= 1)
		o->wire = n[0] != 0;
	else if (!ascii_case_compare(verb, "DIST") && st_ddofx_numbers(ddo, args, n, 2)
	         && (n[0] == 1 || n[0] == 2))
		o->distance = n[0] == 1 ? n[1] : o->distance + n[1];
	else if (!ascii_case_compare(verb, "MOVE") && st_ddofx_numbers(ddo, args, n, 3) && n[0] >= 1 && n[0] <= 3
	         && (n[1] == 1 || n[1] == 2)) {
		double *position = &o->position[n[0] - 1];
		*position        = n[1] == 1 ? n[2] : *position + n[2];
	}
	else if (!ascii_case_compare(verb, "EQU") || !ascii_case_compare(verb, "MERGE")
	         || !ascii_case_compare(verb, "MORPH")) {
		bool morph = !ascii_case_compare(verb, "MORPH");
		if (st_ddofx_numbers(ddo, args, n, morph ? 2 : 1) && n[0] >= 1 && n[0] <= POLYGON_SLOTS
		    && s->object[n[0] - 1] && (!morph || (unsigned)n[1] <= 255)) {
			struct mesh *src = &s->object[n[0] - 1]->geometry;
			if (morph) {
				o->morph_target = (unsigned)n[0];
				o->morph_speed  = (unsigned)n[1];
			}
			else if (!ascii_case_compare(verb, "MERGE"))
				(void)st_ddofx_mesh_append(&o->geometry, src);
			else
				copy_geometry(&o->geometry, src);
		}
	}
	else {
		const char *const axes[] = {"YAW", "ROLL", "PITCH"};
		for (unsigned i = 0; i < 3; ++i)
			if (!ascii_case_compare(verb, axes[i]) && st_ddofx_numbers(ddo, args, n, 2)) {
				int angle = n[1] % 360;
				if (n[0] == 1)
					o->angle[i] = angle;
				else if (n[0] == 2)
					o->angle[i] = (o->angle[i] + angle) % 360;
				else if (n[0] == 3)
					o->increment[i] = angle;
				break;
			}
	}
}

uint32_t
st_ddofx_polygon_wait_ms(const struct st_ddofx *ddo)
{
	const struct polygon_scene *s = ddo->graphics ? ddo->graphics->polygons : NULL;
	uint64_t                    now;
	if (!s || ddo->background_paused)
		return UINT32_MAX;
	uint64_t due = s->page[ddo->graphics->active_graphic_window].due;
	if (s->page[0].due < due)
		due = s->page[0].due;
	if (due == UINT64_MAX || st_time_monotonic_ms(&now))
		return UINT32_MAX;
	return due <= now ? 0 : (uint32_t)(due - now);
}

static void
advance_page(struct st_ddofx *ddo, unsigned owner, uint64_t now)
{
	struct polygon_scene *s = ddo->graphics->polygons;
	if (now < s->page[owner].due)
		return;
	s->page[owner].due = UINT64_MAX;
	for (unsigned i = 0; i < POLYGON_SLOTS; ++i) {
		struct polygon_object *o = s->object[i];
		if (!o || !o->playing || o->owner != owner)
			continue;
		s->page[owner].due = now + 55 * ddo->graphics->animator_divisor;
		for (unsigned j = 0; j < 3; ++j)
			o->angle[j] = (o->angle[j] + o->increment[j]) % 360;
		if (o->morph_target) {
			const struct polygon_object *target = s->object[o->morph_target - 1];
			if (target && st_ddofx_mesh_morph(&o->geometry, &target->geometry, o->morph_speed))
				o->morph_target = 0;
		}
	}
	st_ddofx_render_polygons(ddo, owner);
}

void
st_ddofx_polygon_poll(struct st_ddofx *ddo)
{
	uint64_t now;
	if (st_ddofx_polygon_wait_ms(ddo) || st_time_monotonic_ms(&now))
		return;
	advance_page(ddo, 0, now);
	if (ddo->graphics->active_graphic_window)
		advance_page(ddo, ddo->graphics->active_graphic_window, now);
}
