/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_INPUT_H_
#define ST_RIP3_INPUT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_input;

/* Borrowed nonblocking source/sink, copied at creation. One session thread owns
 * this driver. The source owns its receive/file queue and retains all bytes
 * until next() takes them; arrivals during a delay stay in that queue. False
 * means no byte is currently available, not a parser/session error. Source
 * connection/EOF/error policy remains with its owner.
 *
 * now_ns() is a monotonic nanosecond counter modulo 2^64; elapsed intervals
 * between observations must fit that counter. consume() may begin a delay,
 * including from a complete command callback inside the stream framer. No
 * callback may reenter process(), reset/destroy this driver or change bindings.
 * The queue/context and downstream parser must outlive the driver. */
struct st_rip3_input_source {
	void *context;
	uint64_t (*now_ns)(void *context);
	bool (*next)(void *context, uint8_t *byte);
	void (*consume)(void *context, uint8_t byte);
};

int st_rip3_input_create(struct st_rip3_input **result,
	const struct st_rip3_input_source *source);
void st_rip3_input_destroy(struct st_rip3_input *input);
/* Cancel the gate only. The borrowed queue/parser are neither read nor reset. */
void st_rip3_input_reset(struct st_rip3_input *input);
/* Replace the gate with ticks/60 seconds starting at the current clock value.
 * Nanosecond duration rounds upward, so a command cannot resume early. Zero
 * clears the gate. Consecutive in-stream commands cannot overlap: the pump
 * checks the gate before taking the next byte. */
void st_rip3_input_delay(struct st_rip3_input *input, uint32_t ticks);
/* Remaining relative wait, zero when ready. Does not take any source bytes;
 * callers can use it to bound their normal poll timeout without sleeping here. */
uint64_t st_rip3_input_wait_ns(const struct st_rip3_input *input);
/* Deliver at most budget bytes, stopping at a delay or empty source. Checks
 * again after each consumed byte, so a command-delimiter callback can pause
 * before any following RIP or raw-text byte is taken. No processing status is
 * returned to the terminal/session failure path. */
void st_rip3_input_process(struct st_rip3_input *input, size_t budget);

#endif
