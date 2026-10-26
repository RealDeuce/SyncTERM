/* Local file time for RIP file-query replies. */

/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#ifndef CTERM_RIP_TIME_H_
#define CTERM_RIP_TIME_H_

#include <stdbool.h>
#include <time.h>

bool cterm_rip_localtime(time_t value, struct tm *result);

#endif
