/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_language_internal.h"
#include "st_language_config.h"

#include <stdlib.h>

int
st_language_platform(struct st_language_preferences *result)
{
#if ST_LANGUAGE_HAVE_MACOS_PREFERENCES
	int status = st_language_macos(result);

	if (status != 0 || result->count != 0)
		return status;
#endif
	return st_language_environment(result, getenv("LANGUAGE"),
	    getenv("LC_ALL"), getenv("LC_MESSAGES"), getenv("LANG"));
}
