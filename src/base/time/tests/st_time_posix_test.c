/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "st_time.h"
#include "st_time_config.h"
#include "io_error.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static int clock_status;
static int clock_error;
static struct timespec clock_value = {2, 123000000};

static int
test_clock_gettime(clockid_t clock_id, struct timespec *value)
{
	(void)clock_id;
	errno = clock_error;
	if (clock_status == 0)
		*value = clock_value;
	return clock_status;
}

#define clock_gettime test_clock_gettime
#include "../st_time_posix.c"
#undef clock_gettime

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "failed: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		return 1; \
	} \
} while (0)

int
main(void)
{
	uint64_t value = 7;

	CHECK(st_time_monotonic_ns(&value) == 0 && value == UINT64_C(2123000000));
	CHECK(st_time_monotonic_ms(&value) == 0 && value == 2123);
	clock_status = -1;
	clock_error = EINVAL;
	CHECK(st_time_monotonic_ns(&value) == IO_ERROR_INVALID_ARGUMENT && value == 2123);
	CHECK(st_time_monotonic_ms(&value) == IO_ERROR_INVALID_ARGUMENT && value == 2123);
	/* A native failure must never become success without publishing a time. */
	clock_error = 0;
	CHECK(st_time_monotonic_ns(&value) == IO_ERROR_IO && value == 2123);
	CHECK(st_time_monotonic_ms(&value) == IO_ERROR_IO && value == 2123);
#if ST_TIME_HAVE_PROCESS_CPU_CLOCK
	CHECK(st_time_process_cpu_ns(&value) == IO_ERROR_IO && value == 2123);
#endif
	clock_status = 0;
	clock_value.tv_nsec = 1000000000L;
	CHECK(st_time_monotonic_ns(&value) == IO_ERROR_OUT_OF_RANGE && value == 2123);
	CHECK(st_time_monotonic_ms(&value) == IO_ERROR_OUT_OF_RANGE && value == 2123);
	return 0;
}
