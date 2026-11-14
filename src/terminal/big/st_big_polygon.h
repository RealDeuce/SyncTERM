/* BIG60 raw polygon records, row scheduling and span dispatch. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_POLYGON_H_
#define ST_BIG_POLYGON_H_
#include <stdbool.h>
#include <stdint.h>
struct st_big_state;
struct st_big_producer;
struct st_big_polygon_sink {
	void *context;
	/* Optional synchronous raw device tuple dispatch. Device colour fields
	 * publish after success; normal restore publishes DDO cache before it. */
	int (*color)(void *, uint16_t tuple);
	/* Physical WORD endpoints, unsigned X order. false selects the clipped
	 * opaque pattern bar; true selects the unclipped raw thin-line route.
	 * No origin/CP/thickness change belongs to this scan converter. */
	int (*span)(void *, bool raw_line, const uint16_t points[4]);
};
/* Raw DDO0b03, not a sector/public command or the stage admission guard.
 * Entry count/capacity are captured. Y80xx markers bound live record reads;
 * declared count places intersection scratch immediately after that many
 * four-byte records, within the SAME borrowed point storage. No allocation.
 * Current native extent/binding is checked at reached reads/writes. Missing
 * markers, escaped scratch/styles, short counts, IDIV faults and a proven
 * nonterminating row-state cycle fail without normal colour restoration.
 * IN_PROGRESS from a callback is a protocol error. */
int st_big_polygon_fill(struct st_big_state *, const struct st_big_producer *,
	const struct st_big_polygon_sink *);
#endif
