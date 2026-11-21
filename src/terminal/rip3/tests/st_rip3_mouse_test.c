/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_mouse.h"
#include "st_rip3_host_internal.h"
#include "io_error.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do {if (!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}} while (0)
struct fixture {struct st_rip3_mouse *mouse;unsigned flips,calls;uint8_t result;int identity;bool pointer;};
static void flip(void *context,const struct st_rip3_rect *area)
{
	struct fixture *f=context;(void)area;++f->flips;
}
static void activate(void *context,const uint8_t *program,size_t length,bool reset,int identity,bool pointer,int32_t x,int32_t y)
{
	struct fixture *f=context;++f->calls;
	(void)x;(void)y;(void)reset;
	st_rip3_mouse_kill(f->mouse);
	/* Deliberately read after freeing all source records. */
	if (length==1) f->result=program[0];
	f->identity=identity;
	f->pointer=pointer;
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

struct paint_count {unsigned calls;bool pattern;struct st_rip3_rect area;};
static int count_paint(void *context,const struct st_rip3_rect *area,bool pattern)
{
	struct paint_count *f=context;f->calls++;f->pattern=pattern;f->area=*area;return 0;
}
static int enclosed(void)
{
	struct fixture f={0};struct st_rip3_mouse_sink sink={.context=&f,.invert=flip,.activate=activate};
	CHECK(st_rip3_mouse_create(&f.mouse,&sink)==0);
	const struct st_rip3_rect selector={10,10,20,20};
	const struct st_rip3_rect regions[]={{12,12,18,18},{10,10,20,20},
	    {15,15,25,25},{5,5,25,25},{20,10,30,20},{30,30,40,40}};
	/* PR123's disjoint C/P/O classes: contained/equal, partial/covering,
	 * touching/disjoint. Each class has two independently chosen rectangles. */
	const unsigned removed[]={0,2,2,4,2,4,4,6};
	for (unsigned flags=0;flags<256;flags++) {
		for (unsigned kind=0;kind<2;kind++) for (unsigned i=0;i<6;i++)
			CHECK(st_rip3_mouse_define(f.mouse,(enum st_rip3_mouse_kind)kind,NULL,&regions[i],false,false,-1,NULL,0,NULL)==0);
		struct paint_count count={0};const struct st_rip3_mouse_paint paint={&count,count_paint};
		CHECK(st_rip3_mouse_remove(f.mouse,&selector,flags,&paint)==0);
		unsigned types=((flags&8)==0)+((flags&16)==0);
		CHECK(st_rip3_mouse_count(f.mouse)==12-types*removed[flags&7]);
		unsigned paints=(flags&7)!=0 && (flags&96)!=0 ? ((flags&128)!=0 ? 1 : types*removed[flags&7]) : 0;
		CHECK(count.calls==paints);
		if (paints!=0) CHECK(count.pattern==((flags&64)!=0));
		if (paints!=0 && (flags&128)!=0) CHECK(memcmp(&count.area,&selector,sizeof(selector))==0);
		st_rip3_mouse_kill(f.mouse);
	}
	const struct st_rip3_rect other={30,30,40,40};
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&other,false,false,8,(const uint8_t *)"b",1,NULL)==0);
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_BUTTON,NULL,&selector,true,false,7,(const uint8_t *)"a",1,NULL)==0);
	CHECK(st_rip3_mouse_event(f.mouse,15,15,true) && f.flips==1);
	CHECK(st_rip3_mouse_remove(f.mouse,&other,1,NULL)==0 && st_rip3_mouse_count(f.mouse)==1 && f.flips==1);
	CHECK(st_rip3_mouse_event(f.mouse,15,15,false));
	CHECK(f.flips==2 && f.calls==1 && f.result=='a' && f.identity==7);
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_BUTTON,NULL,&selector,true,false,7,(const uint8_t *)"a",1,NULL)==0);
	CHECK(st_rip3_mouse_event(f.mouse,15,15,true) && f.flips==3);
	CHECK(st_rip3_mouse_remove(f.mouse,&selector,1,NULL)==0 && !st_rip3_mouse_active(f.mouse) && f.flips==4);
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_BUTTON,NULL,&selector,true,false,7,(const uint8_t *)"c",1,NULL)==0);
	CHECK(!st_rip3_mouse_event(f.mouse,15,15,true) && !st_rip3_mouse_event(f.mouse,15,15,false));
	CHECK(f.calls==1);
	CHECK(st_rip3_mouse_event(f.mouse,15,15,true) && st_rip3_mouse_event(f.mouse,15,15,false));
	CHECK(f.flips==6 && f.calls==2 && f.result=='c' && f.identity==7);
	st_rip3_mouse_destroy(f.mouse);return 0;
}

struct paint_fixture {struct st_rip3_mouse *mouse;struct fixture input;unsigned calls;bool valid;};
static int failed_paint(void *context,const struct st_rip3_rect *area,bool pattern)
{
	struct paint_fixture *f=context;
	/* Both fields must be gone before even the first failed paint. Newest
	 * rectangle first; failure must not suppress the second attempt. */
	f->valid&=st_rip3_mouse_count(f->mouse)==1 && pattern
	    && area->x0==(f->calls==0 ? 14 : 12);
	return ++f->calls==1 ? IO_ERROR_OUT_OF_MEMORY : 0;
}
static int paint_failure(void)
{
	struct paint_fixture f={.valid=true};const struct st_rip3_mouse_sink sink={.context=&f.input,.invert=flip,.activate=activate};
	CHECK(st_rip3_mouse_create(&f.mouse,&sink)==0);
	f.input.mouse=f.mouse;
	const struct st_rip3_rect selector={10,10,20,20},a={12,12,13,13},b={14,14,15,15};
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&a,false,false,-1,NULL,0,NULL)==0);
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_BUTTON,NULL,&b,false,false,-1,NULL,0,NULL)==0);
	const struct st_rip3_rect survivor={40,40,50,50};
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&survivor,true,false,9,(const uint8_t *)"e",1,NULL)==0);
	CHECK(st_rip3_mouse_event(f.mouse,45,45,true) && f.input.flips==1);
	const struct st_rip3_mouse_paint paint={&f,failed_paint};
	CHECK(st_rip3_mouse_remove(f.mouse,&selector,1|64,&paint)==IO_ERROR_OUT_OF_MEMORY);
	CHECK(f.valid && f.calls==2 && st_rip3_mouse_count(f.mouse)==1 && f.input.flips==1);
	CHECK(st_rip3_mouse_event(f.mouse,45,45,false) && f.input.flips==2 && f.input.calls==1 && f.input.identity==9 && f.input.result=='e');
	st_rip3_mouse_destroy(f.mouse);return 0;
}

static int keyboard(void)
{
	static const struct {uint8_t key;unsigned omitted;uint8_t extra;bool default_enter;int identity;} cases[]={
	    {'A',0,0,false,2},{'a',0,0,false,2},{13,0,0,false,4},
	    {13,1u<<3,0,false,6},{13,(1u<<3)|(1u<<5),0,false,5},
	    {13,(1u<<3)|(1u<<5)|(1u<<4),0,false,7},{0,0,0,false,7},
	    {0,1u<<6,0,false,0},{128,0,0,false,7},{128,0,128,false,8},
	    {129,0,128,false,7},{'?',0,255,false,8},{'a',0,255,false,2},
	    {13,0,'B',true,4},{13,1u<<3,'B',true,8},{'b',0,'B',true,8},
	    {10,1u<<6,0,false,0},{255,0,0,false,7},{13,0,13,false,8}};
	static const struct st_rip3_mouse_binding bindings[]={{'A',false},{'a',false},{0,false},
	    {13,false},{0,true},{0,true},{255,false}};
	const struct st_rip3_rect area={10,10,20,20};
	for (size_t test=0;test<sizeof(cases)/sizeof(*cases);test++) {
		struct fixture f={0};struct st_rip3_mouse_sink sink={.context=&f,.invert=flip,.activate=activate};
		CHECK(st_rip3_mouse_create(&f.mouse,&sink)==0);
		for (unsigned i=0;i<7;i++) if ((cases[test].omitted&(1u<<i))==0) {
			uint8_t data=(uint8_t)(i+1);
			CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_BUTTON,&bindings[i],&area,false,false,(int)i+1,&data,1,NULL)==0);
		}
		if (cases[test].extra!=0) {
			const struct st_rip3_mouse_binding binding={cases[test].extra,cases[test].default_enter};
			CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_BUTTON,&binding,&area,false,false,8,NULL,0,NULL)==0);
		}
		bool handled=st_rip3_mouse_key(f.mouse,cases[test].key);
		CHECK(handled==(cases[test].identity!=0));
		CHECK(f.calls==(handled ? 1u : 0u) && (!handled || (f.identity==cases[test].identity && !f.pointer)));
		st_rip3_mouse_destroy(f.mouse);
	}
	struct fixture f={0};struct st_rip3_mouse_sink sink={.context=&f,.invert=flip,.activate=activate};
	CHECK(st_rip3_mouse_create(&f.mouse,&sink)==0);
	const struct st_rip3_mouse_binding binding={'B',false};
	const struct st_rip3_rect other={30,30,40,40};
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&area,true,false,1,NULL,0,NULL)==0);
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_BUTTON,&binding,&other,true,false,2,(const uint8_t *)"k",1,NULL)==0);
	CHECK(st_rip3_mouse_event(f.mouse,15,15,true) && f.flips==1);
	CHECK(!st_rip3_mouse_key(f.mouse,'z') && f.flips==1 && f.calls==0);
	st_rip3_mouse_hotkeys(f.mouse,false);
	CHECK(!st_rip3_mouse_key(f.mouse,'b') && f.flips==1 && f.calls==0);
	st_rip3_mouse_hotkeys(f.mouse,true);
	CHECK(st_rip3_mouse_key(f.mouse,'b') && f.flips==4 && f.calls==1 && !f.pointer && f.identity==2 && f.result=='k');
	CHECK(!st_rip3_mouse_event(f.mouse,15,15,false) && f.calls==1);
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_BUTTON,&binding,&other,true,false,2,NULL,0,NULL)==0);
	CHECK(st_rip3_mouse_key(f.mouse,'B') && f.flips==6 && f.calls==2);
	st_rip3_mouse_destroy(f.mouse);return 0;
}

struct visual_fixture {struct fixture input;unsigned selected,cleared,destroyed,changed,errors;};
static int select_visual(void *context,bool selected)
{
	struct visual_fixture *f=context;
	if (selected) f->selected++;else f->cleared++;
	return selected && f->selected==1 ? IO_ERROR_OUT_OF_MEMORY : 0;
}
static void destroy_visual(void *context) {++((struct visual_fixture *)context)->destroyed;}
static void changed_visual(void *context) {++((struct visual_fixture *)context)->changed;}
static void error_visual(void *context) {++((struct visual_fixture *)context)->errors;}
static void activate_visual(void *context,const uint8_t *program,size_t length,bool reset,int identity,bool pointer,int32_t x,int32_t y)
{
	struct visual_fixture *f=context;
	activate(&f->input,program,length,reset,identity,pointer,x,y);
}
static int visuals(void)
{
	struct visual_fixture f={0};const struct st_rip3_mouse_sink sink={.context=&f,
	    .activate=activate_visual,.changed=changed_visual,.render_failure=error_visual};
	CHECK(st_rip3_mouse_create(&f.input.mouse,&sink)==0);
	const struct st_rip3_rect area={10,10,20,20};const struct st_rip3_mouse_binding binding={'A',false};
	const struct st_rip3_mouse_visual visual={&f,select_visual,destroy_visual};
	CHECK(st_rip3_mouse_define(f.input.mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&area,false,false,-1,NULL,0,&visual)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(f.destroyed==0); /* Rejection never takes ownership. */
	CHECK(st_rip3_mouse_define(f.input.mouse,ST_RIP3_MOUSE_BUTTON,&binding,&area,false,false,7,(const uint8_t *)"v",1,&visual)==0);
	CHECK(st_rip3_mouse_event(f.input.mouse,15,15,true) && f.selected==1 && f.errors==1 && f.changed==1);
	CHECK(st_rip3_mouse_event(f.input.mouse,15,15,true) && f.selected==1);
	CHECK(st_rip3_mouse_event(f.input.mouse,25,25,true) && f.cleared==1);
	CHECK(st_rip3_mouse_event(f.input.mouse,15,15,true) && f.selected==2);
	st_rip3_mouse_kill(f.input.mouse);
	CHECK(f.cleared==2 && f.destroyed==1 && f.changed==4 && f.input.calls==0);
	CHECK(!st_rip3_mouse_event(f.input.mouse,15,15,false));
	CHECK(st_rip3_mouse_define(f.input.mouse,ST_RIP3_MOUSE_BUTTON,&binding,&area,false,false,8,(const uint8_t *)"k",1,&visual)==0);
	/* The keyboard action deletes its own selected visual. Cancellation must
	 * redraw once before destruction; post-callback cleanup cannot touch it. */
	CHECK(st_rip3_mouse_key(f.input.mouse,'a'));
	CHECK(f.selected==3 && f.cleared==3 && f.destroyed==2 && f.changed==6 && f.input.result=='k');
	CHECK(st_rip3_mouse_define(f.input.mouse,ST_RIP3_MOUSE_BUTTON,&binding,&area,false,false,9,NULL,0,&visual)==0);
	CHECK(st_rip3_mouse_event(f.input.mouse,15,15,true));
	st_rip3_mouse_destroy(f.input.mouse);
	CHECK(f.selected==4 && f.cleared==4 && f.destroyed==3);return 0;
}

int main(void)
{
	CHECK(identities()==0 && enclosed()==0 && paint_failure()==0 && keyboard()==0 && visuals()==0);
	struct fixture f={0};struct st_rip3_mouse_sink sink={.context=&f,.invert=flip,.activate=activate};
	CHECK(st_rip3_mouse_create(&f.mouse,&sink)==0);
	struct st_rip3_rect area={10,20,30,40};
	CHECK(!st_rip3_mouse_active(f.mouse));
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&area,false,false,-1,NULL,0,NULL)==0);
	CHECK(!st_rip3_mouse_event(f.mouse,30,20,true) && !st_rip3_mouse_event(f.mouse,30,20,false));
	CHECK(!st_rip3_mouse_event(f.mouse,10,40,true) && !st_rip3_mouse_event(f.mouse,10,40,false));
	CHECK(!st_rip3_mouse_event(f.mouse,9,20,true) && !st_rip3_mouse_event(f.mouse,9,20,false));
	CHECK(f.calls==0);st_rip3_mouse_kill(f.mouse);
	for (unsigned i=0;i<128;i++) {uint8_t n=(uint8_t)i;CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&area,true,false,-1,&n,1,NULL)==0);}
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&area,false,false,-1,NULL,0,NULL)!=0);
	CHECK(!st_rip3_mouse_event(f.mouse,9,20,true));
	CHECK(!st_rip3_mouse_event(f.mouse,10,20,false));CHECK(f.calls==0);
	CHECK(st_rip3_mouse_event(f.mouse,10,20,true));CHECK(f.flips==1);
	CHECK(st_rip3_mouse_event(f.mouse,31,40,true));CHECK(f.flips==2);
	CHECK(st_rip3_mouse_event(f.mouse,29,39,true));CHECK(f.flips==3);
	CHECK(st_rip3_mouse_event(f.mouse,29,39,false));
	CHECK(f.flips==4 && f.calls==1 && f.result==127 && !st_rip3_mouse_active(f.mouse));
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&area,true,false,-1,(const uint8_t *)"x",1,NULL)==0);
	CHECK(st_rip3_mouse_event(f.mouse,20,30,true));
	CHECK(st_rip3_mouse_event(f.mouse,-1,-1,false));CHECK(f.calls==1 && f.flips==6);
	CHECK(st_rip3_mouse_event(f.mouse,20,30,true));
	st_rip3_mouse_kill(f.mouse);CHECK(f.flips==8);
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&area,false,false,-1,(const uint8_t *)"y",1,NULL)==0);
	CHECK(!st_rip3_mouse_event(f.mouse,20,30,true));
	CHECK(!st_rip3_mouse_event(f.mouse,20,30,false));CHECK(f.calls==1);
	CHECK(st_rip3_mouse_event(f.mouse,20,30,true));CHECK(st_rip3_mouse_event(f.mouse,20,30,false));
	CHECK(f.calls==2 && f.result=='y' && f.identity==-1);
	/* Same public ID and rectangle do not let a newly appended instance
	 * steal an existing press. Identity remains valid after source deletion. */
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&area,false,false,7,(const uint8_t *)"a",1,NULL)==0);
	CHECK(st_rip3_mouse_event(f.mouse,20,30,true));
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&area,false,false,7,(const uint8_t *)"b",1,NULL)==0);
	CHECK(st_rip3_mouse_event(f.mouse,20,30,false));
	CHECK(f.calls==3 && f.result=='a' && f.identity==7);
	st_rip3_mouse_destroy(f.mouse);return 0;
}
