/* Internal local-time mechanism selected by CMake. */

/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#ifndef SYNCTERM_VALUE_CONVERT_TIME_H_
#define SYNCTERM_VALUE_CONVERT_TIME_H_

#include <stdbool.h>
#include <time.h>

bool value_convert_localtime(time_t value, struct tm *result);

#endif
