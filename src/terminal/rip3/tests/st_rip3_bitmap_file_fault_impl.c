/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_bitmap.h"
#include "io_error.h"
#include <stdbool.h>
#include <stdlib.h>

bool st_rip3_bitmap_file_test_fail_allocate;
bool st_rip3_bitmap_file_test_fail_decode;

static void *
test_malloc(size_t size)
{
	if (st_rip3_bitmap_file_test_fail_allocate) {
		st_rip3_bitmap_file_test_fail_allocate = false;
		return NULL;
	}
	return malloc(size);
}

static int
test_decode(struct st_rip3_bitmap **result, const void *bytes, size_t size)
{
	if (st_rip3_bitmap_file_test_fail_decode) {
		st_rip3_bitmap_file_test_fail_decode = false;
		return IO_ERROR_OUT_OF_MEMORY;
	}
	return st_rip3_bitmap_decode(result,bytes,size);
}

#define malloc test_malloc
#define st_rip3_bitmap_decode test_decode
#include "../st_rip3_bitmap_file.c"
