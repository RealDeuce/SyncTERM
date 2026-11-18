/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "io_error.h"
#include "st_rip3_text_commands.h"
#include "st_rip3_classic_text.h"
#include "st_rip3_bitmap.h"
#include "st_rip3_environments.h"
#include "st_rip3_styles.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_rip3_stream.h"
#include "st_rip3_number.h"
#include "st_pixels.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

extern bool st_rip3_text_commands_test_fail_allocate;
extern bool st_rip3_stroke_text_test_fail_allocate;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr,"check failed at line %d: %s\n",__LINE__,#expression); \
		return 1; \
	} \
} while (0)

struct fixture {
	struct st_rip3_command_host host;
	struct st_rip3_text_context text;
	struct st_rip3_classic_font font;
	uint8_t glyphs[255*3];
	unsigned errors;
	enum st_rip3_command_diagnostic kind;
};

static void
diagnostic(void *context, enum st_rip3_command_diagnostic kind)
{
	struct fixture *f = context; f->errors++; f->kind = kind;
}

static void
command(void *context, const uint8_t *bytes, size_t length)
{
	struct fixture *f = context;
	const struct st_rip3_command_sink sink = {f,diagnostic};
	st_rip3_commands_process(&f->host,&sink,bytes,length);
}

static int
process(struct fixture *f, const char *body, int error)
{
	unsigned previous = f->errors;
	command(f,(const uint8_t *)body,strlen(body));
	CHECK(f->errors == previous+(error < 0 ? 0u : 1u));
	if (error >= 0) CHECK((int)f->kind == error);
	return 0;
}

static struct st_rip3_cursor
cursor(struct fixture *f)
{
	struct st_rip3_port_info info;
	unsigned number;
	if (st_rip3_ports_current(f->host.ports,&number)!=0 || st_rip3_ports_info(f->host.ports,number,&info)!=0)
		return (struct st_rip3_cursor){INT64_MIN,INT64_MIN};
	return (struct st_rip3_cursor){info.cursor.x-info.bounds.x0-info.viewport.x0,
	    info.cursor.y-info.bounds.y0-info.viewport.y0};
}

static int
position(struct fixture *f, int64_t x, int64_t y)
{
	struct st_rip3_port_info info;
	unsigned number;
	CHECK(st_rip3_ports_current(f->host.ports,&number)==0 && st_rip3_ports_info(f->host.ports,number,&info)==0);
	struct st_rip3_cursor point={x+info.bounds.x0+info.viewport.x0,y+info.bounds.y0+info.viewport.y0};
	CHECK(st_rip3_ports_cursor(f->host.ports,number,&point)==0);return 0;
}

static int
create(struct fixture *f, enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_style style = {.drawing_color=12,.background_color=8,.fill_color=2,
	    .pen={.pattern=0xaaaa,.thickness=3,.draw_background=true},.font={0,0,1,0},.write_mode=ST_RIP3_COPY};
	const struct st_rip3_rect bounds = {10,20,79,64}, viewport = {2,1,65,40};
	CHECK(st_rip3_fill_solid(&style.fill,true) == 0 && st_rip3_styles_create(&f->host.styles,&style) == 0);
	{
		struct st_rip3_style prepared;
		CHECK(st_rip3_styles_get(f->host.styles,0,&prepared)==0
		    && st_rip3_styles_define(f->host.styles,3,&prepared)==0);
	}
	CHECK(st_rip3_styles_switch(f->host.styles,3) == 0 && st_rip3_styles_protect(f->host.styles,3,true) == 0);
	CHECK(st_rip3_environments_create(&f->host.environments,canvas) == 0
	    && st_rip3_ports_create(&f->host.ports,canvas) == 0 && st_rip3_palettes_create(&f->host.palettes) == 0);
	CHECK(st_rip3_ports_define(f->host.ports,1,&bounds,false) == 0 && st_rip3_ports_switch(f->host.ports,1) == 0
	    && st_rip3_ports_viewport(f->host.ports,&viewport) == 0 && st_rip3_ports_erase(f->host.ports,85) == 0
	    && st_rip3_ports_protect(f->host.ports,1,true) == 0);
	uint32_t colors[256]; for (unsigned i = 0; i < 256; i++) colors[i] = i*UINT32_C(0x010101);
	CHECK(st_rip3_palettes_switch(f->host.palettes,1) == 0 && st_rip3_palettes_rgb(f->host.palettes,0,8,colors,256) == 0
	    && st_rip3_palettes_protect(f->host.palettes,1,true) == 0);
	struct st_rip3_environment environment;
	CHECK(st_rip3_environments_get(f->host.environments,0,&environment) == 0);
	environment.world_width = width; environment.world_height = height; environment.port = 9; environment.palette = 7;
	CHECK(st_rip3_environments_define(f->host.environments,2,&environment) == 0
	    && st_rip3_environments_select(f->host.environments,2) == 0 && st_rip3_environments_protect(f->host.environments,2,true) == 0);
	const uint8_t a[] = {0x40,0xa0,0xe0}, b[] = {0xc0,0xa0,0xc0}, e[] = {0xe0,0x80,0xe0}, q[] = {0xe0,0x20,0x40};
	memcpy(f->glyphs+('A'-1)*3,a,3); memcpy(f->glyphs+('B'-1)*3,b,3);
	memcpy(f->glyphs+129*3,e,3); memcpy(f->glyphs+('?' -1)*3,q,3); memset(f->glyphs+218*3,0xe0,3);
	f->font = (struct st_rip3_classic_font){.canvas=canvas,.number=0,.data.bitmap={3,3,1,3,f->glyphs}};
	f->text = (struct st_rip3_text_context){.encoding=ST_RIP3_TEXT_CP437,.font=&f->font};
	f->host.text = &f->text; CHECK(position(f,40,15)==0); return 0;
}

static void
destroy(struct fixture *f)
{
	st_rip3_styles_destroy(f->host.styles); st_rip3_environments_destroy(f->host.environments);
	st_rip3_ports_destroy(f->host.ports); st_rip3_palettes_destroy(f->host.palettes);
}

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct fixture f = {0}; struct st_rip3_stream *stream = NULL;
	CHECK(create(&f,canvas,width,height) == 0);
	CHECK(process(&f,"m0509",-1) == 0 && cursor(&f).x == 5 && cursor(&f).y == 9);
	const struct st_rip3_stream_sink sink = {.context=&f,.command=command};
	CHECK(st_rip3_stream_create(&stream,&sink) == 0);
	const uint8_t wire[] = "!|TAB|LZZZZZZZZ|T\x82|@0K02A|m020D|TA B|m0F0D|T\xdb|";
	for (size_t i = 0; i < sizeof(wire)-1; i++) st_rip3_stream_process(stream,wire+i,1);
	CHECK(f.errors == 0 && cursor(&f).x == 18 && cursor(&f).y == 13);
	f.text.encoding = ST_RIP3_TEXT_UTF8;
	CHECK(process(&f,"@0U02\xc3\xa9",-1) == 0 && cursor(&f).x == 33 && cursor(&f).y == 2);
	CHECK(process(&f,"@0Z02\xf0\x9f\x99\x82",-1) == 0);
	CHECK(process(&f,"@1402\xe2\x96\x88",-1) == 0 && cursor(&f).x == 43);
	CHECK(process(&f,"T",-1) == 0 && cursor(&f).x == 43 && cursor(&f).y == 2);
	CHECK(process(&f,"@0203",ST_RIP3_COMMAND_INVALID) == 0 && cursor(&f).x == 43 && cursor(&f).y == 2);
	struct st_rip3_palette palette; struct st_rip3_bitmap *capture = NULL;
	CHECK(st_rip3_palettes_get(f.host.palettes,1,&palette) == 0 && st_rip3_bitmap_capture(&capture,f.host.ports,0,NULL,&palette) == 0);
	const struct { unsigned x,y; const char *rows[3]; } glyphs[] = {
	    {5,9,{".#.","#.#","###"}},{8,9,{"##.","#.#","##."}},{11,9,{"###","#..","###"}},
	    {20,2,{".#.","#.#","###"}},{2,13,{".#.","#.#","###"}},{8,13,{"##.","#.#","##."}},
	    {15,13,{"###","###","###"}},{30,2,{"###","#..","###"}},
	    {35,2,{"###","..#",".#."}},{40,2,{"###","###","###"}},
	};
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		uint8_t expected = 0;
		if (x >= 12 && x <= 75 && y >= 21 && y <= 60) {
			uint32_t vx = x-12, vy = y-21; expected = 85;
			for (size_t i = 0; i < sizeof(glyphs)/sizeof(glyphs[0]); i++)
				if (vx >= glyphs[i].x && vx < glyphs[i].x+3 && vy >= glyphs[i].y && vy < glyphs[i].y+3
				    && glyphs[i].rows[vy-glyphs[i].y][vx-glyphs[i].x] == '#') expected = 12;
		}
		CHECK(capture->pixels->primary[(size_t)y*width+x] == expected);
	}
	for (unsigned i = 0; i < 256; i++) CHECK(capture->palette[i] == i*UINT32_C(0x010101));
	unsigned number;
	struct st_rip3_environment environment; struct st_rip3_style_info style_info; struct st_rip3_port_info port_info;
	CHECK(st_rip3_ports_current(f.host.ports,&number) == 0 && number == 1
	    && st_rip3_ports_info(f.host.ports,1,&port_info) == 0 && port_info.protected);
	CHECK(st_rip3_styles_current(f.host.styles,&number) == 0 && number == 3
	    && st_rip3_styles_info(f.host.styles,3,&style_info) == 0 && style_info.protected);
	CHECK(st_rip3_environments_get(f.host.environments,2,&environment) == 0 && environment.port == 9 && environment.palette == 7);
	st_rip3_bitmap_destroy(capture); st_rip3_stream_destroy(stream); destroy(&f); return 0;
}

static int
set_style(struct fixture *f, const struct st_rip3_style *style)
{
	CHECK(st_rip3_styles_protect(f->host.styles,3,false) == 0 && st_rip3_styles_set(f->host.styles,3,style) == 0
	    && st_rip3_styles_protect(f->host.styles,3,true) == 0); return 0;
}

static int
flat(struct fixture *f, uint8_t color)
{
	struct st_rip3_palette palette; struct st_rip3_bitmap *capture = NULL;
	CHECK(st_rip3_palettes_get(f->host.palettes,1,&palette) == 0 && st_rip3_bitmap_capture(&capture,f->host.ports,1,NULL,&palette) == 0);
	for (size_t i = 0; i < (size_t)capture->pixels->width*capture->pixels->height; i++)
		CHECK(capture->pixels->primary[i] == color);
	st_rip3_bitmap_destroy(capture); return 0;
}

static int
modes(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct fixture f = {0}; struct st_rip3_style style;
	CHECK(create(&f,canvas,width,height) == 0 && st_rip3_styles_get(f.host.styles,3,&style) == 0);
	const uint8_t values[] = {12,89,93,4,170};
	for (unsigned mode = 0; mode < 5; mode++) {
		style.write_mode = (enum st_rip3_write_mode)mode; CHECK(set_style(&f,&style) == 0);
		CHECK(st_rip3_ports_erase(f.host.ports,85) == 0 && process(&f,"@0202A",-1) == 0 && process(&f,"TB",-1) == 0);
		CHECK(cursor(&f).x == 8 && cursor(&f).y == 2);
		const char *const rows[] = {".#.##.","#.##.#","#####."};
		struct st_rip3_palette palette; struct st_rip3_bitmap *capture = NULL;
		CHECK(st_rip3_palettes_get(f.host.palettes,1,&palette) == 0 && st_rip3_bitmap_capture(&capture,f.host.ports,1,NULL,&palette) == 0);
		for (uint32_t y = 0; y < 40; y++) for (uint32_t x = 0; x < 64; x++) {
			uint8_t expected = x >= 2 && x < 8 && y >= 2 && y < 5 && rows[y-2][x-2] == '#' ? values[mode] : 85;
			CHECK(capture->pixels->primary[(size_t)y*64+x] == expected);
		}
		st_rip3_bitmap_destroy(capture);
	}
	style.write_mode = ST_RIP3_COPY; style.font.flags = 32; CHECK(set_style(&f,&style) == 0);
	CHECK(st_rip3_ports_erase(f.host.ports,85) == 0 && process(&f,"@0202A",-1) == 0 && cursor(&f).x == 5);
	const char *const shadow[] = {".F..","F.F.","FFFS",".SSS"};
	for (int32_t y = 0; y < 4; y++) for (int32_t x = 0; x < 4; x++) {
		uint8_t actual, expected = shadow[y][x] == 'F' ? 12 : shadow[y][x] == 'S' ? 8 : 85;
		CHECK(st_rip3_ports_read(f.host.ports,1,4+x,3+y,&actual) == 0 && actual == expected);
	}
	CHECK(process(&f,"E",-1) == 0 && cursor(&f).x == 5 && cursor(&f).y == 2 && flat(&f,8) == 0);
	for (unsigned variant = 0; variant < 3; variant++) {
		style.font.flags = variant == 0 ? 0 : variant == 1 ? 1 : 2;
		style.font.direction = variant == 0 ? 1 : 0;
		CHECK(set_style(&f,&style) == 0 && st_rip3_ports_erase(f.host.ports,85) == 0);
		CHECK(position(&f,20,20)==0);
		CHECK(process(&f,"TA",-1) == 0 && cursor(&f).x == 23 && cursor(&f).y == 20);
		CHECK(process(&f,"@0101A",ST_RIP3_COMMAND_UNSUPPORTED) == 0 && cursor(&f).x == 23 && cursor(&f).y == 20);
		const char *const a[] = {".#.","#.#","###"}, *const vertical[] = {".##","#.#",".##"};
		unsigned ox = variant == 0 ? 20 : variant == 1 ? 17 : 19, oy = variant == 0 ? 18 : 20;
		struct st_rip3_palette palette; struct st_rip3_bitmap *capture = NULL;
		CHECK(st_rip3_palettes_get(f.host.palettes,1,&palette) == 0 && st_rip3_bitmap_capture(&capture,f.host.ports,1,NULL,&palette) == 0);
		for (uint32_t y = 0; y < 40; y++) for (uint32_t x = 0; x < 64; x++) {
			uint8_t expected = 85;
			if (x >= ox && x < ox+3 && y >= oy && y < oy+3
			    && (variant == 0 ? vertical[y-oy][x-ox] : a[y-oy][x-ox]) == '#') expected = 12;
			CHECK(capture->pixels->primary[(size_t)y*64+x] == expected);
		}
		st_rip3_bitmap_destroy(capture);
	}
	destroy(&f); return 0;
}

static int
numeric(void)
{
	for (unsigned radix = 36; radix <= 64; radix += 28) for (unsigned size = 2; size <= 5; size++) {
		struct fixture f = {0}; struct st_rip3_environment environment;
		CHECK(create(&f,ST_RIP3_CANVAS_640_480,640,480) == 0);
		CHECK(st_rip3_environments_get(f.host.environments,2,&environment) == 0);
		environment.base_math = radix; environment.coordinate_size = size; environment.world_width = 320; environment.world_height = 240;
		CHECK(st_rip3_environments_protect(f.host.environments,2,false) == 0
		    && st_rip3_environments_set(f.host.environments,2,&environment) == 0
		    && st_rip3_environments_protect(f.host.environments,2,true) == 0);
		char body[32] = {'m'};
		CHECK(st_rip3_number_encode(body+1,size,2,size,radix) == 0 && st_rip3_number_encode(body+1+size,size,3,size,radix) == 0);
		body[1+2*size] = 0;
		CHECK(process(&f,body,-1) == 0 && cursor(&f).x == 4 && cursor(&f).y == 6);
		CHECK(process(&f,"TA",-1) == 0 && cursor(&f).x == 7 && cursor(&f).y == 6);
		body[0] = '@'; body[1+2*size] = 'B'; body[2+2*size] = 0;
		CHECK(process(&f,body,-1) == 0 && cursor(&f).x == 7 && cursor(&f).y == 6);
		const uint8_t expected[] = {12,12,85,12,85,12,12,12,85};
		/* COPY B overwrites only its ink; the previous A's uncovered pixels remain. */
		for (int32_t y = 0; y < 3; y++) for (int32_t x = 0; x < 3; x++) {
			uint8_t actual;
			uint8_t value = expected[y*3+x]; if ((x == 1 && y == 0) || y == 2) value = 12;
			CHECK(st_rip3_ports_read(f.host.ports,1,6+x,7+y,&actual) == 0 && actual == value);
		}
		destroy(&f);
	}
	struct fixture f = {0}; struct st_rip3_environment environment;
	CHECK(create(&f,ST_RIP3_CANVAS_640_480,640,480) == 0 && process(&f,"m050",-1) == 0
	    && cursor(&f).x == 5 && cursor(&f).y == 0);
	CHECK(process(&f,"m0509",-1) == 0 && process(&f,"Thello world",-1) == 0 && cursor(&f).x == 38 && cursor(&f).y == 9);
	CHECK(process(&f,"@HS2ARIPscrip Buttons",-1) == 0 && cursor(&f).x == 688 && cursor(&f).y == 82);
	CHECK(st_rip3_environments_get(f.host.environments,2,&environment) == 0);
	environment.base_math = 64; environment.coordinate_size = 5; environment.world_width = environment.world_height = 1;
	CHECK(st_rip3_environments_protect(f.host.environments,2,false) == 0 && st_rip3_environments_set(f.host.environments,2,&environment) == 0);
	CHECK(process(&f,"m&&&&&&&&&&",-1) == 0 && cursor(&f).x == 687194766720.0 && cursor(&f).y == 515396075040.0);
	CHECK(process(&f,"TA",-1) == 0 && cursor(&f).x == 687194766723.0 && flat(&f,85) == 0);
	destroy(&f); return 0;
}

static int
vector_and_failures(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct fixture f = {0}; CHECK(create(&f,canvas,width,height) == 0);
	struct st_rip3_style mode;
	CHECK(st_rip3_styles_get(f.host.styles,3,&mode)==0);
	mode.font_mode=ST_RIP3_FONT_OUTLINE;mode.outline.spacing=100;
	CHECK(st_rip3_styles_protect(f.host.styles,3,false)==0 && st_rip3_styles_set(f.host.styles,3,&mode)==0
	    && st_rip3_styles_protect(f.host.styles,3,true)==0);
	CHECK(process(&f,"TA",ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&f,"Y01000400",ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && st_rip3_styles_get(f.host.styles,3,&mode)==0 && mode.font_mode==ST_RIP3_FONT_OUTLINE);
	uint8_t bytes[159] = {0};
	const uint8_t program[] = {0x80,0,0x84,0x80,0x84,0x84,0x80,0x84,0x80,0x80,0,0};
	memcpy(bytes,"PK\010\010TEST\032",9); bytes[9] = 128; memcpy(bytes+11,"TEST",4); bytes[15] = 31;
	bytes[17] = bytes[19] = 1; bytes[128] = '+'; bytes[129] = 1; bytes[132] = 'A'; bytes[133] = 19;
	bytes[136] = 4; bytes[146] = 5; memcpy(bytes+147,program,sizeof(program));
	struct st_rip3_classic_font font = {.canvas=canvas,.number=1};
	CHECK(st_rip3_chr_open(&font.data.stroke,bytes,sizeof(bytes)) == 0);
	f.text.font = &font; CHECK(position(&f,10,6)==0);
	CHECK(st_rip3_styles_protect(f.host.styles,3,false) == 0 && process(&f,"Y01000400",-1) == 0
	    && st_rip3_styles_get(f.host.styles,3,&mode)==0 && mode.font_mode==ST_RIP3_FONT_CLASSIC && cursor(&f).x == 10 && cursor(&f).y == 6);
	CHECK(st_rip3_styles_protect(f.host.styles,3,true) == 0);
	int64_t step = width == 640 ? 5 : width == 800 ? 6 : 8;
	CHECK(process(&f,"TA",-1) == 0 && process(&f,"TA",-1) == 0 && cursor(&f).x == 10+2*step && cursor(&f).y == 6);
	CHECK(process(&f,"E",-1) == 0 && cursor(&f).x == 10+2*step && flat(&f,8) == 0);
	st_rip3_stroke_text_test_fail_allocate = true;
	CHECK(process(&f,"TA",ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_stroke_text_test_fail_allocate
	    && cursor(&f).x == 10+2*step && flat(&f,8) == 0);
	CHECK(process(&f,"TA",-1) == 0 && cursor(&f).x == 10+3*step);
	CHECK(st_rip3_styles_protect(f.host.styles,3,false) == 0 && process(&f,"Y0A000400",-1) == 0
	    && st_rip3_styles_protect(f.host.styles,3,true) == 0);
	CHECK(process(&f,"E",-1) == 0);
	int64_t previous = cursor(&f).x;
	CHECK(process(&f,"TA:",ST_RIP3_COMMAND_UNSUPPORTED) == 0 && cursor(&f).x == previous && flat(&f,8) == 0);
	destroy(&f); return 0;
}

static int
errors(void)
{
	struct fixture f = {0}; CHECK(create(&f,ST_RIP3_CANVAS_640_480,640,480) == 0);
	const char *const unsupported[] = {"T$RESX$","T<<A>>","T((A))","T[ABC]","T^M","T`M","T\\A"};
	for (size_t i = 0; i < sizeof(unsupported)/sizeof(unsupported[0]); i++) CHECK(process(&f,unsupported[i],ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&f,"m0",ST_RIP3_COMMAND_INVALID) == 0 && process(&f,"@000",ST_RIP3_COMMAND_INVALID) == 0);
	f.text.encoding = ST_RIP3_TEXT_UTF8;
	CHECK(process(&f,"T\xc3",ST_RIP3_COMMAND_INVALID) == 0);
	st_rip3_text_commands_test_fail_allocate = true;
	CHECK(process(&f,"TA",ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_text_commands_test_fail_allocate);
	CHECK(cursor(&f).x == 40 && cursor(&f).y == 15 && flat(&f,85) == 0);
	const struct st_rip3_cursor invalid={INT64_MAX,0};
	CHECK(st_rip3_ports_cursor(f.host.ports,1,&invalid)==IO_ERROR_OUT_OF_RANGE);
	CHECK(cursor(&f).x==40 && cursor(&f).y==15);
	f.font.canvas = ST_RIP3_CANVAS_800_600;
	CHECK(process(&f,"TA",ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && cursor(&f).x == 40 && flat(&f,85) == 0);
	f.font.canvas = ST_RIP3_CANVAS_640_480;
	struct st_rip3_text_context *context = f.host.text; f.host.text = NULL;
	CHECK(process(&f,"TA",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.text = context;
	CHECK(process(&f,"@0000A",-1) == 0 && cursor(&f).x == 3 && cursor(&f).y == 0);
	struct st_rip3_stream *stream = NULL;
	const struct st_rip3_stream_sink sink = {.context=&f,.command=command};
	CHECK(st_rip3_stream_create(&stream,&sink) == 0);
	const uint8_t recovery[] = "!|m0|T\xc3|m0202|TA|";
	unsigned previous = f.errors;
	for (size_t i = 0; i < sizeof(recovery)-1; i++) st_rip3_stream_process(stream,recovery+i,1);
	CHECK(f.errors == previous+2 && cursor(&f).x == 5 && cursor(&f).y == 2);
	st_rip3_stream_destroy(stream);
	CHECK(st_rip3_ports_protect(f.host.ports,1,false) == 0 && st_rip3_ports_activate(f.host.ports,false) == 0);
	struct st_rip3_styles *styles = f.host.styles; struct st_rip3_palettes *palettes = f.host.palettes;
	f.host.styles = NULL; f.host.palettes = NULL; f.host.text = NULL;
	CHECK(process(&f,"m0101",-1) == 0 && process(&f,"TA",-1) == 0 && process(&f,"@0101A",-1) == 0);
	CHECK(cursor(&f).x == 5 && cursor(&f).y == 2);
	f.host.styles = styles; f.host.palettes = palettes; f.host.text = context; destroy(&f); return 0;
}

static int
color_modes(void)
{
	struct fixture f = {0}; struct st_rip3_style style; struct st_rip3_environment environment;
	CHECK(create(&f,ST_RIP3_CANVAS_640_480,640,480) == 0 && st_rip3_styles_get(f.host.styles,3,&style) == 0
	    && st_rip3_environments_get(f.host.environments,2,&environment) == 0);
	environment.rgb_bits = 8;
	CHECK(st_rip3_environments_protect(f.host.environments,2,false) == 0
	    && st_rip3_environments_set(f.host.environments,2,&environment) == 0);
	style.drawing_color = 0x0c0c0c; style.background_color = 0x080808; style.font.flags = 32;
	CHECK(set_style(&f,&style) == 0 && process(&f,"@0202A",-1) == 0);
	const char *const rows[] = {".F..","F.F.","FFFS",".SSS"};
	for (int32_t y = 0; y < 4; y++) for (int32_t x = 0; x < 4; x++) {
		uint8_t actual, expected = rows[y][x] == 'F' ? 12 : rows[y][x] == 'S' ? 8 : 85;
		CHECK(st_rip3_ports_read(f.host.ports,1,x+4,y+3,&actual) == 0 && actual == expected);
	}
	CHECK(st_rip3_ports_erase(f.host.ports,85) == 0);
	style.write_mode = ST_RIP3_NOT; CHECK(set_style(&f,&style) == 0);
	struct st_rip3_palettes *palettes = f.host.palettes; f.host.palettes = NULL;
	CHECK(process(&f,"@0202A",-1) == 0 && cursor(&f).x == 5);
	for (int32_t y = 0; y < 4; y++) for (int32_t x = 0; x < 4; x++) {
		uint8_t actual, expected = rows[y][x] == '.' ? 85 : 170;
		CHECK(st_rip3_ports_read(f.host.ports,1,x+4,y+3,&actual) == 0 && actual == expected);
	}
	style.write_mode = ST_RIP3_COPY; CHECK(set_style(&f,&style) == 0);
	CHECK(process(&f,"@0404A",ST_RIP3_COMMAND_UNSUPPORTED) == 0 && cursor(&f).x == 5 && cursor(&f).y == 2);
	/* Palette-index ink needs no RGB service; unused shadow background is not
	 * looked up after changing the ENV mode back from RGB to palette indexes. */
	environment.rgb_bits = 0; CHECK(st_rip3_environments_set(f.host.environments,2,&environment) == 0);
	style.drawing_color = 12; style.font.flags = 0; CHECK(set_style(&f,&style) == 0);
	CHECK(process(&f,"@0404A",-1) == 0 && cursor(&f).x == 7 && cursor(&f).y == 4);
	f.host.palettes = palettes; destroy(&f); return 0;
}

int
main(void)
{
	CHECK(scene(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	CHECK(modes(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(modes(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(modes(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	CHECK(vector_and_failures(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(vector_and_failures(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(vector_and_failures(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	CHECK(numeric() == 0 && errors() == 0 && color_modes() == 0);
	return 0;
}
