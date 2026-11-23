/* Independent local-state query replies, byte widths and failure boundaries. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_internal.h"
#include "st_rip3_local_variables.h"
#include "st_rip3_environments.h"
#include "st_rip3_mouse.h"
#include "st_rip3_condition_internal.h"
#include "io_error.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1; } } while (0)

struct modifiers {bool shift,ctrl;unsigned calls;int status;};
static int
modifiers(void *context, unsigned *down)
{
	struct modifiers *state=context;state->calls++;
	*down=(state->shift ? ST_RIP3_MODIFIER_SHIFT : 0) | (state->ctrl ? ST_RIP3_MODIFIER_CTRL : 0);
	return state->status;
}

struct observation {unsigned replies,errors;enum st_rip3_host_diagnostic kind;char text[64];};
static void
reply(void *context, const uint8_t *bytes, size_t length)
{
	struct observation *seen=context;seen->replies++;
	if (length>=sizeof(seen->text)) {seen->errors++;return;}
	memcpy(seen->text,bytes,length);seen->text[length]=0;
}
static void
diagnostic(void *context, enum st_rip3_host_diagnostic kind)
{
	struct observation *seen=context;seen->errors++;seen->kind=kind;
}

static int
query(const struct st_rip3_host_variables *host, const char *source, const char *expected, int error)
{
	struct observation seen={0};const struct st_rip3_host_sink sink={&seen,reply,diagnostic};
	st_rip3_host_variables_process(host,&sink,(const uint8_t *)source,strlen(source));
	if (error<0) {
		if (seen.replies!=1 || seen.errors!=0 || strcmp(seen.text,expected)!=0)
			fprintf(stderr,"%s got [%s] expected [%s], replies %u errors %u\n",source,seen.text,expected,seen.replies,seen.errors);
		CHECK(seen.replies==1 && seen.errors==0 && strcmp(seen.text,expected)==0);
	}
	else CHECK(seen.replies==0 && seen.errors==1 && (int)seen.kind==error);
	return 0;
}

static bool
doorway_mode(void *context)
{
	return *(const bool *)context;
}

static void
set_doorway_mode(void *context, bool enabled)
{
	*(bool *)context = enabled;
}

static int
options(void)
{
	bool enabled = false;
	struct st_rip3_local_queries local = {
		.context = &enabled, .doorway_mode = doorway_mode, .set_doorway_mode = set_doorway_mode,
	};
	struct st_rip3_host_variables host = {.local = &local};
	CHECK(query(&host, "$OPTION(LIST)$", "DOORWAY", -1) == 0);
	CHECK(query(&host, "$OPTION(DOORWAY)$", "0", -1) == 0);
	struct st_rip3_builtin_reference option;
	const char on[] = "$OPTION(DOORWAY,ON)$";
	CHECK(st_rip3_builtin_reference_parse(&option, (const uint8_t *)on, sizeof(on) - 1) == 0);
	char result[2] = "Q";
	CHECK(st_rip3_host_variables_execute(&host, &option, result, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(!enabled && strcmp(result, "Q") == 0);
	CHECK(st_rip3_host_variables_execute(&host, &option, result, sizeof(result)) == 0);
	CHECK(enabled && result[0] == '\0');
	CHECK(query(&host, "$OPTION(doorway, QUERY)$", "1", -1) == 0);
	CHECK(query(&host, "$DWAYOFF(1)$", NULL, ST_RIP3_HOST_INVALID_REFERENCE) == 0 && enabled);
	host.local = NULL;
	const char list[] = "$OPTION(LIST)$";
	CHECK(st_rip3_builtin_reference_parse(&option, (const uint8_t *)list, sizeof(list) - 1) == 0);
	CHECK(st_rip3_host_variables_execute(&host, &option, result, sizeof(result)) == 0 && result[0] == '\0');
	CHECK(query(&host, "$OPTION(DOORWAY)$", NULL, ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
	const struct st_rip3_mouse_sink sink = {0};
	CHECK(st_rip3_mouse_create(&host.mouse, &sink) == 0);
	st_rip3_mouse_set_tab(host.mouse, false);
	CHECK(query(&host, "$OPTION(LIST)$", "HOTKEY,TAB", -1) == 0);
	st_rip3_mouse_destroy(host.mouse);
	return 0;
}

static int
devices(void)
{
	struct modifiers state={0};bool mouse=true;unsigned colors=256;
	struct st_rip3_local_queries local={.context=&state,.modifiers=modifiers,.mouse_installed=&mouse,.colors=&colors};
	struct st_rip3_host_variables host={.local=&local};
	for (unsigned bits=0; bits<4; bits++) {
		state.shift=(bits&1)!=0;state.ctrl=(bits&2)!=0;
		CHECK(query(&host,"$sHiFt$",state.shift ? "1" : "0",-1)==0);
		CHECK(query(&host,"$CTRL()$",state.ctrl ? "1" : "0",-1)==0);
	}
	CHECK(state.calls==8 && query(&host,"$MSTAT$","YES",-1)==0);mouse=false;
	CHECK(query(&host,"$MSTAT()$","NO",-1)==0);
	const unsigned counts[]={2,16,256,32768,65536,16777216,17};
	const char *const values[]={"2","16","256","32768","65536","16777216","17"};
	for (size_t i=0; i<sizeof(counts)/sizeof(counts[0]); i++) {colors=counts[i];CHECK(query(&host,"$COLORS$",values[i],-1)==0);}
	const char *const invalid[]={"$SHIFT(1)$","$CTRL(NO)$","$MSTAT(0)$","$COLORS(DEVICE)$","$XY(OTHER)$","$XY(WORLD,DEVICE)$"};
	for (size_t i=0; i<sizeof(invalid)/sizeof(invalid[0]); i++) CHECK(query(&host,invalid[i],NULL,ST_RIP3_HOST_INVALID_REFERENCE)==0);
	CHECK(state.calls==8);
	struct st_rip3_builtin_reference parsed;char output[32],before[32];memset(output,'Q',sizeof(output));memcpy(before,output,sizeof(output));
	CHECK(st_rip3_builtin_reference_parse(&parsed,(const uint8_t *)"$SHIFT$",7)==0);
	CHECK(st_rip3_host_variables_execute(&host,&parsed,output,1)==IO_ERROR_OUT_OF_RANGE && state.calls==8 && memcmp(output,before,sizeof(output))==0);
	state.status=IO_ERROR_IO;
	CHECK(st_rip3_host_variables_execute(&host,&parsed,output,sizeof(output))==IO_ERROR_IO && memcmp(output,before,sizeof(output))==0);
	CHECK(query(&host,"$SHIFT$",NULL,ST_RIP3_HOST_OPERATION_FAILED)==0);
	state.status=0;CHECK(query(&host,"$SHIFT$","1",-1)==0);
	local.modifiers=NULL;CHECK(query(&host,"$CTRL$",NULL,ST_RIP3_HOST_UNSUPPORTED_REFERENCE)==0);
	local.mouse_installed=NULL;CHECK(query(&host,"$MSTAT$",NULL,ST_RIP3_HOST_UNSUPPORTED_REFERENCE)==0);
	local.colors=NULL;CHECK(query(&host,"$COLORS$",NULL,ST_RIP3_HOST_UNSUPPORTED_REFERENCE)==0);
	host.local=NULL;CHECK(query(&host,"$SHIFT$",NULL,ST_RIP3_HOST_UNSUPPORTED_REFERENCE)==0);
	CHECK(query(&host,"$COLORS(1)$",NULL,ST_RIP3_HOST_INVALID_REFERENCE)==0);
	return 0;
}

struct condition_probe {const struct st_rip3_host_variables *host;struct observation seen;};
static int
condition_reference(void *context, const struct st_rip3_condition_bytes *reference,
    struct st_rip3_condition_bytes *result)
{
	struct condition_probe *probe=context;
	uint8_t source[258];CHECK(reference->length>=3 && reference->length<=sizeof(source));
	source[0]='$';memcpy(source+1,reference->bytes+2,reference->length-2);
	probe->seen=(struct observation){0};const struct st_rip3_host_sink sink={&probe->seen,reply,diagnostic};
	st_rip3_host_variables_process(probe->host,&sink,source,reference->length-1);
	if (probe->seen.errors!=0) return IO_ERROR_IO;
	*result=(struct st_rip3_condition_bytes){(const uint8_t *)probe->seen.text,strlen(probe->seen.text)};return 0;
}

static int
conditions(void)
{
	struct modifiers keys={.shift=true};unsigned colors=256;
	const struct st_rip3_local_queries local={.context=&keys,.modifiers=modifiers,.colors=&colors};
	const struct st_rip3_host_variables host={.local=&local};
	struct condition_probe probe={.host=&host};
	const struct st_rip3_condition_provider provider={.context=&probe,.reference=condition_reference};
	const char *source="SHIFT=1 OR CTRL=1 AND COLORS=256>>";struct st_rip3_condition_program *program=NULL;
	CHECK(st_rip3_condition_program_parse(&program,(const uint8_t *)source,strlen(source),NULL)==0);
	bool result=false;CHECK(st_rip3_condition_program_evaluate(&result,program,&provider)==0 && result && keys.calls==2);
	colors=16;CHECK(st_rip3_condition_program_evaluate(&result,program,&provider)==0 && !result && keys.calls==4);
	st_rip3_condition_program_destroy(program);program=NULL;
	source="SHIFT!=0>>";CHECK(st_rip3_condition_program_parse(&program,(const uint8_t *)source,strlen(source),NULL)==0);
	keys.status=IO_ERROR_IO;result=true;
	CHECK(st_rip3_condition_program_evaluate(&result,program,&provider)==0 && !result && keys.calls==5);
	st_rip3_condition_program_destroy(program);return 0;
}

static int
positions(enum st_rip3_canvas canvas)
{
	struct st_rip3_frame current;CHECK(st_rip3_frame_init(&current,canvas)==0
	    && st_rip3_frame_world(&current,current.device_width*2,current.device_height*2)==0);
	struct st_rip3_point capture={297,321};
	struct st_rip3_local_queries local={.mouse_position=&capture};
	struct st_rip3_host_variables host={.local=&local,.frame=&current};
	CHECK(query(&host,"$XY(DEVICE)$","0297:0321",-1)==0 && query(&host,"$XY$","0594:0642",-1)==0);
	CHECK(query(&host,"$XY(, world ,,)$","0594:0642",-1)==0);
	struct st_rip3_query_cells window={.origin={0,0},.cell_width=8,.cell_height=16,.columns=80,.rows=25};
	local.text_window_query=true;local.text_window=&window;
	CHECK(query(&host,"$XY$","37:20",-1)==0 && query(&host,"$XY(WORLD)$","0594:0642",-1)==0
	    && query(&host,"$XY(device)$","0297:0321",-1)==0);
	capture=(struct st_rip3_point){-1,-1};CHECK(query(&host,"$XY$","00:00",-1)==0);
	capture=(struct st_rip3_point){640,400};CHECK(query(&host,"$XY$","79:24",-1)==0);
	window.origin=(struct st_rip3_point){100,200};capture=(struct st_rip3_point){108,216};
	CHECK(query(&host,"$XY$","01:01",-1)==0);
	capture=(struct st_rip3_point){99,199};CHECK(query(&host,"$XY$","00:00",-1)==0);
	window.cell_width=1;window.cell_height=1;window.origin=(struct st_rip3_point){INT32_MIN,INT32_MIN};
	window.columns=window.rows=UINT_MAX;capture=(struct st_rip3_point){INT32_MAX,INT32_MAX};
	CHECK(query(&host,"$XY$","-2:-2",-1)==0); /* Clamped UINT_MAX-1, then low signed WORD. */
	local.text_window=NULL;CHECK(query(&host,"$XY$",NULL,ST_RIP3_HOST_UNSUPPORTED_REFERENCE)==0);
	CHECK(query(&host,"$XY(DEVICE)$","-001:-001",-1)==0); /* No text grid required. */
	local.text_window=&window;window.rows=0;
	CHECK(query(&host,"$XY$",NULL,ST_RIP3_HOST_OPERATION_FAILED)==0);
	local.text_window_query=false;
	capture=(struct st_rip3_point){32768,65535};CHECK(query(&host,"$XY(DEVICE)$","-32768:-001",-1)==0);
	capture=(struct st_rip3_point){70000,-70000};CHECK(query(&host,"$XY(DEVICE)$","4464:-4464",-1)==0);
	capture=(struct st_rip3_point){1,-1};
	CHECK(st_rip3_frame_world(&current,current.device_width/2,current.device_height/2)==0);
	CHECK(query(&host,"$XY$","0000:0000",-1)==0); /* Signed truncation, not negative floor. */
	/* Current ENV overrides the fallback frame without needing a PORT binding. */
	struct st_rip3_environments *env=NULL;struct st_rip3_environment value;struct st_rip3_table_host tables={0};
	CHECK(st_rip3_environments_create(&env,canvas)==0 && st_rip3_environments_get(env,0,&value)==0);
	value.world_width=current.device_width*2;value.world_height=current.device_height*2;
	CHECK(st_rip3_environments_define(env,2,&value)==0 && st_rip3_environments_select(env,2)==0 && st_rip3_environments_protect(env,2,true)==0);
	tables.tables[ST_RIP3_TABLE_ENV].live=env;host.tables=&tables;capture=(struct st_rip3_point){297,321};
	CHECK(query(&host,"$XY$","0594:0642",-1)==0);
	host.frame=NULL;CHECK(query(&host,"$XY$","0594:0642",-1)==0);
	tables.tables[ST_RIP3_TABLE_ENV].live=NULL;
	CHECK(query(&host,"$XY$",NULL,ST_RIP3_HOST_UNSUPPORTED_REFERENCE)==0 && query(&host,"$XY(DEVICE)$","0297:0321",-1)==0);
	local.mouse_position=NULL;CHECK(query(&host,"$XY(DEVICE)$",NULL,ST_RIP3_HOST_UNSUPPORTED_REFERENCE)==0);
	st_rip3_environments_destroy(env);return 0;
}

int
main(void)
{
	CHECK(devices()==0 && conditions()==0 && options()==0);
	for (unsigned canvas=0; canvas<3; canvas++) CHECK(positions((enum st_rip3_canvas)canvas)==0);
	return 0;
}
