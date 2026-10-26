/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef STR_LIST_TEST_ALLOCATOR_H_
#define STR_LIST_TEST_ALLOCATOR_H_

#include <stddef.h>

void str_list_test_fail_after(size_t allocation);
void str_list_test_disable_failure(void);
size_t str_list_test_outstanding(void);

void *str_list_test_malloc(size_t size);
void *str_list_test_realloc(void *pointer, size_t size);
void str_list_test_free(void *pointer);
char *str_list_test_strdup(const char *string);
char *str_list_test_strndup(const char *string, size_t length);

#endif
