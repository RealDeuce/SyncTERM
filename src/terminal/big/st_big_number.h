/* BIG 1.03 ordinary numeric acquisition and finite WORD scaling. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_NUMBER_H_
#define ST_BIG_NUMBER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_big_number_context {
	uint16_t scaling;
	uint8_t resolution; /* Cached BIG resolution, not a backend mode query. */
	bool y_coordinate;
	bool auto_mode; /* Suppresses auxiliary-read timeout BEL only. */
};

enum st_big_number_phase {
	ST_BIG_NUMBER_OFF,
	ST_BIG_NUMBER_DIGITS,
	ST_BIG_NUMBER_SKIP_FIRST,
	ST_BIG_NUMBER_SKIP_SECOND,
	ST_BIG_NUMBER_AFTER_X,
	ST_BIG_NUMBER_AFTER_Y,
	ST_BIG_NUMBER_AFTER_R,
	ST_BIG_NUMBER_DONE,
};

/* Stack-owned bounded state. Initialize with begin(); do not edit fields.
 * Count and digit slots are protocol WORDs and deliberately wrap at 16 bits. */
struct st_big_number_reader {
	uint16_t digits[4], count, word;
	unsigned expression;
	enum st_big_number_phase phase;
};

struct st_big_number_result {
	bool complete;
	bool bell;
	uint16_t word;
};

void st_big_number_begin(struct st_big_number_reader *);
void st_big_number_cancel(struct st_big_number_reader *);
bool st_big_number_auxiliary(const struct st_big_number_reader *);
int32_t st_big_number_signed(uint16_t word);
/* Original software/default-precision Y result; other gates preserve word. */
uint16_t st_big_number_scale(uint16_t word, const struct st_big_number_context *);

/* Complete one ordinary numeric field, preserving unread suffix bytes.
 * An owner's expired local deadline supplies timeout=true for the CURRENT
 * byte acquisition only. It consumes a ready byte first (if present), then
 * overrides it with zero for ordinary reads or SPACE for auxiliary reads.
 * No clock, thread, notification output or transport is owned here. Rearm
 * the local deadline after each acquired byte/timeout if still incomplete.
 * The result reports the one timeout's BEL request; later polls of a DONE
 * reader return its word without repeating effects. Cancel/EOF must cancel,
 * never substitute a timeout. Invalid API calls preserve reader state.
 * Final random expansion and stored-loop evaluation belong to the caller:
 * this returns the exact encoded WORD, including an unexpanded 0x8003. */
int st_big_number_feed(struct st_big_number_reader *,
	const struct st_big_number_context *, const uint8_t *, size_t size,
	bool timeout, size_t *consumed, struct st_big_number_result *);

#endif
