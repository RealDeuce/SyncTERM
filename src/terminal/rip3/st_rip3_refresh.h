/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_REFRESH_H_
#define ST_RIP3_REFRESH_H_

#include "st_rip3_resident_query.h"

struct st_rip3_query;

/* One raw expression and its independent execution on the session thread.
 * The evaluated source is owned by the query across prompts and delays. */
struct st_rip3_refresh {
	struct st_rip3_resident_query expression;
	struct st_rip3_query         *query;
};

int  st_rip3_refresh_define(struct st_rip3_refresh *, const uint8_t *bytes, size_t length);
void st_rip3_refresh_clear(struct st_rip3_refresh *);
int  st_rip3_refresh_start(struct st_rip3_refresh *);
bool st_rip3_refresh_pending(const struct st_rip3_refresh *);
#endif
