/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
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
	if (!page->view || page->hidden)
		return;
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

static void
remove_text_page(struct st_ddofx_graphics *g, unsigned id)
{
	struct st_ddofx_text_page *page = &g->windows[id];
	if (!page->view)
		return;
	if (!page->hidden && page->x < WIDTH && page->y < HEIGHT) {
		struct desktop_repair repair = {
		    .g    = g,
		    .area = {page->x, page->y, page->columns * page->cell_width, page->rows * page->cell_height}
                };
		if (repair.area.width > WIDTH - repair.area.x)
			repair.area.width = WIDTH - repair.area.x;
		if (repair.area.height > HEIGHT - repair.area.y)
			repair.area.height = HEIGHT - repair.area.y;
		(void)st_console_modify_pixels(g->display, restore_desktop, &repair);
	}
	st_ddofx_destroy_text_page(page);
}

static void
redraw_text_pages(struct st_ddofx_graphics *g)
{
	for (unsigned i = 1; i <= 10; ++i)
		if (i != g->active_window)
			draw_text_page(&g->windows[i]);
	if (g->active_window)
		draw_text_page(&g->windows[g->active_window]);
}

void
st_ddofx_text_window_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *body)
{
	struct st_ddofx_graphics *g    = ddo->graphics;
	char                     *args = body;
	const char               *op   = st_ddofx_field(&args);
	if (!ascii_case_compare(op, "1CLOSE") && !args && g->active_window) {
		unsigned id = g->active_window;
		remove_text_page(g, id);
		g->active_window = 0;
		for (unsigned i = 1; i < 10; ++i) {
			id = id == 1 ? 10 : id - 1;
			if (g->windows[id].view) {
				g->active_window = id;
				break;
			}
		}
		redraw_text_pages(g);
		return;
	}
	if (ascii_case_compare(op, "1OPEN"))
		return;
	int32_t n[7];
	for (unsigned i = 0; i < 7; ++i)
		if (!st_ddofx_field_number(ddo, &args, &n[i]))
			return;
	if (n[0] < 1 || n[0] > 10 || n[1] < 1 || n[2] <= n[1] || n[2] > 80 || n[3] < 1 || n[4] <= n[3]
	    || n[4] > 100)
		return;
	const char *border = st_ddofx_field(&args);
	const char *hidden = st_ddofx_field(&args);
	for (unsigned i = 0; i < 5; ++i) /* Two trackers, manual scrolling, title and help. */
		if (!st_ddofx_field(&args))
			return;
	if (!args || !border || !hidden)
		return;
	unsigned inset  = (*border == 'y' || *border == 'Y');
	int      width  = n[2] - n[1] + 1 - (int)(2 * inset);
	int      height = n[4] - n[3] + 1 - (int)(2 * inset);
	if (width <= 0 || height <= 0)
		return;
	struct st_ddofx_text_page page = {.columns = (unsigned)width,
	    .rows                                  = (unsigned)height,
	    .x                                     = (n[1] - 1 + (int)inset) * g->cell_width,
	    .y                                     = (n[3] - 1 + (int)inset) * g->cell_height,
	    .cell_width                            = g->cell_width,
	    .cell_height                           = g->cell_height,
	    .hidden                                = *hidden == 'Y' || *hidden == 'y'};
	if (!st_ddofx_create_text_page(g, &page, sink->text_config))
		return;
	remove_text_page(g, (unsigned)n[0]);
	g->windows[n[0]] = page;
	g->active_window = (unsigned)n[0];
	st_ddofx_apply_vga_font(ddo);
	if (*args && !page.hidden)
		st_ddofx_load_vga_font(ddo, sink, args);
	redraw_text_pages(g);
}
