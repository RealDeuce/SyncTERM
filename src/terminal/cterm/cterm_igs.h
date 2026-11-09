/* IGS add-on input at the same pre-emulation boundary as RIP. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_IGS_H_
#define CTERM_IGS_H_

#include "st_graphics_parser.h"
#include "st_igs_image.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct cterminal;
enum { CTERM_IGS_PALETTE_FIRST = 768 };

struct cterm_igs_attributes {
	int marker_type, marker_pen, marker_height, writing_mode;
	int line_type, line_pen, line_width, line_begin, line_end;
	uint16_t user_line;
	int fill_interior, fill_index, fill_pen;
	bool fill_perimeter;
	uint16_t user_fill[16];
	int text_pen, text_rotation, text_effects, text_height, text_points;
	bool text_point_mode;
	int text_face, text_align_h, text_align_v;
};

struct cterm_igs {
	struct cterminal *terminal;
	st_graphics_parser_t *parser;
	unsigned resolution;
	unsigned saved_resolution;
	uint32_t width, height;
	struct cterm_igs_attributes attributes, saved_attributes;
	uint16_t patterns[8][16];
	uint16_t raw_palette[16];
	uint32_t palette[16], saved_palette[16];
	int requested_palette[16][3];
	int draw_x, draw_y, scaling_mode, scaling_resolution;
	int memory_route, double_step, marker_pen_cache;
	bool clipping;
	int clip_x1, clip_y1, clip_x2, clip_y2;
	int16_t math[128];
	uint32_t random_seed;
	int random_range[2][2];
	/* Each slot owns a distinct indexed image, preserving its native bytes. */
	struct st_igs_image *memory[2];
	/* Local monotonic milliseconds; injectable for deadline tests. */
	int (*now_ms)(uint64_t *);
	uint64_t input_deadline;
	unsigned sync_wait_hz;
	uint64_t wait_deadline;
	int pending_resolution;
	int pending_palette;
	bool feeding, reset_pending;
	uint8_t warned[32];
};

int cterm_igs_create(struct cterminal *);
void cterm_igs_destroy(struct cterm_igs *);
void cterm_igs_reset(struct cterm_igs *);
int cterm_igs_feed(struct cterm_igs *, const void *, size_t, size_t *);
int cterm_igs_finish(struct cterm_igs *);
int cterm_igs_poll(struct cterm_igs *);
bool cterm_igs_paused(const struct cterm_igs *);
int cterm_igs_initialize(struct cterm_igs *, int operation);
int cterm_igs_resolution(struct cterm_igs *, int requested, int palette);
int cterm_igs_resolution_complete(struct cterm_igs *, int palette);
int cterm_igs_palette_pen(struct cterm_igs *, int pen, int red, int green, int blue);
int cterm_igs_state_init(struct cterm_igs *);
int cterm_igs_state_reset(struct cterm_igs *);
int cterm_igs_set_resolution(struct cterminal *, unsigned resolution);
void cterm_igs_clip_screen(struct cterm_igs *);

#endif
