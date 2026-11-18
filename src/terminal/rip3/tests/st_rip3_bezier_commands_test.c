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
extern unsigned st_rip3_bezier_test_malloc_countdown;
extern unsigned st_rip3_bezier_command_test_malloc_countdown;
extern unsigned st_rip3_polygon_test_malloc_countdown;
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

static void
shape(char *bytes, char kind, unsigned type, unsigned terminal, unsigned x, unsigned y,
    unsigned scale, unsigned xy, unsigned base, unsigned count)
{
	const unsigned controls[]={x+1,y+1,x+1,y+5,x+5,y+5,x+5,y+1};
	size_t pos=1;
	bytes[0]=kind;
	if (kind!='Z') {
		number(bytes+pos,2,2,base);number(bytes+pos+2,2,count,base);pos+=4;
		bytes[pos++]=(char)('0'+type);
	}
	for (unsigned i=0; i<8; i++) {
		if (kind!='Z' && i==6) bytes[pos++]=(char)('0'+terminal);
		number(bytes+pos,xy,controls[i]*scale*(i%2!=0 ? 2 : 1)+(i%2!=0 || scale==2 ? 1 : 0),base);pos+=xy;
	}
	if (kind=='Z') {number(bytes+pos,2,count,base);pos+=2;}
	bytes[pos]=0;
}

static int
matrix(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, bool offscreen)
{
	/* At N=4 the converted controls (1,1),(1,5),(5,5),(5,1)
	 * generate (1,1),(1,3),(3,4),(5,1), deliberately omitting t=3/4.
	 * Literal grids follow those four points and pixel-center intersections. */
	static const char *const arch[]={"........",".#...#..",".#..#...",".##.#...","..##....","........","........","........"};
	static const char *const inside[]={"........",".####...",".###....","..#.....","........","........","........","........"};
	static const unsigned line[]={12,0x59,0x5d,4,0xaa,12};
	static const unsigned fill[]={7,0x52,0x57,5,0xaa,7};
	static const unsigned back[]={1,0x54,0x55,1,0xaa,1};
	struct st_rip3_command_host host={0};
	struct st_rip3_environment env;
	struct st_rip3_style style;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture=NULL;
	struct scene scene={&host,0};
	const struct st_rip3_stream_sink sink={.context=&scene,.command=scene_command};
	struct st_rip3_stream *stream=NULL;
	const struct st_rip3_rect screen={10,20,137,115},bitmap={0,0,127,95};
	const struct st_rip3_rect view={2,3,121,90},copy={250,101,369,188};
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
		for (unsigned row=0; row<6; row++) {
			bool rgb=row==5;env.rgb_bits=rgb ? 8 : 0;
			CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
			style.write_mode=(enum st_rip3_write_mode)(rgb ? 0 : row);
			style.drawing_color=rgb ? 0x0c0c0c : 12;style.fill_color=rgb ? 0x070707 : 7;
			style.background_color=rgb ? 0x010101 : 1;
			style.borders=borders;
			CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
			for (unsigned col=0; col<8; col++) {
				static const char kinds[]={'Z','Z','t','t','z','z','x','x'};
				static const unsigned types[]={1,1,1,2,5,6,1,2};
				static const unsigned terminals[]={0,3,4};
				const struct st_rip3_rect cell={(int32_t)col*10,(int32_t)row*10,(int32_t)col*10+7,(int32_t)row*10+7};
				char bytes[64];borders=col==7;
				CHECK(process(&host, borders ? "N01" : "N00", -1) == 0);
				CHECK(st_rip3_ports_fill(host.ports,&cell,0x55,ST_RIP3_COPY)==0);
				shape(bytes,kinds[col],types[col],terminals[(col+row)%3],col*10,row*10,scale,xy,base,col==0 ? 2 : 4);
				if (col%2!=0) {
					size_t pos=strlen(bytes)-(kinds[col]=='Z' ? 2 : xy);
					unsigned final=kinds[col]=='Z' ? 4 : (row*10+1)*scale*2+1;
					if (final<base) {number(bytes+pos,1,final,base);bytes[pos+1]=0;}
				}
				wire(stream,"!|");wire(stream,bytes);wire(stream,"\r");
			}
		}
		CHECK(scene.errors==0);
		if (offscreen) CHECK(st_rip3_ports_copy(host.ports,1,NULL,0,&copy,ST_RIP3_TRANSFER_COPY)==0);
		CHECK(st_rip3_palettes_get(host.palettes,0,&palette)==0 && st_rip3_ports_snapshot(host.ports,&palette,&capture)==0);
		unsigned left=offscreen ? 250 : 12,top=offscreen ? 101 : 23;
		unsigned phase_x=offscreen ? 2 : 12,phase_y=offscreen ? 3 : 23;
		for (uint32_t y=0; y<height; y++) for (uint32_t x=0; x<width; x++) {
			unsigned expected=0;
			if (x>=left && x<left+80 && y>=top && y<top+60) {
				unsigned col=(x-left)/10,row=(y-top)/10,u=(x-left)%10,v=(y-top)%10;
				if (u<8 && v<8) {
					bool edge=col==0 ? v==1 && u>=1 && u<=5 : arch[v][u]=='#';
					if (col>=4 && v==1 && u>=1 && u<=5) edge=true;
					expected=0x55;
					if (col>=6 && inside[v][u]=='#')
						expected=((phase_x+x-left+phase_y+y-top)%2==0 ? fill : back)[row];
					if (edge && col<6) expected=line[row];
					if (edge && col==7) expected=12;
				}
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
	static const char *const invalid[]={"Z","Z0101010505050501","Z0101010505050501040","Z01010105050505010?",
	    "z0004","z01041","z010470101","z01040010?","z020400101","t01041010101050505",
	    "z02041010101050505005010","z0200101010105050500501"};
	struct st_rip3_command_host host={0},missing;
	struct st_rip3_environment env;
	struct st_rip3_style style,original;
	const struct st_rip3_rect area={0,0,7,7};
	bool borders=true;
	CHECK(create(&host,ST_RIP3_CANVAS_640_480,640,480)==0);
	CHECK(process(&host, borders ? "N01" : "N00", -1) == 0);
	CHECK(st_rip3_ports_viewport(host.ports,&area)==0 && st_rip3_ports_erase(host.ports,0x55)==0);
	CHECK(st_rip3_styles_get(host.styles,0,&original)==0);
	st_rip3_bezier_command_test_malloc_countdown=1;
	for (size_t i=0; i<sizeof(invalid)/sizeof(invalid[0]); i++) CHECK(process(&host,invalid[i],ST_RIP3_COMMAND_INVALID)==0);
	CHECK(st_rip3_bezier_command_test_malloc_countdown==1 && unchanged(&host)==0);
	st_rip3_bezier_command_test_malloc_countdown=0;
	/* Z N=0/1 never submits a drawable polyline, including with missing
	 * graphics services; its complete numeric body is still validated. */
	missing=host;missing.styles=NULL;missing.ports=NULL;missing.palettes=NULL;
	CHECK(process(&missing,"Z010101050505050100",-1)==0 && process(&missing,"Z01010105050505011",-1)==0);
	/* One submitted point cannot draw through either native API. In
	 * particular a lone N=1 cubic is distinct from its two scratch outputs. */
	missing=host;missing.styles=NULL;missing.palettes=NULL;missing.styles=NULL;
	CHECK(process(&missing,"z01011010101050505",-1)==0 && unchanged(&host)==0);
	CHECK(process(&missing,"x01011010101050505",-1)==0 && unchanged(&host)==0);
	CHECK(process(&missing,"t010000101",-1)==0 && unchanged(&host)==0);
	CHECK(process(&host,"Z010101050505050104 comment",ST_RIP3_COMMAND_UNSUPPORTED)==0);
	for (const char *kind="Zztx"; *kind!=0; kind++) {
		char bytes[64];shape(bytes,*kind,1,0,0,0,1,2,36,4);
		for (unsigned allocation=1; allocation<=2; allocation++) {
			st_rip3_bezier_command_test_malloc_countdown=allocation;
			CHECK(process(&host,bytes,ST_RIP3_COMMAND_RESOURCE_FAILURE)==0);
			CHECK(st_rip3_bezier_command_test_malloc_countdown==0 && unchanged(&host)==0);
		}
		if (*kind!='Z') {
			st_rip3_bezier_test_malloc_countdown=1;
			CHECK(process(&host,bytes,ST_RIP3_COMMAND_RESOURCE_FAILURE)==0);
			CHECK(st_rip3_bezier_test_malloc_countdown==0 && unchanged(&host)==0);
		}
		st_rip3_drawing_test_fail_calloc=true;
		CHECK(process(&host,bytes,ST_RIP3_COMMAND_RESOURCE_FAILURE)==0 && !st_rip3_drawing_test_fail_calloc);
		CHECK(unchanged(&host)==0);
	}
	char bytes[64];shape(bytes,'x',1,0,0,0,1,2,36,4);
	for (unsigned allocation=1; allocation<=2; allocation++) {
		st_rip3_polygon_test_malloc_countdown=allocation;
		CHECK(process(&host,bytes,ST_RIP3_COMMAND_RESOURCE_FAILURE)==0);
		CHECK(st_rip3_polygon_test_malloc_countdown==0 && unchanged(&host)==0);
	}
	style=original;style.drawing_color=0xffffff;
	style.borders=borders;
	CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
	CHECK(process(&host,"Z010101050505050104",ST_RIP3_COMMAND_OPERATION_FAILED)==0 && unchanged(&host)==0);
	style=original;style.fill_color=0xffffff;
	style.borders=borders;
	CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
	CHECK(process(&host,bytes,ST_RIP3_COMMAND_OPERATION_FAILED)==0 && unchanged(&host)==0);
	original.borders=borders;
	CHECK(st_rip3_styles_set(host.styles,0,&original)==0);
	missing=host;missing.styles=NULL;
	CHECK(process(&missing,bytes,ST_RIP3_COMMAND_UNSUPPORTED)==0 && unchanged(&host)==0);
	CHECK(st_rip3_ports_activate(host.ports,false)==0);
	st_rip3_bezier_command_test_malloc_countdown=1;st_rip3_bezier_test_malloc_countdown=1;
	CHECK(process(&missing,bytes,-1)==0);
	CHECK(st_rip3_bezier_command_test_malloc_countdown==1 && st_rip3_bezier_test_malloc_countdown==1 && unchanged(&host)==0);
	st_rip3_bezier_command_test_malloc_countdown=0;st_rip3_bezier_test_malloc_countdown=0;
	CHECK(st_rip3_ports_activate(host.ports,true)==0);
	/* A terminal curve is closed to the first record in z/x; t rejects
	 * the same body without drawing a prefix. N=2 makes the closed pair equal. */
	CHECK(process(&host,"t01021010101050505",ST_RIP3_COMMAND_INVALID)==0 && unchanged(&host)==0);
	CHECK(process(&host,"z01021010101050505",-1)==0);
	uint8_t value;
	CHECK(st_rip3_ports_read(host.ports,0,1,1,&value)==0 && value==12);
	CHECK(st_rip3_ports_erase(host.ports,0x55)==0);
	CHECK(process(&host,"x01021010101050505",-1)==0);
	CHECK(st_rip3_ports_read(host.ports,0,1,1,&value)==0 && value==12);
	CHECK(st_rip3_ports_erase(host.ports,0x55)==0);
	/* Constant-y cubic/point joins test duplicate endpoint union, continuing
	 * custom-pattern phase and one XOR at every covered pixel. */
	style=original;style.pen.pattern=0xaaaa;style.pen.draw_background=true;style.write_mode=ST_RIP3_XOR;
	style.borders=borders;
	CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
	CHECK(process(&host,"t0204100030203060300703",-1)==0);
	for (int32_t y=0; y<8; y++) for (int32_t x=0; x<8; x++)
		CHECK(st_rip3_ports_read(host.ports,0,x,y,&value)==0 && value==(y==3 ? (x%2==0 ? 0x59 : 0x54) : 0x55));
	original.borders=borders;
	CHECK(st_rip3_styles_set(host.styles,0,&original)==0 && st_rip3_ports_erase(host.ports,0x55)==0);
	/* Degenerate filled point records still use the polygon border's
	 * patterned COPY rule; unused NOT-interior inks need no lookup. */
	style=original;style.pen.pattern=0xaaaa;style.pen.draw_background=false;
	style.write_mode=ST_RIP3_NOT;style.fill_color=style.background_color=0xffffff;
	style.borders=borders;
	CHECK(st_rip3_styles_set(host.styles,0,&style)==0 && st_rip3_ports_erase(host.ports,0x55)==0);
	CHECK(process(&host,"x" "02" "00" "0" "0003" "0" "0703",-1)==0);
	for (int32_t y=0; y<8; y++) for (int32_t x=0; x<8; x++)
		CHECK(st_rip3_ports_read(host.ports,0,x,y,&value)==0 && value==(y==3 && x%2==0 ? 12 : 0x55));
	style.pen.draw_background=true;
	style.borders=borders;
	CHECK(st_rip3_styles_set(host.styles,0,&style)==0 && st_rip3_ports_erase(host.ports,0x55)==0);
	CHECK(process(&host,"x" "02" "00" "0" "0003" "0" "0703",ST_RIP3_COMMAND_OPERATION_FAILED)==0 && unchanged(&host)==0);
	style.borders=borders;
	style.background_color=1;CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
	CHECK(process(&host,"x" "02" "00" "0" "0003" "0" "0703",-1)==0);
	for (int32_t y=0; y<8; y++) for (int32_t x=0; x<8; x++)
		CHECK(st_rip3_ports_read(host.ports,0,x,y,&value)==0 && value==(y==3 ? (x%2==0 ? 12 : 1) : 0x55));
	original.borders=borders;
	CHECK(st_rip3_styles_set(host.styles,0,&original)==0 && st_rip3_ports_erase(host.ports,0x55)==0);
	/* Valid complete original SHAPES.RIP sample, followed by truncation and
	 * a simple curve. Recovery must be owned by the parser and preserve state. */
	struct scene scene={&host,0};
	const struct st_rip3_stream_sink sink={.context=&scene,.command=scene_command};
	struct st_rip3_stream *stream=NULL;
	CHECK(st_rip3_stream_create(&stream,&sink)==0);
	wire(stream,"!|z081428KGC8KESASF85CGF8E4F8BCGC5DKGCEOGCE4GW5DKHGD0I0BWI06ASI0BWGWASFS09OGW09OI008KI0|");
	wire(stream,"z01041|Z010101050505050100|Z010101050505050102\r");
	CHECK(scene.errors==1);
	for (int32_t y=0; y<8; y++) for (int32_t x=0; x<8; x++)
		CHECK(st_rip3_ports_read(host.ports,0,x,y,&value)==0 && value==(y==1 && x>=1 && x<=5 ? 12 : 0x55));
	st_rip3_stream_destroy(stream);
	/* Wider coordinate words reach distinct signed16 cubic endpoints and
	 * unsigned16 point records after device conversion. */
	CHECK(st_rip3_environments_get(host.environments,0,&env)==0);env.base_math=64;env.coordinate_size=5;
	CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	char wide[64]="Z";
	const unsigned coordinates[]={5,3,5,3,5,3,65537,3};
	for (unsigned i=0; i<8; i++) number(wide+1+5*i,5,coordinates[i],64);
	number(wide+41,2,2,64);wide[43]=0;
	CHECK(st_rip3_ports_erase(host.ports,0x55)==0 && process(&host,wide,-1)==0);
	for (int32_t y=0; y<8; y++) for (int32_t x=0; x<8; x++)
		CHECK(st_rip3_ports_read(host.ports,0,x,y,&value)==0 && value==(y==3 && x>=1 && x<=5 ? 12 : 0x55));
	/* Standalone Z accepts the full count range without poly reduction;
	 * t applies the independent generated limit. Constant controls expose
	 * a single XOR write despite thousands of repeated vertices. */
	style=original;style.write_mode=ST_RIP3_XOR;
	style.borders=borders;
	CHECK(st_rip3_styles_set(host.styles,0,&style)==0);
	for (unsigned i=0; i<8; i++) number(wide+1+5*i,5,i%2==0 ? 1 : 3,64);
	number(wide+41,2,4095,64);
	CHECK(st_rip3_ports_erase(host.ports,0x55)==0 && process(&host,wide,-1)==0);
	CHECK(st_rip3_ports_read(host.ports,0,1,3,&value)==0 && value==0x59);
	char many[64];many[0]='t';number(many+1,2,2,64);number(many+3,2,4095,64);many[5]='6';
	for (unsigned i=0; i<6; i++) number(many+6+5*i,5,i%2==0 ? 1 : 3,64);
	many[36]='4';number(many+37,5,1,64);number(many+42,5,3,64);many[47]=0;
	CHECK(st_rip3_ports_erase(host.ports,0x55)==0 && process(&host,many,-1)==0);
	CHECK(st_rip3_ports_read(host.ports,0,1,3,&value)==0 && value==0x59);
	original.borders=borders;
	CHECK(st_rip3_styles_set(host.styles,0,&original)==0);
	/* Collection capacity is separate from the generated-point budget. */
	env.coordinate_size=2;CHECK(st_rip3_environments_set(host.environments,0,&env)==0);
	char *large=malloc(5+2731*5+1);CHECK(large!=NULL);
	large[0]='z';number(large+1,2,2730,64);number(large+3,2,0,64);
	for (unsigned i=0; i<2731; i++) memcpy(large+5+5*i,"00101",5);
	large[5+2730*5]=0;
	CHECK(st_rip3_ports_erase(host.ports,0x55)==0 && process(&host,large,-1)==0);
	number(large+1,2,2731,64);memcpy(large+5+2730*5,"00101",6);
	CHECK(st_rip3_ports_erase(host.ports,0x55)==0 && process(&host,large,ST_RIP3_COMMAND_INVALID)==0 && unchanged(&host)==0);
	free(large);destroy(&host);return 0;
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
