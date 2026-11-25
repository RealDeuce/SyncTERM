/* Independent PR89 matrix, asynchronous ownership and nonfatal failure checks. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_user_variables.h"
#include "st_rip3_host_internal.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { fprintf(stderr,"line %d: %s\n",__LINE__,#expression);return 1; } \
} while (0)

extern bool st_rip3_user_variables_test_fail_allocation;
extern int st_rip3_user_variables_test_store_status;

struct fixture {
	struct st_rip3_user_variables *processor;
	struct st_rip3_variables *session,*persistent;
	unsigned diagnostics,formats;
	enum st_rip3_user_variable_diagnostic last;
	enum st_rip3_user_variable_source source;
	struct st_rip3_variable_value input;
	int format_status;
	bool bad_length;
};

static void
diagnostic(void *context, enum st_rip3_user_variable_diagnostic kind)
{
	struct fixture *f=context;
	f->diagnostics++;f->last=kind;
}

static int
format(void *context, const struct st_rip3_variable_reference *reference,
    enum st_rip3_user_variable_source source, const struct st_rip3_variable_value *input,
    struct st_rip3_variable_value *result)
{
	struct fixture *f=context;
	f->formats++;f->source=source;f->input=*input;
	*result=*input;
	if (reference->conversion==ST_RIP3_VARIABLE_TOUPPER)
		for (size_t i=0; i<result->length; i++)
			if (result->bytes[i]>='a' && result->bytes[i]<='z') result->bytes[i]-='a'-'A';
	if (f->bad_length) result->length=256;
	return f->format_status;
}

static int
create(struct fixture *f, bool adapter)
{
	*f=(struct fixture){0};
	CHECK(st_rip3_variables_create(&f->session)==0 && st_rip3_variables_create(&f->persistent)==0);
	struct st_rip3_user_variable_host host={.session=f->session,.persistent=f->persistent,
	    .context=f,.format=adapter ? format : NULL,.diagnostic=diagnostic};
	CHECK(st_rip3_user_variables_create(&f->processor,&host)==0);
	return 0;
}

static void
destroy(struct fixture *f)
{
	st_rip3_user_variables_destroy(f->processor);
	st_rip3_variables_destroy(f->session);st_rip3_variables_destroy(f->persistent);
}

static int
set(struct st_rip3_variables *table, const char *value)
{
	CHECK(st_rip3_variables_set(table,"NAME",4,(const uint8_t *)value,strlen(value))==0);return 0;
}

static int
stored(struct st_rip3_variables *table, const char *expected)
{
	struct st_rip3_variable_value value;
	int status=st_rip3_variables_get(table,"NAME",4,&value);
	if (expected==NULL) CHECK(status==IO_ERROR_NOT_FOUND);
	else CHECK(status==0 && value.length==strlen(expected) && memcmp(value.bytes,expected,value.length)==0);
	return 0;
}

static int
result(struct fixture *f, const char *expected)
{
	const struct st_rip3_variable_value *value=st_rip3_user_variables_result(f->processor);
	CHECK(value!=NULL && value->length==strlen(expected) && memcmp(value->bytes,expected,value->length)==0
	    && value->bytes[value->length]==0);
	CHECK(st_rip3_user_variables_state(f->processor)==ST_RIP3_USER_VARIABLE_COMPLETE
	    && st_rip3_user_variables_prompt(f->processor)==NULL && st_rip3_user_variables_initial(f->processor)==NULL);
	return 0;
}

static int
process(struct fixture *f, const char *text)
{
	struct st_rip3_variable_reference reference;
	CHECK(st_rip3_variable_reference_parse(&reference,(const uint8_t *)text,strlen(text))==0);
	st_rip3_user_variables_process(f->processor,&reference);return 0;
}

static int
matrix(void)
{
	/* Literal table, in the specification's row order. V=existing, D=default,
	 * R=accepted response. This is not generated from the implementation. */
	static const struct row { unsigned transparent;bool exists,defaulted,prompt;char initial,selected; } rows[]={
		{0,true,true,true,'V','R'},{0,true,false,true,'V','R'},
		{0,false,true,true,'D','R'},{0,false,false,true,0,'R'},
		{1,true,true,false,0,'D'},{1,true,false,true,'V','R'},
		{1,false,true,false,0,'D'},{1,false,false,true,0,'R'},
		{2,true,true,false,0,'V'},{2,true,false,false,0,'V'},
		{2,false,true,false,0,'D'},{2,false,false,true,0,'R'},
		{3,true,true,false,0,'D'},{3,true,false,true,'V','R'},
		{3,false,true,false,0,'D'},{3,false,false,true,0,'R'},
	};
	for (size_t row=0; row<sizeof(rows)/sizeof(rows[0]); row++)
	for (unsigned destination=0; destination<4; destination++)
	for (unsigned ancillary=0; ancillary<4; ancillary++)
	for (unsigned location=0; location<3; location++) {
		struct fixture f;CHECK(create(&f,true)==0);
		const struct row *r=&rows[row];
		const char *old_session=r->exists && location!=1 ? "S" : NULL;
		const char *old_persistent=r->exists && location!=0 ? "P" : NULL;
		if (old_session!=NULL) CHECK(set(f.session,old_session)==0);
		if (old_persistent!=NULL) CHECK(set(f.persistent,old_persistent)==0);
		char v=old_session!=NULL ? 'S' : 'P';
		struct st_rip3_variable_reference reference={.name="NAME",.has_default=r->defaulted,
		    .default_value={.length=1,.bytes={'D'}}};
		reference.directives=(r->transparent&1 ? ST_RIP3_VARIABLE_SET : 0)
		    | (r->transparent&2 ? ST_RIP3_VARIABLE_GET : 0)
		    | (destination&1 ? ST_RIP3_VARIABLE_SESSION : 0)
		    | (destination&2 ? ST_RIP3_VARIABLE_PERSISTENT : 0)
		    | (ancillary&1 ? ST_RIP3_VARIABLE_REQUIRED : 0)
		    | (ancillary&2 ? ST_RIP3_VARIABLE_MASKED : 0);
		st_rip3_user_variables_process(f.processor,&reference);
		if (r->prompt) {
			CHECK(st_rip3_user_variables_state(f.processor)==ST_RIP3_USER_VARIABLE_PROMPT && f.formats==0);
			const struct st_rip3_variable_value *initial=st_rip3_user_variables_initial(f.processor);
			char expected=r->initial=='V' ? v : r->initial;
			CHECK(initial!=NULL && initial->length==(expected==0 ? 0 : 1));
			if (expected!=0) CHECK(initial->bytes[0]==(uint8_t)expected);
			CHECK(stored(f.session,old_session)==0 && stored(f.persistent,old_persistent)==0);
			st_rip3_user_variables_respond(f.processor,(const uint8_t *)"R",1,false);
		}
		char selected=r->selected=='V' ? v : r->selected;
		char chosen[]={selected,0};
		CHECK(result(&f,r->transparent==1 ? "" : chosen)==0 && f.formats==1 && f.input.length==1
		    && f.input.bytes[0]==(uint8_t)selected && f.diagnostics==0);
		CHECK(f.source==(r->selected=='R' ? ST_RIP3_USER_VARIABLE_RESPONSE
		    : r->selected=='D' ? ST_RIP3_USER_VARIABLE_DEFAULT : ST_RIP3_USER_VARIABLE_EXISTING));
		CHECK(stored(f.session,destination==1 ? chosen : old_session)==0);
		CHECK(stored(f.persistent,(destination&2)!=0 ? chosen : old_persistent)==0);
		destroy(&f);
	}
	return 0;
}

static int
empty_and_cancellation(void)
{
	struct fixture f;CHECK(create(&f,true)==0);
	CHECK(process(&f,"$-=NAME=$")==0 && result(&f,"")==0 && stored(f.session,"")==0);
	CHECK(process(&f,"$&NAME=other$")==0 && result(&f,"NULL")==0 && f.source==ST_RIP3_USER_VARIABLE_EXISTING);
	CHECK(process(&f,"$-=NAME$")==0 && st_rip3_user_variables_initial(f.processor)->length==0);
	st_rip3_user_variables_respond(f.processor,NULL,0,false);
	CHECK(result(&f,"")==0 && stored(f.session,"")==0);
	CHECK(process(&f,"$=NAME$")==0);st_rip3_user_variables_respond(f.processor,NULL,0,false);
	CHECK(result(&f,"NULL")==0 && stored(f.session,"")==0);
	CHECK(set(f.session,"S")==0 && set(f.persistent,"P")==0);
	const char *const forms[]={"$+=NAME$","$-+=NAME$","$&+=OTHER$","$-&+=NAME$"};
	for (unsigned i=0; i<4; i++) {
		unsigned formats=f.formats;
		CHECK(process(&f,forms[i])==0 && st_rip3_user_variables_state(f.processor)==ST_RIP3_USER_VARIABLE_PROMPT);
		st_rip3_user_variables_respond(f.processor,NULL,SIZE_MAX,true);
		CHECK(result(&f,i==0 ? "NONE" : "")==0 && f.formats==formats
		    && stored(f.session,"S")==0 && stored(f.persistent,"P")==0);
	}
	CHECK(process(&f,"$*#+=NAME$")==0);
	st_rip3_user_variables_respond(f.processor,NULL,0,true);
	CHECK(f.last==ST_RIP3_USER_VARIABLE_CANCELLATION_DISABLED
	    && st_rip3_user_variables_state(f.processor)==ST_RIP3_USER_VARIABLE_PROMPT);
	st_rip3_user_variables_respond(f.processor,NULL,0,false);
	CHECK(result(&f,"NULL")==0 && stored(f.session,"S")==0 && stored(f.persistent,"")==0);
	/* Required does not force a prompt on a transparent success. */
	CHECK(process(&f,"$*#&NAME$")==0 && result(&f,"S")==0);
	/* Protocol sentinels are ordinary data when supplied explicitly. */
	CHECK(process(&f,"$-&=NAME=NONE$")==0 && result(&f,"NONE")==0 && stored(f.session,"NONE")==0);
	CHECK(process(&f,"$-&=NAME=NULL$")==0 && result(&f,"NULL")==0 && stored(f.session,"NULL")==0);
	destroy(&f);return 0;
}

static int
ownership_and_failures(void)
{
	struct fixture f;CHECK(create(&f,true)==0);
	CHECK(set(f.session,"S")==0 && set(f.persistent,"P")==0);
	struct st_rip3_variable_reference reference;
	const char *text="$*#+=20,50:name(ANY,TOUPPER),30@Question=default$";
	CHECK(st_rip3_variable_reference_parse(&reference,(const uint8_t *)text,strlen(text))==0);
	st_rip3_user_variables_process(f.processor,&reference);
	memset(&reference,0,sizeof(reference));
	const struct st_rip3_variable_reference *prompt=st_rip3_user_variables_prompt(f.processor);
	CHECK(prompt!=NULL && strcmp(prompt->name,"NAME")==0 && prompt->has_position && prompt->x==20 && prompt->y==50
	    && prompt->has_width && prompt->width==30 && prompt->has_question && prompt->question.length==8
	    && memcmp(prompt->question.bytes,"Question",8)==0 && prompt->default_value.length==7
	    && (prompt->directives&ST_RIP3_VARIABLE_MASKED)!=0);
	CHECK(set(f.session,"changed")==0 && st_rip3_user_variables_initial(f.processor)->bytes[0]=='S');
	CHECK(process(&f,"$-=OTHER=bad$")==0 && f.last==ST_RIP3_USER_VARIABLE_BUSY
	    && strcmp(st_rip3_user_variables_prompt(f.processor)->name,"NAME")==0);
	st_rip3_user_variables_respond(f.processor,(const uint8_t *)"ada",3,false);
	CHECK(result(&f,"ADA")==0 && stored(f.session,"changed")==0 && stored(f.persistent,"ADA")==0);
	CHECK(process(&f,"$*+=NAME$")==0);st_rip3_user_variables_abort(f.processor);
	CHECK(st_rip3_user_variables_state(f.processor)==ST_RIP3_USER_VARIABLE_IDLE && stored(f.persistent,"ADA")==0);
	for (unsigned which=0; which<4; which++) {
		f.format_status=which==0 ? IO_ERROR_INVALID_FORMAT : 0;f.bad_length=which==1;
		st_rip3_user_variables_test_store_status=which==2 ? IO_ERROR_OUT_OF_MEMORY : which==3 ? IO_ERROR_OUT_OF_RANGE : 0;
		CHECK(process(&f,"$-&+=NAME=new$")==0 && st_rip3_user_variables_state(f.processor)==ST_RIP3_USER_VARIABLE_IDLE
		    && st_rip3_user_variables_result(f.processor)==NULL && stored(f.session,"changed")==0 && stored(f.persistent,"ADA")==0);
		CHECK(f.last==(which==2 ? ST_RIP3_USER_VARIABLE_RESOURCE_FAILED : ST_RIP3_USER_VARIABLE_OPERATION_FAILED));
		f.format_status=0;f.bad_length=false;st_rip3_user_variables_test_store_status=0;
		CHECK(process(&f,"$&NAME$")==0 && result(&f,"changed")==0);
	}
	/* A resumed prompt's failed write also emits nothing and can recover. */
	CHECK(process(&f,"$+=NAME$")==0);
	st_rip3_user_variables_test_store_status=IO_ERROR_OUT_OF_MEMORY;
	st_rip3_user_variables_respond(f.processor,(const uint8_t *)"lost",4,false);
	CHECK(f.last==ST_RIP3_USER_VARIABLE_RESOURCE_FAILED && st_rip3_user_variables_result(f.processor)==NULL
	    && st_rip3_user_variables_state(f.processor)==ST_RIP3_USER_VARIABLE_IDLE
	    && stored(f.session,"changed")==0 && stored(f.persistent,"ADA")==0);
	st_rip3_user_variables_test_store_status=0;
	CHECK(process(&f,"$+=NAME$")==0);
	st_rip3_user_variables_respond(f.processor,(const uint8_t *)"ADA",3,false);
	CHECK(result(&f,"ADA")==0 && stored(f.persistent,"ADA")==0);
	CHECK(process(&f,"$=NAME$")==0);st_rip3_user_variables_respond(f.processor,NULL,1,false);
	CHECK(f.last==ST_RIP3_USER_VARIABLE_INVALID && st_rip3_user_variables_state(f.processor)==ST_RIP3_USER_VARIABLE_IDLE);
	st_rip3_user_variables_respond(f.processor,NULL,0,false);CHECK(f.last==ST_RIP3_USER_VARIABLE_INVALID);
	st_rip3_user_variables_process(f.processor,NULL);CHECK(f.last==ST_RIP3_USER_VARIABLE_INVALID);
	for (unsigned which=0; which<7; which++) {
		reference=(struct st_rip3_variable_reference){.name="NAME"};
		switch(which) {
		case 0:memset(reference.name,'A',sizeof(reference.name));break;
		case 1:reference.name[0]='_';break;case 2:reference.directives=64;break;
		case 3:reference.mode=(enum st_rip3_variable_mode)99;break;
		case 4:reference.conversion=(enum st_rip3_variable_conversion)99;break;
		case 5:reference.question.length=101;break;default:reference.default_value.length=101;break;
		}
		unsigned calls=f.formats;
		st_rip3_user_variables_process(f.processor,&reference);
		CHECK(f.last==ST_RIP3_USER_VARIABLE_INVALID && f.formats==calls && stored(f.persistent,"ADA")==0);
	}
	/* Bounded response bytes retain CP437, embedded NUL and the first 255 bytes. */
	uint8_t bytes[300];memset(bytes,0xff,sizeof(bytes));bytes[0]=0x80;bytes[1]=0;
	CHECK(process(&f,"$=NAME$")==0);st_rip3_user_variables_respond(f.processor,bytes,sizeof(bytes),false);
	const struct st_rip3_variable_value *output=st_rip3_user_variables_result(f.processor);
	struct st_rip3_variable_value saved;
	CHECK(output!=NULL && output->length==255 && memcmp(output->bytes,bytes,255)==0 && output->bytes[255]==0);
	CHECK(st_rip3_variables_get(f.session,"NAME",4,&saved)==0 && saved.length==255 && memcmp(saved.bytes,bytes,255)==0);
	struct st_rip3_user_variable_host host={.session=f.session,.persistent=f.persistent};
	struct st_rip3_user_variables *old=f.processor;
	st_rip3_user_variables_test_fail_allocation=true;
	CHECK(st_rip3_user_variables_create(&f.processor,&host)==IO_ERROR_OUT_OF_MEMORY && f.processor==old);
	st_rip3_user_variables_test_fail_allocation=false;
	CHECK(st_rip3_user_variables_create(NULL,&host)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_user_variables_create(&old,NULL)==IO_ERROR_INVALID_ARGUMENT);
	host.persistent=host.session;CHECK(st_rip3_user_variables_create(&old,&host)==IO_ERROR_INVALID_ARGUMENT);
	destroy(&f);
	CHECK(create(&f,false)==0 && process(&f,"$-&NAME=plain$")==0 && result(&f,"plain")==0);
	CHECK(process(&f,"$-&=NAME(TOUPPER)=plain$")==0 && result(&f,"PLAIN")==0
	    && stored(f.session,"PLAIN")==0);
	CHECK(process(&f,"$&NAME(ToLower)$")==0 && result(&f,"plain")==0
	    && stored(f.session,"PLAIN")==0);
	CHECK(process(&f,"$=NAME(ANY,Toname)$")==0
	    && st_rip3_user_variables_state(f.processor)==ST_RIP3_USER_VARIABLE_PROMPT);
	const char response[] = "tOM\t  mCDONALD \x82";
	st_rip3_user_variables_respond(f.processor,(const uint8_t *)response,sizeof(response)-1,false);
	CHECK(result(&f,"Tom\t  Mcdonald \x82")==0 && stored(f.session,"Tom\t  Mcdonald \x82")==0);
	CHECK(process(&f,"$=NAME(ALPHA)$")==0);
	prompt=st_rip3_user_variables_prompt(f.processor);
	CHECK(prompt!=NULL && prompt->mode==ST_RIP3_VARIABLE_ALPHA);
	destroy(&f);
	st_rip3_user_variables_abort(NULL);st_rip3_user_variables_destroy(NULL);
	st_rip3_user_variables_process(NULL,NULL);st_rip3_user_variables_respond(NULL,NULL,0,true);
	CHECK(st_rip3_user_variables_state(NULL)==ST_RIP3_USER_VARIABLE_IDLE && st_rip3_user_variables_result(NULL)==NULL
	    && st_rip3_user_variables_prompt(NULL)==NULL && st_rip3_user_variables_initial(NULL)==NULL);
	return 0;
}

int
main(void)
{
	CHECK(matrix()==0 && empty_and_cancellation()==0 && ownership_and_failures()==0);
	return 0;
}
