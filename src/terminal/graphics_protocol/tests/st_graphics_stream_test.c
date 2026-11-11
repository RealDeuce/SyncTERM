/* Incremental raw observers: opaque bytes, exact ownership and explicit end. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_graphics_parser.h"
#include "io_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)

struct capture {
	uint8_t raw[512], payload[512], text[128];
	size_t received, field_received, payload_size, text_size, fields, commands;
	size_t stop, limit;
	int fail;
	bool zero, overflow, immediate, suspend;
	const uint8_t *borrowed;
};

static int
text(void *context, const uint8_t *bytes, size_t size)
{
	struct capture *c = context;
	CHECK(size <= sizeof(c->text)-c->text_size);
	memcpy(c->text+c->text_size,bytes,size); c->text_size += size; return 0;
}

static int
next(void *context, const struct st_graphics_command_view *view, struct st_graphics_parameter *parameter)
{
	(void)context;
	*parameter = view->count == 1 ? (struct st_graphics_parameter){.kind = ST_GRAPHICS_BYTES,.size = view->arguments[0].operand.value}
	    : (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	return 0;
}

static int
field(void *context, const struct st_graphics_command_view *view, const struct st_graphics_parameter *parameter)
{
	struct capture *c = context;
	++c->fields;
	if (parameter->kind == ST_GRAPHICS_BYTES) {
		CHECK((view->code == 'V' && view->count == 1) || (view->code == 'Z' && view->count < 2));
		c->field_received = 0;
	}
	return 0;
}

static int
raw(void *context, const struct st_graphics_command_view *view,
    const struct st_graphics_parameter *parameter, size_t offset,
    const uint8_t *bytes, size_t size, size_t *accepted, bool *end)
{
	struct capture *c = context;
	CHECK(((view->code == 'V' && view->count == 1) || (view->code == 'Z' && view->count < 2))
	    && parameter->kind == ST_GRAPHICS_BYTES);
	CHECK(offset == c->field_received && size > 0 && size <= parameter->size-offset);
	if (c->overflow) { *accepted = size+1; return 0; }
	if (c->zero || c->immediate) { *accepted = 0; *end = c->immediate; return 0; }
	if (c->limit != 0 && size > c->limit) size = c->limit;
	if (c->stop != 0 && size > c->stop-offset) size = c->stop-offset;
	CHECK(size <= sizeof(c->raw)-c->received);
	memcpy(c->raw+c->received,bytes,size); c->received += size; c->field_received += size;
	*accepted = size; *end = c->stop != 0 && c->field_received == c->stop;
	return c->fail;
}

static int
command(void *context, const struct st_graphics_command_view *view)
{
	struct capture *c = context; ++c->commands;
	if (view->code == 'Z') {
		CHECK(view->count == 2 && view->arguments[0].size == 3 && view->arguments[1].size == 2);
		CHECK(memcmp(view->arguments[0].bytes,"\0G#",3) == 0 && memcmp(view->arguments[1].bytes,">\033",2) == 0);
		c->payload_size = 5; return 0;
	}
	if (view->code == 'V') {
		CHECK(view->count == 2 && view->arguments[1].kind == ST_GRAPHICS_BYTES);
		c->payload_size = view->arguments[1].size;
		CHECK(c->payload_size <= sizeof(c->payload));
		if (c->payload_size != 0) memcpy(c->payload,view->arguments[1].bytes,c->payload_size);
		c->borrowed = view->arguments[1].bytes;
		return c->suspend ? IO_ERROR_IN_PROGRESS : 0;
	}
	CHECK(view->code == 'M' && view->arguments[0].operand.value == 2);
	return 0;
}

static const struct st_graphics_parameter number[] = {{.kind = ST_GRAPHICS_NUMBER}};
static const struct st_graphics_parameter pair[] = {{.kind = ST_GRAPHICS_BYTES,.size = 3},{.kind = ST_GRAPHICS_BYTES,.size = 2}};
static const struct st_graphics_command commands[] = {{'V',number,1,next},{'M',number,1,NULL},{'Z',pair,2,NULL}};
static const uint8_t attention[] = {'G','#'};
static const struct st_graphics_dialect dialect = {.attention = attention,.attention_size = 2,
    .chain_indicator = true,.chain = '>',.number_skip_crlf = true,.number_digits = 4};

static int
create(st_graphics_parser_t **p, struct st_graphics_parser_sink *sink, struct capture *c, size_t capacity)
{
	const struct st_graphics_parser_config config = {.dialect = &dialect,.commands = commands,
	    .command_count = 3,.max_arguments = 8,.max_payload = capacity};
	*sink = (struct st_graphics_parser_sink){.context = c,.text = text,.command = command,.field = field,.raw = raw};
	return st_graphics_parser_create(p,&config);
}

static int
partitions(void)
{
	static const uint8_t wire[] = "AG#V>5:\0\033G#>M 2:B";
	for (unsigned single = 0; single < 2; ++single)
		for (size_t split = 0; split <= sizeof(wire); ++split) {
			st_graphics_parser_t *p = NULL; struct capture c = {.limit = single}; struct st_graphics_parser_sink s;
			CHECK(create(&p,&s,&c,512) == 0); size_t used, size = sizeof(wire)-1;
			if (split > size) {
				for (size_t i = 0; i < size; ++i) CHECK(st_graphics_parser_feed(p,&s,wire+i,1,&used) == 0 && used == 1);
			} else {
				CHECK(st_graphics_parser_feed(p,&s,wire,split,&used) == 0 && used == split);
				CHECK(st_graphics_parser_feed(p,&s,wire+split,size-split,&used) == 0 && used == size-split);
			}
			CHECK(c.received == 5 && c.payload_size == 5 && memcmp(c.raw,"\0\033G#>",5) == 0);
			CHECK(memcmp(c.raw,c.payload,5) == 0 && c.commands == 2 && c.fields == 3);
			CHECK(c.text_size == 2 && memcmp(c.text,"AB",2) == 0);
			st_graphics_parser_destroy(p);
		}
	st_graphics_parser_t *p = NULL; struct capture c = {0}; struct st_graphics_parser_sink s; size_t used;
	CHECK(create(&p,&s,&c,512) == 0);
	const char header[] = "G#V 256:";
	CHECK(st_graphics_parser_feed(p,&s,(const uint8_t *)header,sizeof(header)-1,&used) == 0);
	uint8_t bytes[256]; for (unsigned i = 0; i < 256; ++i) bytes[i] = (uint8_t)i;
	CHECK(st_graphics_parser_feed(p,&s,bytes,sizeof(bytes),&used) == 0 && used == sizeof(bytes));
	CHECK(c.received == 256 && c.payload_size == 256 && memcmp(c.payload,bytes,256) == 0 && c.text_size == 0);
	st_graphics_parser_destroy(p);
	memset(&c,0,sizeof(c)); CHECK(create(&p,&s,&c,5) == 0);
	const uint8_t multiple[] = "G#Z \0G#>\033TAIL";
	CHECK(st_graphics_parser_feed(p,&s,multiple,sizeof(multiple)-1,&used) == 0 && used == sizeof(multiple)-1);
	CHECK(c.fields == 2 && c.commands == 1 && c.received == 5 && c.payload_size == 5);
	CHECK(c.text_size == 4 && memcmp(c.text,"TAIL",4) == 0);
	st_graphics_parser_destroy(p); return 0;
}

static int
early_end_and_errors(void)
{
	const uint8_t prefix[] = "G#V 5:";
	for (unsigned action = 0; action < 7; ++action) {
		st_graphics_parser_t *p = NULL; struct capture c = {0}; struct st_graphics_parser_sink s; size_t used;
		c.stop = action == 0 ? 3 : 0; c.limit = action == 1 || action == 2 ? 2 : 0;
		c.fail = action == 1 ? IO_ERROR_IO : action == 2 ? IO_ERROR_IN_PROGRESS : 0;
		c.zero = action == 3; c.overflow = action == 4; c.immediate = action == 5;
		CHECK(create(&p,&s,&c,action == 6 ? 4 : 512) == 0);
		int result = st_graphics_parser_feed(p,&s,prefix,sizeof(prefix)-1,&used);
		if (action == 6) {
			CHECK(result == IO_ERROR_OUT_OF_RANGE && c.received == 0 && c.commands == 0 && c.fields == 1);
			st_graphics_parser_destroy(p); continue;
		}
		CHECK(result == 0);
		result = st_graphics_parser_feed(p,&s,(const uint8_t *)"abcTAIL",7,&used);
		if (action == 0 || action == 5) {
			CHECK(result == 0 && used == 7 && c.commands == 1 && c.payload_size == (action == 0 ? 3u : 0u));
			CHECK(c.text_size == (action == 0 ? 4u : 7u));
			CHECK(memcmp(c.text,action == 0 ? "TAIL" : "abcTAIL",c.text_size) == 0);
		} else {
			CHECK(result == (action == 1 ? IO_ERROR_IO : IO_ERROR_PROTOCOL));
			CHECK(used == (action == 1 || action == 2 ? 2u : 0u) && c.commands == 0);
			CHECK(!st_graphics_parser_in_command(p));
		}
		st_graphics_parser_destroy(p);
	}
	return 0;
}

static int
owner_end_and_suspend(void)
{
	const uint8_t bytes[] = "G#V 5:a\0";
	for (unsigned explicit = 0; explicit < 2; ++explicit) {
		st_graphics_parser_t *p = NULL; struct capture c = {0}; struct st_graphics_parser_sink s; size_t used;
		CHECK(create(&p,&s,&c,512) == 0);
		CHECK(st_graphics_parser_feed(p,&s,bytes,sizeof(bytes)-1,&used) == 0 && used == sizeof(bytes)-1);
		CHECK(c.received == 2 && c.commands == 0);
		if (explicit) {
			CHECK(st_graphics_parser_end_bytes(p,&s) == 0 && c.commands == 1 && c.payload_size == 2);
			CHECK(memcmp(c.payload,"a\0",2) == 0);
			CHECK(st_graphics_parser_end_bytes(p,&s) == IO_ERROR_INVALID_ARGUMENT);
			CHECK(st_graphics_parser_finish(p,&s) == 0);
		} else CHECK(st_graphics_parser_finish(p,&s) == IO_ERROR_PROTOCOL && c.commands == 0);
		st_graphics_parser_destroy(p);
	}
	st_graphics_parser_t *p = NULL; struct capture c = {.suspend = true}; struct st_graphics_parser_sink s; size_t used;
	CHECK(create(&p,&s,&c,512) == 0);
	CHECK(st_graphics_parser_feed(p,&s,(const uint8_t *)"G#V 5:abcdeTAIL",15,&used) == IO_ERROR_IN_PROGRESS && used == 11);
	CHECK(c.commands == 1 && memcmp(c.borrowed,"abcde",5) == 0);
	CHECK(st_graphics_parser_end_bytes(p,&s) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_graphics_parser_feed(p,&s,(const uint8_t *)"TAIL",4,&used) == IO_ERROR_IN_PROGRESS && used == 0);
	CHECK(st_graphics_parser_resume(p) == 0);
	CHECK(st_graphics_parser_feed(p,&s,(const uint8_t *)"TAIL",4,&used) == 0 && used == 4 && c.commands == 1);
	st_graphics_parser_destroy(p); return 0;
}

int
main(void)
{
	CHECK(partitions() == 0); CHECK(early_end_and_errors() == 0); CHECK(owner_end_and_suspend() == 0); return 0;
}
