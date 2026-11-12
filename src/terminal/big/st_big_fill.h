/* BIG fill installation and application selection, references11/50/54/83. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_FILL_H_
#define ST_BIG_FILL_H_
#include "st_big_state.h"
/* Public BGI wrappers: validation/publication precede lower installation.
 * Custom source is borrowed for eight bytes and read independently for
 * library and device copies. Old BGI errors do not suppress valid calls. */
int st_big_fill_builtin(struct st_big_state *, uint16_t style, uint16_t color);
int st_big_fill_custom(struct st_big_state *, const uint8_t pattern[8], uint16_t color);
/* The common helper clamps CURRENT application selection, then performs
 * separate built-in/custom tests. select() supplies A's preceding store. */
int st_big_fill_apply(struct st_big_state *);
int st_big_fill_select(struct st_big_state *, uint16_t requested);
/* X1's frame locals and fixed eight-read loop. Slot/alter remain live
 * between serialized row calls; changing alter never shortens that loop. */
struct st_big_fill_edit {
	uint16_t slot, alter;
	size_t row;
	bool active, editing;
	int error;
};
int st_big_fill_edit_begin(struct st_big_fill_edit *, uint16_t slot, uint16_t alter);
int st_big_fill_edit_row(struct st_big_fill_edit *, struct st_big_state *, uint16_t value);
int st_big_fill_edit_finish(struct st_big_fill_edit *, struct st_big_state *);
/* Cancels frame ownership only; accepted application row stores remain. */
void st_big_fill_edit_cancel(struct st_big_fill_edit *);
#endif
