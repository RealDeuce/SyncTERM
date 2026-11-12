/* BIG numeric sources at the shared graphics-parser boundary. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_NUMERIC_H_
#define ST_BIG_NUMERIC_H_

#include "st_big_loop.h"
#include "st_big_number.h"
#include "st_graphics_parser.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Portable local-time policy: 18.2 nominal wait units per second. The
 * original reader's strict >73 threshold first expires at this ns offset.
 * No foreign tick counter, midnight state, IRQ or hardware phase is owned. */
#define ST_BIG_NUMERIC_TIMEOUT_NS UINT64_C(4010989011)

struct st_big_numeric_io {
	void *context;
	/* NULL selects st_time_monotonic_ns. Clock and peek are synchronous;
	 * peek does not consume a key. Both key functions may be NULL for none. */
	int (*now_ns)(void *, uint64_t *);
	int (*peek_key)(void *, bool *ready);
	/* Called only after ready. May return IN_PROGRESS; retries do not repeat
	 * peek. Only returned bytes03/18 cancel stored evaluation. */
	int (*read_key)(void *, uint8_t *);
	void (*bell)(void *);
};

enum st_big_numeric_phase {
	ST_BIG_NUMERIC_OFF,
	ST_BIG_NUMERIC_SELECT,
	ST_BIG_NUMERIC_WIRE,
	ST_BIG_NUMERIC_WIRE_STORED,
	ST_BIG_NUMERIC_WIRE_RANDOM,
	ST_BIG_NUMERIC_DONE_SOURCE,
	ST_BIG_NUMERIC_DONE_WIRE,
	ST_BIG_NUMERIC_FAILED,
};

struct st_big_numeric {
	/* Application-owned mutable state, retained across field cancellation.
	 * Rendering I/R defaults must not implicitly reset these owners. */
	struct st_big_loop loop;
	struct st_big_number_context context;
	uint32_t random_state;
	uint16_t random_from, random_to;
	struct st_big_numeric_io io;
	bool initialized;
	/* Per-field state: owned by these functions, not directly edited. */
	struct {
		struct st_big_number_reader number;
		struct st_big_loop_reader stored;
		enum st_big_numeric_phase phase;
		uint64_t wire_deadline, delay_start, last_sample;
		bool wire_wait, delay_wait, sample_valid, key_pending, y_coordinate;
		uint16_t word;
		int error;
	} field;
};

/* Init resets application state to the loaded numeric defaults (seed1,
 * range0..199, cached resolution2). Invalid IO leaves the object unchanged.
 * All calls are serialized by the terminal owner; callbacks must not reenter. */
int st_big_numeric_init(struct st_big_numeric *, const struct st_big_numeric_io *);
/* Ends a field on EOF/reset/cancel without synthesizing input or rolling back
 * already published loop/RNG effects. Reinitialization is a separate owner. */
void st_big_numeric_cancel(struct st_big_numeric *);
/* Current local deadline, when waiting for a byte or index-zero loop delay.
 * RNG/key waits have no deadline. Zero is a valid timestamp. */
bool st_big_numeric_deadline(const struct st_big_numeric *, uint64_t *);

/* Production shared-parser callbacks. Sink context is st_big_numeric*;
 * other command/text callbacks can reach their owner through io.context.
 * field() must run first; nonnumeric fields end the numeric field lifetime.
 * number() selects supplied versus wire input once. read_number() retains
 * unread suffixes, final random waits and exact accepted counts on errors. */
int st_big_numeric_field(void *, const struct st_graphics_command_view *,
	const struct st_graphics_parameter *);
int st_big_numeric_number(void *, const struct st_graphics_command_view *,
	const struct st_graphics_parameter *, int *value, bool *supplied);
int st_big_numeric_read_number(void *, const struct st_graphics_command_view *,
	const struct st_graphics_parameter *, const uint8_t *, size_t,
	size_t *accepted, int *value, bool *complete);

#endif
