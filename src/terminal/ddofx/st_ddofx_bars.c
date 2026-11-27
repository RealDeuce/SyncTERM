/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_time.h"
#include "string_util.h"

struct bar {
	int                    x, y, length, thickness, direction;
	int                    colour, first, last, step;
	uint8_t                painted_colour;
	bool                   cycle, wrap;
	struct st_console_rect painted;
};

struct st_ddofx_bars {
	struct bar tracks[ST_TRACKER_METER_CHANNELS];
	uint64_t   next_due;
	unsigned   owner;
	bool       enabled;
};

struct bar_frame {
	struct st_ddofx       *ddo;
	struct st_console_rect next[ST_TRACKER_METER_CHANNELS];
	unsigned               count;
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

/* The desktop's visual page is the console itself. Composite its bars
 * after restoring HP and before painting any covering graphic windows. */
void
st_ddofx_repaint_desktop_bars(const struct st_ddofx_graphics *g, const struct st_pixel_buffer *pixels,
    struct bounds clip)
{
	if (!g->bars || g->bars->owner)
		return;
	for (unsigned i = 0; i < ST_TRACKER_METER_CHANNELS; ++i) {
		const struct bar      *bar = &g->bars->tracks[i];
		struct st_console_rect r   = bar->painted;
		if (!r.width)
			continue;
		int left = (int)r.x, right = left + (int)r.width - 1;
		int top = (int)r.y, bottom = top + (int)r.height - 1;
		if (left < clip.left)
			left = clip.left;
		if (right > clip.right)
			right = clip.right;
		if (top < clip.top)
			top = clip.top;
		if (bottom > clip.bottom)
			bottom = clip.bottom;
		for (int y = top; y <= bottom; ++y)
			for (int x = left; x <= right; ++x)
				pixels->primary[y * WIDTH + x] = pixels->alternate[y * WIDTH + x] =
				    bar->painted_colour;
	}
}

static bool
fits(struct st_console_rect r, unsigned width, unsigned height)
{
	return r.x && r.x < width && r.width <= width - r.x && r.y && r.y < height && r.height <= height - r.y;
}

static int
paint(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	struct bar_frame               *frame = context;
	struct st_ddofx_graphics       *g     = frame->ddo->graphics;
	struct st_ddofx_bars           *bars  = g->bars;
	struct st_ddofx_graphic_window *w     = &g->graphic_windows[bars->owner];
	if (pixels->width != WIDTH || pixels->height != HEIGHT || (bars->owner && !w->visual))
		return 0;
	struct st_console_rect previous[ST_TRACKER_METER_CHANNELS];
	struct bounds          damage = {WIDTH, -1, HEIGHT, -1};
	/* Restore every previous footprint before drawing overlapping bars.
	 * A closed window discards its painted rectangles along with its VGP. */
	for (unsigned i = 0; i < ST_TRACKER_METER_CHANNELS; ++i) {
		struct bar            *bar = &bars->tracks[i];
		struct st_console_rect r = previous[i] = bar->painted;
		if (w->visual && r.width) {
			for (unsigned y = r.y; y < r.y + r.height; ++y) {
				size_t at = (size_t)y * w->width + r.x;
				memcpy(w->visual + at, w->hidden_page + at, r.width);
			}
		}
		bar->painted = frame->next[i];
		if (i < frame->count)
			bar->painted_colour = bar->colour;
	}
	for (unsigned i = 0; i < ST_TRACKER_METER_CHANNELS && w->visual; ++i) {
		const struct bar      *bar = &bars->tracks[i];
		struct st_console_rect r   = bar->painted;
		for (unsigned y = r.y; y < r.y + r.height; ++y)
			memset(w->visual + (size_t)y * w->width + r.x, bar->painted_colour, r.width);
	}
	for (unsigned i = 0; i < ST_TRACKER_METER_CHANNELS; ++i) {
		struct st_console_rect rects[] = {previous[i], frame->next[i]};
		for (unsigned j = 0; j < 2; ++j) {
			struct st_console_rect r = rects[j];
			if (!r.width
			    || (!j && r.x == rects[1].x && r.y == rects[1].y && r.width == rects[1].width
			        && r.height == rects[1].height))
				continue;
			struct bounds b = {r.x, r.x + r.width - 1, r.y, r.y + r.height - 1};
			st_ddofx_publish_page_area(frame->ddo, bars->owner, HIDDEN, pixels, b, &damage);
		}
	}
	if (damage.left <= damage.right)
		*changed = (struct st_console_rect) {damage.left, damage.top, damage.right - damage.left + 1,
		    damage.bottom - damage.top + 1};
	return 0;
}

void
st_ddofx_bar_window_closed(struct st_ddofx_graphics *g, unsigned owner)
{
	if (g->bars && g->bars->owner == owner)
		for (unsigned i = 0; i < ST_TRACKER_METER_CHANNELS; ++i)
			g->bars->tracks[i].painted = (struct st_console_rect) {0};
}

static void
clear(struct st_ddofx *ddo)
{
	struct bar_frame frame = {.ddo = ddo};
	(void)st_console_modify_pixels(ddo->graphics->display, paint, &frame);
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
		unsigned                owner  = g->alert_graphic_window;
		struct st_ddofx_surface target = st_ddofx_surface(g, owner, VISUAL);
		int32_t                 n[11];
		if (!st_ddofx_numbers(ddo, body + 8, n, 11) || n[0] < 1 || n[0] > ST_TRACKER_METER_CHANNELS
		    || n[1] <= 0 || n[1] >= target.width || n[2] <= 0 || n[2] >= target.height || n[3] < 1
		    || n[3] > (n[10] <= 2 ? target.height : target.width) / 4 || n[4] < 1
		    || n[4] > (n[10] <= 2 ? target.width : target.height) || (unsigned)n[5] > 255
		    || (unsigned)n[6] > 1 || n[10] < 1 || n[10] > 4)
			return true;
		if (n[6] && ((unsigned)n[7] > 255 || (unsigned)n[8] > 255 || n[7] > n[8] || (unsigned)n[9] > 1))
			return true;
		struct bar *slot = &g->bars->tracks[n[0] - 1];
		struct bar  bar  = {
		    .x         = n[1],
		    .y         = n[2],
		    .length    = n[3],
		    .thickness = n[4],
		    .colour    = n[5],
		    .cycle     = n[6],
		    .first     = n[7],
		    .last      = n[8],
		    .wrap      = n[9] == 1,
		    .step      = 1,
		    .direction = n[10],
		};
		struct st_console_rect r = rectangle(&bar, (unsigned)bar.length * 4);
		if (!fits(r, target.width, target.height))
			return true;
		if (g->bars->owner != owner) {
			clear(ddo);
			g->bars->owner = owner;
		}
		bar.painted        = slot->painted;
		bar.painted_colour = slot->painted_colour;
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
	else
		clear(ddo);
	return true;
}

uint32_t
st_ddofx_bar_wait_ms(const struct st_ddofx *ddo)
{
	const struct st_ddofx_bars *bars = ddo->graphics ? ddo->graphics->bars : NULL;
	if (!bars || !bars->enabled || !ddo->player || ddo->background_paused
	    || (bars->owner && bars->owner != ddo->graphics->active_graphic_window))
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
	bars->next_due                 = now + 50;
	struct bar_frame        frame  = {.ddo = ddo};
	struct st_ddofx_surface target = st_ddofx_surface(ddo->graphics, bars->owner, VISUAL);
	uint8_t                 levels[ST_TRACKER_METER_CHANNELS];
	unsigned                count = st_tracker_levels(ddo->player, levels);
	frame.count                   = count;
	for (unsigned i = count; i < ST_TRACKER_METER_CHANNELS; ++i)
		frame.next[i] = bars->tracks[i].painted;
	for (unsigned i = 0; i < count; ++i) {
		struct bar *bar = &bars->tracks[i];
		if (!bar->length || !fits(rectangle(bar, (unsigned)bar->length * 4), target.width, target.height))
			continue;
		unsigned steps = ((unsigned)bar->length * levels[i] + 127) / 255;
		frame.next[i]  = rectangle(bar, 4 * steps);
	}
	if (st_console_modify_pixels(ddo->graphics->display, paint, &frame) == 0)
		for (unsigned i = 0; i < count; ++i)
			next_colour(&bars->tracks[i]);
}
