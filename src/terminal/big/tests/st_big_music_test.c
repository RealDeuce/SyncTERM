/* Independent original N15/67 arithmetic, record ordering and shared-state cases. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_music.h"
#include "st_big_control.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture {
	uint8_t bytes[ST_BIG_MUSIC_CAPACITY]; size_t capacity;
	struct st_big_music music;
	struct { uint16_t address,data; } output[32];
	uint16_t reads[64]; bool words[64]; unsigned read_count,writes;
	unsigned block_read,fail_read,block_write,fail_write,mutate;
};
static int read_value(void *context, uint16_t offset, bool word, uint16_t *value)
{
	struct fixture *f=context;
	if (f->read_count<64) { f->reads[f->read_count]=offset; f->words[f->read_count]=word; }
	++f->read_count;
	if (f->block_read==f->read_count) return IO_ERROR_IN_PROGRESS;
	if (f->fail_read==f->read_count) return IO_ERROR_IO;
	if ((size_t)offset>=f->capacity || (word && f->capacity-(size_t)offset<2)) return IO_ERROR_OUT_OF_RANGE;
	*value=f->bytes[offset]; if (word) *value|=(uint16_t)((uint16_t)f->bytes[offset+1]<<8);
	if (f->mutate==1 && !word) f->music.value_offset=9;
	return 0;
}
static int write_value(void *context, uint16_t address, uint16_t data)
{
	struct fixture *f=context; ++f->writes;
	if (f->block_write==f->writes) return IO_ERROR_IN_PROGRESS;
	if (f->fail_write==f->writes) return IO_ERROR_IO;
	CHECK(f->writes<=32); f->output[f->writes-1].address=address; f->output[f->writes-1].data=data;
	return 0;
}
static void init(struct fixture *f, const uint8_t *bytes, size_t size, uint16_t length)
{
	memset(f,0,sizeof(*f)); f->capacity=sizeof(f->bytes); CHECK(size<=f->capacity);
	if (size!=0) memcpy(f->bytes,bytes,size);
	f->music.length=length; f->music.running=1; f->music.position_low=2;
	f->music.end_low=(uint16_t)(length-1); f->music.delay=1;
}
static int poll(struct fixture *f, struct st_big_music_callback *r, size_t amount, uint16_t *result, bool *done)
{
	const struct st_big_music_sink sink={f,read_value,write_value};
	return st_big_music_callback_poll(r,&f->music,&sink,&amount,result,done);
}
static void arithmetic(void)
{
	static const uint16_t remainders[]={0,1,2,534,535,999,1000,32768,65535};
	for (unsigned word=0;word<65536;++word) {
		CHECK(st_big_music_selector((uint16_t)word)==(word<=5?word:2));
		for (size_t i=0;i<sizeof(remainders)/sizeof(*remainders);++i) {
			unsigned expected=(unsigned)(((uint64_t)word*1000+remainders[i])%65536);
			CHECK(st_big_music_pair((uint16_t)word,remainders[i],false)==expected);
			CHECK(st_big_music_pair((uint16_t)word,remainders[i],true)==(expected==65535?65534:expected));
		}
	}
	CHECK(st_big_music_pair(66,0,true)==464 && st_big_music_pair(65,535,true)==65534);
	CHECK(st_big_music_end(15,false,0)==14 && st_big_music_start(14,false,0)==2);
	CHECK(st_big_music_end(15,true,0)==1 && st_big_music_start(1,true,0)==UINT32_C(0xfffffffe));
	CHECK(st_big_music_end(30,true,11)==10 && st_big_music_start(10,true,4)==5);
	CHECK(st_big_music_end(15,true,300)==14 && st_big_music_start(14,true,20)==11);
	CHECK(st_big_music_end(0,false,0)==65535); /* Actual high0/lowWORD publication. */
	CHECK(st_big_music_end(1,true,65535)==0 && st_big_music_start(0,true,65535)==UINT32_C(0xfffffffd));
	for (unsigned word=0;word<65536;++word) {
		CHECK(st_big_music_end(65534,true,(uint16_t)word)==(word-word%3+1<65533?word-word%3+1:65533));
		unsigned rounded=word-word%3+2;
		CHECK(st_big_music_start(14,true,(uint16_t)word)==(rounded<11?rounded:11));
	}
}
static void original_callbacks(void)
{
	static const uint8_t two[]={0xb5,9,0x20,0x23,1,0xff,4,0,0x21,0x56,0,0xff,0,0,0xff};
	static const uint8_t boundary[]={0xb5,9,0x20,0x23,1,0xff,4,0,0xff,0,0,0xff};
	static const uint8_t stale[]={0xb5,9,0x20,0x23,1,0x21,0x56,0,0xff,0x34,0x12};
	for (unsigned budget=1;budget<=64;++budget) {
		struct fixture f; init(&f,two,sizeof(two),15); struct st_big_music_callback r; uint16_t returned=999; bool done=false;
		st_big_music_callback_begin(&r);
		for (unsigned turn=0;!done;++turn) { CHECK(turn<64); int status=poll(&f,&r,budget,&returned,&done); CHECK(status==(done?0:IO_ERROR_IN_PROGRESS)); }
		CHECK(returned==4 && f.music.running==1 && st_big_music_position(&f.music)==8 && f.writes==1);
		CHECK(f.output[0].address==0x20 && f.output[0].data==0x123 && f.music.address==255 && f.music.data==4);
		CHECK(f.read_count==4 && f.reads[0]==2 && !f.words[0] && f.reads[1]==3 && f.words[1] && f.reads[2]==5 && f.reads[3]==6);
		st_big_music_callback_begin(&r); done=false;
		while (!done) { int status=poll(&f,&r,budget,&returned,&done); CHECK(status==0 || status==IO_ERROR_IN_PROGRESS); }
		CHECK(returned==1 && f.music.running==0 && st_big_music_position(&f.music)==14 && f.writes==2 && f.output[1].address==0x21 && f.output[1].data==0x56);
		st_big_music_callback_begin(&r); done=false; unsigned reads=f.read_count;
		while (!done) { int status=poll(&f,&r,budget,&returned,&done); CHECK(status==(done?0:IO_ERROR_IN_PROGRESS)); }
		CHECK(returned==1 && f.read_count==reads && f.writes==2);
		f.music.delay=7; returned=999;
		CHECK(poll(&f,&r,0,&returned,&done)==0 && done && returned==1); /* Completed callback's returned value is stable. */
	}
	struct fixture f; struct st_big_music_callback r; uint16_t returned=999; bool done;
	init(&f,boundary,sizeof(boundary),12); st_big_music_callback_begin(&r);
	CHECK(poll(&f,&r,100,&returned,&done)==0 && done && returned==1 && f.music.data==4 && st_big_music_position(&f.music)==8);
	init(&f,stale,sizeof(stale),9); st_big_music_callback_begin(&r);
	CHECK(poll(&f,&r,100,&returned,&done)==0 && done && returned==1 && f.writes==2 && f.music.data==0x1234 && st_big_music_position(&f.music)==11);
	/* Actual retained allocation supplies the ff WORD, not loaded length9. */
	init(&f,stale,sizeof(stale),9); f.capacity=9; st_big_music_callback_begin(&r);
	CHECK(poll(&f,&r,100,&returned,&done)==IO_ERROR_OUT_OF_RANGE && !done && f.writes==2 && st_big_music_position(&f.music)==8);
	CHECK(f.music.running==0 && f.music.value_offset==9);
	CHECK(poll(&f,&r,100,&returned,&done)==IO_ERROR_OUT_OF_RANGE && !done && f.writes==2);
	init(&f,NULL,0,1); f.bytes[65533]=255; f.music.position_low=65533; st_big_music_callback_begin(&r);
	CHECK(poll(&f,&r,100,&returned,&done)==IO_ERROR_OUT_OF_RANGE && f.music.address==255 && f.music.value_offset==65534 && f.music.position_low==65533);
	init(&f,NULL,0,1); f.music.position_low=65535; st_big_music_callback_begin(&r);
	CHECK(poll(&f,&r,2,&returned,&done)==IO_ERROR_IN_PROGRESS && f.music.value_offset==0); /* Explicit low-WORD start offset. */
	CHECK(poll(&f,&r,100,&returned,&done)==IO_ERROR_OUT_OF_RANGE && f.read_count==1); /* No out-of-capacity memory alias. */
}
static void live_fields_and_prefixes(void)
{
	static const uint8_t bytes[]={0,0,0x20,0xcd,0xab,0xff,4,0,0xff,0x78,0x56};
	struct fixture f; struct st_big_music_callback r; uint16_t returned=999; bool done=false;
	init(&f,bytes,sizeof(bytes),15); st_big_music_callback_begin(&r);
	/* Entry, then value-pointer construction precede current address read. */
	CHECK(poll(&f,&r,2,&returned,&done)==IO_ERROR_IN_PROGRESS && f.music.value_offset==3 && f.read_count==0);
	f.music.position_low=5;
	CHECK(poll(&f,&r,100,&returned,&done)==0 && done && returned==0xabcd && f.music.address==255 && f.music.data==0xabcd && st_big_music_position(&f.music)==8);
	init(&f,bytes,sizeof(bytes),15); f.mutate=1; st_big_music_callback_begin(&r);
	CHECK(poll(&f,&r,100,&returned,&done)==0 && done && f.output[0].data==0x5678); /* Current shared pointer reload. */
	init(&f,bytes,sizeof(bytes),15); st_big_music_callback_begin(&r);
	CHECK(poll(&f,&r,4,&returned,&done)==IO_ERROR_IN_PROGRESS && f.music.address==0x20 && f.music.data==0xabcd);
	f.music.address=255; f.music.data=7;
	CHECK(poll(&f,&r,1,&returned,&done)==IO_ERROR_IN_PROGRESS && f.music.delay==7);
	f.music.address=0x21; f.music.data=0x1234;
	CHECK(poll(&f,&r,1,&returned,&done)==IO_ERROR_IN_PROGRESS);
	f.block_write=1;
	CHECK(poll(&f,&r,1,&returned,&done)==IO_ERROR_IN_PROGRESS && f.writes==1);
	f.music.address=255; f.music.data=999;
	CHECK(poll(&f,&r,100,&returned,&done)==0 && done && f.writes==2 && f.output[1].address==0x21 && f.output[1].data==0x1234 && returned==7);
	/* Low store/carry precedes the fresh high-word addition. */
	init(&f,bytes,sizeof(bytes),15); f.music.position_low=65535; f.music.position_high=3; st_big_music_callback_begin(&r);
	r.phase=ST_BIG_MUSIC_CALLBACK_LOW;
	CHECK(poll(&f,&r,1,&returned,&done)==IO_ERROR_IN_PROGRESS && f.music.position_low==2 && f.music.position_high==3);
	f.music.position_high=9; CHECK(poll(&f,&r,1,&returned,&done)==IO_ERROR_IN_PROGRESS && f.music.position_high==10);
	init(&f,bytes,sizeof(bytes),15); f.music.running=2; f.music.delay=0; st_big_music_callback_begin(&r);
	CHECK(poll(&f,&r,100,&returned,&done)==0 && done && returned==0 && f.read_count==0);
	init(&f,bytes,sizeof(bytes),15); f.music.position_low=5; f.music.delay=99; f.bytes[6]=0; st_big_music_callback_begin(&r);
	CHECK(poll(&f,&r,100,&returned,&done)==0 && done && returned==0 && f.music.running==1); /* Active zero delay is preserved. */
	init(&f,bytes,sizeof(bytes),15); f.music.position_high=1; st_big_music_callback_begin(&r);
	CHECK(poll(&f,&r,100,&returned,&done)==0 && done && returned==1 && f.writes==1 && f.reads[0]==2 && st_big_music_position(&f.music)==UINT32_C(0x10008));
	init(&f,bytes,sizeof(bytes),15); f.music.end_high=0x8000; f.music.end_low=0; st_big_music_callback_begin(&r);
	CHECK(poll(&f,&r,100,&returned,&done)==0 && done && returned==4 && f.music.running==1); /* T-3 wraps to positive7ffffffd. */
	init(&f,bytes,sizeof(bytes),15); f.music.end_high=0x8000; f.music.end_low=3; st_big_music_callback_begin(&r);
	CHECK(poll(&f,&r,100,&returned,&done)==0 && done && returned==1 && f.music.running==0); /* Signed80000000 cutoff. */
	init(&f,bytes,sizeof(bytes),15); st_big_music_callback_begin(&r);
	CHECK(poll(&f,&r,3,&returned,&done)==IO_ERROR_IN_PROGRESS && r.phase==ST_BIG_MUSIC_CALLBACK_DATA);
	f.block_read=2; CHECK(poll(&f,&r,1,&returned,&done)==IO_ERROR_IN_PROGRESS && f.reads[1]==3);
	f.music.value_offset=9;
	CHECK(poll(&f,&r,100,&returned,&done)==0 && done && f.output[0].data==0xabcd && f.reads[2]==3); /* Selected WORD read's latched argument. */
}
static void bounded_waits_and_errors(void)
{
	struct fixture f; init(&f,NULL,0,1); struct st_big_music_callback r; st_big_music_callback_begin(&r);
	uint16_t returned=123; bool done;
	CHECK(poll(&f,&r,0,&returned,&done)==IO_ERROR_IN_PROGRESS && !done && returned==123 && f.read_count==0);
	/* No ff: a cleared running flag does not exit the callback. Work limits
	 * suspend it rather than append a sentinel, stop it or reject a cycle. */
	for (unsigned i=0;i<10;++i) CHECK(poll(&f,&r,10,&returned,&done)==IO_ERROR_IN_PROGRESS && !done);
	CHECK(f.music.running==0 && f.writes>1 && returned==123);
	init(&f,NULL,0,1); f.block_read=1; st_big_music_callback_begin(&r);
	CHECK(poll(&f,&r,100,&returned,&done)==IO_ERROR_IN_PROGRESS && f.music.value_offset==3 && f.read_count==1);
	f.music.value_offset=10; f.fail_read=2;
	f.music.position_low=5;
	CHECK(poll(&f,&r,100,&returned,&done)==IO_ERROR_IO && returned==123 && f.music.value_offset==10);
	CHECK(poll(&f,&r,100,&returned,&done)==IO_ERROR_IO && f.read_count==2 && f.reads[1]==2);
	static const uint8_t write_failure[]={0,0,0x20,0xcd,0xab};
	init(&f,write_failure,sizeof(write_failure),15); f.fail_write=1; st_big_music_callback_begin(&r);
	CHECK(poll(&f,&r,100,&returned,&done)==IO_ERROR_IO && f.music.position_low==2 && f.music.address==0x20 && f.music.data==0xabcd && f.music.running==1 && returned==123);
	CHECK(st_big_music_callback_poll(NULL,&f.music,NULL,NULL,&returned,&done)==IO_ERROR_INVALID_ARGUMENT);
}
static void shared_x7_progress(void)
{
	static const uint8_t bytes[]={0,0,0x20,0x23,1,0xff,4,0,0xff,0,0,0xff,0,0,0xff};
	struct fixture f; init(&f,bytes,sizeof(bytes),15);
	struct st_big_control control; st_big_control_init(&control); control.music=f.music; control.music.sync=1;
	struct st_big_sync_reader gate; st_big_sync_begin(&gate,&control);
	CHECK(st_big_sync_poll(&gate,&control)==IO_ERROR_IN_PROGRESS);
	struct st_big_music_callback reader; st_big_music_callback_begin(&reader);
	const struct st_big_music_sink sink={&f,read_value,write_value}; size_t budget=100; uint16_t returned; bool done;
	CHECK(st_big_music_callback_poll(&reader,&control.music,&sink,&budget,&returned,&done)==0 && done && returned==4);
	CHECK(control.music.position_low==8 && st_big_sync_poll(&gate,&control)==0);
	st_big_sync_begin(&gate,&control); CHECK(st_big_sync_poll(&gate,&control)==IO_ERROR_IN_PROGRESS);
	control.music.running=0; st_big_music_callback_begin(&reader); budget=100;
	CHECK(st_big_music_callback_poll(&reader,&control.music,&sink,&budget,&returned,&done)==0 && done && returned==1);
	CHECK(st_big_sync_poll(&gate,&control)==IO_ERROR_IN_PROGRESS); /* Stopping alone does not release an entered gate. */
}
int main(void) { arithmetic(); original_callbacks(); live_fields_and_prefixes(); bounded_waits_and_errors(); shared_x7_progress(); return 0; }
