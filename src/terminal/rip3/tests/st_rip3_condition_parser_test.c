/* Independent PR92 source-byte, stage-boundary and failure expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_condition_internal.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { \
	fprintf(stderr,"line %d: %s\n",__LINE__,#expression);return 1; } } while (0)
extern bool st_rip3_condition_parser_test_fail;
extern size_t st_rip3_condition_parser_test_allocations;

struct fixture {
	const uint8_t *value;
	size_t length,calls,templates;
	unsigned slot;
	int status;
	char references[8][259];
	bool changing;
	uint8_t scratch;
};

static int
value(struct fixture *f, struct st_rip3_condition_bytes *result)
{
	if (f->changing) {
		f->scratch=(uint8_t)('A'+f->calls-1);
		*result=(struct st_rip3_condition_bytes){&f->scratch,1};
	}
	else *result=(struct st_rip3_condition_bytes){f->value,f->length};
	return f->status;
}

static int
reference(void *context, const struct st_rip3_condition_bytes *reference,
    struct st_rip3_condition_bytes *result)
{
	struct fixture *f=context;
	if (f->calls<8 && reference->length<259) {
		memcpy(f->references[f->calls],reference->bytes,reference->length);
		f->references[f->calls][reference->length]=0;
	}
	f->calls++;
	return value(f,result);
}

static int
template(void *context, unsigned number, struct st_rip3_condition_bytes *result)
{
	struct fixture *f=context;
	f->calls++;f->templates++;f->slot=number;
	return value(f,result);
}

static int
evaluate(struct fixture *f, const uint8_t *source, size_t length, bool expected)
{
	struct st_rip3_condition_program *program=NULL;
	struct st_rip3_condition_provider provider={f,reference,template};
	size_t consumed=SIZE_MAX;
	CHECK(st_rip3_condition_program_parse(&program,source,length,&consumed)==0 && consumed<=length);
	bool result=!expected;
	CHECK(st_rip3_condition_program_evaluate(&result,program,&provider)==0 && result==expected);
	st_rip3_condition_program_destroy(program);return 0;
}

static int
text(struct fixture *f, const char *source, bool expected)
{
	return evaluate(f,(const uint8_t *)source,strlen(source),expected);
}

static int
examples(void)
{
	const struct { const char *source,*literal,*reference; } cases[]={
		{"N=A\\ B>>","A B","$&N$"},
		{"N=\"a\\\\\\\"b\">>","a\\\"b","$&N$"},
		{"N=\"a\\\"b\">>","a\"b","$&N$"},
		{"N=\"a\\>>b\">>","a>>b","$&N$"},
		{"N=A\\)>>","A)","$&N$"},
		{"(N=)>>","","$&N$"},
		{"N=>>","","$&N$"},{"N=\"\">>","","$&N$"},
		{"N=\\ A\\ >>"," A","$&N$"},
		{"N=\"$SHIFT$\">>","$SHIFT$","$&N$"},
		{"N=\\$N\\$>>","$N$","$&N$"},
		{"n=A>>","A","$&n$"},{"$n$=A>>","A","$&n$"},
		{"$RAND(9,2)$=A>>","A","$&RAND(9,2)$"},
		{"F(MY\\ BBS)=A>>","A","$&F(MY BBS)$"},
		{"$F(A\\\\)B)$=A>>","A","$&F(A\\)B)$"},
		{"N=A\"B>>","A\"B","$&N$"},
		{"N='A'>>","'A'","$&N$"},
	};
	for (size_t i=0; i<sizeof(cases)/sizeof(cases[0]); i++) {
		struct fixture f={.value=(const uint8_t *)cases[i].literal,.length=strlen(cases[i].literal)};
		CHECK(text(&f,cases[i].source,true)==0 && f.calls==1 && f.templates==0
		    && strcmp(f.references[0],cases[i].reference)==0);
	}
	const char *const invalid[]={
		"N=A\\\\ B>>","N=\"a>>b\">>","N=\"a\\\\>>b\">>","1N=A>>","$1N$=A>>",
		"N=","N=\"a\"b>>","N=A AND\tN=B>>","NOT\tN=A>>","N=)>>",
		"$&N$=A>>","$?N$=A>>","_N=A>>","&N=A>>","\\N=A>>","\"N\"=A>>",
		"N$=A>>","$N=A>>","?aX=A>>","N>>","N!A>>","N=A","N=\\",
		"N=A AND >>","(N=A>>","N=A)>>","()>>","NOT >>","N=\"A>>",
	};
	for (size_t i=0; i<sizeof(invalid)/sizeof(invalid[0]); i++) {
		size_t consumed=99;
		CHECK(st_rip3_condition_source_validate((const uint8_t *)invalid[i],strlen(invalid[i]),&consumed)!=0 && consumed==99);
	}
	struct fixture f={.value=(const uint8_t *)"A",.length=1};
	CHECK(text(&f,"?a=A>>",true)==0 && f.calls==1 && f.templates==1 && f.slot==10);
	f.calls=f.templates=0;
	CHECK(text(&f,"?0=A>>",true)==0 && f.calls==1 && f.templates==1 && f.slot==0);
	const char *const provider_invalid[]={"$F(A\\\"B)$=A>>","$F(A\\\\\\\\B)$!=A>>","$N\\$TAIL$=A>>","$N )$=A>>"};
	for (size_t i=0; i<sizeof(provider_invalid)/sizeof(provider_invalid[0]); i++) {
		f.calls=0;
		CHECK(text(&f,provider_invalid[i],false)==0 && f.calls==0);
	}
	return 0;
}

static int
bytes_and_structure(void)
{
	for (unsigned byte=0; byte<256; byte++) {
		uint8_t source[]={(uint8_t)byte,'=','A','>','>'};
		bool letter=(byte>='A' && byte<='Z') || (byte>='a' && byte<='z');
		CHECK((st_rip3_condition_source_validate(source,sizeof(source),NULL)==0)==letter);
		uint8_t quoted[]={'N','=','"',(uint8_t)byte,'"','>','>'};
		bool payload=byte!=0 && byte!='"' && byte!='\\';
		CHECK((st_rip3_condition_source_validate(quoted,sizeof(quoted),NULL)==0)==payload);
		if (payload) {
			uint8_t value=(uint8_t)byte;struct fixture f={.value=&value,.length=1};
			CHECK(evaluate(&f,quoted,sizeof(quoted),true)==0);
		}
	}
	const uint8_t utf8[]={0xe2,0x98,0x83};struct fixture f={.value=utf8,.length=sizeof(utf8)};
	CHECK(text(&f,"N=\xe2\x98\x83>>",true)==0);
	const char *const valid[]={"N=A AND N=A>>","N=A\tAND N=A>>","N=A AND \t(N=A)>>",
	    "not (N=A)>>","NOT NOT N=A>>","N=\"A\"OR N=A>>"};
	for (size_t i=0; i<sizeof(valid)/sizeof(valid[0]); i++)
		CHECK(st_rip3_condition_source_validate((const uint8_t *)valid[i],strlen(valid[i]),NULL)==0);
	f=(struct fixture){.value=(const uint8_t *)"1",.length=1};
	CHECK(text(&f,"N=1 OR N=0 AND N=0>>",false)==0 && f.calls==3);
	f.calls=0;CHECK(text(&f,"NOT N=1 OR N=1>>",false)==0 && f.calls==2);
	f.calls=0;CHECK(text(&f,"NOT (N=1 OR N=0)>>",false)==0 && f.calls==2);
	f=(struct fixture){.changing=true};
	CHECK(text(&f,"N=A AND M=B>>",true)==0 && f.calls==2
	    && strcmp(f.references[0],"$&N$")==0 && strcmp(f.references[1],"$&M$")==0);
	f=(struct fixture){.value=(const uint8_t *)"1",.length=1};
	CHECK(text(&f,"N=0 AND M=1>>",false)==0 && f.calls==2); /* No short circuit. */
	char deep[270];memset(deep,'(',128);memcpy(deep+128,"N=1",3);memset(deep+131,')',128);memcpy(deep+259,">>",3);
	CHECK(text(&f,deep,true)==0);
	const uint8_t nul[]={'N','=','A',0,'>','>'};
	CHECK(st_rip3_condition_source_validate(nul,sizeof(nul),NULL)==IO_ERROR_INVALID_FORMAT);
	size_t consumed=0;
	CHECK(st_rip3_condition_source_validate((const uint8_t *)"  N=A>>tail",11,&consumed)==0 && consumed==7);
	return 0;
}

static int
limits_and_failures(void)
{
	uint8_t source[1600],value[256];memset(value,'A',sizeof(value));
	memcpy(source,"N=",2);
	for (size_t i=0; i<255; i++) { source[2+2*i]='\\';source[3+2*i]='A'; }
	memcpy(source+512,">>",2);
	struct fixture f={.value=value,.length=255};
	CHECK(evaluate(&f,source,514,true)==0 && f.calls==1);
	memcpy(source+512,"\\A>>",4);
	CHECK(st_rip3_condition_source_validate(source,516,NULL)==IO_ERROR_OUT_OF_RANGE);
	/* Capacity is checked before trimming escaped whitespace. */
	memcpy(source,"N=",2);memset(source+2,'A',254);memcpy(source+256,"\\ >>",4);
	f.length=254;CHECK(evaluate(&f,source,260,true)==0);
	memcpy(source+258,"\\ >>",4);
	CHECK(st_rip3_condition_source_validate(source,262,NULL)==IO_ERROR_OUT_OF_RANGE);
	/* The special escaped terminator pair needs two available output slots. */
	memcpy(source,"N=",2);memset(source+2,'A',253);memcpy(source+255,"\\>>>>",5);
	value[253]=value[254]='>';f.length=255;
	CHECK(evaluate(&f,source,260,true)==0);
	memcpy(source,"N=",2);memset(source+2,'A',254);memcpy(source+256,"\\>>>>",5);
	CHECK(st_rip3_condition_source_validate(source,261,NULL)==IO_ERROR_OUT_OF_RANGE);
	/* Long source encoding is legal; the decoded left body has its own bound,
	 * independent of the provider's narrower identifier/argument validation. */
	source[0]='$';source[1]='N';
	for (size_t i=0; i<254; i++) { source[2+2*i]='\\';source[3+2*i]='A'; }
	memcpy(source+510,"$=A>>",5);
	CHECK(st_rip3_condition_source_validate(source,515,NULL)==0);
	memcpy(source+510,"\\A$=A>>",7);
	CHECK(st_rip3_condition_source_validate(source,517,NULL)==IO_ERROR_OUT_OF_RANGE);
	f=(struct fixture){.value=value,.length=256};CHECK(text(&f,"N!=A>>",false)==0 && f.calls==1);
	f=(struct fixture){.status=IO_ERROR_NOT_FOUND};CHECK(text(&f,"N!=>>",false)==0 && f.calls==1);
	f=(struct fixture){0};CHECK(text(&f,"N=>>",true)==0 && f.calls==1);
	struct st_rip3_condition_program *program=NULL;
	char owned[]="n=A>>later";size_t consumed=99;
	CHECK(st_rip3_condition_program_parse(&program,(const uint8_t *)owned,strlen(owned),&consumed)==0 && consumed==5);
	memset(owned,'?',sizeof(owned)-1);
	f=(struct fixture){.value=(const uint8_t *)"A",.length=1};
	struct st_rip3_condition_provider provider={&f,reference,template};bool result=false;
	CHECK(st_rip3_condition_program_evaluate(&result,program,&provider)==0 && result && strcmp(f.references[0],"$&n$")==0);
	struct st_rip3_condition_program *saved=program;
	st_rip3_condition_parser_test_fail=true;consumed=99;
	size_t allocations=st_rip3_condition_parser_test_allocations;
	CHECK(st_rip3_condition_source_validate((const uint8_t *)"N=A>>",5,&consumed)==0 && consumed==5
	    && st_rip3_condition_parser_test_allocations==allocations);
	consumed=99;
	CHECK(st_rip3_condition_program_parse(&program,(const uint8_t *)"N=A>>",5,&consumed)==IO_ERROR_OUT_OF_MEMORY
	    && program==saved && consumed==99);
	allocations=st_rip3_condition_parser_test_allocations;
	CHECK(st_rip3_condition_program_parse(&program,(const uint8_t *)"N=",2,&consumed)==IO_ERROR_INVALID_FORMAT
	    && program==saved && consumed==99 && st_rip3_condition_parser_test_allocations==allocations);
	st_rip3_condition_parser_test_fail=false;
	st_rip3_condition_program_destroy(program);program=NULL;
	CHECK(st_rip3_condition_program_parse(&program,(const uint8_t *)"?0=A>>",6,NULL)==0);
	provider.template=NULL;result=true;
	CHECK(st_rip3_condition_program_evaluate(&result,program,&provider)==0 && !result);
	result=true;CHECK(st_rip3_condition_program_evaluate(&result,program,NULL)==0 && !result);
	st_rip3_condition_program_destroy(program);
	CHECK(st_rip3_condition_program_parse(NULL,(const uint8_t *)"N=A>>",5,NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_condition_source_validate(NULL,0,NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_condition_source_validate((const uint8_t *)"N=A>>",SIZE_MAX,NULL)==IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_condition_program_evaluate(NULL,NULL,NULL)==IO_ERROR_INVALID_ARGUMENT);
	st_rip3_condition_program_destroy(NULL);
	return 0;
}

int
main(void)
{
	CHECK(examples()==0 && bytes_and_structure()==0 && limits_and_failures()==0);
	return 0;
}
