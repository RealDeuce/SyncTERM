/* Per-terminal SkyPix graphics, text, font, and timing state. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_SKYPIX_H_
#define CTERM_SKYPIX_H_
#include "cterm_skypix_parser.h"
#include "st_amiga_font.h"
#include "st_console.h"
#include "st_pixels.h"
#include "st_unicode_text.h"
enum { CTERM_SKYPIX_PALETTE_FIRST = 512 };
struct cterminal;
struct cterm_skypix {
	struct cterminal *terminal;
	struct cterm_skypix_parser parser;
	const st_amiga_font_t *font;
	const char *font_directory;
	struct st_pixel_buffer *brush, *undo;
	struct st_console_rect undo_rect, pending_rect, undo_pending_rect;
	uint8_t *pending, *undo_pending;
	int x, y, pen_x, pen_y, undo_x, undo_y;
	unsigned colours, brush_depth;
	bool custom, positioned, receiving, kick13, presented;
	uint64_t delay_until;
	uint16_t default_font;
	char warned[1024];
};
int cterm_skypix_create(struct cterminal *, const char *);
void cterm_skypix_destroy(struct cterm_skypix *);
int cterm_skypix_feed(struct cterm_skypix *, const uint8_t *, size_t, size_t *);
int cterm_skypix_finish(struct cterm_skypix *);
int cterm_skypix_reset(struct cterm_skypix *);
int cterm_skypix_reset_font(struct cterm_skypix *);
int cterm_skypix_reset_style(struct cterm_skypix *);
bool cterm_skypix_paused(void *);
int cterm_skypix_present(struct cterm_skypix *, const struct st_unicode_presentation_event *,
						 bool *);
bool cterm_skypix_control(struct cterm_skypix *, uint8_t);
int cterm_skypix_receive(struct cterm_skypix *, const void *, size_t);
#endif
