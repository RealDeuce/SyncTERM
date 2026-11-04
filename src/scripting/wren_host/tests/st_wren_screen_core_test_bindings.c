/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_wren_screen_test_calls.h"
struct st_wren_screen_fault st_wren_test_screen_fault;
bool st_wren_test_no_pixels;
#include <stdlib.h>
static void *
test_screen_calloc(size_t count, size_t size)
{
	if (st_wren_test_screen_fault.name != NULL
	    && strcmp(st_wren_test_screen_fault.name, "binding.captureStorage") == 0) {
		++st_wren_test_screen_fault.calls;
		return NULL;
	}
	return calloc(count, size);
}
#define calloc test_screen_calloc
#include "../wren_bind_core.c"
