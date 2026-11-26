/* POSIX local file time for RIP file-query replies. */

/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "cterm_rip_time.h"

bool
cterm_rip_localtime(time_t value, struct tm *result)
{
	return result != NULL && localtime_r(&value, result) != NULL;
}
