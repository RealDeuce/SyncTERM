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
	unsigned moves,cursors;
	unsigned x[36],y[36];
	bool defined[36],enabled[36],visible[36];
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

static int
position(void *context, unsigned number, const struct st_rip3_text_cursor_position *position)
{
	struct fixture *f=context;f->moves++;
	if (f->operation_status!=0) return f->operation_status;
	if (f->defined[number] && f->enabled[number]) {
		if (position->set_column) f->x[number]=position->column>299 ? 299 : (unsigned)position->column;
		if (position->set_row) f->y[number]=position->row>99 ? 99 : (unsigned)position->row;
	}
	return 0;
}

static int
cursor(void *context, const unsigned *numbers, size_t count, bool visible)
{
	struct fixture *f=context;f->cursors++;
	if (f->operation_status!=0) return f->operation_status;
	for (size_t i=0;i<count;i++)
		if (f->defined[numbers[i]] && f->enabled[numbers[i]]) f->visible[numbers[i]]=visible;
	return 0;
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
	struct st_rip3_text_window_operations operations={.context=&f,.current=current,.activate=activate};
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
	/* Coordinates are decimal, target-relative and wider than old nominal
	 * grids; CUR axes retain that target rather than the current slot. */
	host.text_windows=&operations;operations.position=position;operations.cursor=cursor;
	f.current=0;f.defined[0]=f.defined[7]=f.defined[35]=true;
	f.enabled[0]=f.enabled[7]=f.enabled[35]=true;f.x[0]=12;f.y[0]=13;f.x[7]=21;f.y[7]=22;
	CHECK(run(&host,&f,"$TWGOTO(7,CUR,2)$",-1)==0 && f.moves==1 && f.x[7]==21 && f.y[7]==2 && f.x[0]==12 && f.y[0]==13);
	CHECK(run(&host,&f,"$twgoto(7, 250, cur )$",-1)==0 && f.moves==2 && f.x[7]==250 && f.y[7]==2);
	CHECK(run(&host,&f,"$TWGOTO(7,3)$",-1)==0 && f.moves==3 && f.x[7]==3 && f.y[7]==2);
	CHECK(run(&host,&f,"$TWGOTO(7,99999999999999999999,65536)$",-1)==0 && f.moves==4 && f.x[7]==299 && f.y[7]==99);
	CHECK(run(&host,&f,"$TWGOTO(CUR,0,0)$",-1)==0 && f.moves==5 && f.x[0]==0 && f.y[0]==0);
	unsigned lookups=f.current_calls;
	CHECK(run(&host,&f,"$TWGOTO$",-1)==0 && run(&host,&f,"$TWGOTO(CUR)$",-1)==0 && f.moves==5 && f.current_calls==lookups);
	CHECK(run(&host,&f,"$TWGOTO(7,CUR,CUR)$",-1)==0 && f.moves==6 && f.x[7]==299 && f.y[7]==99);
	CHECK(run(&host,&f,"$TWHOME(7)$",-1)==0 && f.moves==7 && f.x[7]==0 && f.y[7]==0);
	f.x[0]=12;f.y[0]=13;
	CHECK(run(&host,&f,"$TWHOME$",-1)==0 && f.moves==8 && f.x[0]==0 && f.y[0]==0);
	f.enabled[7]=false;f.x[7]=8;f.y[7]=9;
	CHECK(run(&host,&f,"$TWHOME(7)$",-1)==0 && f.moves==9 && f.x[7]==8 && f.y[7]==9);
	CHECK(run(&host,&f,"$TWGOTO(1,2,3)$",-1)==0 && f.moves==10 && f.x[1]==0 && f.y[1]==0);
	lookups=f.current_calls;
	const char *const invalid[]={"$TWGOTO(CUR,3,NO)$","$TWGOTO(7,-1)$","$TWGOTO(7,+1)$",
	    "$TWGOTO(7,1,2,3)$","$TWGOTO(ALL,1)$","$TWGOTO(36)$","$TWHOME(ALL)$","$TWHOME(0,1)$",
	    "$CON(CUR,36)$","$COFF(ALL,NO)$","$TWGOTO(7,9999999999999999999x)$"};
	for (size_t i=0;i<sizeof(invalid)/sizeof(*invalid);i++)
		CHECK(run(&host,&f,invalid[i],ST_RIP3_HOST_INVALID_REFERENCE)==0 && f.moves==10 && f.cursors==0 && f.current_calls==lookups);
	CHECK(run(&host,&f,"$CON$",-1)==0 && f.cursors==1 && f.visible[0] && !f.visible[7]);
	CHECK(run(&host,&f,"$COFF$",-1)==0 && f.cursors==2 && !f.visible[0]);
	CHECK(run(&host,&f,"$CON(ALL)$",-1)==0 && f.cursors==3 && f.visible[0] && !f.visible[7] && f.visible[35]);
	f.visible[7]=true;
	CHECK(run(&host,&f,"$coff( cur,ALL,35,35 )$",-1)==0 && f.cursors==4 && !f.visible[0] && f.visible[7] && !f.visible[35]);
	operations.position=NULL;
	CHECK(run(&host,&f,"$TWHOME$",ST_RIP3_HOST_UNSUPPORTED_REFERENCE)==0 && f.moves==10);
	operations.position=position;operations.cursor=NULL;
	CHECK(run(&host,&f,"$COFF$",ST_RIP3_HOST_UNSUPPORTED_REFERENCE)==0 && f.cursors==4);
	operations.cursor=cursor;f.operation_status=IO_ERROR_IO;
	CHECK(run(&host,&f,"$TWGOTO(0,1)$",ST_RIP3_HOST_OPERATION_FAILED)==0 && f.moves==11 && f.x[0]==0);
	CHECK(run(&host,&f,"$CON$",ST_RIP3_HOST_OPERATION_FAILED)==0 && f.cursors==5 && !f.visible[0]);
	f.operation_status=0;
	CHECK(run(&host,&f,"$CON$",-1)==0 && f.cursors==6 && f.visible[0]);
	return 0;
}
