/* BIG pre-ANSI owner; protocol state stays outside the base ANSI parser. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_BIG_H_
#define CTERM_BIG_H_
#include "st_big_numeric.h"
#include "st_big_state.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct cterminal;
enum { CTERM_BIG_PALETTE_FIRST = 800 };
struct cterm_big {
	struct cterminal *terminal;
	struct st_graphics_parser *parser;
	struct st_big_numeric numeric;
	struct st_big_state state;
	uint64_t pair_deadline, pair_sample;
	bool pair_wait, feeding, reset_pending;
	uint8_t warned[32];
};
int cterm_big_create(struct cterminal *);
void cterm_big_destroy(struct cterm_big *);
void cterm_big_reset(struct cterm_big *);
int cterm_big_feed(struct cterm_big *, const void *, size_t, size_t *);
int cterm_big_poll(struct cterm_big *);
int cterm_big_finish(struct cterm_big *);
bool cterm_big_paused(const struct cterm_big *);
#endif
