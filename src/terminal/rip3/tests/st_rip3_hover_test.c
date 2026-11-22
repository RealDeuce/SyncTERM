/* Independent hover ordering, membership, mutation and suspension vectors. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_mouse.h"
#include "io_error.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do {if (!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}} while (0)
extern size_t st_rip3_hover_test_attempts,st_rip3_hover_test_live,st_rip3_hover_test_fail_at;
struct fixture {
	struct st_rip3_mouse *mouse;
	int ids[32];uint8_t events[32];size_t calls;
	bool busy,suspend,remove_old,append_new,replace_query,kill,off_query,boundary;unsigned failures;
	size_t last_length;
};
static void failure(void *context) {((struct fixture *)context)->failures++;}
static bool busy(void *context) {return ((struct fixture *)context)->busy;}
static void activate(void *context,const uint8_t *program,size_t length,bool reset,int identity,bool pointer,int32_t x,int32_t y)
{
	(void)reset;(void)pointer;(void)x;(void)y;struct fixture *f=context;
	if (f->remove_old) {
		f->remove_old=false;const struct st_rip3_rect old={10,10,60,60};
		(void)st_rip3_mouse_remove(f->mouse,&old,1,NULL);
	}
	if (f->append_new) {
		f->append_new=false;const struct st_rip3_rect added={10,10,60,60};
		(void)st_rip3_mouse_define(f->mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&added,false,false,7,(const uint8_t *)"C",1,NULL);
	}
	if (f->replace_query) {f->replace_query=false;(void)st_rip3_mouse_query(f->mouse,true,(const uint8_t *)"N",1);}
	if (f->kill) {f->kill=false;st_rip3_mouse_kill(f->mouse);}
	if (f->off_query) {f->off_query=false;(void)st_rip3_mouse_query(f->mouse,true,(const uint8_t *)"$OFF$",5);}
	if (f->boundary) {f->boundary=false;st_rip3_mouse_boundary(f->mouse);}
	f->last_length=length;
	/* Read after the stored source or field records have been freed. */
	if (f->calls<32) {f->ids[f->calls]=identity;f->events[f->calls]=length==1 ? program[0] : '?';}
	f->calls++;if (f->suspend) {f->suspend=false;f->busy=true;}
}
static int initialize(struct fixture *f)
{
	const struct st_rip3_mouse_sink sink={.context=f,.activate=activate,.busy=busy,.resource_failure=failure};
	CHECK(st_rip3_mouse_create(&f->mouse,&sink)==0);
	const struct st_rip3_rect a={10,10,60,60},b={20,20,80,80};
	CHECK(st_rip3_mouse_define(f->mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&a,false,false,7,(const uint8_t *)"A",1,NULL)==0
	    && st_rip3_mouse_define(f->mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&b,false,false,8,(const uint8_t *)"B",1,NULL)==0);
	uint8_t query='E';CHECK(st_rip3_mouse_query(f->mouse,true,&query,1)==0);query='?';
	CHECK(st_rip3_mouse_query(f->mouse,false,(const uint8_t *)"X",1)==0);return 0;
}
static int lifecycle(void)
{
	struct fixture f={0};CHECK(initialize(&f)==0);
	/* Both absent still maintain inside membership; enabling is not entry. */
	CHECK(st_rip3_mouse_query(f.mouse,true,(const uint8_t *)"$OFF$",5)==0
	    && st_rip3_mouse_query(f.mouse,false,(const uint8_t *)"$OFF$",5)==0);
	st_rip3_mouse_move(f.mouse,30,30);
	CHECK(st_rip3_mouse_query(f.mouse,true,(const uint8_t *)"E",1)==0);
	st_rip3_mouse_move(f.mouse,30,30);CHECK(f.calls==0);
	st_rip3_mouse_move(f.mouse,90,90);st_rip3_mouse_move(f.mouse,30,30);
	CHECK(f.calls==2 && memcmp(f.events,"EE",2)==0);
	CHECK(st_rip3_mouse_event(f.mouse,30,30,true));
	st_rip3_mouse_boundary(f.mouse);CHECK(st_rip3_mouse_press_token(f.mouse)==0 && f.calls==2);
	/* A still-held press cannot reacquire capture across a focus boundary. */
	CHECK(!st_rip3_mouse_event(f.mouse,30,30,true));
	st_rip3_mouse_move(f.mouse,30,30);CHECK(f.calls==4);
	CHECK(!st_rip3_mouse_event(f.mouse,30,30,false));
	st_rip3_mouse_destroy(f.mouse);
	f=(struct fixture){0};CHECK(initialize(&f)==0);f.off_query=true;
	st_rip3_mouse_move(f.mouse,30,30);CHECK(f.calls==1);
	CHECK(st_rip3_mouse_query(f.mouse,true,(const uint8_t *)"E",1)==0);
	st_rip3_mouse_move(f.mouse,30,30);CHECK(f.calls==1); /* Older entry committed without handler. */
	st_rip3_mouse_destroy(f.mouse);
	f=(struct fixture){0};CHECK(initialize(&f)==0);f.replace_query=true;
	st_rip3_mouse_move(f.mouse,30,30);CHECK(f.calls==2 && memcmp(f.events,"EN",2)==0);
	st_rip3_mouse_destroy(f.mouse);
	f=(struct fixture){0};CHECK(initialize(&f)==0);f.suspend=true;
	st_rip3_mouse_move(f.mouse,30,30);CHECK(f.calls==1 && st_rip3_mouse_pending(f.mouse));
	CHECK(st_rip3_mouse_query(f.mouse,true,(const uint8_t *)"N",1)==0);
	f.busy=false;st_rip3_mouse_resume(f.mouse);CHECK(f.calls==2 && memcmp(f.events,"EN",2)==0);
	st_rip3_mouse_destroy(f.mouse);
	f=(struct fixture){0};CHECK(initialize(&f)==0);f.suspend=true;
	st_rip3_mouse_move(f.mouse,30,30);CHECK(f.calls==1);
	st_rip3_mouse_boundary(f.mouse);f.busy=false;st_rip3_mouse_resume(f.mouse);
	CHECK(f.calls==1 && !st_rip3_mouse_pending(f.mouse));
	st_rip3_mouse_move(f.mouse,30,30);CHECK(f.calls==3);
	st_rip3_mouse_destroy(f.mouse);
	f=(struct fixture){0};CHECK(initialize(&f)==0);
	uint8_t source[1024];memset(source,'A',sizeof(source));
	CHECK(st_rip3_mouse_query(f.mouse,true,source,1023)==0);
	CHECK(st_rip3_mouse_query(f.mouse,true,NULL,0)==IO_ERROR_INVALID_FORMAT);
	CHECK(st_rip3_mouse_query(f.mouse,true,source,1024)==IO_ERROR_OUT_OF_RANGE);
	st_rip3_mouse_move(f.mouse,30,30);CHECK(f.calls==2 && f.last_length==1023);
	const uint8_t off[]="$oFf$\0ignored";
	CHECK(st_rip3_mouse_query(f.mouse,true,off,sizeof(off)-1)==0);
	st_rip3_mouse_move(f.mouse,90,90);st_rip3_mouse_move(f.mouse,30,30);CHECK(f.calls==4);
	/* Normalized codepage scalars retain their original byte budget. */
	uint8_t normalized[3*1024];
	for (size_t i=0;i<sizeof(normalized);i+=3) {normalized[i]=0xe2;normalized[i+1]=0x96;normalized[i+2]=0x88;}
	CHECK(st_rip3_mouse_query_encoded(f.mouse,true,normalized,3*1023,true)==0);
	CHECK(st_rip3_mouse_query_encoded(f.mouse,true,normalized,3*1024,true)==IO_ERROR_OUT_OF_RANGE);
	st_rip3_mouse_move(f.mouse,90,90);st_rip3_mouse_move(f.mouse,30,30);
	CHECK(f.calls==8 && f.last_length==3*1023);
	st_rip3_mouse_destroy(f.mouse);
	f=(struct fixture){0};CHECK(initialize(&f)==0);
	st_rip3_mouse_move(f.mouse,30,30);
	const struct st_rip3_rect c={90,90,120,120};
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&c,false,false,9,NULL,0,NULL)==0);
	f.replace_query=true;st_rip3_mouse_move(f.mouse,100,100);
	CHECK(f.calls==5 && memcmp(f.events,"EEXXN",5)==0 && f.ids[4]==9);
	st_rip3_mouse_destroy(f.mouse);
	f=(struct fixture){0};CHECK(initialize(&f)==0);st_rip3_mouse_kill(f.mouse);
	const struct st_rip3_rect a={10,10,60,60};
	for (unsigned i=0;i<2;i++) CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&a,false,false,7,NULL,0,NULL)==0);
	st_rip3_mouse_move(f.mouse,30,30);CHECK(f.calls==2 && f.ids[0]==7 && f.ids[1]==7);
	const char *ordinary[]={" $OFF$","$OFF$ ","$OFF$x","\\$OFF$","'$OFF$'","$NAME$"};
	for (size_t i=0;i<sizeof(ordinary)/sizeof(ordinary[0]);i++) {
		f.calls=0;st_rip3_mouse_boundary(f.mouse);
		CHECK(st_rip3_mouse_query(f.mouse,true,(const uint8_t *)ordinary[i],strlen(ordinary[i]))==0 && f.calls==0);
		st_rip3_mouse_move(f.mouse,30,30);CHECK(f.calls==2 && f.last_length==strlen(ordinary[i]));
	}
	st_rip3_mouse_destroy(f.mouse);return 0;
}

int main(void)
{
	CHECK(lifecycle()==0);
	struct fixture f={0};CHECK(initialize(&f)==0);
	st_rip3_mouse_move(f.mouse,30,30);
	CHECK(f.calls==2 && f.ids[0]==8 && f.ids[1]==7 && memcmp(f.events,"EE",2)==0 && !st_rip3_mouse_pending(f.mouse));
	CHECK(st_rip3_mouse_event(f.mouse,30,30,true));
	st_rip3_mouse_move(f.mouse,90,90);st_rip3_mouse_move(f.mouse,30,30);
	CHECK(f.calls==6 && memcmp(f.events,"EEXXEE",6)==0 && st_rip3_mouse_press_token(f.mouse)!=0);
	CHECK(st_rip3_mouse_event(f.mouse,30,30,false) && f.calls==7 && f.events[6]=='B' && f.ids[6]==8);
	st_rip3_mouse_move(f.mouse,30,30);CHECK(f.calls==7);
	CHECK(st_rip3_mouse_query(f.mouse,true,(const uint8_t *)"$oFf$",5)==0);
	st_rip3_mouse_move(f.mouse,90,90);st_rip3_mouse_move(f.mouse,30,30);CHECK(f.calls==9);
	CHECK(st_rip3_mouse_query(f.mouse,true,(const uint8_t *)"E",1)==0);
	st_rip3_mouse_move(f.mouse,30,30);CHECK(f.calls==9); /* OFF retains membership. */
	st_rip3_mouse_move(f.mouse,90,90);st_rip3_mouse_move(f.mouse,30,30);CHECK(f.calls==13);
	st_rip3_mouse_destroy(f.mouse);
	f=(struct fixture){0};CHECK(initialize(&f)==0);
	st_rip3_mouse_move(f.mouse,15,15);st_rip3_mouse_move(f.mouse,70,70);
	CHECK(f.calls==3 && memcmp(f.events,"EXE",3)==0 && f.ids[0]==7 && f.ids[1]==7 && f.ids[2]==8);
	st_rip3_mouse_move(f.mouse,80,30);CHECK(f.calls==4 && f.events[3]=='X' && f.ids[3]==8);
	st_rip3_mouse_move(f.mouse,60,30);CHECK(f.calls==5 && f.ids[4]==8);
	st_rip3_mouse_move(f.mouse,59,30);CHECK(f.calls==6 && f.ids[5]==7);
	const struct st_rip3_rect anonymous={100,100,120,120};
	CHECK(st_rip3_mouse_define(f.mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&anonymous,false,false,-1,NULL,0,NULL)==0);
	st_rip3_mouse_move(f.mouse,110,110);CHECK(f.calls==8 && memcmp(f.events+6,"XX",2)==0);
	st_rip3_mouse_move(f.mouse,111,111);CHECK(f.calls==8);
	st_rip3_mouse_destroy(f.mouse);
	f=(struct fixture){0};CHECK(initialize(&f)==0);
	CHECK(st_rip3_mouse_query(f.mouse,true,(const uint8_t *)"$OFF$",5)==0
	    && st_rip3_mouse_query(f.mouse,false,(const uint8_t *)"$OFF$",5)==0);
	st_rip3_mouse_move(f.mouse,30,30);CHECK(f.calls==0 && !st_rip3_mouse_pending(f.mouse));
	CHECK(st_rip3_mouse_query(f.mouse,false,(const uint8_t *)"X",1)==0);
	st_rip3_mouse_move(f.mouse,90,90);CHECK(f.calls==2 && memcmp(f.events,"XX",2)==0);
	st_rip3_mouse_destroy(f.mouse);
	f=(struct fixture){0};CHECK(initialize(&f)==0);f.remove_old=true;f.append_new=true;f.replace_query=true;
	st_rip3_mouse_move(f.mouse,30,30);
	CHECK(f.calls==1 && f.ids[0]==8 && f.events[0]=='E');
	st_rip3_mouse_move(f.mouse,30,30);CHECK(f.calls==2 && f.ids[1]==7 && f.events[1]=='N');
	st_rip3_mouse_destroy(f.mouse);
	f=(struct fixture){0};CHECK(initialize(&f)==0);f.suspend=true;
	st_rip3_mouse_move(f.mouse,30,30);CHECK(f.calls==1 && st_rip3_mouse_pending(f.mouse));
	st_rip3_mouse_resume(f.mouse);CHECK(f.calls==1);
	f.busy=false;st_rip3_mouse_resume(f.mouse);
	CHECK(f.calls==2 && f.ids[1]==7 && !st_rip3_mouse_pending(f.mouse));
	st_rip3_mouse_move(f.mouse,90,90);CHECK(f.calls==4 && memcmp(f.events,"EEXX",4)==0);
	st_rip3_mouse_destroy(f.mouse);
	f=(struct fixture){0};CHECK(initialize(&f)==0);f.kill=true;
	st_rip3_mouse_move(f.mouse,30,30);CHECK(f.calls==1 && f.events[0]=='E' && !st_rip3_mouse_pending(f.mouse));
	st_rip3_mouse_move(f.mouse,90,90);CHECK(f.calls==1);
	st_rip3_mouse_destroy(f.mouse);
	f=(struct fixture){0};CHECK(initialize(&f)==0);
	st_rip3_hover_test_fail_at=st_rip3_hover_test_attempts;
	CHECK(st_rip3_mouse_query(f.mouse,true,(const uint8_t *)"N",1)==IO_ERROR_OUT_OF_MEMORY);
	st_rip3_hover_test_fail_at=SIZE_MAX;
	st_rip3_mouse_move(f.mouse,30,30);CHECK(f.calls==2 && memcmp(f.events,"EE",2)==0);
	st_rip3_mouse_move(f.mouse,90,90);CHECK(f.calls==4);
	st_rip3_hover_test_fail_at=st_rip3_hover_test_attempts;
	st_rip3_mouse_move(f.mouse,30,30);
	CHECK(f.failures==1 && f.calls==5 && f.ids[4]==7 && !st_rip3_mouse_pending(f.mouse));
	st_rip3_hover_test_fail_at=SIZE_MAX;
	st_rip3_mouse_move(f.mouse,90,90);st_rip3_mouse_move(f.mouse,30,30);
	CHECK(f.calls==9 && memcmp(f.events+5,"XXEE",4)==0);
	st_rip3_mouse_destroy(f.mouse);CHECK(st_rip3_hover_test_live==0);return 0;
}
