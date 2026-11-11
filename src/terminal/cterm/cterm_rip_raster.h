/* RIP's per-terminal BGI-compatible pixel drawing state. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef CTERM_RIP_RASTER_H_
#define CTERM_RIP_RASTER_H_

#include "st_console.h"
#include "st_codepage.h"

#include <stdbool.h>
#include <stdint.h>

struct cterm_rip_button_style {
	int width;
	int height;
	int orientation;
	int flags;
	int bevel_size;
	int foreground;
	int shadow_label;
	int highlight;
	int shadow;
	int surface;
	int group;
	int flags2;
	int underline;
	int corner;
	int type;
};

struct cterm_rip_button_draw {
	struct cterm_rip_button_style style;
	int x1;
	int y1;
	int x2;
	int y2;
	int hotkey;
	int font_number;
	int font_size;
	int font_vertical;
	const char *icon;
	const char *label;
};

struct cterm_rip_saved_screen {
	struct st_pixel_buffer *pixels;
	int sx;
	int sy;
	int ex;
	int ey;
};

struct cterm_rip_saved_clipboard {
	struct st_pixel_buffer *pixels;
	int x;
	int y;
};

struct cterm_rip_raster {
	st_console_t *console;
	const char *rip_directory;
	/* RIP fonts remain byte-indexed even when command text is UTF-8. */
	enum st_codepage text_codepage;
	bool unicode_text;
	struct st_pixel_buffer *clipboard;
	struct cterm_rip_button_style last_button_style;
	int last_button_x2;
	int last_button_y2;
	int clipx;
	int clipy;
	struct cterm_rip_saved_clipboard saved_clipboard;
	struct cterm_rip_saved_screen screen_saves[11];
	uint32_t screen_width;
	uint32_t screen_height;
	int x_dim;
	int y_dim;
	int x_max;
	int y_max;
	int x;
	int y;
	struct {
		int sx;
		int sy;
		int ex;
		int ey;
	} viewport;
	int colour;
	bool xor_mode;
	uint16_t line_pattern;
	int line_width;
	int line_style;
	bool borders;
	int version;
	int font_number;
	int font_vertical;
	int font_size;
	struct {
		int sx;
		int sy;
		int ex;
		int ey;
		int xpos;
		int ypos;
		int line_height;
	} text_region;
	uint8_t fill_pattern[8];
	int fill_colour;
	int fill_style;
	/* Fixed BGI-to-console index mapping; palette commands change slot RGB. */
	uint32_t palette[16];
	uint8_t master_palette[16];
	struct cterm_rip_button_style button_style;
	/* Active only while rasterizing a filled poly-Bézier. */
	uint8_t *poly_fill_marks;
	/* Borrowed only inside a console raster edit; never owned or retained. */
	const struct st_pixel_buffer *drawing_pixels;
	struct st_console_rect *drawing_damage;
	int error;
};

int cterm_rip_raster_init(struct cterm_rip_raster *raster,
    st_console_t *console);
void cterm_rip_raster_destroy(struct cterm_rip_raster *raster);
int cterm_rip_raster_reset_windows(struct cterm_rip_raster *raster);
int cterm_rip_raster_save_screen(struct cterm_rip_raster *raster, int slot);
int cterm_rip_raster_restore_screen(struct cterm_rip_raster *raster,
	int slot, bool clear_after);
int cterm_rip_raster_save_clipboard(struct cterm_rip_raster *raster);
int cterm_rip_raster_restore_clipboard(struct cterm_rip_raster *raster);
int cterm_rip_raster_paste_clipboard(struct cterm_rip_raster *raster);
int cterm_rip_raster_map_x(const struct cterm_rip_raster *raster, int x);
int cterm_rip_raster_map_y(const struct cterm_rip_raster *raster, int y);
int cterm_rip_raster_unmap_x(const struct cterm_rip_raster *raster, int x);
int cterm_rip_raster_unmap_y(const struct cterm_rip_raster *raster, int y);
void cterm_rip_raster_line(struct cterm_rip_raster *raster,
    int x1, int y1, int x2, int y2);
void cterm_rip_raster_set_line(struct cterm_rip_raster *raster,
    int x1, int y1, int x2, int y2, uint32_t colour,
    uint16_t pattern, int width);
void cterm_rip_raster_fill_style(struct cterm_rip_raster *raster,
    int pattern, int colour);
void cterm_rip_raster_bar(struct cterm_rip_raster *raster,
    int x1, int y1, int x2, int y2);
void cterm_rip_raster_rectangle(struct cterm_rip_raster *raster,
    int x1, int y1, int x2, int y2);
int cterm_rip_raster_invert_rectangle(struct cterm_rip_raster *raster,
    int x1, int y1, int x2, int y2);
int cterm_rip_raster_draw_button(struct cterm_rip_raster *raster,
	struct cterm_rip_button_draw *button, bool inverted);
/* Raster commands; terminal and host-interaction commands remain with the host. */
int cterm_rip_raster_command(struct cterm_rip_raster *raster,
    int level, int sublevel, uint8_t code, const char *arguments,
    bool *handled);

#endif
