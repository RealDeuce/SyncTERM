/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_styles.h"
#include "st_rip3_ports.h"
#include "st_rip3_palettes.h"
#include "st_rip3_stream.h"
#include "st_pixels.h"

#include <math.h>
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
border(const char *pattern, unsigned thickness, unsigned u, unsigned v, bool opaque, unsigned *ink)
{
	/* Signed distance to the radius-two contour (1,1)..(9,9), computed
	 * independently of the production line/quarter-ellipse tessellation. */
	const double pi=3.14159265358979323846264338328;
	double qx=fabs((double)u-5)-2,qy=fabs((double)v-5)-2;
	double distance=hypot(fmax(qx,0),fmax(qy,0))+fmin(fmax(qx,qy),0)-2;
	if (fabs(distance)>thickness/2.0) return false;
	double phase;
	if (u>7 && v<3) phase=4+2*atan2((double)u-7,3-(double)v);
	else if (u>7 && v>7) phase=8+pi+2*atan2((double)v-7,(double)u-7);
	else if (u<3 && v>7) phase=12+2*pi+2*atan2(3-(double)u,(double)v-7);
	else if (u<3 && v<3) phase=16+3*pi+2*atan2(3-(double)v,3-(double)u);
	else if (v<=3 && u>=3 && u<=7) phase=(double)u-3;
	else if (u>=7 && v>=3 && v<=7) phase=4+pi+(double)v-3;
	else if (v>=7 && u>=3 && u<=7) phase=8+2*pi+7-(double)u;
	else phase=12+3*pi+7-(double)v;
	bool on=pattern[(unsigned)floor(phase)%16]=='#';
	if (!on && !opaque) return false;
	*ink=on ? 12 : 1;return true;
}

static bool
interior(unsigned u, unsigned v)
{
	/* The filled contour is unchanged by the pen. Pixel centers sample it. */
	double x=u+0.5,y=v+0.5;
	if (x<1 || x>=9 || y<1 || y>=9) return false;
	if ((x>=3 && x<=7) || (y>=3 && y<=7)) return true;
	double dx=x-(x<3 ? 3 : 7),dy=y-(y<3 ? 3 : 7);
	return dx*dx+dy*dy<4;
}

static void
rounded(char *bytes, char kind, unsigned col, unsigned row, unsigned scale, unsigned xy, unsigned base)
{
	unsigned values[]={col*12+1,row*12+1,col*12+9,row*12+9,2};
	if (col%2!=0) {unsigned x=values[0],y=values[1];values[0]=values[2];values[1]=values[3];values[2]=x;values[3]=y;}
	bytes[0]=kind;
	for (unsigned i=0; i<5; i++) number(bytes+1+i*xy,xy,values[i]*scale*(i%2==0 ? 2 : 1),base);
	bytes[1+5*xy]=0;
}

static int
matrix(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, bool offscreen)
{
	struct st_rip3_command_host host={0};
	struct st_rip3_environment env;
	struct st_rip3_style style,after;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture=NULL;
	struct scene scene={&host,0};
	const struct st_rip3_stream_sink sink={.context=&scene,.command=scene_command};
	struct st_rip3_stream *stream=NULL;
	const struct st_rip3_rect screen={10,20,261,467},bitmap={0,0,251,447};
	const struct st_rip3_rect view={2,3,245,438},copy={251,31,494,466};
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
		env.world_width=width*scale*2;env.world_height=height*scale;
		env.coordinate_size=xy;env.base_math=base;
		CHECK(st_rip3_ports_erase(host.ports,0)==0);
		for (unsigned row=0; row<36; row++) {
			unsigned mode=row%6,group=(row/6)%3,thickness=row<18 ? 1 : 3;
			bool rgb=mode==5,filled=group!=0;env.rgb_bits=rgb ? 8 : 0;borders=group==2;
			CHECK(process(&host, borders ? "N01" : "N00", -1) == 0);
			CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
			style.write_mode=(enum st_rip3_write_mode)(rgb ? 0 : mode);
			style.drawing_color=rgb ? 0x0c0c0c : 12;style.fill_color=rgb ? 0x070707 : 7;
			style.background_color=rgb ? 0x010101 : 1;
			style.borders=borders;
			CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
			for (unsigned col=0; col<20; col++) {
				unsigned type=col%5,flags=col/5;
				const struct st_rip3_rect cell={(int32_t)col*12,(int32_t)row*12,(int32_t)col*12+11,(int32_t)row*12+11};
				char setting[10]="=00000000",bytes[32];
				number(setting+1,1,flags,base);number(setting+2,1,type,base);number(setting+3,4,col%2!=0 ? 65535 : 1,base);
				number(setting+7,2,thickness*((flags&2)!=0 ? 2*scale : 1),base);
				wire(stream,"!|");wire(stream,setting);wire(stream,"\r");
				CHECK(st_rip3_styles_get(host.styles,0,&style)==0 && style.pen.thickness==thickness);
				CHECK(st_rip3_ports_fill(host.ports,&cell,0x55,ST_RIP3_COPY)==0);
				rounded(bytes,filled ? 'u' : 'U',col,row,scale,xy,base);
				wire(stream,"!|");wire(stream,bytes);wire(stream,"\r");
				CHECK(st_rip3_styles_get(host.styles,0,&after)==0 && after.line_type==style.line_type
				    && after.pen.pattern==style.pen.pattern && after.pen.thickness==style.pen.thickness
				    && after.pen.draw_background==style.pen.draw_background && after.write_mode==style.write_mode);
			}
		}
		CHECK(scene.errors==0);
		if (offscreen) CHECK(st_rip3_ports_copy(host.ports,1,NULL,0,&copy,ST_RIP3_TRANSFER_COPY)==0);
		CHECK(st_rip3_palettes_get(host.palettes,0,&palette)==0 && st_rip3_ports_snapshot(host.ports,&palette,&capture)==0);
		unsigned left=offscreen ? 251 : 12,top=offscreen ? 31 : 23;
		unsigned phase_x=offscreen ? 2 : 12,phase_y=offscreen ? 3 : 23;
		for (uint32_t y=0; y<height; y++) for (uint32_t x=0; x<width; x++) {
			unsigned expected=0;
			if (x>=left && x<left+240 && y>=top && y<top+432) {
				unsigned col=(x-left)/12,row=(y-top)/12,u=(x-left)%12,v=(y-top)%12;
				unsigned mode=row%6==5 ? 0 : row%6,group=(row/6)%3,thickness=row<18 ? 1 : 3,type=col%5,flags=col/5,ink=0;
				static const char *const patterns[]={"################","..##..##..##..##","...####...######","...#####...#####"};
				const char *pattern=group==2 ? "################" : type<4 ? patterns[type]
				    : col%2!=0 ? "################" : "...............#";
				bool covered=group!=1 && border(pattern,thickness,u,v,group==0 && flags==1,&ink);
				expected=0x55;
				if (group!=0 && interior(u,v))
					expected=rop(mode,(phase_x+x-left+phase_y+y-top)%2==0 ? 7 : 1,expected);
				if (covered) expected=group==2 ? 12 : rop(mode,ink,expected);
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
	for (int32_t y=0; y<12; y++) for (int32_t x=0; x<12; x++)
		CHECK(st_rip3_ports_read(host->ports,0,x,y,&value)==0 && value==0x55);
	return 0;
}

static int
boundaries(void)
{
	struct st_rip3_command_host host={0},missing;
	struct st_rip3_environment env;
	struct st_rip3_style style;
	const struct st_rip3_rect area={0,0,11,11};
	bool borders=true;
	uint8_t value;
	CHECK(create(&host,ST_RIP3_CANVAS_640_480,640,480)==0);
	CHECK(process(&host, borders ? "N01" : "N00", -1) == 0);
	CHECK(st_rip3_ports_viewport(host.ports,&area)==0 && st_rip3_ports_erase(host.ports,0x55)==0);
	const char *invalid[]={"U", "u01010909", "U010109090?", "u010109092 comment", "U01010909?2"};
	st_rip3_drawing_test_fail_calloc=true;
	for (size_t i=0; i<sizeof(invalid)/sizeof(invalid[0]); i++) CHECK(process(&host,invalid[i],ST_RIP3_COMMAND_INVALID)==0);
	CHECK(st_rip3_drawing_test_fail_calloc && unchanged(&host)==0);st_rip3_drawing_test_fail_calloc=false;
	/* Four corners each allocate a vertex/run array and a coverage mask;
	 * four straight sides allocate coverage. Every failure precedes fill. */
	for (unsigned n=1; n<=8; n++) {
		st_rip3_drawing_test_calloc_countdown=n;
		CHECK(process(&host,"u0101090902",ST_RIP3_COMMAND_RESOURCE_FAILURE)==0);
		CHECK(st_rip3_drawing_test_calloc_countdown==0 && unchanged(&host)==0);
		st_rip3_ellipse_test_realloc_countdown=n;
		CHECK(process(&host,"u0101090902",ST_RIP3_COMMAND_RESOURCE_FAILURE)==0);
		CHECK(st_rip3_ellipse_test_realloc_countdown==0 && unchanged(&host)==0);
	}
	missing=host;missing.styles=NULL;
	CHECK(process(&missing,"u0101090902",ST_RIP3_COMMAND_UNSUPPORTED)==0);
	CHECK(process(&host,"U0101090902 comment",-1)==0); /* U uses STYLE independently of N. */
	CHECK(st_rip3_ports_erase(host.ports,0x55)==0);
	CHECK(st_rip3_ports_activate(host.ports,false)==0);st_rip3_drawing_test_fail_calloc=true;
	CHECK(process(&host,"u0101090902",-1)==0 && st_rip3_drawing_test_fail_calloc);
	st_rip3_drawing_test_fail_calloc=false;CHECK(st_rip3_ports_activate(host.ports,true)==0);
	CHECK(st_rip3_styles_get(host.styles,0,&style)==0 && st_rip3_fill_solid(&style.fill,true)==0);
	style.write_mode=ST_RIP3_NOT;style.fill_color=style.drawing_color=style.background_color=0xffffff;
	style.borders=borders;
	CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
	CHECK(st_rip3_environments_get(host.environments,0,&env)==0);env.rgb_bits=8;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	struct st_rip3_palettes *palette=host.palettes;host.palettes=NULL;
	CHECK(process(&host,"u0101090902",ST_RIP3_COMMAND_UNSUPPORTED)==0 && unchanged(&host)==0);
	CHECK(process(&host,"N00",-1)==0 && process(&host,"u0101090902",-1)==0);
	host.palettes=palette; CHECK(process(&host,"N01",-1)==0);
	style.write_mode=ST_RIP3_COPY;style.fill_color=7;style.drawing_color=12;style.background_color=1;
	style.borders=borders;
	CHECK(st_rip3_styles_set(host.styles,0,&style)==0);env.rgb_bits=0;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	/* Independently fitted 8x6 ellipse, not a common radius-three circle. */
	borders=false;
	CHECK(process(&host, borders ? "N01" : "N00", -1) == 0);
	CHECK(st_rip3_ports_erase(host.ports,0x55)==0 && process(&host,"u0101090705",-1)==0);
	CHECK(st_rip3_ports_read(host.ports,0,2,1,&value)==0 && value==0x55);
	CHECK(st_rip3_ports_read(host.ports,0,4,1,&value)==0 && value==7);
	CHECK(st_rip3_ports_read(host.ports,0,1,3,&value)==0 && value==7);
	/* Normal X-world radius and Y placement are independent. */
	env.world_width=1280;env.world_height=480;CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	CHECK(st_rip3_ports_erase(host.ports,0x55)==0 && process(&host,"u02010I0904",-1)==0);
	CHECK(st_rip3_ports_read(host.ports,0,1,1,&value)==0 && value==0x55);
	CHECK(st_rip3_ports_read(host.ports,0,2,1,&value)==0 && value==7);
	/* The low-WORD Y defect has distinct sign/zero cases. */
	env.world_width=640;env.coordinate_size=5;env.base_math=64;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	char bytes[32];
	const unsigned radii[]={32768,65535,65536};
	for (unsigned i=0; i<3; i++) {
		unsigned values[]={1,1,9,7,radii[i]};bytes[0]='u';
		for (unsigned j=0; j<5; j++) number(bytes+1+5*j,5,values[j],64);
		bytes[26]=0;
		CHECK(st_rip3_ports_erase(host.ports,0x55)==0 && process(&host,bytes,-1)==0);
		CHECK(st_rip3_ports_read(host.ports,0,2,1,&value)==0 && value==(i==0 ? 0x55 : 7));
		CHECK(st_rip3_ports_read(host.ports,0,1,1,&value)==0 && value==(i==2 ? 7 : 0x55));
	}
	/* Maximum five-digit inputs retain wide device geometry at WORLD=1. */
	env.world_width=env.world_height=1;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	const unsigned wide[]={0,0,1073741823,1073741823,0};bytes[0]='u';
	for (unsigned j=0; j<5; j++) number(bytes+1+5*j,5,wide[j],64);
	bytes[26]=0;
	CHECK(st_rip3_ports_erase(host.ports,0x55)==0 && process(&host,bytes,-1)==0);
	for (int32_t y=0; y<12; y++) for (int32_t x=0; x<12; x++)
		CHECK(st_rip3_ports_read(host.ports,0,x,y,&value)==0 && value==7);
	number(bytes+21,5,1073741823,64);
	CHECK(st_rip3_ports_erase(host.ports,0x55)==0 && process(&host,bytes,-1)==0 && unchanged(&host)==0);
	env.world_width=640;env.world_height=480;
	/* Only the final radius can shorten at XY=3. */
	env.coordinate_size=3;env.base_math=36;CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	CHECK(st_rip3_ports_erase(host.ports,0x55)==0 && process(&host,"U00100100900902",-1)==0);
	CHECK(st_rip3_ports_read(host.ports,0,5,1,&value)==0 && value==12);
	CHECK(st_rip3_ports_read(host.ports,0,1,1,&value)==0 && value==0x55);
	env.coordinate_size=2;CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	struct scene scene={&host,0};const struct st_rip3_stream_sink sink={.context=&scene,.command=scene_command};
	struct st_rip3_stream *stream=NULL;CHECK(st_rip3_stream_create(&stream,&sink)==0);
	wire(stream,"!|U010109090?|u0101090900\r");
	CHECK(scene.errors==1 && st_rip3_ports_read(host.ports,0,1,1,&value)==0 && value==7);
	/* Accepted zero extents still consume their used inks; no radius growth. */
	CHECK(st_rip3_ports_erase(host.ports,0x55)==0 && process(&host,"u0404040402",-1)==0 && unchanged(&host)==0);
	borders=true;
	CHECK(process(&host, borders ? "N01" : "N00", -1) == 0);
	CHECK(process(&host,"U0404040402",-1)==0);
	for (int32_t y=0; y<12; y++) for (int32_t x=0; x<12; x++)
		CHECK(st_rip3_ports_read(host.ports,0,x,y,&value)==0 && value==(x==4 && y==4 ? 12 : 0x55));
	CHECK(st_rip3_ports_erase(host.ports,0x55)==0 && process(&host,"u0404040402",-1)==0);
	CHECK(st_rip3_ports_read(host.ports,0,4,4,&value)==0 && value==12);
	CHECK(st_rip3_ports_erase(host.ports,0x55)==0);borders=false;
	CHECK(process(&host, borders ? "N01" : "N00", -1) == 0);
	style.borders=borders;
	style.fill_color=0xffffff;CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
	CHECK(process(&host,"u0404040402",ST_RIP3_COMMAND_OPERATION_FAILED)==0 && unchanged(&host)==0);
	st_rip3_stream_destroy(stream);destroy(&host);return 0;
}

static int
literal_examples(void)
{
	struct st_rip3_command_host host={0};
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture=NULL;
	bool borders=true;
	CHECK(create(&host,ST_RIP3_CANVAS_640_480,640,480)==0);
	CHECK(process(&host, borders ? "N01" : "N00", -1) == 0);
	CHECK(st_rip3_ports_erase(host.ports,0x55)==0);
	CHECK(process(&host,"U8K3KDK6C0K",-1)==0);
	CHECK(process(&host,"u8K7GDKA80K",-1)==0);
	CHECK(st_rip3_palettes_get(host.palettes,0,&palette)==0 && st_rip3_ports_snapshot(host.ports,&palette,&capture)==0);
	/* Literal source coordinates: X=308..488, U Y=128..228,
	 * u Y=268..368, device radius 20 in both axes. */
	const unsigned xs[]={400,308,400,308,400,308,400};
	const unsigned ys[]={128,178,178,128,268,268,318};
	const unsigned expected[]={12,12,0x55,0x55,12,0x55,7};
	for (unsigned i=0; i<7; i++)
		CHECK(capture->primary[(size_t)ys[i]*640+xs[i]]==(ST_PIXEL_COLOR_RGB_TAG | expected[i]*UINT32_C(0x010101)));
	st_pixel_buffer_destroy(capture);destroy(&host);return 0;
}

int
main(void)
{
	CHECK(boundaries()==0);CHECK(literal_examples()==0);
	CHECK(matrix(ST_RIP3_CANVAS_640_480,640,480,false)==0);
	CHECK(matrix(ST_RIP3_CANVAS_640_480,640,480,true)==0);
	CHECK(matrix(ST_RIP3_CANVAS_800_600,800,600,false)==0);
	CHECK(matrix(ST_RIP3_CANVAS_800_600,800,600,true)==0);
	CHECK(matrix(ST_RIP3_CANVAS_1024_768,1024,768,false)==0);
	CHECK(matrix(ST_RIP3_CANVAS_1024_768,1024,768,true)==0);
	return 0;
}
