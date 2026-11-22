/* Independent plain-button geometry, state and input expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_button_styles.h"
#include "st_rip3_button_payload.h"
#include "st_rip3_clipboard.h"
#include "st_rip3_environments.h"
#include "st_rip3_styles.h"
#include "st_rip3_ports.h"
#include "st_rip3_palettes.h"
#include "st_rip3_mouse.h"
#include "st_rip3_text_commands.h"
#include "st_rip3_number.h"
#include "st_rip3_table_host.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_button_render.h"
#include "st_rip3_graphical.h"
#include "st_rip3_user_variables.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do {if (!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}} while (0)
static void diagnostic(void *context,enum st_rip3_command_diagnostic kind) {(void)kind;++*(unsigned *)context;}
static int pixel(struct st_rip3_ports *ports,int x,int y,unsigned color)
{
	uint8_t value=255;CHECK(st_rip3_ports_read(ports,0,x,y,&value)==0 && value==color);return 0;
}
static void command(struct st_rip3_command_host *host,unsigned *errors,const char *wire)
{
	const struct st_rip3_command_sink sink={errors,diagnostic};
	st_rip3_commands_process(host,&sink,(const uint8_t *)wire,strlen(wire));
}

static int payloads(void)
{
	static const struct {const char *text,*icon,*label,*host;int identity;} cases[]={
	    {"icon<>label<>host","icon","label","host",-1},
	    {"<>label<>host","","label","host",-1},{"icon<>label","icon","label","",-1},
	    {"icon<>label<>","icon","label","",-1},{"<>label","","label","",-1},
	    {"<>label<>","","label","",-1},{"<><>host","","","host",-1},
	    {"icon","icon","","",-1},{"icon<>","icon","","",-1},
	    {"icon<><>","icon","","",-1},{"<>","","","",-1},{"<><>","","","",-1},
	    {"<><><>","","","<>",-1},{"<>label<>host<>tail","","label","host<>tail",-1},
	    {"Style B","Style B","","",-1},{"<>Style B","","Style B","",-1},
	    {"ID=7: \t\n\v\f\r<> label <> host ",""," label "," host ",7},
	    {" ID=7:<>label<>host","ID=7:","label","host",-1},
	    {"one\\<>two<>three","one\\","two","three",-1},
	    {"\"one<>two\"<>three","\"one","two\"","three",-1},
	    {"id=00007:<>label<>ID=8:host","","label","ID=8:host",7}};
	for (size_t i=0;i<sizeof(cases)/sizeof(*cases);i++) {
		struct st_rip3_button_payload p;
		CHECK(st_rip3_button_payload_parse(&p,(const uint8_t *)cases[i].text,strlen(cases[i].text))==0);
		CHECK(p.identity==cases[i].identity);
		CHECK(p.icon.length==strlen(cases[i].icon) && memcmp(p.icon.data,cases[i].icon,p.icon.length)==0);
		CHECK(p.label.length==strlen(cases[i].label) && memcmp(p.label.data,cases[i].label,p.label.length)==0);
		CHECK(p.host.length==strlen(cases[i].host) && memcmp(p.host.data,cases[i].host,p.host.length)==0);
	}
	struct st_rip3_button_payload p={.identity=123};
	CHECK(st_rip3_button_payload_parse(&p,(const uint8_t *)"ID=7: \t",7)!=0 && p.identity==123);
	uint8_t text[264];memset(text,'X',sizeof(text));
	CHECK(st_rip3_button_payload_parse(&p,text,127)==0 && p.icon.length==127);
	CHECK(st_rip3_button_payload_parse(&p,text,128)!=0);
	text[0]='<';text[1]='>';
	CHECK(st_rip3_button_payload_parse(&p,text,257)==0 && p.label.length==255);
	CHECK(st_rip3_button_payload_parse(&p,text,258)!=0);
	uint8_t normalized[2+3*256];normalized[0]='<';normalized[1]='>';
	for (size_t i=2;i<sizeof(normalized);i+=3) {normalized[i]=0xe2;normalized[i+1]=0x94;normalized[i+2]=0x82;}
	CHECK(st_rip3_button_payload_parse_encoded(&p,normalized+2,3*127,true)==0 && p.icon.length==3*127);
	CHECK(st_rip3_button_payload_parse_encoded(&p,normalized+2,3*128,true)!=0);
	CHECK(st_rip3_button_payload_parse_encoded(&p,normalized,2+3*255,true)==0 && p.label.length==3*255);
	CHECK(st_rip3_button_payload_parse_encoded(&p,normalized,2+3*256,true)!=0);
	CHECK(st_rip3_button_payload_parse_encoded(&p,normalized+2,3*127,false)!=0);

	return 0;
}

static void variable_error(void *context,enum st_rip3_host_diagnostic kind) {(void)kind;++*(unsigned *)context;}
static int mouse_pointers(void)
{
	struct st_rip3_command_host host={0};unsigned errors=0;
	CHECK(st_rip3_environments_create(&host.environments,ST_RIP3_CANVAS_640_480)==0);
	struct st_rip3_environment env;CHECK(st_rip3_environment_defaults(&env)==0);
	env.base_math=64;env.coordinate_size=5;env.world_width=1280;
	CHECK(st_rip3_environments_define(host.environments,35,&env)==0
	    && st_rip3_environments_select(host.environments,35)==0);
	struct st_rip3_table_host tables={0};tables.tables[ST_RIP3_TABLE_ENV].live=host.environments;
	const struct st_rip3_host_variables variables={.tables=&tables};
	const struct st_rip3_host_sink sink={.context=&errors,.diagnostic=variable_error};
	for (unsigned pointer=0;pointer<=7;pointer++) for (unsigned res=0;res<=4;res++) {
		char wire[9]="1c000000";wire[3]=(char)('0'+pointer);wire[4+res]=0;
		command(&host,&errors,wire);
		CHECK(errors==0 && st_rip3_environments_get(host.environments,35,&env)==0
		    && env.mouse_pointer==pointer && env.base_math==64 && env.coordinate_size==5 && env.world_width==1280);
	}
	CHECK(st_rip3_environments_protect(host.environments,35,true)==0);
	command(&host,&errors,"1c00");CHECK(errors==1);
	CHECK(st_rip3_environments_get(host.environments,35,&env)==0 && env.mouse_pointer==7);
	st_rip3_host_variables_process(&variables,&sink,(const uint8_t *)"$MCURSOR(2)$",12);
	CHECK(errors==1 && st_rip3_environments_get(host.environments,35,&env)==0 && env.mouse_pointer==2);
	st_rip3_host_variables_process(&variables,&sink,(const uint8_t *)"$MCURSOR(7)$",12);
	CHECK(errors==1 && st_rip3_environments_get(host.environments,35,&env)==0 && env.mouse_pointer==7);
	st_rip3_host_variables_process(&variables,&sink,(const uint8_t *)"$MCURSOR(8)$",12);
	CHECK(errors==2 && st_rip3_environments_get(host.environments,35,&env)==0 && env.mouse_pointer==7);
	st_rip3_host_variables_process(&variables,&sink,(const uint8_t *)"$MCURSOR$",9);
	CHECK(errors==2 && st_rip3_environments_get(host.environments,35,&env)==0 && env.mouse_pointer==0);
	CHECK(st_rip3_environments_protect(host.environments,35,false)==0);
	command(&host,&errors,"1c08");command(&host,&errors,"1c060001");command(&host,&errors,"1c0");
	CHECK(errors==5 && st_rip3_environments_get(host.environments,35,&env)==0 && env.mouse_pointer==0);
	st_rip3_environments_destroy(host.environments);return 0;
}

static int clipboard_buttons(void)
{
	struct st_rip3_command_host host={0};unsigned errors=0;
	struct st_rip3_style drawing;struct st_rip3_button_style style;
	struct st_rip3_clipboard_selection clipboard={0};host.clipboard=&clipboard;
	CHECK(st_rip3_style_defaults(&drawing)==0 && st_rip3_styles_create(&host.styles,&drawing)==0);
	CHECK(st_rip3_environments_create(&host.environments,ST_RIP3_CANVAS_640_480)==0);
	CHECK(st_rip3_ports_create(&host.ports,ST_RIP3_CANVAS_640_480)==0);
	CHECK(st_rip3_button_styles_create(&host.button_styles)==0);
	struct st_rip3_environment env;CHECK(st_rip3_environments_get(host.environments,0,&env)==0);
	env.world_width=640;env.world_height=480;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	CHECK(st_rip3_button_style_defaults(&style)==0);
	style.flags=1|256|1024;style.width=50;style.height=40;style.flags2=96;
	CHECK(st_rip3_button_styles_set(host.button_styles,0,&style)==0);
	struct st_rip3_mouse_sink sink={0};CHECK(st_rip3_mouse_create(&host.mouse,&sink)==0);
	/* Missing assignment and a stale assignment both reject before painting
	 * or adding a field, even with PLAIN also present. */
	command(&host,&errors,"1U2S2S00000000<><>");CHECK(errors==1);
	clipboard=(struct st_rip3_clipboard_selection){true,4};
	command(&host,&errors,"1U2S2S00000000<><>");CHECK(errors==2);
	CHECK(pixel(host.ports,100,100,0)==0 && st_rip3_mouse_count(host.mouse)==0);
	const struct st_rip3_rect bitmap={0,0,3,2},view={1,0,3,1};
	CHECK(st_rip3_ports_define(host.ports,4,&bitmap,true)==0 && st_rip3_ports_switch(host.ports,4)==0);
	for (int y=0;y<3;y++) for (int x=0;x<4;x++)
		CHECK(st_rip3_ports_plot(host.ports,x,y,(uint8_t)(1+x+4*y),ST_RIP3_COPY)==0);
	CHECK(st_rip3_ports_viewport(host.ports,&view)==0 && st_rip3_ports_protect(host.ports,4,true)==0);
	CHECK(st_rip3_ports_switch(host.ports,0)==0);
	command(&host,&errors,"1U2S2S00000000<><>");CHECK(errors==2);
	/* Native 3x2 viewport, not fixed 50x40 or WORLD-projected dimensions.
	 * PLAIN/no-fill/pattern bits cannot replace the clipboard face. */
	CHECK(pixel(host.ports,100,100,2)==0 && pixel(host.ports,102,101,8)==0);
	CHECK(pixel(host.ports,103,100,0)==0 && pixel(host.ports,100,102,0)==0);
	CHECK(st_rip3_mouse_event(host.mouse,100,100,true) && st_rip3_mouse_event(host.mouse,102,101,false));
	CHECK(!st_rip3_mouse_event(host.mouse,103,100,true) && !st_rip3_mouse_event(host.mouse,103,100,false));
	CHECK(!st_rip3_mouse_event(host.mouse,100,102,true) && !st_rip3_mouse_event(host.mouse,100,102,false));
	env.world_width=1280;env.world_height=960;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	command(&host,&errors,"1U5K5K01010000<><>");CHECK(errors==2);
	CHECK(pixel(host.ports,100,100,2)==0 && pixel(host.ports,102,101,8)==0 && st_rip3_mouse_count(host.mouse)==2);
	/* Nonmouse destination may alias the source. Copy reads original pixels
	 * on overlap and preserves the current selection and source cursor. */
	CHECK(st_rip3_ports_protect(host.ports,4,false)==0 && st_rip3_ports_switch(host.ports,4)==0);
	const struct st_rip3_rect full={0,0,3,2};
	CHECK(st_rip3_ports_viewport(host.ports,&full)==0);
	style.flags=1;CHECK(st_rip3_button_styles_set(host.button_styles,0,&style)==0);
	command(&host,&errors,"1U020200000000<><>");CHECK(errors==2);
	uint8_t value;CHECK(st_rip3_ports_read(host.ports,4,1,1,&value)==0 && value==1);
	CHECK(st_rip3_ports_read(host.ports,4,3,2,&value)==0 && value==7);
	unsigned current;CHECK(st_rip3_ports_current(host.ports,&current)==0 && current==4);
	CHECK(clipboard.set && clipboard.port==4 && st_rip3_mouse_count(host.mouse)==2);
	/* PR122 permits an inactive source, independently of destination gates. */
	CHECK(st_rip3_ports_activate(host.ports,false)==0 && st_rip3_ports_switch(host.ports,0)==0);
	style.flags=1|1024;CHECK(st_rip3_button_styles_set(host.button_styles,0,&style)==0);
	command(&host,&errors,"1U5K5K00000000<><>");
	CHECK(errors==2 && st_rip3_mouse_count(host.mouse)==3);
	CHECK(pixel(host.ports,100,100,1)==0 && pixel(host.ports,103,102,7)==0);
	const struct st_rip3_rect small={0,0,1,1},oversized={0,0,3,2},unavailable={5,5,8,7};
	CHECK(st_rip3_ports_define(host.ports,6,&small,true)==0 && st_rip3_ports_switch(host.ports,6)==0);
	for (int y=0;y<2;y++) for (int x=0;x<2;x++) CHECK(st_rip3_ports_plot(host.ports,x,y,(uint8_t)(2+x+2*y),ST_RIP3_COPY)==0);
	CHECK(st_rip3_ports_viewport_device(host.ports,&oversized)==0 && st_rip3_ports_activate(host.ports,false)==0
	    && st_rip3_ports_protect(host.ports,6,true)==0 && st_rip3_ports_switch(host.ports,0)==0);
	clipboard.port=6;
	const struct st_rip3_rect old_ink={200,200,210,210};CHECK(st_rip3_ports_screen_fill(host.ports,&old_ink,9)==0);
	command(&host,&errors,"1UB4B401010000<><>");CHECK(errors==2 && st_rip3_mouse_count(host.mouse)==4);
	for (int y=0;y<3;y++) for (int x=0;x<4;x++)
		CHECK(pixel(host.ports,200+x,200+y,x<2 && y<2 ? (unsigned)(2+x+2*y) : 0)==0);
	CHECK(pixel(host.ports,204,200,9)==0 && pixel(host.ports,200,203,9)==0);
	CHECK(st_rip3_mouse_event(host.mouse,203,202,true) && st_rip3_mouse_event(host.mouse,203,202,false));
	CHECK(!st_rip3_mouse_event(host.mouse,204,202,true) && !st_rip3_mouse_event(host.mouse,204,202,false));
	/* Changing source geometry never reflows the existing hit rectangle. */
	CHECK(st_rip3_ports_protect(host.ports,6,false)==0 && st_rip3_ports_switch(host.ports,6)==0
	    && st_rip3_ports_viewport_device(host.ports,&unavailable)==0 && st_rip3_ports_switch(host.ports,0)==0);
	command(&host,&errors,"1UB4B400000000<><>");CHECK(errors==3 && st_rip3_mouse_count(host.mouse)==4);
	CHECK(st_rip3_mouse_event(host.mouse,203,202,true) && st_rip3_mouse_event(host.mouse,203,202,false));
	st_rip3_mouse_destroy(host.mouse);st_rip3_button_styles_destroy(host.button_styles);
	st_rip3_styles_destroy(host.styles);st_rip3_ports_destroy(host.ports);st_rip3_environments_destroy(host.environments);
	return 0;
}
/* PR123 literal C/P/O fixture and all 256 independent flag results. */
static int deletion_paint(void)
{
	struct st_rip3_command_host host={0};unsigned errors=0;
	struct st_rip3_style drawing;CHECK(st_rip3_style_defaults(&drawing)==0);
	drawing.fill_color=2;drawing.background_color=1;drawing.write_mode=ST_RIP3_XOR;
	memset(drawing.fill.rows,0,8);drawing.fill.rows[0]=0x80;
	drawing.fill.origin=(struct st_rip3_point){3,5}; /* Stored origin must not leak. */
	CHECK(st_rip3_styles_create(&host.styles,&drawing)==0);
	CHECK(st_rip3_environments_create(&host.environments,ST_RIP3_CANVAS_640_480)==0);
	CHECK(st_rip3_ports_create(&host.ports,ST_RIP3_CANVAS_640_480)==0);
	CHECK(st_rip3_palettes_create(&host.palettes)==0);
	const struct st_rip3_mouse_sink sink={0};CHECK(st_rip3_mouse_create(&host.mouse,&sink)==0);
	struct st_rip3_environment env;CHECK(st_rip3_environments_get(host.environments,0,&env)==0);
	env.world_width=1280;env.world_height=960;CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	const struct st_rip3_rect regions[]={{15,25,25,35},{10,20,50,60},{40,30,70,50},
	    {5,15,55,65},{50,25,60,35},{80,80,90,90}};
	const unsigned selections[]={0,3,12,15,48,51,60,63};
	const struct st_rip3_rect screen={0,0,639,479},view={100,100,199,199},offscreen={0,0,7,7};
	CHECK(st_rip3_ports_viewport(host.ports,&view)==0 && st_rip3_ports_activate(host.ports,false)==0);
	CHECK(st_rip3_ports_define(host.ports,4,&offscreen,true)==0 && st_rip3_ports_switch(host.ports,4)==0);
	for (unsigned flags=0;flags<256;flags++) {
		CHECK(st_rip3_ports_screen_fill(host.ports,&screen,9)==0);
		for (unsigned i=0;i<6;i++) CHECK(st_rip3_mouse_define(host.mouse,
		    i%2==0 ? ST_RIP3_MOUSE_SIMPLE : ST_RIP3_MOUSE_BUTTON,NULL,&regions[i],false,false,(int)i,NULL,0,NULL)==0);
		unsigned mask=selections[flags&7];
		if ((flags&8)!=0) mask&=~21u;
		if ((flags&16)!=0) mask&=~42u;
		char wire[]="1k0K142S3C0000";
		CHECK(st_rip3_number_encode(wire+10,4,flags,4,36)==0);
		command(&host,&errors,wire);CHECK(errors==0);
		unsigned deleted=0;for (unsigned i=0;i<6;i++) deleted+=(mask>>i)&1;
		CHECK(st_rip3_mouse_count(host.mouse)==6-deleted);
		for (int y=0;y<100;y++) for (int x=0;x<100;x++) {
			bool painted=false;
			if ((flags&7)!=0 && (flags&96)!=0) {
				if ((flags&128)!=0) painted=x>=10 && x<50 && y>=20 && y<60;
				else for (unsigned i=0;i<6;i++) if ((mask&(1u<<i))!=0
				    && x>=regions[i].x0 && x<regions[i].x1 && y>=regions[i].y0 && y<regions[i].y1) painted=true;
			}
			unsigned color=9;
			if (painted) color=(flags&64)!=0 ? (x%8==0 && y%8==0 ? 2 : 1) : 0;
			CHECK(pixel(host.ports,x,y,color)==0); /* Raw read ignores the inactive viewport. */
		}
		unsigned current;struct st_rip3_style after;
		CHECK(st_rip3_ports_current(host.ports,&current)==0 && current==4);
		CHECK(st_rip3_styles_get(host.styles,0,&after)==0 && after.write_mode==ST_RIP3_XOR
		    && after.fill.origin.x==3 && after.fill.origin.y==5);
		st_rip3_mouse_kill(host.mouse);
	}
	/* Only inks reached by mask bits need palette conversion. A real
	 * binding failure is preflight, before deleting an otherwise eligible field. */
	memset(drawing.fill.rows,255,8);drawing.background_color=0xffffff;
	CHECK(st_rip3_styles_set(host.styles,0,&drawing)==0);
	command(&host,&errors,"1k0K142S3C0061");CHECK(errors==0 && pixel(host.ports,10,20,2)==0);
	memset(drawing.fill.rows,0,8);drawing.fill_color=0xffffff;drawing.background_color=1;
	CHECK(st_rip3_styles_set(host.styles,0,&drawing)==0);
	command(&host,&errors,"1k0K142S3C0061");CHECK(errors==0 && pixel(host.ports,10,20,1)==0);
	memset(drawing.fill.rows,255,8);
	CHECK(st_rip3_styles_set(host.styles,0,&drawing)==0);
	CHECK(st_rip3_mouse_define(host.mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&regions[0],false,false,-1,NULL,0,NULL)==0);
	command(&host,&errors,"1k0K142S3C001T");CHECK(errors==1 && st_rip3_mouse_count(host.mouse)==1);
	st_rip3_mouse_kill(host.mouse);errors=0;
	/* Reverse/empty/collapsed selectors, flag validation before emptiness,
	 * and outside selection beyond the physical canvas. */
	CHECK(st_rip3_mouse_define(host.mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&regions[0],false,false,-1,NULL,0,NULL)==0);
	command(&host,&errors,"1k0K140K3C0073");CHECK(errors==0 && st_rip3_mouse_count(host.mouse)==1);
	command(&host,&errors,"1k0K140L3C0073");CHECK(errors==0 && st_rip3_mouse_count(host.mouse)==1);
	command(&host,&errors,"1k000000000074");CHECK(errors==1 && st_rip3_mouse_count(host.mouse)==1);
	command(&host,&errors,"1k2S3C0K140003");CHECK(errors==1 && st_rip3_mouse_count(host.mouse)==0);
	CHECK(st_rip3_mouse_define(host.mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&regions[0],false,false,-1,NULL,0,NULL)==0);
	command(&host,&errors,"1kZUZUZZZZ004K");CHECK(errors==1 && st_rip3_mouse_count(host.mouse)==0);
	st_rip3_mouse_destroy(host.mouse);st_rip3_palettes_destroy(host.palettes);
	st_rip3_styles_destroy(host.styles);st_rip3_ports_destroy(host.ports);st_rip3_environments_destroy(host.environments);
	return 0;
}
static bool same_style(const struct st_rip3_button_style *a,const struct st_rip3_button_style *b)
{
	return a->width==b->width && a->height==b->height && a->flags==b->flags && a->bevel==b->bevel
	    && a->orientation==b->orientation && a->group==b->group && a->flags2==b->flags2
	    && memcmp(a->colors,b->colors,sizeof(a->colors))==0;
}

static int autoclip_buttons(void)
{
	struct st_rip3_command_host host={0};unsigned errors=0;
	struct st_rip3_style drawing;struct st_rip3_text_context text={.encoding=ST_RIP3_TEXT_CP437};
	struct st_rip3_clipboard_selection clipboard={0};uint64_t budget=100000;
	host.clipboard=&clipboard;host.offscreen_pixel_budget=&budget;host.text=&text;
	CHECK(st_rip3_style_defaults(&drawing)==0 && st_rip3_styles_create(&host.styles,&drawing)==0);
	CHECK(st_rip3_environments_create(&host.environments,ST_RIP3_CANVAS_640_480)==0);
	CHECK(st_rip3_ports_create(&host.ports,ST_RIP3_CANVAS_640_480)==0);
	CHECK(st_rip3_button_styles_create(&host.button_styles)==0 && st_rip3_button_styles_select(host.button_styles,5)==0);
	struct st_rip3_mouse_sink sink={0};CHECK(st_rip3_mouse_create(&host.mouse,&sink)==0);
	struct st_rip3_environment env;CHECK(st_rip3_environments_get(host.environments,0,&env)==0);
	env.world_width=640;env.world_height=480;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	struct st_rip3_button_style original,after,expected;
	CHECK(st_rip3_button_style_defaults(&original)==0);
	original.flags=256|512|64|16|8|32768|1024;original.group=4;
	original.colors[ST_RIP3_BUTTON_LABEL_FOREGROUND]=12;
	for (unsigned protect=0;protect<2;protect++) {
		CHECK(st_rip3_button_styles_protect(host.button_styles,5,false)==0);
		CHECK(st_rip3_button_styles_set(host.button_styles,5,&original)==0);
		CHECK(st_rip3_button_styles_protect(host.button_styles,5,protect!=0)==0);
		budget=100000;unsigned previous=errors;
		command(&host,&errors,"1U0A0K321O0000<>Label<>");CHECK(errors==previous);
		CHECK(clipboard.set && clipboard.port==1 && st_rip3_mouse_count(host.mouse)==1);
		struct st_rip3_port_info port;CHECK(st_rip3_ports_info(host.ports,1,&port)==0 && port.defined);
		CHECK(port.bounds.x1==105 && port.bounds.y1==45
		    && port.viewport.x1==105 && port.viewport.y1==45); /* Exact baked 106x46 crop. */
		for (int y=0;y<46;y++) for (int x=0;x<106;x++) {
			uint8_t value;CHECK(st_rip3_ports_read(host.ports,1,x,y,&value)==0);
			CHECK(value!=0 && value!=12); /* Neither black recess nor later red label. */
		}
		unsigned red=0;
		for (int y=20;y<60;y++) for (int x=10;x<110;x++) {
			uint8_t value;CHECK(st_rip3_ports_read(host.ports,0,x,y,&value)==0);red+=value==12;
		}
		CHECK(red!=0);
		expected=original;expected.flags=1025; /* CLIPBOARD + MOUSE; every other attribute retained. */
		CHECK(st_rip3_button_styles_get(host.button_styles,5,&after)==0 && same_style(&after,&expected));
		struct st_rip3_button_style_info info;
		CHECK(st_rip3_button_styles_info(host.button_styles,5,&info)==0 && info.defined && info.protected==(protect!=0));
		command(&host,&errors,"1U5K5K00000000<><>");CHECK(errors==previous && st_rip3_mouse_count(host.mouse)==2);
		CHECK(pixel(host.ports,200,200,15)==0 && pixel(host.ports,305,245,8)==0 && pixel(host.ports,306,200,0)==0);
		/* A protected clipboard rejects capture after face/effect painting,
		 * without mutating the style or installing a new mouse field. */
		CHECK(st_rip3_button_styles_protect(host.button_styles,5,false)==0
		    && st_rip3_button_styles_set(host.button_styles,5,&original)==0
		    && st_rip3_button_styles_protect(host.button_styles,5,protect!=0)==0);
		CHECK(st_rip3_ports_protect(host.ports,1,true)==0);
		command(&host,&errors,"1U8C2SB43W0000<>Label<>");CHECK(errors==previous+1);
		CHECK(st_rip3_button_styles_get(host.button_styles,5,&after)==0 && same_style(&after,&original));
		CHECK(clipboard.set && clipboard.port==1 && st_rip3_mouse_count(host.mouse)==2 && pixel(host.ports,330,120,7)==0);
		/* Budget failure follows the shared destructive replacement contract. */
		CHECK(st_rip3_ports_protect(host.ports,1,false)==0);budget=0;
		command(&host,&errors,"1U8C2SB43W0000<>Label<>");CHECK(errors==previous+2);
		CHECK(st_rip3_button_styles_get(host.button_styles,5,&after)==0 && same_style(&after,&original));
		CHECK(!clipboard.set && st_rip3_ports_info(host.ports,1,&port)==0 && !port.defined && st_rip3_mouse_count(host.mouse)==2);
		st_rip3_mouse_kill(host.mouse);
	}
	const struct st_rip3_rect tiny={0,0,0,0};
	for (unsigned i=1;i<36;i++) CHECK(st_rip3_ports_define(host.ports,i,&tiny,true)==0);
	budget=100000;unsigned previous=errors;
	command(&host,&errors,"1U0A0K321O0000<>Label<>");CHECK(errors==previous+1 && !clipboard.set);
	CHECK(st_rip3_button_styles_get(host.button_styles,5,&after)==0 && same_style(&after,&original));
	/* The documented same-destination guard skips capture before deletion,
	 * allocation and converted-style writes; the label remains on PORT6. */
	for (unsigned i=1;i<36;i++) CHECK(st_rip3_ports_delete(host.ports,i)==0);
	const struct st_rip3_rect block={0,0,31,31};
	CHECK(st_rip3_ports_fill(host.ports,&block,3,ST_RIP3_COPY)==0);
	CHECK(st_rip3_ports_define(host.ports,6,&block,true)==0 && st_rip3_ports_switch(host.ports,6)==0);
	CHECK(st_rip3_ports_fill(host.ports,&block,4,ST_RIP3_COPY)==0);
	clipboard=(struct st_rip3_clipboard_selection){true,6};original.flags=256|64;
	original.bevel=0;
	CHECK(st_rip3_button_styles_protect(host.button_styles,5,false)==0
	    && st_rip3_button_styles_set(host.button_styles,5,&original)==0);
	uint64_t generation=st_rip3_ports_generation(host.ports,6);
	command(&host,&errors,"1U00000K0G0000<>A<>");CHECK(errors==previous+1);
	unsigned current;CHECK(st_rip3_ports_current(host.ports,&current)==0 && current==6 && clipboard.set && clipboard.port==6);
	CHECK(st_rip3_ports_generation(host.ports,6)==generation);
	CHECK(st_rip3_button_styles_get(host.button_styles,5,&after)==0 && same_style(&after,&original));
	unsigned red=0;
	for (int y=0;y<16;y++) for (int x=0;x<20;x++) {
		uint8_t value;CHECK(st_rip3_ports_read(host.ports,6,x,y,&value)==0);red+=value==12;
		CHECK(pixel(host.ports,x,y,3)==0);
	}
	CHECK(red!=0 && st_rip3_ports_switch(host.ports,0)==0);
	/* Font realization belongs to the later label stage. A missing face
	 * cannot roll back a successful capture or the style's captured flags. */
	original.flags=256|64;
	CHECK(st_rip3_button_styles_set(host.button_styles,5,&original)==0);
	drawing.font_mode=ST_RIP3_FONT_OUTLINE;
	memcpy(drawing.outline.name,"NoSuchFont",11);
	CHECK(st_rip3_styles_set(host.styles,0,&drawing)==0);
	command(&host,&errors,"1U14141O1K0000<>Label<>");CHECK(errors==previous+2);
	CHECK(st_rip3_button_styles_get(host.button_styles,5,&after)==0 && after.flags==1);
	struct st_rip3_port_info port;
	CHECK(st_rip3_ports_info(host.ports,6,&port)==0 && port.bounds.x1==19 && port.bounds.y1==15);
	/* CLIPBOARD wins over PLAIN and may itself auto-capture; the already
	 * copied image survives replacement of its old clipboard source. */
	original.flags=1|64|256;
	CHECK(st_rip3_button_styles_set(host.button_styles,5,&original)==0);
	command(&host,&errors,"1U282800000000<><>");CHECK(errors==previous+2);
	CHECK(st_rip3_ports_info(host.ports,6,&port)==0 && port.bounds.x1==19 && port.bounds.y1==15);
	for (int y=0;y<16;y++) for (int x=0;x<20;x++) {
		uint8_t value;CHECK(st_rip3_ports_read(host.ports,6,x,y,&value)==0 && value==7);
	}
	/* Even an exhausted budget cannot make the guarded path destructive. */
	original.flags=256|64;
	CHECK(st_rip3_button_styles_set(host.button_styles,5,&original)==0 && st_rip3_ports_switch(host.ports,6)==0);
	budget=0;generation=st_rip3_ports_generation(host.ports,6);
	command(&host,&errors,"1U00000K0G0000<><>");CHECK(errors==previous+2);
	CHECK(st_rip3_ports_current(host.ports,&current)==0 && current==6 && clipboard.set);
	CHECK(st_rip3_ports_info(host.ports,6,&port)==0 && port.defined
	    && st_rip3_ports_generation(host.ports,6)==generation);
	CHECK(st_rip3_button_styles_get(host.button_styles,5,&after)==0 && same_style(&after,&original));
	CHECK(st_rip3_ports_switch(host.ports,0)==0);
	budget=100000;
	/* Missing capture bindings are preflight failures, before even the face. */
	original.flags=256|64;
	CHECK(st_rip3_button_styles_set(host.button_styles,5,&original)==0);
	const struct st_rip3_rect preflight={120,40,139,55};
	CHECK(st_rip3_ports_fill(host.ports,&preflight,3,ST_RIP3_COPY)==0);
	host.offscreen_pixel_budget=NULL;
	command(&host,&errors,"1U3C143W1K0000<><>");CHECK(errors==previous+3 && pixel(host.ports,130,45,3)==0);
	host.offscreen_pixel_budget=&budget;host.clipboard=NULL;
	command(&host,&errors,"1U3C143W1K0000<><>");CHECK(errors==previous+4 && pixel(host.ports,130,45,3)==0);
	st_rip3_mouse_destroy(host.mouse);st_rip3_button_styles_destroy(host.button_styles);
	st_rip3_styles_destroy(host.styles);st_rip3_ports_destroy(host.ports);st_rip3_environments_destroy(host.environments);
	return 0;
}

struct staged {
	struct st_rip3_command_host host;struct st_rip3_text_context text;
	struct st_rip3_graphical *worker;struct st_rip3_user_variables *user;
	struct st_rip3_variables *session,*persistent;
	struct st_rip3_host_variables variables;struct st_rip3_table_host tables;
	struct st_rip3_templates *slots;
	struct st_rip3_host_sink expansion_sink;struct st_rip3_frame frame;
	struct st_rip3_clipboard_selection clipboard;uint64_t budget;
	unsigned errors,actions,formats;char value[256],program[64];
};
static void staged_error(void *ctx) {((struct staged *)ctx)->errors++;}
static void staged_expansion_error(void *ctx,enum st_rip3_host_diagnostic kind) {(void)kind;staged_error(ctx);}
static bool staged_busy(void *ctx) {return st_rip3_button_renderer_pending(((struct staged *)ctx)->host.buttons);}
static int staged_format(void *ctx,const struct st_rip3_variable_reference *reference,
    enum st_rip3_user_variable_source source,const struct st_rip3_variable_value *value,struct st_rip3_variable_value *out)
{
	(void)reference;(void)source;struct staged *f=ctx;f->formats++;
	memcpy(f->value,value->bytes,value->length);f->value[value->length]=0;*out=*value;return 0;
}
static void staged_activate(void *ctx,const uint8_t *program,size_t length,bool reset,int identity,bool pointer,int32_t x,int32_t y)
{
	(void)reset;(void)identity;(void)pointer;(void)x;(void)y;
	struct staged *f=ctx;f->actions++;
	if (length>=sizeof(f->program)) {f->errors++;return;}
	memcpy(f->program,program,length);f->program[length]=0;
}
static int staged_create(struct staged *f,bool converted)
{
	struct st_rip3_style drawing;CHECK(st_rip3_style_defaults(&drawing)==0);
	CHECK(st_rip3_styles_create(&f->host.styles,&drawing)==0);
	CHECK(st_rip3_environments_create(&f->host.environments,ST_RIP3_CANVAS_640_480)==0);
	CHECK(st_rip3_ports_create(&f->host.ports,ST_RIP3_CANVAS_640_480)==0);
	CHECK(st_rip3_button_styles_create(&f->host.button_styles)==0 && st_rip3_palettes_create(&f->host.palettes)==0);
	struct st_rip3_environment env;CHECK(st_rip3_environments_get(f->host.environments,0,&env)==0);
	env.world_height=480;CHECK(st_rip3_environments_set(f->host.environments,0,&env)==0);
	struct st_rip3_button_style style;CHECK(st_rip3_button_styles_get(f->host.button_styles,0,&style)==0);
	style.flags=256|1024|2;style.width=style.height=style.bevel=0;style.colors[ST_RIP3_BUTTON_SURFACE]=3;
	style.colors[ST_RIP3_BUTTON_LABEL_FOREGROUND]=12;CHECK(st_rip3_button_styles_set(f->host.button_styles,0,&style)==0);
	f->text.encoding=converted ? ST_RIP3_TEXT_UTF8 : ST_RIP3_TEXT_CP437;f->host.text=&f->text;
	f->host.clipboard=&f->clipboard;f->budget=100000;f->host.offscreen_pixel_budget=&f->budget;
	const struct st_rip3_mouse_sink mouse={.context=f,.activate=staged_activate,.busy=staged_busy,
	    .resource_failure=staged_error,.render_failure=staged_error};
	CHECK(st_rip3_mouse_create(&f->host.mouse,&mouse)==0 && st_rip3_templates_create(&f->slots)==0);
	st_rip3_mouse_bind_templates(f->host.mouse,f->slots);
	CHECK(st_rip3_variables_create(&f->session)==0 && st_rip3_variables_create(&f->persistent)==0);
	const struct st_rip3_user_variable_host user={.session=f->session,.persistent=f->persistent,.context=f,.format=staged_format};
	CHECK(st_rip3_user_variables_create(&f->user,&user)==0);
	f->tables.tables[ST_RIP3_TABLE_ENV].live=f->host.environments;f->tables.tables[ST_RIP3_TABLE_PORT].live=f->host.ports;
	f->tables.tables[ST_RIP3_TABLE_BUT].live=f->host.button_styles;f->tables.tables[ST_RIP3_TABLE_STYLE].live=f->host.styles;
	CHECK(st_rip3_frame_init(&f->frame,ST_RIP3_CANVAS_640_480)==0);
	f->variables=(struct st_rip3_host_variables){.frame=&f->frame,.tables=&f->tables,.user_variables=f->user};
	f->expansion_sink=(struct st_rip3_host_sink){.context=f,.diagnostic=staged_expansion_error};
	CHECK(st_rip3_graphical_create(&f->worker,&f->variables,&f->expansion_sink,converted)==0);
	const struct st_rip3_command_sink sink={&f->errors,diagnostic};
	CHECK(st_rip3_button_renderer_create(&f->host.buttons,&f->host,&sink,f->worker,converted)==0);return 0;
}
static void staged_destroy(struct staged *f)
{
	st_rip3_button_renderer_cancel(f->host.buttons);st_rip3_mouse_destroy(f->host.mouse);
	st_rip3_templates_destroy(f->slots);
	st_rip3_button_renderer_destroy(f->host.buttons);st_rip3_graphical_destroy(f->worker);
	st_rip3_user_variables_destroy(f->user);st_rip3_variables_destroy(f->session);st_rip3_variables_destroy(f->persistent);
	st_rip3_ports_destroy(f->host.ports);st_rip3_palettes_destroy(f->host.palettes);
	st_rip3_button_styles_destroy(f->host.button_styles);st_rip3_styles_destroy(f->host.styles);st_rip3_environments_destroy(f->host.environments);
}
static void staged_pump(struct staged *f)
{
	for (unsigned i=0;i<10000;i++) {
		if (st_rip3_button_renderer_pending(f->host.buttons)) st_rip3_button_renderer_process(f->host.buttons,1);
		else if (st_rip3_mouse_pending(f->host.mouse)) st_rip3_mouse_resume(f->host.mouse);
		else break;
		if (st_rip3_graphical_state(f->worker)==ST_RIP3_GRAPHICAL_PROMPT) break;
	}
}
static void staged_start(struct staged *f,const char *command)
{
	st_rip3_button_renderer_start(f->host.buttons,(const uint8_t *)command,strlen(command));staged_pump(f);
}
static int staged_buttons(void)
{
	struct staged f={0};CHECK(staged_create(&f,false)==0);
	staged_start(&f,"1U0A0K1O140000$WORLDW(0,1280)$<>$RESX$<>NO");
	CHECK(f.errors==0 && st_rip3_mouse_count(f.host.mouse)==1 && f.actions==0);
	struct st_rip3_environment env;CHECK(st_rip3_environments_get(f.host.environments,0,&env)==0 && env.world_width==1280);
	CHECK(pixel(f.host.ports,59,39,3)==0 && pixel(f.host.ports,60,39,0)==0);staged_destroy(&f);
	f=(struct staged){0};CHECK(staged_create(&f,false)==0);
	struct st_rip3_button_style style;CHECK(st_rip3_button_styles_get(f.host.button_styles,0,&style)==0);
	style.flags=256|1024|64;CHECK(st_rip3_button_styles_set(f.host.button_styles,5,&style)==0
	    && st_rip3_button_styles_select(f.host.button_styles,5)==0 && st_rip3_button_styles_protect(f.host.button_styles,5,true)==0);
	staged_start(&f,"1U0A0K1O140000<>$&MISSING$<>$MKILL$");
	CHECK(st_rip3_graphical_state(f.worker)==ST_RIP3_GRAPHICAL_PROMPT && f.clipboard.set
	    && st_rip3_mouse_count(f.host.mouse)==0 && f.actions==0);
	CHECK(st_rip3_button_styles_get(f.host.button_styles,5,&style)==0 && style.flags==(1024|1));
	uint64_t captured=st_rip3_ports_generation(f.host.ports,f.clipboard.port);
	st_rip3_user_variables_respond(f.user,(const uint8_t *)"Ready",5,false);staged_pump(&f);
	CHECK(f.errors==0 && f.formats==1 && st_rip3_mouse_count(f.host.mouse)==1
	    && st_rip3_ports_generation(f.host.ports,f.clipboard.port)==captured && f.actions==0);
	for (int y=0;y<20;y++) for (int x=0;x<50;x++) {
		uint8_t value;CHECK(st_rip3_ports_read(f.host.ports,f.clipboard.port,x,y,&value)==0 && value==3);
	}
	staged_destroy(&f);f=(struct staged){0};CHECK(staged_create(&f,false)==0);
	CHECK(st_rip3_variables_set(f.session,"LABEL",5,(const uint8_t *)"One<>Two",8)==0);
	staged_start(&f,"1U0A0K1O141T00ID=7:<>$&LABEL$<>go");
	CHECK(f.errors==0 && f.formats==1 && strcmp(f.value,"One<>Two")==0 && st_rip3_mouse_count(f.host.mouse)==1);
	CHECK(st_rip3_variables_set(f.session,"LABEL",5,(const uint8_t *)"Two",3)==0);
	CHECK(st_rip3_mouse_event(f.host.mouse,20,30,true));staged_pump(&f);
	CHECK(f.formats==2 && strcmp(f.value,"Two")==0);
	CHECK(st_rip3_mouse_event(f.host.mouse,20,30,false));staged_pump(&f);
	CHECK(f.errors==0 && f.actions==1 && strcmp(f.program,"go")==0 && f.formats==3);
	CHECK(st_rip3_variables_remove(f.session,"LABEL",5)==0 && st_rip3_mouse_key(f.host.mouse,'a'));
	staged_pump(&f);CHECK(st_rip3_graphical_state(f.worker)==ST_RIP3_GRAPHICAL_PROMPT && f.actions==1);
	st_rip3_mouse_kill(f.host.mouse);CHECK(st_rip3_graphical_state(f.worker)==ST_RIP3_GRAPHICAL_IDLE);
	const struct st_rip3_rect replacement={10,20,60,40};const struct st_rip3_mouse_binding key={.hotkey='A'};
	CHECK(st_rip3_mouse_define(f.host.mouse,ST_RIP3_MOUSE_BUTTON,&key,&replacement,false,false,7,(const uint8_t *)"new",3,NULL)==0);
	staged_pump(&f);CHECK(f.errors==0 && f.actions==2 && strcmp(f.program,"go")==0 && f.formats==3);
	staged_pump(&f);CHECK(f.actions==2);staged_destroy(&f);
	f=(struct staged){0};CHECK(staged_create(&f,false)==0);
	staged_start(&f,"1U0A0K1O140000$&FILE$<>Label<>old");
	CHECK(st_rip3_graphical_state(f.worker)==ST_RIP3_GRAPHICAL_PROMPT && st_rip3_mouse_count(f.host.mouse)==0);
	CHECK(st_rip3_ports_reset(f.host.ports,false)==0);
	const struct st_rip3_rect scene={0,0,639,479};CHECK(st_rip3_ports_screen_fill(f.host.ports,&scene,9)==0);
	st_rip3_user_variables_respond(f.user,(const uint8_t *)"Name",4,false);staged_pump(&f);
	CHECK(!st_rip3_button_renderer_pending(f.host.buttons) && st_rip3_mouse_count(f.host.mouse)==0
	    && pixel(f.host.ports,20,30,9)==0 && f.errors==0);staged_destroy(&f);
	return 0;
}

/* Independent PR129 geometry/provider vectors; no emulator-derived raster. */
static int staged_auto_stamp(void)
{
	struct staged f={0};CHECK(staged_create(&f,false)==0);
	struct st_rip3_button_style style;CHECK(st_rip3_button_styles_get(f.host.button_styles,0,&style)==0);
	style.flags=256|1024|64|512|2;style.bevel=3;style.flags2=8;
	CHECK(st_rip3_button_styles_set(f.host.button_styles,0,&style)==0);
	CHECK(st_rip3_variables_set(f.session,"FILE",4,(const uint8_t *)"UNUSED",6)==0
	    && st_rip3_variables_set(f.session,"LABEL",5,(const uint8_t *)"Old",3)==0);
	staged_start(&f,"1U2S1E3C1O0000ID=9:$&FILE$<>$&LABEL$<>go");
	CHECK(f.errors==0 && f.formats==2 && f.actions==0 && st_rip3_mouse_count(f.host.mouse)==1);
	struct st_rip3_port_info info;CHECK(st_rip3_ports_info(f.host.ports,f.clipboard.port,&info)==0
	    && info.viewport.x0==0 && info.viewport.y0==0 && info.viewport.x1==25 && info.viewport.y1==15);
	/* LEFT anchor is F.left + 10 = 110, not C.left + 10 = 107. */
	unsigned red=0;
	for (int y=40;y<70;y++) for (int x=97;x<150;x++) {
		uint8_t value;CHECK(st_rip3_ports_read(f.host.ports,0,x,y,&value)==0);
		if (value==12) {CHECK(x>=110);red++;}
	}
	CHECK(red!=0);
	/* Replacement source is partially backed: its 16x10 viewport supplies
	 * 9x5 pixels; the same C gets COPY padding without moving F or H. */
	const struct st_rip3_rect backing={0,0,13,11},view={5,7,20,16};
	CHECK(st_rip3_ports_define(f.host.ports,7,&backing,true)==0 && st_rip3_ports_switch(f.host.ports,7)==0
	    && st_rip3_ports_fill(f.host.ports,&backing,4,ST_RIP3_COPY)==0
	    && st_rip3_ports_viewport_device(f.host.ports,&view)==0 && st_rip3_ports_activate(f.host.ports,false)==0
	    && st_rip3_ports_protect(f.host.ports,7,true)==0 && st_rip3_ports_switch(f.host.ports,0)==0);
	f.clipboard.port=7;
	CHECK(st_rip3_variables_set(f.session,"LABEL",5,(const uint8_t *)" ",1)==0);
	CHECK(st_rip3_mouse_event(f.host.mouse,98,48,true));staged_pump(&f);
	CHECK(f.errors==0 && f.formats==4 && f.actions==0);
	CHECK(st_rip3_mouse_event(f.host.mouse,98,48,false));staged_pump(&f);
	CHECK(f.errors==0 && f.formats==6 && f.actions==1 && strcmp(f.program,"go")==0);
	for (int y=47;y<63;y++) for (int x=97;x<123;x++) CHECK(pixel(f.host.ports,x,y,x<106 && y<52 ? 4 : 0)==0);
	CHECK(!st_rip3_mouse_event(f.host.mouse,123,48,true));
	CHECK(!st_rip3_mouse_event(f.host.mouse,123,48,false));
	/* A positive source crop with no backing fails before LABEL; the host
	 * action still commits once after optional redraw failure. */
	CHECK(st_rip3_ports_protect(f.host.ports,7,false)==0 && st_rip3_ports_switch(f.host.ports,7)==0);
	const struct st_rip3_rect unavailable={40,40,60,60};
	CHECK(st_rip3_ports_viewport_device(f.host.ports,&unavailable)==0 && st_rip3_ports_switch(f.host.ports,0)==0);
	CHECK(st_rip3_mouse_event(f.host.mouse,98,48,true));staged_pump(&f);
	CHECK(f.formats==7 && f.errors==1);
	CHECK(st_rip3_mouse_event(f.host.mouse,98,48,false));staged_pump(&f);
	CHECK(f.formats==8 && f.errors==2 && f.actions==2);
	staged_destroy(&f);
	/* Bevel outside the effective viewport is padding at its C-relative
	 * offset, not a stretched/shifted source or a helper allocation pixel. */
	f=(struct staged){0};CHECK(staged_create(&f,false)==0);
	CHECK(st_rip3_button_styles_get(f.host.button_styles,0,&style)==0);style.flags=256|64|512;style.bevel=3;
	CHECK(st_rip3_button_styles_set(f.host.button_styles,0,&style)==0);
	staged_start(&f,"1U00000K0A0000<><>");
	CHECK(f.errors==0 && st_rip3_ports_info(f.host.ports,f.clipboard.port,&info)==0
	    && info.viewport.x1==25 && info.viewport.y1==15);
	for (int y=0;y<16;y++) for (int x=0;x<26;x++) {
		uint8_t value;CHECK(st_rip3_ports_read(f.host.ports,f.clipboard.port,x,y,&value)==0);
		if (x<3 || y<3) CHECK(value==0);
		else if (x<23 && y<13) CHECK(value==3);
	}
	staged_destroy(&f);
	/* Same-target nonmouse capture is ignored even with zero budget. A
	 * suspended label stays on that original generation and never PORT0. */
	f=(struct staged){0};CHECK(staged_create(&f,false)==0);
	const struct st_rip3_rect bitmap={0,0,63,63};
	CHECK(st_rip3_ports_define(f.host.ports,6,&bitmap,true)==0 && st_rip3_ports_switch(f.host.ports,6)==0);
	f.clipboard=(struct st_rip3_clipboard_selection){true,6};f.budget=0;
	CHECK(st_rip3_button_styles_get(f.host.button_styles,0,&style)==0);style.flags=256|64;
	CHECK(st_rip3_button_styles_set(f.host.button_styles,0,&style)==0);
	uint64_t generation=st_rip3_ports_generation(f.host.ports,6);
	staged_start(&f,"1U0A0K1O140000<>$&MISSING$<>");
	CHECK(f.errors==0 && st_rip3_graphical_state(f.worker)==ST_RIP3_GRAPHICAL_PROMPT
	    && st_rip3_ports_generation(f.host.ports,6)==generation);
	CHECK(st_rip3_button_styles_get(f.host.button_styles,0,&style)==0 && style.flags==(256|64));
	st_rip3_user_variables_respond(f.user,(const uint8_t *)"A",1,false);staged_pump(&f);
	unsigned current;CHECK(st_rip3_ports_current(f.host.ports,&current)==0 && current==6 && f.formats==1 && f.errors==0);
	red=0;for (int y=20;y<40;y++) for (int x=10;x<60;x++) {
		uint8_t value;CHECK(st_rip3_ports_read(f.host.ports,6,x,y,&value)==0);red+=value==12;
		CHECK(pixel(f.host.ports,x,y,0)==0);
	}
	CHECK(red!=0 && st_rip3_mouse_count(f.host.mouse)==0);
	staged_start(&f,"1U0A0K1O140000<>$&STALE$<>");
	CHECK(st_rip3_graphical_state(f.worker)==ST_RIP3_GRAPHICAL_PROMPT);
	CHECK(st_rip3_ports_define(f.host.ports,6,&bitmap,true)==0);
	staged_pump(&f);
	CHECK(!st_rip3_button_renderer_pending(f.host.buttons) && st_rip3_graphical_state(f.worker)==ST_RIP3_GRAPHICAL_IDLE
	    && f.formats==1 && f.errors==0 && st_rip3_mouse_count(f.host.mouse)==0);
	for (int y=20;y<40;y++) for (int x=10;x<60;x++) {
		uint8_t value;CHECK(st_rip3_ports_read(f.host.ports,6,x,y,&value)==0 && value==0);
		CHECK(pixel(f.host.ports,x,y,0)==0);
	}
	staged_destroy(&f);f=(struct staged){0};CHECK(staged_create(&f,false)==0);
	staged_start(&f,"1U0A0K1O140000<>$&CAPACITY$<>never");
	CHECK(st_rip3_graphical_state(f.worker)==ST_RIP3_GRAPHICAL_PROMPT);
	const struct st_rip3_rect field={0,0,1,1};
	for (unsigned i=0;i<128;i++) CHECK(st_rip3_mouse_define(f.host.mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&field,
	    false,false,(int)i,(const uint8_t *)"old",3,NULL)==0);
	st_rip3_user_variables_respond(f.user,(const uint8_t *)"A",1,false);staged_pump(&f);
	CHECK(f.errors==1 && f.actions==0 && st_rip3_mouse_count(f.host.mouse)==128
	    && !st_rip3_button_renderer_pending(f.host.buttons));staged_destroy(&f);
	return 0;
}

static int initially_selected_faces(void)
{
	for (unsigned type=0;type<3;type++) {
		struct staged f={0};CHECK(staged_create(&f,false)==0);
		struct st_rip3_button_style style;CHECK(st_rip3_button_styles_get(f.host.button_styles,0,&style)==0);
		style.flags=(type==1 ? 1u : 256u|512u|16u)|1024u|2u|(type==2 ? 64u : 0u);style.bevel=3;style.flags2=1;
		CHECK(st_rip3_button_styles_set(f.host.button_styles,0,&style)==0);
		if (type==1) {
			const struct st_rip3_rect source={0,0,3,2},view={0,0,5,4};
			CHECK(st_rip3_ports_define(f.host.ports,6,&source,true)==0 && st_rip3_ports_switch(f.host.ports,6)==0
			    && st_rip3_ports_fill(f.host.ports,&source,3,ST_RIP3_COPY)==0
			    && st_rip3_ports_viewport_device(f.host.ports,&view)==0 && st_rip3_ports_activate(f.host.ports,false)==0
			    && st_rip3_ports_protect(f.host.ports,6,true)==0 && st_rip3_ports_switch(f.host.ports,0)==0);
			f.clipboard=(struct st_rip3_clipboard_selection){true,6};
		}
		staged_start(&f,"1U0A0K1O141T10ID=12:<><>opaque");
		CHECK(f.errors==0 && f.actions==0 && st_rip3_mouse_count(f.host.mouse)==1);
		CHECK(pixel(f.host.ports,10,20,12)==0);
		if (type==2) {
			uint8_t value;CHECK(f.clipboard.set && st_rip3_ports_read(f.host.ports,f.clipboard.port,3,3,&value)==0 && value==3);
		}
		if (type!=1) CHECK(pixel(f.host.ports,7,25,0)==0 && pixel(f.host.ports,5,25,8)==0);
		else CHECK(pixel(f.host.ports,14,20,15)==0 && pixel(f.host.ports,10,23,15)==0);
		CHECK(st_rip3_mouse_event(f.host.mouse,11,21,true));staged_pump(&f);
		CHECK(f.errors==0 && f.actions==0 && pixel(f.host.ports,10,20,3)==0);
		CHECK(st_rip3_mouse_event(f.host.mouse,100,100,false));staged_pump(&f);
		CHECK(f.errors==0 && f.actions==0 && pixel(f.host.ports,10,20,12)==0);
		CHECK(st_rip3_mouse_key(f.host.mouse,'a'));staged_pump(&f);
		CHECK(f.errors==0 && f.actions==1 && strcmp(f.program,"opaque")==0 && pixel(f.host.ports,10,20,3)==0);
		if (type==1) {
			uint8_t value;CHECK(st_rip3_ports_read(f.host.ports,6,0,0,&value)==0 && value==3);
			CHECK(pixel(f.host.ports,14,20,0)==0 && pixel(f.host.ports,10,23,0)==0);
		}
		staged_destroy(&f);
	}
	return 0;
}

int main(void)
{
	CHECK(initially_selected_faces()==0 && staged_auto_stamp()==0 && staged_buttons()==0 && payloads()==0 && clipboard_buttons()==0 && autoclip_buttons()==0 && deletion_paint()==0 && mouse_pointers()==0);
	struct st_rip3_command_host host={0};unsigned errors=0;
	struct st_rip3_style drawing;struct st_rip3_text_context text={.encoding=ST_RIP3_TEXT_CP437};
	CHECK(st_rip3_style_defaults(&drawing)==0 && st_rip3_styles_create(&host.styles,&drawing)==0);
	CHECK(st_rip3_environments_create(&host.environments,ST_RIP3_CANVAS_640_480)==0);
	CHECK(st_rip3_ports_create(&host.ports,ST_RIP3_CANVAS_640_480)==0);
	CHECK(st_rip3_button_styles_create(&host.button_styles)==0);host.text=&text;
	CHECK(st_rip3_palettes_create(&host.palettes)==0);
	struct st_rip3_environment env;
	CHECK(st_rip3_environments_get(host.environments,0,&env)==0);
	env.world_width=640;env.world_height=480;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	struct st_rip3_button_style style;
	CHECK(st_rip3_button_styles_get(host.button_styles,0,&style)==0);
	style.flags=256;style.colors[ST_RIP3_BUTTON_SURFACE]=3;
	CHECK(st_rip3_button_styles_set(host.button_styles,0,&style)==0);
	command(&host,&errors,"1U0A0K1O140000<><>");CHECK(errors==0);
	CHECK(pixel(host.ports,10,20,3)==0 && pixel(host.ports,59,39,3)==0);
	CHECK(pixel(host.ports,9,20,0)==0 && pixel(host.ports,60,40,0)==0);
	/* Literal radix-64, five-digit XY input uses the live ENV, not the
	 * button-style definition's earlier wire radix/coordinate width. */
	env.base_math=64;env.coordinate_size=5;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	command(&host,&errors,"1U0000A0000K0000U0000e0000<><>");CHECK(errors==0);
	env.base_math=36;env.coordinate_size=2;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	/* Raw payload failures are pure preflight: not even the face is painted. */
	const struct st_rip3_rect preflight_area={10,20,59,39};unsigned invalid=0;
	CHECK(st_rip3_ports_fill(host.ports,&preflight_area,6,ST_RIP3_COPY)==0);
	command(&host,&invalid,"1U0A0K1O140000");
	command(&host,&invalid,"1U0A0K1O140000bad/name<>label<>host");
	char excessive[300];const char *label_prefix="1U0A0K1O140000<>";
	size_t prefix_length=strlen(label_prefix);memcpy(excessive,label_prefix,prefix_length);
	memset(excessive+prefix_length,'X',256);excessive[prefix_length+256]=0;
	command(&host,&invalid,excessive);
	CHECK(invalid==3 && pixel(host.ports,10,20,6)==0 && pixel(host.ports,59,39,6)==0);
	/* Complete fixed size ignores both far coordinates and never rescales
	 * stored device dimensions after a WORLD change. A mixed size is dynamic. */
	style.width=30;style.height=10;style.colors[ST_RIP3_BUTTON_SURFACE]=4;
	CHECK(st_rip3_button_styles_set(host.button_styles,0,&style)==0);
	env.world_width=1280;env.world_height=960;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	command(&host,&errors,"1U5K5K00000000<><>");CHECK(errors==0);
	CHECK(pixel(host.ports,100,100,4)==0 && pixel(host.ports,129,109,4)==0 && pixel(host.ports,130,110,0)==0);
	style.width=0;style.flags=768;style.bevel=3;style.colors[ST_RIP3_BUTTON_SURFACE]=7;
	CHECK(st_rip3_button_styles_set(host.button_styles,0,&style)==0);
	env.world_width=640;env.world_height=480;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	command(&host,&errors,"1U0A0K1O140000<><>");CHECK(errors==0);
	CHECK(pixel(host.ports,10,17,15)==0 && pixel(host.ports,7,20,15)==0);
	CHECK(pixel(host.ports,62,20,8)==0 && pixel(host.ports,10,42,8)==0);
	const struct st_rip3_rect sizing_area={400,300,559,379};
	for (unsigned i=0;i<4;i++) {
		style.flags=256;style.width=(i&1)!=0 ? 30 : 0;style.height=(i&2)!=0 ? 10 : 0;
		style.colors[ST_RIP3_BUTTON_SURFACE]=4;
		CHECK(st_rip3_button_styles_set(host.button_styles,0,&style)==0);
		CHECK(st_rip3_ports_fill(host.ports,&sizing_area,0,ST_RIP3_COPY)==0);
		command(&host,&errors,"1UB48CFKAK0000<><>");CHECK(errors==0);
		CHECK(pixel(host.ports,429,309,4)==0);
		CHECK(pixel(host.ports,559,379,i==3 ? 0 : 4)==0);
	}
	style.colors[ST_RIP3_BUTTON_SURFACE]=7;
	/* Drawing permits protected configuration without editing it. */
	CHECK(st_rip3_button_styles_select(host.button_styles,5)==0);
	style.flags=256;style.width=style.height=0;
	CHECK(st_rip3_button_styles_set(host.button_styles,5,&style)==0);
	CHECK(st_rip3_button_styles_protect(host.button_styles,5,true)==0);
	struct st_rip3_cursor cursor={123,234};
	CHECK(st_rip3_ports_cursor(host.ports,0,&cursor)==0);
	command(&host,&errors,"1U2S2S5K3C0000<>Label<>");CHECK(errors==0);
	struct st_rip3_port_info port;CHECK(st_rip3_ports_info(host.ports,0,&port)==0);
	CHECK(port.cursor.x==123 && port.cursor.y==234 && text.outline_cache==NULL);
	struct st_rip3_button_style_info info;CHECK(st_rip3_button_styles_info(host.button_styles,5,&info)==0 && info.protected);
	command(&host,&errors,"1Ubad");CHECK(errors==1);
	/* All flags2 combinations on decorations accept inapplicable mouse bits.
	 * NOFILL overrides PATTERN, whose phase stays in the drawing context. */
	CHECK(st_rip3_button_styles_select(host.button_styles,0)==0);
	const unsigned rows[8]={0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa};
	const struct st_rip3_point origin={0,0};
	CHECK(st_rip3_fill_custom(&drawing.fill,rows,&origin)==0);
	drawing.fill_color=2;drawing.background_color=1;
	CHECK(st_rip3_styles_set(host.styles,0,&drawing)==0);
	const struct st_rip3_rect area={10,20,59,39};
	for (unsigned i=0;i<256;i++) {
		style.flags=256;style.flags2=(uint8_t)i;
		CHECK(st_rip3_button_styles_set(host.button_styles,0,&style)==0);
		CHECK(st_rip3_ports_fill(host.ports,&area,6,ST_RIP3_COPY)==0);
		command(&host,&errors,"1U0A0K1O140000<><>");CHECK(errors==1);
		CHECK(pixel(host.ports,10,20,(i&32)!=0 ? 6 : (i&64)!=0 ? 2 : 7)==0);
		CHECK(pixel(host.ports,11,20,(i&32)!=0 ? 6 : (i&64)!=0 ? 1 : 7)==0);
	}
	struct st_rip3_mouse_sink mouse_sink={0};
	CHECK(st_rip3_mouse_create(&host.mouse,&mouse_sink)==0);
	style.flags=256|1024|2048;style.flags2=0;style.colors[ST_RIP3_BUTTON_UNDERLINE]=14;
	CHECK(st_rip3_button_styles_set(host.button_styles,0,&style)==0);
	command(&host,&errors,"1U0A0K1O141T00<>Aaa<>");CHECK(errors==1);
	unsigned yellow=0;
	for (int y=20;y<40;y++) for (int x=10;x<60;x++) {
		uint8_t value;CHECK(st_rip3_ports_read(host.ports,0,x,y,&value)==0);
		if (value==14) {CHECK(x>=23 && x<31);yellow++;}
	}
	CHECK(yellow==8); /* First matching hot character only, native font width 8. */
	style.flags=256|1024;style.flags2=2;
	CHECK(st_rip3_button_styles_set(host.button_styles,0,&style)==0);
	command(&host,&errors,"1U0A0K1O141T00<>Aaa<>");CHECK(errors==1);
	yellow=0;
	for (int y=20;y<40;y++) for (int x=10;x<60;x++) {
		uint8_t value;CHECK(st_rip3_ports_read(host.ports,0,x,y,&value)==0);
		if (value==14) {CHECK(x>=23 && x<31);yellow++;}
	}
	CHECK(yellow>0);
	/* Decorations use the current screen/offscreen context; mouse buttons
	 * always use PORT0 and restore the caller's current selection. */
	st_rip3_mouse_kill(host.mouse);
	const struct st_rip3_rect screen_port={100,100,199,199},bitmap_port={0,0,79,79};
	CHECK(st_rip3_ports_define(host.ports,5,&screen_port,false)==0 && st_rip3_ports_switch(host.ports,5)==0);
	style.flags=256;style.flags2=0;style.colors[ST_RIP3_BUTTON_SURFACE]=3;
	CHECK(st_rip3_button_styles_set(host.button_styles,0,&style)==0);
	command(&host,&errors,"1U0A0K1O140000<><>");CHECK(errors==1);
	unsigned current;CHECK(st_rip3_ports_current(host.ports,&current)==0 && current==5);
	CHECK(pixel(host.ports,110,120,3)==0);
	style.flags=256|1024;
	CHECK(st_rip3_button_styles_set(host.button_styles,0,&style)==0);
	command(&host,&errors,"1U0A0K1O140000<><>");CHECK(errors==1);
	CHECK(st_rip3_ports_current(host.ports,&current)==0 && current==5);
	CHECK(!st_rip3_mouse_event(host.mouse,160,130,true));
	CHECK(!st_rip3_mouse_event(host.mouse,20,30,false));
	CHECK(st_rip3_mouse_event(host.mouse,20,30,true) && st_rip3_mouse_event(host.mouse,20,30,false));
	CHECK(st_rip3_ports_define(host.ports,6,&bitmap_port,true)==0 && st_rip3_ports_switch(host.ports,6)==0);
	style.flags=256;style.colors[ST_RIP3_BUTTON_SURFACE]=5;
	CHECK(st_rip3_button_styles_set(host.button_styles,0,&style)==0);
	command(&host,&errors,"1U0A0K1O140000<><>");CHECK(errors==1);
	uint8_t color;CHECK(st_rip3_ports_read(host.ports,6,10,20,&color)==0 && color==5);
	CHECK(pixel(host.ports,10,20,3)==0 && st_rip3_ports_current(host.ports,&current)==0 && current==6);
	/* PR117: normalize before conversion, and reject zero DEVICE area before
	 * bevel can turn a line into a field. These skips consume no capacity. */
	st_rip3_mouse_kill(host.mouse);
	CHECK(st_rip3_ports_switch(host.ports,0)==0);
	style.flags=256|1024|512;style.bevel=3;
	CHECK(st_rip3_button_styles_set(host.button_styles,0,&style)==0);
	command(&host,&errors,"1U0A0K0A140000<><>");
	command(&host,&errors,"1U0A0K00000000<><>");
	env.world_width=1280;env.world_height=960;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	command(&host,&errors,"1U0A0K0B0L0000<><>");
	CHECK(errors==1 && st_rip3_mouse_count(host.mouse)==0);
	env.world_width=640;env.world_height=480;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	command(&host,&errors,"1U0U140A0K0000<><>");
	CHECK(errors==1 && st_rip3_mouse_count(host.mouse)==1);
	CHECK(st_rip3_mouse_event(host.mouse,7,17,true) && st_rip3_mouse_event(host.mouse,32,42,false));
	CHECK(!st_rip3_mouse_event(host.mouse,33,20,true) && !st_rip3_mouse_event(host.mouse,33,20,false));
	CHECK(!st_rip3_mouse_event(host.mouse,10,43,true) && !st_rip3_mouse_event(host.mouse,10,43,false));
	st_rip3_mouse_kill(host.mouse);
	/* Inactive PORT0 skips while another active port is current. Reactivation
	 * does not resurrect the skipped button. An unrelated inactive current
	 * bitmap does not prevent a later valid PORT0 definition. */
	CHECK(st_rip3_ports_activate(host.ports,false)==0 && st_rip3_ports_switch(host.ports,5)==0);
	command(&host,&errors,"1U0A0K1O140000<><>");
	CHECK(errors==1 && st_rip3_mouse_count(host.mouse)==0);
	CHECK(st_rip3_ports_switch(host.ports,0)==0 && st_rip3_ports_activate(host.ports,true)==0);
	CHECK(st_rip3_mouse_count(host.mouse)==0);
	const struct st_rip3_rect clipped_view={100,100,149,139};
	CHECK(st_rip3_ports_viewport_device(host.ports,&clipped_view)==0);
	command(&host,&errors,"1U1H0A1O0K0000<><>"); /* bevel starts at exclusive right 50 */
	command(&host,&errors,"1U2S0A3C0K0000<><>"); /* wholly clipped */
	command(&host,&errors,"1M001E0A1O0K0000000"); /* simple field touches right */
	CHECK(errors==1 && st_rip3_mouse_count(host.mouse)==0);
	CHECK(st_rip3_ports_switch(host.ports,6)==0 && st_rip3_ports_activate(host.ports,false)==0);
	command(&host,&errors,"1U0A0K1O140000<><>");
	CHECK(errors==1 && st_rip3_mouse_count(host.mouse)==1);
	CHECK(st_rip3_ports_current(host.ports,&current)==0 && current==6);
	CHECK(st_rip3_mouse_event(host.mouse,107,117,true) && st_rip3_mouse_event(host.mouse,149,139,false));
	CHECK(!st_rip3_mouse_event(host.mouse,150,120,true) && !st_rip3_mouse_event(host.mouse,150,120,false));
	CHECK(!st_rip3_mouse_event(host.mouse,110,140,true) && !st_rip3_mouse_event(host.mouse,110,140,false));
	/* Creation snapshots survive subsequent viewport motion/deactivation,
	 * including a gesture already captured at the old absolute rectangle. */
	CHECK(st_rip3_mouse_event(host.mouse,110,120,true));
	CHECK(st_rip3_ports_switch(host.ports,0)==0);
	const struct st_rip3_rect moved_view={300,300,349,339};
	CHECK(st_rip3_ports_viewport_device(host.ports,&moved_view)==0 && st_rip3_ports_activate(host.ports,false)==0);
	CHECK(st_rip3_mouse_event(host.mouse,110,120,false));
	CHECK(st_rip3_mouse_event(host.mouse,110,120,true) && st_rip3_mouse_event(host.mouse,110,120,false));
	CHECK(!st_rip3_mouse_event(host.mouse,310,320,true) && !st_rip3_mouse_event(host.mouse,310,320,false));
	st_rip3_mouse_kill(host.mouse);
	CHECK(st_rip3_ports_activate(host.ports,true)==0);
	const struct st_rip3_rect outside_view={700,500,749,539};
	CHECK(st_rip3_ports_viewport_device(host.ports,&outside_view)==0);
	command(&host,&errors,"1U0A0K1O140000<><>");
	command(&host,&errors,"1M000A0K1O140000000");
	CHECK(errors==1 && st_rip3_mouse_count(host.mouse)==0);
	st_rip3_mouse_destroy(host.mouse);st_rip3_palettes_destroy(host.palettes);
	st_rip3_button_styles_destroy(host.button_styles);st_rip3_styles_destroy(host.styles);
	st_rip3_ports_destroy(host.ports);st_rip3_environments_destroy(host.environments);return 0;
}
