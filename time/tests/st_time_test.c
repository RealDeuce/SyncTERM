/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "st_time.h"

#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed: %s:%d\n", \
    __FILE__, __LINE__); return EXIT_FAILURE; } } while (0)

int
main(void)
{
	uint64_t first;
	uint64_t second;
	uint64_t first_ns;
	uint64_t second_ns;
	struct st_deadline deadline;
	struct timespec utc;
	int64_t remaining;

	CHECK(st_time_monotonic_ms(&first) == 0);
	CHECK(st_time_monotonic_ms(&second) == 0);
	CHECK(second >= first);
	CHECK(st_time_monotonic_ns(&first_ns) == 0);
	CHECK(st_time_monotonic_ns(&second_ns) == 0);
	CHECK(second_ns >= first_ns);
	CHECK(st_time_monotonic_ms(NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_time_monotonic_ns(NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_deadline_start(&deadline, -1) == 0);
	CHECK(st_deadline_remaining_ms(&deadline, &remaining) == 0);
	CHECK(remaining == -1);
	CHECK(st_deadline_start(&deadline, 0) == 0);
	CHECK(st_deadline_remaining_ms(&deadline, &remaining) == 0);
	CHECK(remaining == 0);
	CHECK(st_deadline_start(&deadline, -2) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_time_utc_deadline_after_ms(&utc, 1500) == 0);
	CHECK(utc.tv_nsec >= 0 && utc.tv_nsec < 1000000000L);
	CHECK(st_time_utc_deadline_after_ms(NULL, 0)
	    == IO_ERROR_INVALID_ARGUMENT);
	return EXIT_SUCCESS;
}
