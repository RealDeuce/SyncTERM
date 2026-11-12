/* Production numeric callbacks: independent local-time/source expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_numeric.h"
#include "st_big_random.h"
#include "io_error.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)
static const uint64_t timeout = UINT64_C(4010989011);
static const struct st_graphics_command_view view = {.code = 'S'};
static const struct st_graphics_parameter scalar = {.kind = ST_GRAPHICS_NUMBER};
static const struct st_graphics_parameter y = {.kind = ST_GRAPHICS_NUMBER,.axis = ST_GRAPHICS_Y};

struct fixture {
	struct st_big_numeric numeric;
	uint64_t now;
	unsigned clocks, peeks, reads, bells, fail_clock_at;
	int clock_error, peek_error, read_error;
	bool ready, bell_scale, bell_activate;
	uint8_t key;
	size_t commands, text_size, payload_size;
	int values[4]; uint8_t text[32], payload[16];
};

static int
now_ns(void *context, uint64_t *value)
{
	struct fixture *f = context;
	++f->clocks;
	if (f->clock_error != 0) return f->clock_error;
	if (f->fail_clock_at == f->clocks) return IO_ERROR_IO;
	*value = f->now; return 0;
}
static int peek(void *context, bool *ready)
{
	struct fixture *f = context; ++f->peeks; *ready = f->ready; return f->peek_error;
}
static int read_key(void *context, uint8_t *key)
{
	struct fixture *f = context; ++f->reads;
	if (f->read_error != 0) return f->read_error;
	*key = f->key; f->ready = false; return 0;
}
static void bell(void *context)
{
	struct fixture *f = context; ++f->bells;
	if (f->bell_scale) { f->numeric.context.scaling = 3; f->numeric.context.resolution = 3; }
	if (f->bell_activate) f->numeric.loop.active = 1;
}
static int init(struct fixture *f)
{
	memset(f,0,sizeof(*f));
	const struct st_big_numeric_io io = {f,now_ns,peek,read_key,bell};
	return st_big_numeric_init(&f->numeric,&io);
}
static int wire_begin(struct fixture *f, bool is_y)
{
	const struct st_graphics_parameter *p = is_y ? &y : &scalar;
	int value = 12345; bool supplied = true;
	CHECK(st_big_numeric_field(&f->numeric,&view,p) == 0);
	CHECK(st_big_numeric_number(&f->numeric,&view,p,&value,&supplied) == 0);
	CHECK(!supplied && value == 12345);
	return 0;
}
static int wire_read(struct fixture *f, const void *bytes, size_t size,
    size_t *used, int *value, bool *complete)
{
	return st_big_numeric_read_number(&f->numeric,&view,&scalar,bytes,size,used,value,complete);
}
static int loop_setup(struct fixture *f, const uint16_t *words, size_t count, uint16_t delay)
{
	struct st_big_loop *loop = &f->numeric.loop;
	st_big_loop_init(loop); loop->step = 1; loop->type = ','; loop->command = 'L';
	loop->count = (uint16_t)count; loop->delay = delay;
	for (size_t i = 0; i < count; ++i) loop->words[i] = words[i];
	CHECK(st_big_loop_publish(loop) == 0 && st_big_loop_scan(loop) == 0);
	return 0;
}

static int
byte_deadlines(void)
{
	struct fixture f; size_t used; int value = 12345; bool complete;
	uint64_t deadline;
	CHECK(ST_BIG_NUMERIC_TIMEOUT_NS == timeout);
	CHECK(init(&f) == 0 && wire_begin(&f,false) == 0);
	CHECK(st_big_numeric_deadline(&f.numeric,&deadline) && deadline == timeout);
	f.now = timeout-1;
	CHECK(wire_read(&f,"1",1,&used,&value,&complete) == 0 && used == 1 && !complete);
	CHECK(st_big_numeric_deadline(&f.numeric,&deadline) && deadline == 2*timeout-1);
	f.now = 2*timeout-2;
	CHECK(wire_read(&f,NULL,0,&used,&value,&complete) == 0 && used == 0 && !complete && f.bells == 0);
	f.now = 2*timeout-1;
	CHECK(wire_read(&f,NULL,0,&used,&value,&complete) == 0 && complete && value == 1 && f.bells == 1);
	CHECK(!st_big_numeric_deadline(&f.numeric,&deadline));
	unsigned clocks = f.clocks;
	CHECK(wire_read(&f,"9:",2,&used,&value,&complete) == 0 && used == 0 && complete && value == 1);
	CHECK(f.bells == 1 && f.clocks == clocks);
	CHECK(init(&f) == 0 && wire_begin(&f,false) == 0); f.now = timeout;
	CHECK(wire_read(&f,"9:",2,&used,&value,&complete) == 0 && used == 1 && complete && value == 0 && f.bells == 1);
	/* Auxiliary timeout SPACE owns one acquisition; the next has a fresh wait. */
	CHECK(init(&f) == 0 && wire_begin(&f,false) == 0); f.numeric.context.auto_mode = true;
	CHECK(wire_read(&f,"12_",3,&used,&value,&complete) == 0 && used == 3 && !complete);
	f.now = timeout;
	CHECK(wire_read(&f,NULL,0,&used,&value,&complete) == 0 && !complete && f.bells == 0);
	CHECK(st_big_numeric_deadline(&f.numeric,&deadline) && deadline == 2*timeout);
	f.now = 2*timeout;
	CHECK(wire_read(&f,"y",1,&used,&value,&complete) == 0 && used == 1 && !complete && f.bells == 0);
	CHECK(st_big_numeric_deadline(&f.numeric,&deadline) && deadline == 3*timeout);
	f.now = 3*timeout;
	CHECK(wire_read(&f,NULL,0,&used,&value,&complete) == 0 && complete && value == 102 && f.bells == 1);
	/* BEL occurs before construction/scaling, including its owner effects. */
	CHECK(init(&f) == 0 && wire_begin(&f,true) == 0); f.bell_scale = true;
	CHECK(wire_read(&f,"5",1,&used,&value,&complete) == 0 && !complete);
	f.now = timeout;
	CHECK(wire_read(&f,NULL,0,&used,&value,&complete) == 0 && complete && value == 11 && f.bells == 1);
	return 0;
}

static int
stored_waits_and_keys(void)
{
	struct fixture f; uint64_t deadline;
	int value = 12345; bool supplied;
	const uint16_t words[] = {10,5}, x[] = {0x8001};
	CHECK(init(&f) == 0 && loop_setup(&f,words,2,2) == 0);
	f.numeric.context.scaling = 3; f.numeric.context.resolution = 3;
	CHECK(st_big_numeric_field(&f.numeric,&view,&y) == 0);
	CHECK(st_big_numeric_number(&f.numeric,&view,&y,&value,&supplied) == IO_ERROR_IN_PROGRESS && !supplied && value == 12345);
	CHECK(f.clocks == 2 && f.peeks == 1 && f.reads == 0);
	CHECK(st_big_numeric_deadline(&f.numeric,&deadline) && deadline == UINT64_C(109890110));
	f.now = UINT64_C(109890109);
	CHECK(st_big_numeric_number(&f.numeric,&view,&y,&value,&supplied) == IO_ERROR_IN_PROGRESS && !supplied);
	f.numeric.loop.delay = 1; /* Current duration, with the same local start. */
	CHECK(st_big_numeric_deadline(&f.numeric,&deadline) && deadline == UINT64_C(54945055));
	CHECK(st_big_numeric_number(&f.numeric,&view,&y,&value,&supplied) == 0 && supplied && value == 10);
	CHECK(f.numeric.loop.index == 1 && !st_big_numeric_deadline(&f.numeric,&deadline));
	CHECK(st_big_numeric_field(&f.numeric,&view,&y) == 0);
	f.ready = true; f.key = 'A'; unsigned clocks = f.clocks;
	CHECK(st_big_numeric_number(&f.numeric,&view,&y,&value,&supplied) == 0 && supplied && value == 5);
	CHECK(f.numeric.loop.active == 0 && f.reads == 1 && f.clocks == clocks); /* No index-zero delay/scaling. */
	/* A key can shorten delay, then wait at read without a repeated peek. */
	CHECK(init(&f) == 0 && loop_setup(&f,x,1,5) == 0); f.numeric.loop.to = 2;
	f.ready = true; f.key = 0x18; f.read_error = IO_ERROR_IN_PROGRESS;
	CHECK(st_big_numeric_field(&f.numeric,&view,&scalar) == 0);
	CHECK(st_big_numeric_number(&f.numeric,&view,&scalar,&value,&supplied) == IO_ERROR_IN_PROGRESS && !supplied);
	CHECK(f.peeks == 2 && f.reads == 1 && !st_big_numeric_deadline(&f.numeric,&deadline));
	f.read_error = 0;
	CHECK(st_big_numeric_number(&f.numeric,&view,&scalar,&value,&supplied) == 0 && supplied && value == 0);
	CHECK(f.peeks == 2 && f.reads == 2 && f.numeric.loop.active == 0);
	const uint8_t keys[] = {3,0x18,0x1b,'A'};
	for (size_t i = 0; i < sizeof(keys); ++i) {
		CHECK(init(&f) == 0 && loop_setup(&f,x,1,0xffff) == 0); f.numeric.loop.to = 2;
		f.ready = true; f.key = keys[i];
		CHECK(st_big_numeric_field(&f.numeric,&view,&scalar) == 0);
		CHECK(st_big_numeric_number(&f.numeric,&view,&scalar,&value,&supplied) == 0 && supplied && value == 0);
		CHECK(f.numeric.loop.active == (i < 2 ? 0 : 1) && f.reads == 1 && f.peeks == 2 && f.clocks == 2);
	}
	return 0;
}

static int
random_and_common_tail(void)
{
	struct fixture f; size_t used; int value = 12345; bool complete; uint64_t deadline;
	CHECK(init(&f) == 0 && wire_begin(&f,false) == 0);
	f.numeric.random_from = f.numeric.random_to = 10;
	CHECK(wire_read(&f,"r:tail",6,&used,&value,&complete) == IO_ERROR_IN_PROGRESS && used == 2 && !complete && value == 12345);
	CHECK(!f.numeric.loop.command_read && !st_big_numeric_deadline(&f.numeric,&deadline));
	uint32_t first = f.numeric.random_state; unsigned clocks = f.clocks;
	CHECK(wire_read(&f,"tail",4,&used,&value,&complete) == IO_ERROR_IN_PROGRESS && used == 0 && !complete);
	CHECK(f.numeric.random_state != first && f.clocks == clocks && !f.numeric.loop.command_read);
	f.numeric.random_from = f.numeric.random_to = 0;
	CHECK(wire_read(&f,NULL,0,&used,&value,&complete) == 0 && complete && value == 0);
	CHECK(f.numeric.loop.command_read && f.clocks == clocks);
	CHECK(init(&f) == 0 && wire_begin(&f,false) == 0); f.numeric.loop.random_suppression = 1;
	CHECK(wire_read(&f,"r:",2,&used,&value,&complete) == 0 && used == 2 && complete && value == -32765 && f.numeric.random_state == 1);
	CHECK(init(&f) == 0 && wire_begin(&f,false) == 0);
	CHECK(wire_read(&f,"+r:",3,&used,&value,&complete) == 0 && complete && value == 32765 && f.numeric.random_state == 1);
	/* Recheck exact loop1 at the common tail if a real owner effect changes it. */
	CHECK(init(&f) == 0);
	const uint16_t literal[] = {7};
	CHECK(loop_setup(&f,literal,1,0) == 0); f.numeric.loop.active = 0;
	CHECK(wire_begin(&f,false) == 0); f.bell_activate = true; f.now = timeout;
	CHECK(wire_read(&f,NULL,0,&used,&value,&complete) == 0 && complete && value == 7);
	CHECK(f.numeric.loop.active == 0 && f.numeric.loop.current == 1 && f.peeks == 2);
	CHECK(init(&f) == 0 && wire_begin(&f,false) == 0); f.numeric.loop.active = 2;
	CHECK(wire_read(&f,"5:",2,&used,&value,&complete) == 0 && complete && value == 5);
	CHECK(st_big_numeric_field(&f.numeric,&view,&scalar) == 0); bool supplied;
	CHECK(st_big_numeric_number(&f.numeric,&view,&scalar,&value,&supplied) == 0 && supplied && value == 0);
	return 0;
}

static int
failures_and_lifetimes(void)
{
	struct fixture f; size_t used; int value = 12345; bool complete; uint64_t deadline;
	CHECK(init(&f) == 0 && wire_begin(&f,false) == 0); f.fail_clock_at = 3;
	CHECK(wire_read(&f,"12:",3,&used,&value,&complete) == IO_ERROR_IO && used == 1 && !complete && value == 12345);
	CHECK(!st_big_numeric_deadline(&f.numeric,&deadline));
	unsigned clocks = f.clocks;
	CHECK(wire_read(&f,NULL,0,&used,&value,&complete) == IO_ERROR_IO && used == 0 && f.clocks == clocks);
	f.numeric.random_state = 12345; f.numeric.loop.active = 2;
	st_big_numeric_cancel(&f.numeric);
	CHECK(f.numeric.random_state == 12345 && f.numeric.loop.active == 2);
	CHECK(wire_read(&f,NULL,0,&used,&value,&complete) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(init(&f) == 0); f.now = 100; CHECK(wire_begin(&f,false) == 0); f.now = 99;
	CHECK(wire_read(&f,NULL,0,&used,&value,&complete) == IO_ERROR_OUT_OF_RANGE && used == 0 && !complete);
	CHECK(init(&f) == 0); f.now = UINT64_MAX-100; CHECK(wire_begin(&f,false) == 0);
	CHECK(st_big_numeric_deadline(&f.numeric,&deadline) && deadline == UINT64_MAX);
	f.now = UINT64_MAX-1;
	CHECK(wire_read(&f,NULL,0,&used,&value,&complete) == 0 && !complete);
	f.now = UINT64_MAX;
	CHECK(wire_read(&f,NULL,0,&used,&value,&complete) == 0 && complete && value == 0 && f.bells == 1);
	/* Invalid config/API calls preserve the reader; source errors latch. */
	CHECK(init(&f) == 0 && wire_begin(&f,false) == 0);
	struct st_big_numeric before; memcpy(&before,&f.numeric,sizeof(before));
	const struct st_big_numeric_io invalid = {.context = &f,.peek_key = peek};
	CHECK(st_big_numeric_init(&f.numeric,&invalid) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(memcmp(&before,&f.numeric,sizeof(before)) == 0);
	CHECK(wire_read(&f,NULL,1,&used,&value,&complete) == IO_ERROR_INVALID_ARGUMENT && used == 0);
	CHECK(memcmp(&before,&f.numeric,sizeof(before)) == 0);
	CHECK(init(&f) == 0); const uint16_t x[] = {0x8001};
	CHECK(loop_setup(&f,x,1,0) == 0); f.peek_error = IO_ERROR_IN_PROGRESS;
	CHECK(st_big_numeric_field(&f.numeric,&view,&scalar) == 0); bool supplied;
	CHECK(st_big_numeric_number(&f.numeric,&view,&scalar,&value,&supplied) == IO_ERROR_PROTOCOL && !supplied);
	unsigned peeks = f.peeks;
	CHECK(st_big_numeric_number(&f.numeric,&view,&scalar,&value,&supplied) == IO_ERROR_PROTOCOL && f.peeks == peeks);
	/* Exercise the real project monotonic clock without sleeping/audio. */
	CHECK(st_big_numeric_init(&f.numeric,NULL) == 0 && wire_begin(&f,false) == 0);
	CHECK(wire_read(&f,"7:",2,&used,&value,&complete) == 0 && complete && value == 7);
	return 0;
}

static int text(void *context, const uint8_t *bytes, size_t size)
{
	struct st_big_numeric *n = context; struct fixture *f = n->io.context;
	CHECK(size <= sizeof(f->text)-f->text_size);
	if (size != 0) memcpy(f->text+f->text_size,bytes,size);
	f->text_size += size; return 0;
}
static int command(void *context, const struct st_graphics_command_view *v)
{
	struct st_big_numeric *n = context; struct fixture *f = n->io.context;
	CHECK(v->count <= 4); ++f->commands;
	for (size_t i = 0; i < v->count; ++i) {
		if (v->arguments[i].kind == ST_GRAPHICS_NUMBER) {
			CHECK(v->arguments[i].has_value); f->values[i] = v->arguments[i].value;
		} else {
			f->payload_size = v->arguments[i].size;
			CHECK(f->payload_size <= sizeof(f->payload));
			if (f->payload_size != 0) memcpy(f->payload,v->arguments[i].bytes,f->payload_size);
		}
	}
	return 0;
}
static int raw_next(void *context, const struct st_graphics_command_view *v, struct st_graphics_parameter *p)
{
	(void)context; *p = (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	if (v->count == 1) {
		CHECK(v->arguments[0].has_value && v->arguments[0].value == 2);
		*p = (struct st_graphics_parameter){.kind = ST_GRAPHICS_BYTES,.size = 2};
	}
	return 0;
}
static int parser_create(struct fixture *f, st_graphics_parser_t **parser, struct st_graphics_parser_sink *sink)
{
	static const uint8_t attention[] = {'I','#'};
	static const struct st_graphics_dialect dialect = {.attention = attention,.attention_size = 2,
		.chain_indicator = true,.chain = '>',.number_digits = 4};
	static const struct st_graphics_parameter line[] = {
		{.kind = ST_GRAPHICS_NUMBER,.axis = ST_GRAPHICS_X},{.kind = ST_GRAPHICS_NUMBER,.axis = ST_GRAPHICS_Y},
		{.kind = ST_GRAPHICS_NUMBER,.axis = ST_GRAPHICS_X},{.kind = ST_GRAPHICS_NUMBER,.axis = ST_GRAPHICS_Y}};
	static const struct st_graphics_command commands[] = {{'L',line,4,NULL},{'S',&scalar,1,NULL},{'V',&scalar,1,raw_next}};
	static const struct st_graphics_parser_config config = {.dialect = &dialect,.commands = commands,
		.command_count = 3,.max_arguments = 4,.max_payload = 16};
	CHECK(init(f) == 0);
	*sink = (struct st_graphics_parser_sink){.context = &f->numeric,.text = text,.command = command,
		.field = st_big_numeric_field,.number = st_big_numeric_number,.read_number = st_big_numeric_read_number};
	return st_graphics_parser_create(parser,&config);
}
static int shared_parser(void)
{
	static const uint8_t wire[] = "I#L 20,5:tail";
	for (size_t split = 0; split <= sizeof(wire); ++split) {
		struct fixture f; st_graphics_parser_t *parser = NULL; struct st_graphics_parser_sink sink; size_t used;
		CHECK(parser_create(&f,&parser,&sink) == 0); const uint16_t words[] = {10,5};
		CHECK(loop_setup(&f,words,2,0) == 0); f.numeric.context.scaling = f.numeric.context.resolution = 3;
		if (split < sizeof(wire)) {
			CHECK(st_graphics_parser_feed(parser,&sink,wire,split,&used) == 0 && used == split);
			CHECK(st_graphics_parser_feed(parser,&sink,wire+split,sizeof(wire)-1-split,&used) == 0 && used == sizeof(wire)-1-split);
		} else for (size_t i = 0; i < sizeof(wire)-1; ++i)
			CHECK(st_graphics_parser_feed(parser,&sink,wire+i,1,&used) == 0 && used == 1);
		CHECK(f.commands == 1 && f.values[0] == 10 && f.values[1] == 5 && f.values[2] == 20 && f.values[3] == 11);
		CHECK(f.text_size == 4 && memcmp(f.text,"tail",4) == 0);
		st_graphics_parser_destroy(parser);
	}
	struct fixture f; st_graphics_parser_t *parser = NULL; struct st_graphics_parser_sink sink; size_t used;
	CHECK(parser_create(&f,&parser,&sink) == 0);
	const uint8_t raw[] = {'I','#','V',' ','r',':',0,'I','#','t','a','i','l'};
	CHECK(st_graphics_parser_feed(parser,&sink,raw,sizeof(raw),&used) == 0 && used == sizeof(raw));
	CHECK(f.commands == 1 && f.values[0] == 2 && f.numeric.random_state == UINT32_C(0x015a4e36));
	CHECK(f.payload_size == 2 && memcmp(f.payload,"\0I",2) == 0 && f.text_size == 5 && memcmp(f.text,"#tail",5) == 0);
	CHECK(!st_big_numeric_deadline(&f.numeric,&f.now)); /* Raw entry ended numeric ownership. */
	st_graphics_parser_destroy(parser);
	CHECK(parser_create(&f,&parser,&sink) == 0);
	CHECK(st_graphics_parser_feed(parser,&sink,(const uint8_t *)"I#S 12_\r\n",9,&used) == 0 && used == 9);
	f.now = timeout;
	CHECK(st_graphics_parser_feed(parser,&sink,NULL,0,&used) == 0 && used == 0);
	CHECK(f.commands == 1 && f.values[0] == 102 && f.bells == 1);
	CHECK(st_graphics_parser_feed(parser,&sink,(const uint8_t *)"I#S 5",5,&used) == 0);
	CHECK(st_graphics_parser_finish(parser,&sink) == IO_ERROR_PROTOCOL);
	st_big_numeric_cancel(&f.numeric);
	CHECK(st_graphics_parser_feed(parser,&sink,(const uint8_t *)"I#S 7:",6,&used) == 0 && f.commands == 2 && f.values[0] == 7);
	st_graphics_parser_destroy(parser);
	CHECK(parser_create(&f,&parser,&sink) == 0); f.numeric.random_from = f.numeric.random_to = 10;
	CHECK(st_graphics_parser_feed(parser,&sink,(const uint8_t *)"I#S r:tail",10,&used) == IO_ERROR_IN_PROGRESS && used == 6);
	CHECK(f.commands == 0 && !f.numeric.loop.command_read);
	CHECK(st_graphics_parser_feed(parser,&sink,(const uint8_t *)"tail",4,&used) == IO_ERROR_IN_PROGRESS && used == 0);
	f.numeric.random_from = f.numeric.random_to = 0;
	CHECK(st_graphics_parser_feed(parser,&sink,(const uint8_t *)"tail",4,&used) == 0 && used == 4);
	CHECK(f.commands == 1 && f.values[0] == 0 && f.text_size == 4 && memcmp(f.text,"tail",4) == 0);
	st_graphics_parser_destroy(parser);
	/* A stored final-r wait can retain a source after clearing loop mode. */
	CHECK(parser_create(&f,&parser,&sink) == 0); const uint16_t r[] = {0x8003};
	CHECK(loop_setup(&f,r,1,0) == 0); f.numeric.random_from = f.numeric.random_to = 10;
	CHECK(st_graphics_parser_feed(parser,&sink,(const uint8_t *)"I#S tail",8,&used) == IO_ERROR_IN_PROGRESS && used == 4);
	CHECK(f.numeric.loop.active == 0 && f.numeric.loop.current == 1 && f.peeks == 2);
	f.numeric.random_from = f.numeric.random_to = 0;
	CHECK(st_graphics_parser_feed(parser,&sink,(const uint8_t *)"tail",4,&used) == 0 && used == 4);
	CHECK(f.commands == 1 && f.values[0] == 0 && f.peeks == 2 && f.numeric.loop.current == 1);
	st_graphics_parser_destroy(parser);
	/* A clock failure after one digit reports that consumed prefix exactly. */
	CHECK(parser_create(&f,&parser,&sink) == 0); f.fail_clock_at = 3;
	CHECK(st_graphics_parser_feed(parser,&sink,(const uint8_t *)"I#S 12:tail",11,&used) == IO_ERROR_IO && used == 5);
	CHECK(f.commands == 0 && !st_graphics_parser_in_command(parser));
	f.fail_clock_at = 0;
	CHECK(st_graphics_parser_feed(parser,&sink,(const uint8_t *)"2:tail",6,&used) == 0 && used == 6);
	CHECK(f.text_size == 6 && memcmp(f.text,"2:tail",6) == 0);
	CHECK(st_graphics_parser_feed(parser,&sink,(const uint8_t *)"I#S 7:",6,&used) == 0 && f.commands == 1 && f.values[0] == 7);
	st_graphics_parser_destroy(parser);
	return 0;
}

int main(void)
{
	CHECK(byte_deadlines() == 0);
	CHECK(stored_waits_and_keys() == 0);
	CHECK(random_and_common_tail() == 0);
	CHECK(failures_and_lifetimes() == 0);
	CHECK(shared_parser() == 0);
	return 0;
}
