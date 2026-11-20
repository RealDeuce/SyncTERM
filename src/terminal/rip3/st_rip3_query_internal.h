/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_QUERY_INTERNAL_H_
#define ST_RIP3_QUERY_INTERNAL_H_

#include "st_rip3_query.h"
#include <stdint.h>

/* Private preflight/ownership status, consumed by the void command parser.
 * A busy evaluator retains its current program. No side effects on failure.
 * Success starts up to 64 work steps; remaining angle/ordinary work is resumed
 * by process(). Preprocessing failures are diagnosed/consumed internally. */
int st_rip3_query_start(struct st_rip3_query *query, const uint8_t *bytes, size_t length);

#endif
