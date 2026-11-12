/* Private BIG terminal state; native fixtures may inspect serialized owners. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_BIG_INTERNAL_H_
#define CTERM_BIG_INTERNAL_H_
#include "cterm_big.h"
#include "st_big_numeric.h"
#include "st_big_state.h"
struct cterm_big {
	struct cterminal *terminal;
	struct st_graphics_parser *parser;
	struct st_big_numeric numeric;
	struct st_big_state state;
	uint64_t pair_deadline, pair_sample;
	bool pair_wait, feeding, reset_pending;
	uint8_t warned[32];
	uint16_t console_fonts[3];
	bool console_font_valid[3];
};
#endif
