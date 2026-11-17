/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdlib.h>

bool st_rip3_wallpaper_test_fail_allocate;

static void *
test_malloc(size_t size)
{
	if (st_rip3_wallpaper_test_fail_allocate) {
		st_rip3_wallpaper_test_fail_allocate = false;
		return NULL;
	}
	return malloc(size);
}

#define malloc test_malloc
#include "../st_rip3_wallpaper.c"
