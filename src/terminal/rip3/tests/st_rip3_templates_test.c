/* Independent intended-composition, ownership and failure expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_templates.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { \
	fprintf(stderr,"line %d: %s\n",__LINE__,#expression);return 1; } } while (0)
extern bool st_rip3_templates_test_fail;

static int
set(struct st_rip3_templates *slots, unsigned number, const char *value)
{
	CHECK(st_rip3_templates_set(slots,number,(const uint8_t *)value,strlen(value))==0);return 0;
}

static int
chain(struct st_rip3_templates *slots, const uint8_t *numbers, size_t count,
    const char *feed, const char *expected)
{
	uint8_t result[4096];size_t length=SIZE_MAX;
	CHECK(st_rip3_templates_chain(slots,numbers,count,(const uint8_t *)feed,strlen(feed),
	    result,sizeof(result),&length,NULL)==0 && length==strlen(expected) && memcmp(result,expected,length)==0);
	return 0;
}

int
main(void)
{
	struct st_rip3_templates *slots=NULL;struct st_rip3_template_bytes view;
	CHECK(st_rip3_templates_create(&slots)==0);
	for (unsigned i=0; i<36; i++) CHECK(st_rip3_templates_resolve(slots,i,&view)==0 && view.length==0);
	CHECK(set(slots,3,"plain")==0 && set(slots,4,"inserts $?$ here")==0 && set(slots,5,"two $?$ and $?$ codes")==0);
	CHECK(chain(slots,(const uint8_t[]){3},1,"LOST","plain")==0);
	CHECK(chain(slots,(const uint8_t[]){4},1,"SOMETHING","inserts SOMETHING here")==0);
	CHECK(chain(slots,(const uint8_t[]){5},1,"HERE","two HERE and HERE codes")==0);
	CHECK(set(slots,1,"red green $?$ blue")==0 && set(slots,14,"LOUD QUIET $?$ YELL")==0
	    && set(slots,3,"soft $?$ hard smooth gritty")==0);
	CHECK(chain(slots,(const uint8_t[]){1,14,3},3,"(host command)",
	    "soft LOUD QUIET red green (host command) blue YELL hard smooth gritty")==0);
	CHECK(set(slots,3,"$D(1)$")==0 && set(slots,4,"FOUR")==0);
	CHECK(chain(slots,(const uint8_t[]){3,4},2,"X","FOUR")==0);
	CHECK(set(slots,3,"$?$")==0 && chain(slots,(const uint8_t[]){3},1,"$?$","$?$")==0);
	CHECK(set(slots,4,"print(\"$?$\")^m")==0);
	CHECK(chain(slots,(const uint8_t[]){4,4},2,"HELLO","print(\"print(\"HELLO\")^m\")^m")==0);
	uint8_t levels[36];memset(levels,3,sizeof(levels));
	CHECK(chain(slots,levels,36,"unchanged","unchanged")==0);
	/* Blank ordinary slots discard their feed; required blanks report the
	 * missing group without publishing either a partial result or length. */
	CHECK(chain(slots,(const uint8_t[]){0},1,"X","")==0);
	CHECK(st_rip3_templates_required(slots,0,true)==0);
	view=(struct st_rip3_template_bytes){.length=99};
	CHECK(st_rip3_templates_resolve(slots,0,&view)==IO_ERROR_NOT_FOUND && view.length==99);
	uint8_t result[4096];memset(result,0x5a,sizeof(result));size_t length=99;unsigned missing=99;
	CHECK(st_rip3_templates_chain(slots,(const uint8_t[]){3,0,4},3,(const uint8_t *)"X",1,result,sizeof(result),&length,&missing)
	    ==IO_ERROR_NOT_FOUND && missing==0 && length==99);
	for (size_t i=0; i<sizeof(result); i++) CHECK(result[i]==0x5a);
	CHECK(set(slots,0,"selected")==0 && st_rip3_templates_resolve(slots,0,&view)==0 && view.length==8);
	CHECK(set(slots,0,"")==0 && st_rip3_templates_resolve(slots,0,&view)==IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_templates_required(slots,0,false)==0 && st_rip3_templates_resolve(slots,0,&view)==0);
	/* Definitions own their capped bytes and can safely be replaced from
	 * their own borrowed view. Storage counts raw, unexpanded bytes. */
	uint8_t source[5000];memset(source,'A',sizeof(source));
	CHECK(st_rip3_templates_set(slots,35,source,sizeof(source))==0);memset(source,'B',sizeof(source));
	CHECK(st_rip3_templates_resolve(slots,35,&view)==0 && view.length==4096 && view.bytes[4095]=='A');
	CHECK(st_rip3_templates_set(slots,35,view.bytes,view.length)==0);
	CHECK(st_rip3_templates_chain(slots,(const uint8_t[]){35},1,NULL,0,result,sizeof(result),&length,NULL)==0 && length==4096);
	for (size_t i=0; i<length; i++) CHECK(result[i]=='A');
	CHECK(set(slots,35,"x$?$y$?$z")==0);
	CHECK(st_rip3_templates_chain(slots,(const uint8_t[]){35},1,source,sizeof(source),result,sizeof(result),&length,NULL)==0
	    && length==4096 && result[0]=='x');
	for (size_t i=1; i<length; i++) CHECK(result[i]=='B');
	const uint8_t terminated[]={0x80,0xff,0,'X'};
	CHECK(st_rip3_templates_set(slots,34,terminated,sizeof(terminated))==0
	    && st_rip3_templates_resolve(slots,34,&view)==0 && view.length==2 && view.bytes[0]==0x80 && view.bytes[1]==0xff);
	st_rip3_templates_test_fail=true;
	CHECK(st_rip3_templates_set(slots,3,(const uint8_t *)"lost",4)==IO_ERROR_OUT_OF_MEMORY);
	CHECK(st_rip3_templates_resolve(slots,3,&view)==0 && view.length==3 && memcmp(view.bytes,"$?$",3)==0);
	struct st_rip3_templates *old=slots;
	CHECK(st_rip3_templates_create(&slots)==IO_ERROR_OUT_OF_MEMORY && slots==old);
	st_rip3_templates_test_fail=false;
	CHECK(st_rip3_templates_create(NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_templates_set(NULL,0,NULL,0)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_templates_set(slots,36,NULL,0)==IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_templates_set(slots,0,NULL,1)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_templates_set(slots,0,source,SIZE_MAX)==IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_templates_required(slots,36,true)==IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_templates_required(NULL,0,true)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_templates_resolve(slots,36,&view)==IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_templates_resolve(slots,0,NULL)==IO_ERROR_INVALID_ARGUMENT);
	length=99;missing=99;memset(result,0x5a,sizeof(result));
	CHECK(st_rip3_templates_chain(slots,levels,37,NULL,0,result,sizeof(result),&length,&missing)==IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_templates_chain(slots,levels,0,NULL,0,result,sizeof(result),&length,&missing)==IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_templates_chain(slots,(const uint8_t[]){3,36},2,NULL,0,result,sizeof(result),&length,&missing)==IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_templates_chain(slots,levels,1,NULL,1,result,sizeof(result),&length,&missing)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_templates_chain(slots,levels,1,NULL,0,result,4095,&length,&missing)==IO_ERROR_OUT_OF_RANGE);
	CHECK(length==99 && missing==99);
	for (size_t i=0; i<sizeof(result); i++) CHECK(result[i]==0x5a);
	st_rip3_templates_destroy(slots);st_rip3_templates_destroy(NULL);
	return 0;
}
