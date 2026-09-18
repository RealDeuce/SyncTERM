/* Monotonic elapsed-time and C11 UTC deadline helpers. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_TIME_H_
#define ST_TIME_H_

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

struct st_deadline {
	uint64_t end_ms;
	bool infinite;
};

int st_time_monotonic_ms(uint64_t *result);
int st_time_monotonic_ns(uint64_t *result);
/* timeout_ms is -1 for infinity or a nonnegative relative duration. */
int st_deadline_start(struct st_deadline *result, int64_t timeout_ms);
/* Returns -1 through result for an infinite deadline. */
int st_deadline_remaining_ms(const struct st_deadline *deadline,
	int64_t *result);
/* Builds an absolute TIME_UTC deadline for st_condition_timedwait(). */
int st_time_utc_deadline_after_ms(struct timespec *result,
	int64_t timeout_ms);

#endif
