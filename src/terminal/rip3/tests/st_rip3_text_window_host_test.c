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
	unsigned moves,cursors,erases,eol_erases;
	unsigned x[36],y[36];
	bool defined[36],enabled[36],visible[36];
	unsigned observations;
	struct st_rip3_text_window_snapshot observation;
};

static int
current(void *context, unsigned *number)
{
	struct fixture *f=context;f->current_calls++;*number=f->current;return f->current_status;
}

static int
info(void *context, unsigned number, struct st_rip3_text_window_info *result)
{
	struct fixture *f=context;
	*result=(struct st_rip3_text_window_info){.defined=f->defined[number],.active=f->enabled[number],
	    .extended=f->observation.layout.extended};return 0;
}

static int
snapshot(void *context, unsigned number, struct st_rip3_text_window_snapshot *result)
{
	struct fixture *f=context;(void)number;f->observations++;
	if (f->operation_status!=0) return f->operation_status;
	*result=f->observation;return 0;
}

static int
query_value(const struct st_rip3_host_variables *host, const char *source, const char *expected)
{
	struct st_rip3_builtin_reference parsed;
	CHECK(st_rip3_builtin_reference_parse(&parsed,(const uint8_t *)source,strlen(source))==0);
	char output[64];memset(output,'Q',sizeof(output));
	int status=st_rip3_host_variables_execute(host,&parsed,output,sizeof(output));
	if (status!=0) fprintf(stderr,"%s: status %d\n",source,status);
	CHECK(status==0 && strcmp(output,expected)==0);return 0;
}

static int
queries(void)
{
	struct fixture f={.current=7};f.defined[7]=f.enabled[7]=true;
	f.observation=(struct st_rip3_text_window_snapshot){.column=3,.row=5,.cursor_visible=true,.font_known=true,
	    .layout={.extended=true,.font=4,.x=131,.y=66,.bound_x=100,.bound_y=50,.bound_width=320,.bound_height=200,
	        .columns=12,.rows=7,.cell_width=20,.cell_height=24}};
	struct st_rip3_text_window_operations ops={.context=&f,.current=current,.info=info,.snapshot=snapshot};
	struct st_rip3_frame frame={1000,500,640,350};
	struct st_rip3_host_variables host={.text_windows=&ops,.frame=&frame};
	const struct {const char *source,*expected;} cases[]={
	    {"$CURX$","3"},{"$CURY(7)$","5"},{"$CURSOR(CUR)$","YES"},{"$TWFONT$","5"},
	    {"$TWW$","12"},{"$TWH(CUR)$","7"},{"$TWW(7,TEXT,COLS)$","12"},{"$TWH(7,TEXT,ROWS)$","7"},
	    {"$TWW(7,BOUND,DEVICE)$","320"},{"$TWH(7,BOUND,DEVICE)$","200"},
	    {"$TWW(7,TEXT,DEVICE)$","240"},{"$TWH(7,TEXT,WORLD)$","117"},
	    {"$TWW(7,CELL)$","12"},{"$TWH(7,CELL,DEVICE)$","24"},
	    {"$TWX0(7,BOUND,DEVICE)$","100"},{"$TWY0(7,BOUND)$","35"},
	    {"$TWX1(7,TEXT,DEVICE)$","371"},{"$TWY1(7,TEXT,WORLD)$","163"},
	    {"$TWX0$","-1"},{"$TWY1(7)$","-1"},{"$CURX(35)$","0"},{"$TWW(35)$","0"},
	    {"$CURSOR(35)$","NO"},{"$TWX1(35,TEXT,DEVICE)$","-1"}};
	for (size_t i=0;i<sizeof(cases)/sizeof(*cases);i++) CHECK(query_value(&host,cases[i].source,cases[i].expected)==0);
	f.enabled[7]=false;unsigned observations=f.observations;
	CHECK(query_value(&host,"$CURX$","0")==0 && query_value(&host,"$TWFONT$","0")==0
	    && query_value(&host,"$CURSOR$","NO")==0 && query_value(&host,"$TWY0$","-1")==0
	    && f.observations==observations);
	f.enabled[7]=true;f.observation.layout.extended=false;f.observation.layout.x0=10;f.observation.layout.y0=5;
	f.observation.layout.x1=21;f.observation.layout.y1=11;
	CHECK(query_value(&host,"$TWX0$","10")==0 && query_value(&host,"$TWY0$","5")==0
	    && query_value(&host,"$TWX1$","21")==0 && query_value(&host,"$TWY1$","11")==0);
	f.observation.cursor_visible=false;CHECK(query_value(&host,"$CURSOR$","NO")==0);
	for (unsigned i=0;i<5;i++) {f.observation.layout.font=i;char expected[2]={(char)('1'+i),0};
		CHECK(query_value(&host,"$TWFONT$",expected)==0);}
	const char *const invalid[]={"$CURX(ALL)$","$CURY(36)$","$CURSOR(CUR,0)$","$TWFONT(0,1)$",
	    "$TWW(7,CELL,COLS)$","$TWH(7,TEXT,COLS)$","$TWX0(7,CELL)$","$TWX0(7,TEXT,COLS)$",
	    "$TWW(7,,WORLD)$","$TWW(7,TEXT,WORLD,DEVICE)$"};
	for (size_t i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
		struct st_rip3_builtin_reference parsed;
		CHECK(st_rip3_builtin_reference_parse(&parsed,(const uint8_t *)invalid[i],strlen(invalid[i]))==0);
		char output[32];memset(output,'Q',sizeof(output));unsigned calls=f.current_calls,reads=f.observations;
		CHECK(st_rip3_host_variables_execute(&host,&parsed,output,sizeof(output))==IO_ERROR_INVALID_FORMAT
		    && output[0]=='Q' && f.current_calls==calls && f.observations==reads);
	}
	struct st_rip3_builtin_reference parsed;
	const char *const unresolved[]={"$TWX1(CUR,TEXT)$","$TWY1(7,TEXT)$","$TWX1(7,TEXT,COLS)$"};
	for (size_t i=0;i<sizeof(unresolved)/sizeof(*unresolved);i++) {
		CHECK(st_rip3_builtin_reference_parse(&parsed,(const uint8_t *)unresolved[i],strlen(unresolved[i]))==0);
		char output[8]="KEEP";unsigned calls=f.current_calls,reads=f.observations;
		CHECK(st_rip3_host_variables_execute(&host,&parsed,output,sizeof(output))==IO_ERROR_NOT_SUPPORTED
		    && strcmp(output,"KEEP")==0 && f.current_calls==calls && f.observations==reads);
	}
	CHECK(st_rip3_builtin_reference_parse(&parsed,(const uint8_t *)"$TWFONT$",8)==0);
	f.observation.font_known=false;char output[32]="KEEP";
	CHECK(st_rip3_host_variables_execute(&host,&parsed,output,sizeof(output))==IO_ERROR_NOT_SUPPORTED && strcmp(output,"KEEP")==0);
	f.observation.font_known=true;
	CHECK(st_rip3_host_variables_execute(&host,&parsed,output,1)==IO_ERROR_OUT_OF_RANGE && strcmp(output,"KEEP")==0);
	f.observation.layout.bound_x=UINT64_C(2305843009213693952);frame.device_width=INT32_MAX;frame.world_width=65535;
	CHECK(query_value(&host,"$TWX0(7,BOUND,WORLD)$","70367670468607")==0);
	f.observation.layout.bound_x=UINT64_MAX;
	CHECK(st_rip3_builtin_reference_parse(&parsed,(const uint8_t *)"$TWX1(7,BOUND,DEVICE)$",sizeof("$TWX1(7,BOUND,DEVICE)$")-1)==0);
	CHECK(st_rip3_host_variables_execute(&host,&parsed,output,sizeof(output))==IO_ERROR_OUT_OF_RANGE && strcmp(output,"KEEP")==0);
	return 0;
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
erase(void *context, const unsigned *numbers, size_t count)
{
	struct fixture *f=context;f->erases++;
	if (f->operation_status!=0) return f->operation_status;
	for (size_t i=0;i<count;i++) if (f->defined[numbers[i]] && f->enabled[numbers[i]])
		f->x[numbers[i]]=f->y[numbers[i]]=0;
	return 0;
}

static int
erase_eol(void *context, const unsigned *numbers, size_t count)
{
	struct fixture *f=context;f->eol_erases++;f->count=count;
	memcpy(f->numbers,numbers,count*sizeof(*numbers));return f->operation_status;
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
	CHECK(queries()==0);
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
	operations.erase=erase;f.x[0]=10;f.y[0]=20;
	CHECK(run(&host,&f,"$ETW$",-1)==0 && f.erases==1 && f.x[0]==0 && f.y[0]==0);
	f.x[0]=10;f.y[0]=20;f.x[35]=11;f.y[35]=21;
	CHECK(run(&host,&f,"$etw( all,cur,0,35,35 )$",-1)==0 && f.erases==2
	    && f.x[0]==0 && f.y[0]==0 && f.x[35]==0 && f.y[35]==0 && f.x[7]==8 && f.y[7]==9);
	lookups=f.current_calls;
	CHECK(run(&host,&f,"$ETW(CUR,36)$",ST_RIP3_HOST_INVALID_REFERENCE)==0 && f.erases==2 && f.current_calls==lookups);
	CHECK(run(&host,&f,"$ETW(1,7)$",-1)==0 && f.erases==3 && f.x[7]==8 && f.y[7]==9);
	operations.erase=NULL;
	CHECK(run(&host,&f,"$ETW$",ST_RIP3_HOST_UNSUPPORTED_REFERENCE)==0 && f.erases==3);
	operations.erase=erase;f.operation_status=IO_ERROR_OUT_OF_MEMORY;
	CHECK(run(&host,&f,"$ETW$",ST_RIP3_HOST_RESOURCE_FAILURE)==0 && f.erases==4);
	f.operation_status=0;
	CHECK(run(&host,&f,"$ETW(CUR)$",-1)==0 && f.erases==5);
	operations.erase_eol=erase_eol;
	CHECK(run(&host,&f,"$TWERASEEOL$",-1)==0 && f.eol_erases==1 && f.count==1 && f.numbers[0]==0);
	CHECK(run(&host,&f,"$tweraseeol( all,cur,35,35 )$",-1)==0 && f.eol_erases==2 && f.count==36);
	for (unsigned i=0;i<36;i++) CHECK(f.numbers[i]==i);
	lookups=f.current_calls;
	const char *const bad_eol[]={"$TWERASEEOL(CUR,36)$","$TWERASEEOL(ALL,NO)$",
	    "$TWERASEEOL(-1)$","$TWERASEEOL(0,3A)$","$TWERASEEOL(CUR, ,0)$"};
	for (size_t i=0;i<sizeof(bad_eol)/sizeof(*bad_eol);i++)
		CHECK(run(&host,&f,bad_eol[i],ST_RIP3_HOST_INVALID_REFERENCE)==0
		    && f.eol_erases==2 && f.current_calls==lookups);
	operations.erase_eol=NULL;
	CHECK(run(&host,&f,"$TWERASEEOL$",ST_RIP3_HOST_UNSUPPORTED_REFERENCE)==0 && f.eol_erases==2);
	operations.erase_eol=erase_eol;f.operation_status=IO_ERROR_OUT_OF_MEMORY;
	CHECK(run(&host,&f,"$TWERASEEOL$",ST_RIP3_HOST_RESOURCE_FAILURE)==0 && f.eol_erases==3);
	f.operation_status=0;
	CHECK(run(&host,&f,"$TWERASEEOL(35,0,35)$",-1)==0 && f.eol_erases==4 && f.count==2
	    && f.numbers[0]==0 && f.numbers[1]==35 && f.erases==5);
	return 0;
}
