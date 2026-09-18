/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "value_convert_time.h"

bool
value_convert_localtime(time_t value, struct tm *result)
{
	return result != NULL && localtime_r(&value, result) != NULL;
}
