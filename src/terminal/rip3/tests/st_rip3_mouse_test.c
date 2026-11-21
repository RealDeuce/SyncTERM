/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_mouse.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do {if (!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}} while (0)
struct fixture {struct st_rip3_mouse *mouse;unsigned flips,calls;uint8_t result;};
static void flip(void *context,const struct st_rip3_rect *area)
{
	struct fixture *f=context;(void)area;++f->flips;
}
static void activate(void *context,const uint8_t *program,size_t length,int32_t x,int32_t y)
{
	struct fixture *f=context;++f->calls;
	(void)x;(void)y;
	st_rip3_mouse_kill(f->mouse);
	/* Deliberately read after freeing all source records. */
	if (length==1) f->result=program[0];
}
int main(void)
{
	struct fixture f={0};struct st_rip3_mouse_sink sink={.context=&f,.invert=flip,.activate=activate};
	CHECK(st_rip3_mouse_create(&f.mouse,&sink)==0);
	struct st_rip3_rect area={10,20,30,40};
	CHECK(!st_rip3_mouse_active(f.mouse));
	for (unsigned i=0;i<128;i++) {uint8_t n=(uint8_t)i;CHECK(st_rip3_mouse_define(f.mouse,&area,true,&n,1)==0);}
	CHECK(st_rip3_mouse_define(f.mouse,&area,false,NULL,0)!=0);
	CHECK(!st_rip3_mouse_event(f.mouse,9,20,true));
	CHECK(!st_rip3_mouse_event(f.mouse,10,20,false));CHECK(f.calls==0);
	CHECK(st_rip3_mouse_event(f.mouse,10,20,true));CHECK(f.flips==1);
	CHECK(st_rip3_mouse_event(f.mouse,31,40,true));CHECK(f.flips==2);
	CHECK(st_rip3_mouse_event(f.mouse,30,40,true));CHECK(f.flips==3);
	CHECK(st_rip3_mouse_event(f.mouse,30,40,false));
	CHECK(f.flips==4 && f.calls==1 && f.result==127 && !st_rip3_mouse_active(f.mouse));
	CHECK(st_rip3_mouse_define(f.mouse,&area,true,(const uint8_t *)"x",1)==0);
	CHECK(st_rip3_mouse_event(f.mouse,20,30,true));
	CHECK(st_rip3_mouse_event(f.mouse,-1,-1,false));CHECK(f.calls==1 && f.flips==6);
	CHECK(st_rip3_mouse_event(f.mouse,20,30,true));
	st_rip3_mouse_kill(f.mouse);CHECK(f.flips==8);
	CHECK(st_rip3_mouse_define(f.mouse,&area,false,(const uint8_t *)"y",1)==0);
	CHECK(!st_rip3_mouse_event(f.mouse,20,30,true));
	CHECK(!st_rip3_mouse_event(f.mouse,20,30,false));CHECK(f.calls==1);
	CHECK(st_rip3_mouse_event(f.mouse,20,30,true));CHECK(st_rip3_mouse_event(f.mouse,20,30,false));
	CHECK(f.calls==2 && f.result=='y');st_rip3_mouse_destroy(f.mouse);return 0;
}
