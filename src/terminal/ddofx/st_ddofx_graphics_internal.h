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
	int32_t                  cx, cy;
	struct bounds            clip;
	int                      horizontal_thickness, vertical_thickness;
};

bool st_ddofx_numbers(struct st_ddofx *, char *, int32_t *, unsigned);
void st_ddofx_draw(struct st_ddofx *, const struct st_ddofx_sink *, const char *, char *);
#endif
