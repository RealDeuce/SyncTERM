/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_backup.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_host_internal.h"
#include "st_rip3_chr.h"
#include "st_rip3_classic_text.h"
#include "st_rip3_text_commands.h"
#include "st_rip3_outline_cache.h"
#include "st_rip3_outline_font.h"
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
process(const struct st_rip3_command_host *host, const char *command, int error)
{
	struct observation seen = {0};
	const struct st_rip3_command_sink sink = {&seen,diagnostic};
	st_rip3_commands_process(host, &sink, (const uint8_t *)command, strlen(command));
	if (seen.errors != (error < 0 ? 0u : 1u) || (error >= 0 && (int)seen.kind != error))
		fprintf(stderr, "%s: errors %u, kind %d, expected %d\n", command, seen.errors, (int)seen.kind, error);
	CHECK(seen.errors == (error < 0 ? 0u : 1u));
	if (error >= 0) CHECK((int)seen.kind == error);
	return 0;
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

struct reply_state { char text[64]; unsigned replies,errors; };
static void
host_reply(void *context, const uint8_t *bytes, size_t length)
{
	struct reply_state *state=context;state->replies++;
	if (length>=sizeof(state->text)) {state->errors++;return;}
	memcpy(state->text,bytes,length);state->text[length]=0;
}
static void
host_error(void *context, enum st_rip3_host_diagnostic kind)
{
	struct reply_state *state=context;(void)kind;state->errors++;
}
static int
query(const struct st_rip3_command_host *host, const char *reference, const char *expected)
{
	struct st_rip3_table_host tables={0};
	struct st_rip3_host_variables variables={.tables=&tables};
	struct reply_state state={0};const struct st_rip3_host_sink sink={&state,host_reply,host_error};
	tables.tables[ST_RIP3_TABLE_PORT].live=host->ports;tables.tables[ST_RIP3_TABLE_ENV].live=host->environments;
	st_rip3_host_variables_process(&variables,&sink,(const uint8_t *)reference,strlen(reference));
	if ((expected==NULL && (state.replies!=0 || state.errors!=1))
	    || (expected!=NULL && (state.replies!=1 || state.errors!=0 || strcmp(state.text,expected)!=0)))
		fprintf(stderr,"%s: got %s, replies %u errors %u; expected %s\n",reference,state.text,state.replies,state.errors,expected==NULL ? "error" : expected);
	CHECK(expected==NULL ? state.replies==0 && state.errors==1 : state.replies==1 && state.errors==0 && strcmp(state.text,expected)==0);
	return 0;
}

static int
cursor(const struct st_rip3_command_host *host, unsigned number, int64_t x, int64_t y)
{
	struct st_rip3_port_info info;
	CHECK(st_rip3_ports_info(host->ports,number,&info)==0);
	if (info.cursor.x!=x || info.cursor.y!=y) fprintf(stderr,"cursor port %u: got %lld,%lld expected %lld,%lld\n",
	    number,(long long)info.cursor.x,(long long)info.cursor.y,(long long)x,(long long)y);
	CHECK(info.cursor.x==x && info.cursor.y==y);
	return 0;
}

static int
font(struct st_rip3_classic_font *font, enum st_rip3_canvas canvas, uint8_t bytes[156], bool ink)
{
	memset(bytes,0,156);memcpy(bytes,"PK\010\010TEST\032",9);bytes[9]=128;memcpy(bytes+11,"TEST",4);
	bytes[15]=28;bytes[17]=bytes[19]=1;
	bytes[128]='+';bytes[129]=2;bytes[132]='A';bytes[133]=22;bytes[136]=4;
	bytes[148]=bytes[149]=5;
	if (ink) memcpy(bytes+150,(const uint8_t[]){0x80,0,0x84,0x80,0,0},6);
	*font=(struct st_rip3_classic_font){.canvas=canvas,.number=1};
	CHECK(st_rip3_chr_open(&font->data.stroke,bytes,156)==0);return 0;
}

static int
state_and_queries(void)
{
	struct st_rip3_command_host host={0};
	struct st_rip3_environment env;
	struct st_rip3_backup *backup=NULL;
	struct st_rip3_port_info before,after;
	const struct st_rip3_rect bounds={0,0,79,79},view={5,7,74,76};
	struct st_rip3_cursor point={297,321};
	unsigned current;
	CHECK(create(&host,ST_RIP3_CANVAS_800_600,1280,960)==0);
	CHECK(st_rip3_ports_define(host.ports,1,&bounds,false)==0 && st_rip3_ports_switch(host.ports,1)==0
	    && st_rip3_ports_viewport(host.ports,&view)==0 && st_rip3_ports_protect(host.ports,1,true)==0);
	CHECK(process(&host,"m0A0K",-1)==0 && cursor(&host,1,11,19)==0); /* No text binding is needed. */
	CHECK(query(&host,"$TEXTXY$","11:19")==0 && query(&host,"$TEXTXY(CUR,DEVICE)$","6:11")==0);
	struct st_rip3_classic_font glyphs;uint8_t bytes[156];
	struct st_rip3_text_context text={.encoding=ST_RIP3_TEXT_CP437};
	struct st_rip3_style style;
	CHECK(font(&glyphs,ST_RIP3_CANVAS_800_600,bytes,false)==0);host.text=&text;text.font=&glyphs;
	CHECK(st_rip3_styles_get(host.styles,0,&style)==0);style.font.font=1;style.font.magnification=4;
	CHECK(st_rip3_styles_set(host.styles,0,&style)==0 && process(&host,"TAB",-1)==0 && cursor(&host,1,23,19)==0);
	CHECK(query(&host,"$TEXTXY$","23:19")==0 && query(&host,"$TEXTXY(CUR,DEVICE)$","14:11")==0);
	CHECK(process(&host,"m0U14",-1)==0 && query(&host,"$TEXTXY$","23:32")==0);
	CHECK(process(&host,"@0A0KAB",-1)==0 && cursor(&host,1,27,27)==0 && query(&host,"$TEXTXY$","27:27")==0);

	CHECK(st_rip3_ports_cursor(host.ports,1,&point)==0);
	CHECK(query(&host,"$TEXTXY(1)$","297:321")==0 && query(&host,"$TEXTXY(001,device)$","185:200")==0);
	/* Bare omission reads a deactivated cursor; explicit CUR does not. */
	CHECK(st_rip3_ports_protect(host.ports,1,false)==0 && st_rip3_ports_activate(host.ports,false)==0);
	CHECK(query(&host,"$TEXTXY$","297:321")==0 && query(&host,"$TEXTXY(CUR)$","-1")==0
	    && query(&host,"$TEXTXY(1,DEVICE)$","-1")==0 && query(&host,"$TEXTXY(35)$","-1")==0);
	CHECK(process(&host,"m0U14",-1)==0 && cursor(&host,1,297,321)==0);
	CHECK(st_rip3_ports_activate(host.ports,true)==0 && st_rip3_ports_protect(host.ports,1,true)==0);
	point=(struct st_rip3_cursor){70000,-70000};CHECK(st_rip3_ports_cursor(host.ports,1,&point)==0);
	CHECK(query(&host,"$TEXTXY$","4464:-4464")==0 && query(&host,"$TEXTXY(CUR,WORLD)$","4464:-4464")==0
	    && query(&host,"$TEXTXY(1,DEVICE)$","-21786:21786")==0);
	point=(struct st_rip3_cursor){-3,-3};CHECK(st_rip3_ports_cursor(host.ports,1,&point)==0);
	CHECK(query(&host,"$TEXTXY(1,DEVICE)$","-1:-1")==0); /* Truncation toward zero. */
	const char *const invalid[]={"$TEXTXY(36)$","$TEXTXY(-1)$","$TEXTXY(A)$","$TEXTXY(DEVICE)$", "$TEXTXY(CUR,BAD)$","$TEXTXY(CUR,WORLD,0)$"};
	for (unsigned i=0; i<sizeof(invalid)/sizeof(invalid[0]); i++) CHECK(query(&host,invalid[i],NULL)==0 && query(&host,"$TEXTXY$","-3:-3")==0);
	struct st_rip3_table_host tables={0};struct st_rip3_host_variables variables={.tables=&tables};
	struct st_rip3_builtin_reference reference;char result[8]="KEEP";
	tables.tables[ST_RIP3_TABLE_PORT].live=host.ports;tables.tables[ST_RIP3_TABLE_ENV].live=host.environments;
	CHECK(st_rip3_builtin_reference_parse(&reference,(const uint8_t *)"$TEXTXY$",8)==0);
	CHECK(st_rip3_host_variables_execute(&variables,&reference,result,5)==IO_ERROR_OUT_OF_RANGE && strcmp(result,"KEEP")==0);
	CHECK(st_rip3_host_variables_execute(&variables,&reference,result,0)==IO_ERROR_OUT_OF_RANGE && strcmp(result,"KEEP")==0);
	struct st_rip3_command_host missing=host;missing.ports=NULL;
	CHECK(query(&missing,"$TEXTXY$",NULL)==0);
	missing=host;missing.environments=NULL;
	CHECK(query(&missing,"$TEXTXY$","-3:-3")==0 && query(&missing,"$TEXTXY(CUR,DEVICE)$",NULL)==0);
	CHECK(st_rip3_ports_info(host.ports,1,&before)==0);
	point=(struct st_rip3_cursor){INT64_MAX,0};CHECK(st_rip3_ports_cursor(host.ports,1,&point)==IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_ports_cursor(host.ports,36,&point)==IO_ERROR_OUT_OF_RANGE && st_rip3_ports_cursor(host.ports,35,&point)==IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_ports_cursor(NULL,1,&point)==IO_ERROR_INVALID_ARGUMENT && st_rip3_ports_cursor(host.ports,1,NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_ports_info(host.ports,1,&after)==0 && before.cursor.x==after.cursor.x && before.cursor.y==after.cursor.y);
	/* Backups restore image/selection, while every live per-port cursor stays. */
	CHECK(st_rip3_ports_define(host.ports,2,&bounds,true)==0);
	point=(struct st_rip3_cursor){515,616};CHECK(st_rip3_ports_cursor(host.ports,1,&point)==0);
	point=(struct st_rip3_cursor){111,222};CHECK(st_rip3_ports_cursor(host.ports,2,&point)==0);
	CHECK(st_rip3_ports_backup_create(&backup)==0 && st_rip3_backup_save(backup,ST_RIP3_BACKUP_BASE,0,host.ports)==0);
	point=(struct st_rip3_cursor){717,818};CHECK(st_rip3_ports_cursor(host.ports,1,&point)==0);
	point=(struct st_rip3_cursor){333,444};CHECK(st_rip3_ports_cursor(host.ports,2,&point)==0);
	CHECK(st_rip3_ports_switch(host.ports,2)==0 && st_rip3_styles_switch(host.styles,4)==0);
	CHECK(st_rip3_environments_get(host.environments,0,&env)==0 && st_rip3_environments_define(host.environments,3,&env)==0
	    && st_rip3_environments_select(host.environments,3)==0);
	CHECK(st_rip3_backup_restore(backup,ST_RIP3_BACKUP_BASE,0,host.ports)==0);
	CHECK(cursor(&host,1,717,818)==0 && cursor(&host,2,333,444)==0);
	CHECK(st_rip3_ports_current(host.ports,&current)==0 && current==1
	    && st_rip3_styles_current(host.styles,&current)==0 && current==4
	    && st_rip3_environments_current(host.environments,&current)==0 && current==3);
	/* Replacing viewport geometry cannot move an absolute stored cursor. */
	const struct st_rip3_rect replacement={10,11,30,31};
	CHECK(st_rip3_ports_protect(host.ports,1,false)==0 && st_rip3_ports_viewport_device(host.ports,&replacement)==0
	    && st_rip3_ports_protect(host.ports,1,true)==0 && cursor(&host,1,717,818)==0);
	CHECK(process(&host,"E",-1)==0 && cursor(&host,1,717,818)==0);
	CHECK(st_rip3_ports_protect(host.ports,1,false)==0 && st_rip3_ports_delete(host.ports,1)==0);
	CHECK(st_rip3_backup_restore(backup,ST_RIP3_BACKUP_BASE,0,host.ports)==0 && cursor(&host,1,0,0)==0);
	st_rip3_backup_destroy(backup);destroy(&host);return 0;
}

static int
metrics(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	static const unsigned numerator[]={3,2,3,1,4,5,2,5,3,4},denominator[]={5,3,4,1,3,3,1,2,1,1};
	static const unsigned flags[]={0,1,2,4,8,16,32};
	struct st_rip3_command_host host={0};struct st_rip3_style style;
	struct st_rip3_classic_font glyphs;uint8_t bytes[156];
	struct st_rip3_text_context text={.encoding=ST_RIP3_TEXT_CP437};
	const struct st_rip3_cursor start={100,200};
	CHECK(create(&host,canvas,width,height)==0 && font(&glyphs,canvas,bytes,false)==0);host.text=&text;text.font=&glyphs;
	CHECK(st_rip3_styles_get(host.styles,0,&style)==0);style.font.font=1;
	for (unsigned encoding=0; encoding<2; encoding++) for (unsigned size=1; size<=10; size++)
		for (unsigned direction=0; direction<2; direction++) for (unsigned f=0; f<7; f++) for (unsigned length=1; length<=9; length++) {
			char command[12]="T";memset(command+1,'A',length);command[length+1]=0;
			text.encoding=encoding==0 ? ST_RIP3_TEXT_CP437 : ST_RIP3_TEXT_UTF8;
			style.font.direction=direction;style.font.flags=flags[f];style.font.magnification=size;
			CHECK(st_rip3_styles_set(host.styles,0,&style)==0 && st_rip3_ports_cursor(host.ports,0,&start)==0);
			CHECK(process(&host,command,-1)==0);
			int64_t advance=(int64_t)(length*5*numerator[size-1]*width/(denominator[size-1]*640));
			CHECK(cursor(&host,0,100+advance,200)==0);
			CHECK(process(&host,"T",-1)==0 && cursor(&host,0,100+advance,200)==0);
			char positioned[16]="@0A0K";memset(positioned+5,'A',length);positioned[length+5]=0;
			CHECK(process(&host,positioned,-1)==0 && cursor(&host,0,10+advance,20)==0);

		}
	/* Narrow the entire integer metric to a signed WORD, not each glyph. */
	char large[1102];large[0]='T';memset(large+1,'A',1100);large[1101]=0;
	style.font.magnification=10;style.font.direction=0;style.font.flags=0;
	CHECK(st_rip3_styles_set(host.styles,0,&style)==0 && st_rip3_ports_cursor(host.ports,0,&start)==0);
	CHECK(process(&host,large,-1)==0);
	int64_t advance=(int64_t)(1100*5*4*width/640);advance=(advance+32768)%65536-32768;
	CHECK(cursor(&host,0,100+advance,200)==0);
	/* Nonzero classic TEXT_XY stores raw X/Y plus origin, even when scaled. */
	struct st_rip3_environment env;CHECK(st_rip3_environments_get(host.environments,0,&env)==0);
	env.world_width=width*2;env.world_height=height*2;CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	CHECK(process(&host,"@0A0KAA",-1)==0);
	advance=(int64_t)(2*5*4*width/640);CHECK(cursor(&host,0,10+advance,20)==0);
	CHECK(process(&host,"@0A0K",ST_RIP3_COMMAND_INVALID)==0 && cursor(&host,0,10+advance,20)==0);
	destroy(&host);return 0;
}

static int
render(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_command_host host={0};struct st_rip3_style style;
	struct st_rip3_classic_font glyphs;uint8_t bytes[156];
	struct st_rip3_text_context text={.encoding=ST_RIP3_TEXT_CP437};
	struct st_rip3_palette palette;struct st_pixel_buffer *capture=NULL;
	const struct st_rip3_rect bounds={10,20,49,49},view={2,3,31,22},bitmap={0,0,29,19};
	const struct st_rip3_rect destination={70,40,99,59},replacement={17,29,47,49};
	unsigned advance=width==640 ? 5 : width==800 ? 6 : 8;
	unsigned span=width==640 ? 4 : width==800 ? 5 : 6;
	unsigned baseline=height==480 ? 5 : height==600 ? 7 : 9;
	char expected_query[32];
	CHECK(create(&host,canvas,width,height)==0 && font(&glyphs,canvas,bytes,true)==0);host.text=&text;text.font=&glyphs;
	CHECK(st_rip3_styles_get(host.styles,0,&style)==0);style.font.font=1;style.font.magnification=4;
	CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
	CHECK(st_rip3_ports_define(host.ports,1,&bounds,false)==0 && st_rip3_ports_switch(host.ports,1)==0
	    && st_rip3_ports_viewport(host.ports,&view)==0 && st_rip3_ports_protect(host.ports,1,true)==0);
	CHECK(process(&host,"m0202",-1)==0 && process(&host,"TA",-1)==0 && cursor(&host,1,14+advance,25)==0);
	CHECK(st_rip3_ports_define(host.ports,2,&bitmap,true)==0 && st_rip3_ports_switch(host.ports,2)==0);
	text.encoding=ST_RIP3_TEXT_UTF8;
	CHECK(process(&host,"m0202",-1)==0 && process(&host,"TB",-1)==0 && cursor(&host,2,2+advance,2)==0);
	CHECK(st_rip3_ports_switch(host.ports,1)==0 && st_rip3_styles_switch(host.styles,1)==0);
	CHECK(process(&host,"Y01000400",-1)==0 && process(&host,"c03",-1)==0 && process(&host,"TB",-1)==0);
	CHECK(cursor(&host,1,14+2*advance,25)==0 && cursor(&host,2,2+advance,2)==0);
	CHECK(st_rip3_ports_protect(host.ports,1,false)==0 && st_rip3_ports_viewport_device(host.ports,&replacement)==0
	    && st_rip3_ports_protect(host.ports,1,true)==0 && process(&host,"TA",-1)==0);
	CHECK(cursor(&host,1,14+3*advance,25)==0);
	CHECK(st_rip3_ports_copy(host.ports,2,NULL,0,&destination,ST_RIP3_TRANSFER_COPY)==0);
	snprintf(expected_query,sizeof(expected_query),"%u:25",14+3*advance);
	CHECK(query(&host,"$TEXTXY$",expected_query)==0);
	snprintf(expected_query,sizeof(expected_query),"%u:2",2+advance);
	CHECK(query(&host,"$TEXTXY(2)$",expected_query)==0);
	CHECK(st_rip3_palettes_get(host.palettes,0,&palette)==0 && st_rip3_ports_snapshot(host.ports,&palette,&capture)==0);
	for (uint32_t y=0; y<height; y++) for (uint32_t x=0; x<width; x++) {
		unsigned expected=0;
		if (y==25+baseline && x>=14 && x<=14+span) expected=12;
		if (y==25+baseline && ((x>=14+advance && x<=14+advance+span)
		    || (x>=14+2*advance && x<=14+2*advance+span))) expected=3;
		if (y==42+baseline && x>=72 && x<=72+span) expected=12;
		CHECK(capture->primary[(size_t)y*width+x]==(ST_PIXEL_COLOR_RGB_TAG | expected*UINT32_C(0x010101)));
	}
	st_pixel_buffer_destroy(capture);destroy(&host);return 0;
}

static int
cached_zero(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_command_host host={0};struct st_rip3_style style;
	struct st_rip3_frame frame;struct st_rip3_outline_font prepared;
	struct st_rip3_environment environment;
	uint8_t blank[255]={0};
	const struct st_rip3_classic_font bitmap={.canvas=canvas,.number=0,.data.bitmap={1,1,1,1,blank}};
	struct st_rip3_text_context text={.encoding=ST_RIP3_TEXT_CP437,.font=&bitmap};
	const struct st_rip3_rect bounds={0,0,159,159},view={5,7,154,156};
	CHECK(create(&host,canvas,width,height)==0);host.text=&text;
	CHECK(st_rip3_ports_define(host.ports,1,&bounds,false)==0 && st_rip3_ports_switch(host.ports,1)==0
	    && st_rip3_ports_viewport_device(host.ports,&view)==0);
	CHECK(st_rip3_styles_get(host.styles,0,&style)==0);
	/* Missing binding is the cold snapshot, not a classic-width substitute. */
	CHECK(process(&host,"@2S28F",-1)==0 && cursor(&host,1,105,87)==0);
	CHECK(st_rip3_outline_cache_create(&text.outline_cache)==0);
	for (unsigned direction=0; direction<2; direction++) for (unsigned justify=0; justify<3; justify++) {
		style.font.direction=direction;style.font.flags=justify;
		CHECK(st_rip3_styles_set(host.styles,0,&style)==0
		    && process(&host,"@2S28F",-1)==0 && cursor(&host,1,105,87)==0);
	}
	CHECK(st_rip3_frame_init(&frame,canvas)==0 && st_rip3_frame_world(&frame,width,height)==0);
	CHECK(st_rip3_outline_font_builtin(&prepared,"Marin",&frame,90,0,0)==0
	    && st_rip3_outline_cache_prepare(text.outline_cache,&prepared,100)==0);
	for (unsigned direction=0; direction<2; direction++) for (unsigned justify=0; justify<3; justify++) {
		style.font.direction=direction;style.font.flags=justify;
		CHECK(st_rip3_styles_set(host.styles,0,&style)==0
		    && process(&host,"@2S28F",-1)==0 && cursor(&host,1,155,87)==0);
	}
	CHECK(query(&host,"$TEXTXY$","155:87")==0);
	CHECK(process(&host,"@2S28\x80\xff",-1)==0 && cursor(&host,1,210,87)==0);
	text.encoding=ST_RIP3_TEXT_UTF8;
	CHECK(process(&host,"@2S28\xc3\x87\xc2\xa0",-1)==0 && cursor(&host,1,210,87)==0);
	CHECK(st_rip3_styles_define(host.styles,4,&style)==0 && st_rip3_styles_switch(host.styles,4)==0);
	CHECK(st_rip3_ports_define(host.ports,2,&bounds,true)==0 && st_rip3_ports_switch(host.ports,2)==0);
	CHECK(process(&host,"@2S28F",-1)==0 && cursor(&host,2,150,80)==0 && cursor(&host,1,210,87)==0);
	CHECK(st_rip3_ports_switch(host.ports,1)==0);
	/* Classic selection and viewport erase retain the context-wide snapshot. */
	CHECK(process(&host,"Y00000100",-1)==0 && st_rip3_styles_get(host.styles,4,&style)==0);
	style.background_color=0;CHECK(st_rip3_styles_set(host.styles,4,&style)==0);
	CHECK(process(&host,"E",-1)==0 && process(&host,"@2S28F",-1)==0 && cursor(&host,1,155,87)==0);
	CHECK(st_rip3_environments_get(host.environments,0,&environment)==0);
	environment.world_width=2*width;environment.world_height=2*height;
	CHECK(st_rip3_environments_set(host.environments,0,&environment)==0);
	struct st_rip3_outline_font bad=prepared;bad.points=0;
	CHECK(st_rip3_outline_cache_prepare(text.outline_cache,&bad,125)==IO_ERROR_OUT_OF_RANGE);
	CHECK(process(&host,"@2S28F",-1)==0 && cursor(&host,1,155,87)==0);
	CHECK(st_rip3_frame_world(&frame,2*width,2*height)==0);
	CHECK(st_rip3_outline_font_builtin(&prepared,"Marin",&frame,90,0,0)==0
	    && st_rip3_outline_cache_prepare(text.outline_cache,&prepared,100)==0);
	CHECK(process(&host,"@2S28F",-1)==0 && cursor(&host,1,130,87)==0);
	CHECK(process(&host,"@2S28\xc3",ST_RIP3_COMMAND_INVALID)==0 && cursor(&host,1,130,87)==0);
	CHECK(process(&host,"@2S28F",-1)==0 && cursor(&host,1,130,87)==0);
	CHECK(process(&host,"T",-1)==0 && process(&host,"@2S28",ST_RIP3_COMMAND_INVALID)==0
	    && cursor(&host,1,130,87)==0);
	CHECK(st_rip3_ports_activate(host.ports,false)==0 && process(&host,"@2S28F",-1)==0
	    && cursor(&host,1,130,87)==0 && st_rip3_outline_cache_valid(text.outline_cache));
	CHECK(st_rip3_ports_activate(host.ports,true)==0);
	/* Restore the old frame for preparation, then change it without preparing:
	 * the cached 50 stays, including its final signed-WORD handoff. */
	environment.world_width=width;environment.world_height=height;
	CHECK(st_rip3_environments_set(host.environments,0,&environment)==0
	    && st_rip3_frame_world(&frame,width,height)==0);
	CHECK(st_rip3_outline_font_builtin(&prepared,"Marin",&frame,90,0,0)==0
	    && st_rip3_outline_cache_prepare(text.outline_cache,&prepared,100)==0);
	environment.world_width=2*width;environment.world_height=2*height;
	CHECK(st_rip3_environments_set(host.environments,0,&environment)==0);
	char long_text[706]="@2S28";memset(long_text+5,'F',700);long_text[705]=0;
	CHECK(process(&host,long_text,-1)==0 && cursor(&host,1,-30431,87)==0);
	/* A late cached-width failure remains inside the void command boundary. */
	bad=prepared;bad.flags|=0x400;bad.bounds.xmin=0;bad.bounds.xmax=INT64_MAX;
	CHECK(st_rip3_outline_cache_prepare(text.outline_cache,&bad,101)==0);
	long_text[105]=0;
	CHECK(process(&host,long_text,ST_RIP3_COMMAND_OPERATION_FAILED)==0 && cursor(&host,1,-30431,87)==0);
	st_rip3_outline_cache_reset(text.outline_cache);
	CHECK(process(&host,"@2S28F",-1)==0 && cursor(&host,1,105,87)==0);
	CHECK(process(&host,"TF",-1)==0 && cursor(&host,1,106,87)==0); /* Classic T keeps its own metric. */
	struct st_rip3_palette palette;struct st_pixel_buffer *capture=NULL;
	CHECK(st_rip3_palettes_get(host.palettes,0,&palette)==0 && st_rip3_ports_snapshot(host.ports,&palette,&capture)==0);
	for (size_t i=0; i<(size_t)width*height; i++) CHECK(capture->primary[i]==ST_PIXEL_COLOR_RGB_TAG);
	CHECK(query(&host,"$TEXTXY$","106:87")==0 && !st_rip3_outline_cache_valid(text.outline_cache));
	st_pixel_buffer_destroy(capture);st_rip3_outline_cache_destroy(text.outline_cache);destroy(&host);
	return 0;
}

int
main(void)
{
	CHECK(state_and_queries()==0);
	CHECK(render(ST_RIP3_CANVAS_640_480,640,480)==0);
	CHECK(render(ST_RIP3_CANVAS_800_600,800,600)==0);
	CHECK(render(ST_RIP3_CANVAS_1024_768,1024,768)==0);
	CHECK(metrics(ST_RIP3_CANVAS_640_480,640,480)==0);
	CHECK(metrics(ST_RIP3_CANVAS_800_600,800,600)==0);
	CHECK(metrics(ST_RIP3_CANVAS_1024_768,1024,768)==0);
	CHECK(cached_zero(ST_RIP3_CANVAS_640_480,640,480)==0);
	CHECK(cached_zero(ST_RIP3_CANVAS_800_600,800,600)==0);
	CHECK(cached_zero(ST_RIP3_CANVAS_1024_768,1024,768)==0);
	return 0;
}
