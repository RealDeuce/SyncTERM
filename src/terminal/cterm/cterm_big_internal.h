/* Private BIG terminal state; native fixtures may inspect serialized owners. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_BIG_INTERNAL_H_
#define CTERM_BIG_INTERNAL_H_
#include "cterm_big.h"
#include "st_big_numeric.h"
#include "st_big_state.h"
#include "st_big_control.h"
#include "cterm_key.h"
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
	bool footer_active, starting_body, gang_selecting, gang_wire;
	uint64_t pair_deadline, pair_sample;
	bool pair_wait, feeding, reset_pending;
	uint8_t warned[32];
	uint16_t console_fonts[3];
	bool console_font_valid[3];
};
bool cterm_big_input_owned(const struct cterm_big *);
int cterm_big_input_peek(void *, bool *);
int cterm_big_input_read(void *, uint8_t *);
int cterm_big_input_forward(struct cterm_big *);
#endif
