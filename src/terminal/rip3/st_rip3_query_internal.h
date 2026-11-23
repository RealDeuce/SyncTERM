/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_QUERY_INTERNAL_H_
#define ST_RIP3_QUERY_INTERNAL_H_

#include "st_rip3_query.h"
#include <stdint.h>

enum st_rip3_playback_kind { ST_RIP3_PLAYBACK_JPEG, ST_RIP3_PLAYBACK_WAVE };
enum { ST_RIP3_PLAYBACK_FILENAME_MAX = 20 };

/* The session owns the media services. The filename is borrowed only during
 * the call, and its operation status is consumed within the interpreter. */
void st_rip3_query_bind_playback(struct st_rip3_query *query, void *context,
	int (*play)(void *context, enum st_rip3_playback_kind kind, const char *filename));

/* Private preflight/ownership status, consumed by the void command parser.
 * A busy evaluator retains its current program. No side effects on failure.
 * Success starts up to 64 work steps; remaining angle/ordinary work is resumed
 * by process(). Preprocessing failures are diagnosed/consumed internally. */
int st_rip3_query_start(struct st_rip3_query *query, const uint8_t *bytes, size_t length);

/* Grouped callbacks carry the original stable owner through every stage;
 * zero uses ordinary definitions. Deleting an owner never retargets writes. */
int st_rip3_query_start_owned(struct st_rip3_query *,const uint8_t *,size_t,uint64_t owner);

#endif
