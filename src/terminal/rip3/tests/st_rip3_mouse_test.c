/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_mouse.h"
#include "st_rip3_host_internal.h"
#include "io_error.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do {if (!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}} while (0)
struct fixture {struct st_rip3_mouse *mouse;unsigned flips,calls;uint8_t result;int identity;};
static void flip(void *context,const struct st_rip3_rect *area)
{
	struct fixture *f=context;(void)area;++f->flips;
}
static void activate(void *context,const uint8_t *program,size_t length,bool reset,int identity,int32_t x,int32_t y)
{
	struct fixture *f=context;++f->calls;
	(void)x;(void)y;(void)reset;
	st_rip3_mouse_kill(f->mouse);
	/* Deliberately read after freeing all source records. */
	if (length==1) f->result=program[0];
	f->identity=identity;
}
static int identities(void)
{
	static const struct {const char *text;int identity;const char *rest;} cases[]={
	    {"ID=0:GO",0,"GO"},{"id=00007:GO",7,"GO"},{"Id=32767:GO",32767,"GO"},
	    {"iD=32768:GO",-1,NULL},{"ID=000001:GO",-1,NULL},{"ID=+7:GO",-1,NULL},
	    {"ID=-7:GO",-1,NULL},{"ID= 7:GO",-1,NULL},{"ID=7 :GO",-1,NULL},
	    {" ID=7:GO",-1,NULL},{"ID=0x7:GO",-1,NULL},{"ID=:GO",-1,NULL},
	    {"ID=7GO",-1,NULL},{"ID=7:ID=8:GO",7,"ID=8:GO"},{"ID=7:",7,""},
	    {"ID=00000:GO",0,"GO"},{"ID=99999:GO",-1,NULL},{"GO",-1,NULL},
	    {"ID=Z:GO",-1,NULL},{"ID=7\x80:GO",-1,NULL}};
	for (size_t i=0;i<sizeof(cases)/sizeof(*cases);i++) {
		const uint8_t *input=(const uint8_t *)cases[i].text,*original=input;
		size_t size=strlen(cases[i].text);
		CHECK(st_rip3_mouse_identity(&input,&size)==cases[i].identity);
		const char *expected=cases[i].rest!=NULL ? cases[i].rest : cases[i].text;
		CHECK(size==strlen(expected) && memcmp(input,expected,size)==0);
		if (cases[i].identity<0) CHECK(input==original);
	}
	struct st_rip3_builtin_reference parsed;
	CHECK(st_rip3_builtin_reference_parse(&parsed,(const uint8_t *)"$FIELDID$",9)==0);
	struct st_rip3_field_context context={false,123};
	struct st_rip3_host_variables host={.field_context=&context};char result[16]="KEEP";
	CHECK(st_rip3_host_variables_execute(&host,&parsed,result,sizeof(result))==IO_ERROR_NOT_SUPPORTED && strcmp(result,"KEEP")==0);
	context=(struct st_rip3_field_context){true,7};
	CHECK(st_rip3_host_variables_execute(&host,&parsed,result,sizeof(result))==0 && strcmp(result,"7")==0);
	struct st_rip3_field_context outer=context;
	context.identity=8;
	CHECK(st_rip3_host_variables_execute(&host,&parsed,result,sizeof(result))==0 && strcmp(result,"8")==0);
	context=outer;
	CHECK(st_rip3_host_variables_execute(&host,&parsed,result,sizeof(result))==0 && strcmp(result,"7")==0);
	context.identity=-1;
	CHECK(st_rip3_host_variables_execute(&host,&parsed,result,sizeof(result))==0 && strcmp(result,"-1")==0);
	context.identity=32767;
	CHECK(st_rip3_host_variables_execute(&host,&parsed,result,2)==IO_ERROR_OUT_OF_RANGE && strcmp(result,"-1")==0);
	context.active=false;
	CHECK(st_rip3_host_variables_execute(&host,&parsed,result,sizeof(result))==IO_ERROR_NOT_SUPPORTED);
	return 0;
}

static int enclosed(void)
{
	struct fixture f={0};struct st_rip3_mouse_sink sink={.context=&f,.invert=flip,.activate=activate};
	CHECK(st_rip3_mouse_create(&f.mouse,&sink)==0);
	const struct st_rip3_rect selector={10,10,20,20};
	const struct st_rip3_rect regions[]={{12,12,18,18},{10,10,20,20},
	    {15,15,25,25},{20,10,30,20},{30,30,40,40}};
	/* Independent counts for contained/equal/partial/touching/disjoint.
	 * The pending B120 normative reconciliation is kept separate from this
	 * explicit half-open geometric predicate table. */
	const unsigned removed[]={0,2,3,3,2,4,5,5};
	for (unsigned flags=0;flags<32;flags++) {
		for (unsigned kind=0;kind<2;kind++) for (unsigned i=0;i<5;i++)
			CHECK(st_rip3_mouse_define(f.mouse,(enum st_rip3_mouse_kind)kind,&regions[i],false,false,-1,NULL,0)==0);
		CHECK(st_rip3_mouse_remove(f.mouse,&selector,flags)==0);
		unsigned types=((flags&8)==0)+((flags&16)==0);
		CHECK(st_rip3_mouse_count(f.mouse)==10-types*removed[flags&7]);
		st_rip3_mouse_kill(f.mouse);
	}
	const struct st_rip3_rect other={30,30,40,40};
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,&other,false,false,8,(const uint8_t *)"b",1)==0);
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_BUTTON,&selector,true,false,7,(const uint8_t *)"a",1)==0);
	CHECK(st_rip3_mouse_event(f.mouse,15,15,true) && f.flips==1);
	CHECK(st_rip3_mouse_remove(f.mouse,&other,1)==0 && st_rip3_mouse_count(f.mouse)==1 && f.flips==1);
	CHECK(st_rip3_mouse_event(f.mouse,15,15,false));
	CHECK(f.flips==2 && f.calls==1 && f.result=='a' && f.identity==7);
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_BUTTON,&selector,true,false,7,(const uint8_t *)"a",1)==0);
	CHECK(st_rip3_mouse_event(f.mouse,15,15,true) && f.flips==3);
	CHECK(st_rip3_mouse_remove(f.mouse,&selector,1)==0 && !st_rip3_mouse_active(f.mouse) && f.flips==4);
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_BUTTON,&selector,true,false,7,(const uint8_t *)"c",1)==0);
	CHECK(!st_rip3_mouse_event(f.mouse,15,15,true) && !st_rip3_mouse_event(f.mouse,15,15,false));
	CHECK(f.calls==1);
	CHECK(st_rip3_mouse_event(f.mouse,15,15,true) && st_rip3_mouse_event(f.mouse,15,15,false));
	CHECK(f.flips==6 && f.calls==2 && f.result=='c' && f.identity==7);
	st_rip3_mouse_destroy(f.mouse);return 0;
}

int main(void)
{
	CHECK(identities()==0 && enclosed()==0);
	struct fixture f={0};struct st_rip3_mouse_sink sink={.context=&f,.invert=flip,.activate=activate};
	CHECK(st_rip3_mouse_create(&f.mouse,&sink)==0);
	struct st_rip3_rect area={10,20,30,40};
	CHECK(!st_rip3_mouse_active(f.mouse));
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,&area,false,false,-1,NULL,0)==0);
	CHECK(!st_rip3_mouse_event(f.mouse,30,20,true) && !st_rip3_mouse_event(f.mouse,30,20,false));
	CHECK(!st_rip3_mouse_event(f.mouse,10,40,true) && !st_rip3_mouse_event(f.mouse,10,40,false));
	CHECK(!st_rip3_mouse_event(f.mouse,9,20,true) && !st_rip3_mouse_event(f.mouse,9,20,false));
	CHECK(f.calls==0);st_rip3_mouse_kill(f.mouse);
	for (unsigned i=0;i<128;i++) {uint8_t n=(uint8_t)i;CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,&area,true,false,-1,&n,1)==0);}
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,&area,false,false,-1,NULL,0)!=0);
	CHECK(!st_rip3_mouse_event(f.mouse,9,20,true));
	CHECK(!st_rip3_mouse_event(f.mouse,10,20,false));CHECK(f.calls==0);
	CHECK(st_rip3_mouse_event(f.mouse,10,20,true));CHECK(f.flips==1);
	CHECK(st_rip3_mouse_event(f.mouse,31,40,true));CHECK(f.flips==2);
	CHECK(st_rip3_mouse_event(f.mouse,29,39,true));CHECK(f.flips==3);
	CHECK(st_rip3_mouse_event(f.mouse,29,39,false));
	CHECK(f.flips==4 && f.calls==1 && f.result==127 && !st_rip3_mouse_active(f.mouse));
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,&area,true,false,-1,(const uint8_t *)"x",1)==0);
	CHECK(st_rip3_mouse_event(f.mouse,20,30,true));
	CHECK(st_rip3_mouse_event(f.mouse,-1,-1,false));CHECK(f.calls==1 && f.flips==6);
	CHECK(st_rip3_mouse_event(f.mouse,20,30,true));
	st_rip3_mouse_kill(f.mouse);CHECK(f.flips==8);
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,&area,false,false,-1,(const uint8_t *)"y",1)==0);
	CHECK(!st_rip3_mouse_event(f.mouse,20,30,true));
	CHECK(!st_rip3_mouse_event(f.mouse,20,30,false));CHECK(f.calls==1);
	CHECK(st_rip3_mouse_event(f.mouse,20,30,true));CHECK(st_rip3_mouse_event(f.mouse,20,30,false));
	CHECK(f.calls==2 && f.result=='y' && f.identity==-1);
	/* Same public ID and rectangle do not let a newly appended instance
	 * steal an existing press. Identity remains valid after source deletion. */
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,&area,false,false,7,(const uint8_t *)"a",1)==0);
	CHECK(st_rip3_mouse_event(f.mouse,20,30,true));
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,&area,false,false,7,(const uint8_t *)"b",1)==0);
	CHECK(st_rip3_mouse_event(f.mouse,20,30,false));
	CHECK(f.calls==3 && f.result=='a' && f.identity==7);
	st_rip3_mouse_destroy(f.mouse);return 0;
}
