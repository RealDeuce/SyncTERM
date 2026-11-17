/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "image_area_fixture.h"
#include "st_rip3_commands.h"
#include "st_rip3_jpeg_file.h"
#include "st_rip3_bitmap.h"
#include "st_rip3_environments.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_rip3_resources.h"
#include "st_rip3_styles.h"
#include "st_rip3_stream.h"
#include "st_pixel_image.h"
#include "st_pixels.h"
#include "filesystem.h"
#include "path.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern bool st_rip3_jpeg_file_test_fail_allocate, st_rip3_jpeg_file_test_fail_decode;
extern bool st_rip3_jpeg_file_test_fail_size, st_rip3_jpeg_file_test_fail_close;
extern bool st_rip3_jpeg_file_test_unavailable;
extern unsigned st_rip3_jpeg_file_test_read_fault;
extern bool st_rip3_image_commands_test_fail_allocate, st_rip3_image_test_fail_allocate;
extern unsigned st_rip3_image_test_fills;
extern struct st_rip3_rect st_rip3_image_test_fill_area;
extern uint8_t st_rip3_image_test_fill_color;
extern enum st_rip3_write_mode st_rip3_image_test_fill_mode;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr,"check failed at line %d: %s\n",__LINE__,#expression); \
		return 1; \
	} \
} while (0)

struct fixture {
	struct st_rip3_command_host host;
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

static int
put(const char *directory, const char *name, const void *bytes, size_t size)
{
	char *path = NULL;
	int status = path_join_alloc(&path,directory,name);
	if (status == 0) status = fs_replace_file(path,bytes,size);
	free(path); return status;
}

static int
asset(const char *source, const char *directory, const char *name, const char *filename, size_t trim)
{
	char *path = NULL; FILE *stream = NULL; uint8_t bytes[1024];
	CHECK(path_join_alloc(&path,source,filename) == 0 && fs_open(&stream,path,"rb") == 0);
	size_t size = fread(bytes,1,sizeof(bytes),stream);
	CHECK(!ferror(stream) && feof(stream) && fclose(stream) == 0 && size > trim);
	free(path); return put(directory,name,bytes,size-trim);
}

static int
load(const struct st_rip3_resource_paths *paths, const char *name, int error, bool gray)
{
	struct st_pixel_buffer marker = {0}, *image = &marker;
	CHECK(st_rip3_jpeg_load(&image,paths,name) == error);
	if (error != 0) { CHECK(image == &marker); return 0; }
	CHECK(image != &marker && image->width == 17 && image->height == 9 && image->alternate == NULL);
	/* The JPEGs contain independently authored DC-only blocks. Expectations
	 * come from generate.py's coefficients, never the decoder under test. */
	for (size_t y = 0; y < 9; y++) for (size_t x = 0; x < 17; x++) {
		unsigned value = y < 8 ? 64 : 192;
		uint32_t expected = gray ? ST_PIXEL_COLOR_RGB_VALUE(value,value,value) : ST_PIXEL_COLOR_RGB_VALUE(170,45,23);
		CHECK(image->primary[y*17+x] == expected);
	}
	st_pixel_buffer_destroy(image); return 0;
}

static int
files(const struct st_rip3_resource_paths *paths)
{
	CHECK(st_rip3_jpeg_load(NULL,paths,"Gray.JPG") == IO_ERROR_INVALID_ARGUMENT);
	CHECK(load(NULL,"Gray.JPG",IO_ERROR_INVALID_ARGUMENT,true) == 0);
	CHECK(load(paths,NULL,IO_ERROR_INVALID_ARGUMENT,true) == 0);
	CHECK(load(paths,"",IO_ERROR_INVALID_ARGUMENT,true) == 0);
	st_rip3_jpeg_file_test_unavailable = true;
	CHECK(load(paths,"Gray.JPG",IO_ERROR_NOT_SUPPORTED,true) == 0);
	st_rip3_jpeg_file_test_unavailable = false;
	if (!st_pixel_image_jpeg_compiled()) {
		CHECK(!st_pixel_image_jpeg_supported());
		CHECK(load(paths,"Gray.JPG",IO_ERROR_NOT_SUPPORTED,true) == 0);
		return 0;
	}
	CHECK(st_pixel_image_jpeg_supported());
	CHECK(load(paths,"gRAY.jpg",0,true) == 0 && load(paths,"Shared.JPG",0,true) == 0);
	CHECK(load(paths,"Scene.JPG",0,false) == 0); /* Host wins over shared. */
	CHECK(load(paths,"422.JPG",0,false) == 0 && load(paths,"420.JPG",0,false) == 0);
	CHECK(load(paths,"\xcf\x89MEGA.jpg",0,true) == 0); /* Ordered search + Unicode caseless. */
	CHECK(load(paths,"Gray",IO_ERROR_NOT_FOUND,true) == 0); /* No guessed extension. */
	CHECK(load(paths,"Odd.ext",0,true) == 0);
	CHECK(load(paths,"Missing.JPG",IO_ERROR_NOT_FOUND,true) == 0);
	CHECK(load(paths,"Empty.JPG",IO_ERROR_INVALID_FORMAT,true) == 0);
	CHECK(load(paths,"Broken.JPG",IO_ERROR_INVALID_FORMAT,true) == 0);
	CHECK(load(paths,"Truncated.JPG",IO_ERROR_INVALID_FORMAT,true) == 0);
	CHECK(load(paths,"Cmyk.JPG",IO_ERROR_NOT_SUPPORTED,true) == 0);
	CHECK(load(paths,"../Gray.JPG",IO_ERROR_INVALID_ARGUMENT,true) == 0);
	CHECK(load(paths,"*.JPG",IO_ERROR_INVALID_ARGUMENT,true) == 0);
	CHECK(load(paths,"\xff.JPG",IO_ERROR_INVALID_ENCODING,true) == 0);
	st_rip3_jpeg_file_test_fail_allocate = true;
	CHECK(load(paths,"Gray.JPG",IO_ERROR_OUT_OF_MEMORY,true) == 0 && !st_rip3_jpeg_file_test_fail_allocate);
	st_rip3_jpeg_file_test_fail_decode = true;
	CHECK(load(paths,"Gray.JPG",IO_ERROR_OUT_OF_MEMORY,true) == 0 && !st_rip3_jpeg_file_test_fail_decode);
	st_rip3_jpeg_file_test_fail_size = true;
	CHECK(load(paths,"Gray.JPG",IO_ERROR_IO,true) == 0 && !st_rip3_jpeg_file_test_fail_size);
	st_rip3_jpeg_file_test_read_fault = 1;
	CHECK(load(paths,"Gray.JPG",IO_ERROR_INVALID_FORMAT,true) == 0 && st_rip3_jpeg_file_test_read_fault == 0);
	st_rip3_jpeg_file_test_read_fault = 2;
	CHECK(load(paths,"Gray.JPG",IO_ERROR_IO,true) == 0 && st_rip3_jpeg_file_test_read_fault == 0);
	st_rip3_jpeg_file_test_fail_close = true;
	CHECK(load(paths,"Gray.JPG",IO_ERROR_IO,true) == 0 && !st_rip3_jpeg_file_test_fail_close);
	CHECK(load(paths,"Gray.JPG",0,true) == 0);
	return 0;
}

static int
create(struct fixture *f, enum st_rip3_canvas canvas, const struct st_rip3_resource_paths *paths)
{
	struct st_rip3_style style = {.background_color=8,.pen={.pattern=0xffff,.thickness=1},
	    .font={0,0,1,0},.write_mode=ST_RIP3_COPY};
	const struct st_rip3_rect port = {10,20,35,45}, view = {2,3,23,24};
	CHECK(st_rip3_fill_solid(&style.fill,true) == 0 && st_rip3_styles_create(&f->host.styles,&style) == 0);
	CHECK(st_rip3_styles_switch(f->host.styles,1) == 0);
	CHECK(st_rip3_environments_create(&f->host.environments,canvas) == 0
	    && st_rip3_ports_create(&f->host.ports,canvas) == 0 && st_rip3_palettes_create(&f->host.palettes) == 0);
	CHECK(st_rip3_ports_erase(f->host.ports,85) == 0 && st_rip3_ports_define(f->host.ports,1,&port,false) == 0
	    && st_rip3_ports_switch(f->host.ports,1) == 0 && st_rip3_ports_viewport(f->host.ports,&view) == 0);
	uint32_t colors[256]; for (unsigned i = 0; i < 256; i++) colors[i] = i*UINT32_C(0x010101);
	CHECK(st_rip3_palettes_switch(f->host.palettes,3) == 0 && st_rip3_palettes_rgb(f->host.palettes,0,8,colors,256) == 0
	    && st_rip3_palettes_protect(f->host.palettes,3,true) == 0);
	struct st_rip3_environment environment;
	CHECK(st_rip3_environments_get(f->host.environments,0,&environment) == 0);
	environment.port = 9; environment.palette = 7; environment.graphics_style = 6;
	environment.world_width = 13; environment.world_height = 11; /* Image size uses DEVICE style, not WORLD. */
	CHECK(st_rip3_environments_define(f->host.environments,2,&environment) == 0
	    && st_rip3_environments_select(f->host.environments,2) == 0);
	f->host.resources = paths; return 0;
}

static void
destroy(struct fixture *f)
{
	st_rip3_styles_destroy(f->host.styles); st_rip3_environments_destroy(f->host.environments);
	st_rip3_ports_destroy(f->host.ports); st_rip3_palettes_destroy(f->host.palettes);
}

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height,
    const struct st_rip3_resource_paths *paths, unsigned flags, enum st_rip3_write_mode mode)
{
	struct fixture f = {0}; struct st_rip3_style style; struct st_rip3_palette palette;
	struct st_rip3_bitmap *capture = NULL; struct st_rip3_stream *stream = NULL;
	const struct st_rip3_rect area = {1,1,18,18};
	CHECK(create(&f,canvas,paths) == 0 && st_rip3_styles_get(f.host.styles,1,&style) == 0);
	style.write_mode = mode;
	CHECK(st_rip3_styles_set(f.host.styles,1,&style) == 0 && st_rip3_styles_protect(f.host.styles,1,true) == 0);
	CHECK(rip3_test_image_area(f.host.ports,1,&area,flags) == 0 && st_rip3_ports_protect(f.host.ports,1,true) == 0);
	const struct st_rip3_stream_sink sink = {.context=&f,.command=command};
	CHECK(st_rip3_stream_create(&stream,&sink) == 0);
	/* Exercise the literal documented command with every byte boundary. */
	const char wire[] = "!|1p0000gRaY.JpG\r";
	st_rip3_image_test_fills = 0;
	for (size_t i = 0; i < sizeof(wire)-1; i++) st_rip3_stream_process(stream,(const uint8_t *)wire+i,1);
	CHECK(f.errors == 0);
	CHECK(st_rip3_image_test_fills == ((flags&4) != 0 ? 0u : 1u));
	if ((flags&4) == 0) {
		CHECK(st_rip3_image_test_fill_color == 0 && st_rip3_image_test_fill_mode == ST_RIP3_COPY);
		CHECK(st_rip3_image_test_fill_area.x0 == 1 && st_rip3_image_test_fill_area.x1 == 18
		    && st_rip3_image_test_fill_area.y0 == ((flags&1) != 0 ? 5 : 1)
		    && st_rip3_image_test_fill_area.y1 == ((flags&1) != 0 ? 13 : 18));
	}
	CHECK(st_rip3_palettes_get(f.host.palettes,3,&palette) == 0
	    && st_rip3_bitmap_capture(&capture,f.host.ports,0,NULL,&palette) == 0);
	CHECK(capture->pixels->width == width && capture->pixels->height == height);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		unsigned expected = 85;
		if (x >= 13 && x <= 30 && y >= 24 && y <= 41) {
			unsigned row = y-24;
			bool margin = (flags&1) != 0 && (row < 4 || row > 12);
			if (margin) expected = 85;
			else {
				unsigned source = (flags&1) != 0 ? (row == 12 ? 192 : 64) : (row >= 16 ? 192 : 64);
				expected = source; /* JPEG rows ignore every vector write mode. */
			}
		}
		CHECK(capture->pixels->primary[(size_t)y*width+x] == expected);
	}
	for (unsigned i = 0; i < 256; i++) CHECK(capture->palette[i] == i*UINT32_C(0x010101));
	unsigned current; struct st_rip3_port_info info; struct st_rip3_environment env;
	CHECK(st_rip3_ports_current(f.host.ports,&current) == 0 && current == 1
	    && st_rip3_ports_info(f.host.ports,1,&info) == 0 && info.protected && info.image.flags == flags);
	CHECK(st_rip3_environments_get(f.host.environments,2,&env) == 0 && env.port == 9 && env.palette == 7 && env.graphics_style == 6);
	st_rip3_bitmap_destroy(capture); st_rip3_stream_destroy(stream); destroy(&f); return 0;
}

static int
unchanged(struct fixture *f)
{
	struct st_rip3_palette palette; struct st_rip3_bitmap *capture = NULL;
	CHECK(st_rip3_palettes_get(f->host.palettes,3,&palette) == 0
	    && st_rip3_bitmap_capture(&capture,f->host.ports,0,NULL,&palette) == 0);
	for (size_t i = 0; i < (size_t)capture->pixels->width*capture->pixels->height; i++) CHECK(capture->pixels->primary[i] == 85);
	st_rip3_bitmap_destroy(capture); return 0;
}

static int
background(const struct st_rip3_resource_paths *paths)
{
	struct fixture f = {0}; struct st_rip3_style style; struct st_rip3_environment env;
	struct st_rip3_palette palette; struct st_rip3_bitmap *capture = NULL;
	const struct st_rip3_rect area = {1,1,17,9};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480,paths) == 0);
	CHECK(st_rip3_environments_get(f.host.environments,2,&env) == 0);
	env.rgb_bits = 8; env.base_math = 64;
	CHECK(st_rip3_environments_define(f.host.environments,2,&env) == 0);
	CHECK(st_rip3_styles_get(f.host.styles,1,&style) == 0);
	style.background_color = 0x606060; style.write_mode = ST_RIP3_COPY;
	CHECK(st_rip3_styles_set(f.host.styles,1,&style) == 0 && rip3_test_image_area(f.host.ports,1,&area,0) == 0);
	CHECK(process(&f,"1p0000Gray.JPG",-1) == 0);
	CHECK(st_rip3_palettes_get(f.host.palettes,3,&palette) == 0
	    && st_rip3_bitmap_capture(&capture,f.host.ports,1,NULL,&palette) == 0);
	for (uint32_t y = 0; y < capture->pixels->height; y++) for (uint32_t x = 0; x < capture->pixels->width; x++) {
		unsigned expected = x >= 1 && x <= 17 && y >= 1 && y <= 9 ? (y == 9 ? 192 : 64) : 85;
		CHECK(capture->pixels->primary[(size_t)y*capture->pixels->width+x] == expected);
	}
	st_rip3_bitmap_destroy(capture); capture = NULL;
	/* A skipped clear must not resolve an unused out-of-range background. */
	env.rgb_bits = 0;
	CHECK(st_rip3_environments_define(f.host.environments,2,&env) == 0);
	style.background_color = 256; style.write_mode = ST_RIP3_COPY;
	CHECK(st_rip3_styles_set(f.host.styles,1,&style) == 0 && rip3_test_image_area(f.host.ports,1,&area,4) == 0);
	CHECK(process(&f,"1p0000Gray.JPG",-1) == 0);
	/* Even when clearing is enabled, JPEG uses black rather than STYLE's
	 * background. No STYLE service is required for presentation at all. */
	CHECK(rip3_test_image_area(f.host.ports,1,&area,0) == 0);
	CHECK(process(&f,"1p0000Gray.JPG",-1) == 0);
	struct st_rip3_styles *styles = f.host.styles; f.host.styles = NULL;
	CHECK(process(&f,"1p0000Gray.JPG",-1) == 0); f.host.styles = styles;
	destroy(&f); return 0;
}

static int
recovery(const struct st_rip3_resource_paths *paths)
{
	struct fixture f = {0};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480,paths) == 0);
	CHECK(process(&f,"1p000",ST_RIP3_COMMAND_INVALID) == 0 && process(&f,"1p0000",ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&f,"1p0001Gray.JPG",ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&f,"1p0000$&FILE$",ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&f,"1p0000<<IF 1>>Gray.JPG",ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&f,"1p0000Gray[.JPG",ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&f,"1p0000Gray\\.JPG",ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	const uint8_t nul[] = {'1','p','0','0','0','0',0,'G'};
	unsigned before = f.errors; command(&f,nul,sizeof(nul)); CHECK(f.errors == before+1 && f.kind == ST_RIP3_COMMAND_INVALID);
	st_rip3_jpeg_file_test_unavailable = true;
	CHECK(process(&f,"1p0000Gray.JPG",ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	st_rip3_jpeg_file_test_unavailable = false;
	if (st_pixel_image_jpeg_compiled()) {
		CHECK(process(&f,"1p0000Missing.JPG",ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
		CHECK(process(&f,"1p0000Broken.JPG",ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
		CHECK(process(&f,"1p0000Truncated.JPG",ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
		CHECK(process(&f,"1p0000../Gray.JPG",ST_RIP3_COMMAND_INVALID) == 0);
		CHECK(process(&f,"1p0000\xff.JPG",ST_RIP3_COMMAND_INVALID) == 0);
		bool *const failures[] = {&st_rip3_jpeg_file_test_fail_allocate,&st_rip3_jpeg_file_test_fail_decode,
		    &st_rip3_image_commands_test_fail_allocate,&st_rip3_image_test_fail_allocate};
		for (size_t i = 0; i < sizeof(failures)/sizeof(failures[0]); i++) {
			*failures[i] = true; st_rip3_image_test_fills = 0;
			CHECK(process(&f,"1p0000Gray.JPG",ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !*failures[i]);
			CHECK(st_rip3_image_test_fills == 0);
		}
	}
	/* DELETE is stored without any file side effect. The explicit internal
	 * inhibit state is not wire 1i's all-zero rectangle. */
	const struct st_rip3_rect area = {1,1,18,18}, disabled = {0,0,0,0};
	CHECK(rip3_test_image_area(f.host.ports,1,&area,2) == 0);
	CHECK(process(&f,"1p0000Gray.JPG",st_pixel_image_jpeg_compiled() ? -1 : ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(st_rip3_ports_erase(f.host.ports,85) == 0);
	CHECK(rip3_test_image_area(f.host.ports,1,&disabled,4) == 0);
	CHECK(process(&f,"1p0000Gray.JPG",-1) == 0);
	CHECK(rip3_test_image_area(f.host.ports,1,&disabled,0) == 0);
	struct st_rip3_command_host saved = f.host;
	f.host.resources = NULL; f.host.palettes = NULL; f.host.styles = NULL;
	CHECK(process(&f,"1p0000Missing.JPG",-1) == 0);
	CHECK(rip3_test_image_area(f.host.ports,1,&area,0) == 0);
	CHECK(st_rip3_ports_activate(f.host.ports,false) == 0 && process(&f,"1p0000Missing.JPG",-1) == 0);
	CHECK(st_rip3_ports_activate(f.host.ports,true) == 0 && process(&f,"1p0000Gray.JPG",ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	f.host = saved;
	CHECK(unchanged(&f) == 0);
	/* A bad image and a later valid ENV edit share one byte-split stream. */
	struct st_rip3_stream *stream = NULL;
	const struct st_rip3_stream_sink sink = {.context=&f,.command=command};
	CHECK(st_rip3_stream_create(&stream,&sink) == 0);
	const char wire[] = "!|1p0000Broken.JPG|J1S\r";
	before = f.errors;
	for (size_t i = 0; i < sizeof(wire)-1; i++) st_rip3_stream_process(stream,(const uint8_t *)wire+i,1);
	struct st_rip3_environment env;
	CHECK(f.errors == before+1 && st_rip3_environments_get(f.host.environments,2,&env) == 0 && env.base_math == 64);
	CHECK(unchanged(&f) == 0);
	st_rip3_stream_destroy(stream); destroy(&f); return 0;
}

static int
inactive_style(enum st_rip3_canvas canvas, uint32_t width, uint32_t height,
    const struct st_rip3_resource_paths *paths)
{
	struct fixture f = {0}; struct st_rip3_environment env;
	CHECK(create(&f,canvas,paths) == 0 && st_rip3_environments_get(f.host.environments,2,&env) == 0);
	env.world_width = width; env.world_height = height;
	CHECK(st_rip3_environments_define(f.host.environments,2,&env) == 0);
	CHECK(process(&f,"1i00000K0K000F",-1) == 0 && st_rip3_ports_activate(f.host.ports,false) == 0);
	CHECK(st_rip3_ports_protect(f.host.ports,1,true) == 0);
	struct st_rip3_stream *stream = NULL;
	const struct st_rip3_stream_sink sink = {.context=&f,.command=command};
	CHECK(st_rip3_stream_create(&stream,&sink) == 0);
	const char set[] = "!|1i01010J0J0005|";
	for (size_t i = 0; i < sizeof(set)-1; i++) st_rip3_stream_process(stream,(const uint8_t *)set+i,1);
	struct st_rip3_port_info before, after;
	CHECK(f.errors == 0 && st_rip3_ports_info(f.host.ports,1,&before) == 0 && !before.viewport_active && before.protected);
	CHECK(before.image.enabled && !before.image.automatic && before.image.flags == 5
	    && before.image.area.x0 == 1 && before.image.area.y0 == 1 && before.image.area.x1 == 20 && before.image.area.y1 == 20);
	/* No allocator, lookup or decoder is reached while inactive, including
	 * the JPEG-disabled build. The newly stored style remains intact. */
	struct st_rip3_command_host saved = f.host;
	f.host.resources = NULL; f.host.palettes = NULL;
	st_rip3_image_commands_test_fail_allocate = true;
	CHECK(process(&f,"1p0000Missing.JPG",-1) == 0 && st_rip3_image_commands_test_fail_allocate);
	st_rip3_image_commands_test_fail_allocate = false; f.host = saved;
	CHECK(st_rip3_ports_protect(f.host.ports,1,false) == 0 && st_rip3_ports_activate(f.host.ports,true) == 0
	    && st_rip3_ports_protect(f.host.ports,1,true) == 0);
	const char show[] = "1p0000Gray.JPG|"; st_rip3_image_test_fills = 0;
	for (size_t i = 0; i < sizeof(show)-1; i++) st_rip3_stream_process(stream,(const uint8_t *)show+i,1);
	st_rip3_stream_destroy(stream);
	bool decoded = st_pixel_image_jpeg_compiled();
	CHECK(f.errors == (decoded ? 0u : 1u) && st_rip3_image_test_fills == 0);
	CHECK(st_rip3_ports_info(f.host.ports,1,&after) == 0 && after.viewport_active && after.protected
	    && memcmp(&before.image,&after.image,sizeof(before.image)) == 0);
	struct st_rip3_palette palette; struct st_rip3_bitmap *capture = NULL;
	CHECK(st_rip3_palettes_get(f.host.palettes,3,&palette) == 0 && st_rip3_bitmap_capture(&capture,f.host.ports,0,NULL,&palette) == 0);
	CHECK(capture->pixels->width == width && capture->pixels->height == height);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		unsigned expected = decoded && x >= 13 && x <= 30 && y >= 28 && y <= 36 ? y == 36 ? 192 : 64 : 85;
		CHECK(capture->pixels->primary[(size_t)y*width+x] == expected);
	}
	st_rip3_bitmap_destroy(capture); capture = NULL;
	/* Wire zeros are an enabled stored (0,0,1,1), reaching physical-device
	 * fallback on a full viewport. They never create the inhibit sentinel. */
	CHECK(st_rip3_ports_switch(f.host.ports,0) == 0 && process(&f,"1i000000000004",-1) == 0);
	CHECK(st_rip3_ports_info(f.host.ports,0,&after) == 0 && after.image.enabled && !after.image.automatic
	    && after.image.area.x1 == 1 && after.image.area.y1 == 1 && after.image.flags == 4);
	CHECK(process(&f,"1p0000Gray.JPG",decoded ? -1 : ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(st_rip3_bitmap_capture(&capture,f.host.ports,0,NULL,&palette) == 0);
	/* Source row8 begins at these independently calculated nearest-center
	 * thresholds for a 9-row DC-only authored JPEG fitted to each canvas. */
	unsigned top = height == 480 ? 427 : height == 600 ? 533 : 683;
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		unsigned expected = decoded ? y < top ? 64 : 192 : 85;
		CHECK(capture->pixels->primary[(size_t)y*width+x] == expected);
	}
	st_rip3_bitmap_destroy(capture); destroy(&f); return 0;
}

int
main(int argc, char **argv)
{
	CHECK(argc == 3);
	char *host = NULL, *shared = NULL, *search = NULL;
	CHECK(path_join_alloc(&host,argv[1],"host") == 0 && fs_create_directories(host) == 0);
	CHECK(path_join_alloc(&shared,argv[1],"shared") == 0 && fs_create_directories(shared) == 0);
	CHECK(path_join_alloc(&search,argv[1],"search") == 0 && fs_create_directories(search) == 0);
	const char *const entries[] = {search};
	const struct st_rip3_resource_paths paths = {host,shared,NULL,entries,1};
	CHECK(asset(argv[2],host,"Gray.JPG","gray.jpg",0) == 0 && asset(argv[2],host,"Scene.JPG","444.jpg",0) == 0);
	CHECK(asset(argv[2],shared,"scene.jpg","gray.jpg",0) == 0 && asset(argv[2],shared,"Shared.JPG","gray.jpg",0) == 0);
	CHECK(asset(argv[2],search,"\xce\xa9mega.JPG","gray.jpg",0) == 0 && asset(argv[2],host,"Odd.ext","gray.jpg",0) == 0);
	CHECK(asset(argv[2],host,"Truncated.JPG","gray.jpg",2) == 0 && asset(argv[2],host,"Cmyk.JPG","cmyk.jpg",0) == 0);
	CHECK(put(host,"Empty.JPG",NULL,0) == 0 && put(host,"Broken.JPG","bad",3) == 0);
	CHECK(asset(argv[2],host,"422.JPG","422.jpg",0) == 0 && asset(argv[2],host,"420.JPG","420.jpg",0) == 0);
	char *path = NULL; struct fs_info before, after;
	CHECK(path_join_alloc(&path,host,"Gray.JPG") == 0 && fs_set_modified_time(path,INT64_C(1234567890)) == 0
	    && fs_get_info(&before,path) == 0);
	CHECK(files(&paths) == 0 && recovery(&paths) == 0);
	CHECK(inactive_style(ST_RIP3_CANVAS_640_480,640,480,&paths) == 0);
	CHECK(inactive_style(ST_RIP3_CANVAS_800_600,800,600,&paths) == 0);
	CHECK(inactive_style(ST_RIP3_CANVAS_1024_768,1024,768,&paths) == 0);
	if (st_pixel_image_jpeg_compiled()) {
		const enum st_rip3_canvas canvases[] = {ST_RIP3_CANVAS_640_480,ST_RIP3_CANVAS_800_600,ST_RIP3_CANVAS_1024_768};
		CHECK(background(&paths) == 0);
		const uint32_t widths[] = {640,800,1024}, heights[] = {480,600,768};
		const unsigned flags[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
		for (size_t i = 0; i < 3; i++) for (size_t j = 0; j < sizeof(flags)/sizeof(flags[0]); j++)
			for (unsigned mode = ST_RIP3_COPY; mode <= ST_RIP3_NOT; mode++)
				CHECK(scene(canvases[i],widths[i],heights[i],&paths,flags[j],(enum st_rip3_write_mode)mode) == 0);
	}
	CHECK(fs_get_info(&after,path) == 0 && before.size == after.size && before.modified_time == after.modified_time);
	free(path); free(host); free(shared); free(search); return 0;
}
