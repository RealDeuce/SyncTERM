/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_script.h"
#include "st_time.h"
#include "string_util.h"
#include "value_convert_time.h"

enum {
	CLOCK_COUNT = 30
};

enum clock_kind {
	ANALOG,
	DIGITAL,
	CLOCK_KINDS
};

struct clock_face {
	struct st_console_rect bounds;
	int                    offset;
	unsigned               window;
	uint8_t                colours[5];
	uint8_t               *font;
	bool                   running;
};

struct st_ddofx_clocks {
	int               seconds;
	struct clock_face faces[CLOCK_KINDS][CLOCK_COUNT];
	uint64_t          due;
};

static void
clear_family(struct st_ddofx_clocks *clocks, enum clock_kind kind)
{
	for (int i = 0; i < CLOCK_COUNT; ++i)
		free(clocks->faces[kind][i].font);
	memset(clocks->faces[kind], 0, sizeof(clocks->faces[kind]));
}

void
st_ddofx_clear_clocks(struct st_ddofx_graphics *g)
{
	if (g->clocks) {
		clear_family(g->clocks, DIGITAL);
		free(g->clocks);
		g->clocks = NULL;
	}
}

static int
offset_seconds(const int32_t *fields)
{
	return (int)((fields[0] + 60LL * fields[1] + 3600LL * fields[2]) % 86400);
}

static bool
define_analog(struct st_ddofx *ddo, char *args, struct clock_face *face)
{
	int32_t n[11];
	if (!st_ddofx_numbers(ddo, args, n, 11))
		return false;
	int32_t                 x = n[0], y = n[1], r = n[2];
	struct st_ddofx_surface target = st_ddofx_surface(ddo->graphics, face->window, VISUAL);
	if (r <= 0 || r >= target.height || x < r || x >= target.width - r || y < r || y >= target.height - r)
		return false;
	for (int i = 0; i < 5; ++i) {
		if ((unsigned)n[3 + i] > 255)
			return false;
		face->colours[i] = (uint8_t)n[3 + i];
	}
	face->bounds = (struct st_console_rect) {x - r, y - r, 2 * r + 1, 2 * r + 1};
	face->offset = offset_seconds(n + 8);
	return true;
}

static bool
define_digital(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *args, struct clock_face *face)
{
	int32_t n[7];
	for (int i = 0; i < 7; ++i)
		if (!st_ddofx_field_number(ddo, &args, &n[i]))
			return false;
	struct st_ddofx_surface target = st_ddofx_surface(ddo->graphics, face->window, VISUAL);
	if (!args || n[0] < 0 || n[0] > target.width - 8 * ddo->graphics->cell_width
	    || n[1] < ddo->graphics->cell_height - 1 || n[1] >= target.height || (unsigned)n[2] > 15
	    || (unsigned)n[3] > 7)
		return false;
	face->font = malloc(4096);
	if (!face->font)
		return false;
	const char *font = st_ddofx_value(ddo, args);
	if (!(*font && st_ddofx_read_vga_font(sink, font, face->font))
	    && !st_ddofx_read_vga_font(sink, "IBMSTD.FNT", face->font)) {
		free(face->font);
		return false;
	}
	face->bounds     = (struct st_console_rect) {n[0], n[1] + 1 - ddo->graphics->cell_height,
	    8 * ddo->graphics->cell_width, ddo->graphics->cell_height};
	face->colours[0] = (uint8_t)n[2];
	face->colours[1] = (uint8_t)n[3];
	face->offset     = offset_seconds(n + 4);
	return true;
}

static void
clock_pixel(const struct st_ddofx_surface *target, const struct st_pixel_buffer *pixels, int pos, uint8_t colour)
{
	if (pixels)
		st_pixel_buffer_set_solid(pixels, pos, colour);
	else
		target->data[pos] = colour;
}

static void
radial_line(const struct st_ddofx_surface *target, const struct st_pixel_buffer *pixels, int x, int y, int angle,
    int inner, int outer, uint8_t colour, const struct bounds *clip)
{
	double cosine, sine;
	st_ddofx_rotation(angle, &cosine, &sine);
	for (int r = inner; r <= outer; ++r) {
		int px = x + (int)lround(r * sine), py = y - (int)lround(r * cosine);
		if (px < clip->left || px > clip->right || py < clip->top || py > clip->bottom)
			continue;
		clock_pixel(target, pixels, py * target->width + px, colour);
	}
}

static struct st_console_rect
face_area(const struct st_ddofx_graphics *g, const struct clock_face *face)
{
	if (!face->font)
		return face->bounds;
	unsigned bottom = face->bounds.y + face->bounds.height;
	unsigned height = bottom < (unsigned)g->cell_height ? bottom : (unsigned)g->cell_height;
	return (struct st_console_rect){face->bounds.x, bottom - height, 8 * g->cell_width, height};
}

static void
draw_face(struct st_ddofx_graphics *g, const struct clock_face *face, const struct st_pixel_buffer *pixels,
    int now, const struct bounds *clip)
{
	int time = (now + face->offset + 86400) % 86400;
	int hour = time / 3600, minute = time / 60 % 60, second = time % 60;
	if (face->font) {
		uint8_t text[8] = {'0' + hour / 10, '0' + hour % 10, ':', '0' + minute / 10, '0' + minute % 10,
		    ':', '0' + second / 10, '0' + second % 10};
		struct st_console_rect a       = face_area(g, face);
		struct bounds          visible = *clip;
		if (visible.left < (int)a.x)
			visible.left = (int)a.x;
		if (visible.top < (int)a.y)
			visible.top = (int)a.y;
		if (visible.right >= (int)(a.x + a.width))
			visible.right = (int)(a.x + a.width) - 1;
		if (visible.bottom >= (int)(a.y + a.height))
			visible.bottom = (int)(a.y + a.height) - 1;
		st_ddofx_paint_vga_string(g, face->window, pixels, face->font, text, 8, a.x, a.y + a.height - 1,
		    face->colours[0], face->colours[1], &visible);
		return;
	}
	struct st_ddofx_surface target = st_ddofx_surface(g, face->window, VISUAL);
	int                     radius = (int)face->bounds.width / 2;
	int                     x = (int)face->bounds.x + radius, y = (int)face->bounds.y + radius;
	for (int dy = -radius; dy <= radius; ++dy) {
		if (y + dy < clip->top || y + dy > clip->bottom)
			continue;
		int span  = (int)sqrt(radius * radius - dy * dy);
		int left  = x - span < clip->left ? clip->left : x - span,
		    right = x + span > clip->right ? clip->right : x + span;
		for (int at = (y + dy) * target.width + left, end = (y + dy) * target.width + right; at <= end;
		    ++at)
			clock_pixel(&target, pixels, at, face->colours[0]);
	}
	for (int tick = 0; tick < 60; ++tick) {
		int inner = radius - radius / (tick % 5 == 0 ? 10 : 20);
		radial_line(&target, pixels, x, y, 6 * tick, inner, radius, face->colours[1], clip);
	}
	radial_line(&target, pixels, x, y, 30 * (hour % 12) + minute / 2, 0, radius - radius / 2, face->colours[4],
	    clip);
	radial_line(&target, pixels, x, y, 6 * minute, 0, radius - radius / 5, face->colours[3], clip);
	radial_line(&target, pixels, x, y, 6 * second, 0, radius - radius / 10, face->colours[2], clip);
}

void
st_ddofx_repaint_clocks(struct st_ddofx *ddo, unsigned window, const struct st_pixel_buffer *pixels,
    const struct st_console_rect *area)
{
	const struct st_ddofx_clocks *clocks = ddo->graphics->clocks;
	if (!clocks || !clocks->due)
		return;
	struct bounds clip = {(int)area->x, (int)(area->x + area->width) - 1, (int)area->y,
	    (int)(area->y + area->height) - 1};
	for (int kind = 0; kind < CLOCK_KINDS; ++kind)
		for (int i = 0; i < CLOCK_COUNT; ++i) {
			const struct clock_face *face = &clocks->faces[kind][i];
			struct st_console_rect   b    = face_area(ddo->graphics, face);
			if (face->running && face->window == window && area->x < b.x + b.width
			    && area->x + area->width > b.x && area->y < b.y + b.height
			    && area->y + area->height > b.y)
				draw_face(ddo->graphics, face, pixels, clocks->seconds, &clip);
		}
}

static int
paint(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *damage)
{
	struct st_ddofx          *ddo     = context;
	struct st_ddofx_graphics *g       = ddo->graphics;
	struct bounds             changed = {WIDTH, -1, HEIGHT, -1};
	struct bounds             desktop = changed;
	if (pixels->width != WIDTH || pixels->height != HEIGHT)
		return 0;
	for (int kind = 0; kind < CLOCK_KINDS; ++kind) {
		for (int i = 0; i < CLOCK_COUNT; ++i) {
			const struct clock_face *face = &g->clocks->faces[kind][i];
			if (!face->running || (face->window && face->window != g->active_graphic_window))
				continue;
			struct st_ddofx_surface target = st_ddofx_surface(g, face->window, VISUAL);
			struct st_console_rect  area   = face_area(g, face);
			if (area.x >= (unsigned)target.width || area.y >= (unsigned)target.height)
				continue;
			struct bounds clip = {area.x, area.x + area.width - 1, area.y, area.y + area.height - 1};
			if (clip.right >= target.width)
				clip.right = target.width - 1;
			if (clip.bottom >= target.height)
				clip.bottom = target.height - 1;
			if (face->window) {
				draw_face(g, face, NULL, g->clocks->seconds, &clip);
				st_ddofx_repaint_marquees(ddo, face->window, NULL, clip);
				st_ddofx_publish_page_area(ddo, face->window, VISUAL, pixels, clip, &changed);
			}
			else
				st_ddofx_extend_bounds(&desktop, clip);
		}
	}
	if (desktop.right >= desktop.left)
		st_ddofx_publish_page_area(ddo, 0, VISUAL, pixels, desktop, &changed);
	if (changed.right >= changed.left)
		*damage = (struct st_console_rect) {changed.left, changed.top, changed.right - changed.left + 1,
		    changed.bottom - changed.top + 1};
	return 0;
}

uint32_t
st_ddofx_clock_wait_ms(const struct st_ddofx *ddo)
{
	const struct st_ddofx_clocks *clocks = ddo->graphics ? ddo->graphics->clocks : NULL;
	uint64_t                      now;
	if (!clocks || !clocks->due || ddo->background_paused || st_time_monotonic_ms(&now))
		return UINT32_MAX;
	return clocks->due > now ? (uint32_t)(clocks->due - now) : 0;
}

void
st_ddofx_clock_poll(struct st_ddofx *ddo)
{
	struct st_ddofx_clocks *clocks = ddo->graphics ? ddo->graphics->clocks : NULL;
	uint64_t                now;
	if (!clocks || !clocks->due || ddo->background_paused || st_time_monotonic_ms(&now) || now < clocks->due)
		return;
	struct timespec wall;
	struct tm       local;
	clocks->due = now + 1000;
	if (timespec_get(&wall, TIME_UTC) != TIME_UTC || !value_convert_localtime(wall.tv_sec, &local))
		return;
	clocks->due -= (uint64_t)wall.tv_nsec / 1000000;
	clocks->seconds = 3600 * local.tm_hour + 60 * local.tm_min + local.tm_sec;
	(void)st_console_modify_pixels(ddo->graphics->display, paint, ddo);
}

bool
st_ddofx_clock_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *verb, char *args)
{
	enum clock_action {
		DEFINE,
		SHOW,
		STOP,
		KILL
	};

	static const struct {
		const char       *verb;
		enum clock_kind   kind;
		enum clock_action action;
	} commands[] = {
	    {"5SCLOCK",  ANALOG,  DEFINE},
            {"5SHWCLK",  ANALOG,  SHOW  },
            {"5STPCLK",  ANALOG,  STOP  },
	    {"5SOCLOCK", DIGITAL, DEFINE},
            {"5UOCLOCK", DIGITAL, SHOW  },
            {"5POCLOCK", DIGITAL, STOP  },
	    {"5KOCLOCK", DIGITAL, KILL  }
        };

	unsigned cmd = 0;
	while (cmd < sizeof(commands) / sizeof(*commands) && ascii_case_compare(verb, commands[cmd].verb))
		++cmd;
	if (cmd == sizeof(commands) / sizeof(*commands))
		return false;
	int32_t id;
	if (!st_ddofx_field_number(ddo, &args, &id) || id < 0 || id > CLOCK_COUNT)
		return true;
	enum clock_action       action = commands[cmd].action;
	enum clock_kind         kind   = commands[cmd].kind;
	struct st_ddofx_clocks *clocks = ddo->graphics->clocks;
	if (action == DEFINE && id) {
		struct clock_face next = {.window = ddo->graphics->alert_graphic_window};
		if (!(kind == ANALOG ? define_analog(ddo, args, &next) : define_digital(ddo, sink, args, &next)))
			return true;
		if (!clocks)
			ddo->graphics->clocks = clocks = calloc(1, sizeof(*clocks));
		if (!clocks) {
			free(next.font);
			return true;
		}
		free(clocks->faces[kind][id - 1].font);
		clocks->faces[kind][id - 1] = next;
	}
	else if (!clocks)
		return true;
	else if (action == DEFINE)
		clear_family(clocks, kind);
	else if (id && !args) {
		struct clock_face *face = &clocks->faces[kind][id - 1];
		if (action == KILL) {
			free(face->font);
			*face = (struct clock_face) {0};
		}
		else if (action == SHOW && face->bounds.width) {
			face->running = true;
			clocks->due   = 1;
			st_ddofx_clock_poll(ddo);
			return true;
		}
		else if (action == STOP)
			face->running = false;
	}
	for (int k = 0; k < CLOCK_KINDS; ++k)
		for (int i = 0; i < CLOCK_COUNT; ++i)
			if (clocks->faces[k][i].running)
				return true;
	clocks->due = 0;
	return true;
}
