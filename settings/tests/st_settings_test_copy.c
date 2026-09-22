/* Test-only settings-store copy failure control. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_settings_test_copy.h"

#include <stdint.h>

static size_t attempt;
static size_t failure = SIZE_MAX;

void
st_settings_test_fail_copy_after(size_t copy)
{
	attempt = 0;
	failure = copy;
}

void
st_settings_test_disable_copy_failure(void)
{
	attempt = 0;
	failure = SIZE_MAX;
}

bool
st_settings_test_copy(struct st_settings *result,
	const struct st_settings *source)
{
	if (attempt++ == failure)
		return false;
	return st_settings_copy(result, source);
}
