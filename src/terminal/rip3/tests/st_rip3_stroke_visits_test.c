/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_draw_internal.h"
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_styles.h"
#include "st_rip3_ports.h"
#include "st_rip3_palettes.h"
#include "st_rip3_stream.h"
#include "st_pixels.h"
#include "io_error.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern bool st_rip3_drawing_test_fail_calloc;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr,"check failed at line %d: %s\n",__LINE__,#expression); \
		return 1; \
	} \
} while (0)

static const unsigned once[]={0x0f,0xaa,0xaf,0x05,0x5a};
static const unsigned twice[]={0x0f,0xa5,0xaf,0x05,0xa5};

static int
paths(void)
{
	static const struct st_rip3_vector subdivided[]={{2,3},{5,3},{5,3},{8,3}};
	static const struct st_rip3_vector backtrack[]={{2,3},{8,3},{2,3}};
	static const struct st_rip3_vector crossing[]={{2,2},{8,8},{2,8},{8,2}};
	static const struct st_rip3_vector rectangle[]={{2,2},{5,2},{8,2},{8,8},{2,8},{2,2},{2,2}};
	static const struct st_rip3_vector two_turns[]={{2,2},{8,2},{8,8},{2,8},{2,2},{8,2},{8,8},{2,8}};
	struct st_rip3_ports *ports=NULL;
	const struct st_rip3_rect area={0,0,11,11};
	CHECK(st_rip3_ports_create(&ports,ST_RIP3_CANVAS_640_480)==0);
	CHECK(st_rip3_ports_viewport(ports,&area)==0);
	for (unsigned mode=0; mode<5; mode++) for (unsigned shape=0; shape<6; shape++) {
		const struct st_rip3_vector *points=shape==0 ? subdivided : shape<=2 ? backtrack : shape==3 ? crossing : shape==4 ? rectangle : two_turns;
		size_t count=shape==0 ? 4 : shape<=2 ? 3 : shape==3 ? 4 : shape==4 ? 7 : 8;
		bool closed=shape==2 || shape>=4;
		struct st_rip3_pen pen={.pattern=0xffff,.thickness=1};
		CHECK(st_rip3_ports_erase(ports,0xa5)==0);
		CHECK(st_rip3_stroke_visits(ports,points,count,closed,&pen,0x0f,0x66,(enum st_rip3_write_mode)mode)==0);
		for (int32_t y=0; y<12; y++) for (int32_t x=0; x<12; x++) {
			unsigned visits=0,value;
			uint8_t actual;
			if (shape<=2 && y==3 && x>=2 && x<=8)
				visits=shape==0 || x==8 || (shape==2 && x==2) ? 1 : 2;
			if (shape==3 && ((x>=2 && x<=8 && (x==y || x+y==10)) || (y==8 && x>=2 && x<=8)))
				visits=x==5 && y==5 ? 2 : 1;
			if (shape>=4 && x>=2 && x<=8 && y>=2 && y<=8 && (x==2 || x==8 || y==2 || y==8))
				visits=shape==4 ? 1 : 2;
			value=visits==0 ? 0xa5 : visits==1 ? once[mode] : twice[mode];
			CHECK(st_rip3_ports_read(ports,0,x,y,&actual)==0 && actual==value);
		}
	}
	/* A thick ordinary closed join is one visit, including the seam. */
	struct st_rip3_pen thick={.pattern=0xffff,.thickness=3};
	CHECK(st_rip3_ports_erase(ports,0xa5)==0
	    && st_rip3_stroke_visits(ports,rectangle,7,true,&thick,0x0f,0x66,ST_RIP3_XOR)==0);
	for (int32_t y=0; y<12; y++) for (int32_t x=0; x<12; x++) {
		uint8_t value;
		bool covered=x>=1 && x<=9 && y>=1 && y<=9 && !(x>=4 && x<=6 && y>=4 && y<=6);
		CHECK(st_rip3_ports_read(ports,0,x,y,&value)==0 && value==(covered ? 0xaa : 0xa5));
	}
	/* First/last sources differ on a real retraversal. These literal results
	 * establish source ordering for COPY and parity/composition for every W. */
	const struct st_rip3_vector patterned[]={{2,3},{10,3},{2,3}};
	static const unsigned mixed[]={0x66,0xcc,0xef,0x04,0xa5};
	for (unsigned mode=0; mode<5; mode++) for (unsigned opaque=0; opaque<2; opaque++) {
		struct st_rip3_pen pen={.pattern=0xff00,.thickness=1,.draw_background=opaque!=0};
		uint8_t value;
		CHECK(st_rip3_ports_erase(ports,0xa5)==0
		    && st_rip3_stroke_visits(ports,patterned,3,false,&pen,0x0f,0x66,(enum st_rip3_write_mode)mode)==0);
		CHECK(st_rip3_ports_read(ports,0,5,3,&value)==0 && value==(opaque ? mixed[mode] : once[mode]));
	}
	st_rip3_ports_destroy(ports);return 0;
}

static int
failures(void)
{
	struct st_rip3_ports *ports=NULL;
	const struct st_rip3_rect area={0,0,11,11};
	struct st_rip3_vector points[]={{2,3},{8,3},{2,3}};
	struct st_rip3_pen pen={.pattern=0xffff,.thickness=1};
	CHECK(st_rip3_ports_create(&ports,ST_RIP3_CANVAS_640_480)==0
	    && st_rip3_ports_viewport(ports,&area)==0 && st_rip3_ports_erase(ports,0xa5)==0);
	st_rip3_drawing_test_fail_calloc=true;
	CHECK(st_rip3_stroke_visits(ports,points,3,false,&pen,15,0,ST_RIP3_XOR)==IO_ERROR_OUT_OF_MEMORY
	    && !st_rip3_drawing_test_fail_calloc);
	points[2].x=NAN;
	CHECK(st_rip3_stroke_visits(ports,points,3,false,&pen,15,0,ST_RIP3_XOR)==IO_ERROR_OUT_OF_RANGE);
	points[2].x=2;
	CHECK(st_rip3_stroke_visits(ports,points,0,false,&pen,15,0,ST_RIP3_XOR)==IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_ports_activate(ports,false)==0);
	st_rip3_drawing_test_fail_calloc=true;
	CHECK(st_rip3_stroke_visits(ports,points,3,false,&pen,15,0,ST_RIP3_XOR)==0
	    && st_rip3_drawing_test_fail_calloc);
	st_rip3_drawing_test_fail_calloc=false;
	for (int32_t y=0; y<12; y++) for (int32_t x=0; x<12; x++) {
		uint8_t value;
		CHECK(st_rip3_ports_read(ports,0,x,y,&value)==0 && value==0xa5);
	}
	st_rip3_ports_destroy(ports);return 0;
}

struct observation { struct st_rip3_command_host *host; unsigned errors; };

static void
diagnostic(void *context, enum st_rip3_command_diagnostic kind)
{
	struct observation *seen=context;
	(void)kind; seen->errors++;
}

static void
command(void *context, const uint8_t *bytes, size_t length)
{
	struct observation *seen=context;
	const struct st_rip3_command_sink sink={seen,diagnostic};
	st_rip3_commands_process(seen->host,&sink,bytes,length);
}

static void
wire(struct st_rip3_stream *stream, const char *bytes)
{
	st_rip3_stream_process(stream,(const uint8_t *)"!|",2);
	for (size_t i=0; bytes[i]!=0; i++) st_rip3_stream_process(stream,(const uint8_t *)bytes+i,1);
	st_rip3_stream_process(stream,(const uint8_t *)"\r",1);
}

static void
number(char *bytes, unsigned value)
{
	static const char digits[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	bytes[0]=digits[value/36];bytes[1]=digits[value%36];
}

static int
scene(enum st_rip3_canvas canvas, unsigned width, unsigned height)
{
	struct st_rip3_command_host host={0};
	struct st_rip3_style style;
	struct st_rip3_environment env;
	struct st_rip3_port_info info;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture=NULL;
	struct st_rip3_stream *stream=NULL;
	struct observation seen={&host,0};
	const struct st_rip3_stream_sink sink={.context=&seen,.command=command};
	const struct st_rip3_rect area={0,0,239,159},copy={320,0,559,159};
	uint32_t colors[256];
	CHECK(st_rip3_style_defaults(&style)==0 && st_rip3_styles_create(&host.styles,&style)==0);
	CHECK(st_rip3_environments_create(&host.environments,canvas)==0
	    && st_rip3_environments_get(host.environments,0,&env)==0);
	env.world_width=width;env.world_height=height;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	CHECK(st_rip3_ports_create(&host.ports,canvas)==0 && st_rip3_ports_define(host.ports,3,&area,true)==0);
	CHECK(st_rip3_palettes_create(&host.palettes)==0);
	for (unsigned i=0; i<256; i++) colors[i]=i*UINT32_C(0x010101);
	CHECK(st_rip3_palettes_rgb(host.palettes,0,8,colors,256)==0 && st_rip3_stream_create(&stream,&sink)==0);
	for (unsigned target=0; target<=3; target+=3) {
		CHECK(st_rip3_ports_switch(host.ports,target)==0 && st_rip3_ports_fill(host.ports,&area,0xa5,ST_RIP3_COPY)==0);
		wire(stream,"m0102");
		for (unsigned mode=0; mode<5; mode++) {
			unsigned left=mode*40;
			char bytes[80]="l03";
			style.drawing_color=0x0f;style.write_mode=(enum st_rip3_write_mode)mode;
			CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
			unsigned line[]={left+10,10,left+30,10,left+10,10};
			for (unsigned i=0; i<6; i++) number(bytes+3+i*2,line[i]);bytes[15]=0;
			if (mode==0) {
				wire(stream,"]??");st_rip3_drawing_test_fail_calloc=true;wire(stream,bytes);
				CHECK(!st_rip3_drawing_test_fail_calloc);
			}
			wire(stream,bytes);
			unsigned square[]={left+10,30,left+30,30,left+30,50,left+10,50,
			    left+10,30,left+30,30,left+30,50,left+10,50};
			memcpy(bytes,"P08",3);
			for (unsigned i=0; i<16; i++) number(bytes+3+i*2,square[i]);bytes[35]=0;wire(stream,bytes);
			/* Cubic P0=P3, P1=P2: N=4 reaches x=25 at t=1/2,
			 * then returns. Independent Bernstein arithmetic fixes that peak. */
			unsigned cubic[]={left+10,65,left+30,65,left+30,65,left+10,65};
			bytes[0]='Z';for (unsigned i=0; i<8; i++) number(bytes+1+i*2,cubic[i]);
			memcpy(bytes+17,"04",3);wire(stream,bytes);
			bytes[0]='&';number(bytes+1,left+20);number(bytes+3,90);memcpy(bytes+5,"0A0A00",7);wire(stream,bytes);
			bytes[0]=']';number(bytes+1,left+20);number(bytes+3,130);memcpy(bytes+5,"0A0A00JZ00",11);wire(stream,bytes);
		}
		CHECK(st_rip3_ports_info(host.ports,target,&info)==0 && info.cursor.x==1 && info.cursor.y==2);
	}
	CHECK(seen.errors==4 && st_rip3_ports_copy(host.ports,3,NULL,0,&copy,ST_RIP3_TRANSFER_COPY)==0);
	CHECK(st_rip3_palettes_get(host.palettes,0,&palette)==0 && st_rip3_ports_snapshot(host.ports,&palette,&capture)==0);
	for (unsigned target=0; target<2; target++) for (unsigned mode=0; mode<5; mode++) {
		unsigned left=target*320+mode*40;
		for (unsigned x=10; x<=30; x++)
			CHECK(capture->primary[10*width+left+x]==(ST_PIXEL_COLOR_RGB_TAG | (x==30 ? once[mode] : twice[mode])*UINT32_C(0x010101)));
		for (unsigned y=30; y<=50; y++) for (unsigned x=10; x<=30; x++) {
			unsigned value=x==10 || x==30 || y==30 || y==50 ? twice[mode] : 0xa5;
			CHECK(capture->primary[y*width+left+x]==(ST_PIXEL_COLOR_RGB_TAG | value*UINT32_C(0x010101)));
		}
		for (unsigned x=10; x<=25; x++)
			CHECK(capture->primary[65*width+left+x]==(ST_PIXEL_COLOR_RGB_TAG | (x==25 ? once[mode] : twice[mode])*UINT32_C(0x010101)));
		for (unsigned figure=0; figure<2; figure++) {
			unsigned y=figure ? 130 : 90, value=figure ? twice[mode] : once[mode];
			const unsigned cardinal[][2]={{20,y-10},{10,y},{30,y},{20,y+10}};
			for (unsigned i=0; i<4; i++)
				CHECK(capture->primary[cardinal[i][1]*width+left+cardinal[i][0]]==(ST_PIXEL_COLOR_RGB_TAG | value*UINT32_C(0x010101)));
		}
	}
	st_pixel_buffer_destroy(capture);st_rip3_stream_destroy(stream);st_rip3_ports_destroy(host.ports);
	st_rip3_palettes_destroy(host.palettes);st_rip3_styles_destroy(host.styles);st_rip3_environments_destroy(host.environments);
	return 0;
}

int
main(void)
{
	CHECK(paths()==0 && failures()==0);
	CHECK(scene(ST_RIP3_CANVAS_640_480,640,480)==0);
	CHECK(scene(ST_RIP3_CANVAS_800_600,800,600)==0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768,1024,768)==0);
	return 0;
}
