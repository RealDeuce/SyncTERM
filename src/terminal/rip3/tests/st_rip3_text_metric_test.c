/* Independent signed reply, prefix, retained-source and byte metric vectors. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_text_metric.h"
#include "st_rip3_commands.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_environments.h"
#include "st_rip3_styles.h"
#include "st_rip3_ports.h"
#include "st_rip3_text_commands.h"
#include "st_rip3_classic_text.h"
#include "st_rip3_outline_cache.h"
#include "io_error.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do {if (!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}} while (0)
struct output {uint8_t bytes[8192];size_t length;unsigned calls;};
static void reply(void *context,const uint8_t *bytes,size_t length)
{
	struct output *out=context;out->calls++;
	if (length>sizeof(out->bytes)-out->length) {out->length=sizeof(out->bytes);return;}
	memcpy(out->bytes+out->length,bytes,length);out->length+=length;
}
static int
execute(struct st_rip3_metric_state *state,struct st_rip3_command_host *host,
    const char *command,const uint8_t *expanded,size_t length,bool converted,const char *expected)
{
	struct st_rip3_metric_argument argument;size_t offset=0;struct output out={0};
	CHECK(st_rip3_metric_argument_prepare(host,(const uint8_t *)command,strlen(command),&argument,&offset)==0 && offset==7);
	const struct st_rip3_host_sink sink={.context=&out,.reply=reply};
	CHECK(st_rip3_metric_complete(state,host,&argument,expanded,length,converted,&sink)==0);
	CHECK(out.length==strlen(expected) && memcmp(out.bytes,expected,out.length)==0);
	CHECK(out.calls==(out.length!=0 ? 1u : 0u));return 0;
}
int main(void)
{
	struct st_rip3_command_host host={0};struct st_rip3_style style;
	CHECK(st_rip3_style_defaults(&style)==0 && st_rip3_styles_create(&host.styles,&style)==0
	    && st_rip3_environments_create(&host.environments,ST_RIP3_CANVAS_640_480)==0
	    && st_rip3_ports_create(&host.ports,ST_RIP3_CANVAS_640_480)==0);
	static const uint8_t glyphs[256*7]={0};
	const struct st_rip3_classic_font font={.canvas=ST_RIP3_CANVAS_640_480,.number=0,.data.bitmap={3,7,1,7,glyphs}};
	struct st_rip3_text_context text={.encoding=ST_RIP3_TEXT_CP437,.font=&font};host.text=&text;
	struct st_rip3_metric_state state={0};struct st_rip3_environment env;
	CHECK(st_rip3_environments_get(host.environments,0,&env)==0);env.world_width=1280;env.world_height=960;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	CHECK(execute(&state,&host,"r010000$TEXTDATA$",NULL,0,false,"0\r")==0);
	CHECK(execute(&state,&host,"r110000$TEXTDATA$",NULL,0,false,"11\r")==0);
	CHECK(execute(&state,&host,"r310000AB",(const uint8_t *)"AB",2,false,"11:11\r")==0);
	CHECK(execute(&state,&host,"r01xxxxab",(const uint8_t *)"ab",2,false,"16\r")==0);
	CHECK(execute(&state,&host,"r20????ab",(const uint8_t *)"ab",2,false,"16:16\r")==0);
	CHECK(execute(&state,&host,"r000000$textdata$",NULL,0,false,"32\r")==0);
	CHECK(state.length==2 && memcmp(state.bytes,"ab",2)==0);
	CHECK(execute(&state,&host,"r210000",NULL,0,false,"\r")==0);
	CHECK(execute(&state,&host,"r010000",NULL,0,false,"0\r")==0);
	const uint8_t nul[]={'a',0,'b'};
	CHECK(execute(&state,&host,"r010000a",nul,sizeof(nul),false,"8\r")==0 && state.length==1);
	/* Normalized codepage is recovered to its original byte; actual UTF-8
	 * retains the documented independent raw-byte metric profile. */
	text.encoding=ST_RIP3_TEXT_UTF8;
	const uint8_t accented[]={0xc3,0xa9};
	CHECK(execute(&state,&host,"r210000x",accented,2,true,"8\r")==0);
	CHECK(execute(&state,&host,"r210000x",accented,2,false,"8:8\r")==0);
	/* A later decode failure sends no partial reply and retains the newly
	 * completed TEXTDATA assignment rather than restoring its predecessor. */
	const uint8_t bad[]={0xff};struct output failed={0};
	const struct st_rip3_host_sink failed_sink={.context=&failed,.reply=reply};
	const struct st_rip3_metric_argument ordinary={.mode=0,.domain=1};
	CHECK(st_rip3_metric_complete(&state,&host,&ordinary,bad,1,true,&failed_sink)!=0
	    && failed.calls==0 && state.length==1 && state.bytes[0]==0xff);
	text.encoding=ST_RIP3_TEXT_CP437;
	CHECK(execute(&state,&host,"r010000$TEXTDATA$",NULL,0,false,"8\r")==0);
	/* Whole string narrows once, before signed world multiplication/division. */
	style.font.magnification=10;CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
	uint8_t large[1022];memset(large,'A',sizeof(large));
	CHECK(execute(&state,&host,"r010000x",large,sizeof(large),false,"16224\r")==0);
	CHECK(execute(&state,&host,"r000000$TEXTDATA$",NULL,0,false,"32448\r")==0);
	struct st_rip3_metric_argument before={.mode=3,.domain=1},argument=before;size_t offset=99;
	CHECK(st_rip3_metric_argument_prepare(&host,(const uint8_t *)"r410000$SIDE$",13,&argument,&offset)!=0
	    && argument.mode==before.mode && argument.domain==before.domain
	    && argument.retained==before.retained && argument.literal_empty==before.literal_empty
	    && offset==99 && state.length==sizeof(large));
	CHECK(st_rip3_metric_argument_prepare(&host,(const uint8_t *)"r020000x",8,&argument,&offset)!=0);
	/* Literal empty outline input skips font preparation/reply; a nonempty
	 * expression producing empty still enters the measurement path. */
	style.font_mode=ST_RIP3_FONT_OUTLINE;style.outline.size=90;memcpy(style.outline.name,"Marin",6);
	CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
	CHECK(execute(&state,&host,"r010000",NULL,0,false,"")==0);
	CHECK(!st_rip3_outline_cache_valid(text.outline_cache));
	CHECK(execute(&state,&host,"r010000$NULL$",NULL,0,false,"0\r")==0);
	CHECK(st_rip3_outline_cache_valid(text.outline_cache));
	/* Known Marin F width at 90 points, WORLD=640x960 is 50 device X
	 * units, independent of character/string rotation. */
	env.world_width=640;CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	CHECK(execute(&state,&host,"r010000F",(const uint8_t *)"F",1,false,"50\r")==0);
	CHECK(execute(&state,&host,"r210000FF",(const uint8_t *)"FF",2,false,"50:50\r")==0);
	style.outline.spacing=125;CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
	CHECK(execute(&state,&host,"r010000F",(const uint8_t *)"F",1,false,"62\r")==0);
	memset(large,'F',sizeof(large));
	CHECK(execute(&state,&host,"r010000x",large,700,false,"-22136\r")==0);
	/* Height is the global font axis, independent of decorations/rotations.
	 * Current WORLD conversion uses retained device metrics on failure. */
	style.outline.spacing=100;
	env.world_height=480;CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
	CHECK(execute(&state,&host,"r110000FF",(const uint8_t *)"FF",2,false,"104\r")==0);
	CHECK(execute(&state,&host,"r310000$TEXTDATA$",NULL,0,false,"104:104\r")==0);
	for (unsigned rotation=0;rotation<4;rotation++) {
		style.outline.character_rotation=rotation*900;
		style.outline.string_rotation=((rotation+1)%4)*900;
		CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
		CHECK(execute(&state,&host,"r110000F",(const uint8_t *)"F",1,false,"104\r")==0);
	}
	memcpy(style.outline.name,"absent",7);CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
	env.world_width=1280;env.world_height=960;CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	CHECK(execute(&state,&host,"r000000F",(const uint8_t *)"F",1,false,"100\r")==0);
	CHECK(execute(&state,&host,"r100000$TEXTDATA$",NULL,0,false,"208\r")==0);
	env.world_height=0;CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	CHECK(execute(&state,&host,"r100000$TEXTDATA$",NULL,0,false,"0\r")==0);
	CHECK(execute(&state,&host,"r110000$TEXTDATA$",NULL,0,false,"104\r")==0);
	memcpy(style.outline.name,"Marin",6);CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
	env.world_width=65535;env.world_height=65535;CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	CHECK(execute(&state,&host,"r010000F",(const uint8_t *)"F",1,false,"0\r")==0);
	CHECK(execute(&state,&host,"r110000$TEXTDATA$",NULL,0,false,"0\r")==0);
	CHECK(st_rip3_outline_cache_valid(text.outline_cache));
	memcpy(style.outline.name,"absent",7);CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
	st_rip3_outline_cache_reset(text.outline_cache);
	CHECK(execute(&state,&host,"r110000$TEXTDATA$",NULL,0,false,"0\r")==0);
	/* Independent original-font vectors at the documented reference frame. */
	st_rip3_environments_destroy(host.environments);
	st_rip3_ports_destroy(host.ports);
	CHECK(st_rip3_environments_create_device(&host.environments,640,350)==0
	    && st_rip3_ports_create_device(&host.ports,640,350)==0);
	CHECK(st_rip3_environments_get(host.environments,0,&env)==0);
	style.font_mode=ST_RIP3_FONT_CLASSIC;style.font.font=1;style.font.magnification=4;
	for (unsigned direction=0;direction<2;direction++) {
		style.font.direction=direction;CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
		CHECK(execute(&state,&host,"r010000AB",(const uint8_t *)"AB",2,false,"39\r")==0);
		CHECK(execute(&state,&host,"r110000$TEXTDATA$",NULL,0,false,"32\r")==0);
		CHECK(execute(&state,&host,"r210000$TEXTDATA$",NULL,0,false,"19:20\r")==0);
		CHECK(execute(&state,&host,"r310000$TEXTDATA$",NULL,0,false,"32:32\r")==0);
	}
	style.font.magnification=2;CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
	CHECK(execute(&state,&host,"r010000AB",(const uint8_t *)"AB",2,false,"26\r")==0);
	CHECK(execute(&state,&host,"r110000$TEXTDATA$",NULL,0,false,"21\r")==0);
	CHECK(execute(&state,&host,"r210000$TEXTDATA$",NULL,0,false,"12:13\r")==0);
	CHECK(execute(&state,&host,"r310000$TEXTDATA$",NULL,0,false,"21:21\r")==0);
	style.font.font=0;CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
	CHECK(execute(&state,&host,"r010000AB",(const uint8_t *)"AB",2,false,"32\r")==0);
	CHECK(execute(&state,&host,"r210000$TEXTDATA$",NULL,0,false,"16:16\r")==0);
	CHECK(execute(&state,&host,"r310000$TEXTDATA$",NULL,0,false,"16:16\r")==0);
	CHECK(execute(&state,&host,"r110000",NULL,0,false,"16\r")==0);
	CHECK(execute(&state,&host,"r310000",NULL,0,false,"\r")==0);
	struct st_rip3_classic_font broken={.number=1};text.font=&broken;
	style.font.font=1;CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
	CHECK(execute(&state,&host,"r010000AB",(const uint8_t *)"AB",2,false,"0\r")==0);
	CHECK(execute(&state,&host,"r310000$TEXTDATA$",NULL,0,false,"0:0\r")==0);
	/* Failed classic preparation cannot replace the shared outline metrics. */
	text.font=NULL;style.font_mode=ST_RIP3_FONT_OUTLINE;memcpy(style.outline.name,"Marin",6);
	style.outline.size=10;env.world_width=1280;env.world_height=700;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0 && st_rip3_styles_set(host.styles,0,&style)==0);
	CHECK(execute(&state,&host,"r010000FF",(const uint8_t *)"FF",2,false,"4\r")==0);
	CHECK(execute(&state,&host,"r110000$TEXTDATA$",NULL,0,false,"5\r")==0);
	style.outline.size=4095;env.world_width=640;env.world_height=1;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0 && st_rip3_styles_set(host.styles,0,&style)==0);
	CHECK(execute(&state,&host,"r110000F",(const uint8_t *)"F",1,false,"32762\r")==0);
	CHECK(execute(&state,&host,"r100000$TEXTDATA$",NULL,0,false,"93\r")==0);
	style.outline.size=10;env.world_width=1280;env.world_height=700;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0 && st_rip3_styles_set(host.styles,0,&style)==0);
	CHECK(execute(&state,&host,"r110000FF",(const uint8_t *)"FF",2,false,"5\r")==0);
	unsigned port_number=0;
	CHECK(st_rip3_ports_operate_views(host.ports,&port_number,1,ST_RIP3_VIEW_DEACTIVATE,0)==0);
	style.outline.size=90;CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
	CHECK(execute(&state,&host,"r010000F",(const uint8_t *)"F",1,false,"2\r")==0);
	CHECK(execute(&state,&host,"r110000$TEXTDATA$",NULL,0,false,"5\r")==0);
	CHECK(st_rip3_styles_define(host.styles,1,&style)==0 && st_rip3_styles_switch(host.styles,1)==0
	    && st_rip3_styles_protect(host.styles,1,true)==0);
	CHECK(st_rip3_environments_define(host.environments,1,&env)==0 && st_rip3_environments_select(host.environments,1)==0
	    && st_rip3_environments_protect(host.environments,1,true)==0);
	const struct st_rip3_rect area={0,0,639,349};
	CHECK(st_rip3_ports_define(host.ports,1,&area,false)==0 && st_rip3_ports_switch(host.ports,1)==0
	    && st_rip3_ports_operate_views(host.ports,(const unsigned[]){1},1,ST_RIP3_VIEW_DEACTIVATE,0)==0
	    && st_rip3_ports_protect(host.ports,1,true)==0);
	CHECK(execute(&state,&host,"r110000$TEXTDATA$",NULL,0,false,"5\r")==0);
	/* Raw C-prefix sentinel recognition is exact and case insensitive. */
	const uint8_t sentinel_nul[]="$tExTdAtA$\0ignored";
	CHECK(st_rip3_metric_sentinel(sentinel_nul,sizeof(sentinel_nul)-1));
	CHECK(!st_rip3_metric_sentinel((const uint8_t *)" $TEXTDATA$",11));
	CHECK(!st_rip3_metric_sentinel((const uint8_t *)"$TEXTDATA$x",11));
	st_rip3_outline_cache_destroy(text.outline_cache);
	st_rip3_styles_destroy(host.styles);st_rip3_environments_destroy(host.environments);st_rip3_ports_destroy(host.ports);return 0;
}
