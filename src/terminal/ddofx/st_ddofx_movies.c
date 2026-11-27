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
	unsigned               limit, played, fps;
	bool                   once, palette;
	uint64_t               due;
};

struct st_ddofx_movies {
	struct movie slots[MOVIE_COUNT];
	uint64_t     next_due;
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
	movies->next_due = 0;
	for (unsigned i = 0; i < MOVIE_COUNT; ++i) {
		uint64_t due = movies->slots[i].due;
		if (due && (!movies->next_due || due < movies->next_due))
			movies->next_due = due;
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
	if (!args || !st_ddofx_dos_filename(args) || !sink->open_asset || (unsigned)n[0] >= WIDTH
	    || (unsigned)n[1] >= HEIGHT || n[2] <= 0 || (unsigned)n[3] > 1 || n[4] < 1 || n[4] > 100
	    || (unsigned)n[5] > 1)
		return;
	FILE *file = sink->open_asset(sink->context, "BBSTHEME", NULL, args);
	if (!file)
		return;
	struct movie              m = {.fps = (unsigned)n[4], .once = n[3] != 0, .palette = n[5] != 0};
	struct st_ddofx_graphics *g = ddo->graphics;
	if (!st_ddofx_flic_header(&m.flic, file) || m.flic.width > WIDTH - (unsigned)n[0]
	    || m.flic.height > HEIGHT - (unsigned)n[1]) {
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
	if (!movies || !movies->next_due || ddo->background_paused || st_time_monotonic_ms(&now))
		return UINT32_MAX;
	return movies->next_due > now ? (uint32_t)(movies->next_due - now) : 0;
}

struct presentation {
	struct st_ddofx    *ddo;
	const struct movie *m;
};

static int
present(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *damage)
{
	const struct presentation *p = context;
	const struct movie        *m = p->m;
	if (pixels->width != WIDTH || pixels->height != HEIGHT)
		return 0;
	const uint8_t *src = m->flic.image;
	for (unsigned y = 0; y < m->area.height; ++y) {
		size_t at = (y + m->area.y) * WIDTH + m->area.x;
		for (unsigned x = 0; x < m->area.width; ++x)
			pixels->primary[at + x] = pixels->alternate[at + x] = *src++;
	}
	*damage = m->area;
	st_ddofx_repaint_clocks(p->ddo, pixels, damage);
	st_ddofx_repaint_marquees(p->ddo, pixels, damage);
	return 0;
}

void
st_ddofx_movie_poll(struct st_ddofx *ddo)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	uint64_t                  now;
	if (!g || !g->movies || !g->movies->next_due || ddo->background_paused || st_time_monotonic_ms(&now)
	    || now < g->movies->next_due)
		return;
	for (unsigned i = 0; i < MOVIE_COUNT; ++i) {
		struct movie *m = &g->movies->slots[i];
		if (!m->due || now < m->due)
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
		struct presentation frame = {ddo, m};
		(void)st_console_modify_pixels(g->display, present, &frame);
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
