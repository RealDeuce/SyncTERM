/* Monotonic elapsed-time and C11 UTC deadline helpers. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_time.h"

#include "io_error.h"

#include <limits.h>
#include <stdint.h>

int
st_deadline_start(struct st_deadline *result, int64_t timeout_ms)
{
	uint64_t now;
	uint64_t duration;
	int status;

	if (result == NULL || timeout_ms < -1)
		return IO_ERROR_INVALID_ARGUMENT;
	if (timeout_ms < 0) {
		*result = (struct st_deadline){0, true};
		return 0;
	}
	status = st_time_monotonic_ms(&now);
	if (status != 0)
		return status;
	duration = (uint64_t)timeout_ms;
	*result = (struct st_deadline){
		.end_ms = duration > UINT64_MAX - now ? UINT64_MAX : now + duration,
		.infinite = false,
	};
	return 0;
}

int
st_deadline_remaining_ms(const struct st_deadline *deadline, int64_t *result)
{
	uint64_t now;
	uint64_t remaining;
	int status;

	if (deadline == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (deadline->infinite) {
		*result = -1;
		return 0;
	}
	status = st_time_monotonic_ms(&now);
	if (status != 0)
		return status;
	remaining = deadline->end_ms > now ? deadline->end_ms - now : 0;
	*result = remaining > INT64_MAX ? INT64_MAX : (int64_t)remaining;
	return 0;
}

int
st_time_utc_deadline_after_ms(struct timespec *result, int64_t timeout_ms)
{
	struct timespec now;
	uint64_t seconds;
	uint64_t total;
	long nanoseconds;
	time_t converted;

	if (result == NULL || timeout_ms < 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (timespec_get(&now, TIME_UTC) != TIME_UTC || now.tv_sec < 0
	    || now.tv_nsec < 0 || now.tv_nsec >= 1000000000L)
		return IO_ERROR_IO;
	seconds = (uint64_t)timeout_ms / 1000;
	nanoseconds = now.tv_nsec + (long)(timeout_ms % 1000) * 1000000L;
	if (nanoseconds >= 1000000000L) {
		nanoseconds -= 1000000000L;
		seconds++;
	}
	if (seconds > UINT64_MAX - (uint64_t)now.tv_sec)
		return IO_ERROR_OUT_OF_RANGE;
	total = (uint64_t)now.tv_sec + seconds;
	converted = (time_t)total;
	if (converted < 0 || (uint64_t)converted != total)
		return IO_ERROR_OUT_OF_RANGE;
	*result = (struct timespec){converted, nanoseconds};
	return 0;
}
