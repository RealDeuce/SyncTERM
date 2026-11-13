/* Independent public setpalette/state prefixes, original BIG26/55. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_state.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture { struct st_big_state state; size_t calls,index; uint8_t value; int status; };
static int palette(void *context, size_t first, const uint8_t *codes, size_t count, bool bulk)
{
	struct fixture *f=context; CHECK(!bulk && count==1 && first<16);
	CHECK(f->state.bgi.palette[first]==codes[0] && f->state.bgi.error==-11);
	if (first==0) CHECK(f->state.bgi.background==0);
	++f->calls; f->index=first; f->value=codes[0]; return f->status;
}
static int install(void *context, const struct st_big_mode *mode) { (void)context; (void)mode; CHECK(false); return 0; }
static int page(void *context, bool visual, uint16_t index) { (void)context; (void)visual; (void)index; CHECK(false); return 0; }
static void prepare(struct fixture *f)
{
	memset(f,0,sizeof(*f)); st_big_state_init(&f->state);
	f->state.bgi.error=-11; f->state.bgi.background=7;
	f->state.bgi.x=17; f->state.bgi.y=29; f->state.bgi.ddo_x=31; f->state.bgi.device_y=43;
	f->state.memory_page=3; f->state.active_page=2; f->state.visual_page=1;
	memset(f->state.local_palette,0x80,sizeof(f->state.local_palette));
	for (size_t i=0;i<16;++i) f->state.saved_palette[i]=(uint16_t)(0xff00u+i);
}
static void expect(struct fixture *f, const struct st_big_state *before, uint16_t index, uint16_t value)
{
	f->state=*before; f->calls=0; f->status=0;
	struct st_big_state expected=*before;
	/* Independent inclusive count boundary represented as an exclusive
	 * integer65536 when the original BYTE size is0. */
	unsigned limit=before->bgi.palette_size==0?65536u:before->bgi.palette_size;
	bool eligible=before->bgi.state!=0 && index<limit && value<32768;
	int status=eligible && index>=16?IO_ERROR_OUT_OF_RANGE:0;
	if (eligible && index<16) {
		expected.bgi.palette[index]=(uint8_t)(value%256);
		if (index==0) expected.bgi.background=0;
	}
	const struct st_big_state_sink sink={.context=f,.palette=palette};
	CHECK(st_big_state_palette(&f->state,index,value,&sink)==status);
	CHECK(f->calls==(eligible && index<16?1u:0u));
	if (f->calls!=0) CHECK(f->index==index && f->value==value%256);
	CHECK(memcmp(&expected,&f->state,sizeof(expected))==0);
}
static void words_and_counts(void)
{
	struct fixture f; prepare(&f); struct st_big_state before=f.state;
	CHECK(before.bgi.palette_size==16);
	for (unsigned index=0;index<16;++index) for (unsigned value=0;value<65536;++value)
		expect(&f,&before,(uint16_t)index,(uint16_t)value);
	static const uint8_t sizes[]={0,1,4,16,17,255};
	for (size_t i=0;i<sizeof(sizes);++i) {
		before.bgi.palette_size=sizes[i];
		for (unsigned index=0;index<65536;++index) expect(&f,&before,(uint16_t)index,0x0106);
	}
	static const uint16_t indices[]={0,1,3,4,15,16,17,254,255,65535};
	for (unsigned size=0;size<256;++size) for (size_t i=0;i<sizeof(indices)/sizeof(indices[0]);++i) {
		before.bgi.palette_size=(uint8_t)size;
		expect(&f,&before,indices[i],128); expect(&f,&before,indices[i],65535);
	}
	static const uint16_t states[]={0,1,2,3,65535};
	for (size_t i=0;i<sizeof(states)/sizeof(states[0]);++i) {
		before.bgi.state=states[i]; before.bgi.palette_size=16;
		for (unsigned index=0;index<17;++index) for (unsigned value=0;value<256;++value)
			expect(&f,&before,(uint16_t)index,(uint16_t)value);
	}
}
static void failures_and_lifetimes(void)
{
	struct fixture f; prepare(&f);
	const struct st_big_state_sink sink={&f,install,palette,page};
	static const int errors[]={IO_ERROR_IO,IO_ERROR_OUT_OF_MEMORY,IO_ERROR_IN_PROGRESS};
	for (size_t i=0;i<sizeof(errors)/sizeof(errors[0]);++i) {
		prepare(&f); struct st_big_state expected=f.state;
		expected.bgi.palette[0]=128; expected.bgi.background=0; f.status=errors[i];
		CHECK(st_big_state_palette(&f.state,0,128,&sink)==(errors[i]==IO_ERROR_IN_PROGRESS?IO_ERROR_PROTOCOL:errors[i]));
		CHECK(f.calls==1 && f.index==0 && f.value==128 && memcmp(&expected,&f.state,sizeof(expected))==0);
	}
	prepare(&f); CHECK(st_big_state_palette(&f.state,3,262,&sink)==0 && f.state.bgi.palette[3]==6 && f.value==6);
	CHECK(st_big_state_palette(&f.state,5,32767,&sink)==0 && f.state.bgi.palette[5]==255 && f.value==255);
	CHECK(f.state.local_palette[4]==128 && f.state.saved_palette[3]==0xff03);
	st_big_state_enter(&f.state);
	CHECK(f.state.local_palette[0]==16 && f.state.local_palette[4]==6 && f.state.local_palette[6]==255 && f.state.saved_palette[3]==0xff03);
	/* I1's internal setters share public validation, without S feature
	 * gates. An abnormal smaller count limits its accepted pen range. */
	prepare(&f); f.state.bgi.palette_size=4; uint8_t resolution=3;
	CHECK(st_big_state_initialize(&f.state,1,&resolution,&sink)==0 && f.calls==4 && f.index==3);
	CHECK(f.state.bgi.palette_size==4 && f.state.application.hollow==0 && resolution==3);
	st_big_state_enter(&f.state); CHECK(f.state.local_palette[0]==4);
	CHECK(st_big_state_palette(NULL,0,0,&sink)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_state_palette(&f.state,0,0,NULL)==IO_ERROR_INVALID_ARGUMENT);
	const struct st_big_state_sink empty={0};
	CHECK(st_big_state_palette(&f.state,0,0,&empty)==IO_ERROR_INVALID_ARGUMENT);
}
int main(void) { words_and_counts(); failures_and_lifetimes(); return 0; }
