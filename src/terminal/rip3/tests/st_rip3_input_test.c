/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_input.h"
#include "io_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

struct fixture {
	struct st_rip3_input *input;
	uint64_t now;
	uint8_t queue[32], delivered[32];
	size_t length, read, count;
	unsigned reads, clocks;
	bool trigger;
};

static uint64_t
now(void *context)
{
	struct fixture *f = context;
	f->clocks++; return f->now;
}

static bool
next(void *context, uint8_t *byte)
{
	struct fixture *f = context;
	f->reads++;
	if (f->read == f->length) return false;
	*byte = f->queue[f->read++]; return true;
}

static void
consume(void *context, uint8_t byte)
{
	struct fixture *f = context;
	f->delivered[f->count++] = byte;
	if (f->trigger && byte == '!') st_rip3_input_delay(f->input,1);
}

static int
create(struct fixture *f)
{
	const struct st_rip3_input_source source = {.context=f,.now_ns=now,.next=next,.consume=consume};
	CHECK(st_rip3_input_create(&f->input,&source) == 0);
	return 0;
}

static int
queue_test(void)
{
	struct fixture f = {.now=100,.queue={'a','!','b','c'},.length=4,.trigger=true};
	CHECK(create(&f) == 0);
	st_rip3_input_process(f.input,0);
	CHECK(f.reads == 0 && f.count == 0);
	st_rip3_input_process(f.input,32);
	CHECK(f.read == 2 && f.count == 2 && f.reads == 2);
	CHECK(st_rip3_input_wait_ns(f.input) == 16666667);
	/* More arrivals stay owned by the source, with no read-ahead. */
	f.queue[f.length++] = 'd';
	f.now += 16666666;
	st_rip3_input_process(f.input,32);
	CHECK(f.reads == 2 && f.count == 2 && st_rip3_input_wait_ns(f.input) == 1);
	f.now++;
	st_rip3_input_process(f.input,1);
	CHECK(f.count == 3 && f.read == 3 && f.delivered[2] == 'b');
	st_rip3_input_process(f.input,32);
	CHECK(f.count == 5 && f.read == f.length && memcmp(f.delivered,"a!bcd",5) == 0);
	CHECK(st_rip3_input_wait_ns(f.input) == 0);
	st_rip3_input_destroy(f.input);
	return 0;
}

static int
timing(void)
{
	struct fixture f = {0};
	const struct { uint32_t ticks; uint64_t duration; } cases[] = {
		{0,0},{1,16666667},{2,33333334},{3,50000000},{60,1000000000},
		{85,1416666667},{16777215,279620250000000},
		{UINT32_MAX,71582788250000000},
	};
	CHECK(create(&f) == 0);
	for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
		uint64_t started = f.now;
		st_rip3_input_delay(f.input,cases[i].ticks);
		CHECK(st_rip3_input_wait_ns(f.input) == cases[i].duration);
		if (cases[i].duration != 0) {
			f.now = started+cases[i].duration-1;
			CHECK(st_rip3_input_wait_ns(f.input) == 1);
		}
		f.now = started+cases[i].duration;
		CHECK(st_rip3_input_wait_ns(f.input) == 0);
		f.now += 10;
	}
	f.now = UINT64_MAX-4;
	st_rip3_input_delay(f.input,1);
	f.now = 5;
	CHECK(st_rip3_input_wait_ns(f.input) == 16666657);
	f.now = 16666662;
	CHECK(st_rip3_input_wait_ns(f.input) == 0);
	st_rip3_input_delay(f.input,60);
	CHECK(st_rip3_input_wait_ns(f.input) == 1000000000);
	unsigned reads = f.reads;
	st_rip3_input_reset(f.input);
	CHECK(st_rip3_input_wait_ns(f.input) == 0 && f.reads == reads);
	st_rip3_input_destroy(f.input);
	return 0;
}

static int
lifetime(void)
{
	struct fixture f = {.queue={'x','y'},.length=2};
	struct st_rip3_input *unchanged = NULL;
	struct st_rip3_input_source source = {.context=&f,.now_ns=now,.next=next,.consume=consume};
	CHECK(st_rip3_input_create(NULL,&source) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_input_create(&unchanged,NULL) == IO_ERROR_INVALID_ARGUMENT && unchanged == NULL);
	source.next = NULL;
	CHECK(st_rip3_input_create(&unchanged,&source) == IO_ERROR_INVALID_ARGUMENT && unchanged == NULL);
	source.next = next; source.now_ns = NULL;
	CHECK(st_rip3_input_create(&unchanged,&source) == IO_ERROR_INVALID_ARGUMENT && unchanged == NULL);
	source.now_ns = now; source.consume = NULL;
	CHECK(st_rip3_input_create(&unchanged,&source) == IO_ERROR_INVALID_ARGUMENT && unchanged == NULL);
	CHECK(create(&f) == 0);
	st_rip3_input_delay(f.input,60);
	st_rip3_input_process(f.input,10);
	CHECK(f.read == 0 && f.reads == 0);
	st_rip3_input_reset(f.input);
	st_rip3_input_process(f.input,10);
	CHECK(f.count == 2 && memcmp(f.delivered,"xy",2) == 0);
	st_rip3_input_delay(f.input,60);
	st_rip3_input_destroy(f.input);
	CHECK(f.length == 2 && f.read == 2); /* The borrowed source remains owned. */
	st_rip3_input_reset(NULL); st_rip3_input_delay(NULL,1);
	st_rip3_input_process(NULL,10); st_rip3_input_destroy(NULL);
	CHECK(st_rip3_input_wait_ns(NULL) == 0);
	return 0;
}

int
main(void)
{
	CHECK(queue_test() == 0);
	CHECK(timing() == 0);
	CHECK(lifetime() == 0);
	return 0;
}
