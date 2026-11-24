/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_pixels.h"
#include "st_rip3_ports.h"
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
capture_pixels(const struct st_rip3_ports *ports, unsigned number,
    const struct st_rip3_rect *area, bool viewport, bool alternate, struct st_pixel_buffer **result)
{
	if (st_rip3_bitmap_capture_test_failure == 2) {
		st_rip3_bitmap_capture_test_failure = 0;
		return IO_ERROR_OUT_OF_MEMORY;
	}
	return st_rip3_ports_capture_pixels(ports, number, area, viewport, alternate, result);
}

#define calloc test_calloc
#define st_rip3_ports_capture_pixels capture_pixels
#include "../st_rip3_bitmap_capture.c"
