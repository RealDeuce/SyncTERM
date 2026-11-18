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
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr,"check failed at line %d: %s\n",__LINE__,#expression); \
		return 1; \
	} \
} while (0)

struct fixture { struct st_rip3_command_host host; unsigned errors; };

static void
diagnostic(void *context, enum st_rip3_command_diagnostic kind)
{
	struct fixture *f = context;
	(void)kind; f->errors++;
}

static void
command(void *context, const uint8_t *bytes, size_t length)
{
	struct fixture *f = context;
	const struct st_rip3_command_sink sink = {f,diagnostic};
	st_rip3_commands_process(&f->host,&sink,bytes,length);
}

static int
process(struct fixture *f, const char *bytes)
{
	unsigned before = f->errors;
	command(f,(const uint8_t *)bytes,strlen(bytes));
	CHECK(f->errors == before);
	return 0;
}

static int
create(struct fixture *f, enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_style initial = {.drawing_color=12,.background_color=1,.fill_color=7,
	    .pen={.pattern=0xffff,.thickness=1},.font={0,0,1,0},.write_mode=ST_RIP3_COPY};
	struct st_rip3_environment env;
	uint32_t colors[256];
	*f = (struct fixture){0};
	CHECK(st_rip3_fill_solid(&initial.fill,true) == 0);
	/* Caller-owned device-kernel phase must not become wire phase or be
	 * rewritten by drawing through different contexts. */
	initial.fill.origin = (struct st_rip3_point){INT32_MIN,INT32_MAX};
	CHECK(st_rip3_styles_create(&f->host.styles,&initial) == 0);
	CHECK(st_rip3_ports_create(&f->host.ports,canvas) == 0);
	CHECK(st_rip3_palettes_create(&f->host.palettes) == 0);
	CHECK(st_rip3_environments_create(&f->host.environments,canvas) == 0);
	CHECK(st_rip3_environments_get(f->host.environments,0,&env) == 0);
	env.world_width = width; env.world_height = height;
	CHECK(st_rip3_environments_set(f->host.environments,0,&env) == 0);
	for (unsigned i = 0; i < 256; i++) colors[i] = i*UINT32_C(0x010101);
	CHECK(st_rip3_palettes_rgb(f->host.palettes,0,8,colors,256) == 0);
	return 0;
}

static void
destroy(struct fixture *f)
{
	st_rip3_styles_destroy(f->host.styles); st_rip3_ports_destroy(f->host.ports);
	st_rip3_palettes_destroy(f->host.palettes); st_rip3_environments_destroy(f->host.environments);
}

static void
number(char *bytes, unsigned value, unsigned width, unsigned radix)
{
	static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#&";
	while (width != 0) { bytes[--width] = digits[value%radix]; value /= radix; }
}

static int
rectangle(struct fixture *f, char kind, unsigned x0, unsigned y0,
    unsigned x1, unsigned y1, unsigned scale, unsigned width, unsigned radix)
{
	const unsigned values[] = {x0,y0,x1,y1};
	char bytes[22]; bytes[0] = kind;
	for (unsigned i = 0; i < 4; i++) number(bytes+1+i*width,values[i]*scale,width,radix);
	bytes[1+4*width] = 0;
	return process(f,bytes);
}

static int
matrix(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, bool offscreen)
{
	/* Literal pictures transcribed from the canonical reference diagrams,
	 * independently of the byte table consumed by S. */
	static const char *const pictures[] = {
		"........" "........" "........" "........" "........" "........" "........" "........",
		"########" "########" "########" "########" "########" "########" "########" "########",
		"########" "########" "........" "........" "########" "########" "........" "........",
		".......#" "......#." ".....#.." "....#..." "...#...." "..#....." ".#......" "#.......",
		"###....." "##.....#" "#.....##" ".....###" "....###." "...###.." "..###..." ".###....",
		"####...." ".####..." "..####.." "...####." "....####" "#....###" "##....##" "###....#",
		"#.#..#.#" "##.#..#." ".##.#..#" "#.##.#.." ".#.##.#." "..#.##.#" "#..#.##." ".#..#.##",
		"########" "#...#..." "#...#..." "#...#..." "########" "#...#..." "#...#..." "#...#...",
		"#......#" ".#....#." "..#..#.." "...##..." "...##..." "..#..#.." ".#....#." "#......#",
		"##..##.." "..##..##" "##..##.." "..##..##" "##..##.." "..##..##" "##..##.." "..##..##",
		"#......." "........" "....#..." "........" "#......." "........" "....#..." "........",
		"#...#..." "........" "..#...#." "........" "#...#..." "........" "..#...#." "........",
	};
	static const unsigned foreground[] = {7,0x52,0x57,5,0xaa,255};
	static const unsigned background[] = {1,0x54,0x55,1,0xaa,1};
	struct fixture f;
	struct st_rip3_environment env;
	struct st_rip3_style style;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	const struct st_rip3_rect screen = {25,25,280,152}, bitmap = {0,0,255,127};
	const struct st_rip3_rect copy = {257,101,496,212};
	CHECK(create(&f,canvas,width,height) == 0);
	CHECK(st_rip3_ports_define(f.host.ports,1,offscreen ? &bitmap : &screen,offscreen) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,1) == 0);
	CHECK(st_rip3_environments_get(f.host.environments,0,&env) == 0);
	for (unsigned scale = 1; scale <= 2; scale++) for (unsigned radix = 36; radix <= 64; radix += 28) for (unsigned xy = 2; xy <= 5; xy++) {
		env.world_width = width*scale; env.world_height = height*scale;
		env.base_math = radix; env.coordinate_size = xy; env.rgb_bits = 0;
		CHECK(st_rip3_environments_set(f.host.environments,0,&env) == 0);
		CHECK(process(&f,"k1") == 0);
		CHECK(st_rip3_ports_protect(f.host.ports,1,false) == 0);
		CHECK(rectangle(&f,'v',5,3,244,114,scale,xy,radix) == 0);
		CHECK(st_rip3_ports_protect(f.host.ports,1,true) == 0);
		CHECK(st_rip3_ports_erase(f.host.ports,0) == 0);
		for (unsigned mode = 0; mode < 6; mode++) {
			if (mode == 5) {
				env.rgb_bits = 8;
				CHECK(st_rip3_environments_set(f.host.environments,0,&env) == 0);
				CHECK(process(&f,"k0G41") == 0); /* UltraNum 0x010101 -> index 1. */
			}
			char setting[] = "W0"; setting[1] += mode == 5 ? 0 : (char)mode;
			CHECK(process(&f,setting) == 0);
			for (unsigned p = 0; p < 12; p++) {
				char stock[8] = "S007"; number(stock+1,p,2,radix);
				if (mode == 5) memcpy(stock+3,"&&&&",5); /* RGB white -> index 255. */
				const struct st_rip3_rect cell = {(int32_t)p*16,(int32_t)mode*16,(int32_t)p*16+7,(int32_t)mode*16+7};
				CHECK(st_rip3_ports_fill(f.host.ports,&cell,0x55,ST_RIP3_COPY) == 0);
				CHECK(process(&f,stock) == 0);
				CHECK(rectangle(&f,'B',p*16,mode*16,p*16+7,mode*16+7,scale,xy,radix) == 0);
			}
		}
		if (offscreen) CHECK(st_rip3_ports_copy(f.host.ports,1,NULL,0,&copy,ST_RIP3_TRANSFER_COPY) == 0);
		CHECK(st_rip3_palettes_get(f.host.palettes,0,&palette) == 0);
		CHECK(st_rip3_ports_snapshot(f.host.ports,&palette,&capture) == 0);
		unsigned left = offscreen ? 257 : 5, top = offscreen ? 101 : 3;
		for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
			unsigned expected = 0;
			if (x >= left && x < left+240 && y >= top && y < top+112) {
				unsigned u = x-left, v = y-top, p = u/16, mode = v/16;
				if (p < 12 && mode < 6 && u%16 < 8 && v%16 < 8) {
					bool on = pictures[p][((3+v)%8)*8+(5+u)%8] == '#';
					expected = (on ? foreground : background)[mode];
				}
			}
			CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | expected*UINT32_C(0x010101)));
		}
		st_pixel_buffer_destroy(capture); capture = NULL;
		CHECK(st_rip3_styles_get(f.host.styles,0,&style) == 0
		    && style.fill.origin.x == INT32_MIN && style.fill.origin.y == INT32_MAX
		    && style.drawing_color == 12 && style.background_color == 0x010101 && style.fill_color == 0xffffff);
	}
	destroy(&f); return 0;
}

static bool
ellipse_pixel(unsigned x, unsigned y, int cx, int cy, int rx, int ry)
{
	int64_t dx = 2*(int64_t)x+1-2*cx, dy = 2*(int64_t)y+1-2*cy;
	return dx*dx*ry*ry+dy*dy*rx*rx < 4*(int64_t)rx*rx*ry*ry;
}

static int
consumers(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct fixture f;
	struct st_rip3_stream *stream = NULL;
	const struct st_rip3_stream_sink sink = {.context=&f,.command=command};
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	const struct st_rip3_rect screen = {2,1,257,128};
	CHECK(create(&f,canvas,width,height) == 0);
	CHECK(st_rip3_ports_define(f.host.ports,1,&screen,false) == 0 && st_rip3_ports_switch(f.host.ports,1) == 0);
	CHECK(st_rip3_stream_create(&stream,&sink) == 0);
	/* Exact reference phase example plus rectangle, polygon and ellipse
	 * consumers. Only mask[0]'s leftmost bit is on. Adjacent bars share phase. */
	const char input[] = "!|v05036S36|s3K0000000000000007|B00000808|B08000G08|K000G0G0O|p040W000G000G080W08|G1V0L06|o2R0L0705\r";
	for (size_t i = 0; i < sizeof(input)-1; i++) st_rip3_stream_process(stream,(const uint8_t *)input+i,1);
	CHECK(f.errors == 0);
	/* Change the viewport without setting another mask. Phase still follows
	 * context coordinates, not the viewport active when s was executed. */
	CHECK(process(&f,"v06046S36") == 0);
	CHECK(process(&f,"B3K103S18") == 0); /* Context [134,142]x[40,48]. */
	CHECK(st_rip3_palettes_get(f.host.palettes,0,&palette) == 0);
	CHECK(st_rip3_ports_snapshot(f.host.ports,&palette,&capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		bool covered = (x >= 5 && x <= 21 && y >= 3 && y <= 11)
		    || (x >= 5 && x < 21 && y >= 19 && y < 27)
		    || (x >= 21 && x < 37 && y >= 3 && y < 11)
		    || ellipse_pixel(x,y,72,24,6,6) || ellipse_pixel(x,y,104,24,7,5)
		    || (x >= 134 && x <= 142 && y >= 40 && y <= 48);
		unsigned expected = covered ? (x%8 == 0 && y%8 == 0 ? 7 : 1) : 0;
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | expected*UINT32_C(0x010101)));
	}
	st_pixel_buffer_destroy(capture); st_rip3_stream_destroy(stream); destroy(&f);
	return 0;
}

int
main(void)
{
	CHECK(consumers(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(consumers(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(consumers(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_640_480,640,480,false) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_800_600,800,600,false) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_1024_768,1024,768,false) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_640_480,640,480,true) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_800_600,800,600,true) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_1024_768,1024,768,true) == 0);
	return 0;
}
