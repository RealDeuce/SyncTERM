/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics.h"
#include "st_ddofx_script.h"
#include "string_util.h"

enum {
	WIDTH  = 640,
	HEIGHT = 480
};

enum page {
	WORK,
	BACKGROUND,
	HIDDEN,
	VISUAL
};

struct st_ddofx_graphics {
	st_console_t            *display, *view;
	st_font_catalog_t       *fonts;
	st_cterm_t              *text;
	struct st_console_config previous_mode;
	st_console_snapshot_t   *previous_screen;
	uint32_t                 palette[256];
	uint8_t                  page[3][HEIGHT][WIDTH];
	bool                     transparent[256];
	uint8_t                  colour;
};

static const uint32_t text_colours[16] = {
    0x000000,
    0x0000aa,
    0x00aa00,
    0x00aaaa,
    0xaa0000,
    0xaa00aa,
    0xaa5500,
    0xaaaaaa,
    0x555555,
    0x5555ff,
    0x55ff55,
    0x55ffff,
    0xff5555,
    0xff55ff,
    0xffff00,
    0xffffff,
};

st_console_t *
st_ddofx_graphics_view(struct st_ddofx_graphics *g)
{
	return g->view;
}

st_cterm_t *
st_ddofx_graphics_text(struct st_ddofx_graphics *g)
{
	return g->text;
}

bool
st_ddofx_svga(const struct st_ddofx_sink *sink)
{
	uint32_t caps;
	return sink->text_config != NULL && st_console_capabilities(sink->text_config->console, &caps) == 0
	       && (caps & ST_CONSOLE_CAP_PIXELS);
}

void
st_ddofx_graphics_exit(struct st_ddofx *ddo)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	if (!g)
		return;
	st_cterm_destroy(&g->text);
	st_font_catalog_destroy(g->fonts);
	if (g->view)
		st_console_destroy(g->view);
	if (g->previous_screen) {
		(void)st_console_reconfigure(g->display, &g->previous_mode);
		(void)st_console_snapshot_restore(g->display, g->previous_screen, NULL);
		st_console_snapshot_destroy(g->previous_screen);
	}
	ddo->graphics = NULL;
	free(g);
}

static int
blank(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	(void)context;
	size_t size = (size_t)pixels->width * pixels->height * sizeof(*pixels->primary);
	memset(pixels->primary, 0, size);
	memset(pixels->alternate, 0, size);
	*changed = (struct st_console_rect) {0, 0, pixels->width, pixels->height};
	return 0;
}

void
st_ddofx_graphics_enter(struct st_ddofx *ddo, const struct st_ddofx_sink *sink)
{
	if (!st_ddofx_svga(sink))
		return;
	struct st_ddofx_graphics *g = calloc(1, sizeof(*g));
	if (g == NULL)
		return;
	struct st_cterm_config config = *sink->text_config;
	g->display                    = config.console;
	if (st_console_get_config(g->display, &g->previous_mode) != 0
	    || st_console_snapshot_create(&g->previous_screen, g->display) != 0) {
		free(g);
		return;
	}
	ddo->graphics                 = g;
	struct st_console_config mode = {.columns = 80,
	    .rows                                 = 30,
	    .cell_width                           = 8,
	    .cell_height                          = 16,
	    .aspect_width                         = 4,
	    .aspect_height                        = 3,
	    .preserve_window_size                 = true,
	    .scaling_mode                         = g->previous_mode.scaling_mode,
	    .scaling_policy                       = g->previous_mode.scaling_policy,
	    .color_emoji                          = g->previous_mode.color_emoji};
	g->colour                     = 10;
	(void)st_console_get_default_palette(0, g->palette, 256);
	memcpy(g->palette, text_colours, sizeof(text_colours));
	if (ddo->saved_palette == NULL) {
		ddo->saved_palette = malloc(sizeof(g->palette));
		if (ddo->saved_palette == NULL) {
			st_console_snapshot_destroy(g->previous_screen);
			free(g);
			ddo->graphics = NULL;
			return;
		}
		memcpy(ddo->saved_palette, g->palette, sizeof(g->palette));
	}
	else
		memcpy(g->palette, ddo->saved_palette, sizeof(g->palette));
	if (st_console_reconfigure(g->display, &mode) != 0
	    || st_console_set_cursor(g->display, false, ST_CONSOLE_CURSOR_BLOCK) != 0
	    || st_console_set_palette(g->display, 0, g->palette, 256) != 0
	    || st_console_modify_pixels(g->display, blank, NULL) != 0
	    || st_console_create_raster_view(&g->view, g->display, 80, 100, 8, 16, 0, 0) != 0
	    || st_font_catalog_create(&g->fonts, g->view) != 0) {
		st_ddofx_graphics_exit(ddo);
		return;
	}
	static const uint16_t pc_indices[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	config.console                       = g->view;
	config.fonts                         = g->fonts;
	config.terminal_rows                 = 100;
	config.status_line                   = false;
	config.scrollback_lines              = 0;
	config.size_changed                  = NULL;
	config.emulation                     = ST_CTERM_ANSI_BBS;
	config.palette_mapping               = pc_indices;
	/* Text and DDO share one hardware palette, including indices 0..15. */
	config.disabled_capabilities |= ST_CONSOLE_CAP_EXTENDED_PALETTE;
	if (st_cterm_create(&g->text, &config) != 0 || st_cterm_start_text(g->text) != 0
	    || st_console_raster_view_adopt_indexed(g->view) != 0)
		st_ddofx_graphics_exit(ddo);
}

struct transfer {
	struct st_ddofx_graphics *g;
	enum page                 source, dest;
	int                       x, y, width, height, dx, dy;
	bool                      transparent;
};

static bool
page_name(const char *s, enum page *page)
{
	static const char *const names[] = {"WP", "BP", "HP", "VP"};
	for (unsigned i = 0; i < 4; ++i) {
		if (ascii_case_ncompare(s, names[i], 2) == 0) {
			*page = (enum page)i;
			return true;
		}
	}
	return false;
}

/* Most source pixels already carry hardware indices. Text extensions can
 * paint direct RGB, in which case capture the nearest available DAC entry. */
static uint8_t
index_of(const uint32_t *palette, uint32_t pixel)
{
	if (pixel < 256)
		return (uint8_t)pixel;
	uint32_t rgb     = ST_CONSOLE_COLOR_IS_RGB(pixel) ? pixel : palette[(uint16_t)pixel];
	unsigned closest = 0, distance = UINT32_MAX;
	for (unsigned i = 0; i < 256; ++i) {
		int      dr = (int)((rgb >> 16) & 255) - (int)(palette[i] >> 16);
		int      dg = (int)((rgb >> 8) & 255) - (int)((palette[i] >> 8) & 255);
		int      db = (int)(rgb & 255) - (int)(palette[i] & 255);
		unsigned d  = (unsigned)(dr * dr + dg * dg + db * db);
		if (d < distance) {
			closest  = i;
			distance = d;
			if (!d)
				break;
		}
	}
	return (uint8_t)closest;
}

static int
copy_region(void *context, const struct st_pixel_buffer *pixels, const uint32_t *palette,
    struct st_console_rect *changed)
{
	const struct transfer    *t = context;
	struct st_ddofx_graphics *g = t->g;
	if (pixels != NULL && (pixels->width != WIDTH || pixels->height != HEIGHT))
		return 0;
	for (int row = 0; row < t->height; ++row) {
		const uint8_t *src = t->source == VISUAL ? NULL : &g->page[t->source][t->y + row][t->x];
		uint8_t       *dst = t->dest == VISUAL ? NULL : &g->page[t->dest][t->dy + row][t->dx];
		if (src && dst && !t->transparent) {
			memcpy(dst, src, (size_t)t->width);
			continue;
		}
		size_t from = (size_t)(t->y + row) * WIDTH + t->x;
		size_t to   = (size_t)(t->dy + row) * WIDTH + t->dx;
		for (int x = 0; x < t->width; ++x) {
			uint8_t index = src ? src[x] : index_of(palette, pixels->primary[from + x]);
			if (t->transparent && g->transparent[index])
				continue;
			if (dst)
				dst[x] = index;
			else
				pixels->primary[to + x] = pixels->alternate[to + x] = index;
		}
	}
	if (t->dest == VISUAL) {
		*changed = (struct st_console_rect) {t->dx, t->dy, t->width, t->height};
		/* VP publication supplies the hidden restoration image for animations.
		 * Read back after the copy so a cut from HP can overlap its source. */
		for (int y = t->dy; y < t->dy + t->height; ++y) {
			for (int x = t->dx; x < t->dx + t->width; ++x)
				g->page[HIDDEN][y][x] = index_of(palette, pixels->primary[y * WIDTH + x]);
		}
	}
	return 0;
}

static bool
numbers(struct st_ddofx *ddo, char *args, int32_t *n, unsigned count)
{
	for (unsigned i = 0; i < count; ++i)
		if (!st_ddofx_field_number(ddo, &args, &n[i]))
			return false;
	return args == NULL;
}

static void
copy_page(struct st_ddofx *ddo, const char *verb, char *args, bool cut)
{
	size_t          length = strlen(verb);
	struct transfer t      = {.g = ddo->graphics, .width = WIDTH, .height = HEIGHT};
	if (length != 9 && !(cut && length == 10 && (verb[9] == 't' || verb[9] == 'T')))
		return;
	t.transparent = length == 10;
	if (verb[6] != '2' || !page_name(verb + 4, &t.source) || !page_name(verb + 7, &t.dest)
	    || t.source == t.dest || (t.transparent && t.source == VISUAL))
		return;
	if (cut) {
		int32_t n[6];
		if (!numbers(ddo, args, n, 6) || n[0] < 0 || n[1] <= n[0] || n[1] >= WIDTH || n[2] < 0
		    || n[3] <= n[2] || n[3] >= HEIGHT)
			return;
		t.x      = n[0];
		t.y      = n[2];
		t.width  = n[1] - n[0] + 1;
		t.height = n[3] - n[2] + 1;
		if (n[4] < 0 || n[4] > WIDTH - t.width || n[5] < t.height - 1 || n[5] >= HEIGHT)
			return;
		t.dx = n[4];
		t.dy = n[5] - (t.height - 1);
	}
	else if (args != NULL)
		return;
	if (t.source == VISUAL || t.dest == VISUAL)
		(void)st_console_modify_pixels_with_palette(t.g->display, copy_region, &t);
	else
		(void)copy_region(&t, NULL, NULL, NULL);
}

static uint32_t
dac(int32_t value)
{
	return (uint32_t)((value << 2) | (value >> 4));
}

static void
set_palette(struct st_ddofx_graphics *g, unsigned first, unsigned count)
{
	(void)st_console_set_palette(g->display, (uint16_t)first, g->palette + first, count);
	(void)st_console_set_palette(g->view, (uint16_t)first, g->palette + first, count);
}

void
st_ddofx_graphics_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *body)
{
	struct st_ddofx_graphics *g    = ddo->graphics;
	char                     *args = body;
	const char               *verb = st_ddofx_field(&args);
	int32_t                   n[4];
	if (!ascii_case_compare(verb, "2GEXIT") && args == NULL)
		st_ddofx_graphics_exit(ddo);
	else if (!ascii_case_ncompare(verb, "2CPY", 4))
		copy_page(ddo, verb, args, false);
	else if (!ascii_case_ncompare(verb, "2CUT", 4))
		copy_page(ddo, verb, args, true);
	else if (!ascii_case_compare(verb, "2CCOL") && numbers(ddo, args, n, 2) && (unsigned)n[0] <= 255) {
		if (n[1] == 0 || n[1] == 1)
			g->transparent[n[0]] = n[1] == 1;
		else if (n[0] == 0 && (n[1] == 3 || n[1] == 4))
			for (unsigned i = 0; i < 256; ++i)
				g->transparent[i] = n[1] == 3;
	}
	else if (!ascii_case_compare(verb, "2C") && numbers(ddo, args, n, 1) && (unsigned)n[0] <= 255)
		g->colour = (uint8_t)n[0];
	else if (verb[1] == 'a') {
		const char *op = verb + 2;
		if (!ascii_case_compare(op, "P") || !ascii_case_compare(op, "PX")
		    || !ascii_case_compare(op, "G")) {
			if (!numbers(ddo, args, n, 2) || (unsigned)n[0] >= WIDTH || (unsigned)n[1] >= HEIGHT)
				return;
			uint8_t *pixel = &g->page[WORK][n[1]][n[0]];
			if (!ascii_case_compare(op, "G")) {
				char reply[4];
				snprintf(reply, sizeof(reply), "%u", (unsigned)*pixel);
				st_ddofx_query_reply(ddo, sink, reply, true);
			}
			else if (op[1] != 0)
				*pixel ^= g->colour;
			else
				*pixel = g->colour;
		}
		else if (args == NULL && (!ascii_case_compare(op, "FILL") || !ascii_case_compare(op, "ERASE")))
			memset(g->page[WORK], !ascii_case_compare(op, "FILL") ? g->colour : 0,
			    sizeof(g->page[WORK]));
		else if (!ascii_case_compare(op, "SRGB") && numbers(ddo, args, n, 4) && (unsigned)n[0] < 256
		         && (unsigned)n[1] < 64 && (unsigned)n[2] < 64 && (unsigned)n[3] < 64) {
			g->palette[n[0]] = dac(n[1]) << 16 | dac(n[2]) << 8 | dac(n[3]);
			set_palette(g, (unsigned)n[0], 1);
		}
		else if (!ascii_case_compare(op, "GRGB") && numbers(ddo, args, n, 1) && (unsigned)n[0] < 256) {
			uint32_t rgb = g->palette[n[0]];
			char     reply[12];
			snprintf(reply, sizeof(reply), "%u,%u,%u", (unsigned)(rgb >> 18),
			    (unsigned)((rgb >> 10) & 63), (unsigned)((rgb >> 2) & 63));
			st_ddofx_query_reply(ddo, sink, reply, false);
		}
		else if (args == NULL) {
			if (!ascii_case_compare(op, "STOREPAL"))
				memcpy(ddo->saved_palette, g->palette, sizeof(g->palette));
			else if (!ascii_case_compare(op, "RESTOREPAL")) {
				memcpy(g->palette, ddo->saved_palette, sizeof(g->palette));
				set_palette(g, 0, 256);
			}
			else if (!ascii_case_compare(op, "ZEROPAL")) {
				memset(g->palette, 0, sizeof(g->palette));
				set_palette(g, 0, 256);
			}
			else if (!ascii_case_compare(op, "DEFPAL")) {
				memcpy(g->palette, text_colours, sizeof(text_colours));
				set_palette(g, 0, 16);
			}
		}
	}
}
