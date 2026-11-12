/* Literal protocol examples, raw ownership and streaming/error boundaries. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_graphics_parser.h"
#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; \
} } while (0)

struct capture {
	uint8_t trace[8192], text[1024], payload[1024], codes[64];
	size_t size, text_size, payload_size, commands, boundaries, unknown;
	struct st_graphics_operand operands[128];
	size_t numbers, fields;
	int field_fail;
	bool suspend;
	int fail;
};

static int
put(struct capture *c, unsigned value)
{
	if (c->size == sizeof(c->trace))
		return IO_ERROR_NO_SPACE;
	c->trace[c->size++] = (uint8_t)value;
	return 0;
}

static int
text(void *context, const uint8_t *bytes, size_t size)
{
	struct capture *c = context;
	if (size > sizeof(c->text) - c->text_size)
		return IO_ERROR_NO_SPACE;
	memcpy(c->text + c->text_size, bytes, size);
	c->text_size += size;
	for (size_t i = 0; i < size; ++i) {
		if (put(c, 't') != 0 || put(c, bytes[i]) != 0)
			return IO_ERROR_NO_SPACE;
	}
	return c->fail;
}

static int
boundary(void *context)
{
	struct capture *c = context;
	++c->boundaries;
	return put(c, '|');
}

static int
command(void *context, const struct st_graphics_command_view *view)
{
	struct capture *c = context;
	if (c->commands == sizeof(c->codes))
		return IO_ERROR_NO_SPACE;
	c->codes[c->commands++] = view->code;
	if (put(c, 'c') != 0 || put(c, view->code) != 0
	    || put(c, view->chained) != 0 || put(c, (unsigned)view->count) != 0)
		return IO_ERROR_NO_SPACE;
	for (size_t i = 0; i < view->count; ++i) {
		const struct st_graphics_argument *a = &view->arguments[i];
		if (put(c, a->kind) != 0 || put(c, a->axis) != 0)
			return IO_ERROR_NO_SPACE;
		if (a->kind == ST_GRAPHICS_NUMBER) {
			if (c->numbers == sizeof(c->operands) / sizeof(*c->operands))
				return IO_ERROR_NO_SPACE;
			c->operands[c->numbers++] = a->operand;
			if (put(c, a->operand.source) != 0
			    || put(c, a->operand.operation) != 0)
				return IO_ERROR_NO_SPACE;
			for (unsigned shift = 0; shift < 32; shift += 8) {
				if (put(c, a->operand.value >> shift) != 0)
					return IO_ERROR_NO_SPACE;
			}
		}
		else {
			if (a->size > sizeof(c->payload) - c->payload_size)
				return IO_ERROR_NO_SPACE;
			if (a->size != 0)
				memcpy(c->payload + c->payload_size, a->bytes, a->size);
			c->payload_size += a->size;
			if (put(c, (unsigned)(a->size >> 8)) != 0
			    || put(c, (unsigned)a->size) != 0)
				return IO_ERROR_NO_SPACE;
			for (size_t j = 0; j < a->size; ++j) {
				if (put(c, a->bytes[j]) != 0)
					return IO_ERROR_NO_SPACE;
			}
		}
	}
	return c->fail != 0 ? c->fail : c->suspend ? IO_ERROR_IN_PROGRESS : 0;
}

static void
unknown(void *context, uint8_t code)
{
	struct capture *c = context;
	++c->unknown;
	(void)put(c, 'u');
	(void)put(c, code);
}

static struct st_graphics_parser_sink
sink(struct capture *c)
{
	return (struct st_graphics_parser_sink){.context = c, .text = text, .boundary = boundary, .command = command, .unknown = unknown};
}

static const struct st_graphics_parameter number[] = {
	{.kind = ST_GRAPHICS_NUMBER},
};
static const struct st_graphics_parameter line[] = {
	{.kind = ST_GRAPHICS_NUMBER, .axis = ST_GRAPHICS_X},
	{.kind = ST_GRAPHICS_NUMBER, .axis = ST_GRAPHICS_Y},
	{.kind = ST_GRAPHICS_NUMBER, .axis = ST_GRAPHICS_X},
	{.kind = ST_GRAPHICS_NUMBER, .axis = ST_GRAPHICS_Y},
};
static const struct st_graphics_parameter pens[] = {
	{.kind = ST_GRAPHICS_NUMBER}, {.kind = ST_GRAPHICS_NUMBER},
};
static const struct st_graphics_parameter write_text[] = {
	{.kind = ST_GRAPHICS_NUMBER, .axis = ST_GRAPHICS_X},
	{.kind = ST_GRAPHICS_NUMBER, .axis = ST_GRAPHICS_Y},
	{.kind = ST_GRAPHICS_TEXT, .size = 128, .terminator = '@',
	 .skip_crlf = true, .empty_is_terminator = true},
};

/* A length-prefixed raw test command exercises the same descriptor mechanism
 * needed by variable-arity IGS commands, without claiming their semantics. */
static int
raw_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *result)
{
	(void)context;
	*result = (struct st_graphics_parameter){.kind = ST_GRAPHICS_END};
	if (view->count == 1) {
		if (view->arguments[0].operand.source != ST_GRAPHICS_CONSTANT)
			return IO_ERROR_PROTOCOL;
		result->kind = ST_GRAPHICS_BYTES;
		result->size = view->arguments[0].operand.value;
	}
	return 0;
}

static const struct st_graphics_command commands[] = {
	{'L', line, 4, NULL}, {'C', pens, 2, NULL},
	{'W', write_text, 3, NULL}, {'s', number, 1, NULL},
	{'$', number, 1, raw_next}, {'Q', NULL, 0, NULL},
};
static const uint8_t attention[] = {'G', '#'};
static const struct st_graphics_dialect igs = {
	attention, sizeof(attention), true, '>', true, 4,
};

static int
create(st_graphics_parser_t **p, const struct st_graphics_dialect *dialect,
    size_t arguments, size_t payload)
{
	const struct st_graphics_parser_config config = {
		dialect, commands, sizeof(commands) / sizeof(*commands), arguments, payload,
	};
	return st_graphics_parser_create(p, &config);
}

static int
replay(const uint8_t *bytes, size_t size, size_t split, bool bytewise,
    struct capture *c)
{
	st_graphics_parser_t *p = NULL;
	struct st_graphics_parser_sink s = sink(c);
	size_t used;
	CHECK(create(&p, &igs, 16, 1024) == 0);
	if (bytewise) {
		for (size_t i = 0; i < size; ++i) {
			CHECK(st_graphics_parser_feed(p, &s, bytes + i, 1, &used) == 0);
			CHECK(used == 1);
		}
	}
	else {
		CHECK(st_graphics_parser_feed(p, &s, bytes, split, &used) == 0);
		CHECK(used == split);
		CHECK(st_graphics_parser_feed(p, &s, bytes + split, size - split, &used) == 0);
		CHECK(used == size - split);
	}
	CHECK(st_graphics_parser_finish(p, &s) == 0);
	st_graphics_parser_destroy(p);
	return 0;
}

static int
partitions(const uint8_t *bytes, size_t size, struct capture *expected)
{
	CHECK(replay(bytes, size, size, false, expected) == 0);
	for (size_t split = 0; split <= size; ++split) {
		struct capture c = {0};
		CHECK(replay(bytes, size, split, false, &c) == 0);
		CHECK(c.size == expected->size);
		CHECK(memcmp(c.trace, expected->trace, c.size) == 0);
	}
	struct capture c = {0};
	CHECK(replay(bytes, size, 0, true, &c) == 0);
	CHECK(c.size == expected->size);
	CHECK(memcmp(c.trace, expected->trace, c.size) == 0);
	return 0;
}

static int
literal_examples(void)
{
	/* Reference 02/13: only G# enters command mode; > belongs to dispatch,
	 * while numeric readers own the commas and colon. Lowercase is distinct. */
	const uint8_t bytes[] = "preGG#L>0,0,100,100:C 1,2:postG";
	struct capture c = {0};
	CHECK(partitions(bytes, sizeof(bytes) - 1, &c) == 0);
	CHECK(c.commands == 2 && memcmp(c.codes, "LC", 2) == 0);
	CHECK(c.numbers == 6 && c.operands[2].value == 100);
	CHECK(c.operands[4].value == 1 && c.operands[5].value == 2);
	CHECK(c.text_size == 9 && memcmp(c.text, "preGpostG", 9) == 0);
	memset(&c, 0, sizeof(c));
	const uint8_t unknown_bytes[] = "G#c>Q tail";
	CHECK(partitions(unknown_bytes, sizeof(unknown_bytes) - 1, &c) == 0);
	CHECK(c.commands == 1 && c.codes[0] == 'Q' && c.unknown == 1);
	CHECK(c.text_size == 4 && memcmp(c.text, "tail", 4) == 0);
	/* Ordinary CR/LF are retained; numeric/text readers skip their own. */
	memset(&c, 0, sizeof(c));
	const uint8_t text_bytes[] = "\r\nG#W>2,3,A\r\nG#\0B@W 0,0,@!";
	CHECK(partitions(text_bytes, sizeof(text_bytes) - 1, &c) == 0);
	const uint8_t payload[] = {'A', 'G', '#', 0, 'B', '@'};
	CHECK(c.payload_size == sizeof(payload));
	CHECK(memcmp(c.payload, payload, sizeof(payload)) == 0);
	CHECK(c.commands == 2 && c.text_size == 3);
	CHECK(memcmp(c.text, "\r\n!", 3) == 0);
	return 0;
}

static int
readers(void)
{
	const uint8_t bytes[] = "G#L>1\r\n2,3_XX4,5,6:W 0,0,ok@";
	struct capture c = {0};
	CHECK(partitions(bytes, sizeof(bytes) - 1, &c) == 0);
	CHECK(c.numbers == 6 && c.operands[0].value == 12);
	CHECK(c.operands[1].value == 34 && c.operands[2].value == 5);
	const uint8_t operands[] = "G#L>x,y,r,R:L +2,-3,!4,12]:";
	memset(&c, 0, sizeof(c));
	CHECK(partitions(operands, sizeof(operands) - 1, &c) == 0);
	CHECK(c.numbers == 8 && c.operands[0].source == ST_GRAPHICS_LOOP_X);
	CHECK(c.operands[1].source == ST_GRAPHICS_LOOP_Y);
	CHECK(c.operands[2].source == ST_GRAPHICS_RANDOM_R);
	CHECK(c.operands[3].source == ST_GRAPHICS_RANDOM_R2);
	CHECK(c.operands[4].operation == ST_GRAPHICS_STEP_PLUS);
	CHECK(c.operands[5].operation == ST_GRAPHICS_STEP_MINUS);
	CHECK(c.operands[6].operation == ST_GRAPHICS_MINUS_STEP);
	CHECK(c.operands[7].source == ST_GRAPHICS_ARRAY && c.operands[7].value == 12);
	/* Native 128-byte text limit leaves the following @ unread (11). */
	uint8_t limit[160];
	memcpy(limit, "G#W 0,0,", 8);
	memset(limit + 8, 'a', 128);
	memcpy(limit + 136, "@end", 4);
	memset(&c, 0, sizeof(c));
	CHECK(partitions(limit, 140, &c) == 0);
	CHECK(c.payload_size == 128 && c.text_size == 4);
	CHECK(memcmp(c.text, "@end", 4) == 0);
	return 0;
}

static int
raw_fields(void)
{
	const uint8_t bytes[] = {'G','#','$','>','9',':',0,'G','#','@',17,19,'\r','\n',255,
	    '$','>','0',':','s',' ','1',':','!'};
	const uint8_t payload[] = {0,'G','#','@',17,19,'\r','\n',255};
	struct capture c = {0};
	CHECK(partitions(bytes, sizeof(bytes), &c) == 0);
	CHECK(c.commands == 3 && memcmp(c.codes, "$$s", 3) == 0);
	CHECK(c.payload_size == sizeof(payload));
	CHECK(memcmp(c.payload, payload, sizeof(payload)) == 0);
	CHECK(c.text_size == 1 && c.text[0] == '!');
	return 0;
}

static int
pause_and_errors(void)
{
	st_graphics_parser_t *p = NULL;
	struct capture c = {.suspend = true};
	struct st_graphics_parser_sink s = sink(&c);
	const uint8_t bytes[] = "G#s>1:C 2,3:tail";
	size_t used = 99;
	CHECK(create(&p, &igs, 16, 1024) == 0);
	CHECK(st_graphics_parser_feed(p, &s, bytes, sizeof(bytes)-1, &used) == IO_ERROR_IN_PROGRESS);
	CHECK(used == 6 && c.commands == 1);
	CHECK(st_graphics_parser_in_command(p));
	CHECK(st_graphics_parser_feed(p, &s, bytes+6, 4, &used) == IO_ERROR_IN_PROGRESS);
	CHECK(used == 0 && c.commands == 1);
	CHECK(st_graphics_parser_finish(p, &s) == IO_ERROR_IN_PROGRESS);
	c.suspend = false;
	CHECK(st_graphics_parser_resume(p) == 0);
	CHECK(st_graphics_parser_resume(p) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_graphics_parser_feed(p, &s, bytes+6, sizeof(bytes)-7, &used) == 0);
	CHECK(c.commands == 2 && c.text_size == 4);
	CHECK(!st_graphics_parser_in_command(p));
	const uint8_t truncated[] = "G#W 2,3,part";
	CHECK(st_graphics_parser_feed(p, &s, truncated, sizeof(truncated)-1, &used) == 0);
	CHECK(st_graphics_parser_finish(p, &s) == IO_ERROR_PROTOCOL);
	CHECK(!st_graphics_parser_in_command(p) && c.commands == 2);
	const uint8_t malformed[] = "G#s 12345:ignored";
	CHECK(st_graphics_parser_feed(p, &s, malformed, sizeof(malformed)-1, &used) == IO_ERROR_OUT_OF_RANGE);
	CHECK(used == 10 && c.commands == 2);
	CHECK(st_graphics_parser_feed(p, &s, (const uint8_t *)"G#s 4:", 6, &used) == 0);
	CHECK(c.commands == 3 && c.operands[c.numbers-1].value == 4);
	c.fail = IO_ERROR_IO;
	CHECK(st_graphics_parser_feed(p, &s, (const uint8_t *)"G#s 5:extra", 11, &used) == IO_ERROR_IO);
	CHECK(used == 6 && c.commands == 4 && !st_graphics_parser_in_command(p));
	c.fail = 0;
	CHECK(st_graphics_parser_feed(p, &s, (const uint8_t *)"G#W 0,0,ab", 10, &used) == 0);
	st_graphics_parser_cancel(p);
	CHECK(st_graphics_parser_finish(p, &s) == 0 && c.commands == 4);
	st_graphics_parser_destroy(p);
	/* Resource limits are checked before accepting a field's data. */
	CHECK(create(&p, &igs, 1, 4) == 0);
	CHECK(st_graphics_parser_feed(p, &s, (const uint8_t *)"G#C 1,2:", 8, &used) == IO_ERROR_OUT_OF_RANGE);
	CHECK(used == 6);
	st_graphics_parser_destroy(p);
	CHECK(create(&p, &igs, 2, 4) == 0);
	CHECK(st_graphics_parser_feed(p, &s, (const uint8_t *)"G#$ 5:abcde", 11, &used) == IO_ERROR_OUT_OF_RANGE);
	CHECK(used == 6);
	st_graphics_parser_destroy(p);
	return 0;
}

static int
boundaries_and_validation(void)
{
	st_graphics_parser_t *p = NULL;
	struct capture c = {0};
	struct st_graphics_parser_sink s = sink(&c);
	size_t used = 99;
	CHECK(st_graphics_parser_create(&p, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(p == NULL);
	CHECK(create(&p, &igs, 0, 0) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(create(&p, &igs, 16, 0) == 0);
	CHECK(st_graphics_parser_feed(p, &s, NULL, 1, &used) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(used == 0);
	CHECK(st_graphics_parser_feed(p, &s, NULL, 0, &used) == 0 && used == 0);
	CHECK(st_graphics_parser_feed(p, &s, (const uint8_t *)"G#s 9999:", 9, &used) == 0);
	CHECK(c.commands == 1 && c.operands[0].value == 9999);
	CHECK(st_graphics_parser_feed(p, &s, (const uint8_t *)"G#s 12+3:", 9, &used) == IO_ERROR_PROTOCOL);
	CHECK(c.commands == 1 && used == 7);
	CHECK(st_graphics_parser_feed(p, &s, (const uint8_t *)"G", 1, &used) == 0);
	CHECK(c.text_size == 0);
	CHECK(st_graphics_parser_begin(p, &s) == IO_ERROR_BUSY);
	CHECK(st_graphics_parser_finish(p, &s) == 0);
	CHECK(c.text_size == 1 && c.text[0] == 'G');
	/* Every incomplete header/field class is rejected at explicit EOF. */
	const char *partial[] = {"G#", "G#s", "G#s ", "G#s 12_", "G#s x",
	    "G#s 12]", "G#$ 1:"};
	for (size_t i = 0; i < sizeof(partial)/sizeof(*partial); ++i) {
		int status = st_graphics_parser_feed(p, &s,
		    (const uint8_t *)partial[i], strlen(partial[i]), &used);
		if (i == 6)
			CHECK(status == IO_ERROR_OUT_OF_RANGE);
		else {
			CHECK(status == 0);
			CHECK(st_graphics_parser_finish(p, &s) == IO_ERROR_PROTOCOL);
		}
		CHECK(c.commands == 1);
	}
	st_graphics_parser_destroy(p);
	/* Multi-byte overlapping attention has the same byte ownership. */
	const uint8_t prefix[] = {'a','b','a'};
	struct st_graphics_dialect d = {prefix, 3, true, '>', true, 4};
	CHECK(create(&p, &d, 16, 128) == 0);
	memset(&c, 0, sizeof(c));
	const uint8_t overlap[] = "aababs 2:";
	for (size_t i = 0; i < sizeof(overlap)-1; ++i)
		CHECK(st_graphics_parser_feed(p, &s, overlap+i, 1, &used) == 0);
	CHECK(st_graphics_parser_finish(p, &s) == 0);
	/* 'a' is ordinary; 'aba' enters graphics, then unknown b consumes s as
	 * its chain byte. Remaining space/2/colon are ordinary text. */
	CHECK(c.unknown == 1 && c.text_size == 4);
	CHECK(memcmp(c.text, "a 2:", 4) == 0);
	st_graphics_parser_destroy(p);
	d = igs;
	d.number_skip_crlf = false;
	CHECK(create(&p, &d, 16, 128) == 0);
	memset(&c, 0, sizeof(c));
	CHECK(st_graphics_parser_feed(p, &s, (const uint8_t *)"G#s 1\n2:", 8, &used) == 0);
	CHECK(c.commands == 1 && c.operands[0].value == 1 && c.text_size == 2);
	st_graphics_parser_destroy(p);
	return 0;
}

static int
alternate_framing(void)
{
	/* Extension contract only: not a BIG or CONDOR implementation. */
	const uint8_t big_attention[] = {'I','#'};
	struct st_graphics_dialect dialect = igs;
	dialect.attention = big_attention;
	st_graphics_parser_t *p = NULL;
	struct capture c = {0};
	struct st_graphics_parser_sink s = sink(&c);
	size_t used;
	CHECK(create(&p, &dialect, 16, 1024) == 0);
	const uint8_t bytes[] = "G#s 9:I#s 2:";
	CHECK(st_graphics_parser_feed(p, &s, bytes, sizeof(bytes)-1, &used) == 0);
	CHECK(c.commands == 1 && c.operands[0].value == 2 && c.text_size == 6);
	st_graphics_parser_destroy(p);
	dialect.chain_indicator = false;
	CHECK(create(&p, &dialect, 16, 1024) == 0);
	memset(&c, 0, sizeof(c));
	CHECK(st_graphics_parser_begin(p, &s) == 0);
	CHECK(st_graphics_parser_begin(p, &s) == IO_ERROR_BUSY);
	CHECK(st_graphics_parser_feed(p, &s, (const uint8_t *)"s3:tail", 7, &used) == 0);
	CHECK(c.commands == 1 && c.operands[0].value == 3 && c.text_size == 4);
	st_graphics_parser_destroy(p);
	return 0;
}

static int
field_entry(void *context, const struct st_graphics_command_view *view,
    const struct st_graphics_parameter *parameter)
{
	struct capture *c = context;
	++c->fields;
	if (view->code == 'W') {
		if (view->count > 2 || parameter->kind != (view->count < 2 ? ST_GRAPHICS_NUMBER : ST_GRAPHICS_TEXT))
			return IO_ERROR_PROTOCOL;
		if (view->count == 2 && (view->arguments[0].operand.value != 1 || view->arguments[1].operand.value != 2))
			return IO_ERROR_PROTOCOL;
	}
	return parameter->kind == ST_GRAPHICS_TEXT ? c->field_fail : 0;
}

static int
field_entries(void)
{
	const uint8_t bytes[] = "G#W 1,2,ABC@";
	for (size_t split = 0; split <= sizeof(bytes); ++split) {
		st_graphics_parser_t *p = NULL; struct capture c = {0};
		struct st_graphics_parser_sink s = sink(&c); s.field = field_entry; size_t used;
		CHECK(create(&p,&igs,16,1024) == 0);
		if (split == sizeof(bytes)) {
			for (size_t i = 0; i < sizeof(bytes)-1; ++i)
				CHECK(st_graphics_parser_feed(p,&s,bytes+i,1,&used) == 0 && used == 1);
		} else {
			CHECK(st_graphics_parser_feed(p,&s,bytes,split,&used) == 0 && used == split);
			CHECK(st_graphics_parser_feed(p,&s,bytes+split,sizeof(bytes)-1-split,&used) == 0 && used == sizeof(bytes)-1-split);
		}
		CHECK(c.fields == 3 && c.commands == 1 && c.payload_size == 3 && memcmp(c.payload,"ABC",3) == 0);
		st_graphics_parser_destroy(p);
	}
	st_graphics_parser_t *p = NULL; struct capture c = {0};
	struct st_graphics_parser_sink s = sink(&c); s.field = field_entry; size_t used;
	CHECK(create(&p,&igs,16,1024) == 0);
	CHECK(st_graphics_parser_feed(p,&s,(const uint8_t *)"G#W 1,2,",8,&used) == 0 && used == 8);
	CHECK(c.fields == 3 && c.commands == 0); /* Entry precedes payload arrival. */
	CHECK(st_graphics_parser_feed(p,&s,NULL,0,&used) == 0 && c.fields == 3);
	st_graphics_parser_cancel(p); memset(&c,0,sizeof(c));
	CHECK(st_graphics_parser_feed(p,&s,(const uint8_t *)"G#$ 0:",6,&used) == 0 && c.fields == 2 && c.commands == 1);
	for (unsigned error = 0; error < 2; ++error) {
		memset(&c,0,sizeof(c)); c.field_fail = error ? IO_ERROR_IN_PROGRESS : IO_ERROR_OUT_OF_MEMORY;
		CHECK(st_graphics_parser_feed(p,&s,bytes,sizeof(bytes)-1,&used) == (error ? IO_ERROR_PROTOCOL : IO_ERROR_OUT_OF_MEMORY));
		CHECK(used == 8 && c.fields == 3 && c.commands == 0 && !st_graphics_parser_in_command(p));
		c.field_fail = 0;
		CHECK(st_graphics_parser_feed(p,&s,(const uint8_t *)"tail",4,&used) == 0 && c.text_size == 4);
	}
	st_graphics_parser_destroy(p); return 0;
}

/* Owner completion is an explicit header event, not input-byte injection. */
static int
header_completion(void)
{
	for (size_t split = 0; split <= 3; ++split) {
		st_graphics_parser_t *p = NULL; struct capture c = {0};
		struct st_graphics_parser_sink s = sink(&c); size_t used;
		CHECK(create(&p,&igs,16,1024) == 0);
		CHECK(!st_graphics_parser_header_remaining(p));
		CHECK(st_graphics_parser_complete_header(p,&s,'Q',false) == IO_ERROR_BUSY);
		CHECK(st_graphics_parser_feed(p,&s,(const uint8_t *)"G#s",split,&used) == 0 && used == split);
		CHECK(st_graphics_parser_feed(p,&s,(const uint8_t *)"G#s"+split,3-split,&used) == 0 && used == 3-split);
		CHECK(st_graphics_parser_header_remaining(p));
		CHECK(st_graphics_parser_complete_header(p,&s,'Q',false) == 0);
		CHECK(!st_graphics_parser_header_remaining(p) && c.commands == 0);
		CHECK(st_graphics_parser_complete_header(p,&s,'Q',false) == IO_ERROR_BUSY);
		CHECK(st_graphics_parser_feed(p,&s,(const uint8_t *)"7:tail",6,&used) == 0 && used == 6);
		CHECK(c.commands == 1 && c.codes[0] == 's' && c.operands[0].value == 7);
		CHECK(c.text_size == 4 && memcmp(c.text,"tail",4) == 0 && c.boundaries == 1);
		/* No identifier: use the explicit fallback and honor chaining. */
		CHECK(st_graphics_parser_feed(p,&s,(const uint8_t *)"G#",2,&used) == 0);
		CHECK(st_graphics_parser_complete_header(p,&s,'Q',true) == 0);
		CHECK(c.commands == 2 && c.codes[1] == 'Q' && st_graphics_parser_header_remaining(p));
		CHECK(st_graphics_parser_complete_header(p,&s,'@',false) == 0);
		CHECK(c.unknown == 1 && !st_graphics_parser_in_command(p));
		/* Accepted suspension retains its own return boundary. */
		c.suspend = true;
		CHECK(st_graphics_parser_feed(p,&s,(const uint8_t *)"G#Q",3,&used) == 0);
		CHECK(st_graphics_parser_complete_header(p,&s,0,false) == IO_ERROR_IN_PROGRESS);
		CHECK(!st_graphics_parser_header_remaining(p) && c.commands == 3);
		CHECK(st_graphics_parser_complete_header(p,&s,0,false) == IO_ERROR_BUSY);
		CHECK(st_graphics_parser_resume(p) == 0);
		/* A callback error cancels, with its completed effect retained. */
		c.suspend = false; c.fail = IO_ERROR_IO;
		CHECK(st_graphics_parser_feed(p,&s,(const uint8_t *)"G#Q",3,&used) == 0);
		CHECK(st_graphics_parser_complete_header(p,&s,0,false) == IO_ERROR_IO);
		CHECK(!st_graphics_parser_in_command(p) && c.commands == 4);
		st_graphics_parser_destroy(p);
	}
	return 0;
}

static int unknown_command(void *context, const struct st_graphics_command_view *view)
{
	struct capture *c=context;
	CHECK(view->count==0 && c->unknown==c->commands+1);
	return command(context,view);
}
static int unknown_completion(void)
{
	for (size_t split=0;split<=4;++split) {
		st_graphics_parser_t *p=NULL; struct capture c={.suspend=true};
		struct st_graphics_parser_sink s=sink(&c); s.unknown_command=unknown_command; size_t used;
		CHECK(create(&p,&igs,16,1024)==0);
		const uint8_t wire[]="G#@>Q tail";
		/* Feed both sides of every header split; unknown owns only pair. */
		size_t count=split<4?split:4;
		CHECK(st_graphics_parser_feed(p,&s,wire,count,&used)==(count==4?IO_ERROR_IN_PROGRESS:0) && used==count);
		if (count<4) CHECK(st_graphics_parser_feed(p,&s,wire+count,sizeof(wire)-1-count,&used)==IO_ERROR_IN_PROGRESS && used==4-count);
		CHECK(c.commands==1 && c.codes[0]=='@' && c.unknown==1 && c.text_size==0);
		CHECK(st_graphics_parser_feed(p,&s,NULL,0,&used)==IO_ERROR_IN_PROGRESS && c.commands==1);
		CHECK(st_graphics_parser_resume(p)==0); c.suspend=false;
		CHECK(st_graphics_parser_feed(p,&s,wire+4,sizeof(wire)-5,&used)==0);
		CHECK(c.commands==2 && c.codes[1]=='Q' && c.text_size==4 && memcmp(c.text,"tail",4)==0);
		/* Source-selected unknowns retain normal legacy rejection unless opted in. */
		CHECK(st_graphics_parser_start_command(p,&s,'@',false)==0);
		c.suspend=true;
		/* The test callback expects its notification count relative to unknown calls. */
		c.commands=0; c.unknown=0;
		CHECK(st_graphics_parser_feed(p,&s,NULL,0,&used)==IO_ERROR_IN_PROGRESS && c.commands==1 && c.unknown==1);
		CHECK(st_graphics_parser_finish(p,&s)==IO_ERROR_IN_PROGRESS);
		st_graphics_parser_cancel(p); c.commands=0; c.unknown=0; c.fail=IO_ERROR_IO;
		CHECK(st_graphics_parser_feed(p,&s,wire,4,&used)==IO_ERROR_IO && used==4 && !st_graphics_parser_in_command(p));
		CHECK(c.commands==1 && c.unknown==1);
		s.unknown_command=NULL;
		CHECK(st_graphics_parser_start_command(p,&s,'@',false)==IO_ERROR_NOT_SUPPORTED);
		st_graphics_parser_destroy(p);
	}
	return 0;
}

int
main(void)
{
	CHECK(unknown_completion() == 0);
	CHECK(header_completion() == 0);
	CHECK(field_entries() == 0);
	CHECK(literal_examples() == 0);
	CHECK(readers() == 0);
	CHECK(raw_fields() == 0);
	CHECK(pause_and_errors() == 0);
	CHECK(alternate_framing() == 0);
	CHECK(boundaries_and_validation() == 0);
	return 0;
}
