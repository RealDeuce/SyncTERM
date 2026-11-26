/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stddef.h>
#include <stdlib.h>

unsigned st_rip3_ellipse_test_realloc_countdown;

static void *
test_realloc(void *pointer, size_t size)
{
	if (st_rip3_ellipse_test_realloc_countdown != 0 && --st_rip3_ellipse_test_realloc_countdown == 0)
		return NULL;
	return realloc(pointer, size);
}

/* Exercise actual mesh allocation/cleanup without a production hook. */
#define realloc test_realloc
#include "../st_rip3_ellipse.c"
