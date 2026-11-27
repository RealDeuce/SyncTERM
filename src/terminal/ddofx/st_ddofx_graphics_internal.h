/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_DDOFX_GRAPHICS_INTERNAL_H_
#define ST_DDOFX_GRAPHICS_INTERNAL_H_

#include "st_ddofx_graphics.h"

struct bounds {
	int left, right, top, bottom;
};

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

enum {
	SPRITE_COUNT = 3200
};

struct st_ddofx_sprite {
	char     name[9];
	uint16_t width, height, allocation;
	uint8_t  active;
	uint8_t *pixels;
};

struct st_ddofx_mouse_scan {
	unsigned next, buttons, stage;
	int      x, y;
	bool     waiting, repeating;
	uint64_t repeat_due;
};

struct st_ddofx_mouse_state {
	struct st_ddofx_mouse_scan scan;
	struct st_ddofx_fields *fields;
	struct st_ddofx_regions *regions;
	struct st_ddofx_pointers *pointers;
	int x, y;
	struct bounds limits;
	int rate_x, rate_y, fraction_x, fraction_y;
	int host_x, host_y;
	bool controlled, host_known, keep_position, outside;
	unsigned buttons;
	bool hidden, ignore_hold, visibility_changed, previous_visibility;
	struct {
		unsigned count;
		int x, y;
	} history[3][2];
};

struct st_ddofx_graphics {
	struct st_ddofx_buttons    *buttons;
	struct st_ddofx_text_fields *text_fields;
	struct st_ddofx_choices    *choices[2];
	struct st_ddofx_mouse_state mouse;
	struct st_ddofx_sprite  sprites[SPRITE_COUNT];
	struct st_ddofx_bmf    *bitmap_fonts[32];
	unsigned                bitmap_slot;
	struct st_ddofx_simple *simple;
	struct st_ddofx_bars   *bars;
	struct st_ddofx_grid   *grid;
	struct st_ddofx_marquees *marquees;
	struct st_ddofx_clocks *clocks;
	struct st_ddofx_movies *movies;
	unsigned animator_divisor;

	st_console_t            *display, *view;
	st_font_catalog_t       *fonts;
	st_cterm_t              *text;
	struct st_console_config previous_mode;
	st_console_snapshot_t   *previous_screen;
	uint32_t                 palette[256];

	enum {
		FADE_NONE,
		FADE_OUT,
		FADE_RANGE,
		FADE_IN
	} fade;

	unsigned      fade_first, fade_last;
	uint32_t      fade_target;
	uint8_t       page[3][HEIGHT][WIDTH];
	bool          transparent[256];
	uint8_t       colour;
	int32_t       cx, cy;
	struct bounds clip;
	int           horizontal_thickness, vertical_thickness;
};

struct st_ddofx_sprite *st_ddofx_find_sprite(struct st_ddofx_graphics *, const char *);
bool                    st_ddofx_sprite_fits(const struct st_ddofx_sprite *, int32_t x, int32_t y);
void    st_ddofx_flood(uint8_t *, int stride, const struct bounds *, int32_t x, int32_t y, uint8_t colour);
void st_ddofx_clear_text_fields(struct st_ddofx_graphics *);
bool st_ddofx_text_field_command(struct st_ddofx *, const struct st_ddofx_sink *, const char *, char *);
bool st_ddofx_text_field_mouse(struct st_ddofx *, int x, int y);
bool    st_ddofx_choice_command(struct st_ddofx *, const char *verb, char *args);
void st_ddofx_field_pointer(struct st_ddofx *, unsigned slot, bool animated);
void    st_ddofx_define_button(struct st_ddofx *, char *args);
bool    st_ddofx_button_mouse(struct st_ddofx *, unsigned, int x, int y, unsigned buttons);
void st_ddofx_pointer_command(struct st_ddofx *, const struct st_ddofx_sink *, const char *, char *);
void st_ddofx_mouse_defaults(struct st_ddofx_mouse_state *);
bool st_ddofx_ensure_pointer(struct st_ddofx_mouse_state *);
void st_ddofx_pointer_refresh(struct st_ddofx *);
const uint8_t *st_ddofx_vga_bitmap(const struct st_ddofx *);
bool st_ddofx_read_vga_font(const struct st_ddofx_sink *, const char *, uint8_t bitmap[4096]);
void st_ddofx_paint_vga_string(struct st_ddofx_graphics *g, const struct st_pixel_buffer *, const uint8_t *bitmap, const uint8_t *text,
    unsigned length, int x, int top, unsigned ink, unsigned paper, const struct bounds *clip);
void st_ddofx_repaint_clocks(struct st_ddofx *, const struct st_pixel_buffer *, const struct st_console_rect *);
void st_ddofx_clear_movies(struct st_ddofx_graphics *);
bool st_ddofx_movie_command(struct st_ddofx *, const struct st_ddofx_sink *, const char *, char *);
void st_ddofx_movie_resolution(struct st_ddofx_graphics *, unsigned);
void st_ddofx_repaint_marquees(struct st_ddofx *, const struct st_pixel_buffer *, const struct st_console_rect *);
void st_ddofx_clear_clocks(struct st_ddofx_graphics *);
bool st_ddofx_clock_command(struct st_ddofx *, const struct st_ddofx_sink *, const char *, char *);
void st_ddofx_clear_marquees(struct st_ddofx_graphics *);
bool st_ddofx_marquee_command(struct st_ddofx *, const struct st_ddofx_sink *, const char *, char *);
void st_ddofx_print_vga_text(struct st_ddofx *, const char *, char *);
void st_ddofx_apply_vga_font(struct st_ddofx *);
void st_ddofx_grid_command(struct st_ddofx *, const char *, char *);
void st_ddofx_rotation(int angle, double *cosine, double *sine);
void st_ddofx_grid_destroy(struct st_ddofx_graphics *, bool erase);
bool    st_ddofx_page_name(const char *, enum page *);
void    st_ddofx_bitmap_text(struct st_ddofx *, const struct st_ddofx_sink *, const char *, char *);
void    st_ddofx_set_palette(struct st_ddofx_graphics *, unsigned, unsigned);
void    st_ddofx_resource_pcx(struct st_ddofx *, const struct st_ddofx_sink *, enum page, const char *);
void    st_ddofx_page_file(struct st_ddofx *, enum page file, enum page target, bool save);
void st_ddofx_sprite_bank(struct st_ddofx *, const struct st_ddofx_sink *, char *args, bool load);
void st_ddofx_invert_sprite(struct st_ddofx_sprite *);
void    st_ddofx_free_sprites(struct st_ddofx_graphics *);
int     st_ddofx_sprite_slot(struct st_ddofx_graphics *, const char *, bool create);
void    st_ddofx_draw_sprite(struct st_ddofx_graphics *, const struct st_ddofx_sprite *, enum page, char op,
    int32_t x, int32_t y);
void    st_ddofx_simple_command(struct st_ddofx *, const char *, char *);
void    st_ddofx_sprite_command(struct st_ddofx *, const char *, char *);
bool    st_ddofx_numbers(struct st_ddofx *, char *, int32_t *, unsigned);
void    st_ddofx_draw(struct st_ddofx *, const struct st_ddofx_sink *, const char *, char *);
#endif
