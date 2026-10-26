/* POSIX monotonic clock. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_time.h"
#include "st_time_config.h"

#include "io_error.h"

#include <errno.h>
#include <stdint.h>
#include <time.h>

int
st_time_process_cpu_ns(uint64_t *result)
{
#if ST_TIME_HAVE_PROCESS_CPU_CLOCK
	struct timespec now;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now) != 0) {
		int status = io_error_from_errno(errno);

		return status != 0 ? status : IO_ERROR_IO;
	}
	if (now.tv_sec < 0 || now.tv_nsec < 0 || now.tv_nsec >= 1000000000L
	    || (uint64_t)now.tv_sec >= UINT64_MAX / UINT64_C(1000000000))
		return IO_ERROR_OUT_OF_RANGE;
	*result = (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
	return 0;
#else
	clock_t ticks;
	uint64_t seconds;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	/* POSIX clock() measures process CPU, not elapsed time. */
	ticks = clock();
	if (ticks == (clock_t)-1 || ticks < 0)
		return IO_ERROR_IO;
	seconds = (uint64_t)ticks / CLOCKS_PER_SEC;
	if (seconds >= UINT64_MAX / UINT64_C(1000000000))
		return IO_ERROR_OUT_OF_RANGE;
	*result = seconds * UINT64_C(1000000000)
	    + (uint64_t)((long double)((uint64_t)ticks % CLOCKS_PER_SEC)
	    * 1000000000.0L / CLOCKS_PER_SEC);
	return 0;
#endif
}

int
st_time_monotonic_ns(uint64_t *result)
{
	struct timespec now;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
		int status = io_error_from_errno(errno);

		return status != 0 ? status : IO_ERROR_IO;
	}
	if (now.tv_sec < 0 || now.tv_nsec < 0 || now.tv_nsec >= 1000000000L
	    || (uint64_t)now.tv_sec > UINT64_MAX / UINT64_C(1000000000)
	    || ((uint64_t)now.tv_sec
	    == UINT64_MAX / UINT64_C(1000000000)
	    && (uint64_t)now.tv_nsec
	    > UINT64_MAX % UINT64_C(1000000000)))
		return IO_ERROR_OUT_OF_RANGE;
	*result = (uint64_t)now.tv_sec * UINT64_C(1000000000)
	    + (uint64_t)now.tv_nsec;
	return 0;
}

int
st_time_monotonic_ms(uint64_t *result)
{
	uint64_t nanoseconds;
	int status;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_time_monotonic_ns(&nanoseconds);
	if (status != 0)
		return status;
	*result = nanoseconds / UINT64_C(1000000);
	return 0;
}
