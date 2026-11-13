/* Literal original RNG/range expectations and scheduling partition checks. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_random.h"
#include "io_error.h"

#include <stdio.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)

static int
seed_and_next(void)
{
	/* Reference 33 controls-examples.json: zero -> state1/R0; one ->
	 * state015a4e36/R346. ffffffff * M + 1 = 1-M modulo2^32. */
	uint32_t state = UINT32_MAX;
	st_big_random_seed(&state,0);
	CHECK(state == 0 && st_big_random_next(&state) == 0 && state == 1);
	CHECK(st_big_random_next(&state) == 346 && state == UINT32_C(0x015a4e36));
	st_big_random_seed(&state,1);
	CHECK(state == 1 && st_big_random_next(&state) == 346);
	state = UINT32_MAX;
	CHECK(st_big_random_next(&state) == 32421 && state == UINT32_C(0xfea5b1cc));
	st_big_random_seed(&state,UINT16_MAX);
	CHECK(state == UINT16_MAX); /* srand clears the previous high half. */
	return 0;
}

static int
range_cases(void)
{
	uint32_t state;
	uint16_t value = 12345;
	size_t attempts;
	st_big_random_seed(&state,1);
	/* First R346: trunc(346*199/32768) = 2; no FROM addition. */
	CHECK(st_big_random_range(&state,0,199,1,&attempts,&value) == 0);
	CHECK(attempts == 1 && value == 2 && state == UINT32_C(0x015a4e36));
	st_big_random_seed(&state,1);
	CHECK(st_big_random_range(&state,3,199,1,&attempts,&value) == IO_ERROR_IN_PROGRESS);
	CHECK(attempts == 1 && value == 2 && state == UINT32_C(0x015a4e36));
	st_big_random_seed(&state,1);
	CHECK(st_big_random_range(&state,2,199,1,&attempts,&value) == 0 && value == 2);
	st_big_random_seed(&state,1);
	CHECK(st_big_random_range(&state,0,0,99,&attempts,&value) == 0);
	CHECK(attempts == 1 && value == 0 && state == UINT32_C(0x015a4e36));
	/* Signed division truncates a tiny negative product to zero, which
	 * still fails the negative upper bound. Do not clamp/repair in helper. */
	st_big_random_seed(&state,1); value = 12345;
	CHECK(st_big_random_range(&state,0,UINT16_MAX,1,&attempts,&value) == IO_ERROR_IN_PROGRESS);
	CHECK(attempts == 1 && value == 12345 && state == UINT32_C(0x015a4e36));
	/* A negative FROM is compared as a WORD, not normalized here. */
	st_big_random_seed(&state,1);
	CHECK(st_big_random_range(&state,UINT16_MAX,199,1,&attempts,&value) == 0 && value == 2);
	return 0;
}

static int
bounded_progress(void)
{
	const uint16_t bounds[][2] = {{10,10},{200,199},{0,0x8000},{0x8000,0x8000}};
	for (size_t b = 0; b < sizeof(bounds)/sizeof(*bounds); ++b) {
		uint32_t bulk = 1, split = 1, expected = 1;
		uint16_t value = 54321;
		size_t used;
		CHECK(st_big_random_range(&bulk,bounds[b][0],bounds[b][1],257,&used,&value)
		    == IO_ERROR_IN_PROGRESS && used == 257 && value == 54321);
		for (size_t i = 0; i < 257; ++i) {
			CHECK(st_big_random_range(&split,bounds[b][0],bounds[b][1],1,&used,&value)
			    == IO_ERROR_IN_PROGRESS && used == 1 && value == 54321);
			(void)st_big_random_next(&expected);
		}
		CHECK(bulk == split && split == expected);
		/* New bounds resume the advanced stream, with no hidden restart. */
		(void)st_big_random_next(&expected);
		CHECK(st_big_random_range(&split,0,0,1,&used,&value) == 0);
		CHECK(used == 1 && value == 0 && split == expected);
	}
	/* Sweep every possible rand output by choosing its unique predecessor
	 * state: the odd multiplier has inverse 2925141d modulo2^32. For TO199,
	 * the entire attainable range is 0..198, never inclusive upper199. */
	CHECK((uint32_t)(UINT32_C(0x015a4e35)*UINT32_C(0x2925141d)) == 1);
	for (uint32_t r = 0; r < 32768; ++r) {
		uint32_t state = ((r<<16)-1)*UINT32_C(0x2925141d);
		uint16_t value = 54321;
		size_t used;
		CHECK(st_big_random_range(&state,0,199,1,&used,&value) == 0 && used == 1);
		CHECK(state == r<<16 && value == (uint16_t)(r*199/32768) && value < 199);
	}
	return 0;
}

static int
validation(void)
{
	uint32_t state = 1;
	uint16_t value = 7;
	size_t used = 99;
	CHECK(st_big_random_range(&state,0,199,0,&used,&value) == IO_ERROR_IN_PROGRESS);
	CHECK(state == 1 && used == 0 && value == 7);
	CHECK(st_big_random_range(NULL,0,199,1,&used,&value) == IO_ERROR_INVALID_ARGUMENT && used == 0);
	CHECK(st_big_random_range(&state,0,199,1,NULL,&value) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_random_range(&state,0,199,1,&used,NULL) == IO_ERROR_INVALID_ARGUMENT && used == 0);
	CHECK(state == 1 && value == 7);
	return 0;
}
static int
repair(void)
{
	const uint16_t edges[]={0,1,10,199,200,32766,32767,32768,65535};
	for (unsigned word=0;word<65536;++word) for (size_t i=0;i<sizeof(edges)/sizeof(*edges);++i) {
		for (unsigned side=0;side<2;++side) {
			uint16_t from=side?edges[i]:(uint16_t)word, to=side?(uint16_t)word:edges[i];
			unsigned lower=from<32768?from:0;
			int upper=to<32768?to:(int)to-65536;
			uint16_t expected=upper<(int)lower?(uint16_t)(lower+1):to;
			CHECK(st_big_random_repair(&from,&to)==0 && from==lower && to==expected);
		}
	}
	uint16_t from=32767,to=0;
	CHECK(st_big_random_repair(&from,&to)==0 && from==32767 && to==32768);
	CHECK(st_big_random_repair(NULL,&to)==IO_ERROR_INVALID_ARGUMENT && to==32768);
	CHECK(st_big_random_repair(&from,NULL)==IO_ERROR_INVALID_ARGUMENT && from==32767);
	return 0;
}

int
main(void)
{
	CHECK(seed_and_next() == 0);
	CHECK(range_cases() == 0);
	CHECK(bounded_progress() == 0);
	CHECK(validation() == 0);
	CHECK(repair() == 0);
	return 0;
}
