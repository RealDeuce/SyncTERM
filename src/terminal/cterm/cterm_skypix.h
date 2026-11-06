/* Per-terminal SkyPix graphics, text, font, and timing state. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_SKYPIX_H_
#define CTERM_SKYPIX_H_
#include "cterm_skypix_parser.h"
#include "st_amiga_font.h"
#include "st_codepage.h"
#include "st_console.h"
#include "st_pixels.h"
#include "st_unicode_text.h"
enum { CTERM_SKYPIX_PALETTE_FIRST = 512, CTERM_SKYPIX_GADGET_COUNT = 20 };
struct cterminal;
struct cterm_skypix {
	struct cterminal *terminal;
	struct cterm_skypix_parser parser;
	const st_amiga_font_t *font, *default_data;
	uint32_t width, height, cell_width, cell_height;
	enum st_codepage font_codepage;
	bool native_default;
	const char *font_directory;
	struct st_pixel_buffer *brush, *undo;
	struct st_console_rect undo_rect, pending_rect, undo_pending_rect;
	uint8_t *pending, *undo_pending;
	int x, y, pen_x, pen_y, undo_x, undo_y;
	int text_forecast;
	size_t text_count;
	unsigned colours, brush_depth;
	bool custom, positioned, receiving, kick13, presented;
	uint64_t delay_until, cursor_deadline;
	uint32_t cursor_colour;
	bool cursor_on, cursor_hidden, cursor_blink;
	uint16_t default_font;
	uint16_t gadgets[CTERM_SKYPIX_GADGET_COUNT][5];
	char warned[1024];
};
struct st_cterm_config;
int cterm_skypix_create(struct cterminal *, const struct st_cterm_config *);
void cterm_skypix_destroy(struct cterm_skypix *);
int cterm_skypix_feed(struct cterm_skypix *, const uint8_t *, size_t, size_t *);
int cterm_skypix_finish(struct cterm_skypix *);
int cterm_skypix_reset(struct cterm_skypix *);
int cterm_skypix_reset_font(struct cterm_skypix *);
int cterm_skypix_reset_style(struct cterm_skypix *);
bool cterm_skypix_paused(void *);
void cterm_skypix_cursor_mode(struct cterm_skypix *, bool hidden, bool blink);
void cterm_skypix_home(struct cterm_skypix *);
int cterm_skypix_poll(struct cterm_skypix *, uint64_t now);
int cterm_skypix_present(struct cterm_skypix *, const struct st_unicode_presentation_event *,
						 bool *);
bool cterm_skypix_control(struct cterm_skypix *, uint8_t);
bool cterm_skypix_erase_line(struct cterm_skypix *, unsigned);
int cterm_skypix_receive(struct cterm_skypix *, const void *, size_t);
#endif
