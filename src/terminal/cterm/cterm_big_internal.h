/* Private BIG terminal state; native fixtures may inspect serialized owners. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_BIG_INTERNAL_H_
#define CTERM_BIG_INTERNAL_H_
#include "cterm_big.h"
#include "st_big_numeric.h"
#include "st_big_state.h"
#include "st_big_control.h"
#include "cterm_key.h"
#include "st_big_stream.h"
#include "st_big_collect.h"
#include "st_big_fill.h"
#include "st_big_bar3d.h"
#include "st_big_flood.h"
#include "st_big_video.h"
/* Nominal 4096-byte flood workspace, eight bytes per logical record.
 * Native bounds replace the original placement-dependent DOS stack guard. */
enum { CTERM_BIG_FLOOD_RECORDS = 4096 / 8 };
struct cterm_big_flood {
	uint16_t x, y, selector;
	unsigned phase;
	bool active, chained;
	struct st_big_flow_reader flow;
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
	struct st_big_state state;
	struct st_big_control control;
	struct st_big_footer footer;
	struct st_big_control_sink control_io;
	struct cterm_big_input input;
	struct st_big_text text;
	struct cterm_big_setup setup;
	struct cterm_big_zone zone;
	struct cterm_big_collect collect;
	struct cterm_big_mouse mouse;
	struct st_big_fill_edit fill_edit;
	struct st_big_producer producer;
	struct cterm_big_bar bar;
	struct cterm_big_flood flood;
	struct st_big_flood flood_work;
	struct st_big_flood_record flood_records[CTERM_BIG_FLOOD_RECORDS];
	struct st_pixel_buffer *video_backing;
	struct st_big_video video;
	uint16_t video_start;
	uint8_t video_active_page, video_visual_page;
	uint16_t video_cursors[4][2];
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
int cterm_big_input_peek(void *, bool *);
int cterm_big_input_read(void *, uint8_t *);
int cterm_big_input_forward(struct cterm_big *);
void cterm_big_setup_begin(struct cterm_big *);
int cterm_big_setup_poll(struct cterm_big *, const uint8_t *, size_t, size_t *, size_t *, size_t *, bool *);
int cterm_big_zone_field(struct cterm_big *, const struct st_graphics_command_view *);
int cterm_big_zone_begin(struct cterm_big *, const struct st_graphics_command_view *);
int cterm_big_zone_poll(struct cterm_big *, const uint8_t *, size_t, size_t *, size_t *, size_t *, bool *);
int cterm_big_draw_p(struct cterm_big *, const struct st_graphics_command_view *);
int cterm_big_draw_line(struct cterm_big *, const struct st_graphics_command_view *);
int cterm_big_draw_z(struct cterm_big *, const struct st_graphics_command_view *);
int cterm_big_bar_begin(struct cterm_big *, const struct st_graphics_command_view *);
int cterm_big_bar_poll(struct cterm_big *, const uint8_t *, size_t, size_t *, bool *);
int cterm_big_draw_flood(struct cterm_big *, uint16_t x, uint16_t y, uint16_t boundary);
int cterm_big_flood_begin(struct cterm_big *, const struct st_graphics_command_view *);
int cterm_big_flood_poll(struct cterm_big *, size_t *budget, bool *complete);
int cterm_big_page(struct cterm_big *, bool visual, uint16_t page);
int cterm_big_video_prepare(struct cterm_big *);
void cterm_big_video_reset(struct cterm_big *);
struct st_big_video_view cterm_big_video_view(const struct cterm_big *, const struct st_pixel_buffer *);
int cterm_big_video_segment(const struct cterm_big *, uint16_t *segment);
#endif
