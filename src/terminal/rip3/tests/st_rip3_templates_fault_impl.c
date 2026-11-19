/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdlib.h>

bool st_rip3_templates_test_fail;
static void *test_malloc(size_t size) { return st_rip3_templates_test_fail ? NULL : malloc(size); }
static void *test_calloc(size_t count, size_t size) { return st_rip3_templates_test_fail ? NULL : calloc(count,size); }
#define malloc test_malloc
#define calloc test_calloc
#include "../st_rip3_templates.c"
