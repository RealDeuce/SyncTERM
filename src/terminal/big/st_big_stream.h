/* BIG serial auxiliary/text acquisition; loop reuse and local deadlines. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_STREAM_H_
#define ST_BIG_STREAM_H_
#include "st_big_numeric.h"
#include "st_big_control.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { ST_BIG_TEXT_CAPACITY = 129 };
struct st_big_text {
	uint8_t bytes[ST_BIG_TEXT_CAPACITY];
	uint16_t count, active;
};
enum st_big_stream_phase { ST_BIG_STREAM_OFF, ST_BIG_STREAM_FLOW,
	ST_BIG_STREAM_TEXT_TEST, ST_BIG_STREAM_READ, ST_BIG_STREAM_DONE, ST_BIG_STREAM_FAILED };
struct st_big_stream_reader {
	enum st_big_stream_phase phase;
	bool text, sampled, returned;
	uint8_t byte;
	uint64_t deadline, sample;
	struct st_big_flow_reader flow;
	int error;
};
void st_big_stream_cancel(struct st_big_stream_reader *);
int st_big_stream_aux(struct st_big_stream_reader *, struct st_big_numeric *);
/* Entry bookkeeping precedes reuse tests and any first-pass XON wait.
 * Text storage remains application-owned, including stale timeout suffixes. */
int st_big_stream_text(struct st_big_stream_reader *, struct st_big_numeric *,
	struct st_big_text *, const struct st_big_control *);
/* Optional text/control/output arguments are required only for text mode.
 * ready bytes are handled before testing the saved pre-read clock sample.
 * RAW timeout returns SPACE; TEXT timeout does not append a terminator.
 * Completion and errors latch; cancel/EOF never manufacture a result. */
int st_big_stream_poll(struct st_big_stream_reader *, struct st_big_numeric *,
	struct st_big_text *, struct st_big_control *, const struct st_big_control_sink *,
	const uint8_t *, size_t, size_t *accepted, size_t *output_budget, bool *complete);
#endif
