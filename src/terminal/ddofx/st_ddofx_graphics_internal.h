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
	uint16_t width, height;
	uint8_t *pixels;
};

struct st_ddofx_graphics {
	struct st_ddofx_sprite  sprites[SPRITE_COUNT];
	struct st_ddofx_simple *simple;

	st_console_t            *display, *view;
	st_font_catalog_t       *fonts;
	st_cterm_t              *text;
	struct st_console_config previous_mode;
	st_console_snapshot_t   *previous_screen;
	uint32_t                 palette[256];
	uint8_t                  page[3][HEIGHT][WIDTH];
	bool                     transparent[256];
	uint8_t                  colour;
	int32_t                  cx, cy;
	struct bounds            clip;
	int                      horizontal_thickness, vertical_thickness;
};

void    st_ddofx_set_palette(struct st_ddofx_graphics *, unsigned, unsigned);
void    st_ddofx_resource_pcx(struct st_ddofx *, const struct st_ddofx_sink *, enum page, const char *);
void    st_ddofx_page_file(struct st_ddofx *, enum page file, enum page target, bool save);
uint8_t st_ddofx_pixel_index(const uint32_t *, uint32_t);
void    st_ddofx_free_sprites(struct st_ddofx_graphics *);
int     st_ddofx_sprite_slot(struct st_ddofx_graphics *, const char *, bool create);
void    st_ddofx_draw_sprite(struct st_ddofx_graphics *, const struct st_ddofx_sprite *, enum page, char op,
    int32_t x, int32_t y);
void    st_ddofx_simple_command(struct st_ddofx *, const char *, char *);
void    st_ddofx_sprite_command(struct st_ddofx *, const char *, char *);
bool    st_ddofx_numbers(struct st_ddofx *, char *, int32_t *, unsigned);
void    st_ddofx_draw(struct st_ddofx *, const struct st_ddofx_sink *, const char *, char *);
#endif
