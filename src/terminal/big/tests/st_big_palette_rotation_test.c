/* Independent setter/snapshot/wrap traces and raw ownership, BIG26/55. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_palette.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct event { uint16_t index; uint8_t value; };
struct fixture {
	struct st_big_state s; struct st_big_palette_rotation r;
	struct event events[128]; size_t calls,fail;
	int error,mutation;
};
static int palette(void *context, size_t first, const uint8_t *data, size_t count, bool bulk)
{
	struct fixture *f=context; CHECK(!bulk && count==1 && first<16 && f->calls<128);
	CHECK(f->s.bgi.palette[first]==data[0]);
	if (first==0) CHECK(f->s.bgi.background==0);
	f->events[f->calls++]=(struct event){(uint16_t)first,data[0]};
	if (f->mutation==1 && f->calls==3) f->r.last=2; /* down wrap enables up */
	if (f->mutation==2 && first==0) f->s.saved_palette[1]=42;
	return f->calls==f->fail?f->error:0;
}
static void init(struct fixture *f)
{
	memset(f,0,sizeof(*f)); st_big_state_init(&f->s); f->fail=SIZE_MAX; f->error=IO_ERROR_IO;
	f->s.bgi.error=-11; f->s.bgi.background=7;
	for (unsigned i=0;i<16;++i) { f->s.bgi.palette[i]=(uint8_t)(i+1); f->s.saved_palette[i]=(uint16_t)(31-i); }
	st_big_state_palette_snapshot(&f->s);
	f->r.index=77; f->r.saved=88;
}
static int turn(struct fixture *f) { const struct st_big_state_sink sink={.context=f,.palette=palette}; return st_big_palette_rotation_turn(&f->r,&f->s,&sink); }
static int restore(struct fixture *f) { const struct st_big_state_sink sink={.context=f,.palette=palette}; return st_big_palette_rotation_restore(&f->r,&f->s,&sink); }
static void capture_clamp_and_retention(void)
{
	struct fixture f; init(&f); f.s.bgi.palette[0]=0x7f; f.s.bgi.palette[1]=0x80; f.s.bgi.palette[2]=0xff;
	f.s.local_palette[0]=4; f.s.palette_padding=73;
	CHECK(st_big_palette_rotation_begin(&f.r,&f.s,1,1,1,1)==0);
	CHECK(f.r.first==1 && f.r.last==1 && f.r.count==1 && f.r.delay==1 && f.r.index==77 && f.r.saved==88);
	CHECK(f.s.local_palette[0]==16 && f.s.saved_palette[0]==0x7f && f.s.saved_palette[1]==0xff80 && f.s.saved_palette[2]==0xffff && f.s.palette_padding==73);
	for (unsigned i=3;i<16;++i) CHECK(f.s.saved_palette[i]==i+1);
	struct st_big_state before=f.s;
	for (unsigned word=0;word<65536;++word) {
		CHECK(st_big_palette_rotation_begin(&f.r,&f.s,(uint16_t)word,2,3,4)==0);
		CHECK(f.r.first==(word<16?word:15) && f.r.last==2 && f.r.count==3 && f.r.delay==4);
		CHECK(st_big_palette_rotation_begin(&f.r,&f.s,2,(uint16_t)word,3,4)==0);
		CHECK(f.r.first==2 && f.r.last==(word<16?word:15));
		CHECK(memcmp(&before,&f.s,sizeof(before))==0);
	}
	/* Nonmagic entry never fetches fresh current colours. */
	f.s.bgi.palette[3]=99;
	CHECK(st_big_palette_rotation_begin(&f.r,&f.s,1,1,1,0)==0 && f.s.local_palette[4]==4 && f.s.saved_palette[3]==4);
	f.s.bgi.state=0; CHECK(st_big_palette_rotation_begin(&f.r,&f.s,1,1,1,1)==0 && f.s.local_palette[4]==99 && f.s.saved_palette[3]==99);
}
static void literal_turns(void)
{
	struct fixture f; init(&f); f.s.bgi.palette[2]=7; f.s.bgi.palette[3]=10; f.s.bgi.palette[4]=20; f.s.bgi.palette[5]=9;
	st_big_state_palette_snapshot(&f.s);
	CHECK(st_big_palette_rotation_begin(&f.r,&f.s,3,4,1,0)==0 && turn(&f)==0);
	CHECK(f.calls==3 && f.events[0].index==4 && f.events[0].value==10 && f.events[1].index==3 && f.events[1].value==7 && f.events[2].index==3 && f.events[2].value==20);
	CHECK(f.s.bgi.palette[3]==20 && f.s.bgi.palette[4]==10 && f.s.local_palette[4]==7);
	CHECK(turn(&f)==0 && f.calls==6 && f.events[3].index==4 && f.events[3].value==7 && f.events[4].index==3 && f.events[4].value==7 && f.events[5].index==3 && f.events[5].value==10);
	CHECK(f.s.bgi.palette[3]==10 && f.s.bgi.palette[4]==7 && f.s.local_palette[4]==7);
	init(&f); f.s.bgi.palette[3]=10; f.s.bgi.palette[4]=20; f.s.bgi.palette[5]=9; st_big_state_palette_snapshot(&f.s);
	CHECK(st_big_palette_rotation_begin(&f.r,&f.s,4,3,1,0)==0 && turn(&f)==0);
	CHECK(f.calls==3 && f.events[0].index==3 && f.events[0].value==20 && f.events[1].index==4 && f.events[1].value==9 && f.events[2].index==4 && f.events[2].value==10);
	CHECK(turn(&f)==0 && f.s.bgi.palette[3]==9 && f.s.bgi.palette[4]==20 && f.s.local_palette[5]==9);
}
static void all_intervals(void)
{
	for (unsigned first=0;first<16;++first) for (unsigned last=0;last<16;++last) {
		struct fixture f; init(&f); f.s.palette_padding=41;
		CHECK(st_big_palette_rotation_begin(&f.r,&f.s,(uint16_t)first,(uint16_t)last,2,3)==0);
		struct st_big_state expected=f.s;
		/* Independently authored visit sequence: interval endpoint towards
		 * first, then first again for wrap. Getter copies current values. */
		struct event events[18]; size_t count=0;
		if (first!=last) {
			uint8_t saved=expected.local_palette[last+1];
			int direction=first<last?-1:1;
			for (int index=(int)last;;index+=direction) {
				int neighbour=index+direction;
				uint8_t source=neighbour<0?16:neighbour==16?41:expected.local_palette[neighbour+1];
				events[count++]=(struct event){(uint16_t)index,source}; expected.bgi.palette[index]=source;
				if (index==0) expected.bgi.background=0;
				for (unsigned i=0;i<16;++i) expected.local_palette[i+1]=expected.bgi.palette[i];
				expected.local_palette[0]=expected.bgi.palette_size;
				if (index==(int)first) break;
			}
			events[count++]=(struct event){(uint16_t)first,saved}; expected.bgi.palette[first]=saved;
			if (first==0) expected.bgi.background=0;
		}
		CHECK(turn(&f)==0 && f.calls==count);
		for (size_t i=0;i<count;++i) CHECK(f.events[i].index==events[i].index && f.events[i].value==events[i].value);
		CHECK(memcmp(&f.s,&expected,sizeof(expected))==0);
	}
}
static void negative_candidates_and_restore(void)
{
	struct fixture f; init(&f); f.s.local_palette[4]=128; f.s.local_palette[3]=255; f.s.bgi.palette[3]=12; f.s.bgi.palette[4]=13;
	CHECK(st_big_palette_rotation_begin(&f.r,&f.s,3,4,1,0)==0 && turn(&f)==0);
	/* First negative request is skipped but still refreshes the local.
	 * The next candidate is actual pen2=3, rather than staleff. */
	CHECK(f.calls==2 && f.events[0].index==3 && f.events[0].value==3 && f.events[1].index==3 && f.events[1].value==5);
	CHECK(f.s.bgi.palette[4]==13 && f.s.local_palette[5]==13 && f.s.local_palette[4]==3);
	init(&f); f.s.saved_palette[2]=0xff80; f.s.saved_palette[3]=0xffff; f.s.saved_palette[4]=0x0106;
	uint8_t local[17]; memcpy(local,f.s.local_palette,17); CHECK(restore(&f)==0 && f.calls==14 && f.r.index==16);
	CHECK(f.s.bgi.palette[2]==3 && f.s.bgi.palette[3]==4 && f.s.bgi.palette[4]==6 && memcmp(local,f.s.local_palette,17)==0);
	init(&f); f.mutation=2; CHECK(restore(&f)==0 && f.s.bgi.palette[1]==42 && f.events[1].value==42);
}
static void failures_and_live_bounds(void)
{
	for (unsigned fail=1;fail<=3;++fail) {
		struct fixture f; init(&f); CHECK(st_big_palette_rotation_begin(&f.r,&f.s,3,4,1,0)==0); f.fail=fail;
		CHECK(turn(&f)==IO_ERROR_IO && f.calls==fail);
		/* Failed setter has already published cache but its getter is not
		 * reached. A final wrap failure leaves the earlier stale local. */
		if (fail==1) CHECK(f.s.bgi.palette[4]==4 && f.s.local_palette[5]==5);
		if (fail==2) CHECK(f.s.bgi.palette[3]==3 && f.s.local_palette[4]==4);
		if (fail==3) CHECK(f.s.bgi.palette[3]==5 && f.s.local_palette[4]==3);
	}
	for (unsigned fail=1;fail<=16;++fail) {
		struct fixture f; init(&f); f.fail=fail; CHECK(restore(&f)==IO_ERROR_IO && f.calls==fail && f.r.index==fail-1);
		for (unsigned i=0;i<16;++i) CHECK(f.s.bgi.palette[i]==(i<fail?31-i:i+1));
	}
	struct fixture f; init(&f); f.mutation=1;
	CHECK(st_big_palette_rotation_begin(&f.r,&f.s,3,4,1,0)==0 && turn(&f)==0 && f.calls==6);
	CHECK(f.events[3].index==2 && f.events[4].index==3 && f.events[5].index==3);
	init(&f); CHECK(st_big_palette_rotation_begin(&f.r,&f.s,3,4,1,0)==0); f.r.last=18;
	CHECK(turn(&f)==IO_ERROR_OUT_OF_RANGE && f.calls==0);
	init(&f); f.s.bgi.state=0; CHECK(st_big_palette_rotation_begin(&f.r,&f.s,3,4,1,0)==0 && turn(&f)==0 && f.calls==0);
	CHECK(f.s.local_palette[4]==4 && f.s.local_palette[5]==5);
	const struct st_big_state_sink sink={.context=&f,.palette=palette};
	CHECK(st_big_palette_rotation_begin(NULL,&f.s,1,2,3,4)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_palette_rotation_turn(&f.r,NULL,&sink)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_palette_rotation_restore(&f.r,&f.s,NULL)==IO_ERROR_INVALID_ARGUMENT);
}
int main(void) { capture_clamp_and_retention(); literal_turns(); all_intervals(); negative_candidates_and_restore(); failures_and_live_bounds(); return 0; }
