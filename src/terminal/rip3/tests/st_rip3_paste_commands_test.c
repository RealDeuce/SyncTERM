/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_ports.h"
#include "st_rip3_stream.h"
#include "st_pixels.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern bool st_rip3_copy_test_fail_malloc;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr,"check failed at line %d: %s\n",__LINE__,#expression); \
		return 1; \
	} \
} while (0)

struct fixture {
	struct st_rip3_command_host host;
	struct st_rip3_clipboard_selection clipboard;
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

static int
create(struct fixture *f, enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_environment environment;
	CHECK(st_rip3_environments_create(&f->host.environments,canvas) == 0);
	CHECK(st_rip3_ports_create(&f->host.ports,canvas) == 0);
	CHECK(st_rip3_environments_get(f->host.environments,0,&environment) == 0);
	environment.world_width = width; environment.world_height = height;
	CHECK(st_rip3_environments_set(f->host.environments,0,&environment) == 0);
	f->clipboard = (struct st_rip3_clipboard_selection){true,1};
	f->host.clipboard = &f->clipboard;
	return 0;
}

static void
destroy(struct fixture *f)
{
	st_rip3_ports_destroy(f->host.ports);
	st_rip3_environments_destroy(f->host.environments);
}

static int
row(struct fixture *f, unsigned port, int32_t x, int32_t y, const uint8_t *expected, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		uint8_t actual;
		CHECK(st_rip3_ports_read(f->host.ports,port,x+(int32_t)i,y,&actual) == 0 && actual == expected[i]);
	}
	return 0;
}

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct fixture f = {0};
	struct st_rip3_stream *stream = NULL;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	const struct st_rip3_rect source = {0,0,3,3}, source_view = {1,1,2,2};
	const struct st_rip3_rect destination = {10,20,41,26}, destination_view = {2,1,31,6};
	const uint8_t pixels[] = {3,12,48,192};
	const uint8_t expected_modes[5][4] = {
		{3,12,48,192},{86,89,101,149},{87,93,117,213},{1,4,16,64},{252,243,207,63},
	};
	const uint8_t input[] = "!|1P0000000|1P0400010|1P0800020|1P0C00030|1P0G00040|1P0K00050|1P0K00000|1P0T05000\r";
	const struct st_rip3_stream_sink sink = {.context=&f,.command=command};
	CHECK(create(&f,canvas,width,height) == 0);
	CHECK(st_rip3_ports_define(f.host.ports,1,&source,true) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,1) == 0);
	CHECK(st_rip3_ports_erase(f.host.ports,0xee) == 0);
	CHECK(st_rip3_ports_viewport(f.host.ports,&source_view) == 0);
	for (int32_t y = 0; y < 2; y++) for (int32_t x = 0; x < 2; x++)
		CHECK(st_rip3_ports_plot(f.host.ports,x,y,pixels[2*y+x],ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_protect(f.host.ports,1,true) == 0);
	CHECK(st_rip3_ports_define(f.host.ports,2,&destination,false) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,2) == 0);
	CHECK(st_rip3_ports_viewport(f.host.ports,&destination_view) == 0);
	CHECK(st_rip3_ports_erase(f.host.ports,0x55) == 0);
	CHECK(st_rip3_ports_protect(f.host.ports,2,true) == 0);
	CHECK(st_rip3_stream_create(&stream,&sink) == 0);
	for (size_t i = 0; i < sizeof(input)-1; i++) st_rip3_stream_process(stream,input+i,1);
	CHECK(f.errors == 1 && f.kind == ST_RIP3_COMMAND_INVALID);
	for (unsigned i = 0; i < 256; i++) palette.rgb[i] = i*UINT32_C(0x010101);
	CHECK(st_rip3_ports_snapshot(f.host.ports,&palette,&capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		uint8_t expected = x >= 12 && x <= 41 && y >= 21 && y <= 26 ? 0x55 : 0;
		for (unsigned mode = 0; mode < 5; mode++) {
			uint32_t left = 12+4*mode;
			if (x >= left && x <= left+1 && y >= 21 && y <= 22)
				expected = expected_modes[mode][2*(y-21)+x-left];
		}
		if (x >= 32 && x <= 33 && y >= 21 && y <= 22) expected = pixels[2*(y-21)+x-32];
		if (x == 41 && y == 26) expected = 3;
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | expected*UINT32_C(0x010101)));
	}
	struct st_rip3_port_info info;
	struct st_rip3_environment environment;
	unsigned current;
	CHECK(st_rip3_ports_current(f.host.ports,&current) == 0 && current == 2);
	CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && info.protected
	    && info.viewport.x0 == 1 && info.viewport.y0 == 1 && info.viewport.x1 == 2 && info.viewport.y1 == 2);
	CHECK(st_rip3_ports_info(f.host.ports,2,&info) == 0 && info.protected
	    && info.viewport.x0 == 2 && info.viewport.y0 == 1 && info.viewport.x1 == 31 && info.viewport.y1 == 6);
	CHECK(f.clipboard.set && f.clipboard.port == 1);
	CHECK(st_rip3_environments_get(f.host.environments,0,&environment) == 0 && environment.port == 0);
	CHECK(row(&f,1,1,1,pixels,2) == 0 && row(&f,1,1,2,pixels+2,2) == 0);
	st_pixel_buffer_destroy(capture); st_rip3_stream_destroy(stream); destroy(&f);
	return 0;
}

static int
validation(void)
{
	struct fixture f = {0};
	const struct st_rip3_rect area = {0,0,7,0};
	const uint8_t original[] = {1,2,4,8,16,32,64,128};
	const uint8_t copied[] = {1,2,1,2,4,8,16,32,64,128};
	const char *const invalid[] = {"1P","1P0000","1P00000000","1P0?00000","1P00000?0","1P000000?","1P0000050"};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(st_rip3_ports_define(f.host.ports,1,&area,false) == 0);
	for (int32_t x = 0; x < 8; x++) CHECK(st_rip3_ports_plot(f.host.ports,x,0,original[x],ST_RIP3_COPY) == 0);
	for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
		CHECK(process(&f,invalid[i],ST_RIP3_COMMAND_INVALID) == 0);
		CHECK(row(&f,0,0,0,original,8) == 0);
	}
	CHECK(process(&f,"1P0000001",ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&f,"1P0000000 comment",ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	struct st_rip3_ports *ports = f.host.ports;
	struct st_rip3_environments *environments = f.host.environments;
	f.host.ports = NULL;
	CHECK(process(&f,"1P0000000",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.ports = ports;
	f.host.environments = NULL;
	CHECK(process(&f,"1P0000000",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.environments = environments;
	f.host.clipboard = NULL;
	CHECK(process(&f,"1P0000000",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.clipboard = &f.clipboard;
	f.clipboard.set = false; f.clipboard.port = 99;
	CHECK(process(&f,"1P0200000",-1) == 0 && row(&f,0,0,0,original,8) == 0);
	f.clipboard.set = true; f.clipboard.port = 36;
	CHECK(process(&f,"1P0200000",ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	f.clipboard.port = 0;
	CHECK(process(&f,"1P0200000",ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	f.clipboard.port = 2; /* Undefined pointer semantics need #42, without editing it. */
	CHECK(process(&f,"1P0200000",ST_RIP3_COMMAND_UNSUPPORTED) == 0 && f.clipboard.port == 2);
	f.clipboard.port = 1;
	CHECK(st_rip3_ports_switch(f.host.ports,1) == 0);
	st_rip3_copy_test_fail_malloc = true;
	CHECK(process(&f,"1P0200000",-1) == 0 && st_rip3_copy_test_fail_malloc);
	CHECK(st_rip3_ports_switch(f.host.ports,0) == 0);
	CHECK(process(&f,"1P0200000",ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_copy_test_fail_malloc);
	CHECK(row(&f,0,0,0,original,8) == 0 && f.clipboard.port == 1);
	CHECK(process(&f,"1P0200000",-1) == 0 && row(&f,0,0,0,copied,10) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,1) == 0 && st_rip3_ports_activate(f.host.ports,false) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,0) == 0);
	CHECK(process(&f,"1P0200000",ST_RIP3_COMMAND_UNSUPPORTED) == 0 && row(&f,0,0,0,copied,10) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,1) == 0 && st_rip3_ports_activate(f.host.ports,true) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,0) == 0 && st_rip3_ports_activate(f.host.ports,false) == 0);
	CHECK(process(&f,"1P0200000",-1) == 0 && row(&f,0,0,0,copied,10) == 0);
	destroy(&f);
	return 0;
}

static int
source_view_change(void)
{
	struct fixture f = {0};
	const struct st_rip3_rect area = {0,0,3,0}, subset = {1,0,2,0};
	const uint8_t source[] = {3,12,48,192}, expected[] = {3,12,48,192,12,48,0};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(st_rip3_ports_define(f.host.ports,1,&area,true) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,1) == 0);
	for (int32_t x = 0; x < 4; x++) CHECK(st_rip3_ports_plot(f.host.ports,x,0,source[x],ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,0) == 0);
	CHECK(process(&f,"1P0000000",-1) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,1) == 0 && st_rip3_ports_viewport(f.host.ports,&subset) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,0) == 0);
	CHECK(process(&f,"1P0400000",-1) == 0 && row(&f,0,0,0,expected,7) == 0);
	CHECK(f.clipboard.set && f.clipboard.port == 1);
	destroy(&f);
	return 0;
}

static int
projection(enum st_rip3_canvas canvas, int32_t expected_x, int32_t expected_y)
{
	struct fixture f = {0};
	const struct st_rip3_rect area = {0,0,1,1};
	const uint8_t expected[] = {7,7};
	CHECK(create(&f,canvas,1280,960) == 0);
	CHECK(st_rip3_ports_define(f.host.ports,1,&area,true) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,1) == 0 && st_rip3_ports_erase(f.host.ports,7) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,0) == 0);
	/* Literal canonical V4=1120 and OM=886 in MegaNum. Device endpoints
	 * follow the independent floor ratios of each agreed canvas. */
	CHECK(process(&f,"1PV4OM000",-1) == 0);
	CHECK(row(&f,0,expected_x,expected_y,expected,2) == 0);
	CHECK(row(&f,0,expected_x,expected_y+1,expected,2) == 0);
	destroy(&f);
	return 0;
}

static int
fields(void)
{
	struct fixture f = {0};
	const struct st_rip3_rect area = {0,0,0,0};
	const uint8_t expected[] = {11};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(st_rip3_ports_define(f.host.ports,35,&area,true) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,35) == 0 && st_rip3_ports_erase(f.host.ports,11) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,0) == 0);
	f.clipboard.port = 35;
	CHECK(process(&f,"J1S",-1) == 0);
	CHECK(process(&f,"1P0#0z000",-1) == 0 && row(&f,0,62,61,expected,1) == 0);
	CHECK(process(&f,"1P0&0#0",-1) == 0 && row(&f,0,63,62,expected,1) == 0);
	CHECK(process(&f,"J10",-1) == 0);
	CHECK(process(&f,"1P0#0z000",ST_RIP3_COMMAND_INVALID) == 0);
	for (unsigned width = 2; width <= 5; width++) {
		char body[40];
		(void)snprintf(body,sizeof(body),"n%u",width); CHECK(process(&f,body,-1) == 0);
		(void)snprintf(body,sizeof(body),"1P%0*u%0*u000",(int)width,2u,(int)width,3u);
		CHECK(process(&f,body,-1) == 0 && row(&f,0,2,3,expected,1) == 0);
		(void)snprintf(body,sizeof(body),"1P%0*u%0*u00",(int)width,4u,(int)width,3u);
		CHECK(process(&f,body,-1) == 0 && row(&f,0,4,3,expected,1) == 0);
		(void)snprintf(body,sizeof(body),"1P%0*u%0*u0",(int)width,6u,(int)width,3u);
		CHECK(process(&f,body,-1) == 0 && row(&f,0,6,3,expected,1) == 0);
	}
	CHECK(process(&f,"J1S",-1) == 0);
	/* Maximal valid fields project far beyond int32_t at world size one;
	 * clipping keeps this an empty copy, with no wrapped destination. */
	struct st_rip3_environment environment;
	CHECK(st_rip3_environments_get(f.host.environments,0,&environment) == 0);
	environment.world_width = environment.world_height = 1;
	CHECK(st_rip3_environments_set(f.host.environments,0,&environment) == 0);
	CHECK(process(&f,"1P&&&&&&&&&&000",-1) == 0);
	CHECK(row(&f,0,6,3,expected,1) == 0 && f.clipboard.set && f.clipboard.port == 35);
	destroy(&f);
	return 0;
}

int
main(void)
{
	CHECK(scene(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	CHECK(validation() == 0 && fields() == 0 && source_view_change() == 0);
	CHECK(projection(ST_RIP3_CANVAS_640_480,560,443) == 0);
	CHECK(projection(ST_RIP3_CANVAS_800_600,700,553) == 0);
	CHECK(projection(ST_RIP3_CANVAS_1024_768,896,708) == 0);
	return 0;
}
