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
install(const struct st_ddofx_graphics *g, const struct st_ddofx_vga_font *font)
{
	struct st_console_font strike;
	if (!st_builtin_font_get(0, 8, 16, &strike))
		return false;
	strike.bitmap        = font->bitmap;
	strike.raster_family = NULL;
	uint16_t id          = font->lite_id;
	if (g) {
		/* Text drawn earlier may already be covered by graphics on VP. */
		if (!st_font_catalog_identifier(g->fonts, 0, &id)
		    || st_console_raster_view_adopt_indexed(g->view) != 0)
			return false;
	}
	return st_console_set_font_strike(g ? g->view : font->lite, id, &strike) == 0;
}

void
st_ddofx_apply_vga_font(struct st_ddofx *ddo)
{
	if (ddo->vga_font)
		(void)install(ddo->graphics, ddo->vga_font);
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

void
st_ddofx_load_vga_font(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *name)
{
	if (!*name || strlen(name) > ST_DDOFX_BODY_MAX || !sink->text_config || !sink->open_asset)
		return;
	struct st_ddofx_vga_font *next = malloc(sizeof(*next));
	if (!next)
		return;
	bool loaded = st_ddofx_read_vga_font(sink, name, next->bitmap);
	next->lite  = sink->text_config->console;
	if (!loaded
	    || !st_font_catalog_identifier(sink->text_config->fonts, sink->text_config->font_selector,
		&next->lite_id)
	    || !install(ddo->graphics, next)) {
		free(next);
		return;
	}
	strcpy(next->name, name);
	free(ddo->vga_font);
	ddo->vga_font = next;
}

struct text_run {
	const struct bounds      *clip;
	struct st_ddofx_graphics *g;
	enum page                 page;
	const uint8_t            *font, *text;
	int                       x, top, ink, paper, length;
};

static int
paint(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	const struct text_run *run = context;
	if (pixels && (pixels->width != WIDTH || pixels->height != HEIGHT))
		return 0;
	for (int i = 0; i < run->length; ++i) {
		const uint8_t *rows = run->font + run->text[i] * 16;
		for (int y = 0; y < 16; ++y)
			for (int x = 0; x < 8; ++x) {
				bool lit = (rows[y] & (0x80u >> x)) != 0;
				if (!lit && run->paper == 8)
					continue;
				int col = run->x + i * 8 + x, row = run->top + y;
				if (run->clip
				    && (col < run->clip->left || col > run->clip->right || row < run->clip->top
				        || row > run->clip->bottom))
					continue;
				uint8_t value = (uint8_t)(lit ? run->ink : run->paper);
				if (pixels)
					pixels->primary[row * WIDTH + col] = pixels->alternate[row * WIDTH + col] =
					    value;
				else
					run->g->page[run->page][row][col] = value;
			}
	}
	if (changed)
		*changed = (struct st_console_rect) {run->x, run->top, 8 * run->length, 16};
	return 0;
}

void
st_ddofx_paint_vga_string(const struct st_pixel_buffer *pixels, const uint8_t *bitmap, const uint8_t *text,
    unsigned length, int x, int top, unsigned ink, unsigned paper, const struct bounds *clip)
{
	struct text_run run = {.clip = clip,
	    .font                    = bitmap,
	    .text                    = text,
	    .length                  = (int)length,
	    .x                       = x,
	    .top                     = top,
	    .ink                     = (int)ink,
	    .paper                   = (int)paper};
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
	if (!args || n[0] < 0 || n[0] >= WIDTH || n[1] < 15 || n[1] >= HEIGHT || (unsigned)n[2] > 255
	    || (unsigned)n[3] > 8)
		return;
	run.text      = (const uint8_t *)st_ddofx_value(ddo, args);
	size_t length = strlen((const char *)run.text);
	if (!length || length > (size_t)(WIDTH - n[0]) / 8)
		return;
	run.font = st_ddofx_vga_bitmap(ddo);
	if (!run.font)
		return;
	run.x      = n[0];
	run.top    = n[1] - 15;
	run.ink    = n[2];
	run.paper  = n[3];
	run.length = (int)length;
	if (run.page == VISUAL)
		(void)st_console_modify_pixels(run.g->display, paint, &run);
	else
		(void)paint(&run, NULL, NULL);
}
