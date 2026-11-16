/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_bitmap_stamp.h"
#include "st_rip3_environments.h"
#include "st_rip3_number.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_rip3_resources.h"
#include "st_rip3_stream.h"
#include "st_pixels.h"
#include "filesystem.h"
#include "path.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern bool st_rip3_bitmap_stamp_test_fail_allocate;
extern bool st_rip3_bitmap_stamp_test_fail_duplicate;
extern bool st_rip3_bitmap_file_test_fail_allocate;
extern bool st_rip3_bitmap_file_test_fail_decode;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr,"check failed at line %d: %s\n",__LINE__,#expression); \
		return 1; \
	} \
} while (0)

struct fixture {
	struct st_rip3_command_host host;
	struct st_rip3_resource_paths paths;
	struct st_rip3_bitmap_error_sink feedback;
	struct st_rip3_bitmap_destination last_area;
	unsigned errors, prompts, last_port;
	enum st_rip3_command_diagnostic kind;
	bool area_present, overflow;
	char last_name[80];
};

static void
diagnostic(void *context, enum st_rip3_command_diagnostic kind)
{
	struct fixture *f = context;
	f->errors++; f->kind = kind;
}

static void
show_error(void *context, const char *name, unsigned port,
    const struct st_rip3_bitmap_destination *area)
{
	struct fixture *f = context;
	f->prompts++; f->last_port = port; f->area_present = area != NULL;
	if (area != NULL) f->last_area = *area;
	if (strlen(name) >= sizeof(f->last_name)) { f->overflow = true; return; }
	memcpy(f->last_name,name,strlen(name)+1);
}

static void
command(void *context, const uint8_t *bytes, size_t length)
{
	struct fixture *f = context;
	const struct st_rip3_command_sink sink = {f,diagnostic};
	st_rip3_commands_process(&f->host,&sink,bytes,length);
}

static int
process(struct fixture *f, const char *text, int error)
{
	unsigned previous = f->errors;
	command(f,(const uint8_t *)text,strlen(text));
	CHECK(f->errors == previous+(error < 0 ? 0u : 1u));
	if (error >= 0) CHECK((int)f->kind == error);
	CHECK(!f->overflow);
	return 0;
}

static void
form(char body[160], const char *coordinates, char duplicate, unsigned mode,
    const char *flags, const char *transparent, const char *name)
{
	(void)snprintf(body,160,"1b%s%c%u%s%s0000%s",coordinates,duplicate,mode,flags,transparent,name);
}

static void
put16(uint8_t *p, unsigned value)
{
	p[0] = (uint8_t)value; p[1] = (uint8_t)(value>>8);
}

static void
put32(uint8_t *p, uint32_t value)
{
	put16(p,value&65535); put16(p+2,value>>16);
}

static int
asset(const char *directory, const char *name, bool rgb, bool compressed)
{
	/* Independently authored 3x2 BMP: top 3/12/48, bottom 192/0/255. */
	uint8_t bytes[1102] = {0};
	const uint8_t codes[] = {3,12,48,192,0,255};
	size_t offset = rgb ? 54 : 1078, stride = rgb ? 12 : 4, size = offset+stride*2;
	bytes[0] = 'B'; bytes[1] = 'M';
	put32(bytes+2,(uint32_t)size); put32(bytes+10,(uint32_t)offset);
	put32(bytes+14,40); put32(bytes+18,3); put32(bytes+22,2);
	put16(bytes+26,1); put16(bytes+28,rgb ? 24 : 8); put32(bytes+30,compressed ? 1 : 0);
	if (!rgb) for (unsigned i = 0; i < 256; i++) bytes[54+4*i] = bytes[55+4*i] = bytes[56+4*i] = (uint8_t)i;
	for (unsigned y = 0; y < 2; y++) for (unsigned x = 0; x < 3; x++) {
		uint8_t code = codes[(1-y)*3+x];
		if (rgb) bytes[offset+y*stride+3*x] = bytes[offset+y*stride+3*x+1] = bytes[offset+y*stride+3*x+2] = code;
		else bytes[offset+y*stride+x] = code;
	}
	char *path = NULL;
	CHECK(path_join_alloc(&path,directory,name) == 0 && fs_replace_file(path,bytes,size) == 0);
	free(path); return 0;
}

static uint32_t
adopted_color(unsigned index, unsigned depth)
{
	/* Deliberately duplicate distinct palette indexes, including both 1-bit
	 * entries. RGB equality must not collapse their logical pixel identity. */
	if (depth == 1) return UINT32_C(0x4a9bc2);
	if (index == 2) index = 1;
	return ((index*37u&255u)<<16) | ((index*73u&255u)<<8) | (index*109u&255u);
}

static int
adoption_asset(const char *directory, unsigned depth)
{
	uint8_t bytes[1100] = {0};
	const uint8_t codes[] = {1,2,3,4,0,1}, mono[] = {1,0,1,0,1,0};
	unsigned count = depth == 1 ? 2 : depth == 4 ? 5 : 256;
	size_t offset = 54+count*4, size = offset+8;
	char name[20]; char *path = NULL;
	(void)snprintf(name,sizeof(name),"Adopt%u.BMP",depth);
	bytes[0] = 'B'; bytes[1] = 'M'; put32(bytes+2,(uint32_t)size); put32(bytes+10,(uint32_t)offset);
	put32(bytes+14,40); put32(bytes+18,3); put32(bytes+22,2);
	put16(bytes+26,1); put16(bytes+28,depth); put32(bytes+46,count);
	for (unsigned i = 0; i < count; i++) {
		uint32_t rgb = adopted_color(i,depth);
		bytes[54+i*4] = (uint8_t)rgb; bytes[55+i*4] = (uint8_t)(rgb>>8); bytes[56+i*4] = (uint8_t)(rgb>>16);
	}
	for (unsigned y = 0; y < 2; y++) for (unsigned x = 0; x < 3; x++) {
		uint8_t code = depth == 1 ? mono[(1-y)*3+x] : codes[(1-y)*3+x];
		if (depth == 1) bytes[offset+y*4] |= (uint8_t)(code<<(7-x));
		else if (depth == 4) bytes[offset+y*4+x/2] |= (uint8_t)(code<<(x%2 == 0 ? 4 : 0));
		else bytes[offset+y*4+x] = code;
	}
	CHECK(path_join_alloc(&path,directory,name) == 0 && fs_replace_file(path,bytes,size) == 0);
	free(path); return 0;
}

static int
create(struct fixture *f, enum st_rip3_canvas canvas, uint32_t width, uint32_t height, const char *directory)
{
	const struct st_rip3_rect bounds = {10,20,61,40}, viewport = {2,1,49,18};
	uint32_t colors[256];
	CHECK(st_rip3_environments_create(&f->host.environments,canvas) == 0);
	CHECK(st_rip3_ports_create(&f->host.ports,canvas) == 0 && st_rip3_palettes_create(&f->host.palettes) == 0);
	CHECK(st_rip3_palettes_switch(f->host.palettes,1) == 0);
	for (unsigned i = 0; i < 256; i++) colors[i] = i*UINT32_C(0x010101);
	CHECK(st_rip3_palettes_rgb(f->host.palettes,0,8,colors,256) == 0 && st_rip3_palettes_protect(f->host.palettes,1,true) == 0);
	CHECK(st_rip3_ports_define(f->host.ports,1,&bounds,false) == 0 && st_rip3_ports_switch(f->host.ports,1) == 0);
	CHECK(st_rip3_ports_viewport(f->host.ports,&viewport) == 0 && st_rip3_ports_erase(f->host.ports,85) == 0);
	const struct st_rip3_rect disabled = {0,0,0,0};
	CHECK(st_rip3_ports_image_style(f->host.ports,1,&disabled,15) == 0 && st_rip3_ports_protect(f->host.ports,1,true) == 0);
	struct st_rip3_environment environment;
	CHECK(st_rip3_environments_get(f->host.environments,0,&environment) == 0);
	environment.world_width = width; environment.world_height = height; environment.port = 9; environment.palette = 7;
	CHECK(st_rip3_environments_set(f->host.environments,0,&environment) == 0);
	f->paths.host = directory; f->host.resources = &f->paths;
	f->feedback = (struct st_rip3_bitmap_error_sink){f,show_error}; f->host.bitmap_errors = &f->feedback;
	return 0;
}

static void
destroy(struct fixture *f)
{
	st_rip3_environments_destroy(f->host.environments); st_rip3_ports_destroy(f->host.ports);
	st_rip3_palettes_destroy(f->host.palettes);
}

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, const char *directory)
{
	struct fixture f = {0};
	struct st_rip3_stream *stream = NULL;
	char body[160];
	const char *const positions[] = {"00000000","05000000","0A000000","0F000000","0K000000"};
	const uint8_t expected_modes[5][6] = {{3,12,48,192,0,255},{86,89,101,149,85,170},
	    {87,93,117,213,85,255},{1,4,16,64,0,85},{252,243,207,63,255,0}};
	const uint8_t stretched[] = {3,3,12,12,48,48,3,3,12,12,48,48,
	    192,192,0,0,255,255,192,192,0,0,255,255};
	CHECK(create(&f,canvas,width,height,directory) == 0);
	const struct st_rip3_stream_sink sink = {.context=&f,.command=command};
	CHECK(st_rip3_stream_create(&stream,&sink) == 0);
	const uint8_t opening[] = "!|"; st_rip3_stream_process(stream,opening,2);
	for (unsigned mode = 0; mode < 5; mode++) {
		form(body,positions[mode],'Z',mode,"00","ZZ","tILE");
		for (size_t i = 0; i < strlen(body); i++) st_rip3_stream_process(stream,(const uint8_t *)body+i,1);
		const uint8_t separator = '|'; st_rip3_stream_process(stream,&separator,1);
	}
	form(body,"00030506",'1',0,"01","00","Tile.BMP"); /* Same explicit duplicate is ignored. */
	CHECK(process(&f,body,-1) == 0);
	form(body,"0A030000",'0',0,"08","0C","Tile"); CHECK(process(&f,body,-1) == 0);
	form(body,"0F030000",'0',4,"0A","0C","Color.BMP"); CHECK(process(&f,body,-1) == 0);
	form(body,"1A000000",'0',0,"00","00","Tile"); CHECK(process(&f,body,-1) == 0); /* x=46, edge clips last source column. */
	form(body,"0009000C",'0',0,"00","00","Tile"); CHECK(process(&f,body,-1) == 0);
	CHECK(f.errors == 0 && f.prompts == 0);
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	CHECK(st_rip3_palettes_get(f.host.palettes,1,&palette) == 0 && st_rip3_ports_snapshot(f.host.ports,&palette,&capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		uint8_t expected = 0;
		if (x >= 12 && x <= 59 && y >= 21 && y <= 38) {
			uint32_t vx = x-12, vy = y-21; expected = 85;
			for (unsigned mode = 0; mode < 5; mode++)
				if (vx >= mode*5 && vx <= mode*5+2 && vy < 2) expected = expected_modes[mode][vy*3+vx-mode*5];
			if (vx < 6 && vy >= 3 && vy <= 6) expected = stretched[(vy-3)*6+vx];
			if (vx >= 10 && vx <= 12 && vy >= 3 && vy <= 4) {
				expected = expected_modes[0][(vy-3)*3+vx-10]; if (vx == 11 && vy == 3) expected = 85;
			}
			if (vx >= 15 && vx <= 17 && vy >= 3 && vy <= 4) {
				expected = expected_modes[4][(vy-3)*3+vx-15]; if (vx == 16 && vy == 3) expected = 85;
			}
			if (vx == 0 && vy >= 9 && vy <= 12) expected = vy <= 10 ? 12 : 0;
			if (vx >= 46 && vy < 2) expected = expected_modes[0][vy*3+vx-46];
		}
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | palette.rgb[expected]));
	}
	unsigned current;
	struct st_rip3_port_info info;
	struct st_rip3_environment environment;
	CHECK(st_rip3_ports_current(f.host.ports,&current) == 0 && current == 1);
	CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && info.protected && !info.image.enabled && info.image.flags == 15);
	CHECK(st_rip3_palettes_current(f.host.palettes,&current) == 0 && current == 1);
	CHECK(st_rip3_environments_get(f.host.environments,0,&environment) == 0 && environment.port == 9 && environment.palette == 7);
	st_pixel_buffer_destroy(capture); st_rip3_stream_destroy(stream); destroy(&f);
	return 0;
}

static int
adoption(enum st_rip3_canvas canvas, uint32_t width, uint32_t height,
    unsigned depth, const char *directory)
{
	struct fixture f = {0};
	struct st_rip3_palette before, actual;
	struct st_rip3_bitmap *capture = NULL;
	char name[20], body[160];
	const char *const positions[] = {"00000000","05000000","0A000000","0F000000","0K000000"};
	const uint8_t modes[5][6] = {{1,2,3,4,0,1},{84,87,86,81,85,84},
	    {85,87,87,85,85,85},{1,0,1,4,0,1},{254,253,252,251,255,254}};
	const uint8_t mono[5][6] = {{1,0,1,0,1,0},{84,85,84,85,84,85},
	    {85,85,85,85,85,85},{1,0,1,0,1,0},{254,255,254,255,254,255}};
	const uint8_t (*expected_modes)[6] = depth == 1 ? mono : modes;
	unsigned count = depth == 1 ? 2 : depth == 4 ? 5 : 256;
	CHECK(create(&f,canvas,width,height,directory) == 0);
	(void)snprintf(name,sizeof(name),"Adopt%u.BMP",depth);
	CHECK(st_rip3_palettes_get(f.host.palettes,1,&before) == 0);
	form(body,positions[0],'0',0,"02","00",name);
	CHECK(process(&f,body,ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && f.prompts == 0);
	CHECK(st_rip3_palettes_get(f.host.palettes,1,&actual) == 0 && memcmp(&before,&actual,sizeof(before)) == 0);
	CHECK(st_rip3_palettes_protect(f.host.palettes,1,false) == 0);
	st_rip3_bitmap_stamp_test_fail_allocate = true;
	CHECK(process(&f,body,ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_bitmap_stamp_test_fail_allocate);
	CHECK(st_rip3_palettes_get(f.host.palettes,1,&actual) == 0 && memcmp(&before,&actual,sizeof(before)) == 0);
	uint8_t unchanged;
	CHECK(st_rip3_ports_read(f.host.ports,1,2,1,&unchanged) == 0 && unchanged == 85);
	for (unsigned mode = 0; mode < 5; mode++) {
		form(body,positions[mode],'0',mode,"02","00",name); CHECK(process(&f,body,-1) == 0);
	}
	form(body,"00030000",'0',0,"0A","01",name); CHECK(process(&f,body,-1) == 0);
	form(body,"0A030000",'0',0,"06","00",name); CHECK(process(&f,body,-1) == 0);
	form(body,"0F030000",'0',4,"0E","01",name); CHECK(process(&f,body,-1) == 0);
	CHECK(f.prompts == 0);
	CHECK(st_rip3_palettes_get(f.host.palettes,1,&actual) == 0);
	for (unsigned i = 0; i < 256; i++) CHECK(actual.rgb[i] == (i < count ? adopted_color(i,depth) : before.rgb[i]));
	/* One owned capture holds both the index plane and its RGB palette,
	 * including background pixels recoloured by palette adoption. */
	CHECK(st_rip3_bitmap_capture(&capture,f.host.ports,0,NULL,&actual) == 0);
	CHECK(capture->pixels->width == width && capture->pixels->height == height);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		uint8_t expected = 0;
		if (x >= 12 && x <= 59 && y >= 21 && y <= 38) {
			uint32_t vx = x-12, vy = y-21; expected = 85;
			for (unsigned mode = 0; mode < 5; mode++)
				if (vx >= mode*5 && vx <= mode*5+2 && vy < 2) expected = expected_modes[mode][vy*3+vx-mode*5];
			if (vx < 3 && vy >= 3 && vy <= 4) {
				expected = expected_modes[0][(vy-3)*3+vx]; if (expected == 1) expected = 85;
			}
			if (vx >= 10 && vx <= 12 && vy >= 3 && vy <= 4) expected = expected_modes[0][(vy-3)*3+vx-10];
			if (vx >= 15 && vx <= 17 && vy >= 3 && vy <= 4) {
				uint32_t at = (vy-3)*3+vx-15;
				expected = expected_modes[0][at] == 1 ? 85 : expected_modes[4][at];
			}
		}
		CHECK(capture->pixels->primary[(size_t)y*width+x] == expected);
		CHECK(capture->palette[expected] == (expected < count ? adopted_color(expected,depth) : before.rgb[expected]));
	}
	unsigned number;
	struct st_rip3_port_info info;
	struct st_rip3_environment environment;
	CHECK(st_rip3_ports_current(f.host.ports,&number) == 0 && number == 1);
	CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && info.protected && !info.image.enabled && info.image.flags == 15);
	CHECK(st_rip3_palettes_current(f.host.palettes,&number) == 0 && number == 1);
	CHECK(st_rip3_environments_get(f.host.environments,0,&environment) == 0 && environment.palette == 7 && environment.port == 9);
	st_rip3_bitmap_destroy(capture); destroy(&f); return 0;
}

static int
dithering(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, const char *directory)
{
	struct fixture f = {0};
	uint32_t colors[256] = {0};
	struct st_rip3_palette palette;
	struct st_rip3_bitmap *capture = NULL;
	char body[160];
	/* Literal black/white quantization result for the authored 3x2 BMP
	 * stretched to 8x8. This is an implementation-policy oracle, not a
	 * historical RIPtel pixel capture. */
	const char *const expected[] = {"10000000","00000000","00000010","00000000",
	    "01000111","11100111","01000111","11100111"};
	CHECK(create(&f,canvas,width,height,directory) == 0);
	colors[1] = 0xffffff;
	CHECK(st_rip3_palettes_protect(f.host.palettes,1,false) == 0
	    && st_rip3_palettes_rgb(f.host.palettes,0,8,colors,256) == 0
	    && st_rip3_palettes_protect(f.host.palettes,1,true) == 0);
	form(body,"00000707",'0',0,"04","00","Tile"); CHECK(process(&f,body,-1) == 0);
	/* True-colour ignores adoption but still dithers; palette protection
	 * must not turn this into an attempted table edit. */
	form(body,"0A000H07",'0',0,"06","00","Color.BMP"); CHECK(process(&f,body,-1) == 0);
	form(body,"000A070H",'0',0,"0C","0C","Tile"); CHECK(process(&f,body,-1) == 0);
	form(body,"0A0A0H0H",'0',4,"0E","00","Color.BMP"); CHECK(process(&f,body,-1) == 0);
	CHECK(f.errors == 0 && f.prompts == 0 && st_rip3_palettes_get(f.host.palettes,1,&palette) == 0);
	CHECK(memcmp(palette.rgb,colors,sizeof(colors)) == 0);
	CHECK(st_rip3_bitmap_capture(&capture,f.host.ports,0,NULL,&palette) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		uint8_t value = 0;
		if (x >= 12 && x <= 59 && y >= 21 && y <= 38) {
			uint32_t vx = x-12, vy = y-21; value = 85;
			if (vx < 8 && vy < 8) value = (uint8_t)(expected[vy][vx]-'0');
			if (vx >= 10 && vx <= 17 && vy < 8) value = (uint8_t)(expected[vy][vx-10]-'0');
			if (vx < 8 && vy >= 10 && vy <= 17) {
				value = (uint8_t)(expected[vy-10][vx]-'0');
				if (vx >= 3 && vx <= 4 && vy <= 13) value = 85;
			}
			if (vx >= 10 && vx <= 17 && vy >= 10 && vy <= 17)
				value = expected[vy-10][vx-10] == '0' ? 85 : 254;
		}
		CHECK(capture->pixels->primary[(size_t)y*width+x] == value);
	}
	st_rip3_bitmap_destroy(capture); destroy(&f); return 0;
}

static int
duplicate_pixels(struct fixture *f, unsigned number, uint32_t width, uint32_t height, const uint8_t *expected)
{
	struct st_rip3_port_info info;
	struct st_rip3_palette palette;
	struct st_rip3_bitmap *capture = NULL;
	unsigned current;
	CHECK(st_rip3_ports_info(f->host.ports,number,&info) == 0 && info.defined && info.offscreen
	    && !info.protected && info.viewport_active && info.bounds.x0 == 0 && info.bounds.y0 == 0
	    && info.bounds.x1 == (int32_t)width-1 && info.bounds.y1 == (int32_t)height-1
	    && memcmp(&info.bounds,&info.viewport,sizeof(info.bounds)) == 0
	    && info.image.automatic && info.image.enabled && info.image.flags == 0);
	CHECK(st_rip3_palettes_get(f->host.palettes,1,&palette) == 0);
	CHECK(st_rip3_bitmap_capture(&capture,f->host.ports,number,NULL,&palette) == 0);
	CHECK(capture->pixels->width == width && capture->pixels->height == height);
	for (size_t i = 0; i < (size_t)width*height; i++) CHECK(capture->pixels->primary[i] == expected[i]);
	CHECK(st_rip3_ports_current(f->host.ports,&current) == 0 && current == 1);
	st_rip3_bitmap_destroy(capture); return 0;
}

static int
duplication(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, const char *directory)
{
	struct fixture f = {0}; char body[160];
	const uint8_t modes[5][6] = {{3,12,48,192,0,255},{86,89,101,149,85,170},
	    {87,93,117,213,85,255},{1,4,16,64,0,85},{252,243,207,63,255,0}};
	CHECK(create(&f,canvas,width,height,directory) == 0);
	const struct st_rip3_clipboard_selection clipboard = {.set=true,.port=9};
	f.host.clipboard = &clipboard;
	for (unsigned mode = 0; mode < 5; mode++) {
		CHECK(st_rip3_ports_erase(f.host.ports,85) == 0);
		if (mode == 1) {
			const struct st_rip3_rect small = {1,0,1,1}, disabled = {0,0,0,0};
			CHECK(st_rip3_ports_switch(f.host.ports,2) == 0 && st_rip3_ports_viewport(f.host.ports,&small) == 0
			    && st_rip3_ports_activate(f.host.ports,false) == 0
			    && st_rip3_ports_image_style(f.host.ports,2,&disabled,15) == 0
			    && st_rip3_ports_switch(f.host.ports,1) == 0);
		}
		if (mode == 2) {
			const struct st_rip3_rect overlap = {12,21,16,25};
			CHECK(st_rip3_ports_define(f.host.ports,2,&overlap,false) == 0
			    && st_rip3_ports_plot(f.host.ports,4,4,77,ST_RIP3_COPY) == 0);
		}
		form(body,"00000000",'2',mode,"01","00","Tile"); CHECK(process(&f,body,-1) == 0);
		CHECK(duplicate_pixels(&f,2,3,2,modes[mode]) == 0);
		for (int32_t y = 0; y < 2; y++) for (int32_t x = 0; x < 3; x++) {
			uint8_t actual;
			CHECK(st_rip3_ports_read(f.host.ports,1,x+2,y+1,&actual) == 0 && actual == modes[mode][y*3+x]);
		}
		if (mode == 2) {
			uint8_t actual;
			CHECK(st_rip3_ports_read(f.host.ports,1,6,5,&actual) == 0 && actual == 77);
		}
	}
	CHECK(st_rip3_ports_erase(f.host.ports,85) == 0);
	form(body,"1A000000",'3',4,"09","0C","Tile"); CHECK(process(&f,body,-1) == 0);
	const uint8_t clipped[] = {252,85,63,255};
	CHECK(duplicate_pixels(&f,3,2,2,clipped) == 0);
	form(body,"1A0H0000",'3',4,"09","0C","Tile"); CHECK(process(&f,body,-1) == 0);
	CHECK(duplicate_pixels(&f,3,2,1,clipped) == 0);
	form(body,"00030506",'3',0,"09","0C","Tile"); CHECK(process(&f,body,-1) == 0);
	const uint8_t stretched[] = {3,3,85,85,48,48,3,3,85,85,48,48,
	    192,192,0,0,255,255,192,192,0,0,255,255};
	CHECK(duplicate_pixels(&f,3,6,4,stretched) == 0);
	uint32_t colors[256] = {0}; colors[1] = 0xffffff;
	CHECK(st_rip3_palettes_protect(f.host.palettes,1,false) == 0 && st_rip3_palettes_rgb(f.host.palettes,0,8,colors,256) == 0);
	form(body,"00000707",'4',0,"05","00","Tile"); CHECK(process(&f,body,-1) == 0);
	const char *const rows[] = {"10000000","00000000","00000010","00000000",
	    "01000111","11100111","01000111","11100111"};
	uint8_t dithered[64];
	for (size_t y = 0; y < 8; y++) for (size_t x = 0; x < 8; x++) dithered[y*8+x] = (uint8_t)(rows[y][x]-'0');
	CHECK(duplicate_pixels(&f,4,8,8,dithered) == 0);
	form(body,"00000000",'5',0,"03","00","Adopt8.BMP"); CHECK(process(&f,body,-1) == 0);
	const uint8_t adopted[] = {1,2,3,4,0,1};
	CHECK(duplicate_pixels(&f,5,3,2,adopted) == 0 && f.errors == 0 && f.prompts == 0);
	CHECK(clipboard.set && clipboard.port == 9);
	destroy(&f); return 0;
}

static int
duplicate_failures(const char *directory)
{
	struct fixture f = {0}; char body[160];
	struct st_rip3_palette before, after;
	const struct st_rip3_rect bounds = {0,0,2,1};
	const uint8_t original[] = {42,42,42,42,42,42};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480,640,480,directory) == 0);
	CHECK(st_rip3_ports_define(f.host.ports,2,&bounds,true) == 0 && st_rip3_ports_switch(f.host.ports,2) == 0
	    && st_rip3_ports_erase(f.host.ports,42) == 0 && st_rip3_ports_switch(f.host.ports,1) == 0);
	CHECK(st_rip3_palettes_protect(f.host.palettes,1,false) == 0
	    && st_rip3_palettes_get(f.host.palettes,1,&before) == 0);
	form(body,"00000000",'2',0,"03","00","Adopt8.BMP");
	st_rip3_bitmap_file_test_fail_allocate = true;
	CHECK(process(&f,body,ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_bitmap_file_test_fail_allocate);
	st_rip3_bitmap_stamp_test_fail_allocate = true;
	CHECK(process(&f,body,ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_bitmap_stamp_test_fail_allocate);
	st_rip3_bitmap_stamp_test_fail_duplicate = true;
	CHECK(process(&f,body,ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_bitmap_stamp_test_fail_duplicate);
	CHECK(duplicate_pixels(&f,2,3,2,original) == 0);
	CHECK(st_rip3_palettes_get(f.host.palettes,1,&after) == 0 && memcmp(&before,&after,sizeof(before)) == 0);
	uint8_t value;
	CHECK(st_rip3_ports_read(f.host.ports,1,2,1,&value) == 0 && value == 85);
	CHECK(st_rip3_ports_protect(f.host.ports,2,true) == 0);
	st_rip3_bitmap_file_test_fail_allocate = true;
	CHECK(process(&f,body,ST_RIP3_COMMAND_UNSUPPORTED) == 0 && st_rip3_bitmap_file_test_fail_allocate);
	st_rip3_bitmap_file_test_fail_allocate = false;
	struct st_rip3_port_info info;
	unsigned current;
	CHECK(st_rip3_ports_current(f.host.ports,&current) == 0 && current == 1
	    && st_rip3_ports_info(f.host.ports,2,&info) == 0 && info.protected);
	CHECK(st_rip3_ports_protect(f.host.ports,2,false) == 0);
	form(body,"1E000000",'2',0,"03","00","Adopt8.BMP");
	CHECK(process(&f,body,ST_RIP3_COMMAND_UNSUPPORTED) == 0 && f.prompts == 0);
	CHECK(duplicate_pixels(&f,2,3,2,original) == 0 && st_rip3_palettes_get(f.host.palettes,1,&after) == 0
	    && memcmp(&before,&after,sizeof(before)) == 0);
	CHECK(st_rip3_ports_read(f.host.ports,1,2,1,&value) == 0 && value == 85);
	struct st_rip3_bitmap *unchanged = NULL;
	CHECK(st_rip3_bitmap_capture(&unchanged,f.host.ports,1,NULL,&before) == 0);
	for (size_t i = 0; i < (size_t)unchanged->pixels->width*unchanged->pixels->height; i++)
		CHECK(unchanged->pixels->primary[i] == 85);
	st_rip3_bitmap_destroy(unchanged);
	/* A later command succeeds after every rejected/resource-failed attempt. */
	form(body,"00000000",'2',0,"03","00","Adopt8.BMP"); CHECK(process(&f,body,-1) == 0);
	const uint8_t copied[] = {1,2,3,4,0,1}; CHECK(duplicate_pixels(&f,2,3,2,copied) == 0);
	/* The same current viewport can also be backed by an offscreen surface. */
	CHECK(st_rip3_ports_protect(f.host.ports,1,false) == 0 && st_rip3_ports_define(f.host.ports,1,&bounds,true) == 0
	    && st_rip3_ports_erase(f.host.ports,85) == 0 && st_rip3_ports_protect(f.host.ports,1,true) == 0);
	form(body,"00000000",'3',0,"01","00","Adopt8.BMP"); CHECK(process(&f,body,-1) == 0);
	/* Without adoption, equal RGB source indexes use the first nearest
	 * active-palette entry; duplication retains that final mapped result. */
	const uint8_t mapped[] = {1,1,3,4,0,1};
	CHECK(duplicate_pixels(&f,3,3,2,mapped) == 0);
	destroy(&f); return 0;
}

static int
numeric(const char *directory)
{
	for (unsigned radix = 36; radix <= 64; radix += 28) for (size_t width = 2; width <= 5; width++) {
		struct fixture f = {0};
		struct st_rip3_environment environment;
		char coordinates[21] = {0}, body[160];
		uint8_t actual;
		CHECK(create(&f,ST_RIP3_CANVAS_640_480,640,480,directory) == 0);
		CHECK(st_rip3_environments_get(f.host.environments,0,&environment) == 0);
		environment.base_math = radix; environment.coordinate_size = (unsigned)width;
		environment.world_width = 320; environment.world_height = 240;
		CHECK(st_rip3_environments_define(f.host.environments,2,&environment) == 0
		    && st_rip3_environments_select(f.host.environments,2) == 0
		    && st_rip3_environments_protect(f.host.environments,2,true) == 0);
		const uint64_t native[] = {2,3,0,0}, scaled[] = {10,3,12,4};
		const uint8_t expected[] = {3,3,12,48,48,192,192,0,255,255,192,192,0,255,255};
		for (size_t i = 0; i < 4; i++) CHECK(st_rip3_number_encode(coordinates+i*width,width,native[i],width,radix) == 0);
		/* Unused duplicate/transparency fields are ignored even above 35/255. */
		form(body,coordinates,radix == 64 ? '&' : 'Z',0,"00",radix == 64 ? "&&" : "ZZ","Tile");
		CHECK(process(&f,body,-1) == 0);
		for (int32_t y = 0; y < 3; y++) for (int32_t x = 0; x < 4; x++) {
			const uint8_t native_pixels[] = {3,12,48,192,0,255};
			CHECK(st_rip3_ports_read(f.host.ports,1,6+x,7+y,&actual) == 0);
			CHECK(actual == (x < 3 && y < 2 ? native_pixels[y*3+x] : 85));
		}
		for (size_t i = 0; i < 4; i++) CHECK(st_rip3_number_encode(coordinates+i*width,width,scaled[i],width,radix) == 0);
		form(body,coordinates,'0',0,"00","00","Tile"); CHECK(process(&f,body,-1) == 0);
		for (int32_t y = 0; y < 3; y++) for (int32_t x = 0; x < 5; x++) {
			CHECK(st_rip3_ports_read(f.host.ports,1,22+x,7+y,&actual) == 0 && actual == expected[y*5+x]);
		}
		if (radix == 64) {
			form(body,coordinates,'&',0,"01","00","Tile");
			CHECK(process(&f,body,ST_RIP3_COMMAND_INVALID) == 0);
		}
		struct st_rip3_environment_info info;
		CHECK(st_rip3_environments_info(f.host.environments,2,&info) == 0 && info.protected);
		destroy(&f);
	}
	struct fixture f = {0};
	struct st_rip3_environment environment;
	char body[160];
	CHECK(create(&f,ST_RIP3_CANVAS_640_480,640,480,directory) == 0);
	CHECK(st_rip3_environments_get(f.host.environments,0,&environment) == 0);
	environment.base_math = 64; environment.coordinate_size = 5;
	environment.world_width = 1; environment.world_height = 1;
	CHECK(st_rip3_environments_set(f.host.environments,0,&environment) == 0);
	form(body,"0000100001&&&&&&&&&&",'0',0,"00","00","Missing");
	CHECK(process(&f,body,ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && f.prompts == 1);
	CHECK(f.last_area.x0 == 640 && f.last_area.y0 == 480
	    && f.last_area.x1 == INT64_C(687194766720) && f.last_area.y1 == INT64_C(515396075040));
	form(body,"0000000000&&&&&&&&&&",'0',0,"00","00","Tile");
	CHECK(process(&f,body,-1) == 0);
	for (int32_t y = 1; y <= 18; y++) for (int32_t x = 2; x <= 49; x++) {
		uint8_t actual;
		CHECK(st_rip3_ports_read(f.host.ports,1,x,y,&actual) == 0 && actual == 3);
	}
	destroy(&f); return 0;
}

static int
errors(const char *directory)
{
	struct fixture f = {0}; char body[160];
	CHECK(create(&f,ST_RIP3_CANVAS_640_480,640,480,directory) == 0);
	form(body,"01020304",'0',0,"00","00","Missing.BMP");
	CHECK(process(&f,body,ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && f.prompts == 1 && f.area_present);
	CHECK(strcmp(f.last_name,"Missing.BMP") == 0 && f.last_port == 1 && f.last_area.x0 == 1
	    && f.last_area.y0 == 2 && f.last_area.x1 == 3 && f.last_area.y1 == 4);
	form(body,"01020000",'0',0,"00","00","Missing");
	CHECK(process(&f,body,ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && f.prompts == 2 && !f.area_present);
	form(body,"00000000",'0',0,"00","00","Compressed");
	CHECK(process(&f,body,ST_RIP3_COMMAND_UNSUPPORTED) == 0 && f.prompts == 3);
	const char *const unsupported[] = {"01","0G"};
	for (size_t i = 0; i < sizeof(unsupported)/sizeof(unsupported[0]); i++) {
		form(body,"00000000",'0',0,unsupported[i],"00","Tile");
		CHECK(process(&f,body,ST_RIP3_COMMAND_UNSUPPORTED) == 0 && f.prompts == 3);
	}
	form(body,"00000000",'0',0,"00","00","../Tile");
	CHECK(process(&f,body,ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && f.prompts == 3);
	const char *const invalid[] = {"1b","1b0000","1b000000000000000000"};
	for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) CHECK(process(&f,invalid[i],ST_RIP3_COMMAND_INVALID) == 0);
	form(body,"00000000",'0',5,"00","00","Tile"); CHECK(process(&f,body,ST_RIP3_COMMAND_INVALID) == 0);
	form(body,"00000000",'0',0,"00","00",""); CHECK(process(&f,body,ST_RIP3_COMMAND_INVALID) == 0);
	form(body,"00000000",'0',0,"00","00","Tile"); body[19] = '1'; CHECK(process(&f,body,ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	form(body,"02000101",'0',0,"00","00","Tile"); CHECK(process(&f,body,ST_RIP3_COMMAND_INVALID) == 0);
	form(body,"00000000",'0',0,"08","ZZ","Tile"); CHECK(process(&f,body,ST_RIP3_COMMAND_INVALID) == 0);
	form(body,"00000000",'0',0,"00","00","Tile");
	st_rip3_bitmap_file_test_fail_allocate = true;
	CHECK(process(&f,body,ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_bitmap_file_test_fail_allocate);
	st_rip3_bitmap_file_test_fail_decode = true;
	CHECK(process(&f,body,ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_bitmap_file_test_fail_decode);
	st_rip3_bitmap_stamp_test_fail_allocate = true;
	CHECK(process(&f,body,ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_bitmap_stamp_test_fail_allocate);
	CHECK(f.prompts == 3);
	uint8_t actual;
	CHECK(st_rip3_ports_read(f.host.ports,1,2,1,&actual) == 0 && actual == 85);
	/* The real framer must deliver a valid load after both syntax and file errors. */
	struct st_rip3_stream *stream = NULL;
	const struct st_rip3_stream_sink sink = {.context=&f,.command=command};
	CHECK(st_rip3_stream_create(&stream,&sink) == 0);
	const char recovery[] = "!|1b|1b000000000000000000Missing|1b000000000000000000Tile|";
	unsigned previous = f.errors;
	for (size_t i = 0; i < sizeof(recovery)-1; i++) st_rip3_stream_process(stream,(const uint8_t *)recovery+i,1);
	CHECK(f.errors == previous+2 && f.prompts == 4);
	CHECK(st_rip3_ports_read(f.host.ports,1,2,1,&actual) == 0 && actual == 3);
	st_rip3_stream_destroy(stream);
	const struct st_rip3_bitmap_error_sink *feedback = f.host.bitmap_errors;
	f.host.bitmap_errors = NULL;
	CHECK(process(&f,body,ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.bitmap_errors = feedback;
	CHECK(st_rip3_ports_protect(f.host.ports,1,false) == 0 && st_rip3_ports_activate(f.host.ports,false) == 0);
	struct st_rip3_palettes *palettes = f.host.palettes;
	f.host.resources = NULL; f.host.palettes = NULL; f.host.bitmap_errors = NULL;
	CHECK(process(&f,body,-1) == 0 && f.prompts == 4);
	form(body,"00000000",'0',0,"0F","00","Missing");
	CHECK(process(&f,body,-1) == 0 && f.prompts == 4);
	/* Destruction follows after ignored input, with no outstanding callback ownership. */
	f.host.palettes = palettes; destroy(&f);
	return 0;
}

int
main(int argc, char **argv)
{
	CHECK(argc == 2 && fs_create_directories(argv[1]) == 0);
	CHECK(asset(argv[1],"Tile.BMP",false,false) == 0 && asset(argv[1],"Color.BMP",true,false) == 0);
	CHECK(asset(argv[1],"Compressed.BMP",false,true) == 0);
	CHECK(scene(ST_RIP3_CANVAS_640_480,640,480,argv[1]) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600,800,600,argv[1]) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768,1024,768,argv[1]) == 0);
	for (unsigned depth = 1; depth <= 8; depth = depth == 1 ? 4 : depth*2) {
		CHECK(adoption_asset(argv[1],depth) == 0);
		CHECK(adoption(ST_RIP3_CANVAS_640_480,640,480,depth,argv[1]) == 0);
		CHECK(adoption(ST_RIP3_CANVAS_800_600,800,600,depth,argv[1]) == 0);
		CHECK(adoption(ST_RIP3_CANVAS_1024_768,1024,768,depth,argv[1]) == 0);
	}
	CHECK(dithering(ST_RIP3_CANVAS_640_480,640,480,argv[1]) == 0);
	CHECK(dithering(ST_RIP3_CANVAS_800_600,800,600,argv[1]) == 0);
	CHECK(dithering(ST_RIP3_CANVAS_1024_768,1024,768,argv[1]) == 0);
	CHECK(duplication(ST_RIP3_CANVAS_640_480,640,480,argv[1]) == 0);
	CHECK(duplication(ST_RIP3_CANVAS_800_600,800,600,argv[1]) == 0);
	CHECK(duplication(ST_RIP3_CANVAS_1024_768,1024,768,argv[1]) == 0);
	CHECK(errors(argv[1]) == 0 && numeric(argv[1]) == 0 && duplicate_failures(argv[1]) == 0);
	return 0;
}
