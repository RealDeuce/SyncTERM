/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_script.h"
#include "string_util.h"

/* Local selection starts and ends on whole displayed cells. */
static bool
visible_cells(int first, int last, int position, int size, uint32_t *origin, uint32_t *count, uint64_t *offset)
{
	unsigned skip = position < first ? (unsigned)(first - position + size - 1) / size : 0;
	position += (int)skip * size;
	if (skip >= *count || position + size - 1 > last)
		return false;
	unsigned fit = (unsigned)(last - position + 1) / size;
	*origin += skip;
	*count -= skip;
	if (*count > fit)
		*count = fit;
	*offset = (unsigned)position;
	return true;
}

st_console_t *
st_ddofx_graphics_view(struct st_ddofx_graphics *g, uint64_t *x, uint64_t *y, uint32_t *width, uint32_t *height,
    struct st_console_rect *source)
{
	const struct st_ddofx_text_page *page = &g->windows[g->active_window];
	if (!g->active_window || page->hidden)
		return NULL;
	*x      = page->x;
	*y      = page->y;
	*width  = page->cell_width;
	*height = page->cell_height;
	*source = (struct st_console_rect) {page->origin_x, page->origin_y, page->columns, page->rows};
	if (page->track_x || page->track_y)
		(void)st_console_raster_view_origin(page->view, &source->x, &source->y);
	if (page->owner) {
		const struct st_ddofx_graphic_window *w = &g->graphic_windows[page->owner];
		if (!w->visual || w->hidden)
			return NULL;
		struct bounds port = st_ddofx_window_interior(w);
		if (!visible_cells(port.left, port.right, port.left + page->x - w->x, page->cell_width,
		        &source->x, &source->width, x)
		    || !visible_cells(port.top, port.bottom, port.top + page->y - w->y, page->cell_height,
		        &source->y, &source->height, y))
			return NULL;
		/* Local selection has one rectangular aperture. When another window
		 * covers it, focusing this graphic window exposes the text for selection. */
		if (g->active_graphic_window != page->owner) {
			for (unsigned id = 1; id <= GRAPHIC_WINDOWS; ++id) {
				const struct st_ddofx_graphic_window *cover = &g->graphic_windows[id];
				struct bounds cells = {(int)*x, (int)(*x + source->width * *width) - 1,
				    (int)*y, (int)(*y + source->height * *height) - 1};
				if ((id > page->owner || id == g->active_graphic_window) && cover->visual
				    && !cover->hidden && st_ddofx_intersect_bounds(&cells, &cover->frame))
					return NULL;
			}
		}
	}
	return page->view;
}

st_cterm_t *
st_ddofx_graphics_text(struct st_ddofx_graphics *g)
{
	return g->windows[g->active_window].text;
}

void
st_ddofx_destroy_text_page(struct st_ddofx_text_page *page)
{
	if (page->backing && page->view)
		(void)st_console_set_view_output(page->view, NULL, NULL);
	st_cterm_destroy(&page->text);
	st_font_catalog_destroy(page->fonts);
	if (page->view)
		st_console_destroy(page->view);
	if (page->backing)
		st_console_destroy(page->backing);
	*page = (struct st_ddofx_text_page) {0};
}

bool
st_ddofx_create_text_page(struct st_ddofx_graphics *g, struct st_ddofx_text_page *page,
    const struct st_cterm_config *base)
{
	struct st_cterm_config config = *base;
	page->cell_width              = g->cell_width;
	page->cell_height             = g->cell_height;
	st_console_t *parent          = g->display;
	if (page->owner) {
		/* CHANGE and SCALE can expose any part of the 80 x 100 text page.
		 * Size the native text raster independently of the owner's virtual graphics page. */
		const struct st_console_display_config headless = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
		const struct st_console_config         geometry = {.columns = 1,
		    .rows                                                   = 1,
		    .cell_width                                             = 8,
		    .cell_height                                            = 16,
		    .pixel_width                                            = 80 * 8,
		    .pixel_height                                           = 100 * 16};
		if (st_console_create(&page->backing, &headless, &geometry))
			goto failed;
		parent = page->backing;
	}
	if (st_console_create_clipped_raster_view(&page->view, parent, 80, 100, 8, 16, page->owner ? 0 : page->x,
		page->owner ? 0 : page->y, page->hidden ? 0 : page->columns, page->hidden ? 0 : page->rows)
	    || st_font_catalog_create(&page->fonts, page->view))
		goto failed;
	static const uint16_t pc_indices[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	config.console                       = page->view;
	config.fonts                         = page->fonts;
	config.font_selector                 = 0;
	config.terminal_rows                 = 100;
	config.status_line                   = false;
	config.scrollback_lines              = 0;
	config.size_changed                  = NULL;
	config.emulation                     = ST_CTERM_ANSI_BBS;
	config.palette_mapping               = pc_indices;
	/* Text and DDO share one hardware palette, including indices 0..15. */
	config.disabled_capabilities |= ST_CONSOLE_CAP_EXTENDED_PALETTE;
	if (st_cterm_create(&page->text, &config) || st_cterm_start_text(page->text)
	    || st_console_raster_view_adopt_indexed(page->view)
	    || (g->text_columns != 80 && st_cterm_resize(page->text, g->text_columns, 100)))
		goto failed;
	return true;
failed:
	st_ddofx_destroy_text_page(page);
	return false;
}

void
st_ddofx_close_text_windows(struct st_ddofx_graphics *g)
{
	for (unsigned i = 1; i <= 10; ++i)
		st_ddofx_destroy_text_page(&g->windows[i]);
	g->active_window = 0;
}

struct window_text_output {
	struct st_ddofx_graphics *g;
	unsigned                  owner;
	struct bounds             area;
	const uint32_t           *source, *palette;
	uint32_t                  stride;
};

static void
copy_text_pixels(void *context, const struct st_pixel_buffer *unused, struct bounds area)
{
	const struct window_text_output *output = context;
	struct st_ddofx_graphic_window  *w      = &output->g->graphic_windows[output->owner];
	for (int y = area.top; y <= area.bottom; ++y) {
		const uint32_t *src = output->source + (size_t)(y - output->area.top) * output->stride
		                      + (area.left - output->area.left);
		uint8_t        *dst = w->visual + (size_t)y * w->width + area.left;
		for (int x = area.left; x <= area.right; ++x) {
			uint32_t ink = *src++;
			if (ink > 255 && !ST_CONSOLE_COLOR_IS_RGB(ink))
				ink = ST_CONSOLE_COLOR_RGB_TAG | output->palette[(uint16_t)ink];
			*dst++ = st_console_palette_index256(output->g->palette, ink);
		}
	}
	(void)unused;
}

static void
output_text(void *context, st_console_t *view, struct st_console_rect local, const uint32_t *source,
    uint32_t stride, const uint32_t *palette)
{
	struct st_ddofx          *ddo = context;
	struct st_ddofx_graphics *g   = ddo->graphics;
	/* SWAP moves complete page records. The view keeps its identity. */
	for (unsigned id = 1; id <= 10; ++id) {
		const struct st_ddofx_text_page *p = &g->windows[id];
		if (p->view != view)
			continue;
		const struct st_ddofx_graphic_window *w = &g->graphic_windows[p->owner];
		int                                   x = p->x + (int)local.x, y = p->y + (int)local.y;
		struct bounds             area   = {x, x + (int)local.width - 1, y, y + (int)local.height - 1};
		struct window_text_output output = {g, p->owner, area, source, palette, stride};
		struct bounds             limits = {0, w->width - 1, 0, w->height - 1};
		if (!p->hidden && w->visual && st_ddofx_intersect_bounds(&area, &limits))
			st_ddofx_draw_text_control(ddo, p->owner, false, area, copy_text_pixels, &output);
		break;
	}
}

struct page_repair {
	struct st_ddofx_graphics *g;
	unsigned                  owner;
};

static void
restore_page(void *context, const struct st_pixel_buffer *pixels, struct bounds area)
{
	const struct page_repair       *repair = context;
	struct st_ddofx_graphic_window *w      = &repair->g->graphic_windows[repair->owner];
	for (int y = area.top; y <= area.bottom; ++y) {
		if (repair->owner) {
			size_t start = (size_t)y * w->width + area.left;
			memcpy(w->visual + start, w->hidden_page + start, area.right - area.left + 1);
		}
		else {
			for (int x = area.left; x <= area.right; ++x) {
				size_t i           = (size_t)y * WIDTH + x;
				pixels->primary[i] = pixels->alternate[i] = repair->g->page[HIDDEN][y][x];
			}
		}
	}
}

static struct bounds
outer_area(const struct st_ddofx_text_page *page)
{
	int inset = page->border ? 1 : 0;
	int left  = page->x - inset * page->cell_width;
	int top   = page->y - inset * page->cell_height;
	return (struct bounds) {left, left + ((int)page->columns + 2 * inset) * page->cell_width - 1, top,
	    top + ((int)page->rows + 2 * inset) * page->cell_height - 1};
}

static void
paint_outer_area(struct st_ddofx *ddo, const struct st_ddofx_text_page *page, st_ddofx_region_painter *paint,
    void *context)
{
	const struct st_ddofx_graphic_window *w      = &ddo->graphics->graphic_windows[page->owner];
	struct bounds                         limits = {0, (page->owner ? w->width : WIDTH) - 1, 0,
	    (page->owner ? w->height : HEIGHT) - 1};
	struct bounds                         area   = outer_area(page);
	if (st_ddofx_intersect_bounds(&area, &limits))
		st_ddofx_draw_text_control(ddo, page->owner, false, area, paint, context);
}

static void
uncover_page(struct st_ddofx *ddo, const struct st_ddofx_text_page *page)
{
	if (!page->view || page->hidden)
		return;
	struct page_repair repair = {ddo->graphics, page->owner};
	paint_outer_area(ddo, page, restore_page, &repair);
}

struct window_frame {
	struct st_ddofx_graphics        *graphics;
	const struct st_ddofx_text_page *page;
	const uint8_t                   *font;
	bool                             active, labels_only;
};

static void
paint_frame(void *context, const struct st_pixel_buffer *pixels, struct bounds visible)
{
	const struct window_frame       *frame = context;
	const struct st_ddofx_text_page *p     = frame->page;
	struct bounds                    area  = outer_area(p);
	/* The SDK describes a double-line ANSI frame. Position its title and
	 * help in character space, so cropping either label retains whole glyphs. */
	for (unsigned row = 0; row < p->rows + 2; ++row) {
		bool        top = row == 0, bottom = row == p->rows + 1;
		const char *label = top ? p->title : bottom ? p->help : "";
		unsigned    len   = (unsigned)strlen(label);
		if (len > p->columns)
			len = p->columns;
		unsigned start = 1 + (p->columns - len) / 2;
		for (unsigned col = 0; col < p->columns + 2; ++col) {
			bool left = col == 0, right = col == p->columns + 1;
			if (!top && !bottom && !left && !right)
				continue;
			uint8_t ch = 186, ink = p->frame_colour, paper = 0;
			bool    label_cell = col >= start && col - start < len;
			if (frame->labels_only && !label_cell)
				continue;
			if (label_cell) {
				ch    = (uint8_t)label[col - start];
				ink   = p->title_colour;
				paper = frame->active ? 4 : 1;
			}
			else if (top)
				ch = left ? 201 : right ? 187 : 205;
			else if (bottom)
				ch = left ? 200 : right ? 188 : 205;
			int           x    = area.left + (int)col * p->cell_width;
			int           y    = area.top + (int)row * p->cell_height;
			struct bounds clip = {x, x + p->cell_width - 1, y, y + p->cell_height - 1};
			if (!st_ddofx_intersect_bounds(&clip, &visible))
				continue;
			st_ddofx_paint_vga_string(frame->graphics, p->owner, pixels, frame->font, &ch, 1, x,
			    y + p->cell_height - 1, ink, paper, &clip);
		}
	}
}

static void
draw_frame(struct st_ddofx *ddo, unsigned id, bool labels_only)
{
	struct st_ddofx_graphics        *g = ddo->graphics;
	const struct st_ddofx_text_page *p = &g->windows[id];
	if (!p->view || p->hidden || !p->border || !p->font_width)
		return;
	struct window_frame frame = {g, p, p->font_bitmap, id == g->active_window, labels_only};
	paint_outer_area(ddo, p, paint_frame, &frame);
}

static void
repaint_window(struct st_ddofx *ddo, unsigned id)
{
	struct st_ddofx_text_page *page = &ddo->graphics->windows[id];
	if (page->view && !page->hidden) {
		draw_frame(ddo, id, false);
		(void)st_console_redraw_raster_view(page->view);
	}
}

static void
draw_window(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, unsigned id, const char *font)
{
	struct st_ddofx_text_page *page = &ddo->graphics->windows[id];
	if (!page->view || page->hidden)
		return;
	st_ddofx_select_window_font(ddo, sink, page, font ? font : page->font_name);
	repaint_window(ddo, id);
}

static void
redraw_text_pages(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *active_font)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	(void)st_console_begin_update(g->display);
	for (unsigned i = 1; i <= 10; ++i)
		if (i != g->active_window)
			draw_window(ddo, sink, i, NULL);
	if (g->active_window)
		draw_window(ddo, sink, g->active_window, active_font);
	(void)st_console_end_update(g->display);
}

static bool
text_windows_overlap(const struct st_ddofx_graphics *g, unsigned owner)
{
	for (unsigned i = 1; i <= 10; ++i) {
		const struct st_ddofx_text_page *p = &g->windows[i];
		if (!p->view || p->hidden || p->owner != owner)
			continue;
		for (unsigned j = 1; j < i; ++j) {
			const struct st_ddofx_text_page *other = &g->windows[j];
			if (!other->view || other->hidden || other->owner != owner)
				continue;
			struct bounds a = outer_area(p), b = outer_area(other);
			if (st_ddofx_intersect_bounds(&a, &b))
				return true;
		}
	}
	return false;
}

void
st_ddofx_repaint_text_windows(struct st_ddofx *ddo, unsigned owner)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	unsigned exposed[10], count = 0;
	/* Walk front to back, then paint the exposed windows back to front. */
	for (unsigned rank = 11; rank > 0; --rank) {
		unsigned id = rank == 11 ? g->active_window : rank;
		if (!id || (rank != 11 && id == g->active_window))
			continue;
		const struct st_ddofx_text_page *p = &g->windows[id];
		if (!p->view || p->hidden || p->owner != owner)
			continue;
		struct bounds area = outer_area(p);
		unsigned i;
		for (i = 0; i < count; ++i) {
			struct bounds cover = outer_area(&g->windows[exposed[i]]);
			if (cover.left <= area.left && cover.right >= area.right && cover.top <= area.top
			    && cover.bottom >= area.bottom)
				break;
		}
		if (i == count)
			exposed[count++] = id;
	}
	while (count)
		repaint_window(ddo, exposed[--count]);
}

/* Other owners' visual pages may contain drawings which have never
 * been copied to their hidden pages. */
static void
rebuild_text_owner(struct st_ddofx *ddo, unsigned owner)
{
	struct st_ddofx_graphics *g       = ddo->graphics;
	struct st_ddofx_surface   surface = st_ddofx_surface(g, owner, HIDDEN);
	struct bounds             area    = {0, surface.width - 1, 0, surface.height - 1};
	struct page_repair        repair  = {g, owner};
	st_ddofx_draw_text_control(ddo, owner, false, area, restore_page, &repair);
	st_ddofx_repaint_text_windows(ddo, owner);
}

static void
show_active_window(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, unsigned old, bool opened)
{
	struct st_ddofx_graphics  *g              = ddo->graphics;
	struct st_ddofx_text_page *next           = &g->windows[g->active_window];
	unsigned                   previous_owner = g->windows[old].owner;
	st_ddofx_select_window_font(ddo, sink, next, next->font_name);
	if (old && previous_owner != next->owner && text_windows_overlap(g, previous_owner))
		rebuild_text_owner(ddo, previous_owner);
	else if (old && old != g->active_window)
		draw_frame(ddo, old, true);
	if (text_windows_overlap(g, next->owner))
		rebuild_text_owner(ddo, next->owner);
	else if (opened)
		repaint_window(ddo, g->active_window);
	else
		draw_frame(ddo, g->active_window, true);
}

static int
place_page(const struct st_ddofx_text_page *page, bool hidden)
{
	struct st_console_viewport port = {
	    .cells    = {page->origin_x, page->origin_y, page->columns, page->rows},
	    .hidden   = hidden,
	    .follow_x = page->track_x,
	    .follow_y = page->track_y
        };
	return st_console_place_raster_view(page->view, page->owner ? 0 : page->x, page->owner ? 0 : page->y,
	    &port);
}

/* Origins here are zero-based. Each axis is validated independently. */
static void
move_origin(struct st_ddofx_graphics *g, int64_t x, int64_t y)
{
	struct st_ddofx_text_page *p = &g->windows[g->active_window];
	uint32_t                   width, rows;
	if (st_cterm_size(p->text, &width, &rows))
		return;
	if (x < 0 || x + p->columns > width)
		x = p->origin_x;
	if (y < 0 || y + p->rows > rows)
		y = p->origin_y;
	if ((unsigned)x == p->origin_x && (unsigned)y == p->origin_y)
		return;
	p->origin_x = (unsigned)x;
	p->origin_y = (unsigned)y;
	if (place_page(p, p->hidden) == 0 && !p->hidden)
		(void)st_console_redraw_raster_view(p->view);
}

static void
sync_origin(struct st_ddofx_text_page *p)
{
	if (p->view && (p->track_x || p->track_y))
		(void)st_console_raster_view_origin(p->view, &p->origin_x, &p->origin_y);
}

static void
goto_cursor(struct st_ddofx_graphics *g)
{
	struct st_ddofx_text_page *p = &g->windows[g->active_window];
	struct st_cterm_state      state;
	if (st_cterm_state(p->text, &state) || state.x < 1 || state.y < 1)
		return;
	unsigned x = p->origin_x, cursor = (unsigned)state.x - 1;
	if (cursor < x)
		x = cursor;
	else if (cursor - x >= p->columns)
		x = cursor - p->columns + 1;
	unsigned y = (unsigned)state.y > p->rows ? (unsigned)state.y - p->rows : 0;
	move_origin(g, x, y);
}

bool
st_ddofx_window_scroll_key(struct st_ddofx *ddo, uint16_t word)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	if (!g || !g->active_window || ddo->keys_locked)
		return false;
	struct st_ddofx_text_page *p = &g->windows[g->active_window];
	if (!p->manual_scroll)
		return false;
	sync_origin(p);
	int x = (int)p->origin_x, y = (int)p->origin_y;
	switch (word) {
		case 119 << 8: /* Ctrl-Home */
			--x;
			break;
		case 117 << 8: /* Ctrl-End */
			++x;
			break;
		case 132 << 8: /* Ctrl-PgUp */
			--y;
			break;
		case 118 << 8: /* Ctrl-PgDn */
			++y;
			break;
		default:
			return false;
	}
	move_origin(g, x, y);
	return true;
}

void
st_ddofx_text_scale(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *args)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	int32_t                   n[2];
	if (!st_ddofx_numbers(ddo, args, n, 2) || n[0] < 2 || n[0] > 8 || n[1] < 2 || n[1] > 16
	    || (n[0] == g->cell_width && n[1] == g->cell_height))
		return;
	char active_font[ST_DDOFX_BODY_MAX + 1];
	strcpy(active_font, st_ddofx_vga_font_name(ddo));
	(void)st_console_begin_update(g->display);
	for (unsigned i = 0; i <= 10; ++i) {
		struct st_ddofx_text_page *page = &g->windows[i];
		if (!page->view)
			continue;
		if (i)
			uncover_page(ddo, page);
		sync_origin(page);
		page->x           = page->x / page->cell_width * n[0];
		page->y           = page->y / page->cell_height * n[1];
		page->cell_width  = n[0];
		page->cell_height = n[1];
		(void)place_page(page, page->hidden);
	}
	g->cell_width  = n[0];
	g->cell_height = n[1];
	redraw_text_pages(ddo, sink, active_font);
	struct st_ddofx_text_page *active = &g->windows[g->active_window];
	if (!g->active_window || active->hidden)
		st_ddofx_select_window_font(ddo, sink, active, active_font);
	(void)st_console_end_update(g->display);
}

static bool
configure_window(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *args, unsigned id, bool create)
{
	struct st_ddofx_graphics  *g    = ddo->graphics;
	struct st_ddofx_text_page *old  = &g->windows[id];
	struct st_ddofx_text_page  page = create ? (struct st_ddofx_text_page) {0} : *old;
	int32_t                    n[6];
	if (create)
		page.owner = g->alert_graphic_window;
	struct st_ddofx_surface target = st_ddofx_surface(g, page.owner, HIDDEN);
	for (unsigned i = 0; i < 6; ++i)
		if (!st_ddofx_field_number(ddo, &args, &n[i]))
			return false;
	if (n[0] < 1 || n[1] < n[0] || (n[0] == n[1] && g->text_columns != 1)
	    || n[1] > target.width / g->cell_width || (unsigned)(n[1] - n[0]) >= g->text_columns || n[2] < 1
	    || n[3] <= n[2] || n[3] > target.height / g->cell_height || n[3] - n[2] >= 100 || (unsigned)n[4] > 15
	    || (unsigned)n[5] > 15)
		return false;
	const char *border = st_ddofx_field(&args), *hidden = st_ddofx_field(&args);
	const char *track_x = st_ddofx_field(&args), *track_y = st_ddofx_field(&args),
		   *manual = st_ddofx_field(&args);
	if (!track_x || !track_y || !manual)
		return false;
	page.track_x       = ascii_to_upper((unsigned char)*track_x) == 'Y';
	page.track_y       = ascii_to_upper((unsigned char)*track_y) == 'Y';
	page.manual_scroll = ascii_to_upper((unsigned char)*manual) == 'Y';
	const char *title = st_ddofx_field(&args), *help = st_ddofx_field(&args);
	if (!args || !title || !help || !border || !hidden)
		return false;
	page.border = *border == 'Y' || *border == 'y';
	page.hidden = *hidden == 'Y' || *hidden == 'y';
	int inset   = page.border ? 1 : 0;
	int columns = n[1] - n[0] + 1 - 2 * inset;
	int rows    = n[3] - n[2] + 1 - 2 * inset;
	if (columns <= 0 || rows <= 0)
		return false;
	page.columns      = (unsigned)columns;
	page.rows         = (unsigned)rows;
	if (page.origin_x > g->text_columns - page.columns)
		page.origin_x = g->text_columns - page.columns;
	if (page.origin_y > 100 - page.rows)
		page.origin_y = 100 - page.rows;
	page.cell_width   = g->cell_width;
	page.cell_height  = g->cell_height;
	page.x            = (n[0] - 1 + inset) * g->cell_width;
	page.y            = (n[2] - 1 + inset) * g->cell_height;
	page.frame_colour = (uint8_t)n[4];
	page.title_colour = (uint8_t)n[5];
	snprintf(page.title, sizeof(page.title), "%s", title);
	snprintf(page.help, sizeof(page.help), "%s", help);
	snprintf(page.font_name, sizeof(page.font_name), "%s", *args ? args : st_ddofx_vga_font_name(ddo));
	if (create) {
		if (!st_ddofx_create_text_page(g, &page, sink->text_config))
			return false;
	}
	if (place_page(&page, page.hidden)
	    || (create && page.owner && st_console_set_view_output(page.view, output_text, ddo))) {
		if (create)
			st_ddofx_destroy_text_page(&page);
		return false;
	}
	if (create)
		st_ddofx_destroy_text_page(old);
	*old             = page;
	g->active_window = id;
	return true;
}

static void
set_columns(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, unsigned width)
{
	struct st_ddofx_graphics *g       = ddo->graphics;
	bool                      changed = false;
	(void)st_console_begin_update(g->display);
	for (unsigned id = 1; id <= 10; ++id) {
		struct st_ddofx_text_page *page = &g->windows[id];
		uint32_t                   current, rows;
		if (!page->view || st_cterm_size(page->text, &current, &rows) || current == width)
			continue;
		sync_origin(page);
		if (st_cterm_resize(page->text, width, rows))
			continue;
		uncover_page(ddo, page);
		changed = true;
		/* Preserve the outer position when the new limit cannot hold a frame. */
		if (page->border && width < 3) {
			page->border = false;
			page->x -= page->cell_width;
			page->y -= page->cell_height;
			page->rows += 2;
			page->columns += 2;
		}
		unsigned maximum = width - (page->border ? 2 : 0);
		if (page->columns > maximum)
			page->columns = maximum;
		if (page->origin_x > width - page->columns)
			page->origin_x = width - page->columns;
		(void)place_page(page, page->hidden);
	}
	g->text_columns = width;
	if (changed)
		redraw_text_pages(ddo, sink, NULL);
	(void)st_console_end_update(g->display);
}

static bool
valid_slot(int32_t id)
{
	return id >= 1 && id <= 10;
}

void
st_ddofx_text_window_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *body)
{
	struct st_ddofx_graphics  *g    = ddo->graphics;
	char                      *args = body;
	const char                *op   = st_ddofx_field(&args);
	unsigned                   id   = g->active_window;
	struct st_ddofx_text_page *page = &g->windows[id];
	int32_t                    n[2];
	sync_origin(page);
	if (!ascii_case_compare(op, "1SETC") && st_ddofx_numbers(ddo, args, n, 1)) {
		if (n[0] >= 1 && n[0] <= 80)
			set_columns(ddo, sink, (unsigned)n[0]);
		return;
	}
	if (!ascii_case_compare(op, "1GETPOS") && !args) {
		if (!id)
			st_ddofx_query_reply(ddo, sink, "NONE", false);
		else {
			struct st_cterm_state state;
			if (st_cterm_state(page->text, &state) == 0) {
				char reply[64];
				snprintf(reply, sizeof(reply), "%d,%d,%u,%u", state.x, state.y, page->origin_x + 1,
				    page->origin_y + 1);
				st_ddofx_query_reply(ddo, sink, reply, false);
			}
		}
		return;
	}
	if (!ascii_case_compare(op, "1OPEN")) {
		if (!st_ddofx_field_number(ddo, &args, &n[0]) || !valid_slot(n[0]))
			return;
		unsigned slot = (unsigned)n[0], former_owner = g->windows[slot].owner;
		bool     replaced = g->windows[slot].view != NULL;
		(void)st_console_begin_update(g->display);
		if (configure_window(ddo, sink, args, slot, true)) {
			if (replaced)
				rebuild_text_owner(ddo, former_owner);
			show_active_window(ddo, sink, id, true);
		}
		(void)st_console_end_update(g->display);
		return;
	}
	else if (!id)
		return;
	else if (!ascii_case_compare(op, "1GOTOC") && !args) {
		goto_cursor(g);
		return;
	}
	else if ((!ascii_case_compare(op, "1POS") || !ascii_case_compare(op, "1POSR"))
	         && st_ddofx_numbers(ddo, args, n, 2)) {
		bool    rel = !ascii_case_compare(op, "1POSR");
		int64_t x   = rel ? (int64_t)page->origin_x + n[0] : (int64_t)n[0] - 1;
		int64_t y   = rel ? (int64_t)page->origin_y + n[1] : (int64_t)n[1] - 1;
		move_origin(g, x, y);
		return;
	}
	else if (!ascii_case_compare(op, "1CHANGE")) {
		(void)st_console_begin_update(g->display);
		if (configure_window(ddo, sink, args, id, false)) {
			st_ddofx_select_window_font(ddo, sink, page, page->font_name);
			rebuild_text_owner(ddo, page->owner);
		}
		(void)st_console_end_update(g->display);
		return;
	}
	else if (!ascii_case_compare(op, "1CLOSE") && !args) {
		unsigned owner = page->owner;
		(void)st_console_begin_update(g->display);
		st_ddofx_destroy_text_page(page);
		g->active_window = 0;
		for (unsigned i = 1; i < 10; ++i) {
			id = id == 1 ? 10 : id - 1;
			if (g->windows[id].view) {
				g->active_window = id;
				break;
			}
		}
		struct st_ddofx_text_page *next = &g->windows[g->active_window];
		if (g->active_window)
			st_ddofx_select_window_font(ddo, sink, next, next->font_name);
		rebuild_text_owner(ddo, owner);
		if (g->active_window && next->owner != owner) {
			if (text_windows_overlap(g, next->owner))
				rebuild_text_owner(ddo, next->owner);
			else
				draw_frame(ddo, g->active_window, true);
		}
		(void)st_console_end_update(g->display);
		return;
	}
	else if ((!ascii_case_compare(op, "1HIDE") || !ascii_case_compare(op, "1UNHIDE")) && !args) {
		bool hidden = !ascii_case_compare(op, "1HIDE");
		if (hidden != page->hidden) {
			if (place_page(page, hidden))
				return;
			page->hidden = hidden;
			(void)st_console_begin_update(g->display);
			rebuild_text_owner(ddo, page->owner);
			(void)st_console_end_update(g->display);
		}
		return;
	}
	else if (!ascii_case_compare(op, "1WIN") && st_ddofx_numbers(ddo, args, n, 1) && valid_slot(n[0])
	         && g->windows[n[0]].view && id != (unsigned)n[0]) {
		g->active_window = (unsigned)n[0];
		(void)st_console_begin_update(g->display);
		show_active_window(ddo, sink, id, false);
		(void)st_console_end_update(g->display);
		return;
	}
	else if (!ascii_case_compare(op, "1SWAP") && st_ddofx_numbers(ddo, args, n, 2) && valid_slot(n[0])
	         && valid_slot(n[1]) && g->windows[n[0]].view && g->windows[n[1]].view && n[0] != n[1]) {
		struct st_ddofx_text_page saved = g->windows[n[0]];
		g->windows[n[0]]                = g->windows[n[1]];
		g->windows[n[1]]                = saved;
		if (id == (unsigned)n[0])
			g->active_window = (unsigned)n[1];
		else if (id == (unsigned)n[1])
			g->active_window = (unsigned)n[0];
		unsigned first = g->windows[n[0]].owner, second = g->windows[n[1]].owner;
		(void)st_console_begin_update(g->display);
		if (text_windows_overlap(g, first))
			st_ddofx_repaint_text_windows(ddo, first);
		if (second != first && text_windows_overlap(g, second))
			st_ddofx_repaint_text_windows(ddo, second);
		(void)st_console_end_update(g->display);
	}
}
