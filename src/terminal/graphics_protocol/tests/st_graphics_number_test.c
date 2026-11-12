/* Custom numeric acquisition: independent byte/value ownership contracts. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_graphics_parser.h"
#include "io_error.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)

struct capture {
	size_t fields, sources, reads, commands, boundaries, acquired, total;
	uint8_t bytes[64], text[64], payload[8];
	size_t text_size, payload_size, counts[8];
	struct st_graphics_argument arguments[8][2];
	bool chained[8], supply_x, ready, wait, suspend_command;
	int mode, value;
};

static int
text(void *context, const uint8_t *bytes, size_t size)
{
	struct capture *c = context;
	CHECK(size <= sizeof(c->text)-c->text_size);
	if (size != 0) memcpy(c->text+c->text_size, bytes, size);
	c->text_size += size;
	return 0;
}

static int
boundary(void *context)
{
	++((struct capture *)context)->boundaries;
	return 0;
}

static int
field(void *context, const struct st_graphics_command_view *view,
    const struct st_graphics_parameter *parameter)
{
	struct capture *c = context;
	(void)view;
	++c->fields;
	if (parameter->kind == ST_GRAPHICS_NUMBER) c->acquired = 0;
	return 0;
}

static int
source(void *context, const struct st_graphics_command_view *view,
    const struct st_graphics_parameter *parameter, int *value, bool *supplied)
{
	struct capture *c = context;
	(void)view;
	++c->sources;
	*supplied = c->supply_x && parameter->axis == ST_GRAPHICS_X;
	*value = INT_MAX;
	return 0;
}

/* Deliberately not a BIG decimal implementation. A three-byte opaque field
 * returns a fixed signed value, proving generic framing never interprets it. */
static int
read_number(void *context, const struct st_graphics_command_view *view,
    const struct st_graphics_parameter *parameter, const uint8_t *bytes,
    size_t size, size_t *accepted, int *value, bool *complete)
{
	struct capture *c = context;
	CHECK(view->count < 2 && parameter->kind == ST_GRAPHICS_NUMBER);
	++c->reads;
	if (c->mode == 1) { *accepted = size+1; return 0; }
	if (c->mode == 2) return 0; /* Invalid nonempty zero progress. */
	if (c->mode == 3) { *complete = true; return IO_ERROR_IN_PROGRESS; }
	if (c->mode == 4) { *accepted = size == 0 ? 0 : 1; *complete = true; return IO_ERROR_IO; }
	if (c->ready) { *complete = true; *value = c->value; return 0; }
	if (c->wait) return IO_ERROR_IN_PROGRESS;
	size_t take = 3-c->acquired;
	if (take > size) take = size;
	CHECK(take <= sizeof(c->bytes)-c->total);
	if (take != 0) memcpy(c->bytes+c->total, bytes, take);
	c->total += take;
	c->acquired += take;
	*accepted = take;
	if (c->mode == 5) return IO_ERROR_IO;
	if (c->mode == 6) { c->wait = true; return IO_ERROR_IN_PROGRESS; }
	*complete = c->acquired == 3;
	*value = c->value;
	return 0;
}

static int
command(void *context, const struct st_graphics_command_view *view)
{
	struct capture *c = context;
	CHECK(c->commands < 8 && view->count <= 2);
	size_t n = c->commands++;
	c->counts[n] = view->count;
	c->chained[n] = view->chained;
	for (size_t i = 0; i < view->count; ++i) {
		c->arguments[n][i] = view->arguments[i];
		if (view->arguments[i].kind == ST_GRAPHICS_BYTES) {
			c->payload_size = view->arguments[i].size;
			CHECK(c->payload_size <= sizeof(c->payload));
			if (c->payload_size != 0)
				memcpy(c->payload, view->arguments[i].bytes, c->payload_size);
		}
	}
	return c->suspend_command ? IO_ERROR_IN_PROGRESS : 0;
}

static int
next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *parameter)
{
	(void)context;
	*parameter = (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	if (view->count == 1) {
		CHECK(view->arguments[0].has_value && view->arguments[0].value == 3);
		*parameter = (struct st_graphics_parameter){.kind = ST_GRAPHICS_BYTES, .size = 3};
	}
	return 0;
}

static const uint8_t attention[] = {'I','#'};
static const struct st_graphics_dialect dialect = {
	.attention = attention, .attention_size = 2, .chain_indicator = true,
	.chain = '>', .number_skip_crlf = true, .number_digits = 4,
};
static const struct st_graphics_parameter scalar[] = {{.kind = ST_GRAPHICS_NUMBER}};
static const struct st_graphics_parameter pair[] = {
	{.kind = ST_GRAPHICS_NUMBER, .axis = ST_GRAPHICS_X},
	{.kind = ST_GRAPHICS_NUMBER, .axis = ST_GRAPHICS_Y},
};
static const struct st_graphics_command commands[] = {
	{'S',scalar,1,NULL}, {'P',pair,2,NULL}, {'V',scalar,1,next}, {'Z',NULL,0,NULL},
};

static int
create(st_graphics_parser_t **p, struct st_graphics_parser_sink *sink,
    struct capture *c)
{
	const struct st_graphics_parser_config config = {
		.dialect = &dialect, .commands = commands, .command_count = 4,
		.max_arguments = 2, .max_payload = 8,
	};
	*sink = (struct st_graphics_parser_sink){.context = c, .text = text,
		.boundary = boundary, .field = field, .command = command,
		.number = source, .read_number = read_number};
	return st_graphics_parser_create(p, &config);
}

static int
feed(st_graphics_parser_t *p, struct st_graphics_parser_sink *sink,
    const uint8_t *bytes, size_t size)
{
	size_t used;
	CHECK(st_graphics_parser_feed(p, sink, bytes, size, &used) == 0);
	CHECK(used == size);
	return 0;
}

static int
partitions(void)
{
	static const uint8_t wire[] = {'I','#','P','>',0,'I','#','\r','R',255,
		'Z',' ','t','a','i','l'};
	for (size_t split = 0; split <= sizeof(wire)+1; ++split) {
		st_graphics_parser_t *p = NULL;
		struct st_graphics_parser_sink sink;
		struct capture c = {.value = -1234};
		CHECK(create(&p, &sink, &c) == 0);
		if (split <= sizeof(wire)) {
			CHECK(feed(p, &sink, wire, split) == 0);
			CHECK(feed(p, &sink, wire+split, sizeof(wire)-split) == 0);
		} else {
			for (size_t i = 0; i < sizeof(wire); ++i)
				CHECK(feed(p, &sink, wire+i, 1) == 0);
		}
		CHECK(st_graphics_parser_finish(p, &sink) == 0);
		CHECK(c.fields == 2 && c.sources == 2 && c.commands == 2 && c.boundaries == 2);
		CHECK(c.counts[0] == 2 && c.chained[0] && c.counts[1] == 0 && !c.chained[1]);
		CHECK(c.arguments[0][0].has_value && c.arguments[0][0].value == -1234);
		CHECK(c.arguments[0][0].axis == ST_GRAPHICS_X);
		CHECK(c.arguments[0][1].has_value && c.arguments[0][1].value == -1234);
		CHECK(c.arguments[0][1].axis == ST_GRAPHICS_Y);
		CHECK(c.total == 6 && memcmp(c.bytes, wire+4, 6) == 0);
		CHECK(c.text_size == 4 && memcmp(c.text, "tail", 4) == 0);
		st_graphics_parser_destroy(p);
	}
	return 0;
}

static int
supplied_and_payload(void)
{
	st_graphics_parser_t *p = NULL;
	struct st_graphics_parser_sink sink;
	struct capture c = {.supply_x = true, .value = INT_MIN};
	CHECK(create(&p, &sink, &c) == 0);
	CHECK(feed(p, &sink, (const uint8_t *)"I#P xyz!", 8) == 0);
	CHECK(c.fields == 2 && c.sources == 2 && c.total == 3 && c.commands == 1);
	CHECK(c.arguments[0][0].has_value && c.arguments[0][0].value == INT_MAX);
	CHECK(c.arguments[0][1].has_value && c.arguments[0][1].value == INT_MIN);
	CHECK(c.text_size == 1 && c.text[0] == '!');
	st_graphics_parser_destroy(p);
	/* Dynamic arity sees the evaluated value, then exact opaque raw input. */
	c = (struct capture){.value = 3};
	CHECK(create(&p, &sink, &c) == 0);
	sink.number = NULL;
	static const uint8_t wire[] = {'I','#','V',' ',1,2,3,0,'I','#','!'};
	CHECK(feed(p, &sink, wire, sizeof(wire)) == 0);
	CHECK(c.sources == 0 && c.commands == 1 && c.fields == 2 && c.total == 3);
	CHECK(c.payload_size == 3 && memcmp(c.payload, "\0I#", 3) == 0);
	CHECK(c.text_size == 1 && c.text[0] == '!');
	st_graphics_parser_destroy(p);
	/* A declined source retains the existing default grammar without a reader. */
	c = (struct capture){0};
	CHECK(create(&p, &sink, &c) == 0); sink.read_number = NULL;
	CHECK(feed(p, &sink, (const uint8_t *)"I#S 12:", 7) == 0);
	CHECK(c.commands == 1 && c.sources == 1 && c.reads == 0);
	CHECK(!c.arguments[0][0].has_value && c.arguments[0][0].operand.value == 12);
	st_graphics_parser_destroy(p);
	return 0;
}

static int
waits_and_empty_completion(void)
{
	st_graphics_parser_t *p = NULL;
	struct st_graphics_parser_sink sink;
	struct capture c = {.mode = 6, .value = 17};
	size_t used;
	CHECK(create(&p, &sink, &c) == 0);
	CHECK(st_graphics_parser_feed(p, &sink, (const uint8_t *)"I#S abtail", 10, &used)
	    == IO_ERROR_IN_PROGRESS && used == 7);
	CHECK(c.total == 3 && c.fields == 1 && c.sources == 1 && c.commands == 0);
	CHECK(st_graphics_parser_resume(p) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_graphics_parser_feed(p, &sink, (const uint8_t *)"ail", 3, &used)
	    == IO_ERROR_IN_PROGRESS && used == 0);
	CHECK(c.fields == 1 && c.sources == 1 && c.total == 3);
	c.ready = true;
	CHECK(feed(p, &sink, NULL, 0) == 0 && c.commands == 1);
	CHECK(c.arguments[0][0].has_value && c.arguments[0][0].value == 17);
	CHECK(feed(p, &sink, (const uint8_t *)"ail", 3) == 0);
	CHECK(c.text_size == 3 && memcmp(c.text, "ail", 3) == 0);
	st_graphics_parser_destroy(p);
	/* Input-free completion after a reader deadline, and command suspension,
	 * are separate: an accepted command is never executed twice. */
	c = (struct capture){.value = 42};
	CHECK(create(&p, &sink, &c) == 0);
	CHECK(feed(p, &sink, (const uint8_t *)"I#S ", 4) == 0 && c.reads == 1);
	CHECK(feed(p, &sink, NULL, 0) == 0 && c.reads == 2 && c.commands == 0);
	c.ready = true; c.suspend_command = true;
	CHECK(st_graphics_parser_feed(p, &sink, NULL, 0, &used) == IO_ERROR_IN_PROGRESS && used == 0);
	CHECK(c.commands == 1 && c.fields == 1 && c.sources == 1 && c.reads == 3);
	CHECK(st_graphics_parser_feed(p, &sink, NULL, 0, &used) == IO_ERROR_IN_PROGRESS && c.reads == 3);
	CHECK(st_graphics_parser_resume(p) == 0);
	CHECK(feed(p, &sink, NULL, 0) == 0 && c.commands == 1 && c.reads == 3);
	st_graphics_parser_destroy(p);
	/* Direct/loop command sources can use the same custom wire reader. */
	c = (struct capture){.ready = true, .value = -17};
	CHECK(create(&p, &sink, &c) == 0);
	CHECK(st_graphics_parser_start_command(p, &sink, 'S', false) == 0);
	CHECK(feed(p, &sink, (const uint8_t *)"tail", 4) == 0);
	CHECK(c.commands == 1 && c.fields == 1 && c.reads == 1 && c.total == 0);
	CHECK(c.arguments[0][0].has_value && c.arguments[0][0].value == -17);
	CHECK(c.text_size == 4 && memcmp(c.text, "tail", 4) == 0);
	st_graphics_parser_destroy(p);
	return 0;
}

static int
errors_and_cleanup(void)
{
	for (int mode = 1; mode <= 5; ++mode) {
		st_graphics_parser_t *p = NULL;
		struct st_graphics_parser_sink sink;
		struct capture c = {.mode = mode};
		CHECK(create(&p, &sink, &c) == 0);
		size_t used;
		CHECK(st_graphics_parser_feed(p, &sink, (const uint8_t *)"I#S abc!", 8, &used)
		    == (mode == 5 ? IO_ERROR_IO : IO_ERROR_PROTOCOL));
		CHECK(used == (mode == 5 ? 7U : mode == 4 ? 5U : 4U));
		CHECK(c.commands == 0 && !st_graphics_parser_in_command(p));
		c.mode = 0; c.value = 6;
		CHECK(feed(p, &sink, (const uint8_t *)"I#S xyz", 7) == 0 && c.commands == 1);
		CHECK(c.arguments[0][0].has_value && c.arguments[0][0].value == 6);
		st_graphics_parser_destroy(p);
	}
	for (unsigned cancel = 0; cancel < 2; ++cancel) {
		st_graphics_parser_t *p = NULL;
		struct st_graphics_parser_sink sink;
		struct capture c = {0};
		CHECK(create(&p, &sink, &c) == 0);
		CHECK(feed(p, &sink, (const uint8_t *)"I#S a", 5) == 0 && c.acquired == 1);
		size_t reads = c.reads;
		if (cancel) st_graphics_parser_cancel(p);
		else CHECK(st_graphics_parser_finish(p, &sink) == IO_ERROR_PROTOCOL);
		CHECK(c.reads == reads && c.commands == 0 && !st_graphics_parser_in_command(p));
		CHECK(feed(p, &sink, (const uint8_t *)"I#S xyz", 7) == 0 && c.commands == 1);
		CHECK(c.total == 4 && memcmp(c.bytes, "axyz", 4) == 0);
		st_graphics_parser_destroy(p);
	}
	return 0;
}

int
main(void)
{
	CHECK(partitions() == 0);
	CHECK(supplied_and_payload() == 0);
	CHECK(waits_and_empty_completion() == 0);
	CHECK(errors_and_cleanup() == 0);
	return 0;
}
