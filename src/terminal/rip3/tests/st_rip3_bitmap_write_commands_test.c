/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_bitmap.h"
#include "st_pixels.h"
#include "st_rip3_environments.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_rip3_resources.h"
#include "st_rip3_stream.h"
#include "filesystem.h"
#include "path.h"
#include "io_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern bool st_rip3_resource_test_fail_replace;
extern unsigned st_rip3_bitmap_capture_test_failure;
extern bool st_rip3_bitmap_encode_test_fail_allocate;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr,"check failed at line %d: %s\n",__LINE__,#expression); \
		return 1; \
	} \
} while (0)

struct fixture {
	struct st_rip3_command_host host;
	struct st_rip3_clipboard_selection clipboard;
	struct st_rip3_resource_paths paths;
	struct st_rip3_temporary_resource *temporary_resources;
	unsigned errors;
	enum st_rip3_command_diagnostic kind;
};

static void
diagnostic(void *context, enum st_rip3_command_diagnostic kind)
{
	struct fixture *f = context;
	f->errors++; f->kind = kind;
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
	return 0;
}

static unsigned
little16(const uint8_t *bytes)
{
	return (unsigned)bytes[0]+256u*bytes[1];
}

static uint32_t
little32(const uint8_t *bytes)
{
	return little16(bytes)+UINT32_C(65536)*little16(bytes+2);
}


static int
expect_area(const char *directory, const char *name, unsigned width, unsigned height,
    const uint8_t *pixels)
{
	char *path = NULL;
	FILE *stream = NULL;
	uint8_t bytes[1078], row[128];
	unsigned stride = (width+3)/4*4;
	CHECK(stride <= sizeof(row));
	CHECK(path_join_alloc(&path,directory,name) == 0 && fs_open(&stream,path,"rb") == 0);
	CHECK(fread(bytes,1,sizeof(bytes),stream) == sizeof(bytes));
	/* Independent BMP header, palette, raster and padding expectations. */
	CHECK(bytes[0] == 'B' && bytes[1] == 'M' && little32(bytes+2) == 1078+stride*height);
	CHECK(little32(bytes+6) == 0 && little32(bytes+10) == 1078 && little32(bytes+14) == 40);
	CHECK(little32(bytes+18) == width && little32(bytes+22) == height);
	CHECK(little16(bytes+26) == 1 && little16(bytes+28) == 8 && little32(bytes+30) == 0);
	CHECK(little32(bytes+34) == 0 && little32(bytes+38) == width && little32(bytes+42) == height);
	CHECK(little32(bytes+46) == 0 && little32(bytes+50) == 0);
	for (unsigned i = 0; i < 256; i++) {
		CHECK(bytes[54+4*i] == (i^0x55) && bytes[55+4*i] == 255-i
		    && bytes[56+4*i] == i && bytes[57+4*i] == 0);
	}
	for (unsigned y = 0; y < height; y++) {
		CHECK(fread(row,1,stride,stream) == stride);
		CHECK(memcmp(row,pixels+(size_t)(height-1-y)*width,width) == 0);
		for (unsigned x = width; x < stride; x++) CHECK(row[x] == 0);
	}
	CHECK(fgetc(stream) == EOF && !ferror(stream));
	CHECK(fclose(stream) == 0); free(path);
	return 0;
}

static int
expect(const char *directory, const char *name)
{
	const uint8_t pixels[] = {0,17,255,128,3,200};
	return expect_area(directory,name,3,2,pixels);
}

static int
expect_backing(const char *directory, const char *name)
{
	const uint8_t pixels[] = {
		238,238,238,238,238,
		238,0,17,255,238,
		238,128,3,200,238,
		238,238,238,238,238,
	};
	return expect_area(directory, name, 5, 4, pixels);
}

static int
missing(const char *directory, const char *name)
{
	char *path = NULL;
	struct fs_info info;
	CHECK(path_join_alloc(&path,directory,name) == 0);
	CHECK(fs_get_info(&info,path) == IO_ERROR_NOT_FOUND); free(path);
	return 0;
}

static int
create(struct fixture *f, enum st_rip3_canvas canvas, const char *directory)
{
	const struct st_rip3_rect area = {0,0,4,3}, viewport = {1,1,3,2};
	const uint8_t raster[] = {0,17,255,128,3,200};
	uint32_t colors[256];
	CHECK(st_rip3_environments_create(&f->host.environments,canvas) == 0);
	CHECK(st_rip3_ports_create(&f->host.ports,canvas) == 0);
	CHECK(st_rip3_palettes_create(&f->host.palettes) == 0);
	CHECK(st_rip3_palettes_switch(f->host.palettes,1) == 0);
	for (unsigned i = 0; i < 256; i++) colors[i] = (i<<16)|((255-i)<<8)|(i^0x55);
	CHECK(st_rip3_palettes_rgb(f->host.palettes,0,8,colors,256) == 0);
	CHECK(st_rip3_palettes_protect(f->host.palettes,1,true) == 0);
	CHECK(st_rip3_ports_define(f->host.ports,1,&area,true) == 0);
	CHECK(st_rip3_ports_switch(f->host.ports,1) == 0 && st_rip3_ports_erase(f->host.ports,0xee) == 0);
	CHECK(st_rip3_ports_viewport(f->host.ports,&viewport) == 0);
	for (int32_t y = 0; y < 2; y++) for (int32_t x = 0; x < 3; x++)
		CHECK(st_rip3_ports_plot(f->host.ports,x,y,raster[3*y+x],ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_protect(f->host.ports,1,true) == 0);
	CHECK(st_rip3_ports_switch(f->host.ports,0) == 0);
	struct st_rip3_environment environment;
	CHECK(st_rip3_environments_get(f->host.environments,0,&environment) == 0);
	environment.palette = 7; environment.port = 9;
	CHECK(st_rip3_environments_set(f->host.environments,0,&environment) == 0);
	f->clipboard = (struct st_rip3_clipboard_selection){true,1};
	f->paths.host = directory; f->host.resources = &f->paths; f->host.clipboard = &f->clipboard;
	f->host.temporary_resources = &f->temporary_resources;
	return 0;
}

static void
destroy(struct fixture *f)
{
	st_rip3_resource_purge_temporary(&f->temporary_resources);
	st_rip3_ports_destroy(f->host.ports); st_rip3_palettes_destroy(f->host.palettes);
	st_rip3_environments_destroy(f->host.environments);
}

static int
scene(enum st_rip3_canvas canvas, const char *directory)
{
	struct fixture f = {0};
	struct st_rip3_stream *stream = NULL;
	const struct st_rip3_stream_sink sink = {.context=&f,.command=command};
	const uint8_t input[] = "!|1W2ClipName.icn|1W2Tile.BMP|1W3temporary.hIc|1W2tile.bmp|J1S|1W6Space name.bmp|1W2\xce\xa9mega.BMP|1W2$OFF$\r";
	CHECK(create(&f,canvas,directory) == 0 && st_rip3_stream_create(&stream,&sink) == 0);
	for (size_t i = 0; i < sizeof(input)-1; i++) st_rip3_stream_process(stream,input+i,1);
	CHECK(f.errors == 0);
	CHECK(missing(directory,"ClipName.icn") == 0 && expect_backing(directory,"ClipName.BMP") == 0);
	CHECK(expect_backing(directory,"Tile.BMP") == 0 && expect_backing(directory,"temporary.BMH") == 0);
	CHECK(expect_backing(directory,"Space name.bmp") == 0 && expect_backing(directory,"\xce\xa9mega.BMP") == 0);
	CHECK(expect_backing(directory,"$OFF$.BMP") == 0);
	unsigned number;
	struct st_rip3_port_info info;
	struct st_rip3_palette_info palette;
	struct st_rip3_environment environment;
	CHECK(st_rip3_ports_current(f.host.ports,&number) == 0 && number == 0);
	CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && info.protected
	    && info.viewport.x0 == 1 && info.viewport.y0 == 1 && info.viewport.x1 == 3 && info.viewport.y1 == 2);
	CHECK(st_rip3_palettes_current(f.host.palettes,&number) == 0 && number == 1);
	CHECK(st_rip3_palettes_info(f.host.palettes,1,&palette) == 0 && palette.protected);
	CHECK(st_rip3_environments_get(f.host.environments,0,&environment) == 0
	    && environment.port == 9 && environment.palette == 7 && environment.base_math == 64);
	CHECK(f.clipboard.set && f.clipboard.port == 1);
	/* A selected clipboard can also be current; export still writes it. */
	CHECK(st_rip3_ports_switch(f.host.ports,1) == 0);
	CHECK(process(&f,"1W2same.bmp",-1) == 0 && expect_backing(directory,"same.bmp") == 0);
	const struct st_rip3_rect screen = {10,20,14,23}, screen_view = {1,1,3,2};
	const uint8_t pixels[] = {0,17,255,128,3,200};
	CHECK(st_rip3_ports_define(f.host.ports,2,&screen,false) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,2) == 0 && st_rip3_ports_viewport(f.host.ports,&screen_view) == 0);
	for (int32_t y = 0; y < 2; y++) for (int32_t x = 0; x < 3; x++)
		CHECK(st_rip3_ports_plot(f.host.ports,x,y,pixels[3*y+x],ST_RIP3_COPY) == 0);
	f.clipboard.port = 2;
	CHECK(process(&f,"1W0Screen.bmp",-1) == 0 && expect(directory,"Screen.bmp") == 0);
	CHECK(st_rip3_ports_current(f.host.ports,&number) == 0 && number == 2);
	for (int32_t y = 0; y < 2; y++) for (int32_t x = 0; x < 3; x++) {
		uint8_t index;
		CHECK(st_rip3_ports_read(f.host.ports,2,x+1,y+1,&index) == 0 && index == pixels[3*y+x]);
	}
	st_rip3_stream_destroy(stream); destroy(&f);
	CHECK(missing(directory,"temporary.BMH") == 0 && expect_backing(directory,"Tile.BMP") == 0);
	return 0;
}

static int
recovery(const char *directory)
{
	struct fixture f = {0};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480,directory) == 0);
	CHECK(process(&f,"1W2Tile.BMP",-1) == 0 && expect_backing(directory,"Tile.BMP") == 0);
	for (unsigned stage = 1; stage <= 2; stage++) {
		st_rip3_bitmap_capture_test_failure = stage;
		CHECK(process(&f,"1W2Tile.BMP",ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0);
		CHECK(st_rip3_bitmap_capture_test_failure == 0 && expect_backing(directory,"Tile.BMP") == 0);
	}
	st_rip3_bitmap_encode_test_fail_allocate = true;
	CHECK(process(&f,"1W2Tile.BMP",ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0);
	CHECK(!st_rip3_bitmap_encode_test_fail_allocate && expect_backing(directory,"Tile.BMP") == 0);
	st_rip3_resource_test_fail_replace = true;
	CHECK(process(&f,"1W3Tile.BMP",ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(!st_rip3_resource_test_fail_replace && expect_backing(directory,"Tile.BMP") == 0);
	CHECK(f.temporary_resources == NULL);
	CHECK(process(&f,"1W2Tile.BMP",-1) == 0 && expect_backing(directory,"Tile.BMP") == 0);
	CHECK(process(&f,"1W1",-1) == 0 && f.temporary_resources == NULL);
	const char *const invalid[] = {"1W","1W?bad.bmp"};
	for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++)
		CHECK(process(&f,invalid[i],ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&f,"1W2../escape.bmp",ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	f.clipboard.set = false;
	CHECK(process(&f,"1W2Cleared.bmp",-1) == 0 && missing(directory,"Cleared.bmp") == 0);
	f.clipboard.set = true; f.clipboard.port = 0;
	CHECK(process(&f,"1W2Invalid.bmp",ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	f.clipboard.port = 36;
	CHECK(process(&f,"1W2Invalid.bmp",ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	f.clipboard.port = 7;
	CHECK(process(&f,"1W2Dangling.bmp",-1) == 0 && missing(directory,"Dangling.bmp") == 0);
	f.clipboard.port = 1;
	CHECK(st_rip3_ports_protect(f.host.ports,1,false) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,1) == 0 && st_rip3_ports_activate(f.host.ports,false) == 0);
	CHECK(process(&f,"1W2Inactive.bmp",-1) == 0 && expect_backing(directory,"Inactive.bmp") == 0);
	CHECK(st_rip3_ports_activate(f.host.ports,true) == 0);
	f.host.clipboard = NULL;
	CHECK(process(&f,"1W2Missing.bmp",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.clipboard = &f.clipboard;
	const struct st_rip3_resource_paths *paths = f.host.resources;
	f.host.resources = NULL;
	CHECK(process(&f,"1W2Missing.bmp",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.resources = paths;
	struct st_rip3_ports *ports = f.host.ports;
	f.host.ports = NULL;
	CHECK(process(&f,"1W2Missing.bmp",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.ports = ports;
	struct st_rip3_environments *environments = f.host.environments;
	f.host.environments = NULL;
	CHECK(process(&f,"1W2Missing.bmp",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.environments = environments;
	struct st_rip3_palettes *palettes = f.host.palettes;
	f.host.palettes = NULL;
	CHECK(process(&f,"1W2Missing.bmp",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.palettes = palettes;
	f.paths.host = NULL;
	CHECK(process(&f,"1W2Missing.bmp",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.paths.host = directory;
	CHECK(process(&f,"1W2Recovered.bmp",-1) == 0 && expect_backing(directory,"Recovered.bmp") == 0);
	const uint8_t scaled[] = {128,3,200};
	CHECK(process(&f,"1W0Scaled",-1) == 0 && expect_area(directory,"Scaled.BMP",3,1,scaled) == 0);
	destroy(&f);
	return 0;
}

static bool
same_rect(const struct st_rip3_rect *a, const struct st_rip3_rect *b)
{
	return a->x0 == b->x0 && a->y0 == b->y0 && a->x1 == b->x1 && a->y1 == b->y1;
}

static bool
same_port(const struct st_rip3_port_info *a, const struct st_rip3_port_info *b)
{
	return a->defined == b->defined && a->offscreen == b->offscreen
	    && a->protected == b->protected && a->viewport_active == b->viewport_active
	    && same_rect(&a->bounds,&b->bounds) && same_rect(&a->viewport,&b->viewport)
	    && a->image.automatic == b->image.automatic && a->image.enabled == b->image.enabled
	    && a->image.flags == b->image.flags && same_rect(&a->image.area,&b->image.area);
}

static bool
same_environment(const struct st_rip3_environment *a, const struct st_rip3_environment *b)
{
	return a->graphics_style == b->graphics_style && a->button_style == b->button_style
	    && a->port == b->port && a->text_window == b->text_window && a->palette == b->palette
	    && a->world_width == b->world_width && a->world_height == b->world_height
	    && a->base_math == b->base_math && a->coordinate_size == b->coordinate_size
	    && a->rgb_bits == b->rgb_bits && a->mouse_pointer == b->mouse_pointer;
}

/* Label the backing pixels independently of the requested export rectangle. */
static int
port_write(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, const char *directory)
{
	struct fixture f = {0};
	struct st_rip3_environment environment, original_environment;
	struct st_rip3_port_info before, after;
	const struct st_rip3_rect screen = {10,20,21,27}, bitmap = {0,0,9,5};
	const struct st_rip3_rect screen_view = {2,1,9,4}, bitmap_view = {1,1,8,4};
	const struct {
		unsigned x0, y0, x1, y1, width, height;
	} cases[] = {
		{0,0,0,0,8,4}, {1,1,0,0,7,3},
		{1,1,9,2,7,1}, {1,1,2,9,1,3},
		{2,1,3,2,1,1}, {0,0,8,4,8,4},
	};
	uint8_t backing[60], expected[60];
	char body[160];
	CHECK(create(&f,canvas,directory) == 0);
	f.host.clipboard = NULL;
	CHECK(st_rip3_environments_get(f.host.environments,0,&original_environment) == 0);
	CHECK(st_rip3_ports_protect(f.host.ports,1,false) == 0);
	CHECK(st_rip3_ports_define(f.host.ports,1,&bitmap,true) == 0);
	CHECK(st_rip3_ports_define(f.host.ports,2,&screen,false) == 0);
	for (unsigned number = 1; number <= 2; number++) {
		CHECK(st_rip3_ports_switch(f.host.ports,number) == 0);
		unsigned w = number == 1 ? 10 : 12, h = number == 1 ? 6 : 8;
		for (unsigned y = 0; y < h; y++) for (unsigned x = 0; x < w; x++) {
			uint8_t index = (uint8_t)(x+16*y);
			CHECK(st_rip3_ports_plot(f.host.ports,(int32_t)x,(int32_t)y,index,ST_RIP3_COPY) == 0);
			if (number == 1) backing[y*w+x] = index;
		}
		CHECK(st_rip3_ports_viewport(f.host.ports,number == 1 ? &bitmap_view : &screen_view) == 0);
	}
	/* Source kind, inactive/protected combinations, both radices, every field
	 * width and unequal WORLD sizes must leave the DEVICE export unchanged. */
	for (unsigned number = 1; number <= 2; number++) {
		for (unsigned flags = 0; flags < 4; flags++) {
			CHECK(st_rip3_ports_protect(f.host.ports,number,false) == 0);
			CHECK(st_rip3_ports_switch(f.host.ports,number) == 0);
			CHECK(st_rip3_ports_activate(f.host.ports,(flags&1) == 0) == 0);
			CHECK(st_rip3_ports_protect(f.host.ports,number,(flags&2) != 0) == 0);
			CHECK(st_rip3_ports_switch(f.host.ports,0) == 0);
			CHECK(st_rip3_ports_info(f.host.ports,number,&before) == 0);
			for (unsigned radix = 36; radix <= 64; radix += 28) {
				for (unsigned coordinate = 2; coordinate <= 5; coordinate++) {
					for (unsigned scale = 0; scale < 3; scale++) {
						environment = original_environment;
						environment.base_math = radix; environment.coordinate_size = coordinate;
						environment.world_width = scale == 2 ? 1 : width*(scale+1);
						environment.world_height = scale == 2 ? 1 : height*(scale+1);
						CHECK(st_rip3_environments_set(f.host.environments,0,&environment) == 0);
						for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
							unsigned w = cases[i].width, h = cases[i].height;
							(void)snprintf(body,sizeof(body),"2W%u%0*u%0*u%0*u%0*u0000Port.bmp",
							    number,(int)coordinate,cases[i].x0,(int)coordinate,cases[i].y0,
							    (int)coordinate,cases[i].x1,(int)coordinate,cases[i].y1);
							CHECK(process(&f,body,-1) == 0);
							if (number == 1) {
								CHECK(expect_area(directory,"Port.bmp",10,6,backing) == 0);
							}
							else {
								for (unsigned y = 0; y < h; y++) for (unsigned x = 0; x < w; x++)
									expected[y*w+x] = (uint8_t)(2+cases[i].x0+x+16*(1+cases[i].y0+y));
								CHECK(expect_area(directory,"Port.bmp",w,h,expected) == 0);
							}
						}
						CHECK(st_rip3_ports_info(f.host.ports,number,&after) == 0);
						CHECK(same_port(&before,&after));
						struct st_rip3_environment retained;
						CHECK(st_rip3_environments_get(f.host.environments,0,&retained) == 0);
						CHECK(same_environment(&environment,&retained));
						unsigned current;
						CHECK(st_rip3_ports_current(f.host.ports,&current) == 0 && current == 0);
					}
				}
			}
		}
	}
	for (unsigned number = 1; number <= 2; number++) {
		unsigned w = number == 1 ? 10 : 12, h = number == 1 ? 6 : 8;
		for (unsigned y = 0; y < h; y++) for (unsigned x = 0; x < w; x++) {
			uint8_t index;
			CHECK(st_rip3_ports_read(f.host.ports,number,(int32_t)x,(int32_t)y,&index) == 0);
			CHECK(index == x+16*y);
		}
	}
	struct st_rip3_palette palette;
	unsigned selected;
	CHECK(st_rip3_palettes_current(f.host.palettes,&selected) == 0 && selected == 1);
	CHECK(st_rip3_palettes_get(f.host.palettes,1,&palette) == 0);
	for (unsigned i = 0; i < 256; i++) CHECK(palette.rgb[i] == ((i<<16)|((255-i)<<8)|(i^0x55)));

	environment = original_environment;
	environment.world_width = width; environment.world_height = height;
	CHECK(st_rip3_environments_set(f.host.environments,0,&environment) == 0);
	/* Split every byte through the public stream/parser, including recovery
	 * after a semantic error. The following export must still run. */
	struct st_rip3_stream *stream = NULL;
	const struct st_rip3_stream_sink sink = {.context=&f,.command=command};
	const uint8_t input[] = "!|2W2" "01010902" "0000Stream.bmp|2W2" "01010102" "0000Stream.bmp|2W2" "01010209" "0000After.bmp\r";
	CHECK(st_rip3_stream_create(&stream,&sink) == 0);
	unsigned errors = f.errors;
	for (size_t i = 0; i < sizeof(input)-1; i++) st_rip3_stream_process(stream,input+i,1);
	CHECK(f.errors == errors+1 && f.kind == ST_RIP3_COMMAND_OPERATION_FAILED);
	const uint8_t row[] = {35,36,37,38,39,40,41}, column[] = {35,51,67};
	CHECK(expect_area(directory,"Stream.bmp",7,1,row) == 0);
	CHECK(expect_area(directory,"After.bmp",1,3,column) == 0);
	/* Existing file bytes survive undefined/empty sources and each allocation,
	 * encoding and replacement failure. Success immediately after still runs. */
	const char *const invalid[] = {
		"2W7" "00000000" "0000Stream.bmp",
		"2W2" "01010102" "0000Stream.bmp",
		"2W2" "01010201" "0000Stream.bmp",
		"2W1" "02000101" "0000Stream.bmp",
	};
	for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
		CHECK(process(&f,invalid[i],ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
		CHECK(expect_area(directory,"Stream.bmp",7,1,row) == 0);
	}
	for (unsigned number = 1; number <= 2; number++) {
		(void)snprintf(body,sizeof(body),"2W%u010109020000Stream.bmp",number);
		for (unsigned stage = 1; stage <= 2; stage++) {
			st_rip3_bitmap_capture_test_failure = stage;
			CHECK(process(&f,body,ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0);
			CHECK(st_rip3_bitmap_capture_test_failure == 0 && expect_area(directory,"Stream.bmp",7,1,row) == 0);
		}
		st_rip3_bitmap_encode_test_fail_allocate = true;
		CHECK(process(&f,body,ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0);
		CHECK(!st_rip3_bitmap_encode_test_fail_allocate && expect_area(directory,"Stream.bmp",7,1,row) == 0);
		st_rip3_resource_test_fail_replace = true;
		CHECK(process(&f,body,ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
		CHECK(!st_rip3_resource_test_fail_replace && expect_area(directory,"Stream.bmp",7,1,row) == 0);
	}
	CHECK(process(&f,"2W2" "01010902" "0000Stream.bmp",-1) == 0 && expect_area(directory,"Stream.bmp",7,1,row) == 0);
	const char *const short_commands[] = {"2W","2W1","2W1000","2W1000000000000"};
	for (size_t i = 0; i < sizeof(short_commands)/sizeof(short_commands[0]); i++)
		CHECK(process(&f,short_commands[i],ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&f,"2W1....000ZReserved.bmp",-1) == 0);
	CHECK(expect_area(directory,"Reserved.bmp",10,6,backing) == 0);
	CHECK(f.temporary_resources == NULL);
	st_rip3_resource_test_fail_replace = true;
	CHECK(process(&f,"2W1....01.Reserved.bmp",ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(!st_rip3_resource_test_fail_replace && f.temporary_resources == NULL);
	CHECK(expect_area(directory,"Reserved.bmp",10,6,backing) == 0);
	CHECK(process(&f,"2W1....03.Session.bmp",-1) == 0 && f.temporary_resources != NULL);
	CHECK(expect_area(directory,"Session.bmp",10,6,backing) == 0);
	st_rip3_resource_purge_temporary(&f.temporary_resources);
	CHECK(missing(directory,"Session.bmp") == 0);
	CHECK(expect_area(directory,"Reserved.bmp",10,6,backing) == 0);
	CHECK(process(&f,"2W1" "00000000" "0000../escape.bmp",ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	/* The backing bitmap is exported even with an entirely external crop;
	 * the screen branch cannot construct a positive capture there. */
	CHECK(process(&f,"2W1" "09090000" "0000External.bmp",-1) == 0);
	CHECK(expect_area(directory,"External.bmp",10,6,backing) == 0);
	CHECK(process(&f,"2W2" "09090000" "0000Stream.bmp",ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(expect_area(directory,"Stream.bmp",7,1,row) == 0);
	/* Port 35, the base64-only '#' digit, port zero, and the preserved literal
	 * example exercise named sources independently of the current port. */
	const struct st_rip3_rect small = {0,0,2,1};
	CHECK(st_rip3_ports_define(f.host.ports,35,&small,true) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,35) == 0 && st_rip3_ports_erase(f.host.ports,17) == 0);
	const uint8_t solid[] = {17,17,17,17,17,17};
	CHECK(process(&f,"J1S",-1) == 0);
	CHECK(process(&f,"2WZ" "0#000&01" "0000Ultra.bmp",-1) == 0 && expect_area(directory,"Ultra.bmp",3,2,solid) == 0);
	CHECK(process(&f,"2Wa" "00000000" "0000BadPort.bmp",ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&f,"n5",-1) == 0);
	CHECK(process(&f,"2W1" "&&&&&&&&&&0000000000" "0000Huge.bmp",-1) == 0);
	CHECK(expect_area(directory,"Huge.bmp",10,6,backing) == 0);
	CHECK(process(&f,"2W2" "&&&&&&&&&&0000000000" "0000Stream.bmp",ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(expect_area(directory,"Stream.bmp",7,1,row) == 0);
	CHECK(process(&f,"n2",-1) == 0);

	CHECK(process(&f,"J10",-1) == 0);
	CHECK(process(&f,"2Wz" "00000000" "0000Mega.bmp",-1) == 0 && expect_area(directory,"Mega.bmp",3,2,solid) == 0);
	CHECK(process(&f,"2WZ" "0#000&01" "0000BadBase.bmp",ST_RIP3_COMMAND_INVALID) == 0);
	const struct st_rip3_rect large = {0,0,72,72};
	uint8_t canonical[73*73];
	CHECK(st_rip3_ports_define(f.host.ports,5,&large,true) == 0 && st_rip3_ports_switch(f.host.ports,5) == 0);
	for (int32_t y = 0; y < 73; y++) for (int32_t x = 0; x < 73; x++) {
		uint8_t index = (uint8_t)((unsigned)x+3*(unsigned)y);
		canonical[y*73+x] = index;
		CHECK(st_rip3_ports_plot(f.host.ports,x,y,index,ST_RIP3_COPY) == 0);
	}
	CHECK(process(&f,"2W5000020200000FILENAME.BMP",-1) == 0);
	CHECK(expect_area(directory,"FILENAME.BMP",73,73,canonical) == 0);
	const struct st_rip3_rect zero_view = {5,7,7,8};
	CHECK(st_rip3_ports_switch(f.host.ports,0) == 0 && st_rip3_ports_viewport(f.host.ports,&zero_view) == 0);
	for (int32_t y = 0; y < 2; y++) for (int32_t x = 0; x < 3; x++)
		CHECK(st_rip3_ports_plot(f.host.ports,x,y,solid[3*y+x],ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,5) == 0);
	CHECK(process(&f,"2W0" "00000000" "0000PortZero.bmp",-1) == 0 && expect_area(directory,"PortZero.bmp",3,2,solid) == 0);
	st_rip3_stream_destroy(stream); destroy(&f);
	return 0;
}

static int
capture(const char *directory)
{
	struct fixture f = {0};
	struct st_rip3_palette palette;
	struct st_rip3_bitmap marker = {0}, *result = &marker;
	const struct st_rip3_rect reversed = {2,0,1,0}, external = {INT32_MAX,0,INT32_MAX,0};
	const struct st_rip3_rect crop = {1,0,INT32_MAX,0};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480,directory) == 0);
	CHECK(st_rip3_palettes_get(f.host.palettes,1,&palette) == 0);
	CHECK(st_rip3_bitmap_capture(&result,f.host.ports,1,&reversed,&palette) == IO_ERROR_INVALID_ARGUMENT && result == &marker);
	CHECK(st_rip3_bitmap_capture(&result,f.host.ports,1,&external,&palette) == IO_ERROR_NOT_FOUND && result == &marker);
	CHECK(st_rip3_bitmap_capture(&result,f.host.ports,7,NULL,&palette) == IO_ERROR_NOT_FOUND && result == &marker);
	uint32_t original = palette.rgb[17]; palette.rgb[17] = 0x1000000;
	CHECK(st_rip3_bitmap_capture(&result,f.host.ports,1,NULL,&palette) == IO_ERROR_INVALID_ARGUMENT && result == &marker);
	palette.rgb[17] = original;
	CHECK(st_rip3_bitmap_capture(&result,f.host.ports,1,&crop,&palette) == 0 && result != &marker);
	CHECK(result->bit_depth == 8 && result->palette_size == 256 && result->pixels->width == 2 && result->pixels->height == 1);
	CHECK(result->pixels->primary[0] == 17 && result->pixels->primary[1] == 255 && result->palette[17] == original);
	palette.rgb[17] = 0;
	CHECK(st_rip3_ports_switch(f.host.ports,1) == 0 && st_rip3_ports_erase(f.host.ports,99) == 0);
	struct st_rip3_bitmap *raw = &marker;
	CHECK(st_rip3_ports_protect(f.host.ports,1,false) == 0);
	CHECK(st_rip3_ports_activate(f.host.ports,false) == 0);
	CHECK(st_rip3_ports_protect(f.host.ports,1,true) == 0);
	CHECK(st_rip3_bitmap_capture_port(&raw,f.host.ports,1,&reversed,&palette) == IO_ERROR_INVALID_ARGUMENT && raw == &marker);
	CHECK(st_rip3_bitmap_capture_port(&raw,f.host.ports,1,&external,&palette) == IO_ERROR_NOT_FOUND && raw == &marker);
	CHECK(st_rip3_bitmap_capture_port(&raw,f.host.ports,7,NULL,&palette) == IO_ERROR_NOT_FOUND && raw == &marker);
	CHECK(st_rip3_bitmap_capture_port(&raw,f.host.ports,1,NULL,&palette) == 0 && raw != &marker);
	CHECK(raw->pixels->width == 5 && raw->pixels->height == 4 && raw->palette[17] == 0);
	for (unsigned y = 0; y < 4; y++) for (unsigned x = 0; x < 5; x++)
		CHECK(raw->pixels->primary[5*y+x] == (y >= 1 && y <= 2 && x >= 1 && x <= 3 ? 99u : 0xeeu));

	destroy(&f);
	CHECK(result->pixels->primary[0] == 17 && result->pixels->primary[1] == 255 && result->palette[17] == original);
	CHECK(raw->pixels->primary[6] == 99 && raw->pixels->primary[0] == 0xee);
	st_rip3_bitmap_destroy(raw);
	st_rip3_bitmap_destroy(result);
	return 0;
}

int
main(int argc, char **argv)
{
	CHECK(argc == 2);
	CHECK(scene(ST_RIP3_CANVAS_640_480,argv[1]) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600,argv[1]) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768,argv[1]) == 0);
	CHECK(recovery(argv[1]) == 0 && capture(argv[1]) == 0);
	CHECK(port_write(ST_RIP3_CANVAS_640_480,640,480,argv[1]) == 0);
	CHECK(port_write(ST_RIP3_CANVAS_800_600,800,600,argv[1]) == 0);
	CHECK(port_write(ST_RIP3_CANVAS_1024_768,1024,768,argv[1]) == 0);
	return 0;
}
