/* Deterministic failure at processor allocation/store boundaries. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdlib.h>

#include "st_rip3_variables.h"

bool st_rip3_user_variables_test_fail_allocation;
int  st_rip3_user_variables_test_store_status;

static void *
processor_calloc(size_t count, size_t size)
{
	return st_rip3_user_variables_test_fail_allocation ? NULL : calloc(count, size);
}

static int
processor_set(struct st_rip3_variables *variables, const char *name, size_t name_length, const uint8_t *value,
    size_t value_length)
{
	if (st_rip3_user_variables_test_store_status != 0)
		return st_rip3_user_variables_test_store_status;
	return st_rip3_variables_set(variables, name, name_length, value, value_length);
}

#define calloc                processor_calloc
#define st_rip3_variables_set processor_set
#include "../st_rip3_user_variables.c"
