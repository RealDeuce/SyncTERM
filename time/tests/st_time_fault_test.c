/* Audit-only controlled-clock test; no production hooks or build edits.
 * Compile: cc -std=c17 -Wall -Wextra -Werror -Itime -Iio_error
 *   time/tests/st_time_fault_test.c -o /tmp/st_time_fault_test
 */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_time.h"
#include "io_error.h"
#include <stdio.h>

static struct timespec fake_utc;
static int utc_status = TIME_UTC;
static uint64_t fake_ms;
static int monotonic_status;

static int
audit_timespec_get(struct timespec *result, int base)
{
	(void)base;
	if (utc_status == TIME_UTC)
		*result = fake_utc;
	return utc_status;
}

int
st_time_monotonic_ms(uint64_t *result)
{
	if (monotonic_status == 0)
		*result = fake_ms;
	return monotonic_status;
}

#define timespec_get audit_timespec_get
#include "../st_time.c"

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "failed: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		return 1; \
	} \
} while (0)

int
main(void)
{
	struct timespec output = {123, 456};
	struct st_deadline deadline = {123, false};
	int64_t remaining = 456;

	utc_status = 0;
	CHECK(st_time_utc_deadline_after_ms(&output, 10) == IO_ERROR_IO);
	CHECK(output.tv_sec == 123 && output.tv_nsec == 456);
	utc_status = TIME_UTC;
	fake_utc = (struct timespec){42, 1000000000L};
	CHECK(st_time_utc_deadline_after_ms(&output, 10) == IO_ERROR_IO);
	CHECK(output.tv_sec == 123 && output.tv_nsec == 456);
	fake_utc = (struct timespec){42, 999999999L};
	CHECK(st_time_utc_deadline_after_ms(&output, 1) == 0);
	CHECK(output.tv_sec == 43 && output.tv_nsec == 999999L);
	CHECK(st_time_utc_deadline_after_ms(&output, -1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(output.tv_sec == 43 && output.tv_nsec == 999999L);
	monotonic_status = IO_ERROR_IO;
	CHECK(st_deadline_start(&deadline, 10) == IO_ERROR_IO);
	CHECK(deadline.end_ms == 123 && !deadline.infinite);
	CHECK(st_deadline_remaining_ms(&deadline, &remaining) == IO_ERROR_IO);
	CHECK(remaining == 456);
	CHECK(st_deadline_start(&deadline, -1) == 0);
	CHECK(st_deadline_remaining_ms(&deadline, &remaining) == 0 && remaining == -1);
	monotonic_status = 0;
	fake_ms = UINT64_MAX - 5;
	CHECK(st_deadline_start(&deadline, 10) == 0);
	CHECK(deadline.end_ms == UINT64_MAX && !deadline.infinite);
	CHECK(st_deadline_remaining_ms(&deadline, &remaining) == 0 && remaining == 5);
	fake_ms = 0;
	CHECK(st_deadline_remaining_ms(&deadline, &remaining) == 0 && remaining == INT64_MAX);
	fake_ms = UINT64_MAX;
	CHECK(st_deadline_remaining_ms(&deadline, &remaining) == 0 && remaining == 0);
	return 0;
}
