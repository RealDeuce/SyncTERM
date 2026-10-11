/* CTerm-private rendering support for modern console components. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef CTERM_RENDER_H_
#define CTERM_RENDER_H_

#include "audio.h"
#include "st_audio_runtime.h"
#include "st_codepage.h"
#include "st_console.h"
#include "st_hyperlink.h"
#include "st_unicode_text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define CTERM_FREE_AND_CLEAR(value) do { free(value); (value) = NULL; } while (0)

enum {
	CTERM_COLOR_BLACK, CTERM_COLOR_BLUE, CTERM_COLOR_GREEN, CTERM_COLOR_CYAN, CTERM_COLOR_RED, CTERM_COLOR_MAGENTA, CTERM_COLOR_BROWN, CTERM_COLOR_LIGHT_GRAY,
	CTERM_COLOR_DARK_GRAY, CTERM_COLOR_LIGHT_BLUE, CTERM_COLOR_LIGHT_GREEN, CTERM_COLOR_LIGHT_CYAN, CTERM_COLOR_LIGHT_RED,
	CTERM_COLOR_LIGHT_MAGENTA, CTERM_COLOR_YELLOW, CTERM_COLOR_WHITE
};

#define CTERM_ATTR_BLINK 128
#define CTERM_CURSOR_HIDDEN 0
#define CTERM_CURSOR_BLOCK 1
#define CTERM_CURSOR_UNDERLINE 2
#define CTERM_MODE_IBM 3
#define CTERM_MODE_C64_40 64
#define CTERM_MODE_C128_40 65
#define CTERM_MODE_C128_80 66

#define CTERM_KEY_DOS_HOME        (0x47 << 8)
#define CTERM_KEY_DOS_UP          (0x48 << 8)
#define CTERM_KEY_DOS_END         (0x4f << 8)
#define CTERM_KEY_DOS_DOWN        (0x50 << 8)
#define CTERM_KEY_DOS_F(x)        (((x) < 11) ? ((0x3a + (x)) << 8) : ((0x7a + (x)) << 8))
#define CTERM_KEY_DOS_IC          (0x52 << 8)
#define CTERM_KEY_DOS_DC          (0x53 << 8)
#define CTERM_KEY_DOS_LEFT        (0x4b << 8)
#define CTERM_KEY_DOS_RIGHT       (0x4d << 8)
#define CTERM_KEY_DOS_PPAGE       (0x49 << 8)
#define CTERM_KEY_DOS_NPAGE       (0x51 << 8)
#define CTERM_KEY_DOS_SHIFT_F(x)  (((x) < 11) ? ((0x53 + (x)) << 8) : ((0x7c + (x)) << 8))
#define CTERM_KEY_DOS_CTRL_F(x)   (((x) < 11) ? ((0x5d + (x)) << 8) : ((0x7e + (x)) << 8))
#define CTERM_KEY_DOS_ALT_F(x)    (((x) < 11) ? ((0x67 + (x)) << 8) : ((0x80 + (x)) << 8))
#define CTERM_KEY_DOS_BACKTAB     (0x0f << 8)
#define CTERM_KEY_DOS_SHIFT_UP    0x91e0
#define CTERM_KEY_DOS_CTRL_UP     (0x8d << 8)
#define CTERM_KEY_DOS_SHIFT_LEFT  0x93e0
#define CTERM_KEY_DOS_CTRL_LEFT   (0x73 << 8)
#define CTERM_KEY_DOS_SHIFT_RIGHT 0x94e0
#define CTERM_KEY_DOS_CTRL_RIGHT  (0x74 << 8)
#define CTERM_KEY_DOS_SHIFT_DOWN  0x96e0
#define CTERM_KEY_DOS_CTRL_DOWN   (0x91 << 8)
#define CTERM_KEY_DOS_SHIFT_END   0x95e0
#define CTERM_KEY_DOS_CTRL_END    (0x75 << 8)

#define CTERM_FIRST_DYNAMIC_FONT 46
#define CTERM_PALETTE_SIZE 282

struct cterm_render_state {
	st_console_t *console;
	st_audio_runtime_t *audio;
	st_hyperlink_registry_t *hyperlinks;
	enum st_codepage codepage;
	struct st_console_font_selection fonts;
	uint16_t font_identifiers[UINT8_MAX + 1U];
	bool font_valid[UINT8_MAX + 1U];
	uint16_t selectors[5];
	uint16_t current_hyperlink;
	uint32_t mode_palette[16];
	uint32_t capabilities;
	int mode;
	uint8_t default_attribute;
	int hold_update;
	int wscroll;
	int error;
	bool prestel_reveal;
	bool hyperlink_root_registered;
	st_unicode_presentation_t *unicode;
	uint32_t provisional_x;
	uint32_t provisional_y;
};

struct cterminal;

int cterm_render_make_cell(struct cterminal *cterm,
	struct st_console_cell *cell, uint8_t byte, uint8_t attribute,
	uint32_t foreground, uint32_t background, uint8_t font_selector,
	uint16_t hyperlink, uint32_t flags);
int cterm_render_make_current_cell(struct cterminal *cterm,
	struct st_console_cell *cell, uint8_t byte, uint16_t hyperlink);
uint8_t cterm_render_cell_byte(struct cterminal *cterm,
	const struct st_console_cell *cell);
uint8_t cterm_render_cell_attribute(struct cterminal *cterm,
	const struct st_console_cell *cell);
uint8_t cterm_render_cell_font(struct cterminal *cterm,
	const struct st_console_cell *cell);

int cterm_render_get_video_flags(struct cterminal *cterm);
void cterm_render_set_video_flags(struct cterminal *cterm, int flags);
void cterm_render_set_window(struct cterminal *cterm, int left, int top,
	int right, int bottom);
void cterm_render_set_position(struct cterminal *cterm, int x, int y);
int cterm_render_position_x(struct cterminal *cterm);
int cterm_render_position_y(struct cterminal *cterm);
void cterm_render_set_attribute(struct cterminal *cterm, int attribute);
void cterm_render_set_colours(struct cterminal *cterm, uint32_t foreground,
	uint32_t background);
void cterm_render_set_cursor(struct cterminal *cterm, int cursor);
bool cterm_render_get_custom_cursor(struct cterminal *cterm,
	struct st_console_custom_cursor *cursor);
bool cterm_render_set_custom_cursor(struct cterminal *cterm,
	const struct st_console_custom_cursor *cursor);
int cterm_render_write_byte(struct cterminal *cterm, int value);
int cterm_render_write_string(struct cterminal *cterm, const char *text);
void cterm_render_clear_window(struct cterminal *cterm);
void cterm_render_clear_line(struct cterminal *cterm);
int cterm_render_write_cells(struct cterminal *cterm, int left, int top,
	int right, int bottom, const struct st_console_cell *cells);
int cterm_render_read_cells(struct cterminal *cterm, int left, int top,
	int right, int bottom, struct st_console_cell *cells);
int cterm_render_move_cells(struct cterminal *cterm, int left, int top,
	int right, int bottom, int destination_left, int destination_top);
int cterm_render_move_cells_clear(struct cterminal *cterm, int left,
	int top, int right, int bottom, int destination_left,
	int destination_top, const struct st_console_cell *fill);
int cterm_render_blit_pixels(struct cterminal *cterm,
	const struct st_pixel_buffer *pixels, const struct st_pixel_mask *mask,
	const struct st_pixel_blit *blit);
int cterm_render_set_palette(struct cterminal *cterm, uint32_t entry,
	uint16_t red, uint16_t green, uint16_t blue);
int cterm_render_get_palette(struct cterminal *cterm, uint32_t entry,
	uint8_t *red, uint8_t *green, uint8_t *blue);
uint32_t cterm_render_map_rgb(uint16_t red, uint16_t green,
	uint16_t blue);
void cterm_render_replace_font(struct cterminal *cterm, uint8_t selector,
	char *name, void *data, size_t size);
int cterm_render_get_modepalette(struct cterminal *cterm,
	uint32_t *palette);
int cterm_render_font_selector(struct cterminal *cterm, int slot);
int cterm_render_set_font_selector(struct cterminal *cterm, int selector,
	int force, int slot);
uint8_t cterm_render_attribute_font(struct cterminal *cterm, uint8_t attribute);
void cterm_render_attribute_colours(struct cterminal *cterm,
	uint8_t attribute, uint32_t *foreground, uint32_t *background);
int cterm_render_set_modepalette(struct cterminal *cterm,
	const uint32_t *palette);
int cterm_render_set_hold_update(struct cterminal *cterm, int hold);
int cterm_render_hold_update(const struct cterminal *cterm);
int cterm_render_default_palette(uint32_t entry, uint8_t *red,
	uint8_t *green, uint8_t *blue);
void cterm_render_record_error(struct cterminal *cterm, int error);
int cterm_render_take_error(struct cterminal *cterm);
uint16_t cterm_render_add_hyperlink(struct cterminal *cterm,
	const char *uri, const char *parameter);
char *cterm_render_hyperlink_url(struct cterminal *cterm,
	uint16_t identifier);
char *cterm_render_hyperlink_params(struct cterminal *cterm,
	uint16_t identifier);
void cterm_render_set_hyperlink(struct cterminal *cterm,
	uint16_t identifier);
uint16_t cterm_render_get_hyperlink(struct cterminal *cterm);
uint64_t cterm_render_timer64(void);
uint16_t cterm_render_crc16(const void *data, size_t size,
	uint16_t initial);
int cterm_render_base64_decode(void *destination, size_t capacity,
	const char *source);
int cterm_render_asprintf(char **result, const char *format, ...);
int cterm_render_casecmp_n(const char *left, const char *right,
	size_t count);
void cterm_render_set_prestel_reveal(struct cterminal *cterm, bool reveal);

#endif
