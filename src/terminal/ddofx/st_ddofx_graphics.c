/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"
#include "st_ddofx_script.h"
#include "st_ddofx_samples.h"
#include "st_ddofx_ray.h"
#include "st_time.h"
#include "string_util.h"

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

bool
st_ddofx_svga(const struct st_ddofx_sink *sink)
{
	uint32_t caps;
	return sink->text_config != NULL && st_console_capabilities(sink->text_config->console, &caps) == 0
	       && (caps & ST_CONSOLE_CAP_PIXELS);
}

struct st_ddofx_surface
st_ddofx_surface(struct st_ddofx_graphics *g, unsigned owner, enum page page)
{
	struct st_ddofx_graphic_window *w = &g->graphic_windows[owner];
	if (w->visual && (page == VISUAL || page == HIDDEN))
		return (struct st_ddofx_surface){page == VISUAL ? w->visual : w->hidden_page,
		    w->width, w->height, w->clip};
	struct bounds clip = page == WORK ? g->clip : (struct bounds){0, WIDTH - 1, 0, HEIGHT - 1};
	return (struct st_ddofx_surface){page == VISUAL ? NULL : (uint8_t *)g->page[page], WIDTH, HEIGHT, clip};
}

static void
clear_graphics_objects(struct st_ddofx_graphics *g)
{
	st_ddofx_close_text_windows(g);
	st_ddofx_free_graphic_windows(g);
	if (g->mouse.pointers) {
		(void)st_console_set_indexed_pointer(g->display, 0, 0, NULL);
		free(g->mouse.pointers);
	}
	if (g->mouse.visibility_changed)
		(void)st_console_set_pointer_visible(g->display, g->mouse.previous_visibility);
	for (unsigned i = 0; i < 2; ++i) {
		free(g->choices[i]);
		g->choices[i] = NULL;
	}
	st_ddofx_clear_lists(g);
	st_ddofx_clear_text_fields(g);
	free(g->buttons);
	g->buttons = NULL;
	free(g->mouse.fields);
	free(g->mouse.regions);
	g->mouse = (struct st_ddofx_mouse_state){0};
	st_ddofx_mouse_defaults(&g->mouse);
	st_ddofx_grid_destroy(g);
	st_ddofx_clear_polygons(g);
	st_ddofx_ray_stop_recording(g->ray);
	st_ddofx_ray_clear_profiles(g->ray);
	if (g->ray)
		free(g->ray->environment);
	free(g->ray);
	g->ray = NULL;
	free(g->landscape);
	g->landscape = NULL;
	st_ddofx_clear_movies(g);
	st_ddofx_clear_clocks(g);
	st_ddofx_clear_marquees(g);
	free(g->collisions);
	g->collisions = NULL;
	free(g->icons);
	g->icons = NULL;
	st_ddofx_clear_animations(g);
	free(g->simple);
	g->simple = NULL;
	free(g->bars);
	g->bars = NULL;
	for (unsigned i = 0; i < 32; ++i) {
		free(g->bitmap_fonts[i]);
		g->bitmap_fonts[i] = NULL;
	}
	g->bitmap_slot = 0;
	st_ddofx_free_sprites(g);
}

void
st_ddofx_present_origin(struct st_ddofx *ddo)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	if (!g)
		return;
	bool visible = !ddo->sleeper.warning && !ddo->sleeper.suspended;
	(void)st_console_set_display_origin(g->display, HEIGHT / 16, visible ? g->origin_x : 0,
	    visible ? g->origin_y : 0);
}

static void
set_origin(struct st_ddofx *ddo, uint32_t x, uint32_t y)
{
	ddo->graphics->origin_x = x % WIDTH;
	ddo->graphics->origin_y = y < HEIGHT ? y : HEIGHT;
	st_ddofx_present_origin(ddo);
}

uint32_t
st_ddofx_shake_wait_ms(const struct st_ddofx *ddo)
{
	if (!ddo->graphics || !ddo->shake_phase || ddo->background_paused || ddo->sleeper.warning
	    || ddo->sleeper.suspended)
		return UINT32_MAX;
	uint64_t now;
	if (st_time_monotonic_ms(&now) || now >= ddo->shake_due)
		return 0;
	return (uint32_t)(ddo->shake_due - now);
}

static void
schedule_shake_frame(struct st_ddofx *ddo)
{
	uint64_t now;
	ddo->shake_due = st_time_monotonic_ms(&now) ? 0 : now + 16;
}

void
st_ddofx_shake_poll(struct st_ddofx *ddo)
{
	if (st_ddofx_shake_wait_ms(ddo))
		return;
	unsigned phase = ddo->shake_phase;
	if (phase == 33) {
		set_origin(ddo, 0, 0);
		ddo->shake_phase = 0;
		return;
	}
	/* Alternate the descending even offsets and ascending odd offsets. */
	unsigned offset = phase % 2 ? 31 - phase : phase - 1;
	set_origin(ddo, offset, offset);
	++ddo->shake_phase;
	schedule_shake_frame(ddo);
}

void
st_ddofx_graphics_exit(struct st_ddofx *ddo)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	if (!g)
		return;
	st_ddofx_sleep_close(ddo);
	ddo->background_paused = false;
	(void)st_console_set_display_origin(g->display, 0, 0, 0);
	ddo->sleeper.dismissal_key = 0;
	clear_graphics_objects(g);
	st_ddofx_destroy_text_page(&g->windows[0]);
	if (g->previous_screen) {
		(void)st_console_reconfigure(g->display, &g->previous_mode);
		(void)st_console_snapshot_restore(g->display, g->previous_screen, NULL);
		st_console_snapshot_destroy(g->previous_screen);
	}
	ddo->graphics = NULL;
	free(g);
	st_ddofx_apply_vga_font(ddo);
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
st_ddofx_graphics_reset(struct st_ddofx_graphics *g)
{
	if (!g)
		return;
	clear_graphics_objects(g);
	g->text_columns = 80;
	memset(g->page, 0, sizeof(g->page));
	(void)st_console_modify_pixels(g->display, blank, NULL);
}

void
st_ddofx_graphics_enter(struct st_ddofx *ddo, const struct st_ddofx_sink *sink)
{
	if (!st_ddofx_svga(sink))
		return;
	int32_t available = sink->available_memory ? sink->available_memory() : -1;
	if (available >= 0 && available < 2 * 1024 * 1024)
		st_ddofx_unload_term_theme(ddo);
	st_ddofx_slide_restore(ddo);
	struct st_ddofx_graphics *g = calloc(1, sizeof(*g));
	if (g == NULL)
		return;
	struct st_cterm_config config = *sink->text_config;
	g->display                    = config.console;
	g->text_columns               = 80;
	g->cell_width = 8;
	g->cell_height = 16;
	st_ddofx_mouse_defaults(&g->mouse);
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
	g->animator_divisor = 1;
	g->colour                     = 10;
	g->clip                       = (struct bounds) {0, WIDTH - 1, 0, HEIGHT - 1};
	g->horizontal_thickness = g->vertical_thickness = 1;
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
	    || st_console_modify_pixels(g->display, blank, NULL) != 0) {
		st_ddofx_graphics_exit(ddo);
		return;
	}
	if (!st_ddofx_create_text_page(g, &g->windows[0], &config))
		st_ddofx_graphics_exit(ddo);
	else {
		st_ddofx_apply_vga_font(ddo);
		st_ddofx_sleep_arm(ddo);
	}
}

struct transfer {
	struct st_ddofx_graphics *g;
	enum page                 source, dest;
	int                       x, y, width, height, dx, dy;
	bool                      transparent;
};

bool
st_ddofx_page_name(const char *s, enum page *page)
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
			uint8_t index =
			    src ? src[x] : st_console_palette_index256(palette, pixels->primary[from + x]);
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
		/* HP is the source for a restoration. Only new desktop imagery
		 * replaces the hidden pixels used to repair moving objects. */
		if (t->source == HIDDEN)
			return 0;
		for (int y = t->dy; y < t->dy + t->height; ++y) {
			for (int x = t->dx; x < t->dx + t->width; ++x)
				g->page[HIDDEN][y][x] =
				    st_console_palette_index256(palette, pixels->primary[y * WIDTH + x]);
		}
	}
	return 0;
}

bool
st_ddofx_numbers(struct st_ddofx *ddo, char *args, int32_t *n, unsigned count)
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
	if (verb[6] != '2' || !st_ddofx_page_name(verb + 4, &t.source) || !st_ddofx_page_name(verb + 7, &t.dest)
	    || t.source == t.dest || (t.transparent && t.source == VISUAL))
		return;
	struct st_ddofx_graphic_window *window = &t.g->graphic_windows[t.g->alert_graphic_window];
	bool virtual_pages = window->visual && ((t.source == VISUAL && t.dest == HIDDEN)
	    || (t.source == HIDDEN && t.dest == VISUAL));
	int width = virtual_pages ? window->width : WIDTH;
	int height = virtual_pages ? window->height : HEIGHT;
	t.width = width;
	t.height = height;
	if (cut) {
		int32_t n[6];
		if (!st_ddofx_numbers(ddo, args, n, 6) || n[0] < 0 || n[1] <= n[0] || n[1] >= width || n[2] < 0
		    || n[3] <= n[2] || n[3] >= height)
			return;
		t.x      = n[0];
		t.y      = n[2];
		t.width  = n[1] - n[0] + 1;
		t.height = n[3] - n[2] + 1;
		if (n[4] < 0 || n[4] > width - t.width || n[5] < t.height - 1 || n[5] >= height)
			return;
		t.dx = n[4];
		t.dy = n[5] - (t.height - 1);
	}
	else if (args != NULL)
		return;
	if (virtual_pages) {
		const uint8_t *source = t.source == VISUAL ? window->visual : window->hidden_page;
		uint8_t *dest = t.dest == VISUAL ? window->visual : window->hidden_page;
		for (int row = 0; row < t.height; ++row) {
			const uint8_t *src = source + (t.y + row) * width + t.x;
			uint8_t *dst = dest + (t.dy + row) * width + t.dx;
			if (t.transparent) {
				for (int x = 0; x < t.width; ++x)
					if (!t.g->transparent[src[x]])
						dst[x] = src[x];
			}
			else
				memcpy(dst, src, t.width);
		}
		if (t.dest == VISUAL) {
			st_ddofx_repaint_text_windows(ddo, t.g->alert_graphic_window);
			if (t.transparent)
				st_ddofx_redraw_graphic_windows(ddo);
			else
				st_ddofx_refresh_graphic_interior(ddo);
		}
		return;
	}
	bool batch = t.dest == VISUAL && st_console_begin_update(t.g->display) == 0;
	if (t.source == VISUAL || t.dest == VISUAL)
		(void)st_console_modify_pixels_with_palette(t.g->display, copy_region, &t);
	else
		(void)copy_region(&t, NULL, NULL, NULL);
	if (t.dest == VISUAL) {
		st_ddofx_repaint_text_windows(ddo, 0);
		if (t.source != HIDDEN || t.transparent)
			st_ddofx_redraw_graphic_windows(ddo);
	}
	if (batch)
		(void)st_console_end_update(t.g->display);
}

struct scroll {
	struct st_console_rect area;
	int32_t                jump;
	uint8_t                fill;
	bool                   circular;
};

static void
scroll_rows(void *plane, size_t pixel_size, const struct scroll *s)
{
	const int    height = (int)s->area.height;
	const size_t stride = WIDTH * pixel_size, bytes = s->area.width * pixel_size;
	uint8_t     *top = (uint8_t *)plane + s->area.y * stride + s->area.x * pixel_size;
	if (s->circular) {
		int jump = s->jump % height;
		if (jump < 0)
			jump += height;
		if (jump == 0)
			return;
		/* Rotate each cycle using just one scanline of scratch space. */
		uint32_t saved[WIDTH];
		int      moved = 0;
		for (int first = 0; moved < height; ++first) {
			memcpy(saved, top + first * stride, bytes);
			int dest = first;
			for (;;) {
				int src = (dest + height - jump) % height;
				++moved;
				if (src == first)
					break;
				memcpy(top + dest * stride, top + src * stride, bytes);
				dest = src;
			}
			memcpy(top + dest * stride, saved, bytes);
		}
		return;
	}
	for (int i = 0; i < height; ++i) {
		int      dest = s->jump > 0 ? height - 1 - i : i;
		int64_t  src  = (int64_t)dest - s->jump;
		uint8_t *row  = top + dest * stride;
		if (src >= 0 && src < height)
			memcpy(row, top + (size_t)src * stride, bytes);
		else if (pixel_size == 1)
			memset(row, s->fill, bytes);
		else {
			uint32_t *native = (uint32_t *)(void *)row;
			for (unsigned x = 0; x < s->area.width; ++x)
				native[x] = s->fill;
		}
	}
}

static int
scroll_visible(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	const struct scroll *s = context;
	if (pixels->width == WIDTH && pixels->height == HEIGHT) {
		scroll_rows(pixels->primary, sizeof(*pixels->primary), s);
		scroll_rows(pixels->alternate, sizeof(*pixels->alternate), s);
		*changed = s->area;
	}
	return 0;
}

static void
scroll_page(struct st_ddofx *ddo, char *args)
{
	int32_t   n[6];
	enum page page;
	for (unsigned i = 0; i < 6; ++i)
		if (!st_ddofx_field_number(ddo, &args, &n[i]))
			return;
	if (!args)
		return;
	const char *name = st_ddofx_value(ddo, args);
	if (strlen(name) != 2 || !st_ddofx_page_name(name, &page) || n[0] < 0 || n[1] < n[0] || n[1] >= WIDTH || n[2] < 0
	    || n[3] < n[2] || n[3] >= HEIGHT || n[4] == 0 || (n[5] != 0 && n[5] != 1))
		return;
	struct st_ddofx_graphics *g = ddo->graphics;
	struct scroll             s = {
	    .area     = {n[0], n[2], n[1] - n[0] + 1, n[3] - n[2] + 1},
	    .jump     = n[4],
	    .fill     = g->colour,
	    .circular = n[5] == 0
        };
	if (page == VISUAL)
		(void)st_console_modify_pixels(g->display, scroll_visible, &s);
	else
		scroll_rows(&g->page[page], 1, &s);
}

static uint32_t
dac(int32_t value)
{
	return (uint32_t)((value << 2) | (value >> 4));
}

void
st_ddofx_set_palette(struct st_ddofx_graphics *g, unsigned first, unsigned count)
{
	(void)st_console_set_palette(g->display, (uint16_t)first, g->palette + first, count);
	for (unsigned i = 0; i <= 10; ++i)
		if (g->windows[i].view)
			(void)st_console_set_palette(g->windows[i].view, (uint16_t)first, g->palette + first,
			    count);
}

/* A fade owns the foreground until it reaches the target. Local input and
 * music playback continue between palette updates. */
bool
st_ddofx_fade_step(struct st_ddofx *ddo, uint64_t now)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	if (!g && ddo->lite_fade)
		return st_ddofx_lite_fade_step(ddo, now);
	if (!g || g->fade == FADE_NONE)
		return false;

	enum {
		INTERVAL_MS = 16
	};

	uint64_t elapsed = (now - ddo->fade_due) / INTERVAL_MS;
	int      change  = elapsed >= 31 ? 64 : 2 * (1 + (int)elapsed);
	bool     done    = true;
	for (unsigned i = g->fade_first; i <= g->fade_last; ++i) {
		uint32_t target = g->fade == FADE_IN ? ddo->saved_palette[i] : g->fade_target;
		uint32_t colour = 0;
		for (unsigned shift = 0; shift < 24; shift += 8) {
			int current = (int)((g->palette[i] >> shift) & 255) >> 2;
			int end     = (int)((target >> shift) & 255) >> 2;
			if (current + change < end)
				current += change;
			else if (g->fade != FADE_IN && current - change > end)
				current -= change;
			else
				current = end;
			if (current == end)
				colour |= target & (UINT32_C(255) << shift);
			else {
				done = false;
				colour |= dac(current) << shift;
			}
		}
		g->palette[i] = colour;
	}
	st_ddofx_set_palette(g, g->fade_first, g->fade_last + 1 - g->fade_first);
	if (!done) {
		ddo->fade_due += (1 + elapsed) * INTERVAL_MS;
		return true;
	}
	if (g->fade == FADE_OUT) {
		memset(g->page[HIDDEN], 0, sizeof(g->page[HIDDEN]));
		(void)st_console_modify_pixels(g->display, blank, NULL);
	}
	g->fade = FADE_NONE;
	ddo->fade_due = 0;
	return false;
}

static void
start_fade(struct st_ddofx *ddo, const char *op, char *args)
{
	struct st_ddofx_graphics *g     = ddo->graphics;
	unsigned                  first = 0, last = 255;
	uint32_t                  target = 0;
	if (!ascii_case_compare(op, "FPALOUTA")) {
		int32_t n[3];
		if (!st_ddofx_numbers(ddo, args, n, 3) || (unsigned)n[0] > 255 || (unsigned)n[1] > 255
		    || (unsigned)n[2] > 255 || n[1] > n[2])
			return;
		first   = (unsigned)n[1];
		last    = (unsigned)n[2];
		target  = g->palette[n[0]];
		g->fade = FADE_RANGE;
	}
	else if (!args)
		g->fade = !ascii_case_compare(op, "FPALIN") ? FADE_IN : FADE_OUT;
	else
		return;
	if (g->fade != FADE_IN)
		memcpy(ddo->saved_palette, g->palette, sizeof(g->palette));
	g->fade_first  = first;
	g->fade_last   = last;
	g->fade_target = target;
	uint64_t now;
	if (st_time_monotonic_ms(&now) != 0)
		now = UINT64_MAX;
	ddo->fade_due = now == UINT64_MAX ? 0 : now;
	(void)st_ddofx_fade_step(ddo, now);
}

void
st_ddofx_graphics_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *body)
{
	struct st_ddofx_graphics *g = ddo->graphics;
	if (body[1] == 'b' || body[1] == 'w' || body[1] == 'v') {
		enum page dest = body[1] == 'b' ? BACKGROUND : body[1] == 'w' ? WORK : VISUAL;
		st_ddofx_resource_pcx(ddo, sink, dest, body + 2, 0, 0);
		return;
	}
	char       *args = body;
	const char *verb = st_ddofx_field(&args);
	int32_t     n[4];
	if (st_ddofx_icon_command(ddo, sink, verb, args))
		return;
	if (st_ddofx_graphic_window_command(ddo, sink, verb, args))
		return;
	if (!ascii_case_ncompare(verb, "23D", 3))
		st_ddofx_polygon_command(ddo, sink, verb + 3, args);
	else if (!ascii_case_compare(verb, "2GEXIT") && args == NULL)
		st_ddofx_graphics_exit(ddo);
	else if (!ascii_case_compare(verb, "2aCHORG") && st_ddofx_numbers(ddo, args, n, 2))
		set_origin(ddo, n[0] > 0 ? (uint32_t)n[0] : 0, n[1] > 0 ? (uint32_t)n[1] : 0);
	else if (!ascii_case_compare(verb, "2aSHAKE") && !args) {
		set_origin(ddo, 0, 0);
		ddo->shake_phase = 1;
		schedule_shake_frame(ddo);
	}
	else if (!ascii_case_compare(verb, "2RESA") && st_ddofx_numbers(ddo, args, n, 1) && n[0] >= 1 && n[0] <= 100) {
		st_ddofx_movie_resolution(g, (unsigned)n[0]);
		st_ddofx_animation_resolution(g);
	}
	else if (!ascii_case_ncompare(verb, "2XT", 3))
		st_ddofx_print_vga_text(ddo, verb + 3, args);
	else if (!strcmp(verb, "2F") || (verb[1] == 'F' && !ascii_case_compare(verb + 2, "SELECT"))
	         || !strcmp(verb, "2X"))
		st_ddofx_bitmap_text(ddo, sink, verb, args);
	else if (strlen(verb) == 7 && !args && !ascii_case_compare(verb + 3, "2PCX")) {
		enum page page;
		if (st_ddofx_page_name(verb + 1, &page) && page != HIDDEN)
			st_ddofx_page_file(ddo, page, page, true);
	}
	else if (strlen(verb) == 7 && ascii_case_ncompare(verb, "2L", 2) == 0
	         && !ascii_case_compare(verb + 4, "PCX") && args && strlen(args) == 2) {
		enum page file, dest;
		if (st_ddofx_page_name(verb + 2, &file) && st_ddofx_page_name(args, &dest) && file != HIDDEN && dest != HIDDEN)
			st_ddofx_page_file(ddo, file, dest, false);
	}
	else if (!ascii_case_ncompare(verb, "2CPY", 4))
		copy_page(ddo, verb, args, false);
	else if (!ascii_case_ncompare(verb, "2CUT", 4))
		copy_page(ddo, verb, args, true);
	else if (!ascii_case_compare(verb, "2CCOL") && st_ddofx_numbers(ddo, args, n, 2)
	         && (unsigned)n[0] <= 255) {
		if (n[1] == 0 || n[1] == 1)
			g->transparent[n[0]] = n[1] == 1;
		else if (n[0] == 0 && (n[1] == 3 || n[1] == 4))
			for (unsigned i = 0; i < 256; ++i)
				g->transparent[i] = n[1] == 3;
	}
	else if (!ascii_case_compare(verb, "2C") && st_ddofx_numbers(ddo, args, n, 1) && (unsigned)n[0] <= 255)
		g->colour = (uint8_t)n[0];
	else if (st_ddofx_animation_command(ddo, verb, args))
		return;
	else if (!strcmp(verb, "2SS") || !strcmp(verb, "2SSA") || !strcmp(verb, "2CSS")
	    || !strcmp(verb, "2ACSS") || !strcmp(verb, "2DSS"))
		st_ddofx_simple_command(ddo, verb, args);
	else if (!ascii_case_compare(verb, "2STORES") || !ascii_case_compare(verb, "2LOADS"))
		st_ddofx_sprite_bank(ddo, sink, args, ascii_to_upper((unsigned char)verb[1]) == 'L');
	else if (verb[1] == 'S' || verb[1] == 's')
		st_ddofx_sprite_command(ddo, verb, args);
	else if (verb[1] == 'a') {
		const char *op = verb + 2;
		if (!ascii_case_compare(op, "SCROLL"))
			scroll_page(ddo, args);
		else if (!ascii_case_compare(op, "SRGB") && st_ddofx_numbers(ddo, args, n, 4)
		         && (unsigned)n[0] < 256 && (unsigned)n[1] < 64 && (unsigned)n[2] < 64
		         && (unsigned)n[3] < 64) {
			g->palette[n[0]] = dac(n[1]) << 16 | dac(n[2]) << 8 | dac(n[3]);
			st_ddofx_set_palette(g, (unsigned)n[0], 1);
		}
		else if (!ascii_case_compare(op, "GRGB") && st_ddofx_numbers(ddo, args, n, 1)
		         && (unsigned)n[0] < 256) {
			uint32_t rgb = g->palette[n[0]];
			char     reply[12];
			snprintf(reply, sizeof(reply), "%u,%u,%u", (unsigned)(rgb >> 18),
			    (unsigned)((rgb >> 10) & 63), (unsigned)((rgb >> 2) & 63));
			st_ddofx_query_reply(ddo, sink, reply, false);
		}
		else if (!ascii_case_compare(op, "FPALOUT") || !ascii_case_compare(op, "FPALOUTA")
		         || !ascii_case_compare(op, "FPALIN"))
			start_fade(ddo, op, args);
		else if (args == NULL
		         && (!ascii_case_compare(op, "STOREPAL") || !ascii_case_compare(op, "RESTOREPAL")
		             || !ascii_case_compare(op, "ZEROPAL") || !ascii_case_compare(op, "DEFPAL"))) {
			if (!ascii_case_compare(op, "STOREPAL"))
				memcpy(ddo->saved_palette, g->palette, sizeof(g->palette));
			else if (!ascii_case_compare(op, "RESTOREPAL")) {
				memcpy(g->palette, ddo->saved_palette, sizeof(g->palette));
				st_ddofx_set_palette(g, 0, 256);
			}
			else if (!ascii_case_compare(op, "ZEROPAL")) {
				memset(g->palette, 0, sizeof(g->palette));
				st_ddofx_set_palette(g, 0, 256);
			}
			else if (!ascii_case_compare(op, "DEFPAL")) {
				memcpy(g->palette, text_colours, sizeof(text_colours));
				st_ddofx_set_palette(g, 0, 16);
			}
		}
		else
			st_ddofx_draw(ddo, sink, op, args);
	}
}
