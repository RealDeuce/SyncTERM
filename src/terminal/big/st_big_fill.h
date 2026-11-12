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
#endif
