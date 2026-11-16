/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

unsigned st_rip3_polygon_test_malloc_countdown;

static void *
test_malloc(size_t size)
{
	if (st_rip3_polygon_test_malloc_countdown != 0
	    && --st_rip3_polygon_test_malloc_countdown == 0) return NULL;
	return malloc(size);
}

#define malloc test_malloc
#include "../st_rip3_polygon.c"

/* Exercise the same exact crossing arithmetic at offscreen viewport limits
 * without allocating an enormous bitmap. Expected values are independent. */
int32_t
st_rip3_polygon_test_boundary(int64_t ax, int64_t ay, int64_t bx, int64_t by,
    int32_t y, int32_t limit)
{
	const struct st_rip3_vector a = {(double)ax,(double)ay}, b = {(double)bx,(double)by};
	struct polygon_edge edge = make_edge(&a,&b);
	return edge_boundary(&edge,y,limit);
}
