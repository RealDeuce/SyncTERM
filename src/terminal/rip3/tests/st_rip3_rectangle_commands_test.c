/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
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

extern bool st_rip3_drawing_test_fail_calloc;
extern unsigned st_rip3_ellipse_test_realloc_countdown;
extern unsigned st_rip3_drawing_test_calloc_countdown;

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
	    .font={0,0,1,0},.write_mode=ST_RIP3_COPY};
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
	static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#&";
	while (width != 0) { result[--width] = digits[value%base]; value /= base; }
}

struct scene { const struct st_rip3_command_host *host; unsigned errors; };

static void
scene_error(void *context, enum st_rip3_command_diagnostic kind)
{
	struct scene *scene = context;
	(void)kind; scene->errors++;
}

static void
scene_command(void *context, const uint8_t *bytes, size_t length)
{
	struct scene *scene = context;
	const struct st_rip3_command_sink sink = {scene,scene_error};
	st_rip3_commands_process(scene->host, &sink, bytes,length);
}

static void
wire(struct st_rip3_stream *stream, const char *text)
{
	for (size_t i = 0; i < strlen(text); i++) st_rip3_stream_process(stream, (const uint8_t *)text+i, 1);
}

static unsigned
rop(unsigned mode, unsigned source, unsigned destination)
{
	const unsigned results[]={source,source^destination,source|destination,source&destination,255^destination};
	return results[mode];
}

static bool
dot(unsigned type, unsigned phase)
{
	/* Independent logical dash descriptions, including custom type 4's
	 * fixed dash/dot/dot mapping rather than the stored arbitrary mask. */
	static const char *const runs[]={"########","#.#.#.#.","###.#...","####....","###.#.#."};
	return runs[type][phase%8]=='#';
}

static bool
border(unsigned type, unsigned thickness, unsigned u, unsigned v, bool opaque, unsigned *ink)
{
	if (thickness==3) {
		if (u<1 || u>6 || v<1 || v>6) return false;
		*ink=12;return true;
	}
	bool any=false,on=false;
	if (v==2 && u>=2 && u<=5) {any=true;on |= dot(type,u-2);}
	if (u==5 && v>=2 && v<=5) {any=true;on |= dot(type,3+v-2);}
	if (v==5 && u>=2 && u<=5) {any=true;on |= dot(type,6+5-u);}
	if (u==2 && v>=2 && v<=5) {any=true;on |= dot(type,9+5-v);}
	if (!any || (!on && !opaque)) return false;
	*ink=on ? 12 : 1;return true;
}

static void
rectangle(char *bytes, unsigned col, unsigned row, unsigned scale, unsigned xy, unsigned base)
{
	unsigned values[]={col*8+2,row*8+2,col*8+5,row*8+5};
	if (col%2!=0) {unsigned x=values[0],y=values[1];values[0]=values[2];values[1]=values[3];values[2]=x;values[3]=y;}
	bytes[0]='K';
	for (unsigned i=0; i<4; i++) number(bytes+1+i*xy,xy,values[i]*scale*(i%2!=0 ? 2 : 1),base);
	bytes[1+4*xy]=0;
	unsigned last=values[3]*scale*2;
	if (col%2!=0 && last<base) {number(bytes+1+3*xy,1,last,base);bytes[2+3*xy]=0;}
}

static int
matrix(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, bool offscreen)
{
	static const unsigned custom[]={0x8000,0xffff,0x1111,0xaaaa};
	static const unsigned stored[]={0xffff,0x3333,0x1e3f,0x1f1f};
	struct st_rip3_command_host host={0};
	struct st_rip3_environment env;
	struct st_rip3_style style,after;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture=NULL;
	struct scene scene={&host,0};
	const struct st_rip3_stream_sink sink={.context=&scene,.command=scene_command};
	struct st_rip3_stream *stream=NULL;
	const struct st_rip3_rect screen={10,20,361,147},bitmap={0,0,351,127};
	const struct st_rip3_rect view={2,3,337,114},copy={251,101,586,212};
	bool borders=false;
	CHECK(create(&host,canvas,width,height)==0);
	CHECK(process(&host, borders ? "N01" : "N00", -1) == 0);
	CHECK(st_rip3_ports_define(host.ports,1,offscreen ? &bitmap : &screen,offscreen)==0);
	CHECK(st_rip3_ports_switch(host.ports,1)==0 && st_rip3_ports_viewport(host.ports,&view)==0);
	CHECK(st_rip3_ports_protect(host.ports,1,true)==0);
	CHECK(st_rip3_stream_create(&stream,&sink)==0);
	CHECK(st_rip3_styles_get(host.styles,0,&style)==0);
	CHECK(st_rip3_environments_get(host.environments,0,&env)==0);
	for (unsigned scale=1; scale<=2; scale++) for (unsigned base=36; base<=64; base+=28) for (unsigned xy=2; xy<=5; xy++) {
		env.world_width=width*scale;env.world_height=height*scale*2;
		env.coordinate_size=xy;env.base_math=base;
		CHECK(st_rip3_ports_erase(host.ports,0)==0);
		for (unsigned row=0; row<12; row++) {
			unsigned mode=row%6,thickness=row<6 ? 1 : 3;
			bool rgb=mode==5;env.rgb_bits=rgb ? 8 : 0;
			CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
			style.write_mode=(enum st_rip3_write_mode)(rgb ? 0 : mode);
			style.drawing_color=rgb ? 0x0c0c0c : 12;style.fill_color=rgb ? 0x070707 : 7;
			style.background_color=rgb ? 0x010101 : 1;
			style.borders=borders;
			CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
			for (unsigned col=0; col<40; col++) {
				unsigned type=col%5,flags=(col/5)%4;
				const struct st_rip3_rect cell={(int32_t)col*8,(int32_t)row*8,(int32_t)col*8+7,(int32_t)row*8+7};
				char setting[10]="=00000000",bytes[24];borders=col>=20;
				CHECK(process(&host, borders ? "N01" : "N00", -1) == 0);
				number(setting+1,1,flags,base);number(setting+2,1,type,base);number(setting+3,4,custom[flags],base);
				number(setting+7,2,thickness*((flags&2)!=0 ? scale : 1),base);
				wire(stream,"!|");wire(stream,setting);wire(stream,"\r");
				CHECK(st_rip3_styles_get(host.styles,0,&after)==0 && after.line_type==type
				    && after.pen.pattern==(type==4 ? custom[flags] : stored[type]) && after.pen.thickness==thickness
				    && after.pen.draw_background==(flags==1));
				CHECK(st_rip3_ports_fill(host.ports,&cell,0x55,ST_RIP3_COPY)==0);
				rectangle(bytes,col,row,scale,xy,base);
				wire(stream,"!|");wire(stream,bytes);wire(stream,"\r");
				CHECK(st_rip3_styles_get(host.styles,0,&after)==0 && after.line_type==type
				    && after.pen.pattern==(type==4 ? custom[flags] : stored[type]) && after.pen.thickness==thickness
				    && after.pen.draw_background==(flags==1) && after.write_mode==style.write_mode);
			}
		}
		CHECK(scene.errors==0);
		if (offscreen) CHECK(st_rip3_ports_copy(host.ports,1,NULL,0,&copy,ST_RIP3_TRANSFER_COPY)==0);
		CHECK(st_rip3_palettes_get(host.palettes,0,&palette)==0 && st_rip3_ports_snapshot(host.ports,&palette,&capture)==0);
		unsigned left=offscreen ? 251 : 12,top=offscreen ? 101 : 23;
		unsigned phase_x=offscreen ? 2 : 12,phase_y=offscreen ? 3 : 23;
		for (uint32_t y=0; y<height; y++) for (uint32_t x=0; x<width; x++) {
			unsigned expected=0;
			if (x>=left && x<left+320 && y>=top && y<top+96) {
				unsigned col=(x-left)/8,row=(y-top)/8,u=(x-left)%8,v=(y-top)%8;
				unsigned mode=row%6==5 ? 0 : row%6,thickness=row<6 ? 1 : 3,type=col%5,flags=(col/5)%4,ink=0;
				bool covered=col>=20 && border(type,thickness,u,v,flags==1,&ink);
				bool interior=u>=2 && u<5 && v>=2 && v<5;
				expected=0x55;
				/* These are the documented pass operands: flags 1 partitions
				 * brush and pen; other flags fill first, then transparent pen. */
				if (interior && !(covered && flags==1))
					expected=rop(mode,(phase_x+x-left+phase_y+y-top)%2==0 ? 7 : 1,expected);
				if (covered) expected=rop(mode,ink,expected);
			}
			if (capture->primary[(size_t)y*width+x]!=(ST_PIXEL_COLOR_RGB_TAG | expected*UINT32_C(0x010101))) {
				fprintf(stderr,"scene %u/%u/%u off=%d pixel %u,%u got %x expected %x\n",scale,base,xy,offscreen,x,y,capture->primary[(size_t)y*width+x],expected);return 1;
			}
		}
		st_pixel_buffer_destroy(capture);capture=NULL;
	}
	st_rip3_stream_destroy(stream);destroy(&host);return 0;
}

static int
unchanged(const struct st_rip3_command_host *host)
{
	uint8_t value;
	for (int32_t y=0; y<8; y++) for (int32_t x=0; x<8; x++)
		CHECK(st_rip3_ports_read(host->ports,0,x,y,&value)==0 && value==0x55);
	return 0;
}

static int
boundaries(void)
{
	struct st_rip3_command_host host={0};
	struct st_rip3_environment env;
	struct st_rip3_style style;
	const struct st_rip3_rect area={0,0,7,7};
	bool borders=true;
	uint8_t value;
	CHECK(create(&host,ST_RIP3_CANVAS_640_480,640,480)==0);
	CHECK(process(&host, borders ? "N01" : "N00", -1) == 0);
	CHECK(st_rip3_ports_viewport(host.ports,&area)==0 && st_rip3_ports_erase(host.ports,0x55)==0);
	CHECK(process(&host,"=04000101",-1)==0); /* Custom type, arbitrary mask 1. */
	st_rip3_drawing_test_fail_calloc=true;
	CHECK(process(&host,"K01010505",ST_RIP3_COMMAND_RESOURCE_FAILURE)==0 && !st_rip3_drawing_test_fail_calloc);
	CHECK(unchanged(&host)==0);
	CHECK(process(&host,"K01010505",-1)==0);
	CHECK(st_rip3_ports_read(host.ports,0,1,1,&value)==0 && value==12);
	CHECK(st_rip3_ports_read(host.ports,0,4,1,&value)==0 && value==1);
	char line[]="=04000001";number(line+3,4,65535,36);
	CHECK(process(&host,line,-1)==0);
	CHECK(st_rip3_styles_get(host.styles,0,&style)==0 && style.line_type==4 && style.pen.pattern==65535);
	CHECK(st_rip3_ports_erase(host.ports,0x55)==0 && process(&host,"K01010505",-1)==0);
	CHECK(st_rip3_ports_read(host.ports,0,1,1,&value)==0 && value==12);
	CHECK(st_rip3_ports_read(host.ports,0,4,1,&value)==0 && value==1); /* Still DASHDOTDOT, not solid. */
	/* NOT's single pass never needs a color lookup, including opaque gaps. */
	CHECK(process(&host,"=14000101",-1)==0);
	CHECK(st_rip3_styles_get(host.styles,0,&style)==0);
	style.write_mode=ST_RIP3_NOT;style.drawing_color=style.background_color=style.fill_color=0xffffff;
	style.borders=borders;
	CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
	CHECK(st_rip3_environments_get(host.environments,0,&env)==0);env.rgb_bits=8;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	struct st_rip3_palettes *bound=host.palettes;host.palettes=NULL;
	CHECK(st_rip3_ports_erase(host.ports,0x55)==0 && process(&host,"K01010505",-1)==0);
	for (int32_t y=0; y<8; y++) for (int32_t x=0; x<8; x++)
		CHECK(st_rip3_ports_read(host.ports,0,x,y,&value)==0 && value==(x>=1 && x<=5 && y>=1 && y<=5 ? 0xaa : 0x55));
	borders=false;
	CHECK(process(&host, borders ? "N01" : "N00", -1) == 0);
	st_rip3_drawing_test_fail_calloc=true;
	CHECK(st_rip3_ports_erase(host.ports,0x55)==0 && process(&host,"K04040404",-1)==0);
	CHECK(st_rip3_drawing_test_fail_calloc && unchanged(&host)==0);st_rip3_drawing_test_fail_calloc=false;
	borders=true;
	CHECK(process(&host, borders ? "N01" : "N00", -1) == 0);
	struct scene scene={&host,0};
	const struct st_rip3_stream_sink sink={.context=&scene,.command=scene_command};
	struct st_rip3_stream *stream=NULL;
	CHECK(st_rip3_stream_create(&stream,&sink)==0);
	wire(stream,"!|K0101050?|=40000001|=10000001|K01010505\r");
	CHECK(scene.errors==2);
	for (int32_t y=0; y<8; y++) for (int32_t x=0; x<8; x++)
		CHECK(st_rip3_ports_read(host.ports,0,x,y,&value)==0 && value==(x>=1 && x<=5 && y>=1 && y<=5 ? 0xaa : 0x55));
	CHECK(st_rip3_styles_get(host.styles,0,&style)==0 && style.line_type==0 && style.pen.pattern==65535);
	host.palettes=bound;st_rip3_stream_destroy(stream);destroy(&host);return 0;
}

int
main(void)
{
	CHECK(boundaries()==0);
	CHECK(matrix(ST_RIP3_CANVAS_640_480,640,480,false)==0);
	CHECK(matrix(ST_RIP3_CANVAS_640_480,640,480,true)==0);
	CHECK(matrix(ST_RIP3_CANVAS_800_600,800,600,false)==0);
	CHECK(matrix(ST_RIP3_CANVAS_800_600,800,600,true)==0);
	CHECK(matrix(ST_RIP3_CANVAS_1024_768,1024,768,false)==0);
	CHECK(matrix(ST_RIP3_CANVAS_1024_768,1024,768,true)==0);
	return 0;
}
