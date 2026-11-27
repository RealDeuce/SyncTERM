/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_resource.h"
#include "st_ddofx_script.h"
#include "st_time.h"
#include "string_util.h"

enum {
	MARQUEE_COUNT = 300
};

struct marquee {
	uint8_t *text;
	unsigned length, first_pixel, period;
	unsigned owner, x, bottom, columns, ink, paper;
	uint64_t due;
	bool     on_visual;
};

struct st_ddofx_marquees {
	struct marquee slots[MARQUEE_COUNT];
	uint64_t       next_due[GRAPHIC_WINDOWS + 1];
};

void
st_ddofx_clear_marquees(struct st_ddofx_graphics *g)
{
	if (g->marquees) {
		for (unsigned i = 0; i < MARQUEE_COUNT; ++i)
			free(g->marquees->slots[i].text);
		free(g->marquees);
		g->marquees = NULL;
	}
}

static void
next_deadline(struct st_ddofx_marquees *marquees)
{
	memset(marquees->next_due, 0, sizeof(marquees->next_due));
	for (unsigned i = 0; i < MARQUEE_COUNT; ++i) {
		uint64_t due = marquees->slots[i].due;
		uint64_t *next = &marquees->next_due[marquees->slots[i].owner];
		if (due && (!*next || due < *next))
			*next = due;
	}
}

static void
define(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, unsigned slot, char *args)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	int32_t                   n[6];
	for (unsigned i = 0; i < 6; ++i)
		if (!st_ddofx_field_number(ddo, &args, &n[i]))
			return;
	unsigned                owner  = g->alert_graphic_window;
	struct st_ddofx_surface target = st_ddofx_surface(g, owner, VISUAL);
	if (!args || !*args || n[2] < 1 || n[2] > 80 || n[0] < 0 || n[0] > target.width - g->cell_width * n[2]
	    || n[1] < g->cell_height - 1 || n[1] >= target.height || (unsigned)n[3] > 15 || (unsigned)n[4] > 7
	    || n[5] < 1 || !ddo->resource_prefix[0] || !sink->open_asset)
		return;
	char container[sizeof(ddo->resource_prefix) + 1];
	snprintf(container, sizeof(container), "%sM", ddo->resource_prefix);
	FILE *file = sink->open_asset(sink->context, "BBSTHEME", NULL, container);
	if (!file)
		return;
	uint32_t length;
	uint8_t *text = NULL;
	if (st_ddofx_resource_find(file, st_ddofx_value(ddo, args), -1, &length, NULL) >= 0 && length && length <= 20000) {
		text = malloc(length);
		if (text && fread(text, 1, length, file) != length) {
			free(text);
			text = NULL;
		}
	}
	fclose(file);
	if (!text)
		return;
	for (unsigned i = 0; i < length; ++i)
		if (text[i] < 16)
			text[i] = ' ';
	if (!g->marquees)
		g->marquees = calloc(1, sizeof(*g->marquees));
	if (!g->marquees) {
		free(text);
		return;
	}
	struct marquee *m = &g->marquees->slots[slot];
	free(m->text);
	*m = (struct marquee) {
	    .owner   = owner,
	    .text    = text,
	    .length  = length,
	    .x       = (unsigned)n[0],
	    .bottom  = (unsigned)n[1],
	    .columns = (unsigned)n[2],
	    .ink     = (unsigned)n[3],
	    .paper   = (unsigned)n[4],
	    .period  = (999u + (unsigned)n[5]) / (unsigned)n[5],
	};
	next_deadline(g->marquees);
}

static unsigned
ring_width(const struct st_ddofx_graphics *g, const struct marquee *m)
{
	return g->cell_width * (m->length > m->columns ? m->length : m->columns);
}

static struct bounds
strip_bounds(const struct st_ddofx_graphics *g, const struct marquee *m)
{
	int top = (int)m->bottom + 1 - g->cell_height;
	return (struct bounds) {m->x, m->x + m->columns * g->cell_width - 1, top, m->bottom};
}

static void
paint(const struct marquee *m, const uint8_t *font, const struct st_ddofx_graphics *g,
    const struct st_ddofx_surface *target, const struct st_pixel_buffer *pixels, struct bounds area)
{
	unsigned ring = ring_width(g, m);
	for (int y = area.top; y <= area.bottom; ++y) {
		unsigned row      = 15 - (m->bottom - y);
		unsigned position = (m->first_pixel + area.left - m->x) % ring;
		for (int x = area.left; x <= area.right; ++x) {
			unsigned col = position / g->cell_width;
			uint8_t  ch  = col < m->length ? m->text[col] : ' ';
			unsigned colour =
			    (font[ch * 16 + row] & (0x80u >> (position % g->cell_width))) ? m->ink : m->paper;
			if (pixels) {
				size_t at           = y * WIDTH + x;
				pixels->primary[at] = pixels->alternate[at] = colour;
			}
			else
				target->data[y * target->width + x] = (uint8_t)colour;
			if (++position == ring)
				position = 0;
		}
	}
}

void
st_ddofx_repaint_marquees(struct st_ddofx *ddo, unsigned owner, const struct st_pixel_buffer *pixels,
    struct bounds clip)
{
	struct st_ddofx_marquees *mq = ddo->graphics->marquees;
	if (!mq || !mq->next_due[owner])
		return;
	const uint8_t *font = st_ddofx_vga_bitmap(ddo);
	if (!font)
		return;
	struct st_ddofx_surface target = st_ddofx_surface(ddo->graphics, owner, VISUAL);
	for (unsigned i = 0; i < MARQUEE_COUNT; ++i) {
		const struct marquee *m = &mq->slots[i];
		if (!m->due || !m->on_visual || m->owner != owner)
			continue;
		struct bounds area = strip_bounds(ddo->graphics, m);
		if (st_ddofx_intersect_bounds(&area, &clip)) {
			paint(m, font, ddo->graphics, &target, pixels, area);
			st_ddofx_repaint_animation_faces(ddo->graphics, owner, pixels, area);
		}
	}
}

bool
st_ddofx_marquee_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *verb, char *args)
{
	bool create = !ascii_case_compare(verb, "5CMARQEE"), show = !ascii_case_compare(verb, "5SHOWMQ");
	bool kill = !ascii_case_compare(verb, "5KILLMQ");
	if (!create && !show && !kill && ascii_case_compare(verb, "5STOPMQ"))
		return false;
	int32_t id;
	if (!st_ddofx_field_number(ddo, &args, &id))
		return true;
	struct st_ddofx_graphics *g = ddo->graphics;
	if (create && id == 0)
		st_ddofx_clear_marquees(g);
	else if (id >= 1 && id <= MARQUEE_COUNT) {
		if (create)
			define(ddo, sink, (unsigned)id - 1, args);
		else if (!args && g->marquees) {
			struct marquee *m = &g->marquees->slots[id - 1];
			if (!m->text)
				return true;
			if (kill) {
				free(m->text);
				*m = (struct marquee) {0};
			}
			else if (show) {
				uint64_t       now;
				const uint8_t *font = st_ddofx_vga_bitmap(ddo);
				if (font && (!m->owner || g->graphic_windows[m->owner].visual)
				    && st_time_monotonic_ms(&now) == 0) {
					struct st_ddofx_surface work   = st_ddofx_surface(g, 0, WORK);
					struct bounds           extent = {0, work.width - 1, 0, work.height - 1};
					struct bounds           strip  = strip_bounds(g, m);
					if (st_ddofx_intersect_bounds(&strip, &extent))
						paint(m, font, g, &work, NULL, strip);
					m->on_visual = false;
					m->due       = now + m->period;
				}
			}
			else
				m->due = 0;
			next_deadline(g->marquees);
		}
	}
	return true;
}

uint32_t
st_ddofx_marquee_wait_ms(const struct st_ddofx *ddo)
{
	const struct st_ddofx_marquees *mq = ddo->graphics ? ddo->graphics->marquees : NULL;
	uint64_t                        now;
	if (!mq || ddo->background_paused)
		return UINT32_MAX;
	uint64_t due = mq->next_due[0], active = mq->next_due[ddo->graphics->active_graphic_window];
	if (active && (!due || active < due))
		due = active;
	if (!due || st_time_monotonic_ms(&now))
		return UINT32_MAX;
	return due > now ? (uint32_t)(due - now) : 0;
}

struct update {
	struct st_ddofx *ddo;
	uint64_t         now;
};

static int
scroll(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	const struct update      *update  = context;
	struct st_ddofx_graphics *g       = update->ddo->graphics;
	struct st_ddofx_marquees *mq      = g->marquees;
	struct bounds             damage  = {WIDTH, -1, HEIGHT, -1};
	struct bounds             desktop = damage;
	const uint8_t            *font    = st_ddofx_vga_bitmap(update->ddo);
	if (!font || pixels->width != WIDTH || pixels->height != HEIGHT)
		return 0;
	for (unsigned i = 0; i < MARQUEE_COUNT; ++i) {
		struct marquee *m = &mq->slots[i];
		if (!m->due || m->due > update->now || (m->owner && m->owner != g->active_graphic_window))
			continue;
		unsigned ring       = ring_width(g, m);
		m->first_pixel      = (m->first_pixel + ring - 1) % ring;
		m->on_visual = true;
		m->due              = update->now + m->period;
		struct bounds strip = strip_bounds(g, m);
		if (!m->owner) {
			st_ddofx_extend_bounds(&desktop, strip);
			continue;
		}
		struct st_ddofx_surface target = st_ddofx_surface(g, m->owner, VISUAL);
		struct bounds           extent = {0, target.width - 1, 0, target.height - 1};
		if (target.data && st_ddofx_intersect_bounds(&strip, &extent)) {
			paint(m, font, g, &target, NULL, strip);
			st_ddofx_repaint_animation_faces(g, m->owner, NULL, strip);
			st_ddofx_publish_page_area(update->ddo, m->owner, VISUAL, pixels, strip, &damage);
		}
	}
	if (desktop.right >= desktop.left)
		st_ddofx_publish_page_area(update->ddo, 0, VISUAL, pixels, desktop, &damage);
	if (damage.right >= damage.left)
		*changed = (struct st_console_rect) {damage.left, damage.top, damage.right - damage.left + 1,
		    damage.bottom - damage.top + 1};
	next_deadline(mq);
	return 0;
}

void
st_ddofx_marquee_poll(struct st_ddofx *ddo)
{
	if (st_ddofx_marquee_wait_ms(ddo))
		return;
	struct update update = {.ddo = ddo};
	if (st_time_monotonic_ms(&update.now) == 0)
		(void)st_console_modify_pixels(ddo->graphics->display, scroll, &update);
}
