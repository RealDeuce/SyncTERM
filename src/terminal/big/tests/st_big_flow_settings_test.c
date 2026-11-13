/* Independent X5 state/acquisition traces from complete BIG33/52 caller. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_control.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture { struct st_big_control c; struct st_big_loop loop; struct st_big_flow_settings r; };
static void init(struct fixture *f)
{
	memset(f,0,sizeof(*f)); st_big_control_init(&f->c); st_big_loop_init(&f->loop);
	f->c.flow.mode=13; f->c.flow.byte[0]=0xaa; f->c.flow.byte[1]=0xbb;
	f->c.flow.count[0]=0x4321; f->c.flow.count[1]=0x8765;
	f->c.input.pending=1; f->c.pointer.step_x=6; f->c.frame.command='X';
	st_big_flow_settings_begin(&f->r);
}
static bool next(struct fixture *f) { bool read=false; CHECK(st_big_flow_settings_next(&f->r,&f->c,&read)==0); return read; }
static void value(struct fixture *f, uint16_t word) { CHECK(st_big_flow_settings_value(&f->r,&f->c,&f->loop,word)==0); }
static void modes(void)
{
	const uint16_t loops[]={0,1,2,0xffff};
	for (size_t j=0;j<4;++j) for (unsigned word=0;word<65536;++word) {
		struct fixture f; init(&f); struct st_big_control expected=f.c;
		f.loop.active=loops[j]; CHECK(next(&f)); value(&f,(uint16_t)word);
		expected.flow.mode=loops[j]?1:(uint16_t)word;
		CHECK(memcmp(&expected,&f.c,sizeof(expected))==0);
		bool custom=loops[j]==0 && (word==2 || word==3);
		CHECK(next(&f)==custom);
		if (custom) {
			unsigned which=word==2?0:1;
			value(&f,0x01ff); expected.flow.byte[which]=255;
			CHECK(memcmp(&expected,&f.c,sizeof(expected))==0 && next(&f));
			value(&f,0x8000); expected.flow.count[which]=0x8000;
			CHECK(!next(&f));
		} else if (loops[j]==0 && word>=4 && word<32768) {
			expected.flow.byte[0]=17; expected.flow.byte[1]=19;
			expected.flow.count[0]=expected.flow.count[1]=1;
		}
		CHECK(memcmp(&expected,&f.c,sizeof(expected))==0 && !next(&f));
	}
}
static void chars_counts_and_live_tests(void)
{
	for (unsigned which=0;which<2;++which) for (unsigned word=0;word<65536;++word) {
		struct fixture f; init(&f); CHECK(next(&f)); value(&f,(uint16_t)(which+2)); CHECK(next(&f));
		value(&f,(uint16_t)word); CHECK(f.c.flow.byte[which]==(uint8_t)word && f.c.flow.count[which]==(which?0x8765:0x4321));
		/* Changing mode after selecting a byte does not undo its already
		 * reached count read; count is a full WORD without a signed clamp. */
		f.c.flow.mode=0xffff; CHECK(next(&f)); value(&f,(uint16_t)word);
		CHECK(f.c.flow.count[which]==word && !next(&f) && f.c.flow.mode==0xffff);
	}
	struct fixture f; init(&f); CHECK(next(&f)); value(&f,2); CHECK(next(&f));
	f.c.flow.mode=3; value(&f,260); CHECK(next(&f)); value(&f,2);
	CHECK(next(&f) && f.c.flow.byte[0]==4 && f.c.flow.count[0]==2);
	value(&f,511); CHECK(next(&f) && f.c.flow.byte[1]==255); value(&f,0xffff);
	f.c.flow.mode=4; CHECK(!next(&f) && f.c.flow.mode==4 && f.c.flow.byte[0]==17 && f.c.flow.byte[1]==19 && f.c.flow.count[0]==1 && f.c.flow.count[1]==1);
	/* The loop override observes the post-read owner, not entry state. */
	init(&f); f.loop.active=1; CHECK(next(&f)); f.loop.active=0; value(&f,2); CHECK(next(&f) && f.c.flow.mode==2);
	init(&f); CHECK(next(&f)); f.loop.active=0xffff; value(&f,3); CHECK(!next(&f) && f.c.flow.mode==1);
	struct st_big_control before=f.c;
	CHECK(st_big_flow_settings_value(&f.r,&f.c,&f.loop,5)==IO_ERROR_INVALID_ARGUMENT && memcmp(&before,&f.c,sizeof(before))==0);
	CHECK(st_big_flow_settings_next(NULL,&f.c,&(bool){false})==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_flow_settings_value(&f.r,NULL,&f.loop,0)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_flow_settings_next(&f.r,&f.c,NULL)==IO_ERROR_INVALID_ARGUMENT);
}
int main(void) { modes(); chars_counts_and_live_tests(); return 0; }
