/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_script.h"
#include "string_util.h"
#include "st_time.h"

struct bounds
st_ddofx_window_interior(const struct st_ddofx_graphic_window *w)
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
	struct bounds b = st_ddofx_window_interior(w);
	return w->frame.left >= 0 && w->frame.right < WIDTH && w->frame.top >= 0 && w->frame.bottom < HEIGHT
	       && w->frame.left < w->frame.right && w->frame.top < w->frame.bottom && b.left <= b.right
	       && b.top <= b.bottom && b.right - b.left < w->width && b.bottom - b.top < w->height;
}

static void
change_origin(struct st_ddofx_graphic_window *w, int64_t x, int64_t y, bool clamp)
{
	struct bounds b    = st_ddofx_window_interior(w);
	int           xmax = w->width - (b.right - b.left + 1), ymax = w->height - (b.bottom - b.top + 1);
	if (!clamp && (x < 0 || y < 0 || x > xmax || y > ymax))
		return;
	w->x = x < 0 ? 0 : x > xmax ? xmax : (int)x;
	w->y = y < 0 ? 0 : y > ymax ? ymax : (int)y;
}

struct repaint {
	struct st_ddofx *ddo;
	struct bounds    area;
	const struct st_pixel_buffer *text;
};

static bool
contains(const struct bounds *b, int x, int y)
{
	return x >= b->left && x <= b->right && y >= b->top && y <= b->bottom;
}

static struct bounds
thumb(const struct st_ddofx_graphic_window *w, bool horizontal)
{
	struct bounds view = st_ddofx_window_interior(w);
	int first = horizontal ? view.left + 10 : view.top + 12;
	int last = horizontal ? view.right - 11 : view.bottom - 10;
	int track = last - first + 1;
	int visible = horizontal ? view.right - view.left + 1 : view.bottom - view.top + 1;
	int total = horizontal ? w->width : w->height;
	int origin = horizontal ? w->x : w->y;
	if (total <= visible || track <= 0)
		return (struct bounds){0, -1, 0, -1};

	int size = track * visible / total;
	if (size < 1)
		size = 1;
	first += (track - size) * origin / (total - visible);
	if (horizontal)
		return (struct bounds){first, first + size - 1, w->frame.bottom - 11, w->frame.bottom - 2};
	return (struct bounds){w->frame.right - 10, w->frame.right - 1, first, first + size - 1};
}

/* Frame input and painting use the same desktop coordinates. */
static struct bounds
control_bounds(const struct st_ddofx_graphic_window *w, enum st_ddofx_frame_part part)
{
	struct bounds f = w->frame;
	switch (part) {
		case FRAME_DRAG:
			if (!w->no_drag)
				return (struct bounds){f.left, f.right - (w->icon && !w->no_icon ? 53 : 50), f.top, f.top + 16};
			break;
		case FRAME_ICON:
			if (w->icon && !w->no_icon)
				return (struct bounds){f.right - 52, f.right - 41, f.top + 2, f.top + 16};
			break;
		case FRAME_CLOSE:
			if (*w->close.macro || *w->close.reply)
				return (struct bounds){f.right - 16, f.right - 4, f.top + 2, f.top + 16};
			break;
		case FRAME_ZOOM:
			if (!w->no_zoom)
				return (struct bounds){f.right - 28, f.right - 17, f.top + 2, f.top + 16};
			break;
		case FRAME_RESTORE:
			if (!w->no_restore)
				return (struct bounds){f.right - 40, f.right - 29, f.top + 2, f.top + 16};
			break;
		case FRAME_SCROLL_X:
			if (!w->no_horizontal)
				return thumb(w, true);
			break;
		case FRAME_SCROLL_Y:
			if (!w->no_vertical)
				return thumb(w, false);
			break;
		case FRAME_LEFT:
		case FRAME_RIGHT:
			if (!w->no_horizontal && w->width > f.right - f.left + 1 - 24) {
				int left = part == FRAME_LEFT ? f.left + 11 : f.right - 22;
				return (struct bounds){left, left + 10, f.bottom - 11, f.bottom - 2};
			}
			break;
		case FRAME_UP:
		case FRAME_DOWN:
			if (!w->no_vertical && w->height > f.bottom - f.top + 1 - 34) {
				int top = part == FRAME_UP ? f.top + 21 : f.bottom - 23;
				return (struct bounds){f.right - 10, f.right - 1, top, top + 10};
			}
			break;
		case FRAME_RESIZE:
			if (!w->no_resize)
				return (struct bounds){f.right - 10, f.right, f.bottom - 9, f.bottom};
			break;
		default:
			break;
	}
	return (struct bounds){0, -1, 0, -1};
}

static void
paint_controls(const struct st_ddofx_graphic_window *w, const struct bounds *damage, const struct st_pixel_buffer *p)
{
	static const unsigned char symbols[][5] = {
		[FRAME_ICON] = {0,0,0,31,31}, [FRAME_CLOSE] = {17,10,4,10,17}, [FRAME_ZOOM] = {31,31,17,17,31},
		[FRAME_RESTORE] = {28,20,31,5,7}, [FRAME_LEFT] = {4,8,31,8,4},
		[FRAME_RIGHT] = {4,2,31,2,4}, [FRAME_UP] = {4,14,21,4,4},
		[FRAME_DOWN] = {4,4,21,14,4}, [FRAME_RESIZE] = {1,2,5,10,21}
	};
	for (enum st_ddofx_frame_part part = FRAME_CLOSE; part <= FRAME_RESIZE; ++part) {
		struct bounds b = control_bounds(w, part), area = b;
		if (!st_ddofx_intersect_bounds(&area, damage))
			continue;
		const unsigned char *glyph = symbols[part == FRAME_ZOOM && w->zoomed ? FRAME_RESTORE : part];
		int left = (b.left + b.right - 4) / 2, top = (b.top + b.bottom - 4) / 2;
		for (int y = area.top; y <= area.bottom; ++y)
			for (int x = area.left; x <= area.right; ++x) {
				unsigned colour = 7;
				if (x == b.left || y == b.top)
					colour = 15;
				else if (x == b.right || y == b.bottom)
					colour = 8;
				else if ((unsigned)(x - left) < 5 && (unsigned)(y - top) < 5
				    && (glyph[y - top] & (16 >> (x - left))))
					colour = 0;
				p->primary[y * WIDTH + x] = p->alternate[y * WIDTH + x] = colour;
			}
	}
}

static int
paint_windows(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	const struct repaint *r = context;
	struct st_ddofx *ddo = r->ddo;
	struct st_ddofx_graphics *g = ddo->graphics;
	struct bounds b = r->area;
	if (pixels->width != WIDTH || pixels->height != HEIGHT)
		return -1;
	/* Inactive slots paint in numeric order. The active slot paints last. */
	for (unsigned i = 1; i <= GRAPHIC_WINDOWS + 1; ++i) {
		bool active = i > GRAPHIC_WINDOWS;
		if (!active && i == g->active_graphic_window)
			continue;
		const struct st_ddofx_graphic_window *w =
		    &g->graphic_windows[active ? g->active_graphic_window : i];
		struct bounds area = w->frame;
		if (!w->visual || w->hidden || !st_ddofx_intersect_bounds(&area, &b))
			continue;
		struct bounds view         = st_ddofx_window_interior(w);
		unsigned      title_colour = active ? 4 : 1;
		for (int y = area.top; y <= area.bottom; ++y)
			for (int x = area.left; x <= area.right; ++x) {
				unsigned colour = y < view.top ? title_colour : 7;
				if (x >= view.left && x <= view.right && y >= view.top && y <= view.bottom)
					colour =
					    w->visual[(w->y + y - view.top) * w->width + w->x + x - view.left];
				pixels->primary[y * WIDTH + x] = pixels->alternate[y * WIDTH + x] = colour;
			}
		struct bounds  title = {w->frame.left + 2, w->frame.right - (w->icon && !w->no_icon ? 54 : 42), w->frame.top, view.top - 1};
		const uint8_t *font  = st_ddofx_vga_bitmap(ddo);
		if (!w->borderless && font && st_ddofx_intersect_bounds(&title, &area))
			st_ddofx_paint_vga_string(g, 0, pixels, font, (const uint8_t *)w->title,
			    (unsigned)strlen(w->title), w->frame.left + 2, view.top - 3, 14, title_colour, &title);
		if (!w->borderless)
			paint_controls(w, &area, pixels);
	}
	*changed = (struct st_console_rect){b.left, b.top, b.right - b.left + 1, b.bottom - b.top + 1};
	return 0;
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

	if (r->text)
		st_ddofx_composite_desktop_text(r->ddo, r->text, pixels, b);
	struct st_console_rect desktop = {b.left, b.top, b.right - b.left + 1, b.bottom - b.top + 1};
	st_ddofx_repaint_clocks(r->ddo, 0, pixels, &desktop);
	st_ddofx_repaint_marquees(r->ddo, 0, pixels, b);
	st_ddofx_repaint_desktop_bars(g, pixels, b);

	return paint_windows(context, pixels, changed);
}

static int
paint_with_text(void *context, const struct st_pixel_buffer *text, struct st_console_rect *changed)
{
	struct repaint *r = context;
	(void)changed;
	r->text = text;
	return st_console_modify_pixels(r->ddo->graphics->display, paint, r);
}

static void
refresh(struct st_ddofx *ddo, struct bounds area)
{
	struct repaint r = {.ddo = ddo, .area = area};
	st_ddofx_mouse_view_changed(ddo->graphics);
	if (ddo->graphics->desktop_text)
		(void)st_console_modify_pixels(ddo->graphics->desktop_text, paint_with_text, &r);
	else
		(void)st_console_modify_pixels(ddo->graphics->display, paint, &r);
}

/* Split a changed rectangle around covering windows. Repainting a covering
 * window from its VGP here would expose drawings awaiting an explicit refresh. */
static void
paint_uncovered(const struct st_ddofx_graphics *g, unsigned owner, const struct st_pixel_buffer *pixels,
    struct bounds b, unsigned next, st_ddofx_region_painter *painter, void *context)
{
	if (b.left > b.right || b.top > b.bottom)
		return;
	for (unsigned i = next; i <= GRAPHIC_WINDOWS; ++i) {
		const struct st_ddofx_graphic_window *w = &g->graphic_windows[i];
		if (!w->visual || w->hidden || i == owner
		    || (i < owner && i != g->active_graphic_window))
			continue;
		struct bounds f = w->frame;
		if (f.right < b.left || f.left > b.right || f.bottom < b.top || f.top > b.bottom)
			continue;
		if (b.top < f.top) {
			paint_uncovered(g, owner, pixels, (struct bounds) {b.left, b.right, b.top, f.top - 1},
			    i + 1, painter, context);
			b.top = f.top;
		}
		if (b.bottom > f.bottom) {
			paint_uncovered(g, owner, pixels,
			    (struct bounds) {b.left, b.right, f.bottom + 1, b.bottom}, i + 1, painter, context);
			b.bottom = f.bottom;
		}
		if (b.left < f.left)
			paint_uncovered(g, owner, pixels, (struct bounds) {b.left, f.left - 1, b.top, b.bottom},
			    i + 1, painter, context);
		if (b.right > f.right)
			paint_uncovered(g, owner, pixels, (struct bounds) {f.right + 1, b.right, b.top, b.bottom},
			    i + 1, painter, context);
		return;
	}
	painter(context, pixels, b);
}

struct page_region {
	struct st_ddofx *ddo;
	unsigned         owner;
	enum page        desktop_page;
	int              dx, dy;
};

static void
copy_region(void *context, const struct st_pixel_buffer *pixels, struct bounds b)
{
	const struct page_region       *r   = context;
	struct st_ddofx                *ddo = r->ddo;
	const struct st_ddofx_graphics *g   = ddo->graphics;

	const struct st_ddofx_graphic_window *w = &g->graphic_windows[r->owner];
	if (r->owner || r->desktop_page != VISUAL) {
		const uint8_t *source = r->owner ? w->visual : (const uint8_t *)g->page[r->desktop_page];
		int            stride = r->owner ? w->width : WIDTH;
		for (int y = b.top; y <= b.bottom; ++y) {
			const uint8_t *src = source + (y - r->dy) * stride + (b.left - r->dx);
			for (int x = b.left; x <= b.right; ++x)
				pixels->primary[y * WIDTH + x] = pixels->alternate[y * WIDTH + x] = *src++;
		}
	}
	if (!r->owner) {
		struct st_console_rect area = {b.left, b.top, b.right - b.left + 1, b.bottom - b.top + 1};
		st_ddofx_repaint_clocks(ddo, 0, pixels, &area);
		st_ddofx_repaint_marquees(ddo, 0, pixels, b);
		st_ddofx_repaint_desktop_bars(g, pixels, b);
	}
}

void
st_ddofx_publish_page_area(struct st_ddofx *ddo, unsigned owner, enum page desktop_page,
    const struct st_pixel_buffer *pixels, struct bounds area, struct bounds *damage)
{
	const struct st_ddofx_graphics *g = ddo->graphics;
	struct bounds view = {0, WIDTH - 1, 0, HEIGHT - 1};
	int dx = 0, dy = 0;
	if (owner) {
		const struct st_ddofx_graphic_window *w = &g->graphic_windows[owner];
		if (!w->visual)
			return;
		struct bounds backing = {0, w->width - 1, 0, w->height - 1};
		if (!st_ddofx_intersect_bounds(&area, &backing))
			return;
		if (owner == g->active_graphic_window && desktop_page != VISUAL) {
			/* Background frames can replace overlays even outside the viewport. */
			struct st_console_rect local = {area.left, area.top, area.right - area.left + 1,
			    area.bottom - area.top + 1};
			st_ddofx_repaint_clocks(ddo, owner, NULL, &local);
			st_ddofx_repaint_marquees(ddo, owner, NULL, area);
		}
		if (w->hidden)
			return;
		view = st_ddofx_window_interior(w);
		dx = view.left - w->x;
		dy = view.top - w->y;
		area.left += dx;
		area.right += dx;
		area.top += dy;
		area.bottom += dy;
	}
	if (!st_ddofx_intersect_bounds(&area, &view))
		return;
	unsigned first_cover = owner && owner == g->active_graphic_window ? GRAPHIC_WINDOWS + 1 : 1;
	struct page_region region      = {ddo, owner, desktop_page, dx, dy};
	paint_uncovered(g, owner, pixels, area, first_cover, copy_region, &region);
	st_ddofx_extend_bounds(damage, area);
}

struct text_control {
	struct st_ddofx         *ddo;
	unsigned                 owner;
	struct bounds            area;
	st_ddofx_region_painter *painter;
	void                    *context;
};

static int
show_text_control(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	const struct text_control *control = context;
	if (pixels->width != WIDTH || pixels->height != HEIGHT)
		return 0;
	struct bounds damage = control->area;
	if (control->owner) {
		damage = (struct bounds) {WIDTH, -1, HEIGHT, -1};
		st_ddofx_publish_page_area(control->ddo, control->owner, VISUAL, pixels, control->area, &damage);
	}
	else
		paint_uncovered(control->ddo->graphics, 0, pixels, control->area, 1, control->painter,
		    control->context);
	if (damage.right >= damage.left)
		*changed = (struct st_console_rect) {damage.left, damage.top, damage.right - damage.left + 1,
		    damage.bottom - damage.top + 1};
	return 0;
}

void
st_ddofx_draw_text_control(struct st_ddofx *ddo, unsigned owner, bool work, struct bounds area,
    st_ddofx_region_painter *painter, void *context)
{
	struct st_ddofx_graphic_window *w = &ddo->graphics->graphic_windows[owner];
	if ((owner && !w->visual) || area.left > area.right || area.top > area.bottom)
		return;
	if (owner || work)
		painter(context, NULL, area);
	if (owner ? !w->hidden : !work) {
		struct text_control control = {ddo, owner, area, painter, context};
		(void)st_console_modify_pixels(ddo->graphics->display, show_text_control, &control);
	}
}

void
st_ddofx_refresh_window_area(struct st_ddofx *ddo, unsigned slot, struct bounds area)
{
	const struct st_ddofx_graphic_window *w = &ddo->graphics->graphic_windows[slot];
	if (!w->visual || w->hidden)
		return;
	struct bounds view = st_ddofx_window_interior(w);
	int           dx = view.left - w->x, dy = view.top - w->y;
	area.left += dx;
	area.right += dx;
	area.top += dy;
	area.bottom += dy;
	if (st_ddofx_intersect_bounds(&area, &view))
		refresh(ddo, area);
}

void
st_ddofx_refresh_graphic_interior(struct st_ddofx *ddo)
{
	struct st_ddofx_graphics       *g = ddo->graphics;
	struct st_ddofx_graphic_window *w = &g->graphic_windows[g->alert_graphic_window];
	if (w->visual && !w->hidden)
		refresh(ddo, st_ddofx_window_interior(w));
}

static void
dispose(struct st_ddofx_graphics *g, unsigned slot)
{
	struct st_ddofx_graphic_window *w = &g->graphic_windows[slot];
	if (g->frame_drag.slot == slot)
		g->frame_drag = (struct st_ddofx_frame_drag){0};
	if (w->visual) {
		st_ddofx_animation_window_closed(g, slot);
		st_ddofx_grid_window_closed(g, slot);
		st_ddofx_polygon_window_closed(g, slot);
		st_ddofx_bar_window_closed(g, slot);
	}
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

void
st_ddofx_redraw_graphic_windows(struct st_ddofx *ddo)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	struct repaint r = {.ddo = ddo, .area = {WIDTH, -1, HEIGHT, -1}};
	for (unsigned i = 1; i <= GRAPHIC_WINDOWS; ++i)
		if (g->graphic_windows[i].visual && !g->graphic_windows[i].hidden)
			st_ddofx_extend_bounds(&r.area, g->graphic_windows[i].frame);
	if (r.area.left <= r.area.right)
		(void)st_console_modify_pixels(g->display, paint_windows, &r);
}

static void
redraw_all(struct st_ddofx *ddo)
{
	st_ddofx_mouse_view_changed(ddo->graphics);
	st_ddofx_redraw_graphic_windows(ddo);
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
	if (g->frame_drag.slot == slot)
		g->frame_drag = (struct st_ddofx_frame_drag){0};
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
define_close(struct st_ddofx *ddo, struct st_ddofx_graphic_window *w, char *args)
{
	int32_t high, low, pass;
	if (!st_ddofx_field_number(ddo, &args, &high) || (unsigned)high > 255
	    || !st_ddofx_field_number(ddo, &args, &low) || (unsigned)low > 255
	    || !st_ddofx_field_number(ddo, &args, &pass) || (unsigned)pass > 1)
		return;
	const char *reply = st_ddofx_field(&args);
	if (!reply || !args)
		return;

	st_ddofx_callback_define(ddo, &w->close, reply, args);
	w->close_key = (*w->close.macro || *w->close.reply) ? (uint16_t)((high << 8) | low) : 0;
	w->pass_key = pass != 0;
	if (!w->hidden && !w->borderless)
		refresh(ddo, w->frame);
}

bool
st_ddofx_has_window_key(const struct st_ddofx *ddo)
{
	if (ddo->graphics)
		for (unsigned i = 1; i <= GRAPHIC_WINDOWS; ++i)
			if (ddo->graphics->graphic_windows[i].close_key)
				return true;
	return false;
}

bool
st_ddofx_next_window_key(struct st_ddofx *ddo, uint16_t word, unsigned *next, bool *pass)
{
	while (ddo->graphics && word && *next <= GRAPHIC_WINDOWS) {
		struct st_ddofx_graphic_window *w = &ddo->graphics->graphic_windows[(*next)++];
		if (word == w->close_key && st_ddofx_callback_start(ddo, &w->close, DDO_KEY)) {
			/* The macro can close or recreate the slot before key dispatch resumes. */
			*pass = w->pass_key;
			return true;
		}
	}
	return false;
}

static int
limit(int value, int low, int high)
{
	return value < low ? low : value > high ? high : value;
}

static void
move_handle(struct st_ddofx *ddo, int dx, int dy)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	struct st_ddofx_frame_drag *drag = &g->frame_drag;
	struct st_ddofx_graphic_window *w = &g->graphic_windows[drag->slot];
	struct bounds old = w->frame, start = drag->start;

	switch (drag->part) {
		case FRAME_DRAG:
			dx = limit(dx, -start.left, WIDTH - 1 - start.right);
			dy = limit(dy, -start.top, HEIGHT - 1 - start.bottom);
			w->frame = (struct bounds){start.left + dx, start.right + dx, start.top + dy, start.bottom + dy};
			break;
		case FRAME_RESIZE:
			w->frame.right = start.left + limit(start.right - start.left + dx, 24, w->width + 23);
			w->frame.bottom = start.top + limit(start.bottom - start.top + dy, 34, w->height + 33);
			if (w->frame.right >= WIDTH)
				w->frame.right = WIDTH - 1;
			if (w->frame.bottom >= HEIGHT)
				w->frame.bottom = HEIGHT - 1;
			break;
		case FRAME_SCROLL_X:
		case FRAME_SCROLL_Y:
			change_origin(w, drag->origin_x + (drag->part == FRAME_SCROLL_X ? dx : 0),
			    drag->origin_y + (drag->part == FRAME_SCROLL_Y ? dy : 0), true);
			refresh(ddo, st_ddofx_window_interior(w));
			return;
		default:
			return;
	}
	if (w->frame.left == old.left && w->frame.top == old.top && w->frame.right == old.right
	    && w->frame.bottom == old.bottom)
		return;
	w->zoomed = false;
	change_origin(w, w->x, w->y, true);
	refresh(ddo, old);
	refresh(ddo, w->frame);
}

static void
scroll_arrow(struct st_ddofx *ddo, unsigned delay)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	struct st_ddofx_frame_drag *drag = &g->frame_drag;
	struct st_ddofx_graphic_window *w = &g->graphic_windows[drag->slot];
	int dx = (drag->part == FRAME_RIGHT) - (drag->part == FRAME_LEFT);
	int dy = (drag->part == FRAME_DOWN) - (drag->part == FRAME_UP);
	int x = w->x, y = w->y;
	change_origin(w, x + dx * 20, y + dy * 20, true);
	drag->repeat_due = 0;
	if (x != w->x || y != w->y) {
		refresh(ddo, st_ddofx_window_interior(w));
		uint64_t now;
		if (st_time_monotonic_ms(&now) == 0)
			drag->repeat_due = now + delay;
	}
}

void
st_ddofx_repeat_window_arrow(struct st_ddofx *ddo)
{
	scroll_arrow(ddo, 90);
}

static unsigned
window_at(const struct st_ddofx_graphics *g, int x, int y)
{
	for (unsigned i = GRAPHIC_WINDOWS + 1; i > 0; --i) {
		unsigned slot = i > GRAPHIC_WINDOWS ? g->active_graphic_window : i;
		const struct st_ddofx_graphic_window *w = &g->graphic_windows[slot];
		if (w->visual && !w->hidden && contains(&w->frame, x, y))
			return slot;
	}
	return 0;
}

/* Return the input owner and translate the desktop sample into its page.
 * Negative results are frame pixels or an inactive window. */
int
st_ddofx_window_point(const struct st_ddofx_graphics *g, int *x, int *y)
{
	unsigned slot = window_at(g, *x, *y);
	if (!slot)
		return 0;
	const struct st_ddofx_graphic_window *w = &g->graphic_windows[slot];
	struct bounds view = st_ddofx_window_interior(w);
	if (slot != g->active_graphic_window || !contains(&view, *x, *y))
		return -1;
	*x += w->x - view.left;
	*y += w->y - view.top;
	return (int)slot;
}

bool
st_ddofx_graphic_window_mouse(struct st_ddofx *ddo, unsigned changed)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	struct st_ddofx_mouse_state *m = &g->mouse;
	struct st_ddofx_frame_drag *drag = &g->frame_drag;
	if (drag->slot) {
		if (!m->outside && (m->buttons == 1 || !m->buttons))
			move_handle(ddo, m->x - drag->mouse_x, m->y - drag->mouse_y);
		if (m->buttons != 1)
			*drag = (struct st_ddofx_frame_drag){0};
		return true;
	}
	if (m->outside || m->scan.waiting || m->buttons != 1 || !(changed & 1))
		return false;
	unsigned slot = window_at(g, m->x, m->y);
	if (!slot)
		return false;

	struct st_ddofx_graphic_window *w = &g->graphic_windows[slot];
	if (slot != g->active_graphic_window) {
		g->active_graphic_window = slot;
		redraw_all(ddo);
	}
	struct bounds view = st_ddofx_window_interior(w);
	if (contains(&view, m->x, m->y))
		return false;
	for (enum st_ddofx_frame_part part = FRAME_DRAG; part <= FRAME_RESIZE; ++part) {
		struct bounds bounds = control_bounds(w, part);
		if (!contains(&bounds, m->x, m->y))
			continue;
		*drag = (struct st_ddofx_frame_drag){.slot = slot, .part = part, .start = w->frame,
		    .mouse_x = m->x, .mouse_y = m->y, .origin_x = w->x, .origin_y = w->y};
		if (part == FRAME_CLOSE)
			(void)st_ddofx_callback_start(ddo, &w->close, DDO_EVENT);
		else if (part == FRAME_ICON)
			st_ddofx_close_icon(ddo, w->icon);
		else if (part == FRAME_ZOOM || part == FRAME_RESTORE)
			restore(ddo, w, part == FRAME_ZOOM);
		else if (part >= FRAME_LEFT && part <= FRAME_DOWN)
			scroll_arrow(ddo, 500);
		break;
	}
	return true;
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
			DEFINE_CLOSE,
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
		    [DEFINE_CLOSE] = "2DCGWIN",
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
		else if (command == DEFINE_CLOSE)
			define_close(ddo, w, args);
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
					refresh(ddo, st_ddofx_window_interior(w));
			}
		}
	}
	return true;
}
