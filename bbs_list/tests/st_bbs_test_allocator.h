/* Test-only allocation failure control for the BBS list implementation. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_BBS_TEST_ALLOCATOR_H_
#define ST_BBS_TEST_ALLOCATOR_H_

#include <stddef.h>

void st_bbs_test_fail_after(size_t allocation);
void st_bbs_test_disable_failure(void);
void *st_bbs_test_calloc(size_t count, size_t size);
char *st_bbs_test_strdup(const char *string);

#endif
