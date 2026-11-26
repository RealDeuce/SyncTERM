/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdlib.h>

bool   st_rip3_condition_parser_test_fail;
size_t st_rip3_condition_parser_test_allocations;

static void *
test_calloc(size_t count, size_t size)
{
	st_rip3_condition_parser_test_allocations++;
	return st_rip3_condition_parser_test_fail ? NULL : calloc(count, size);
}

#define calloc test_calloc
#include "../st_rip3_condition_parser.c"
