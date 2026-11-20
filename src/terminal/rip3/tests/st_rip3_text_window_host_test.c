/* Independent selector lists, callback ordering and nonfatal host recovery. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_internal.h"
#include "st_rip3_text_window.h"
#include "io_error.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;} } while (0)

struct fixture {
	unsigned current,current_calls,operations,numbers[36],errors,replies;
	size_t count;
	bool active;
	int current_status,operation_status;
	enum st_rip3_host_diagnostic kind;
};

static int
current(void *context, unsigned *number)
{
	struct fixture *f=context;f->current_calls++;*number=f->current;return f->current_status;
}

static int
activate(void *context, const unsigned *numbers, size_t count, bool active)
{
	struct fixture *f=context;f->operations++;f->count=count;f->active=active;
	if (count>36) return IO_ERROR_OUT_OF_RANGE;
	memcpy(f->numbers,numbers,count*sizeof(*numbers));return f->operation_status;
}

static void
diagnostic(void *context, enum st_rip3_host_diagnostic kind)
{
	struct fixture *f=context;f->errors++;f->kind=kind;
}

static void
reply(void *context, const uint8_t *bytes, size_t length)
{
	struct fixture *f=context;(void)bytes;(void)length;f->replies++;
}

static int
run(const struct st_rip3_host_variables *host, struct fixture *f, const char *source, int error)
{
	unsigned errors=f->errors;const struct st_rip3_host_sink sink={f,reply,diagnostic};
	st_rip3_host_variables_process(host,&sink,(const uint8_t *)source,strlen(source));
	CHECK(f->replies==0);
	if (error<0) CHECK(f->errors==errors);
	else {
		if (f->errors!=errors+1 || (int)f->kind!=error)
			fprintf(stderr,"%s: errors=%u/%u kind=%d/%d\n",source,f->errors,errors+1,(int)f->kind,error);
		CHECK(f->errors==errors+1 && (int)f->kind==error);
	}
	return 0;
}

int
main(void)
{
	struct fixture f={.current=7};
	struct st_rip3_text_window_operations operations={&f,current,activate};
	struct st_rip3_host_variables host={.text_windows=&operations};
	CHECK(run(&host,&f,"$DTW$",-1)==0 && f.operations==1 && f.current_calls==1
	    && !f.active && f.count==1 && f.numbers[0]==7);
	CHECK(run(&host,&f,"$ATW(35,0,35,2)$",-1)==0 && f.operations==2 && f.current_calls==1
	    && f.active && f.count==3 && f.numbers[0]==0 && f.numbers[1]==2 && f.numbers[2]==35);
	CHECK(run(&host,&f,"$dtw( cur, ALL,7,CUR )$",-1)==0 && f.operations==3 && f.current_calls==2
	    && !f.active && f.count==36);
	for (unsigned i=0;i<36;i++) CHECK(f.numbers[i]==i);
	const char *const bad[]={"$ATW(CUR,36)$","$DTW(ALL,NO)$","$DTW(CUR, ,0)$","$ATW(-1)$","$DTW(0,3A)$"};
	for (size_t i=0;i<sizeof(bad)/sizeof(*bad);i++)
		CHECK(run(&host,&f,bad[i],ST_RIP3_HOST_INVALID_REFERENCE)==0 && f.operations==3 && f.current_calls==2);
	struct st_rip3_builtin_reference parsed;
	CHECK(st_rip3_builtin_reference_parse(&parsed,(const uint8_t *)"$DTW$",5)==0);
	char output[4],saved[4];memset(output,'Q',sizeof(output));memcpy(saved,output,sizeof(saved));
	CHECK(st_rip3_host_variables_execute(&host,&parsed,output,0)==IO_ERROR_OUT_OF_RANGE
	    && f.operations==3 && f.current_calls==2 && memcmp(output,saved,sizeof(output))==0);
	f.current_status=IO_ERROR_IO;
	CHECK(run(&host,&f,"$ATW()$",ST_RIP3_HOST_OPERATION_FAILED)==0 && f.operations==3 && f.current_calls==3);
	f.current_status=0;f.current=36;
	CHECK(run(&host,&f,"$ATW$",ST_RIP3_HOST_INVALID_REFERENCE)==0 && f.operations==3 && f.current_calls==4);
	f.current=7;f.operation_status=IO_ERROR_OUT_OF_MEMORY;
	CHECK(st_rip3_host_variables_execute(&host,&parsed,output,sizeof(output))==IO_ERROR_OUT_OF_MEMORY
	    && f.operations==4 && memcmp(output,saved,sizeof(output))==0);
	CHECK(run(&host,&f,"$ATW(0)$",ST_RIP3_HOST_RESOURCE_FAILURE)==0 && f.operations==5);
	f.operation_status=0;
	CHECK(run(&host,&f,"$ATW(CUR)$",-1)==0 && f.operations==6 && f.active && f.numbers[0]==7);
	operations.current=NULL;
	CHECK(run(&host,&f,"$ATW(0)$",-1)==0 && f.operations==7 && f.numbers[0]==0);
	CHECK(run(&host,&f,"$DTW$",ST_RIP3_HOST_UNSUPPORTED_REFERENCE)==0 && f.operations==7);
	operations.current=current;operations.activate=NULL;
	CHECK(run(&host,&f,"$DTW$",ST_RIP3_HOST_UNSUPPORTED_REFERENCE)==0 && f.operations==7);
	operations.activate=activate;
	char repeated[128]="$ATW(";size_t at=5;
	for (unsigned i=0;i<40;i++) {repeated[at++]='0';repeated[at++]=i==39 ? ')' : ',';}
	repeated[at++]='$';repeated[at]=0;
	CHECK(run(&host,&f,repeated,-1)==0 && f.operations==8 && f.count==1 && f.numbers[0]==0);
	/* Empty comma fields occupy no slot in the adopted argument grammar. */
	CHECK(run(&host,&f,"$DTW(CUR,,0)$",-1)==0 && f.operations==9 && !f.active
	    && f.count==2 && f.numbers[0]==0 && f.numbers[1]==7);
	host.text_windows=NULL;
	CHECK(run(&host,&f,"$ATW$",ST_RIP3_HOST_UNSUPPORTED_REFERENCE)==0 && f.operations==9);
	return 0;
}
