/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_pixels.h"
#include "io_error.h"
#include <stdbool.h>
#include <stdlib.h>

unsigned st_rip3_bitmap_capture_test_failure;

static void *
test_calloc(size_t count, size_t size)
{
	if (st_rip3_bitmap_capture_test_failure == 1) {
		st_rip3_bitmap_capture_test_failure = 0;
		return NULL;
	}
	return calloc(count,size);
}

static int
create_pixels(struct st_pixel_buffer **result, uint32_t width, uint32_t height, bool alternate)
{
	if (st_rip3_bitmap_capture_test_failure == 2) {
		st_rip3_bitmap_capture_test_failure = 0;
		return IO_ERROR_OUT_OF_MEMORY;
	}
	return st_pixel_buffer_create(result,width,height,alternate);
}

#define calloc test_calloc
#define st_pixel_buffer_create create_pixels
#include "../st_rip3_bitmap_capture.c"
