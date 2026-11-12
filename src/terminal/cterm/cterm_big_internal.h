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
enum { CTERM_BIG_INPUT_KEYS = 64 };
struct cterm_big_input {
	struct cterm_key keys[CTERM_BIG_INPUT_KEYS];
	size_t first, count;
	uint8_t scan;
	bool scan_pending, replaying, overflow;
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
	bool footer_active, starting_body, gang_selecting, gang_wire;
	uint64_t pair_deadline, pair_sample;
	bool pair_wait, pair_body, feeding, reset_pending;
	uint8_t warned[32];
	uint16_t console_fonts[3];
	bool console_font_valid[3];
};
bool cterm_big_input_owned(const struct cterm_big *);
int cterm_big_input_peek(void *, bool *);
int cterm_big_input_read(void *, uint8_t *);
int cterm_big_input_forward(struct cterm_big *);
void cterm_big_setup_begin(struct cterm_big *);
int cterm_big_setup_poll(struct cterm_big *, const uint8_t *, size_t, size_t *, size_t *, size_t *, bool *);
#endif
