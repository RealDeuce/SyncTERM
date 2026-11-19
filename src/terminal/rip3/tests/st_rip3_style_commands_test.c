/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_backup.h"
#include "st_rip3_table_host.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_text_commands.h"
#include "io_error.h"
#include "st_rip3_environments.h"
#include "st_rip3_styles.h"
#include "st_rip3_ports.h"
#include "st_rip3_palettes.h"
#include "st_rip3_stream.h"
#include "st_pixels.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)


struct observation { unsigned errors; enum st_rip3_command_diagnostic kind; };

static void
diagnostic(void *context, enum st_rip3_command_diagnostic kind)
{
	struct observation *seen = context;
	seen->errors++; seen->kind = kind;
}

static int
process_count(const struct st_rip3_command_host *host, const char *command, int error, unsigned count)
{
	struct observation seen = {0};
	const struct st_rip3_command_sink sink = {&seen,diagnostic};
	st_rip3_commands_process(host, &sink, (const uint8_t *)command, strlen(command));
	if (seen.errors != count || (error >= 0 && (int)seen.kind != error))
		fprintf(stderr, "%s: errors %u, kind %d, expected %d\n", command, seen.errors, (int)seen.kind, error);
	CHECK(seen.errors == count);
	if (error >= 0) CHECK((int)seen.kind == error);
	return 0;
}

static int
process(const struct st_rip3_command_host *host, const char *command, int error)
{
	return process_count(host,command,error,error < 0 ? 0 : 1);
}

static int
create(struct st_rip3_command_host *host, enum st_rip3_canvas canvas, uint32_t world_width, uint32_t world_height)
{
	struct st_rip3_style initial = {.drawing_color=12,.background_color=1,.fill_color=7,
	    .pen={.pattern=0xffff,.thickness=1,.draw_background=true},
	    .font={0,0,1,0},.write_mode=ST_RIP3_COPY,.borders=true};
	struct st_rip3_environment environment;
	uint32_t colors[256];
	CHECK(st_rip3_fill_solid(&initial.fill, true) == 0);
	memcpy(initial.fill.rows,(const uint8_t[]){170,85,170,85,170,85,170,85},8);
	CHECK(st_rip3_styles_create(&host->styles, &initial) == 0);
	CHECK(st_rip3_environments_create(&host->environments, canvas) == 0);
	CHECK(st_rip3_environments_get(host->environments, 0, &environment) == 0);
	environment.world_width = world_width; environment.world_height = world_height;
	CHECK(st_rip3_environments_set(host->environments, 0, &environment) == 0);
	CHECK(st_rip3_ports_create(&host->ports, canvas) == 0);
	CHECK(st_rip3_palettes_create(&host->palettes) == 0);
	for (unsigned i = 0; i < 256; i++) colors[i] = i*UINT32_C(0x010101);
	CHECK(st_rip3_palettes_rgb(host->palettes, 0, 8, colors, 256) == 0);
	return 0;
}

static void
destroy(struct st_rip3_command_host *host)
{
	st_rip3_styles_destroy(host->styles);
	st_rip3_environments_destroy(host->environments);
	st_rip3_ports_destroy(host->ports);
	st_rip3_palettes_destroy(host->palettes);
}

static void
number(char *result, size_t width, unsigned value, unsigned base)
{
	static const char digits[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#&";
	while (width!=0) {result[--width]=digits[value%base];value/=base;}
}

static bool
cursor(const struct st_rip3_command_host *host, int64_t x, int64_t y)
{
	unsigned number;struct st_rip3_port_info info;
	return st_rip3_ports_current(host->ports,&number)==0 && st_rip3_ports_info(host->ports,number,&info)==0
	    && info.cursor.x==x && info.cursor.y==y;
}

static bool
protection(bool previous, unsigned flags, unsigned set, unsigned clear)
{
	if ((flags&set)!=0) previous=true;
	if ((flags&clear)!=0) previous=false;
	return previous;
}

static int
matrix(void)
{
	for (unsigned base=36; base<=64; base+=28) for (unsigned xy=2; xy<=5; xy++)
		for (unsigned source=0; source<2; source++) for (unsigned destination=0; destination<3; destination++)
			for (unsigned flags=0; flags<16; flags++) {
				struct st_rip3_command_host host={0};
				struct st_rip3_environment env,after;
				struct st_rip3_style first,last,selected;
				struct st_rip3_style_info info;
				struct st_rip3_text_context text={.encoding=ST_RIP3_TEXT_UTF8};
				unsigned index;
				char command[6]="2Y200";
				CHECK(create(&host,ST_RIP3_CANVAS_640_480,640,480)==0);host.text=&text;
				const struct st_rip3_cursor point={33,66};CHECK(st_rip3_ports_cursor(host.ports,0,&point)==0);
				CHECK(st_rip3_environments_get(host.environments,0,&env)==0);
				env.base_math=base;env.coordinate_size=xy;env.graphics_style=7;env.port=9;env.palette=11;
				CHECK(st_rip3_environments_define(host.environments,1,&env)==0
				    && st_rip3_environments_select(host.environments,1)==0 && st_rip3_environments_protect(host.environments,1,true)==0);
				CHECK(st_rip3_styles_get(host.styles,0,&first)==0);
				first.font_mode=ST_RIP3_FONT_OUTLINE;first.outline.spacing=100;first.borders=false;
				CHECK(st_rip3_styles_define(host.styles,1,&first)==0 && st_rip3_styles_switch(host.styles,1)==0);
				CHECK(st_rip3_styles_protect(host.styles,1,source!=0)==0);
				last=first;last.drawing_color=5;last.fill_color=3;last.font_mode=ST_RIP3_FONT_CLASSIC;
				if (destination!=0) CHECK(st_rip3_styles_define(host.styles,2,&last)==0);
				if (destination==2) CHECK(st_rip3_styles_protect(host.styles,2,true)==0);
				/* High flag bits do nothing; low source/destination pairs act
				 * sequentially, and clear wins within each pair. */
				number(command+3,2,flags+256,base);
				CHECK(process(&host,command,-1)==0);
				CHECK(st_rip3_styles_current(host.styles,&index)==0 && index==2);
				CHECK(st_rip3_styles_info(host.styles,1,&info)==0 && info.defined
				    && info.protected==protection(source!=0,flags,4,8));
				CHECK(st_rip3_styles_info(host.styles,2,&info)==0 && info.defined
				    && info.protected==protection(destination==2,flags,1,2));
				CHECK(st_rip3_styles_get(host.styles,1,&selected)==0
				    && selected.drawing_color==first.drawing_color && selected.font_mode==ST_RIP3_FONT_OUTLINE && !selected.borders);
				CHECK(st_rip3_styles_get(host.styles,2,&selected)==0);
				CHECK(selected.drawing_color==(destination==0 ? 15 : 5)
				    && selected.fill_color==(destination==0 ? 15 : 3) && selected.font_mode==ST_RIP3_FONT_CLASSIC
				    && selected.borders==(destination==0));
				CHECK(st_rip3_environments_get(host.environments,1,&after)==0
				    && after.graphics_style==7 && after.port==9 && after.palette==11 && after.base_math==base && after.coordinate_size==xy);
				CHECK(st_rip3_ports_current(host.ports,&index)==0 && index==0);
				CHECK(cursor(&host,33,66) && text.encoding==ST_RIP3_TEXT_UTF8 && text.font==NULL);
				destroy(&host);
			}
	return 0;
}

struct reference_result { unsigned errors,replies; };
static void
reference_error(void *context, enum st_rip3_host_diagnostic kind)
{
	struct reference_result *result=context;(void)kind;result->errors++;
}
static void
reference_reply(void *context, const uint8_t *bytes, size_t length)
{
	struct reference_result *result=context;(void)bytes;(void)length;result->replies++;
}
static int
select_reference(struct st_rip3_command_host *host, const char *reference, bool invalid)
{
	struct st_rip3_table_host tables={0};
	struct st_rip3_host_variables variables={.tables=&tables};
	struct reference_result result={0};
	const struct st_rip3_host_sink sink={&result,reference_reply,reference_error};
	tables.tables[ST_RIP3_TABLE_STYLE].live=host->styles;
	st_rip3_host_variables_process(&variables,&sink,(const uint8_t *)reference,strlen(reference));
	CHECK(result.errors==(invalid ? 1u : 0u) && result.replies==0);
	return 0;
}

static int
boundaries(void)
{
	struct st_rip3_command_host host={0},missing;
	struct st_rip3_environment env;
	struct st_rip3_style style;
	struct st_rip3_style_info info;
	unsigned current;
	CHECK(create(&host,ST_RIP3_CANVAS_640_480,640,480)==0);
	const char *const invalid[]={"2Y","2Y1","2Y?00","2Y10?","2Y1000"};
	for (unsigned i=0; i<sizeof(invalid)/sizeof(invalid[0]); i++) {
		CHECK(process(&host,invalid[i],ST_RIP3_COMMAND_INVALID)==0);
		CHECK(st_rip3_styles_current(host.styles,&current)==0 && current==0);
		CHECK(st_rip3_styles_info(host.styles,1,&info)==0 && !info.defined && !info.protected);
	}
	CHECK(process(&host,"2Y100 comment",ST_RIP3_COMMAND_UNSUPPORTED)==0);
	CHECK(process(&host,"2YG0",-1)==0 && st_rip3_styles_current(host.styles,&current)==0 && current==16);
	/* Same-slot effects apply in source-then-destination order. */
	for (unsigned flags=0; flags<16; flags++) {
		char command[6]="2YG00";number(command+3,2,flags,36);
		CHECK(st_rip3_styles_protect(host.styles,16,false)==0 && process(&host,command,-1)==0);
		CHECK(st_rip3_styles_info(host.styles,16,&info)==0 && info.protected==protection(protection(false,flags,4,8),flags,1,2));
	}
	/* The source is slot 16 here: only the destination-zero request diagnoses. */
	CHECK(process(&host,"2Y00F",ST_RIP3_COMMAND_OPERATION_FAILED)==0
	    && st_rip3_styles_info(host.styles,0,&info)==0 && !info.protected);
	CHECK(st_rip3_environments_get(host.environments,0,&env)==0);env.base_math=64;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	CHECK(process(&host,"2Ya0F",-1)==0 && st_rip3_styles_current(host.styles,&current)==0 && current==0);
	CHECK(process(&host,"2Y&0F",-1)==0 && st_rip3_styles_current(host.styles,&current)==0 && current==0);
	/* Selection needs no drawing surface, palette or graphics-text binding. */
	missing=host;missing.ports=NULL;missing.palettes=NULL;missing.text=NULL;
	CHECK(process(&missing,"2Y304",ST_RIP3_COMMAND_OPERATION_FAILED)==0
	    && st_rip3_styles_current(host.styles,&current)==0 && current==3);
	CHECK(st_rip3_styles_info(host.styles,0,&info)==0 && !info.protected);
	CHECK(st_rip3_styles_info(host.styles,3,&info)==0 && info.defined && !info.protected);
	CHECK(process(&missing,"2Y30",-1)==0);
	missing.styles=NULL;CHECK(process(&missing,"2Y10",ST_RIP3_COMMAND_UNSUPPORTED)==0);
	missing=host;missing.environments=NULL;CHECK(process(&missing,"2Y10",ST_RIP3_COMMAND_UNSUPPORTED)==0);
	CHECK(st_rip3_ports_activate(host.ports,false)==0 && process(&host,"2Y40",-1)==0);
	CHECK(st_rip3_styles_get(host.styles,4,&style)==0 && style.font_mode==ST_RIP3_FONT_CLASSIC && style.borders);
	CHECK(select_reference(&host,"$CUR(STYLE,35)$",false)==0);
	CHECK(st_rip3_styles_current(host.styles,&current)==0 && current==35);
	CHECK(st_rip3_styles_get(host.styles,35,&style)==0 && style.drawing_color==15 && style.font_mode==ST_RIP3_FONT_CLASSIC);
	CHECK(select_reference(&host,"$CUR(STYLE,36)$",true)==0 && select_reference(&host,"$CUR(STYLE,-1)$",true)==0);
	CHECK(st_rip3_styles_current(host.styles,&current)==0 && current==35);
	destroy(&host);return 0;
}

struct diagnostic_order {
	const struct st_rip3_command_host *host;
	unsigned count,current[2];
	bool protected_source[2],wrong_kind;
};

static void
ordered_diagnostic(void *context, enum st_rip3_command_diagnostic kind)
{
	struct diagnostic_order *seen=context;
	struct st_rip3_style_info info;
	if (kind!=ST_RIP3_COMMAND_OPERATION_FAILED) seen->wrong_kind=true;
	if (seen->count<2) {
		if (st_rip3_styles_current(seen->host->styles,&seen->current[seen->count])!=0
		    || st_rip3_styles_info(seen->host->styles,3,&info)!=0) seen->wrong_kind=true;
		else seen->protected_source[seen->count]=info.protected;
	}
	seen->count++;
}

static int
published_examples(void)
{
	/* Literal state/diagnostic oracles from the adopted PR81 table. */
	static const struct {
		unsigned source,destination,flags,errors;
		bool source_protected,destination_protected;
	} cases[]={
		{3,5,12,0,false,false},{3,5,3,0,false,false},
		{5,5,6,0,false,false},{5,5,9,0,true,true},
		{0,5,4,1,false,false},{3,0,5,1,true,false},
		{0,0,5,2,false,false},{0,0,15,2,false,false},
		{3,5,16,0,false,false},
	};
	for (unsigned base=36; base<=64; base+=28) for (size_t i=0; i<sizeof(cases)/sizeof(cases[0]); i++) {
		struct st_rip3_command_host host={0};
		struct st_rip3_environment env;
		struct st_rip3_style style;
		struct st_rip3_style_info info;
		unsigned current;
		char command[6]="2Y000";
		CHECK(create(&host,ST_RIP3_CANVAS_640_480,640,480)==0);
		CHECK(st_rip3_environments_get(host.environments,0,&env)==0);env.base_math=base;
		CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
		CHECK(st_rip3_styles_get(host.styles,0,&style)==0
		    && st_rip3_styles_define(host.styles,3,&style)==0 && st_rip3_styles_define(host.styles,5,&style)==0);
		CHECK(st_rip3_styles_switch(host.styles,cases[i].source)==0);
		number(command+2,1,cases[i].destination,base);number(command+3,2,cases[i].flags,base);
		struct diagnostic_order seen={.host=&host};
		const struct st_rip3_command_sink sink={&seen,ordered_diagnostic};
		st_rip3_commands_process(&host,&sink,(const uint8_t *)command,5);
		CHECK(seen.count==cases[i].errors && !seen.wrong_kind);
		CHECK(st_rip3_styles_current(host.styles,&current)==0 && current==cases[i].destination);
		CHECK(st_rip3_styles_info(host.styles,cases[i].source,&info)==0 && info.protected==cases[i].source_protected);
		CHECK(st_rip3_styles_info(host.styles,cases[i].destination,&info)==0 && info.protected==cases[i].destination_protected);
		if (seen.count!=0) {
			CHECK(seen.current[0]==0);
			CHECK(seen.protected_source[0]==(cases[i].source==3));
		}
		/* Optional diagnostics never control the state transition. */
		CHECK(st_rip3_styles_protect(host.styles,3,false)==0 && st_rip3_styles_protect(host.styles,5,false)==0
		    && st_rip3_styles_switch(host.styles,cases[i].source)==0);
		st_rip3_commands_process(&host,NULL,(const uint8_t *)command,5);
		CHECK(st_rip3_styles_current(host.styles,&current)==0 && current==cases[i].destination);
		CHECK(st_rip3_styles_info(host.styles,cases[i].source,&info)==0 && info.protected==cases[i].source_protected);
		CHECK(st_rip3_styles_info(host.styles,cases[i].destination,&info)==0 && info.protected==cases[i].destination_protected);
		destroy(&host);
	}
	return 0;
}

struct stream_state { const struct st_rip3_command_host *host; unsigned errors; };
static void
stream_error(void *context, enum st_rip3_command_diagnostic kind)
{
	struct stream_state *state=context;(void)kind;state->errors++;
}
static void
stream_command(void *context, const uint8_t *bytes, size_t length)
{
	struct stream_state *state=context;const struct st_rip3_command_sink sink={state,stream_error};
	st_rip3_commands_process(state->host,&sink,bytes,length);
}

static int
render(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_command_host host={0};
	struct st_rip3_style style;
	struct st_rip3_text_context text={.encoding=ST_RIP3_TEXT_CP437};
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture=NULL;
	struct st_rip3_stream *stream=NULL;
	struct stream_state state={&host,0};
	const struct st_rip3_stream_sink sink={.context=&state,.command=stream_command};
	CHECK(create(&host,canvas,width,height)==0);host.text=&text;
	const struct st_rip3_cursor point={2,8};CHECK(st_rip3_ports_cursor(host.ports,0,&point)==0);
	CHECK(st_rip3_styles_get(host.styles,0,&style)==0 && st_rip3_fill_solid(&style.fill,true)==0);
	style.font_mode=ST_RIP3_FONT_OUTLINE;style.outline.spacing=100;style.borders=false;
	CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
	CHECK(process(&host,"TA",ST_RIP3_COMMAND_UNSUPPORTED)==0);
	CHECK(st_rip3_stream_create(&stream,&sink)==0);
	const char *wire="!|2Y00F|2Y10?|2Y10|c03|S0109|K08010C05|T\r";
	for (size_t i=0; wire[i]!=0; i++) st_rip3_stream_process(stream,(const uint8_t *)wire+i,1);
	CHECK(state.errors==3 && cursor(&host,2,8) && text.encoding==ST_RIP3_TEXT_CP437);
	CHECK(select_reference(&host,"$CUR(STYLE,0)$",false)==0 && process(&host,"K01010505",-1)==0);
	CHECK(process(&host,"TA",ST_RIP3_COMMAND_UNSUPPORTED)==0);
	text.encoding=ST_RIP3_TEXT_UTF8;
	CHECK(select_reference(&host,"$CUR(STYLE,1)$",false)==0 && process(&host,"T",-1)==0);
	CHECK(cursor(&host,2,8) && text.encoding==ST_RIP3_TEXT_UTF8);
	CHECK(st_rip3_palettes_get(host.palettes,0,&palette)==0 && st_rip3_ports_snapshot(host.ports,&palette,&capture)==0);
	for (uint32_t y=0; y<height; y++) for (uint32_t x=0; x<width; x++) {
		unsigned expected=0;
		if (x>=1 && x<5 && y>=1 && y<5) expected=7;
		if (x>=8 && x<=12 && y>=1 && y<=5) expected=x==8 || x==12 || y==1 || y==5 ? 3 : 9;
		CHECK(capture->primary[(size_t)y*width+x]==(ST_PIXEL_COLOR_RGB_TAG | expected*UINT32_C(0x010101)));
	}
	st_pixel_buffer_destroy(capture);st_rip3_stream_destroy(stream);destroy(&host);return 0;
}

int
main(void)
{
	CHECK(matrix()==0);CHECK(boundaries()==0);CHECK(published_examples()==0);
	CHECK(render(ST_RIP3_CANVAS_640_480,640,480)==0);
	CHECK(render(ST_RIP3_CANVAS_800_600,800,600)==0);
	CHECK(render(ST_RIP3_CANVAS_1024_768,1024,768)==0);
	return 0;
}
