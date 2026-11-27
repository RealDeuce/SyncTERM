/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_script.h"
#include "string_util.h"

static struct bounds
interior(const struct st_ddofx_graphic_window *w)
{
	struct bounds b = w->frame;
	if (!w->borderless) {
		b.left += 12;
		b.right -= 12;
		b.top += 20;
		b.bottom -= 14;
	}
	return b;
}

static bool
fits(const struct st_ddofx_graphic_window *w)
{
	struct bounds b = interior(w);
	return w->frame.left >= 0 && w->frame.right < WIDTH && w->frame.top >= 0 && w->frame.bottom < HEIGHT
	       && w->frame.left < w->frame.right && w->frame.top < w->frame.bottom && b.left <= b.right
	       && b.top <= b.bottom && b.right - b.left < w->width && b.bottom - b.top < w->height;
}

static void
change_origin(struct st_ddofx_graphic_window *w, int64_t x, int64_t y, bool clamp)
{
	struct bounds b    = interior(w);
	int           xmax = w->width - (b.right - b.left + 1), ymax = w->height - (b.bottom - b.top + 1);
	if (!clamp && (x < 0 || y < 0 || x > xmax || y > ymax))
		return;
	w->x = x < 0 ? 0 : x > xmax ? xmax : (int)x;
	w->y = y < 0 ? 0 : y > ymax ? ymax : (int)y;
}

struct repaint {
	struct st_ddofx *ddo;
	struct bounds    area;
};

static bool
intersect(struct bounds *b, const struct bounds *area)
{
	if (b->left < area->left)
		b->left = area->left;
	if (b->right > area->right)
		b->right = area->right;
	if (b->top < area->top)
		b->top = area->top;
	if (b->bottom > area->bottom)
		b->bottom = area->bottom;
	return b->left <= b->right && b->top <= b->bottom;
}

static int
paint(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	const struct repaint     *r = context;
	struct st_ddofx_graphics *g = r->ddo->graphics;
	struct bounds             b = r->area;
	if (pixels->width != WIDTH || pixels->height != HEIGHT)
		return -1;
	for (int y = b.top; y <= b.bottom; ++y)
		for (int x = b.left; x <= b.right; ++x)
			pixels->primary[y * WIDTH + x] = pixels->alternate[y * WIDTH + x] = g->page[HIDDEN][y][x];

	/* Inactive slots paint in numeric order. The active slot paints last. */
	for (unsigned i = 1; i <= GRAPHIC_WINDOWS + 1; ++i) {
		bool active = i > GRAPHIC_WINDOWS;
		if (!active && i == g->active_graphic_window)
			continue;
		const struct st_ddofx_graphic_window *w =
		    &g->graphic_windows[active ? g->active_graphic_window : i];
		struct bounds area = w->frame;
		if (!w->visual || w->hidden || !intersect(&area, &b))
			continue;
		struct bounds view         = interior(w);
		unsigned      title_colour = active ? 4 : 1;
		for (int y = area.top; y <= area.bottom; ++y)
			for (int x = area.left; x <= area.right; ++x) {
				unsigned colour = y < view.top ? title_colour : 7;
				if (x >= view.left && x <= view.right && y >= view.top && y <= view.bottom)
					colour =
					    w->visual[(w->y + y - view.top) * w->width + w->x + x - view.left];
				pixels->primary[y * WIDTH + x] = pixels->alternate[y * WIDTH + x] = colour;
			}
		struct bounds  title = {w->frame.left + 2, w->frame.right - 2, w->frame.top, view.top - 1};
		const uint8_t *font  = st_ddofx_vga_bitmap(r->ddo);
		if (!w->borderless && font && intersect(&title, &area))
			st_ddofx_paint_vga_string(g, pixels, font, (const uint8_t *)w->title,
			    (unsigned)strlen(w->title), w->frame.left + 2, view.top - 3, 14, title_colour, &title);
	}
	*changed = (struct st_console_rect) {b.left, b.top, b.right - b.left + 1, b.bottom - b.top + 1};
	return 0;
}

static void
refresh(struct st_ddofx *ddo, struct bounds area)
{
	struct repaint r = {ddo, area};
	(void)st_console_modify_pixels(ddo->graphics->display, paint, &r);
}

void
st_ddofx_refresh_graphic_interior(struct st_ddofx *ddo)
{
	struct st_ddofx_graphics       *g = ddo->graphics;
	struct st_ddofx_graphic_window *w = &g->graphic_windows[g->alert_graphic_window];
	if (w->visual && !w->hidden)
		refresh(ddo, interior(w));
}

static void
dispose(struct st_ddofx_graphics *g, unsigned slot)
{
	struct st_ddofx_graphic_window *w = &g->graphic_windows[slot];
	free(w->visual);
	free(w->hidden_page);
	*w = (struct st_ddofx_graphic_window) {0};
	if (g->active_graphic_window == slot)
		g->active_graphic_window = 0;
	if (g->alert_graphic_window == slot)
		g->alert_graphic_window = 0;
}

void
st_ddofx_free_graphic_windows(struct st_ddofx_graphics *g)
{
	for (unsigned i = 1; i <= GRAPHIC_WINDOWS; ++i)
		dispose(g, i);
}

static void
redraw_all(struct st_ddofx *ddo)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	for (unsigned i = 1; i <= GRAPHIC_WINDOWS; ++i)
		if (g->graphic_windows[i].visual && !g->graphic_windows[i].hidden)
			refresh(ddo, g->graphic_windows[i].frame);
}

static bool
read_frame(struct st_ddofx *ddo, char **args, struct st_ddofx_graphic_window *w)
{
	int32_t n[4];
	for (unsigned i = 0; i < 4; ++i)
		if (!st_ddofx_field_number(ddo, args, &n[i]))
			return false;
	if (n[0] < 0 || n[1] >= WIDTH || n[0] >= n[1] || n[2] < 0 || n[3] >= HEIGHT || n[2] >= n[3])
		return false;
	w->frame = (struct bounds) {n[0], n[1], n[2], n[3]};
	return true;
}

static bool
read_properties(struct st_ddofx *ddo, char *args, struct st_ddofx_graphic_window *w, bool create)
{
	int32_t n[9];
	for (unsigned i = 0; i < (create ? 9u : 8u); ++i)
		if (!st_ddofx_field_number(ddo, &args, &n[i]) || (unsigned)n[i] > 1)
			return false;
	if (!args)
		return false;
	w->hidden        = n[0];
	w->no_drag       = n[1];
	w->no_resize     = n[2];
	w->no_zoom       = n[3];
	w->no_restore    = n[4];
	w->no_icon       = n[5];
	w->no_horizontal = n[6];
	w->no_vertical   = n[7];
	if (create)
		w->borderless = n[8];
	snprintf(w->title, sizeof(w->title), "%.60s", st_ddofx_value(ddo, args));
	return fits(w);
}

static void
define_window(struct st_ddofx *ddo, unsigned slot, char *args, bool create)
{
	struct st_ddofx_graphics       *g           = ddo->graphics;
	struct st_ddofx_graphic_window *w           = &g->graphic_windows[slot];
	struct st_ddofx_graphic_window  next        = create ? (struct st_ddofx_graphic_window) {0} : *w;
	bool                            was_visible = w->visual && !w->hidden;
	struct bounds                   old         = w->frame;
	if ((!create && !w->visual) || !read_frame(ddo, &args, &next))
		return;
	int32_t active = 0;
	if (create) {
		int32_t width, height, icon;
		if (!st_ddofx_field_number(ddo, &args, &width) || !st_ddofx_field_number(ddo, &args, &height)
		    || !st_ddofx_field_number(ddo, &args, &icon) || !st_ddofx_field_number(ddo, &args, &active)
		    || width < 100 || width > 10000 || height < 100 || height > 10000 || (unsigned)icon > 300
		    || (unsigned)active > 1)
			return;
		next.width    = width;
		next.height   = height;
		next.icon     = icon;
		next.original = next.frame;
		next.clip     = (struct bounds) {0, width - 1, 0, height - 1};
	}
	if (!read_properties(ddo, args, &next, create))
		return;
	if (create) {
		next.visual      = calloc(next.width, next.height);
		next.hidden_page = calloc(next.width, next.height);
		if (!next.visual || !next.hidden_page) {
			free(next.visual);
			free(next.hidden_page);
			return;
		}
		dispose(g, slot);
		if (active)
			g->active_graphic_window = slot;
	}
	next.zoomed = false;
	change_origin(&next, next.x, next.y, true);
	*w = next;
	if (was_visible)
		refresh(ddo, old);
	if (active)
		redraw_all(ddo);
	else if (!next.hidden)
		refresh(ddo, next.frame);
}

static void
query(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char op, int32_t slot)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	char                      stats[80];
	const char               *value;
	if (slot < 1 || slot > GRAPHIC_WINDOWS)
		value = "-1";
	else {
		const struct st_ddofx_graphic_window *w = &g->graphic_windows[slot];
		if (op == 'V')
			value = w->visual ? "Y" : "N";
		else if (!w->visual)
			value = "-2";
		else if (op == 'S') {
			snprintf(stats, sizeof(stats), "%d,%d,%d,%d,%d,%d", w->frame.left, w->frame.right,
			    w->frame.top, w->frame.bottom, w->x, w->y);
			value = stats;
		}
		else
			value = (op == 'H' ? w->hidden : (unsigned)slot == g->active_graphic_window) ? "Y" : "N";
	}
	st_ddofx_query_reply(ddo, sink, value, value[0] == '-');
}

static void
restore(struct st_ddofx *ddo, struct st_ddofx_graphic_window *w, bool zoom)
{
	struct bounds old = w->frame;
	if (zoom && !w->zoomed) {
		int width      = w->width + (w->borderless ? 0 : 24);
		int height     = w->height + (w->borderless ? 0 : 34);
		w->before_zoom = old;
		w->frame       = (struct bounds) {0, width < WIDTH ? width - 1 : WIDTH - 1, 0,
		    height < HEIGHT ? height - 1 : HEIGHT - 1};
		w->zoomed      = true;
	}
	else {
		w->frame  = zoom ? w->before_zoom : w->original;
		w->zoomed = false;
	}
	change_origin(w, w->x, w->y, true);
	if (!w->hidden) {
		refresh(ddo, old);
		refresh(ddo, w->frame);
	}
}

static void
close_window(struct st_ddofx *ddo, unsigned slot)
{
	struct st_ddofx_graphic_window *w       = &ddo->graphics->graphic_windows[slot];
	struct bounds                   old     = w->frame;
	bool                            visible = w->visual && !w->hidden;

	dispose(ddo->graphics, slot);
	if (visible)
		refresh(ddo, old);
}

bool
st_ddofx_graphic_window_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *verb,
    char *args)
{
	if (!strchr(verb, 'W') && !strchr(verb, 'w') && ascii_case_compare(verb, "2VXY")
	    && ascii_case_ncompare(verb, "2CHVGP", 6))
		return false;
	struct st_ddofx_graphics *g = ddo->graphics;
	int32_t                   slot;
	if (!ascii_case_compare(verb, "2VXY")) {
		int32_t x, y;
		if (g->alert_graphic_window && st_ddofx_field_number(ddo, &args, &x)
		    && st_ddofx_field_number(ddo, &args, &y) && args)
			st_ddofx_resource_pcx(ddo, sink, VISUAL, st_ddofx_value(ddo, args), x, y);
	}
	else if (!ascii_case_compare(verb, "2FGWIN") && !args) {
		char reply[4];
		snprintf(reply, sizeof(reply), "%u", g->active_graphic_window);
		st_ddofx_query_reply(ddo, sink, reply, true);
	}
	else if (!ascii_case_compare(verb, "2CALMGWIN") && !args)
		g->alert_graphic_window = 0;
	else if (!ascii_case_compare(verb, "2RGWINI") && !args)
		st_ddofx_refresh_graphic_interior(ddo);
	else if (!ascii_case_compare(verb, "2RGWIN") && !args)
		redraw_all(ddo);
	else if (!ascii_case_compare(verb, "2RAGWIN") && !args) {
		for (unsigned i = 1; i <= GRAPHIC_WINDOWS; ++i)
			if (g->graphic_windows[i].visual)
				restore(ddo, &g->graphic_windows[i], false);
	}
	else {
		enum window_command {
			CREATE,
			CHANGE,
			ACTIVATE,
			CLOSE,
			ZOOM,
			RESTORE,
			ALERT,
			ORIGIN,
			RELATIVE,
			QUERY_HIDDEN,
			QUERY_ACTIVE,
			QUERY_CREATED,
			QUERY_STATS,
			COMMAND_COUNT
		};

		static const char *const names[COMMAND_COUNT] = {[CREATE] = "2GWIN",
		    [CHANGE]                                              = "2CHGWIN",
		    [ACTIVATE]                                            = "2AGWIN",
		    [CLOSE]                                               = "2CGWIN",
		    [ZOOM]                                                = "2ZGWIN",
		    [RESTORE]                                             = "2RGWIN",
		    [ALERT]                                               = "2ALERTGWIN",
		    [ORIGIN]                                              = "2CHVGP",
		    [RELATIVE]                                            = "2CHVGPR",
		    [QUERY_HIDDEN]                                        = "2HGWIN?",
		    [QUERY_ACTIVE]                                        = "2AGWIN?",
		    [QUERY_CREATED]                                       = "2VGWIN?",
		    [QUERY_STATS]                                         = "2SGWIN?"};

		unsigned command = 0;
		while (command < COMMAND_COUNT && ascii_case_compare(verb, names[command]))
			++command;
		if (command == COMMAND_COUNT)
			return false;
		if (!st_ddofx_field_number(ddo, &args, &slot))
			return true;

		if (command >= QUERY_HIDDEN) {
			if (!args)
				query(ddo, sink, names[command][1], slot);
			return true;
		}
		if (slot < 0 || slot > GRAPHIC_WINDOWS)
			return true;

		struct st_ddofx_graphic_window *w = &g->graphic_windows[slot];
		if (!slot && command == CREATE) {
			for (unsigned i = 1; i <= GRAPHIC_WINDOWS; ++i)
				close_window(ddo, i);
		}
		else if (command == ACTIVATE && !args && (!slot || (w->visual && !w->hidden))) {
			g->active_graphic_window = (unsigned)slot;
			redraw_all(ddo);
		}
		else if (slot && (command == CREATE || command == CHANGE))
			define_window(ddo, (unsigned)slot, args, command == CREATE);
		else if (!w->visual)
			return true;
		else if (command == ALERT && !args)
			g->alert_graphic_window = (unsigned)slot;
		else if (command == CLOSE && !args)
			close_window(ddo, (unsigned)slot);
		else if ((command == ZOOM || command == RESTORE) && !args)
			restore(ddo, w, command == ZOOM);
		else if (command == ORIGIN || command == RELATIVE) {
			int32_t xy[2];
			if (st_ddofx_numbers(ddo, args, xy, 2)) {
				change_origin(w, (int64_t)xy[0] + (command == RELATIVE ? w->x : 0),
				    (int64_t)xy[1] + (command == RELATIVE ? w->y : 0), command == RELATIVE);
				if (!w->hidden)
					refresh(ddo, interior(w));
			}
		}
	}
	return true;
}
