/* IGS add-on input at the same pre-emulation boundary as RIP. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_IGS_H_
#define CTERM_IGS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct cterminal;
struct st_graphics_parser;
struct st_graphics_argument;
struct st_igs_image;
enum { CTERM_IGS_PALETTE_FIRST = 768 };
enum { CTERM_IGS_MAX_POINTS = 128 };

struct cterm_igs_attributes {
	/* Writing mode uses the VDI caller's 1..4 convention. */
	/* Pens are effective logical selections; map to physical bits at raster
	 * use sites without revalidating them after a resolution change. */
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
	struct st_graphics_parser *parser;
	unsigned resolution;
	unsigned saved_resolution;
	uint32_t width, height;
	struct cterm_igs_attributes attributes, saved_attributes;
	uint16_t patterns[8][16];
	uint16_t raw_palette[16];
	uint32_t palette[16], saved_palette[16];
	int requested_palette[16][3];
	int draw_x, draw_y, scaling_mode, scaling_resolution;
	int draw_start_x, draw_start_y;
	/* Last-segment ownership survives native polygon/cap compositions. */
	bool last_line;
	bool destructive_backspace;
	/* Serial flow helpers read their own gate once and count/byte per send. */
	int flow_mode, loop_mode, loop_kind;
	size_t text_counter;
	uint8_t shared_text[129];
	size_t shared_text_size;
	uint8_t flow_on, flow_off;
	int flow_on_count, flow_off_count;
	int memory_route, double_step, marker_pen_cache;
	bool clipping;
	int clip_x1, clip_y1, clip_x2, clip_y2;
	int16_t math[128];
	/* Independent 32-bit states for the shared source and A's row generator. */
	uint32_t random_seed;
	uint32_t pattern_seed;
	int random_range[2][2];
	/* Each slot owns a distinct indexed image, preserving its native bytes. */
	struct st_igs_image *memory[2];
	/* Local monotonic milliseconds; injectable for deadline tests. */
	int (*now_ms)(uint64_t *);
	uint64_t input_deadline, text_deadline;
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
int cterm_igs_echo_backspace(struct cterm_igs *);
bool cterm_igs_paused(const struct cterm_igs *);
int cterm_igs_initialize(struct cterm_igs *, int operation);
void cterm_igs_set_scaling(struct cterm_igs *, int mode);
void cterm_igs_set_pen(struct cterm_igs *, int kind, int pen);
void cterm_igs_set_fill(struct cterm_igs *, int interior, int index, int border);
void cterm_igs_set_type(struct cterm_igs *, int kind, int style, int size);
void cterm_igs_set_text(struct cterm_igs *, int effects, int points, int rotation);
unsigned cterm_igs_plane_mask(unsigned resolution);
unsigned cterm_igs_physical_pen(const struct cterm_igs *, int pen);
int cterm_igs_marker(struct cterm_igs *, int x, int y);
int cterm_igs_line(struct cterm_igs *, int x1, int y1, int x2, int y2);
int cterm_igs_polyline(struct cterm_igs *, const int *coordinates, size_t count);
int cterm_igs_polygon(struct cterm_igs *, const int *coordinates, size_t count);
int cterm_igs_flood(struct cterm_igs *, int x, int y);
int cterm_igs_graphic_text(struct cterm_igs *, int x, int y, const uint8_t *, size_t);
int cterm_igs_rectangle(struct cterm_igs *, int x1, int y1, int x2, int y2);
int cterm_igs_box(struct cterm_igs *, int x1, int y1, int x2, int y2, bool rounded);
int cterm_igs_rounded_outline(struct cterm_igs *, int x1, int y1, int x2, int y2);
int cterm_igs_circle(struct cterm_igs *, int x, int y, int radius);
int cterm_igs_pie(struct cterm_igs *, int x, int y, int radius, int begin_degrees, int end_degrees);
int cterm_igs_ellpie(struct cterm_igs *, int x, int y, int x_radius, int y_radius, int begin_degrees, int end_degrees);
int cterm_igs_ellipse(struct cterm_igs *, int x, int y, int x_radius, int y_radius);
int cterm_igs_arc(struct cterm_igs *, int x, int y, int radius, int begin_degrees, int end_degrees);
int cterm_igs_ellarc(struct cterm_igs *, int x, int y, int x_radius, int y_radius, int begin_degrees, int end_degrees);
uint32_t cterm_igs_random(struct cterm_igs *);
/* Apply the descriptor's axis to a decimal constant/index. Lookup and random
 * results are returned as values, without a second coordinate transform. */
int cterm_igs_argument_value(struct cterm_igs *,
	const struct st_graphics_argument *, int *result);
int cterm_igs_resolution(struct cterm_igs *, int requested, int palette);
int cterm_igs_resolution_complete(struct cterm_igs *, int palette);
int cterm_igs_palette_pen(struct cterm_igs *, int pen, int red, int green, int blue);
int cterm_igs_state_init(struct cterm_igs *);
int cterm_igs_state_reset(struct cterm_igs *);
int cterm_igs_set_resolution(struct cterminal *, unsigned resolution);
void cterm_igs_clip_screen(struct cterm_igs *);

#endif
