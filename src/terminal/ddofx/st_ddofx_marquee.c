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
	unsigned x, bottom, columns, ink, paper;
	uint64_t due;
	bool     on_visual;
};

struct st_ddofx_marquees {
	struct marquee slots[MARQUEE_COUNT];
	uint64_t       next_due;
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
	marquees->next_due = 0;
	for (unsigned i = 0; i < MARQUEE_COUNT; ++i) {
		uint64_t due = marquees->slots[i].due;
		if (due && (!marquees->next_due || due < marquees->next_due))
			marquees->next_due = due;
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
	if (!args || !*args || n[2] < 1 || n[2] > WIDTH / g->cell_width || n[0] < 0
	    || n[0] > WIDTH - g->cell_width * n[2] || n[1] < g->cell_height - 1 || n[1] >= HEIGHT
	    || (unsigned)n[3] > 15 || (unsigned)n[4] > 7 || n[5] < 1 || !ddo->resource_prefix[0]
	    || !sink->open_asset)
		return;
	char container[sizeof(ddo->resource_prefix) + 1];
	snprintf(container, sizeof(container), "%sM", ddo->resource_prefix);
	FILE *file = sink->open_asset(sink->context, "BBSTHEME", NULL, container);
	if (!file)
		return;
	uint32_t length;
	uint8_t *text = NULL;
	if (st_ddofx_resource_find(file, args, -1, &length, NULL) >= 0 && length && length <= 20000) {
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

static void
paint(const struct marquee *m, const uint8_t *font, struct st_ddofx_graphics *g,
    const struct st_pixel_buffer *pixels, const struct st_console_rect *clip)
{
	struct st_console_rect area = st_ddofx_text_area(g, m->x, m->bottom, m->columns, 1, 0);
	unsigned               ring = ring_width(g, m);
	unsigned               left = area.x, right = left + area.width, top = area.y, bottom = top + area.height;
	if (clip) {
		if (left < clip->x)
			left = clip->x;
		if (right > clip->x + clip->width)
			right = clip->x + clip->width;
		if (top < clip->y)
			top = clip->y;
		if (bottom > clip->y + clip->height)
			bottom = clip->y + clip->height;
	}
	for (unsigned y = top; y < bottom; ++y) {
		unsigned row      = 15 - (m->bottom - y);
		unsigned position = (m->first_pixel + left - m->x) % ring;
		for (unsigned x = left; x < right; ++x) {
			unsigned col = position / g->cell_width;
			uint8_t  ch  = col < m->length ? m->text[col] : ' ';
			unsigned colour =
			    (font[ch * 16 + row] & (0x80u >> (position % g->cell_width))) ? m->ink : m->paper;
			if (pixels) {
				size_t at           = y * WIDTH + x;
				pixels->primary[at] = pixels->alternate[at] = colour;
			}
			else
				g->page[WORK][y][x] = (uint8_t)colour;
			if (++position == ring)
				position = 0;
		}
	}
}

void
st_ddofx_repaint_marquees(struct st_ddofx *ddo, const struct st_pixel_buffer *pixels,
    const struct st_console_rect *clip)
{
	struct st_ddofx_marquees *mq = ddo->graphics->marquees;
	if (!mq || !mq->next_due)
		return;
	const uint8_t *font = st_ddofx_vga_bitmap(ddo);
	if (!font)
		return;
	for (unsigned i = 0; i < MARQUEE_COUNT; ++i) {
		struct marquee *m = &mq->slots[i];
		if (m->due && m->on_visual)
			paint(m, font, ddo->graphics, pixels, clip);
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
				if (font && st_time_monotonic_ms(&now) == 0) {
					paint(m, font, g, NULL, NULL);
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
	if (!mq || !mq->next_due || ddo->background_paused || st_time_monotonic_ms(&now))
		return UINT32_MAX;
	return mq->next_due > now ? (uint32_t)(mq->next_due - now) : 0;
}

struct update {
	struct st_ddofx_graphics *g;
	const uint8_t            *font;
	uint64_t                  now;
};

static int
scroll(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	struct update *update = context;
	if (pixels->width != WIDTH || pixels->height != HEIGHT)
		return 0;
	struct st_ddofx_marquees *mq   = update->g->marquees;
	unsigned                  left = WIDTH, right = 0, top = HEIGHT, bottom = 0;
	for (unsigned i = 0; i < MARQUEE_COUNT; ++i) {
		struct marquee *m = &mq->slots[i];
		if (!m->due || m->due > update->now)
			continue;
		struct st_console_rect area = st_ddofx_text_area(update->g, m->x, m->bottom, m->columns, 1, 0);
		unsigned               ring = ring_width(update->g, m);
		m->first_pixel              = (m->first_pixel + ring - 1) % ring;
		paint(m, update->font, update->g, pixels, NULL);
		m->on_visual = true;
		m->due       = update->now + m->period;
		if (m->x < left)
			left = m->x;
		if (area.x + area.width > right)
			right = area.x + area.width;
		if (area.y < top)
			top = area.y;
		if (area.y + area.height > bottom)
			bottom = area.y + area.height;
	}
	if (right > left)
		*changed = (struct st_console_rect) {left, top, right - left, bottom - top};
	next_deadline(mq);
	return 0;
}

void
st_ddofx_marquee_poll(struct st_ddofx *ddo)
{
	struct st_ddofx_marquees *mq = ddo->graphics ? ddo->graphics->marquees : NULL;
	uint64_t                  now;
	if (!mq || !mq->next_due || ddo->background_paused || st_time_monotonic_ms(&now) || now < mq->next_due)
		return;
	struct update update = {.g = ddo->graphics, .font = st_ddofx_vga_bitmap(ddo), .now = now};
	if (update.font)
		(void)st_console_modify_pixels(update.g->display, scroll, &update);
}
