/* IGS workstation state, separate from console state.
 * Original contracts: IGS reference 15/16/28/29/31/117/120/123/124/128/129/130. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_igs.h"
#include "cterm_igs_music.h"
#include "cterm_igs_fonts.h"
#include "cterm.h"
#include "cterm_render.h"
#include "st_igs_image.h"
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

unsigned
cterm_igs_plane_mask(unsigned resolution)
{
	return resolution == 0 ? 15 : resolution == 1 ? 3 : 1;
}

unsigned
cterm_igs_physical_pen(const struct cterm_igs *igs, int pen)
{
	return pen_map[pen] & cterm_igs_plane_mask(igs->resolution);
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
cterm_igs_palette_register(struct cterm_igs *igs, int index, int color)
{
	/* Setcolor queries have no side effects; X1 discards the old word.
	 * Raw writes preserve unused bits and leave VDI's request cache alone. */
	if (color < 0)
		return 0;
	unsigned physical = (unsigned)index & 15u;
	igs->raw_palette[physical] = (uint16_t)color;
	if (igs->resolution == 2) {
		if (physical != 0)
			return 0;
		igs->palette[0] = (color & 1) != 0 ? 0xffffff : 0;
		igs->palette[1] = igs->palette[0] ^ UINT32_C(0xffffff);
	}
	else
		igs->palette[physical] = word_rgb((uint16_t)color);
	return publish_palette(igs);
}

int
cterm_igs_palette_pen(struct cterm_igs *igs, int pen, int red, int green, int blue)
{
	unsigned mask = cterm_igs_plane_mask(igs->resolution);
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
	unsigned mask = cterm_igs_plane_mask(igs->resolution);
	unsigned saved_mask = cterm_igs_plane_mask(igs->saved_resolution);
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
	a->text_points = s->text_points;
	a->text_point_mode = false;
	/* The port keeps this cache coherent rather than retaining the original
	 * mismatch between direct attribute restore and later scatter commands. */
	igs->marker_pen_cache = a->marker_pen;
}

static int
select_palette(struct cterm_igs *igs, int selection)
{
	if (selection == 1)
		return restore_palette(igs);
	if (selection != 2 && selection != 3)
		return 0;
	const int (*table)[3] = selection == 2 ? igs_palette : vdi_palette;
	int status = 0;
	for (int pen = 0; pen < 16 && status == 0; ++pen)
		status = cterm_igs_palette_pen(igs, pen, table[pen][0], table[pen][1], table[pen][2]);
	return status;
}

void
cterm_igs_clip_screen(struct cterm_igs *igs)
{
	igs->clipping = true;
	igs->clip_x1 = igs->clip_y1 = 0;
	igs->clip_x2 = (int)igs->width - 1;
	igs->clip_y2 = (int)igs->height - 1;
}

void
cterm_igs_set_scaling(struct cterm_igs *igs, int mode)
{
	igs->scaling_mode = mode >= 0 && mode <= 2 ? mode : 0;
	igs->scaling_resolution = (int)igs->resolution;
}

static int
effective_pen(const struct cterm_igs *igs, int pen)
{
	return pen < 0 || (unsigned)pen > cterm_igs_plane_mask(igs->resolution) ? 1 : pen;
}

void
cterm_igs_set_marker_color(struct cterm_igs *igs, int pen)
{
	igs->attributes.marker_pen = effective_pen(igs, pen);
}

void
cterm_igs_set_pen(struct cterm_igs *igs, int kind, int pen)
{
	if (pen > 15)
		pen = 0;
	if (kind > 3)
		kind = 0;
	int effective = effective_pen(igs, pen);
	switch (kind) {
	case 0:
		igs->attributes.marker_pen = effective;
		/* IGS retains its request, not the workstation's fallback result. */
		igs->marker_pen_cache = pen;
		break;
	case 1: igs->attributes.line_pen = effective; break;
	case 2: igs->attributes.fill_pen = effective; break;
	case 3: igs->attributes.text_pen = effective; break;
	}
}

uint32_t
cterm_igs_random(struct cterm_igs *igs)
{
	igs->random_seed = igs->random_seed * UINT32_C(0xbb40e62d) + 1;
	return (igs->random_seed >> 8) & UINT32_C(0xffffff);
}

void
cterm_igs_set_fill(struct cterm_igs *igs, int interior, int index, int border)
{
	struct cterm_igs_attributes *a = &igs->attributes;
	if (interior < 0 || interior > 4)
		interior = 0;
	a->fill_interior = interior;
	if (interior < 4) {
		int maximum = interior == 2 ? 24 : 12;
		a->fill_index = index >= 1 && index <= maximum ? index : 1;
	}
	else {
		if (index < 0)
			index = 0;
		if (index > 7) {
			igs->pattern_seed = cterm_igs_random(igs);
			if (index == 8) {
				for (unsigned row = 0; row < 16; ++row) {
					igs->pattern_seed = igs->pattern_seed * UINT32_C(0x1afb) + UINT32_C(0x1fccd);
					igs->patterns[0][row] = (uint16_t)igs->pattern_seed;
				}
			}
			else
				memcpy(igs->patterns[0], pattern_zero, sizeof(pattern_zero));
			index = 0;
		}
		/* Correct the original pointer-to-scalar style call. User pattern
		 * rows are selected independently of the unused non-user index. */
		memcpy(a->user_fill, igs->patterns[index], sizeof(a->user_fill));
	}
	a->fill_perimeter = border != 0;
}

void
cterm_igs_set_text(struct cterm_igs *igs, int effects, int points, int rotation)
{
	struct cterm_igs_attributes *a = &igs->attributes;
	a->text_effects = (int)((unsigned)effects & 31u);
	if (rotation > 4) rotation = 0;
	/* Retain native signed rounding and the 3600 attribute. Wide arithmetic
	 * avoids accidental negative-word multiplication/addition wrap. The
	 * renderer must normalize periodic angles instead of reading stale origins. */
	int64_t angle = (int64_t)rotation*900;
	a->text_rotation = (int)((angle+450)/900*900);
	if (points < 8) points = 8;
	if (points > 20) points = 20;
	const struct cterm_igs_font *font = cterm_igs_font_for_points(points);
	a->text_points = font->points;
	a->text_height = font->top;
	a->text_point_mode = true;
}

void
cterm_igs_set_type(struct cterm_igs *igs, int kind, int style, int size)
{
	struct cterm_igs_attributes *a = &igs->attributes;
	if (style < 1 || style > 7)
		style = 1;
	if (kind == 1) {
		if (size < 1 || size > 8)
			size = 1;
		a->marker_type = style == 7 ? 3 : style;
		a->marker_height = size * 11;
		return;
	}
	if (kind != 2)
		return;
	if (style != 7)
		a->line_type = style;
	if (size >= 1 && size <= 41) {
		if (style == 7) {
			if (size <= 32) {
				unsigned bank = size <= 16 ? 6u : 7u;
				unsigned row = (unsigned)size - (bank == 6 ? 1u : 17u);
				uint16_t mask = igs->patterns[bank][row];
				a->user_line = (uint16_t)(mask != 0 ? mask : bank == 6 ? 0xaaaa : 0x5555);
			}
			a->line_type = 7;
		}
		int width = style == 1 ? (size > 40 ? 40 : size) : 1;
		a->line_width = 2 * ((width - 1) / 2) + 1;
		/* Width requests cannot reach an endpoint selector: patterned
		 * styles change the native size local to one before those tests. */
		return;
	}
	static const struct { int selector, begin, end; } ends[] = {
		{0,0,0},{50,1,1},{51,1,0},{52,0,1},{53,1,2},{54,2,1},
		{60,2,2},{61,2,0},{62,0,2},{63,2,1},{64,1,2},
	};
	for (size_t i = 0; i < sizeof(ends)/sizeof(*ends); ++i)
		if (size == ends[i].selector) {
			a->line_begin = ends[i].begin;
			a->line_end = ends[i].end;
			break;
		}
}

int
cterm_igs_wait_sync(struct cterm_igs *igs, unsigned units)
{
	if (units == 0)
		return 0;
	if (igs->sync_wait_hz == 0 || igs->sync_wait_hz > 1000)
		return IO_ERROR_INVALID_ARGUMENT;
	uint64_t now;
	int status = igs->now_ms(&now);
	if (status != 0)
		return status;
	uint64_t duration = ((uint64_t)units * 1000 + igs->sync_wait_hz - 1) / igs->sync_wait_hz;
	if (now > UINT64_MAX - duration)
		return IO_ERROR_OUT_OF_RANGE;
	igs->wait_deadline = now + duration;
	return IO_ERROR_IN_PROGRESS;
}

static int
begin_resolution(struct cterm_igs *igs, unsigned resolution, int palette)
{
	int status = cterm_igs_wait_sync(igs, 1);
	if (status != IO_ERROR_IN_PROGRESS)
		return status;
	igs->pending_resolution = (int)resolution;
	igs->pending_palette = palette;
	return IO_ERROR_IN_PROGRESS;
}

int
cterm_igs_resolution_complete(struct cterm_igs *igs, int palette)
{
	/* R's palette calls follow the mode helper and full-screen clipping.
	 * Invalid palette selectors preserve registers; they are not I values. */
	cterm_igs_clip_screen(igs);
	return select_palette(igs, palette);
}

int
cterm_igs_resolution(struct cterm_igs *igs, int requested, int palette)
{
	unsigned target = requested >= 0 && requested <= 1 ? (unsigned)requested : igs->resolution;
	if (igs->resolution != 2 && target != igs->resolution)
		return begin_resolution(igs, target, palette);
	return cterm_igs_resolution_complete(igs, palette);
}

int
cterm_igs_initialize(struct cterm_igs *igs, int operation)
{
	int status = 0;
	if (operation > 5)
		operation = 0;
	if (operation == 0 || operation == 1)
		status = select_palette(igs, 1);
	if (status != 0)
		return status;
	if (operation == 0 || operation == 2) {
		restore_attributes(igs);
		igs->double_step = 0;
		igs->memory_route = 0;
	}
	else if (operation == 3 || operation == 4) {
		status = select_palette(igs, operation - 1);
	}
	else if (operation == 5) {
		if (igs->saved_resolution == 2) {
			cterm_igs_clip_screen(igs);
			return 0;
		}
		return begin_resolution(igs, igs->saved_resolution, 0);
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
	unsigned mask = cterm_igs_plane_mask(igs->resolution);
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
	int status = cterm_igs_music_reset(igs);
	if (status != 0) return status;
	for (unsigned i = 0; i < 2; ++i) {
		st_igs_image_destroy(igs->memory[i]);
		igs->memory[i] = NULL;
	}
	memset(igs->memory_rect,0,sizeof(igs->memory_rect));
	memset(igs->memory_view,0,sizeof(igs->memory_view));
	igs->bitmap_errors = 0;
	memset(&igs->palette_rotation,0,sizeof(igs->palette_rotation));
	igs->attributes = igs->saved_attributes;
	memset(igs->patterns, 0, sizeof(igs->patterns));
	memset(igs->math, 0, sizeof(igs->math));
	igs->draw_x = igs->draw_y = 0;
	igs->draw_start_x = igs->draw_start_y = 0;
	memset(igs->shared_text,0,sizeof(igs->shared_text));
	igs->shared_text_size = 0; igs->text_counter = 0; igs->loop_kind = 0; igs->text_deadline = 0;
	igs->last_line = true;
	igs->destructive_backspace = false;
	igs->flow_mode = 1;
	igs->flow_configuring = 0;
	igs->loop_mode = 0;
	igs->flow_on = 17; igs->flow_off = 19;
	igs->flow_on_count = igs->flow_off_count = 1;
	igs->scaling_mode = 0;
	igs->scaling_resolution = (int)igs->resolution;
	igs->random_seed = 1;
	igs->pattern_seed = 0;
	igs->marker_cycle = igs->marker_cycle_restart = 0;
	igs->random_range[0][0] = igs->random_range[1][0] = 0;
	igs->random_range[0][1] = igs->random_range[1][1] = 199;
	cterm_igs_clip_screen(igs);
	return cterm_igs_initialize(igs, 0);
}
