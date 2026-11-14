/* Private BIG terminal state; native fixtures may inspect serialized owners. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_BIG_INTERNAL_H_
#define CTERM_BIG_INTERNAL_H_
#include "cterm_big.h"
#include "st_big_numeric.h"
#include "st_big_state.h"
#include "st_big_palette.h"
#include "st_big_spray.h"
#include "st_big_font.h"
#include "st_big_control.h"
#include "cterm_key.h"
#include "st_big_stream.h"
#include "st_big_collect.h"
#include "st_big_fill.h"
#include "st_big_bar3d.h"
#include "st_big_flood.h"
#include "st_big_video.h"
#include "st_big_image.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Nominal 4096-byte flood workspace, eight bytes per logical record.
 * Native bounds replace the original placement-dependent DOS stack guard. */
enum { CTERM_BIG_GRAPH_BYTES = 4096,
	CTERM_BIG_FLOOD_RECORDS = CTERM_BIG_GRAPH_BYTES / 8,
	CTERM_BIG_GRAPH_POINTS = (CTERM_BIG_GRAPH_BYTES-2) / 4 };
struct cterm_big_graph_points {
	uint16_t prefix;
	struct st_big_producer_point records[CTERM_BIG_GRAPH_POINTS];
};
/* One owner, mutually exclusive native scratch views. These are not a DOS
 * near-stack/address model; each operation initializes its reached records. */
union cterm_big_graph_workspace {
	struct st_big_flood_record flood[CTERM_BIG_FLOOD_RECORDS];
	struct cterm_big_graph_points points;
};
_Static_assert(offsetof(struct cterm_big_graph_points,records)==2,"BIG point view prefix");
_Static_assert(sizeof(union cterm_big_graph_workspace)==CTERM_BIG_GRAPH_BYTES,"BIG graph workspace");
struct cterm_big_flood {
	uint16_t x, y, selector;
	unsigned phase;
	bool active, chained;
	struct st_big_flow_reader flow;
};
struct cterm_big_rotation {
	struct st_big_palette_rotation colors;
	struct st_big_flow_reader flow;
	uint64_t deadline, sample;
	uint16_t counter;
	unsigned phase;
	bool active, chained, sampled;
};
struct cterm_big_bar {
	uint16_t points[4], kind;
	unsigned phase;
	bool active, wire, chained;
};
enum cterm_big_setup_phase { CTERM_BIG_SETUP_OFF, CTERM_BIG_SETUP_HEADER,
	CTERM_BIG_SETUP_TARGET, CTERM_BIG_SETUP_TEXT, CTERM_BIG_SETUP_TYPE,
	CTERM_BIG_SETUP_COUNT, CTERM_BIG_SETUP_KEY, CTERM_BIG_SETUP_WORD,
	CTERM_BIG_SETUP_FLOW };
struct cterm_big_setup {
	enum cterm_big_setup_phase phase;
	unsigned header;
	size_t index;
	bool number_started, number_wire, abort, key_pending, cooperative;
	struct st_big_stream_reader stream;
	struct st_big_flow_reader flow;
};
enum cterm_big_zone_phase { CTERM_BIG_ZONE_OFF, CTERM_BIG_ZONE_FLOW,
	CTERM_BIG_ZONE_NEXT, CTERM_BIG_ZONE_READ };
struct cterm_big_zone {
	enum cterm_big_zone_phase phase;
	int32_t id;
	size_t index;
	bool extra, chained, cooperative;
	struct st_big_stream_reader stream;
	struct st_big_flow_reader flow;
};
enum { CTERM_BIG_INPUT_KEYS = 64 };
struct cterm_big_text_output {
	struct st_big_stream_reader stream;
	uint16_t x,y;
	bool active,chained;
};
enum cterm_big_pause_phase { CTERM_BIG_PAUSE_OFF, CTERM_BIG_PAUSE_DRAIN,
	CTERM_BIG_PAUSE_DRAIN_READ, CTERM_BIG_PAUSE_NUMBER, CTERM_BIG_PAUSE_START,
	CTERM_BIG_PAUSE_TIME, CTERM_BIG_PAUSE_KEY, CTERM_BIG_PAUSE_READ };
struct cterm_big_pause {
	enum cterm_big_pause_phase phase;
	uint16_t seconds;
	uint64_t deadline, sample;
	bool chained, number_started, wire, expired, sampled;
};
enum cterm_big_query_phase { CTERM_BIG_QUERY_OFF, CTERM_BIG_QUERY_READY,
	CTERM_BIG_QUERY_VERSION, CTERM_BIG_QUERY_REGISTERED, CTERM_BIG_QUERY_RESOLUTION,
	CTERM_BIG_QUERY_RESOLUTION_COLON, CTERM_BIG_QUERY_MEMORY,
	CTERM_BIG_QUERY_MEMORY_COLON, CTERM_BIG_QUERY_SEND, CTERM_BIG_QUERY_DONE };
struct cterm_big_query {
	enum cterm_big_query_phase phase, next;
	uint16_t selector;
	unsigned index;
	uint8_t byte;
	bool chained;
};
struct cterm_big_mouse_sample {
	int32_t x,y;
	uint32_t buttons;
	uint64_t generation;
	bool allowed;
};
struct cterm_big_event {
	bool mouse;
	union { struct cterm_key key; struct cterm_big_mouse_sample sample; } data;
};
struct cterm_big_input {
	struct cterm_big_event events[CTERM_BIG_INPUT_KEYS];
	size_t first, count;
	uint8_t scan;
	bool scan_pending, replaying, overflow;
};
struct cterm_big_collect {
	struct st_big_collect reader;
	bool active, chained, pointer, shown, host_saved, host_visible;
	bool confirming, extended, armed, mouse_known, mouse_lost, scan_pending;
	uint8_t scan;
	uint16_t x,y, old_x,old_y;
	int32_t mouse_x,mouse_y;
	uint32_t buttons;
	uint64_t generation;
};
struct cterm_big_mouse {
	int32_t x,y, known_x,known_y;
	uint32_t buttons;
	uint64_t generation;
	bool observed, pending, owned, known;
};
struct cterm_big {
	struct cterminal *terminal;
	struct st_graphics_parser *parser;
	struct st_big_numeric numeric;
	/* Local wall-clock seed source, synchronous; waits use numeric.io.now_ns.
	 * X2/X0 take only its seconds low WORD for the shared CRT generator. */
	int (*random_seconds)(void *, uint64_t *);
	struct st_big_state state;
	struct st_big_fonts fonts;
	struct st_big_spray spray;
	struct st_big_control control;
	struct st_big_flow_settings flow_settings;
	size_t flow_settings_arguments;
	struct st_big_footer footer;
	struct st_big_control_sink control_io;
	struct cterm_big_input input;
	struct st_big_text text;
	struct cterm_big_text_output text_output;
	struct cterm_big_pause pause;
	struct cterm_big_query query;
	/* Available AdLib music-buffer flag, separate from mute/playback.
	 * Zero until the audio owner installs its buffer; retained across I/R. */
	uint16_t music_memory;
	struct cterm_big_setup setup;
	struct cterm_big_zone zone;
	struct cterm_big_collect collect;
	struct cterm_big_mouse mouse;
	struct st_big_fill_edit fill_edit;
	struct st_big_producer producer;
	struct cterm_big_bar bar;
	struct cterm_big_flood flood;
	struct cterm_big_rotation rotation;
	struct st_big_flood flood_work;
	union cterm_big_graph_workspace graph_work;
	struct st_pixel_buffer *video_backing;
	struct st_big_video video;
	uint16_t video_start;
	uint8_t video_active_page, video_visual_page;
	uint16_t video_cursors[4][2];
	struct st_big_image image;
	uint8_t image_bytes[ST_BIG_IMAGE_CAPACITY], image_pens[ST_BIG_IMAGE_PENS];
	uint16_t image_operation, image_mode, image_size;
	/* G0 pine/cone, G1 maple/syrup, G2 birch/plywood, G3 oak/acorn.
	 * S tests birch then pine after both reads; internal setters do not. */
	uint16_t feature_gates[4][2];
	size_t image_page_calls;
	int last_error;
	size_t rejected_commands;
	bool footer_active, starting_body, gang_selecting, gang_wire;
	uint64_t pair_deadline, pair_sample;
	bool pair_wait, pair_body, feeding, reset_pending;
	uint8_t warned[32];
	uint16_t console_fonts[3];
	bool console_font_valid[3];
};
bool cterm_big_input_push(struct cterm_big *, const struct cterm_big_event *);
bool cterm_big_input_take(struct cterm_big *, struct cterm_big_event *);
void cterm_big_mouse_flush(struct cterm_big *);
uint8_t cterm_big_key_byte(const struct cterm_key *, uint8_t *scan, bool *extended);
int cterm_big_collect_begin(struct cterm_big *, const struct st_graphics_command_view *);
int cterm_big_collect_poll(struct cterm_big *, size_t *budget, bool *complete);
int cterm_big_collect_cancel(struct cterm_big *);
int cterm_big_collect_sample(struct cterm_big *, const struct cterm_big_mouse_sample *);
int cterm_big_pmarker(struct cterm_big *, uint16_t x, uint16_t y);
bool cterm_big_input_owned(const struct cterm_big *);
void cterm_big_pause_begin(struct cterm_big *, bool chained);
int cterm_big_query_begin(struct cterm_big *, const struct st_graphics_command_view *);
/* The initial readiness request precedes every selector test. Send retries
 * latch a byte; returning send failures are ignored as in the original
 * query caller. Reset interrupts subsequent requests even after a return. */
int cterm_big_query_poll(struct cterm_big *, size_t *output_budget, bool *complete);
int cterm_big_pause_poll(struct cterm_big *, const uint8_t *, size_t,
	size_t *used, size_t *work_budget, bool *complete);
int cterm_big_input_peek(void *, bool *);
uint16_t cterm_big_input_status(struct cterm_big *);
int cterm_big_input_read(void *, uint8_t *);
int cterm_big_input_forward(struct cterm_big *);
void cterm_big_setup_begin(struct cterm_big *);
int cterm_big_setup_poll(struct cterm_big *, const uint8_t *, size_t, size_t *, size_t *, size_t *, bool *);
int cterm_big_zone_field(struct cterm_big *, const struct st_graphics_command_view *);
int cterm_big_zone_begin(struct cterm_big *, const struct st_graphics_command_view *);
int cterm_big_zone_poll(struct cterm_big *, const uint8_t *, size_t, size_t *, size_t *, size_t *, bool *);
int cterm_big_draw_p(struct cterm_big *, const struct st_graphics_command_view *);
int cterm_big_draw_spray(struct cterm_big *);
int cterm_big_draw_text(struct cterm_big *, uint16_t x, uint16_t y);
int cterm_big_draw_line(struct cterm_big *, const struct st_graphics_command_view *);
int cterm_big_draw_k(struct cterm_big *, const struct st_graphics_command_view *);
int cterm_big_draw_z(struct cterm_big *, const struct st_graphics_command_view *);
int cterm_big_bar_begin(struct cterm_big *, const struct st_graphics_command_view *);
int cterm_big_bar_poll(struct cterm_big *, const uint8_t *, size_t, size_t *, bool *);
int cterm_big_draw_flood(struct cterm_big *, uint16_t x, uint16_t y, uint16_t boundary);
int cterm_big_flood_begin(struct cterm_big *, const struct st_graphics_command_view *);
int cterm_big_flood_poll(struct cterm_big *, size_t *budget, bool *complete);
int cterm_big_rotation_begin(struct cterm_big *, const struct st_graphics_command_view *);
int cterm_big_rotation_poll(struct cterm_big *, size_t *output_budget, size_t *body_budget,
	const struct st_big_state_sink *, bool *complete);
int cterm_big_page(struct cterm_big *, bool visual, uint16_t page);
int cterm_big_video_prepare(struct cterm_big *, bool full_memory);
void cterm_big_video_reset(struct cterm_big *);
struct st_big_video_view cterm_big_video_view(const struct cterm_big *, const struct st_pixel_buffer *);
int cterm_big_video_segment(const struct cterm_big *, uint16_t *segment);
int cterm_big_image_field(struct cterm_big *, const struct st_graphics_command_view *);
int cterm_big_image_command(struct cterm_big *, const struct st_graphics_command_view *);
int cterm_big_draw_image(struct cterm_big *, bool capture, const uint16_t *words);
#endif
