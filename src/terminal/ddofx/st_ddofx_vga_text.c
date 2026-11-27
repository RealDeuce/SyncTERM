/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_builtin_fonts.h"
#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_script.h"
#include "string_util.h"

struct st_ddofx_vga_font {
	uint8_t       bitmap[256 * 16];
	char          name[ST_DDOFX_BODY_MAX + 1];
	st_console_t *lite;
	uint16_t      lite_id;
};

const char *
st_ddofx_vga_font_name(const struct st_ddofx *ddo)
{
	return ddo->vga_font ? ddo->vga_font->name : "IBMSTD.FNT";
}

const uint8_t *
st_ddofx_vga_bitmap(const struct st_ddofx *ddo)
{
	struct st_console_font builtin;
	if (ddo->vga_font)
		return ddo->vga_font->bitmap;
	return st_builtin_font_get(0, 8, 16, &builtin) ? builtin.bitmap : NULL;
}

static bool
install(const struct st_ddofx_text_page *page, const struct st_ddofx_vga_font *font, unsigned width,
    unsigned height)
{
	struct st_console_font strike;
	if (!st_builtin_font_get(0, 8, 16, &strike))
		return false;
	const uint8_t *source = font ? font->bitmap : strike.bitmap;
	uint8_t        cropped[4096];
	if (height != 16) {
		for (unsigned ch = 0; ch < 256; ++ch)
			memcpy(cropped + ch * height, source + ch * 16 + 16 - height, height);
		source = cropped;
	}
	strike.bitmap        = source;
	strike.width         = width;
	strike.height        = height;
	strike.glyph_stride  = height;
	strike.bitmap_size   = height * 256;
	strike.raster_family = NULL;
	uint16_t id;
	if (!page)
		return st_console_set_font_strike(font->lite, font->lite_id, &strike) == 0;
	if (!st_font_catalog_identifier(page->fonts, 0, &id)
	    || st_console_raster_view_adopt_indexed_size(page->view, width, height))
		return false;
	if (st_console_set_font_strike(page->view, id, &strike) == 0)
		return true;
	(void)st_console_raster_view_adopt_indexed_size(page->view, page->cell_width, page->cell_height);
	return false;
}

struct st_console_rect
st_ddofx_text_area(const struct st_ddofx_graphics *g, unsigned x, unsigned bottom, unsigned columns, unsigned rows,
    unsigned padding)
{
	unsigned width = columns * (unsigned)g->cell_width, height = rows * (unsigned)g->cell_height + padding;
	if (width > WIDTH - x)
		width = WIDTH - x;
	if (height > bottom + 1)
		height = bottom + 1;
	return (struct st_console_rect) {x, bottom + 1 - height, width, height};
}

void
st_ddofx_apply_vga_font(struct st_ddofx *ddo)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	if (g || ddo->vga_font)
		(void)install(g ? &g->windows[g->active_window] : NULL, ddo->vga_font, g ? g->cell_width : 8,
		    g ? g->cell_height : 16);
}

void
st_ddofx_release_vga_font(struct st_ddofx *ddo)
{
	if (ddo->vga_font) {
		(void)st_console_set_font_strike(ddo->vga_font->lite, ddo->vga_font->lite_id, NULL);
		free(ddo->vga_font);
		ddo->vga_font = NULL;
	}
}

bool
st_ddofx_read_vga_font(const struct st_ddofx_sink *sink, const char *name, uint8_t bitmap[4096])
{
	bool  loaded = false;
	FILE *file   = sink->open_asset ? sink->open_asset(sink->context, "FONTS", NULL, name) : NULL;
	if (file) {
		loaded = fread(bitmap, 1, 4096, file) == 4096 && fgetc(file) == EOF && !ferror(file);
		fclose(file);
	}
	else if (!ascii_case_compare(name, "IBMSTD.FNT")) {
		struct st_console_font builtin;
		if (st_builtin_font_get(0, 8, 16, &builtin)) {
			memcpy(bitmap, builtin.bitmap, 4096);
			loaded = true;
		}
	}
	return loaded;
}

static bool
load_font(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *name,
    struct st_ddofx_text_page *page)
{
	if (!*name || strlen(name) > ST_DDOFX_BODY_MAX || !sink->text_config || !sink->open_asset)
		return false;
	struct st_ddofx_vga_font *next = malloc(sizeof(*next));
	if (!next)
		return false;
	bool loaded = st_ddofx_read_vga_font(sink, name, next->bitmap);
	next->lite  = sink->text_config->console;
	if (!loaded
	    || !st_font_catalog_identifier(sink->text_config->fonts, sink->text_config->font_selector,
		&next->lite_id)
	    || !install(page, next, page ? page->cell_width : 8, page ? page->cell_height : 16)) {
		free(next);
		return false;
	}
	strcpy(next->name, name);
	free(ddo->vga_font);
	ddo->vga_font = next;
	return true;
}

void
st_ddofx_load_vga_font(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *name)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	(void)load_font(ddo, sink, name, g ? &g->windows[g->active_window] : NULL);
}

void
st_ddofx_select_window_font(struct st_ddofx *ddo, const struct st_ddofx_sink *sink,
    struct st_ddofx_text_page *page, const char *name)
{
	const char *current = st_ddofx_vga_font_name(ddo);
	if (ascii_case_compare(name, current)) {
		if (load_font(ddo, sink, name, page))
			return;
		strcpy(page->font_name, current);
	}
	(void)install(page, ddo->vga_font, page->cell_width, page->cell_height);
}

struct text_run {
	const struct bounds      *clip;
	struct st_ddofx_graphics *g;
	enum page                 page;
	const uint8_t            *font, *text;
	int                       x, bottom, ink, paper, length;
};

static int
paint(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	const struct text_run *run = context;
	if (pixels && (pixels->width != WIDTH || pixels->height != HEIGHT))
		return 0;
	/* FNT records always have 16 rows of 8 bits. A scaled font puts its
	 * ink in the lower left. Only the advance and paper rectangle shrink. */
	int           cw = run->g->cell_width, ch = run->g->cell_height;
	struct bounds clip = run->clip ? *run->clip : (struct bounds) {0, WIDTH - 1, 0, HEIGHT - 1};
	int           top  = run->bottom - 15;
	for (int i = 0; i < run->length; ++i) {
		const uint8_t *rows = run->font + run->text[i] * 16;
		int            left = run->x + i * cw;
		for (int y = 0; y < 16; ++y) {
			int row = top + y;
			if (row < clip.top || row > clip.bottom)
				continue;
			for (int x = 0; x < 8; ++x) {
				int col = left + x;
				if (col < clip.left || col > clip.right)
					continue;
				bool ink = (rows[y] & (0x80u >> x)) != 0;
				if (!ink && (run->paper == 8 || x >= cw || y < 16 - ch))
					continue;
				uint8_t colour = (uint8_t)(ink ? run->ink : run->paper);
				if (pixels)
					pixels->primary[row * WIDTH + col] = pixels->alternate[row * WIDTH + col] =
					    colour;
				else
					run->g->page[run->page][row][col] = colour;
			}
		}
	}
	if (changed) {
		int left = run->x, right = left + cw * (run->length - 1) + 8;
		if (right > WIDTH)
			right = WIDTH;
		if (top < 0)
			top = 0;
		*changed = (struct st_console_rect) {left, top, right - left, run->bottom - top + 1};
	}
	return 0;
}

void
st_ddofx_paint_vga_string(struct st_ddofx_graphics *g, const struct st_pixel_buffer *pixels, const uint8_t *bitmap,
    const uint8_t *text, unsigned length, int x, int bottom, unsigned ink, unsigned paper,
    const struct bounds *clip)
{
	struct text_run run = {.g = g,
	    .page                 = WORK,
	    .clip                 = clip,
	    .font                 = bitmap,
	    .text                 = text,
	    .length               = (int)length,
	    .x                    = x,
	    .bottom               = bottom,
	    .ink                  = (int)ink,
	    .paper                = (int)paper};
	(void)paint(&run, pixels, NULL);
}

void
st_ddofx_print_vga_text(struct st_ddofx *ddo, const char *suffix, char *args)
{
	struct text_run run = {.g = ddo->graphics, .page = VISUAL};
	if (*suffix && (strlen(suffix) != 2 || !st_ddofx_page_name(suffix, &run.page)))
		return;
	int32_t n[4];
	for (unsigned i = 0; i < 4; ++i)
		if (!st_ddofx_field_number(ddo, &args, &n[i]))
			return;
	if (!args || n[0] < 0 || n[0] >= WIDTH || n[1] < run.g->cell_height - 1 || n[1] >= HEIGHT
	    || (unsigned)n[2] > 255 || (unsigned)n[3] > 8)
		return;
	run.text      = (const uint8_t *)st_ddofx_value(ddo, args);
	size_t length = strlen((const char *)run.text);
	if (!length || length > (size_t)(WIDTH - n[0]) / (unsigned)run.g->cell_width)
		return;
	run.font = st_ddofx_vga_bitmap(ddo);
	if (!run.font)
		return;
	run.x      = n[0];
	run.bottom = n[1];
	run.ink    = n[2];
	run.paper  = n[3];
	run.length = (int)length;
	if (run.page == VISUAL)
		(void)st_console_modify_pixels(run.g->display, paint, &run);
	else
		(void)paint(&run, NULL, NULL);
}
