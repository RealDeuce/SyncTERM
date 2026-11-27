/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>

#include "st_ddofx_graphics_internal.h"
#include "st_time.h"
#include "string_util.h"

struct bar {
	int                    x, y, length, thickness, direction;
	int                    colour, first, last, step;
	bool                   cycle, wrap;
	struct st_console_rect painted;
};

struct st_ddofx_bars {
	struct bar tracks[ST_TRACKER_METER_CHANNELS];
	uint64_t   next_due;
	bool       enabled;
};

struct bar_frame {
	struct st_ddofx_graphics *graphics;
	struct st_console_rect    next[ST_TRACKER_METER_CHANNELS];
};

static struct st_console_rect
rectangle(const struct bar *bar, unsigned length)
{
	if (!length)
		return (struct st_console_rect) {0};
	unsigned x = (unsigned)bar->x, y = (unsigned)bar->y, thickness = (unsigned)bar->thickness;
	switch (bar->direction) {
		case 1:
			return (struct st_console_rect) {x, y + 1 - length, thickness, length};
		case 2:
			return (struct st_console_rect) {x, y, thickness, length};
		case 3:
			return (struct st_console_rect) {x, y + 1 - thickness, length, thickness};
		default:
			return (struct st_console_rect) {x + 1 - length, y + 1 - thickness, length, thickness};
	}
}

static void
next_colour(struct bar *bar)
{
	if (!bar->cycle || bar->first == bar->last)
		return;
	int next = bar->colour + bar->step;
	if (next < bar->first || next > bar->last) {
		if (bar->wrap)
			next = bar->first;
		else {
			bar->step = -bar->step;
			next      = bar->colour + bar->step;
		}
	}
	bar->colour = next;
}

static int
paint(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	struct bar_frame         *frame = context;
	struct st_ddofx_graphics *g     = frame->graphics;
	if (pixels->width != WIDTH || pixels->height != HEIGHT)
		return -1;
	unsigned left = WIDTH, right = 0, top = HEIGHT, bottom = 0;
	/* Restore exposed background from HP before painting any track, so
	 * shrinking or redefining a bar cannot erase an overlapping track. */
	for (unsigned pass = 0; pass < 2; ++pass) {
		for (unsigned i = 0; i < ST_TRACKER_METER_CHANNELS; ++i) {
			struct bar            *bar  = &g->bars->tracks[i];
			struct st_console_rect next = frame->next[i];
			struct st_console_rect r    = pass ? next : bar->painted;
			for (unsigned y = r.y; y < r.y + r.height; ++y) {
				for (unsigned x = r.x; x < r.x + r.width; ++x) {
					if (!pass && x >= next.x && x < next.x + next.width && y >= next.y
					    && y < next.y + next.height)
						continue;
					size_t   pos    = y * WIDTH + x;
					uint32_t colour = pass ? (uint32_t)bar->colour : g->page[HIDDEN][y][x];
					if (pixels->primary[pos] == colour && pixels->alternate[pos] == colour)
						continue;
					pixels->primary[pos] = pixels->alternate[pos] = colour;
					if (x < left)
						left = x;
					if (x > right)
						right = x;
					if (y < top)
						top = y;
					if (y > bottom)
						bottom = y;
				}
			}
			if (pass)
				bar->painted = next;
		}
	}
	if (left <= right)
		*changed = (struct st_console_rect) {left, top, right - left + 1, bottom - top + 1};
	return 0;
}

bool
st_ddofx_bar_command(struct st_ddofx *ddo, char *body)
{
	bool define = !ascii_case_ncompare(body, "7DEFBAR,", 8);
	bool enable = !ascii_case_ncompare(body, "7MUSBAR,", 8);
	if (!define && !enable && ascii_case_compare(body, "7CLRBAR"))
		return false;
	struct st_ddofx_graphics *g = ddo->graphics;
	if (!g->bars && (define || enable))
		g->bars = calloc(1, sizeof(*g->bars));
	if (!g->bars)
		return true;
	if (define) {
		int32_t n[11];
		if (!st_ddofx_numbers(ddo, body + 8, n, 11) || n[0] < 1 || n[0] > ST_TRACKER_METER_CHANNELS
		    || n[1] <= 0 || n[1] >= WIDTH || n[2] <= 0 || n[2] >= HEIGHT || n[3] < 1 || n[3] > WIDTH / 4
		    || n[4] < 1 || n[4] > WIDTH || (unsigned)n[5] > 255 || (unsigned)n[6] > 1 || n[10] < 1
		    || n[10] > 4)
			return true;
		if (n[6] && ((unsigned)n[7] > 255 || (unsigned)n[8] > 255 || n[7] > n[8] || (unsigned)n[9] > 1))
			return true;
		struct bar            *slot = &g->bars->tracks[n[0] - 1];
		struct bar             bar  = {.x = n[1],
		    .y                           = n[2],
		    .length                      = n[3],
		    .thickness                   = n[4],
		    .colour                      = n[5],
		    .cycle                       = n[6],
		    .first                       = n[7],
		    .last                        = n[8],
		    .wrap                        = n[9] == 1,
		    .step                        = 1,
		    .direction                   = n[10],
		    .painted                     = slot->painted};
		struct st_console_rect r    = rectangle(&bar, (unsigned)bar.length * 4);
		if (!r.x || r.x >= WIDTH || !r.y || r.y >= HEIGHT || r.width > WIDTH - r.x
		    || r.height > HEIGHT - r.y)
			return true;
		if (bar.cycle && (bar.colour < bar.first || bar.colour > bar.last))
			bar.colour = bar.first;
		*slot = bar;
	}
	else if (enable) {
		int32_t value;
		if (st_ddofx_numbers(ddo, body + 8, &value, 1) && (value == 0 || value == 1)) {
			g->bars->enabled  = value == 1;
			g->bars->next_due = 0;
		}
	}
	else {
		struct bar_frame frame = {.graphics = g};
		(void)st_console_modify_pixels(g->display, paint, &frame);
	}
	return true;
}

uint32_t
st_ddofx_bar_wait_ms(const struct st_ddofx *ddo)
{
	const struct st_ddofx_bars *bars = ddo->graphics ? ddo->graphics->bars : NULL;
	if (!bars || !bars->enabled || !ddo->player || ddo->background_paused)
		return UINT32_MAX;
	uint64_t now;
	if (st_time_monotonic_ms(&now) != 0)
		return UINT32_MAX;
	return bars->next_due > now ? (uint32_t)(bars->next_due - now) : 0;
}

void
st_ddofx_bar_poll(struct st_ddofx *ddo)
{
	if (st_ddofx_bar_wait_ms(ddo) != 0)
		return;
	struct st_ddofx_bars *bars = ddo->graphics->bars;
	uint64_t              now;
	if (st_time_monotonic_ms(&now) != 0)
		return;
	bars->next_due         = now + 50;
	struct bar_frame frame = {.graphics = ddo->graphics};
	uint8_t          levels[ST_TRACKER_METER_CHANNELS];
	unsigned         count = st_tracker_levels(ddo->player, levels);
	for (unsigned i = 0; i < count; ++i) {
		struct bar *bar = &bars->tracks[i];
		if (bar->length == 0)
			continue;
		unsigned steps = ((unsigned)bar->length * levels[i] + 127) / 255;
		frame.next[i]  = rectangle(bar, 4 * steps);
	}
	if (st_console_modify_pixels(ddo->graphics->display, paint, &frame) == 0)
		for (unsigned i = 0; i < count; ++i)
			next_colour(&bars->tracks[i]);
}
