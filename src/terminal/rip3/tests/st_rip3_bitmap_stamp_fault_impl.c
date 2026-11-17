/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdlib.h>
#include "st_rip3_surface.h"
#include "io_error.h"

bool st_rip3_bitmap_stamp_test_fail_allocate;
static void *
test_malloc(size_t size)
{
	if (st_rip3_bitmap_stamp_test_fail_allocate) {
		st_rip3_bitmap_stamp_test_fail_allocate = false;
		return NULL;
	}
	return malloc(size);
}

#define malloc test_malloc
#include "../st_rip3_bitmap_stamp.c"

uint32_t
st_rip3_bitmap_stamp_test_sample(uint64_t position, uint64_t span, uint32_t source)
{
	return sample(position,span,source);
}
