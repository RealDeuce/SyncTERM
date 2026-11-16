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
expect(const char *directory, const char *name)
{
	char *path = NULL;
	FILE *stream = NULL;
	uint8_t bytes[1100];
	const uint8_t raster[] = {128,3,200,0,0,17,255,0};
	CHECK(path_join_alloc(&path,directory,name) == 0 && fs_open(&stream,path,"rb") == 0);
	CHECK(fread(bytes,1,sizeof(bytes),stream) == 1086 && !ferror(stream));
	CHECK(fclose(stream) == 0); free(path);
	/* Full independent BMP header/palette/raster expectations; no decoder or
	 * encoder from the engine constructs the expected file. */
	CHECK(bytes[0] == 'B' && bytes[1] == 'M' && little32(bytes+2) == 1086);
	CHECK(little32(bytes+6) == 0 && little32(bytes+10) == 1078 && little32(bytes+14) == 40);
	CHECK(little32(bytes+18) == 3 && little32(bytes+22) == 2);
	CHECK(little16(bytes+26) == 1 && little16(bytes+28) == 8 && little32(bytes+30) == 0);
	CHECK(little32(bytes+34) == 0 && little32(bytes+38) == 3 && little32(bytes+42) == 2);
	CHECK(little32(bytes+46) == 0 && little32(bytes+50) == 0);
	for (unsigned i = 0; i < 256; i++) {
		CHECK(bytes[54+4*i] == (i^0x55) && bytes[55+4*i] == 255-i
		    && bytes[56+4*i] == i && bytes[57+4*i] == 0);
	}
	CHECK(memcmp(bytes+1078,raster,sizeof(raster)) == 0);
	return 0;
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
	return 0;
}

static void
destroy(struct fixture *f)
{
	st_rip3_ports_destroy(f->host.ports); st_rip3_palettes_destroy(f->host.palettes);
	st_rip3_environments_destroy(f->host.environments);
}

static int
scene(enum st_rip3_canvas canvas, const char *directory)
{
	struct fixture f = {0};
	struct st_rip3_stream *stream = NULL;
	const struct st_rip3_stream_sink sink = {.context=&f,.command=command};
	const uint8_t input[] = "!|1W0filename.icn|1W0Tile.BMP|1W1bad.bmp|1W0tile.bmp|J1S|1W0Space name.bmp|1W0\xce\xa9mega.BMP|1W0$OFF$\r";
	CHECK(create(&f,canvas,directory) == 0 && st_rip3_stream_create(&stream,&sink) == 0);
	for (size_t i = 0; i < sizeof(input)-1; i++) st_rip3_stream_process(stream,input+i,1);
	CHECK(f.errors == 1 && f.kind == ST_RIP3_COMMAND_UNSUPPORTED);
	CHECK(expect(directory,"filename.icn") == 0 && expect(directory,"Tile.BMP") == 0);
	CHECK(expect(directory,"Space name.bmp") == 0 && expect(directory,"\xce\xa9mega.BMP") == 0);
	CHECK(missing(directory,"bad.bmp") == 0 && expect(directory,"$OFF$") == 0);
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
	CHECK(process(&f,"1W0same.bmp",-1) == 0 && expect(directory,"same.bmp") == 0);
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
	return 0;
}

static int
recovery(const char *directory)
{
	struct fixture f = {0};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480,directory) == 0);
	CHECK(process(&f,"1W0Tile.BMP",-1) == 0 && expect(directory,"Tile.BMP") == 0);
	for (unsigned stage = 1; stage <= 2; stage++) {
		st_rip3_bitmap_capture_test_failure = stage;
		CHECK(process(&f,"1W0Tile.BMP",ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0);
		CHECK(st_rip3_bitmap_capture_test_failure == 0 && expect(directory,"Tile.BMP") == 0);
	}
	st_rip3_bitmap_encode_test_fail_allocate = true;
	CHECK(process(&f,"1W0Tile.BMP",ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0);
	CHECK(!st_rip3_bitmap_encode_test_fail_allocate && expect(directory,"Tile.BMP") == 0);
	st_rip3_resource_test_fail_replace = true;
	CHECK(process(&f,"1W0Tile.BMP",ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(!st_rip3_resource_test_fail_replace && expect(directory,"Tile.BMP") == 0);
	CHECK(process(&f,"1W0Tile.BMP",-1) == 0 && expect(directory,"Tile.BMP") == 0);
	const char *const invalid[] = {"1W","1W0","1W?bad.bmp"};
	for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++)
		CHECK(process(&f,invalid[i],ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&f,"1W0../escape.bmp",ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	f.clipboard.set = false;
	CHECK(process(&f,"1W0Cleared.bmp",-1) == 0 && missing(directory,"Cleared.bmp") == 0);
	f.clipboard.set = true; f.clipboard.port = 0;
	CHECK(process(&f,"1W0Invalid.bmp",ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	f.clipboard.port = 36;
	CHECK(process(&f,"1W0Invalid.bmp",ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	f.clipboard.port = 7;
	CHECK(process(&f,"1W0Dangling.bmp",ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	f.clipboard.port = 1;
	CHECK(st_rip3_ports_protect(f.host.ports,1,false) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,1) == 0 && st_rip3_ports_activate(f.host.ports,false) == 0);
	CHECK(process(&f,"1W0Inactive.bmp",ST_RIP3_COMMAND_UNSUPPORTED) == 0 && missing(directory,"Inactive.bmp") == 0);
	CHECK(st_rip3_ports_activate(f.host.ports,true) == 0);
	f.host.clipboard = NULL;
	CHECK(process(&f,"1W0Missing.bmp",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.clipboard = &f.clipboard;
	const struct st_rip3_resource_paths *paths = f.host.resources;
	f.host.resources = NULL;
	CHECK(process(&f,"1W0Missing.bmp",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.resources = paths;
	struct st_rip3_ports *ports = f.host.ports;
	f.host.ports = NULL;
	CHECK(process(&f,"1W0Missing.bmp",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.ports = ports;
	struct st_rip3_environments *environments = f.host.environments;
	f.host.environments = NULL;
	CHECK(process(&f,"1W0Missing.bmp",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.environments = environments;
	struct st_rip3_palettes *palettes = f.host.palettes;
	f.host.palettes = NULL;
	CHECK(process(&f,"1W0Missing.bmp",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.palettes = palettes;
	f.paths.host = NULL;
	CHECK(process(&f,"1W0Missing.bmp",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.paths.host = directory;
	CHECK(process(&f,"1W0Recovered.bmp",-1) == 0 && expect(directory,"Recovered.bmp") == 0);
	destroy(&f);
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
	destroy(&f);
	CHECK(result->pixels->primary[0] == 17 && result->pixels->primary[1] == 255 && result->palette[17] == original);
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
	return 0;
}
