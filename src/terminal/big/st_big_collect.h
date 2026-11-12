/* BIG < collection state: local keyboard bytes and counted zone selection. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_COLLECT_H_
#define ST_BIG_COLLECT_H_
#include "st_big_control.h"
#include <stdbool.h>
#include <stdint.h>
struct st_big_collect {
	uint16_t type, options;
	bool active;
	int error;
};
struct st_big_collect_sink {
	void *context;
	/* Ordered native console requests, not bytes for an ANSI/VT52 parser.
	 * Synchronous; IN_PROGRESS is a protocol error. */
	int (*echo)(void *, uint8_t);
};
/* EOF/reset drops only frame state; the caller owns pending cancellation. */
void st_big_collect_cancel(struct st_big_collect *);
/* Append-CR is committed by the caller immediately after its first read.
 * Begin follows the third read; retain all old payload bytes/append state. */
int st_big_collect_begin(struct st_big_collect *, struct st_big_control *, uint16_t type, uint16_t options);
/* One DOS/getch byte, with no extended-key decoding. Errors latch and retain
 * completed count/store/echo prefixes. EOF/reset discards only frame state. */
int st_big_collect_key(struct st_big_collect *, struct st_big_control *, uint8_t,
	const struct st_big_collect_sink *);
/* Caller erases the pointer/restores the current cached mode first. Signed
 * inclusive first hit; strict miss requests BEL/retry, default miss uses47.
 * The selected unsigned length is not re-clamped to the producer's80. */
int st_big_collect_zone(struct st_big_collect *, struct st_big_control *, uint16_t x, uint16_t y,
	const struct st_big_collect_sink *, bool *retry);
/* Explicit native direction event1..4, current X6 WORD steps and the original
 * asymmetric signed clamps. Other event values leave coordinates alone. */
void st_big_collect_move(const struct st_big_control *, uint16_t max_x, uint16_t max_y,
	unsigned direction, uint16_t *x, uint16_t *y);
#endif
