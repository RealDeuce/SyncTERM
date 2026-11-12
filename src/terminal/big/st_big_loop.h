/* BIG 1.03 stored numeric evaluation; transport and local waits stay outside. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_LOOP_H_
#define ST_BIG_LOOP_H_

#include <stdbool.h>
#include <stdint.h>

#define ST_BIG_LOOP_CAPACITY 2048

/* Protocol WORD fields retain the original signed comparisons and wrapping.
 * Original symbols: active=lp_flg, index=lp_eff_ct, current/to/step/delay/
 * origin=lp1..lp5, varying_count=lp_x, saved=lp_was, pass=lp_2nd,
 * varying_total=lp_1st, no_parameters=lp_no_prams, count=lp_num_prams.
 * The serialized terminal owner may change fields between callback returns;
 * it must not change this state concurrently or reenter the evaluator. */
struct st_big_loop {
	uint16_t words[ST_BIG_LOOP_CAPACITY];
	uint16_t active, index, count;
	uint16_t current, to, step, delay, origin;
	uint16_t varying_count, saved, pass, varying_total;
	uint16_t type, command, no_parameters, random_suppression, first_text;
	bool command_read; /* Original bad_cmd: a numeric/text reader was used. */
};

enum st_big_loop_reader_phase {
	ST_BIG_LOOP_READER_OFF,
	ST_BIG_LOOP_READER_ENTRY,
	ST_BIG_LOOP_READER_DELAY,
	ST_BIG_LOOP_READER_CALCULATE,
	ST_BIG_LOOP_READER_KEY,
	ST_BIG_LOOP_READER_RANDOM,
	ST_BIG_LOOP_READER_DONE,
	ST_BIG_LOOP_READER_FAILED,
};

struct st_big_loop_reader {
	enum st_big_loop_reader_phase phase;
	uint16_t word;
	bool delay_seen, supplied;
	int error;
};

struct st_big_loop_sink {
	void *context;
	/* Required even for delay0/negative delay. first is true once per
	 * index-zero acquisition; retries receive the current delay WORD. The
	 * owner samples local monotonic time and probes keys without consuming
	 * them here. Return IN_PROGRESS while waiting, zero when ready. */
	int (*delay)(void *, uint16_t duration, bool first);
	/* After calculation, before endpoint/index work. On success cancel
	 * clears active but does not abandon the calculated value or tail.
	 * IN_PROGRESS retries only this checkpoint; no result is accepted. */
	int (*key)(void *, bool *cancel);
	/* Final r expansion, after endpoint/index/text-marker work. May yield;
	 * retrying never repeats calculation, key or boundary effects. */
	int (*random)(void *, uint16_t *word);
};

void st_big_loop_init(struct st_big_loop *);
/* Header normalization only: do not copy current to origin early. */
uint16_t st_big_loop_setup_step(uint16_t from, uint16_t to, uint16_t step);
/* Publish counters/flags after table load, preserving table suffix and other
 * shared state. Then the caller performs its setup flow operation BEFORE
 * scan(). Both require count1..2048; no arity/record inference is made. */
int st_big_loop_publish(struct st_big_loop *);
int st_big_loop_scan(struct st_big_loop *);

void st_big_loop_reader_begin(struct st_big_loop_reader *);
void st_big_loop_reader_cancel(struct st_big_loop_reader *);
/* All sink callbacks are required. Zero/supplied=false declines this field
 * to ordinary acquisition. Non-one nonzero active returns supplied WORD0
 * without delay/table/index work. Completed/declined readers are latched
 * until begin(); errors latch without rolling back earlier effects.
 * Output word changes only on successful supplied completion. Invalid API
 * calls preserve reader/loop state. Callback views/state are not reentrant.
 * Invalid table count/index is rejected before an out-of-bounds access. */
int st_big_loop_poll(struct st_big_loop_reader *, struct st_big_loop *,
	const struct st_big_loop_sink *, uint16_t *word, bool *supplied);

#endif
