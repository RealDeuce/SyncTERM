/* Authored outer-scanner/branch/provider-order vectors from PR98. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_angle.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do {if (!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}} while (0)
extern size_t st_rip3_angle_test_fail_at,st_rip3_angle_test_allocations;

struct fixture {const char *a,*b,*c,*d,*n;unsigned calls;char order[128];};

static int
value(struct fixture *f, const struct st_rip3_angle_request *request, struct st_rip3_condition_bytes *out)
{
	const char *text=NULL;char key;
	if (request->kind==ST_RIP3_ANGLE_TEMPLATE) key=(char)('0'+request->number);
	else {
		CHECK(request->reference.length>=4 && request->reference.bytes[0]=='$' && request->reference.bytes[1]=='&'
		    && request->reference.bytes[request->reference.length-1]=='$');
		key=(char)request->reference.bytes[2];if (key>='a' && key<='z') key-='a'-'A';
		if (key=='F') CHECK(request->reference.length==strlen("$&F(A\\)B)$")
		    && memcmp(request->reference.bytes,"$&F(A\\)B)$",request->reference.length)==0);
	}
	if (f->calls<sizeof(f->order)-1) {f->order[f->calls]=key;f->order[f->calls+1]=0;}f->calls++;
	switch (key) {case 'A':text=f->a;break;case 'B':text=f->b;break;case 'C':text=f->c;break;
	case 'D':text=f->d;break;case 'N':text=f->n;break;case 'I':case 'E':text="named";break;case 'F':text="argument";break;
	case ':':text="slot10";break;case '0':text="<<?1>>";break;
	case '1':text="<<?2>>";break;case '2':text="<<?3>>";break;case '3':text="<<?4>>";break;
	case '4':text="<<?5>>";break;case '5':text="<<?6>>";break;case '6':text="<<?7>>";break;
	case '7':text="<<?8>>";break;case '8':text="<<?9>>";break;case '9':text="END";break;default:break;}
	if (text==NULL) return IO_ERROR_NOT_FOUND;
	*out=(struct st_rip3_condition_bytes){(const uint8_t *)text,strlen(text)};return 0;
}

static int
run(struct fixture *f, const char *source, const char *expected, const char *order)
{
	struct st_rip3_angle *angle=NULL;f->calls=0;f->order[0]=0;
	CHECK(st_rip3_angle_create(&angle,(const uint8_t *)source,strlen(source))==0);
	for (unsigned i=0; i<10000 && st_rip3_angle_state(angle)<ST_RIP3_ANGLE_COMPLETE; i++) {
		st_rip3_angle_process(angle,1);
		const struct st_rip3_angle_request *request=st_rip3_angle_request(angle);
		if (request!=NULL) {
			/* Waiting does not allocate, advance state or invoke a provider. */
			size_t count=st_rip3_angle_test_allocations;
			st_rip3_angle_process(angle,99);CHECK(st_rip3_angle_request(angle)==request && st_rip3_angle_test_allocations==count);
			struct st_rip3_condition_bytes supplied={0};int status=value(f,request,&supplied);
			st_rip3_angle_supply(angle,&supplied,status);
		}
	}
	CHECK(strcmp(f->order,order)==0);
	const struct st_rip3_condition_bytes *result=st_rip3_angle_result(angle);
	if (expected==NULL) CHECK(st_rip3_angle_state(angle)==ST_RIP3_ANGLE_FAILED && result==NULL);
	else CHECK(st_rip3_angle_state(angle)==ST_RIP3_ANGLE_COMPLETE && result!=NULL && result->length==strlen(expected)
	    && memcmp(result->bytes,expected,result->length)==0);
	st_rip3_angle_destroy(angle);return 0;
}

static int
branches(void)
{
	struct fixture f={.a="1",.b="0",.c="1",.d="q",.n="2"};
	const char *chain="<<IF A=1>>x<<ELSEIF B=1>>y<<ELSEIF C=1>>z<<ELSE>>w<<ENDIF>>";
	CHECK(run(&f,chain,"x","A")==0);f.a="0";f.b="1";CHECK(run(&f,chain,"y","AB")==0);
	f.b="0";CHECK(run(&f,chain,"z","ABC")==0);f.c="0";CHECK(run(&f,chain,"w","ABC")==0);
	f.a="1";
	CHECK(run(&f,"<<IF A=0>><<D>><<IF B=1>>x<<ELSEIF C=1>>y<<ENDIF>><<ELSE>>r<<ENDIF>>","r","AD")==0);
	CHECK(run(&f,"<<IF A=0>><<?3>><<ELSE>>r<<ENDIF>>","r","A3")==0);
	CHECK(run(&f,"<<IF A=0>>\\<<N>><<ELSE>>r<<ENDIF>>","r","A")==0);
	CHECK(run(&f,"<<IF A=1>>x<<ELSEIF N=>>y<<ENDIF>>","x","A")==0);
	CHECK(run(&f,"<<IF A=1>>x<<ELSEIF N>>y<<ENDIF>>",NULL,"A")==0);
	CHECK(run(&f,"<<IF A=0>><<IF N>>x<<ENDIF>><<ENDIF>>",NULL,"A")==0);
	CHECK(run(&f,"<<IF A=1>>x<<ELSE>>y<<ELSEIF B=1>>z<<ENDIF>>",NULL,"A")==0);
	CHECK(run(&f,"<<IF A=0>>x<<ELSE>>y<<ELSE>>z<<ENDIF>>",NULL,"A")==0);
	CHECK(run(&f,"<<ELSE>>",NULL,"")==0 && run(&f,"<<ENDIF>>",NULL,"")==0);
	CHECK(run(&f,"<<IF A=1>>",NULL,"A")==0);
	f.a="1";f.b="0";f.c="0";
	CHECK(run(&f,"<<IF A=1 OR B=1 AND C=1>>x<<ELSE>>y<<ENDIF>>","y","ABC")==0);
	CHECK(run(&f,"<<IF Z!=1>>x<<ELSE>>y<<ENDIF>>","y","Z")==0);
	return 0;
}

static int
lexing_and_passes(void)
{
	struct fixture f={.a="<<B>>",.b="C",.n="a>>b"};
	CHECK(run(&f,"single < and >>","single < and >>","")==0 && run(&f,"<<A>>","C","AB")==0);
	CHECK(run(&f,"\\<<A>>","C","AB")==0 && run(&f,"\\\\<<A>>","C","AB")==0);
	CHECK(run(&f,"<<IF>> <<IFX>>","named named","II")==0);
	CHECK(run(&f,"<<ELSEIF>> <<ELSEIFX>> <<ELSEX>> <<ENDIF_X>>","named named named named","EEEE")==0);
	CHECK(run(&f,"<<F(A\\)B)>>","argument","F")==0 && run(&f,"<<?a>>","slot10",":")==0);
	CHECK(run(&f,"<<if N=\"a\\>>b\">>YES<<else>>NO<<endif>>","YES","N")==0);
	CHECK(run(&f,"<<IF N=\"a\\\\>>b\">>x<<ENDIF>>",NULL,"")==0);
	CHECK(run(&f,"<<IF N=\"a<<b\">>x<<ENDIF>>",NULL,"")==0);
	CHECK(run(&f,"<<IF N=\"a\\<<b\">>x<<ENDIF>>","","N")==0);
	CHECK(run(&f,"<<IF\tN=1>>",NULL,"")==0 && run(&f,"<< IF N=1>>",NULL,"")==0);
	CHECK(run(&f,"<<ELSE >>",NULL,"")==0 && run(&f,"<<$N$>>",NULL,"")==0 && run(&f,"<<>>",NULL,"")==0);
	CHECK(run(&f,"<<?AB>>",NULL,"")==0 && run(&f,"<<?A >>",NULL,"")==0);
	CHECK(run(&f,"<<?0>>","<<?8>>","01234567")==0 && run(&f,"<<?8>>","END","89")==0);
	f.a="<<IF N=1>>Y<<ELSE>>N<<ENDIF>>";f.n="1";CHECK(run(&f,"<<A>>","Y","AN")==0);
	CHECK(run(&f,"<<N",NULL,"")==0 && run(&f,"<<Z>>",NULL,"Z")==0);
	return 0;
}

static int
limits_and_failures(void)
{
	struct fixture f={.a="1",.n="R"};char source[270];
	memcpy(source,"<<N=",4);memset(source+4,'X',253);memcpy(source+257,">>",3);
	CHECK(run(&f,source,"R","N")==0);
	memmove(source+258,source+257,3);source[257]='X';CHECK(run(&f,source,NULL,"")==0);
	CHECK(run(&f,"<<N$trailing>>",NULL,"")==0);
	char large[1001];memset(large,'C',1000);large[1000]=0;f.b=large;
	st_rip3_angle_test_allocations=0;
	CHECK(run(&f,"<<IF A=1>><<B>><<ENDIF>>",large,"AB")==0);
	size_t allocations=st_rip3_angle_test_allocations;CHECK(allocations>=5);
	struct st_rip3_angle *retained=NULL;CHECK(st_rip3_angle_create(&retained,(const uint8_t *)"kept",4)==0);
	for (size_t failure=0; failure<allocations; failure++) {
		st_rip3_angle_test_allocations=0;st_rip3_angle_test_fail_at=failure;
		struct st_rip3_angle *angle=retained;
		int status=st_rip3_angle_create(&angle,(const uint8_t *)"<<IF A=1>><<B>><<ENDIF>>",24);
		if (status!=0) {CHECK(status==IO_ERROR_OUT_OF_MEMORY && angle==retained);continue;}
		for (unsigned step=0; step<100 && st_rip3_angle_state(angle)<ST_RIP3_ANGLE_COMPLETE; step++) {
			st_rip3_angle_process(angle,1);const struct st_rip3_angle_request *request=st_rip3_angle_request(angle);
			if (request!=NULL) {
				struct st_rip3_condition_bytes result={0};status=value(&f,request,&result);st_rip3_angle_supply(angle,&result,status);
			}
		}
		CHECK(st_rip3_angle_state(angle)==ST_RIP3_ANGLE_FAILED && st_rip3_angle_diagnostic(angle)==ST_RIP3_ANGLE_RESOURCE
		    && st_rip3_angle_result(angle)==NULL);st_rip3_angle_destroy(angle);
	}
	st_rip3_angle_test_fail_at=SIZE_MAX;st_rip3_angle_destroy(retained);
	/* A self-replacing name does not consume an entire owner-thread call. */
	struct st_rip3_angle *cycle=NULL;CHECK(st_rip3_angle_create(&cycle,(const uint8_t *)"<<A>>",5)==0);
	const struct st_rip3_condition_bytes repeated={(const uint8_t *)"<<A>>",5};
	for (unsigned i=0; i<3; i++) {
		st_rip3_angle_process(cycle,8);CHECK(st_rip3_angle_request(cycle)!=NULL);
		st_rip3_angle_supply(cycle,&repeated,0);
	}
	CHECK(st_rip3_angle_result(cycle)==NULL);st_rip3_angle_destroy(cycle);
	return 0;
}

static int
depth_and_ownership(void)
{
	char source[1000];size_t used=0;struct fixture f={.a="1"};
	for (unsigned i=0; i<16; i++) {memcpy(source+used,"<<IF A=1>>",10);used+=10;}
	source[used++]='X';for (unsigned i=0; i<16; i++) {memcpy(source+used,"<<ENDIF>>",9);used+=9;}source[used]=0;
	CHECK(run(&f,source,"X","AAAAAAAAAAAAAAAA")==0);
	memmove(source+10,source,used+1);memcpy(source,"<<IF A=1>>",10);
	CHECK(run(&f,source,NULL,"AAAAAAAAAAAAAAAA")==0);
	f.a="0";CHECK(run(&f,source,NULL,"A")==0);
	struct st_rip3_angle *angle=NULL;char owned[]="<<A>>";
	CHECK(st_rip3_angle_create(&angle,(const uint8_t *)owned,strlen(owned))==0);memset(owned,'!',strlen(owned));
	st_rip3_angle_process(angle,10);CHECK(st_rip3_angle_request(angle)!=NULL);
	const uint8_t bytes[]={'X',0,'Y'};const struct st_rip3_condition_bytes text={bytes,sizeof(bytes)};
	st_rip3_angle_supply(angle,&text,0);st_rip3_angle_process(angle,99);
	CHECK(st_rip3_angle_result(angle)!=NULL && st_rip3_angle_result(angle)->length==1 && st_rip3_angle_result(angle)->bytes[0]=='X');
	st_rip3_angle_destroy(angle);
	return 0;
}

int
main(void)
{
	CHECK(branches()==0 && lexing_and_passes()==0 && depth_and_ownership()==0 && limits_and_failures()==0);return 0;
}
