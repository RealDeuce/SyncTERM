/* Literal BIG reader cases and independent binary-rational scaling oracle. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_number.h"
#include "st_graphics_parser.h"
#include "io_error.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)

struct vector {
	const char *wire;
	size_t size, consumed;
	uint16_t word;
	struct st_big_number_context context;
};
#define V(w,u,n,g,r,y) {w,sizeof(w)-1,u,(uint16_t)(n),{g,r,y,false}}
/* Hand transcribed from reference chapters 5/34 and numeric-examples.json;
 * ordered auxiliary cases follow original 04f3:1129..1189. No emulator
 * output or host floating multiplication supplies expected results. */
static const struct vector vectors[] = {
	V("1234:tail",5,1234,0,1,false), V("12345:tail",6,0,0,1,false),
	V("00001,tail",6,0,0,1,false), V("\r12,",1,0,0,1,false),
	V("\n12,",1,0,0,1,false), V(" 12,",1,0,0,1,false),
	V("_\r\n12:",6,12,0,1,false), V("_xy12:",6,12,0,1,false),
	V("1_\r\n2:",6,12,0,1,false), V("12_\r\n34:",8,10234,0,1,false),
	V("12_\r\n:",6,102,0,1,false), V("12_\r\n_\r\n:",9,1002,0,1,false),
	V("123_\r\n4:",8,36498,0,1,false), V("12+34:",6,-10234,0,1,false),
	V("12+:",4,-102,0,1,false), V("+12:",4,-12,0,1,false),
	V("-12:",4,-10012,0,1,false), V("!12:",4,-20012,0,1,false),
	V("12++:",5,-1002,0,1,false), V("+-!12:",6,-20012,0,1,false),
	V("x:tail",2,0x8001,0,1,false), V("xy:tail",3,0x8002,0,1,false),
	V("xyr:tail",4,0x8003,0,1,false), V("xr:tail",3,0x8003,0,1,false),
	V("yx:tail",2,0x8002,0,1,false), V("rx:tail",2,0x8003,0,1,false),
	V("R:tail",1,0,0,1,false), V("12]:tail",3,12,0,1,false),
	V("+r:tail",3,32765,0,1,false), V("12x+34:",7,-10234,0,1,false),
	V("2\0tail",2,2,0,1,false), V("2\xff" "tail",2,2,0,1,false),
	V("1:",2,1,2,2,true), V("2:",2,3,2,2,true),
	V("5:",2,8,2,2,true), V("9999:",5,17498,2,2,true),
	V("5:",2,11,3,3,true), V("10:",3,23,3,3,true),
	V("100:",4,239,3,3,true), V("4:",2,9,3,3,true),
	V("199:",4,477,3,3,true), V("200:",4,479,3,3,true),
	V("0:",2,0,3,3,true), V("5:",2,5,3,3,false),
	V("5:",2,5,1,3,true), V("5:",2,5,2,3,true),
	V("5:",2,5,65535,3,true), V("5:",2,5,3,255,true),
	V("-12:",4,-10021,2,2,true), V("x:",2,0x8001,3,3,true),
	V("12_\r\n34:",8,17909,2,2,true), V("12_\r\n34:",8,24561,3,3,true),
	V("123_\r\n4:",8,-4155,3,3,true),
	V("123_\r\n4:",8,14720,2,2,true),
};
#undef V

static int
literal_partitions(void)
{
	for (size_t v = 0; v < sizeof(vectors)/sizeof(*vectors); ++v) {
		const struct vector *a = &vectors[v];
		for (size_t split = 0; split <= a->size+1; ++split) {
			struct st_big_number_reader reader;
			struct st_big_number_result result = {0};
			size_t total = 0, used;
			st_big_number_begin(&reader);
			if (split <= a->size) {
				CHECK(st_big_number_feed(&reader,&a->context,
				    (const uint8_t *)a->wire,split,false,&used,&result) == 0);
				total += used;
				if (!result.complete) {
					CHECK(used == split);
					CHECK(st_big_number_feed(&reader,&a->context,
					    (const uint8_t *)a->wire+split,a->size-split,false,&used,&result) == 0);
					total += used;
				}
			} else {
				for (size_t i = 0; i < a->size && !result.complete; ++i) {
					CHECK(st_big_number_feed(&reader,&a->context,
					    (const uint8_t *)a->wire+i,1,false,&used,&result) == 0 && used == 1);
					total += used;
				}
			}
			if (!result.complete || total != a->consumed || result.word != a->word) {
				fprintf(stderr,"vector %zu split %zu: complete=%d consumed=%zu word=%u expected=%u\n",
				    v,split,result.complete,total,result.word,a->word);
				return 1;
			}
			CHECK(!result.bell && !st_big_number_auxiliary(&reader));
			CHECK(st_big_number_feed(&reader,&a->context,(const uint8_t *)"unused",6,
			    true,&used,&result) == 0 && used == 0 && result.complete && result.word == a->word && !result.bell);
		}
	}
	return 0;
}

static int
word_domain(void)
{
	/* Multiply the original binary64 rational in two 32-bit limbs. Dividing
	 * that exact product by 2^51 is independent of the implementation's
	 * finite integer formula and never uses floating-point rounding. */
	const uint64_t numerator = UINT64_C(0x13333333333333);
	for (uint32_t raw = 0; raw <= UINT16_MAX; ++raw) {
		int32_t n = raw <= 32767 ? (int32_t)raw : (int32_t)raw-65536;
		CHECK(st_big_number_signed((uint16_t)raw) == n);
		uint32_t magnitude = (uint32_t)(n < 0 ? -n : n);
		uint64_t low = (uint64_t)magnitude*(uint32_t)numerator;
		uint64_t high = (uint64_t)magnitude*(numerator>>32)+(low>>32);
		int32_t q3 = (int32_t)(high>>19);
		int32_t q2 = (int32_t)(magnitude*7/4);
		if (n < 0) { q2 = -q2; q3 = -q3; }
		for (unsigned g = 0; g <= 4; ++g) {
			for (unsigned resolution = 1; resolution <= 3; ++resolution) {
				for (unsigned y = 0; y < 2; ++y) {
					struct st_big_number_context context = {
						(uint16_t)g,(uint8_t)resolution,y != 0,false};
					uint16_t expected = (uint16_t)raw;
					if (y && g == 2 && resolution == 2) expected = (uint16_t)q2;
					if (y && g == 3 && resolution == 3) expected = (uint16_t)q3;
					CHECK(st_big_number_scale((uint16_t)raw,&context) == expected);
				}
			}
		}
	}
	const struct st_big_number_context g2 = {2,2,true,false}, g3 = {3,3,true,false};
	CHECK(st_big_number_scale(0x8000,&g2) == 8192);
	CHECK(st_big_number_scale((uint16_t)-5,&g3) == (uint16_t)-11);
	return 0;
}

static int
count_wrap(void)
{
	static uint8_t bytes[65540];
	memset(bytes,'9',sizeof(bytes));
	memcpy(bytes,"1234",4);
	memcpy(bytes+65536,"56:!",4);
	const size_t chunks[] = {sizeof(bytes),1,32768,65535,65536};
	const struct st_big_number_context context = {0};
	for (size_t i = 0; i < sizeof(chunks)/sizeof(*chunks); ++i) {
		struct st_big_number_reader reader;
		struct st_big_number_result result = {0};
		st_big_number_begin(&reader);
		size_t offset = 0;
		while (!result.complete) {
			size_t size = sizeof(bytes)-offset, used;
			if (size > chunks[i]) size = chunks[i];
			CHECK(st_big_number_feed(&reader,&context,bytes+offset,size,false,&used,&result) == 0);
			CHECK(used != 0);
			offset += used;
		}
		CHECK(offset == 65539 && result.word == 56 && !result.bell);
	}
	return 0;
}

static int
timeouts(void)
{
	struct st_big_number_reader reader;
	struct st_big_number_result result;
	struct st_big_number_context context = {0};
	size_t used;
	st_big_number_begin(&reader);
	CHECK(st_big_number_feed(&reader,&context,(const uint8_t *)"12_\r\n",5,false,&used,&result) == 0);
	CHECK(used == 5 && !result.complete && !st_big_number_auxiliary(&reader));
	CHECK(st_big_number_feed(&reader,&context,NULL,0,true,&used,&result) == 0);
	CHECK(used == 0 && result.complete && result.bell && result.word == 102);
	/* Expiry overrides an actually acquired byte; its suffix stays unread. */
	for (unsigned auto_mode = 0; auto_mode < 2; ++auto_mode) {
		context.auto_mode = auto_mode != 0;
		st_big_number_begin(&reader);
		CHECK(st_big_number_feed(&reader,&context,(const uint8_t *)"9:tail",6,true,&used,&result) == 0);
		CHECK(used == 1 && result.complete && result.bell && result.word == 0);
		st_big_number_begin(&reader);
		CHECK(st_big_number_feed(&reader,&context,(const uint8_t *)"x",1,false,&used,&result) == 0);
		CHECK(used == 1 && !result.complete && st_big_number_auxiliary(&reader));
		CHECK(st_big_number_feed(&reader,&context,(const uint8_t *)"y:",2,true,&used,&result) == 0);
		CHECK(used == 1 && result.complete && result.word == 0x8001 && result.bell == !auto_mode);
		/* Each discarded continuation byte has a fresh owner deadline. */
		st_big_number_begin(&reader);
		CHECK(st_big_number_feed(&reader,&context,(const uint8_t *)"12_",3,false,&used,&result) == 0);
		for (unsigned discard = 0; discard < 2; ++discard) {
			CHECK(st_big_number_auxiliary(&reader));
			CHECK(st_big_number_feed(&reader,&context,NULL,0,true,&used,&result) == 0);
			CHECK(used == 0 && !result.complete && result.bell == !auto_mode);
			CHECK(st_big_number_feed(&reader,&context,NULL,0,false,&used,&result) == 0);
			CHECK(used == 0 && !result.complete && !result.bell);
		}
		CHECK(!st_big_number_auxiliary(&reader));
		CHECK(st_big_number_feed(&reader,&context,NULL,0,true,&used,&result) == 0);
		CHECK(result.complete && result.word == 102 && result.bell);
	}
	return 0;
}

static int
validation_and_reset(void)
{
	struct st_big_number_reader reader = {0}, before;
	struct st_big_number_result result;
	const struct st_big_number_context context = {0};
	size_t used = 99;
	CHECK(st_big_number_feed(&reader,&context,NULL,0,false,&used,&result) == IO_ERROR_INVALID_ARGUMENT && used == 0);
	st_big_number_begin(&reader);
	CHECK(st_big_number_feed(&reader,&context,(const uint8_t *)"12_",3,false,&used,&result) == 0);
	memcpy(&before,&reader,sizeof(reader));
	CHECK(st_big_number_feed(&reader,&context,NULL,1,false,&used,&result) == IO_ERROR_INVALID_ARGUMENT && used == 0);
	CHECK(st_big_number_feed(&reader,NULL,NULL,0,false,&used,&result) == IO_ERROR_INVALID_ARGUMENT && used == 0);
	CHECK(st_big_number_feed(&reader,&context,NULL,0,false,NULL,&result) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_number_feed(&reader,&context,NULL,0,false,&used,NULL) == IO_ERROR_INVALID_ARGUMENT && used == 0);
	CHECK(memcmp(&reader,&before,sizeof(reader)) == 0);
	st_big_number_cancel(&reader);
	CHECK(!st_big_number_auxiliary(&reader));
	CHECK(st_big_number_feed(&reader,&context,NULL,0,true,&used,&result) == IO_ERROR_INVALID_ARGUMENT);
	st_big_number_begin(&reader);
	CHECK(st_big_number_feed(&reader,&context,(const uint8_t *)"7:",2,false,&used,&result) == 0);
	CHECK(used == 2 && result.complete && result.word == 7 && !result.bell);
	return 0;
}

/* Exercise actual BIG acquisition through the shared parser boundary. */
struct capture {
	struct st_big_number_reader reader;
	struct st_big_number_context context;
	int values[8];
	size_t fields, numbers, commands, text_size;
	uint8_t text[16];
	bool supply_y, expired;
	unsigned bells;
};
static int
text(void *context, const uint8_t *bytes, size_t size)
{
	struct capture *c = context;
	CHECK(size <= sizeof(c->text)-c->text_size);
	if (size != 0) memcpy(c->text+c->text_size,bytes,size);
	c->text_size += size;
	return 0;
}
static int
field(void *context, const struct st_graphics_command_view *view,
    const struct st_graphics_parameter *parameter)
{
	struct capture *c = context;
	(void)view;
	CHECK(parameter->kind == ST_GRAPHICS_NUMBER);
	st_big_number_begin(&c->reader);
	c->context.y_coordinate = parameter->axis == ST_GRAPHICS_Y;
	++c->fields;
	return 0;
}
static int
source(void *context, const struct st_graphics_command_view *view,
    const struct st_graphics_parameter *parameter, int *value, bool *supplied)
{
	struct capture *c = context;
	(void)view;
	*supplied = c->supply_y && parameter->axis == ST_GRAPHICS_Y;
	*value = 5;
	return 0;
}
static int
read_number(void *context, const struct st_graphics_command_view *view,
    const struct st_graphics_parameter *parameter, const uint8_t *bytes,
    size_t size, size_t *accepted, int *value, bool *complete)
{
	struct capture *c = context;
	(void)view; (void)parameter;
	struct st_big_number_result result;
	int status = st_big_number_feed(&c->reader,&c->context,bytes,size,c->expired,accepted,&result);
	c->expired = false;
	if (result.bell) ++c->bells;
	*complete = result.complete;
	*value = (int)st_big_number_signed(result.word);
	return status;
}
static int
command(void *context, const struct st_graphics_command_view *view)
{
	struct capture *c = context;
	CHECK(view->count == 2 && c->numbers <= 6);
	for (size_t i = 0; i < 2; ++i) {
		CHECK(view->arguments[i].has_value);
		c->values[c->numbers++] = view->arguments[i].value;
	}
	++c->commands;
	return 0;
}

static int
shared_parser(void)
{
	static const uint8_t attention[] = {'I','#'};
	static const struct st_graphics_dialect dialect = {
		.attention = attention,.attention_size = 2,.chain_indicator = true,
		.chain = '>',.number_skip_crlf = true,.number_digits = 4,
	};
	static const struct st_graphics_parameter pair[] = {
		{.kind = ST_GRAPHICS_NUMBER,.axis = ST_GRAPHICS_X},
		{.kind = ST_GRAPHICS_NUMBER,.axis = ST_GRAPHICS_Y},
	};
	static const struct st_graphics_command commands[] = {{'P',pair,2,NULL}};
	static const struct st_graphics_parser_config config = {
		.dialect = &dialect,.commands = commands,.command_count = 1,.max_arguments = 2,
	};
	const uint8_t wire[] = "I#P>12_\r\n34:5:P 7,8:tail";
	for (size_t split = 0; split <= sizeof(wire); ++split) {
		st_graphics_parser_t *parser = NULL;
		struct capture c = {.context = {.scaling = 3,.resolution = 3}};
		struct st_graphics_parser_sink sink = {.context = &c,.text = text,
			.command = command,.field = field,.number = source,.read_number = read_number};
		size_t used;
		CHECK(st_graphics_parser_create(&parser,&config) == 0);
		if (split < sizeof(wire)) {
			CHECK(st_graphics_parser_feed(parser,&sink,wire,split,&used) == 0 && used == split);
			CHECK(st_graphics_parser_feed(parser,&sink,wire+split,sizeof(wire)-1-split,&used) == 0 && used == sizeof(wire)-1-split);
		} else {
			for (size_t i = 0; i < sizeof(wire)-1; ++i)
				CHECK(st_graphics_parser_feed(parser,&sink,wire+i,1,&used) == 0 && used == 1);
		}
		CHECK(c.fields == 4 && c.commands == 2 && c.numbers == 4 && c.bells == 0);
		CHECK(c.values[0] == 10234 && c.values[1] == 11 && c.values[2] == 7 && c.values[3] == 19);
		CHECK(c.text_size == 4 && memcmp(c.text,"tail",4) == 0);
		st_graphics_parser_destroy(parser);
	}
	st_graphics_parser_t *parser = NULL;
	struct capture c = {.context = {.scaling = 3,.resolution = 3},.supply_y = true};
	struct st_graphics_parser_sink sink = {.context = &c,.text = text,
		.command = command,.field = field,.number = source,.read_number = read_number};
	size_t used;
	CHECK(st_graphics_parser_create(&parser,&config) == 0);
	CHECK(st_graphics_parser_feed(parser,&sink,(const uint8_t *)"I#P 7,tail",10,&used) == 0 && used == 10);
	CHECK(c.fields == 2 && c.commands == 1 && c.values[0] == 7 && c.values[1] == 5);
	CHECK(c.text_size == 4 && memcmp(c.text,"tail",4) == 0); /* Supplied Y is unscaled. */
	st_graphics_parser_cancel(parser); c = (struct capture){0};
	CHECK(st_graphics_parser_feed(parser,&sink,(const uint8_t *)"I#P +12:x",9,&used) == 0 && used == 9);
	CHECK(c.fields == 2 && c.commands == 0);
	c.expired = true;
	CHECK(st_graphics_parser_feed(parser,&sink,NULL,0,&used) == 0 && used == 0);
	CHECK(c.commands == 1 && c.values[0] == -12 && c.values[1] == -32767 && c.bells == 1);
	/* EOF rejects a partial reader; field entry resets its retained slots. */
	CHECK(st_graphics_parser_feed(parser,&sink,(const uint8_t *)"I#P 12_",7,&used) == 0);
	CHECK(st_graphics_parser_finish(parser,&sink) == IO_ERROR_PROTOCOL && c.commands == 1);
	CHECK(st_graphics_parser_feed(parser,&sink,(const uint8_t *)"I#P 2,3:",8,&used) == 0 && used == 8);
	CHECK(c.commands == 2 && c.values[2] == 2 && c.values[3] == 3);
	st_graphics_parser_destroy(parser);
	return 0;
}

int
main(void)
{
	CHECK(literal_partitions() == 0);
	CHECK(word_domain() == 0);
	CHECK(count_wrap() == 0);
	CHECK(timeouts() == 0);
	CHECK(validation_and_reset() == 0);
	CHECK(shared_parser() == 0);
	return 0;
}
