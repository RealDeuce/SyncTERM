/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

bool st_rip3_drawing_test_fail_calloc;
unsigned st_rip3_drawing_test_calloc_countdown;

static void *
test_calloc(size_t count, size_t size)
{
	if (st_rip3_drawing_test_fail_calloc) {
		st_rip3_drawing_test_fail_calloc = false;
		return NULL;
	}
	if (st_rip3_drawing_test_calloc_countdown != 0
	    && --st_rip3_drawing_test_calloc_countdown == 0)
		return NULL;
	return calloc(count, size);
}

/* No production allocation hook; exercise the actual drawing implementation. */
#define calloc test_calloc
#include "../st_rip3_draw.c"
