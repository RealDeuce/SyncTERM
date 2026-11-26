/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

bool   st_rip3_templates_test_fail;
size_t st_rip3_templates_test_attempts, st_rip3_templates_test_fail_at = SIZE_MAX;

static bool
fail(void)
{
	return st_rip3_templates_test_attempts++ == st_rip3_templates_test_fail_at || st_rip3_templates_test_fail;
}

static void *
test_malloc(size_t size)
{
	return fail() ? NULL : malloc(size);
}

static void *
test_calloc(size_t count, size_t size)
{
	return fail() ? NULL : calloc(count, size);
}

#define malloc test_malloc
#define calloc test_calloc
#include "../st_rip3_templates.c"

bool
st_rip3_templates_test_defined(const struct st_rip3_templates *templates, uint64_t token, unsigned n)
{
	const struct member *m = find_member(templates, token);
	return m != NULL && m->bodies[n].defined;
}
