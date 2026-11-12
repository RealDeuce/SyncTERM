/* IGS add-on input at the same pre-emulation boundary as RIP. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_IGS_H_
#define CTERM_IGS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cterm_igs_input.h"
#include "cterm_igs_mouse.h"
#include "cterm_igs_collect.h"
#include "cterm_igs_loop.h"
#include "cterm_igs_script.h"

struct cterminal;
struct st_graphics_parser;
struct st_graphics_argument;
struct st_igs_image;
struct cterm_igs_audio;
struct cterm_igs_music;
struct st_console_test_application;
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

struct cterm_igs_memory_view {
	int width, height, word_width;
	unsigned planes;
	bool screen;
};

struct cterm_igs_palette_rotation {
	/* X8's serial snapshots and restore choice survive I/R and cancellation. */
	int snapshot[16][3];
	uint16_t registers[16];
	bool restore_raw;
	/* A queried value belongs to the write after the next local sync wait. */
	bool active, raw, key, delaying, final;
	int first, last, index, step;
	unsigned remaining;
	uint16_t delay;
	int saved[3], held[3], query[3];
};

struct cterm_igs_sweep {
	bool active, increasing;
	int points[4], axis, step;
	int32_t scalar, bound;
	unsigned wait;
};

struct cterm_igs_note {
	/* Parser-owner state; the audio consumer never reads the pending wait. */
	bool active, key;
	int voice, stop;
};


struct cterm_igs {
	struct cterminal *terminal;
	struct st_graphics_parser *parser;
	struct cterm_igs_audio *audio;
	struct cterm_igs_music *music;
	struct cterm_igs_input input;
	struct cterm_igs_query query;
	struct cterm_igs_collect collect;
	struct cterm_igs_loop loop;
	struct cterm_igs_script script;
	unsigned resolution;
	unsigned saved_resolution;
	uint32_t width, height;
	struct cterm_igs_attributes attributes, saved_attributes;
	uint16_t patterns[8][16];
	/* X7 owns its evaluated prefix while incrementally receiving rows. */
	bool pattern_loading;
	int pattern_slot;
	/* X13 has formatted the shared buffer and owns its numeric prefix. */
	bool math_printing;
	/* X11 owns its complete numeric prefix during incremental raw input. */
	bool bitmap_loading;
	int bitmap_operation, bitmap_section;
	size_t bitmap_offset, bitmap_size, bitmap_received;
	unsigned bitmap_errors;
	uint64_t bitmap_deadline;
	uint16_t raw_palette[16];
	uint32_t palette[16], saved_palette[16];
	int requested_palette[16][3];
	struct cterm_igs_palette_rotation palette_rotation;
	struct cterm_igs_sweep sweep;
	struct cterm_igs_note note;
	int draw_x, draw_y, scaling_mode, scaling_resolution;
	int draw_start_x, draw_start_y;
	/* Last-segment ownership survives native polygon/cap compositions. */
	bool last_line;
	bool destructive_backspace;
	/* Serial flow helpers read their own gate once and count/byte per send. */
	int flow_mode, loop_mode, loop_kind;
	/* X5 commits its mode/byte before reading the remaining count. */
	int flow_configuring;
	size_t text_counter;
	uint8_t shared_text[129];
	size_t shared_text_size;
	uint8_t flow_on, flow_off;
	int flow_on_count, flow_off_count;
	int memory_route, double_step, marker_pen_cache;
	int marker_cycle, marker_cycle_restart;
	bool clipping;
	int clip_x1, clip_y1, clip_x2, clip_y2;
	int16_t math[128];
	/* Independent 32-bit states for the shared source and A's row generator. */
	uint32_t random_seed;
	uint32_t pattern_seed;
	int random_range[2][2];
	/* Each slot owns a distinct indexed image, preserving its native bytes. */
	struct st_igs_image *memory[2];
	int memory_rect[2][4];
	struct cterm_igs_memory_view memory_view[2];
	/* Only a starred G route can own a second copy across a local wait. */
	bool copy_pending;
	int copy_logic, copy_source, copy_destination, copy_points[8];
	/* Local monotonic milliseconds; injectable for deadline tests. */
	int (*now_ms)(uint64_t *);
	uint64_t input_deadline, text_deadline;
	unsigned sync_wait_hz;
	uint64_t wait_deadline;
	/* t's key cancellation and flow lifecycle are separate from X8's state. */
	bool timed_pause, pause_key;
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
/* Optional ST_CONSOLE_TEST capture helper; copies worker state under its lock. */
void cterm_igs_audio_inspect(const struct cterm_igs *, struct st_console_test_application *);
void cterm_igs_input_inspect(const struct cterm_igs *, struct st_console_test_application *);
void cterm_igs_music_inspect(const struct cterm_igs *, struct st_console_test_application *);
int cterm_igs_echo_backspace(struct cterm_igs *);
bool cterm_igs_paused(const struct cterm_igs *);
bool cterm_igs_handle_key(struct cterm_igs *, const struct cterm_igs_key *);
int cterm_igs_initialize(struct cterm_igs *, int operation);
void cterm_igs_set_scaling(struct cterm_igs *, int mode);
void cterm_igs_set_pen(struct cterm_igs *, int kind, int pen);
void cterm_igs_set_fill(struct cterm_igs *, int interior, int index, int border);
void cterm_igs_set_type(struct cterm_igs *, int kind, int style, int size);
void cterm_igs_set_text(struct cterm_igs *, int effects, int points, int rotation);
unsigned cterm_igs_plane_mask(unsigned resolution);
unsigned cterm_igs_physical_pen(const struct cterm_igs *, int pen);
int cterm_igs_pmarker(struct cterm_igs *, int x, int y);
int cterm_igs_marker(struct cterm_igs *, int x, int y);
int cterm_igs_scatter(struct cterm_igs *, int x, int y, int extent_x, int extent_y, int count);
void cterm_igs_set_marker_color(struct cterm_igs *, int pen);
int cterm_igs_line(struct cterm_igs *, int x1, int y1, int x2, int y2);
int cterm_igs_polyline(struct cterm_igs *, const int *coordinates, size_t count);
int cterm_igs_polygon(struct cterm_igs *, const int *coordinates, size_t count);
int cterm_igs_flood(struct cterm_igs *, int x, int y);
int cterm_igs_graphic_text(struct cterm_igs *, int x, int y, const uint8_t *, size_t);
int cterm_igs_math_write(struct cterm_igs *, int operation, int a, int b, int index);
int cterm_igs_math_clear(struct cterm_igs *, int first, int last);
int cterm_igs_math_format(struct cterm_igs *, int index);
int cterm_igs_math_console(struct cterm_igs *, int index);
int cterm_igs_grab(struct cterm_igs *, const int *, size_t count);
int cterm_igs_grab_resume(struct cterm_igs *);
/* Prepared views and caller rectangle, with no G routing/repeat effects. */
int cterm_igs_blit(struct cterm_igs *, int logic, int source, int destination, const int points[8]);
size_t cterm_igs_bitmap_size(int section);
int cterm_igs_bitmap_begin(struct cterm_igs *, int operation, int section);
int cterm_igs_bitmap_bytes(struct cterm_igs *, const uint8_t *, size_t, size_t *accepted, bool *end);
int cterm_igs_bitmap_complete(struct cterm_igs *, int operation, int section);
int cterm_igs_bitmap_wipe(struct cterm_igs *, int section, int fill);
void cterm_igs_bitmap_cancel(struct cterm_igs *);
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
int cterm_igs_wait_sync(struct cterm_igs *, unsigned units);
int cterm_igs_resolution_complete(struct cterm_igs *, int palette);
int cterm_igs_palette_pen(struct cterm_igs *, int pen, int red, int green, int blue);
int cterm_igs_palette_register(struct cterm_igs *, int index, int color);
int cterm_igs_palette_rotation_begin(struct cterm_igs *, int first, int last, int times, int delay);
int cterm_igs_palette_rotation_resume(struct cterm_igs *);
void cterm_igs_palette_rotation_cancel(struct cterm_igs *);
int cterm_igs_sweep_begin(struct cterm_igs *, const int values[9]);
int cterm_igs_sweep_resume(struct cterm_igs *);
void cterm_igs_sweep_cancel(struct cterm_igs *);
int cterm_igs_state_init(struct cterm_igs *);
int cterm_igs_state_reset(struct cterm_igs *);
int cterm_igs_set_resolution(struct cterminal *, unsigned resolution);
void cterm_igs_clip_screen(struct cterm_igs *);

#endif
