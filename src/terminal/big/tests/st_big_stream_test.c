/* Independent reader expectations from BIG chapters7/8/62/64/73. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_stream.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture {
	struct st_big_numeric numeric;
	struct st_big_text text;
	struct st_big_control control;
	struct st_big_stream_reader reader;
	struct st_big_control_sink sink;
	uint64_t now;
	unsigned bells, sent;
	bool block;
};
static int now(void *context, uint64_t *value) { *value=((struct fixture *)context)->now; return 0; }
static void bell(void *context) { ++((struct fixture *)context)->bells; }
static int ready(void *context, enum st_big_output_kind kind)
{
	CHECK(kind==ST_BIG_OUTPUT_XON); return ((struct fixture *)context)->block?IO_ERROR_IN_PROGRESS:0;
}
static int send(void *context, enum st_big_output_kind kind, uint8_t byte)
{
	struct fixture *f=context; CHECK(kind==ST_BIG_OUTPUT_XON && byte==17); ++f->sent; return 0;
}
static void init(struct fixture *f)
{
	memset(f,0,sizeof(*f)); f->now=100;
	const struct st_big_numeric_io io={.context=f,.now_ns=now,.bell=bell};
	CHECK(st_big_numeric_init(&f->numeric,&io)==0); st_big_control_init(&f->control);
	f->sink=(struct st_big_control_sink){f,ready,send};
}
static bool poll(struct fixture *f, const uint8_t *bytes, size_t size, size_t expected)
{
	size_t used=999, budget=64; bool complete=false;
	CHECK(st_big_stream_poll(&f->reader,&f->numeric,&f->text,&f->control,&f->sink,
	    bytes,size,&used,&budget,&complete)==0 && used==expected);
	return complete;
}
static void text_vectors(void)
{
	static const struct { const uint8_t *wire, *result; size_t size, used, count; } cases[]={
		{(const uint8_t *)"Hi@L",(const uint8_t *)"Hi",4,3,2},
		{(const uint8_t *)"@L",(const uint8_t *)"@",2,1,1},
		{(const uint8_t *)"\r\n@L",(const uint8_t *)"@",4,3,1},
		{(const uint8_t *)"A\r\nB@L",(const uint8_t *)"AB",6,5,2},
		{(const uint8_t *)"A\0B@L",(const uint8_t *)"A\0B",5,4,3},
		{(const uint8_t *)"I#_\xff@L",(const uint8_t *)"I#_\xff",6,5,4},
	};
	for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) for (size_t split=0;split<=cases[i].size;++split) {
		struct fixture f; init(&f); memset(f.text.bytes,'Q',sizeof(f.text.bytes));
		CHECK(st_big_stream_text(&f.reader,&f.numeric,&f.text,&f.control)==0);
		size_t first=split<cases[i].used?split:cases[i].used;
		bool done=poll(&f,cases[i].wire,split,first);
		if (!done) CHECK(poll(&f,cases[i].wire+split,cases[i].size-split,cases[i].used-split));
		CHECK(f.text.count==cases[i].count && f.text.active==0 && f.numeric.loop.command_read);
		CHECK(memcmp(f.text.bytes,cases[i].result,cases[i].count+1)==0 && f.text.bytes[cases[i].count+1]=='Q');
		f.numeric.loop.command_read=false; CHECK(poll(&f,NULL,0,0) && !f.numeric.loop.command_read && f.bells==0);
	}
	struct fixture f; init(&f); uint8_t bytes[130]; memset(bytes,'A',128); bytes[128]='@'; bytes[129]='Z';
	CHECK(st_big_stream_text(&f.reader,&f.numeric,&f.text,&f.control)==0);
	CHECK(poll(&f,bytes,sizeof(bytes),128) && f.text.count==128 && f.text.bytes[128]==0);
	for (size_t i=0;i<128;++i) CHECK(f.text.bytes[i]=='A');
}
static void deadlines(void)
{
	struct fixture f; init(&f); memcpy(f.text.bytes,"OLD",4);
	CHECK(st_big_stream_text(&f.reader,&f.numeric,&f.text,&f.control)==0);
	CHECK(!poll(&f,(const uint8_t *)"N",1,1));
	f.now+=ST_BIG_NUMERIC_TIMEOUT_NS-1; CHECK(!poll(&f,NULL,0,0));
	++f.now; CHECK(poll(&f,NULL,0,0));
	CHECK(f.bells==1 && f.text.count==1 && memcmp(f.text.bytes,"NLD",4)==0);
	CHECK(poll(&f,NULL,0,0) && f.bells==1);
	CHECK(st_big_stream_text(&f.reader,&f.numeric,&f.text,&f.control)==0);
	f.now+=ST_BIG_NUMERIC_TIMEOUT_NS; CHECK(poll(&f,(const uint8_t *)"@Z",2,1));
	CHECK(f.bells==2 && f.text.count==1 && f.text.bytes[0]=='@' && f.text.bytes[1]==0);
	for (unsigned automatic=0;automatic<2;++automatic) {
		init(&f); f.numeric.context.auto_mode=automatic;
		CHECK(st_big_stream_aux(&f.reader,&f.numeric)==0);
		CHECK(!poll(&f,NULL,0,0)); f.now+=ST_BIG_NUMERIC_TIMEOUT_NS;
		CHECK(poll(&f,(const uint8_t *)"I@",2,1) && f.reader.byte==' ' && f.bells==!automatic);
		CHECK(poll(&f,NULL,0,0) && f.bells==!automatic);
	}
	init(&f); CHECK(st_big_stream_aux(&f.reader,&f.numeric)==0);
	CHECK(poll(&f,(const uint8_t *)"\xffZ",2,1) && f.reader.byte==255);
	CHECK(st_big_stream_aux(&f.reader,&f.numeric)==0); --f.now;
	size_t used=99, budget=1; bool complete=true;
	CHECK(st_big_stream_poll(&f.reader,&f.numeric,&f.text,&f.control,&f.sink,
	    (const uint8_t *)"X",1,&used,&budget,&complete)==IO_ERROR_OUT_OF_RANGE && used==0 && !complete);
}
static void reuse_and_flow(void)
{
	struct fixture f;
	for (unsigned active=0;active<3;++active) for (unsigned type=0;type<3;++type) {
		init(&f); memcpy(f.text.bytes,"OLD",4); f.numeric.loop.active=(uint16_t)active;
		f.numeric.loop.type=type==0?'@':type==1?'Z':'|';
		CHECK(st_big_stream_text(&f.reader,&f.numeric,&f.text,&f.control)==0);
		bool reuse=(active==1 && type!=0) || (active==0 && type==1);
		CHECK(poll(&f,(const uint8_t *)"N@",2,reuse?0:2));
		CHECK(strcmp((char *)f.text.bytes,reuse?"OLD":"N")==0);
		CHECK(f.sent==0);
		CHECK(f.numeric.loop.type==(active==0 && type==1?' ':(type==0?'@':type==1?'Z':'|')));
	}
	init(&f); f.numeric.loop.active=1; f.numeric.loop.type='@'; f.block=true;
	CHECK(st_big_stream_text(&f.reader,&f.numeric,&f.text,&f.control)==0 && f.numeric.loop.first_text==1);
	CHECK(poll(&f,(const uint8_t *)"A@",2,2) && f.sent==0 && f.bells==0 && f.text.count==1);
	CHECK(f.text.bytes[0]=='A' && f.text.bytes[1]==0);
	f.numeric.loop.first_text=65535;
	CHECK(st_big_stream_text(&f.reader,&f.numeric,&f.text,&f.control)==0 && f.numeric.loop.first_text==0);
	CHECK(poll(&f,(const uint8_t *)"@",1,1) && f.sent==0);
}
int main(void) { text_vectors(); deadlines(); reuse_and_flow(); return 0; }
