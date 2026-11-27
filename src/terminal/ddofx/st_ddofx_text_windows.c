/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_script.h"
#include "string_util.h"

st_console_t *
st_ddofx_graphics_view(struct st_ddofx_graphics *g, uint64_t *x, uint64_t *y, uint32_t *width, uint32_t *height)
{
	const struct st_ddofx_text_page *page = &g->windows[g->active_window];
	if (!g->active_window || page->hidden)
		return NULL;
	*x      = page->x;
	*y      = page->y;
	*width  = page->cell_width;
	*height = page->cell_height;
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
	st_cterm_destroy(&page->text);
	st_font_catalog_destroy(page->fonts);
	if (page->view)
		st_console_destroy(page->view);
	*page = (struct st_ddofx_text_page) {0};
}

bool
st_ddofx_create_text_page(struct st_ddofx_graphics *g, struct st_ddofx_text_page *page,
    const struct st_cterm_config *base)
{
	struct st_cterm_config config = *base;
	page->cell_width              = g->cell_width;
	page->cell_height             = g->cell_height;
	if (st_console_create_clipped_raster_view(&page->view, g->display, 80, 100, 8, 16, page->x, page->y,
		page->hidden ? 0 : page->columns, page->hidden ? 0 : page->rows)
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
	    || st_console_raster_view_adopt_indexed(page->view))
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

static void
draw_text_page(const struct st_ddofx_text_page *page)
{
	struct st_console_cell cells[80];
	struct st_console_rect row = {0, 0, page->columns, 1};
	(void)st_console_begin_update(page->view);
	for (; row.y < page->rows; ++row.y) {
		if (!st_console_read_cells(page->view, &row, cells, row.width))
			(void)st_console_write_complete_cells(page->view, &row, cells, row.width);
	}
	(void)st_console_end_update(page->view);
}

struct desktop_repair {
	const struct st_ddofx_graphics *g;
	struct st_console_rect          area;
};

static int
restore_desktop(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	const struct desktop_repair *repair = context;
	const struct st_console_rect area   = repair->area;
	for (uint32_t y = area.y; y < area.y + area.height; ++y) {
		for (uint32_t x = area.x; x < area.x + area.width; ++x) {
			size_t i           = (size_t)y * pixels->width + x;
			pixels->primary[i] = pixels->alternate[i] = repair->g->page[HIDDEN][y][x];
		}
	}
	*changed = area;
	return 0;
}

static struct st_console_rect
outer_area(const struct st_ddofx_text_page *page)
{
	unsigned inset = page->border ? 1 : 0;
	return (struct st_console_rect) {page->x - inset * page->cell_width, page->y - inset * page->cell_height,
	    (page->columns + 2 * inset) * page->cell_width, (page->rows + 2 * inset) * page->cell_height};
}

static void
uncover_page(struct st_ddofx_graphics *g, const struct st_ddofx_text_page *page)
{
	if (!page->view || page->hidden)
		return;
	struct desktop_repair repair = {.g = g, .area = outer_area(page)};
	if (repair.area.x >= WIDTH || repair.area.y >= HEIGHT)
		return;
	if (repair.area.width > WIDTH - repair.area.x)
		repair.area.width = WIDTH - repair.area.x;
	if (repair.area.height > HEIGHT - repair.area.y)
		repair.area.height = HEIGHT - repair.area.y;
	(void)st_console_modify_pixels(g->display, restore_desktop, &repair);
}

struct window_frame {
	struct st_ddofx_graphics        *graphics;
	const struct st_ddofx_text_page *page;
	const uint8_t                   *font;
	bool                             active;
};

static int
paint_frame(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	const struct window_frame       *frame = context;
	const struct st_ddofx_text_page *p     = frame->page;
	struct st_console_rect           area  = outer_area(p);
	if (area.x >= pixels->width || area.y >= pixels->height)
		return 0;
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
			if (col >= start && col - start < len) {
				ch    = (uint8_t)label[col - start];
				ink   = p->title_colour;
				paper = frame->active ? 4 : 1;
			}
			else if (top)
				ch = left ? 201 : right ? 187 : 205;
			else if (bottom)
				ch = left ? 200 : right ? 188 : 205;
			int x = (int)area.x + (int)col * p->cell_width;
			int y = (int)area.y + (int)row * p->cell_height;
			if (x >= WIDTH || y >= HEIGHT)
				continue;
			struct bounds clip = {x, x + p->cell_width - 1, y, y + p->cell_height - 1};
			if (clip.right >= WIDTH)
				clip.right = WIDTH - 1;
			if (clip.bottom >= HEIGHT)
				clip.bottom = HEIGHT - 1;
			st_ddofx_paint_vga_string(frame->graphics, pixels, frame->font, &ch, 1, x,
			    y + p->cell_height - 1, ink, paper, &clip);
		}
	}
	if (area.width > pixels->width - area.x)
		area.width = pixels->width - area.x;
	if (area.height > pixels->height - area.y)
		area.height = pixels->height - area.y;
	*changed = area;
	return 0;
}

static void
draw_window(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, unsigned id, const char *font)
{
	struct st_ddofx_graphics  *g    = ddo->graphics;
	struct st_ddofx_text_page *page = &g->windows[id];
	if (!page->view || page->hidden)
		return;
	st_ddofx_select_window_font(ddo, sink, page, font ? font : page->font_name);
	if (page->border) {
		struct window_frame frame = {g, page, st_ddofx_vga_bitmap(ddo), id == g->active_window};
		if (frame.font)
			(void)st_console_modify_pixels(g->display, paint_frame, &frame);
	}
	draw_text_page(page);
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

static int
place_page(const struct st_ddofx_text_page *page, bool hidden)
{
	return st_console_place_raster_view(page->view, page->x, page->y, hidden ? 0 : page->columns,
	    hidden ? 0 : page->rows);
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
			uncover_page(g, page);
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
	for (unsigned i = 0; i < 6; ++i)
		if (!st_ddofx_field_number(ddo, &args, &n[i]))
			return false;
	if (n[0] < 1 || n[1] <= n[0] || n[1] > WIDTH / g->cell_width || n[1] - n[0] >= 80 || n[2] < 1
	    || n[3] <= n[2] || n[3] > HEIGHT / g->cell_height || n[3] - n[2] >= 100 || (unsigned)n[4] > 15
	    || (unsigned)n[5] > 15)
		return false;
	const char *border = st_ddofx_field(&args), *hidden = st_ddofx_field(&args);
	for (unsigned i = 0; i < 3; ++i) /* X and Y tracking, manual scroll. */
		if (!st_ddofx_field(&args))
			return false;
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
	else if (place_page(&page, page.hidden))
		return false;
	uncover_page(g, old);
	if (create)
		st_ddofx_destroy_text_page(old);
	*old             = page;
	g->active_window = id;
	if (create && page.hidden)
		st_ddofx_select_window_font(ddo, sink, old, old->font_name);
	return true;
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
	if (!ascii_case_compare(op, "1GETPOS") && !args) {
		if (!id)
			st_ddofx_query_reply(ddo, sink, "NONE", false);
		else {
			struct st_cterm_state state;
			if (st_cterm_state(page->text, &state) == 0) {
				char reply[64];
				snprintf(reply, sizeof(reply), "%d,%d,1,1", state.x, state.y);
				st_ddofx_query_reply(ddo, sink, reply, false);
			}
		}
		return;
	}
	if (!ascii_case_compare(op, "1OPEN")) {
		if (!st_ddofx_field_number(ddo, &args, &n[0]) || !valid_slot(n[0])
		    || !configure_window(ddo, sink, args, (unsigned)n[0], true))
			return;
	}
	else if (!id)
		return;
	else if (!ascii_case_compare(op, "1CHANGE")) {
		if (!configure_window(ddo, sink, args, id, false))
			return;
	}
	else if (!ascii_case_compare(op, "1CLOSE") && !args) {
		uncover_page(g, page);
		st_ddofx_destroy_text_page(page);
		g->active_window = 0;
		for (unsigned i = 1; i < 10; ++i) {
			id = id == 1 ? 10 : id - 1;
			if (g->windows[id].view) {
				g->active_window = id;
				break;
			}
		}
	}
	else if ((!ascii_case_compare(op, "1HIDE") || !ascii_case_compare(op, "1UNHIDE")) && !args) {
		bool hidden = !ascii_case_compare(op, "1HIDE");
		if (hidden != page->hidden) {
			if (place_page(page, hidden))
				return;
			if (hidden)
				uncover_page(g, page);
			page->hidden = hidden;
		}
	}
	else if (!ascii_case_compare(op, "1WIN") && st_ddofx_numbers(ddo, args, n, 1) && valid_slot(n[0])
	         && g->windows[n[0]].view && id != (unsigned)n[0])
		g->active_window = (unsigned)n[0];
	else if (!ascii_case_compare(op, "1SWAP") && st_ddofx_numbers(ddo, args, n, 2) && valid_slot(n[0])
	         && valid_slot(n[1]) && g->windows[n[0]].view && g->windows[n[1]].view && n[0] != n[1]) {
		struct st_ddofx_text_page saved = g->windows[n[0]];
		g->windows[n[0]]                = g->windows[n[1]];
		g->windows[n[1]]                = saved;
		if (id == (unsigned)n[0])
			g->active_window = (unsigned)n[1];
		else if (id == (unsigned)n[1])
			g->active_window = (unsigned)n[0];
	}
	else
		return;
	redraw_text_pages(ddo, sink, NULL);
}
