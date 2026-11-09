/* IGS graphics state and I's selective restoration, separate from console state.
 * Original contracts: IGS reference 16, 28, 31, 117, 120 and 123. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_igs.h"
#include "cterm.h"
#include "cterm_render.h"
#include "io_error.h"

#include <string.h>

static const unsigned pen_map[16] = {0,15,1,2,4,6,3,5,7,8,9,10,12,14,11,13};
static const uint16_t pattern_zero[16] = {
	0,0,0x00c0,0x01e0,0x03b0,0x07b8,0x0fbc,0x0e0c,
	0x1f5e,0x1ffe,0x1f8e,0x1e06,0x1c06,0x1802,0x1000,0,
};
/* Original 2.21 tables at TEXT $13afc and $13144, in VDI pen order. */
static const int igs_palette[16][3] = {
	{1000,1000,1000},{0,0,0},{1000,0,0},{0,1000,0},{0,0,1000},{0,1000,1000},
	{0,0,0},{1000,1000,0},{714,571,857},{714,428,285},{1000,571,428},{0,571,428},
	{428,428,428},{142,428,571},{0,428,0},{285,142,0},
};
static const int vdi_palette[16][3] = {
	{1000,1000,1000},{0,0,0},{1000,0,0},{0,1000,0},{0,0,1000},{0,1000,1000},
	{1000,1000,0},{1000,0,1000},{714,714,714},{428,428,428},{1000,428,428},
	{428,1000,428},{428,428,1000},{428,1000,1000},{1000,1000,428},{1000,428,1000},
};

static unsigned
plane_mask(unsigned resolution)
{
	return resolution == 0 ? 15 : resolution == 1 ? 3 : 1;
}

static uint16_t
rgb_word(uint32_t rgb)
{
	return (uint16_t)(((((rgb >> 16) & 255) * 7 + 127) / 255) << 8
	    | ((((rgb >> 8) & 255) * 7 + 127) / 255) << 4
	    | ((rgb & 255) * 7 + 127) / 255);
}

static uint32_t
word_rgb(uint16_t raw)
{
	return (((raw >> 8) & 7u) * 255u + 3u) / 7u << 16
	    | (((raw >> 4) & 7u) * 255u + 3u) / 7u << 8
	    | ((raw & 7u) * 255u + 3u) / 7u;
}

static int
publish_palette(struct cterm_igs *igs)
{
	return st_console_set_palette(igs->terminal->screen.console,
	    CTERM_IGS_PALETTE_FIRST, igs->palette, 16);
}

int
cterm_igs_palette_pen(struct cterm_igs *igs, int pen, int red, int green, int blue)
{
	unsigned mask = plane_mask(igs->resolution);
	if (pen < 0 || (unsigned)pen > mask)
		return 0;
	int input[3] = {red,green,blue};
	unsigned packed = 0, sum = 0;
	for (unsigned i = 0; i < 3; ++i) {
		igs->requested_palette[pen][i] = input[i];
		int value = input[i] < 0 ? 0 : input[i] > 1000 ? 1000 : input[i];
		packed = (packed << 4) | (unsigned)((value + 72) / 143);
		sum += value < 142 ? 0u : value >= 858 ? 1000u : (unsigned)value;
	}
	if (igs->resolution == 2) {
		if (sum != 0 && sum != 3000)
			return 0;
		igs->raw_palette[0] = sum == 0 ? (uint16_t)pen : (uint16_t)~(unsigned)pen;
		igs->palette[0] = (igs->raw_palette[0] & 1) != 0 ? 0xffffff : 0;
		igs->palette[1] = igs->palette[0] ^ UINT32_C(0xffffff);
	}
	else {
		unsigned physical = pen_map[pen] & mask;
		igs->raw_palette[physical] = (uint16_t)packed;
		igs->palette[physical] = word_rgb((uint16_t)packed);
	}
	return publish_palette(igs);
}

static int
restore_palette(struct cterm_igs *igs)
{
	unsigned mask = plane_mask(igs->resolution);
	unsigned saved_mask = plane_mask(igs->saved_resolution);
	for (unsigned pen = 0; pen <= mask; ++pen) {
		unsigned physical = pen_map[pen] & mask;
		uint32_t rgb = igs->saved_palette[pen_map[pen] & saved_mask];
		igs->palette[physical] = rgb;
		igs->raw_palette[physical] = rgb_word(rgb);
		igs->requested_palette[pen][0] = (int)(((rgb >> 16) & 255) * 1000 / 255);
		igs->requested_palette[pen][1] = (int)(((rgb >> 8) & 255) * 1000 / 255);
		igs->requested_palette[pen][2] = (int)((rgb & 255) * 1000 / 255);
	}
	if (igs->resolution == 2)
		igs->raw_palette[0] = igs->palette[0] != 0 ? 1 : 0;
	return publish_palette(igs);
}

static void
restore_attributes(struct cterm_igs *igs)
{
	struct cterm_igs_attributes *a = &igs->attributes;
	const struct cterm_igs_attributes *s = &igs->saved_attributes;
	a->marker_type = s->marker_type;
	a->marker_pen = s->marker_pen;
	a->marker_height = s->marker_height;
	a->writing_mode = s->writing_mode;
	a->line_type = s->line_type;
	a->line_pen = s->line_pen;
	a->line_width = s->line_width;
	a->line_begin = s->line_begin;
	a->line_end = s->line_end;
	a->fill_interior = s->fill_interior;
	a->fill_pen = s->fill_pen;
	a->fill_index = s->fill_index;
	memcpy(igs->patterns[0], pattern_zero, sizeof(pattern_zero));
	a->text_pen = s->text_pen;
	a->text_rotation = s->text_rotation;
	a->text_effects = 0;
	a->text_height = s->text_height;
	a->text_point_mode = false;
	/* The port keeps this cache coherent rather than retaining the original
	 * mismatch between direct attribute restore and later scatter commands. */
	igs->marker_pen_cache = a->marker_pen;
}

void
cterm_igs_clip_screen(struct cterm_igs *igs)
{
	igs->clipping = true;
	igs->clip_x1 = igs->clip_y1 = 0;
	igs->clip_x2 = (int)igs->width - 1;
	igs->clip_y2 = (int)igs->height - 1;
}

int
cterm_igs_initialize(struct cterm_igs *igs, int operation)
{
	int status = 0;
	if (operation > 5)
		operation = 0;
	if (operation == 0 || operation == 1)
		status = restore_palette(igs);
	if (status != 0)
		return status;
	if (operation == 0 || operation == 2) {
		restore_attributes(igs);
		igs->double_step = 0;
		igs->memory_route = 0;
	}
	else if (operation == 3 || operation == 4) {
		const int (*table)[3] = operation == 3 ? igs_palette : vdi_palette;
		for (int pen = 0; pen < 16 && status == 0; ++pen)
			status = cterm_igs_palette_pen(igs, pen, table[pen][0], table[pen][1], table[pen][2]);
	}
	else if (operation == 5) {
		if (igs->saved_resolution == 2) {
			cterm_igs_clip_screen(igs);
			return 0;
		}
		if (igs->sync_wait_hz == 0 || igs->sync_wait_hz > 1000)
			return IO_ERROR_INVALID_ARGUMENT;
		uint64_t now;
		status = igs->now_ms(&now);
		if (status != 0)
			return status;
		uint64_t duration = (1000u + igs->sync_wait_hz - 1u) / igs->sync_wait_hz;
		if (now > UINT64_MAX - duration)
			return IO_ERROR_OUT_OF_RANGE;
		igs->wait_deadline = now + duration;
		igs->pending_resolution = (int)igs->saved_resolution;
		return IO_ERROR_IN_PROGRESS;
	}
	return status;
}

int
cterm_igs_state_init(struct cterm_igs *igs)
{
	struct cterminal *t = igs->terminal;
	unsigned offset = (t->screen.capabilities & ST_CONSOLE_CAP_EXTENDED_PALETTE) != 0 ? 16 : 0;
	for (unsigned i = 0; i < 16; ++i) {
		int status = st_console_get_palette(t->screen.console,
		    (uint16_t)(t->screen.mode_palette[i] + offset), &igs->saved_palette[i], 1);
		if (status != 0)
			return status;
		igs->palette[i] = igs->saved_palette[i];
		igs->raw_palette[i] = rgb_word(igs->palette[i]);
	}
	unsigned mask = plane_mask(igs->resolution);
	for (unsigned i = 0; i < 16; ++i)
		t->screen.mode_palette[i] = CTERM_IGS_PALETTE_FIRST + (i & mask);
	t->screen.capabilities &= ~ST_CONSOLE_CAP_EXTENDED_PALETTE;
	igs->saved_resolution = igs->resolution;
	igs->saved_attributes = (struct cterm_igs_attributes){
		.marker_type = 1, .marker_pen = 1, .marker_height = 11, .writing_mode = 1,
		.line_type = 1, .line_pen = 1, .line_width = 1, .user_line = 0xffff,
		.fill_interior = 1, .fill_index = 1, .fill_pen = 1, .fill_perimeter = true,
		.text_pen = 1, .text_height = igs->resolution == 2 ? 13 : 6,
		.text_points = igs->resolution == 2 ? 10 : 9, .text_face = 1,
	};
	for (unsigned i = 0; i < 16; ++i)
		igs->saved_attributes.user_fill[i] = 0xffff;
	return cterm_igs_state_reset(igs);
}

int
cterm_igs_state_reset(struct cterm_igs *igs)
{
	igs->attributes = igs->saved_attributes;
	memset(igs->patterns, 0, sizeof(igs->patterns));
	memset(igs->math, 0, sizeof(igs->math));
	igs->draw_x = igs->draw_y = 0;
	igs->scaling_mode = 0;
	igs->scaling_resolution = (int)igs->resolution;
	igs->random_seed = 1;
	igs->random_range[0][0] = igs->random_range[1][0] = 0;
	igs->random_range[0][1] = igs->random_range[1][1] = 199;
	cterm_igs_clip_screen(igs);
	return cterm_igs_initialize(igs, 0);
}
