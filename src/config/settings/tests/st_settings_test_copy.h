/* Test-only settings-store copy failure control. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_SETTINGS_TEST_COPY_H_
#define ST_SETTINGS_TEST_COPY_H_

#include "st_settings.h"

#include <stdbool.h>
#include <stddef.h>

void st_settings_test_fail_copy_after(size_t copy);
void st_settings_test_disable_copy_failure(void);
bool st_settings_test_copy(struct st_settings *result,
	const struct st_settings *source);

#endif
