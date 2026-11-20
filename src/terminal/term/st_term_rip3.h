/* Connected RIP3 owner; the borrowed CTerm handles ordinary ANSI text only. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_TERM_RIP3_H_
#define ST_TERM_RIP3_H_

#include "st_term.h"

struct st_term_rip3;
int st_term_rip3_prepare_console(st_console_t *console);
int st_term_rip3_create(struct st_term_rip3 **result,
	const struct st_term_config *config, st_cterm_t *text,
	st_cterm_output_callback_t output, void *output_context);
void st_term_rip3_destroy(struct st_term_rip3 *rip);
/* All incoming-data, continuation and service failures are nonfatal here. */
void st_term_rip3_write(struct st_term_rip3 *rip, const uint8_t *bytes, size_t length);
void st_term_rip3_poll(struct st_term_rip3 *rip);
void st_term_rip3_cancel(struct st_term_rip3 *rip);
bool st_term_rip3_paused(const struct st_term_rip3 *rip);

#endif
