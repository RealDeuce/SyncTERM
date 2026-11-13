/* Original indexed boundary flood, BIG24/59; native bounded workspace. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_FLOOD_H_
#define ST_BIG_FLOOD_H_
#include "st_big_state.h"
#include "st_big_memory.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct st_big_flood_record {
	uint16_t inverse_y, x, address;
	uint8_t bit, flags;
};
struct st_big_flood {
	struct st_big_flood_record *records;
	size_t storage_count, capacity, count;
	bool overflow, found_directional;
};
/* Initializes the workspace without clearing borrowed records. Bindings
 * must remain valid for the operation. The explicit native capacity replaces
 * the unsafe original absolute SS:BP guard, not a DOS heap-placement model. */
int st_big_flood_bind(struct st_big_flood *, struct st_big_flood_record *, size_t storage_count, size_t capacity);
/* One synchronous operation, no periodic work-budget yields. All calls and
 * callbacks are serialized and must not reenter the operation. Native errors
 * retain accepted prefixes. Queue overflow continues pending work and reports
 * BGI-7 only on normal finish. Public/DDO/device points are unchanged. */
int st_big_flood_fill(struct st_big_flood *, struct st_big_state *, const struct st_big_memory_sink *,
	uint16_t x, uint16_t y, uint16_t boundary);
#endif
