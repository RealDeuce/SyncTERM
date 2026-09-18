/* Windows monotonic clock. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_time.h"

#include "io_error.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int
st_time_monotonic_ms(uint64_t *result)
{
	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*result = (uint64_t)GetTickCount64();
	return 0;
}

int
st_time_monotonic_ns(uint64_t *result)
{
	LARGE_INTEGER counter;
	LARGE_INTEGER frequency;
	uint64_t seconds;
	uint64_t remainder;
	uint64_t fractional;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!QueryPerformanceCounter(&counter)
	    || !QueryPerformanceFrequency(&frequency)
	    || counter.QuadPart < 0 || frequency.QuadPart <= 0)
		return IO_ERROR_IO;
	seconds = (uint64_t)counter.QuadPart / (uint64_t)frequency.QuadPart;
	remainder = (uint64_t)counter.QuadPart % (uint64_t)frequency.QuadPart;
	/* The floating operation avoids overflow in remainder * 1000000000. */
	fractional = (uint64_t)((long double)remainder * 1000000000.0L
	    / (long double)frequency.QuadPart);
	if (fractional >= UINT64_C(1000000000))
		fractional = UINT64_C(999999999);
	if (seconds > UINT64_MAX / UINT64_C(1000000000)
	    || (seconds == UINT64_MAX / UINT64_C(1000000000)
	    && fractional > UINT64_MAX % UINT64_C(1000000000)))
		return IO_ERROR_OUT_OF_RANGE;
	*result = seconds * UINT64_C(1000000000) + fractional;
	return 0;
}
