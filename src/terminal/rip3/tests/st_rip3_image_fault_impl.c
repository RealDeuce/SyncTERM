/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdlib.h>
#include "st_rip3_ports.h"

bool st_rip3_image_test_fail_allocate;
unsigned st_rip3_image_test_fills;
struct st_rip3_rect st_rip3_image_test_fill_area;
uint8_t st_rip3_image_test_fill_color;
enum st_rip3_write_mode st_rip3_image_test_fill_mode;

static int
test_fill(struct st_rip3_ports *ports, const struct st_rip3_rect *area,
    uint8_t color, enum st_rip3_write_mode mode)
{
	st_rip3_image_test_fills++;
	st_rip3_image_test_fill_area = *area;
	st_rip3_image_test_fill_color = color;
	st_rip3_image_test_fill_mode = mode;
	return st_rip3_ports_fill(ports,area,color,mode);
}

static void *
test_malloc(size_t size)
{
	if (st_rip3_image_test_fail_allocate) {
		st_rip3_image_test_fail_allocate = false;
		return NULL;
	}
	return malloc(size);
}
#define malloc test_malloc
#define st_rip3_ports_fill test_fill
#include "../st_rip3_image.c"
