/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "filesystem.h"
#include "st_ddofx_flic.h"
#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_resource.h"
#include "st_ddofx_script.h"
#include "st_time.h"
#include "string_util.h"

enum {
	MOVIE_COUNT = 300
};

struct movie {
	struct st_ddofx_flic   flic;
	struct st_console_rect area;
	unsigned               limit, played, fps, owner;
	bool                   once, palette;
	uint64_t               due;
};

struct st_ddofx_movies {
	struct movie slots[MOVIE_COUNT];
	uint64_t     due[GRAPHIC_WINDOWS + 1];
};

static void
close_movie(struct movie *m)
{
	if (m->flic.file)
		fclose(m->flic.file);
	free(m->flic.image);
	*m = (struct movie) {0};
}

void
st_ddofx_clear_movies(struct st_ddofx_graphics *g)
{
	if (g->movies) {
		for (unsigned i = 0; i < MOVIE_COUNT; ++i)
			close_movie(&g->movies->slots[i]);
		free(g->movies);
		g->movies = NULL;
	}
	g->animator_divisor = 1;
}

static void
schedule(struct st_ddofx_movies *movies)
{
	memset(movies->due, 0, sizeof(movies->due));
	for (unsigned i = 0; i < MOVIE_COUNT; ++i) {
		const struct movie *m = &movies->slots[i];
		uint64_t *due = &movies->due[m->owner];
		if (m->due && (!*due || m->due < *due))
			*due = m->due;
	}
}

static unsigned
period(const struct st_ddofx_graphics *g, const struct movie *m)
{
	return (1000 * g->animator_divisor + m->fps - 1) / m->fps;
}

void
st_ddofx_movie_resolution(struct st_ddofx_graphics *g, unsigned divisor)
{
	g->animator_divisor = divisor;
	uint64_t now;
	if (!g->movies || st_time_monotonic_ms(&now))
		return;
	for (unsigned i = 0; i < MOVIE_COUNT; ++i) {
		struct movie *m = &g->movies->slots[i];
		if (m->due)
			m->due = now + period(g, m);
	}
	schedule(g->movies);
}

static void
open_movie(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, unsigned id, char *args)
{
	int32_t n[6];
	for (int i = 0; i < 6; ++i)
		if (!st_ddofx_field_number(ddo, &args, n + i))
			return;
	struct st_ddofx_graphics *g = ddo->graphics;
	unsigned owner = g->alert_graphic_window;
	struct st_ddofx_surface target = st_ddofx_surface(g, owner, VISUAL);
	if (!args || !sink->open_asset || n[0] < 0 || n[0] >= target.width
	    || n[1] < 0 || n[1] >= target.height || n[2] <= 0 || (unsigned)n[3] > 1 || n[4] < 1 || n[4] > 100
	    || (unsigned)n[5] > 1)
		return;
	const char *name = st_ddofx_asset_name(ddo, args);
	if (!st_ddofx_dos_filename(name))
		return;
	FILE *file = sink->open_asset(sink->context, "BBSTHEME", NULL, name);
	if (!file)
		return;
	struct movie m = {.owner = owner, .fps = (unsigned)n[4], .once = n[3] != 0, .palette = n[5] != 0};
	if (!st_ddofx_flic_header(&m.flic, file) || m.flic.width > (unsigned)(target.width - n[0])
	    || m.flic.height > (unsigned)(target.height - n[1])) {
		fclose(file);
		return;
	}
	m.limit      = (unsigned)n[2] < m.flic.frames ? (unsigned)n[2] : m.flic.frames;
	m.area       = (struct st_console_rect) {n[0], n[1], m.flic.width, m.flic.height};
	m.flic.image = calloc(m.flic.width, m.flic.height);
	if (!g->movies)
		g->movies = calloc(1, sizeof(*g->movies));
	if (!g->movies || !m.flic.image) {
		close_movie(&m);
		return;
	}
	close_movie(&g->movies->slots[id - 1]);
	g->movies->slots[id - 1] = m;
	schedule(g->movies);
}

bool
st_ddofx_movie_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *verb, char *args)
{
	if (strlen(verb) != 6 || ascii_case_compare(verb + 2, "FLIC"))
		return false;
	int op = ascii_to_upper((uint8_t)verb[1]);
	if (op != 'O' && op != 'P' && op != 'U' && op != 'S')
		return false;
	int32_t id;
	if (!st_ddofx_field_number(ddo, &args, &id) || id < 0 || id > MOVIE_COUNT)
		return true;
	struct st_ddofx_graphics *g = ddo->graphics;
	if (op == 'O' && id)
		open_movie(ddo, sink, (unsigned)id, args);
	else if (g->movies && (!args || op == 'O')) {
		uint64_t now = 0;
		if (op == 'U' && st_time_monotonic_ms(&now))
			return true;
		for (int i = id ? id - 1 : 0, end = id ? id : MOVIE_COUNT; i < end; ++i) {
			struct movie *m = &g->movies->slots[i];
			if (!m->flic.file)
				continue;
			if (op == 'O' || op == 'S')
				close_movie(m);
			else if (op == 'P')
				m->due = 0;
			else if (!m->due)
				m->due = now + period(g, m);
		}
		schedule(g->movies);
	}
	return true;
}

uint32_t
st_ddofx_movie_wait_ms(const struct st_ddofx *ddo)
{
	const struct st_ddofx_movies *movies = ddo->graphics ? ddo->graphics->movies : NULL;
	uint64_t                      now;
	if (!movies || ddo->background_paused)
		return UINT32_MAX;
	uint64_t due = movies->due[0], active = movies->due[ddo->graphics->active_graphic_window];
	if (active && (!due || active < due))
		due = active;
	if (!due || st_time_monotonic_ms(&now))
		return UINT32_MAX;
	return due > now ? (uint32_t)(due - now) : 0;
}

void
st_ddofx_movie_poll(struct st_ddofx *ddo)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	uint64_t                  now;
	if (st_ddofx_movie_wait_ms(ddo) || st_time_monotonic_ms(&now))
		return;
	for (unsigned i = 0; i < MOVIE_COUNT; ++i) {
		struct movie *m = &g->movies->slots[i];
		if (!m->due || now < m->due || (m->owner && m->owner != g->active_graphic_window))
			continue;
		uint32_t palette[256];
		if (m->palette)
			memcpy(palette, g->palette, sizeof(palette));
		if (!st_ddofx_flic_frame(&m->flic, m->palette ? palette : NULL)) {
			close_movie(m);
			continue;
		}
		if (m->palette && memcmp(palette, g->palette, sizeof(palette))) {
			memcpy(g->palette, palette, sizeof(palette));
			st_ddofx_set_palette(g, 0, 256);
		}
		st_ddofx_animation_background(ddo, m->owner, &m->area, m->flic.image, -1);
		if (++m->played == m->limit) {
			if (m->once || fs_stream_seek(m->flic.file, 128)) {
				close_movie(m);
				continue;
			}
			m->played = 0;
			memset(m->flic.image, 0, m->area.width * m->area.height);
		}
		m->due = now + period(g, m);
	}
	schedule(g->movies);
}
