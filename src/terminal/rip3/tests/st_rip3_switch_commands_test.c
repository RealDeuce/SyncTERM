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
create(struct fixture *f, enum st_rip3_canvas canvas)
{
	const struct st_rip3_rect first = {10,20,13,23}, second = {0,0,3,3};
	CHECK(st_rip3_environments_create(&f->host.environments,canvas) == 0);
	CHECK(st_rip3_ports_create(&f->host.ports,canvas) == 0);
	CHECK(st_rip3_ports_define(f->host.ports,1,&first,false) == 0);
	CHECK(st_rip3_ports_define(f->host.ports,2,&second,true) == 0);
	return 0;
}

static void
destroy(struct fixture *f)
{
	st_rip3_ports_destroy(f->host.ports); st_rip3_environments_destroy(f->host.environments);
}

static int
state(struct fixture *f, unsigned current, bool first, bool second)
{
	unsigned actual;
	struct st_rip3_port_info info;
	CHECK(st_rip3_ports_current(f->host.ports,&actual) == 0 && actual == current);
	CHECK(st_rip3_ports_info(f->host.ports,0,&info) == 0 && !info.protected);
	CHECK(st_rip3_ports_info(f->host.ports,1,&info) == 0 && info.protected == first);
	CHECK(st_rip3_ports_info(f->host.ports,2,&info) == 0 && info.protected == second);
	return 0;
}

static int
flags(void)
{
	struct fixture f = {0};
	const unsigned masks[] = {0,1,2,4,5,6,8,9,10};
	const char *const fields[] = {"00","01","02","04","05","06","08","09","0A"};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480) == 0);
	const struct st_rip3_rect image = {1,1,2,2};
	CHECK(st_rip3_ports_image_style(f.host.ports,1,&image,5) == 0);
	for (unsigned initial = 0; initial < 4; initial++) {
		for (size_t i = 0; i < sizeof(masks)/sizeof(masks[0]); i++) {
			bool first = (initial & 1) != 0, second = (initial & 2) != 0;
			char body[8];
			CHECK(st_rip3_ports_protect(f.host.ports,1,first) == 0);
			CHECK(st_rip3_ports_protect(f.host.ports,2,second) == 0);
			CHECK(st_rip3_ports_switch(f.host.ports,1) == 0);
			(void)snprintf(body,sizeof(body),"2s2%s",fields[i]);
			CHECK(process(&f,body,-1) == 0);
			CHECK(state(&f,2,(masks[i]&4) ? true : (masks[i]&8) ? false : first,
			    (masks[i]&1) ? true : (masks[i]&2) ? false : second) == 0);
		}
	}
	const char *const same[] = {"2s100","2s101","2s102","2s104","2s105","2s108","2s10A"};
	const bool expected[] = {false,true,false,true,true,false,false};
	CHECK(st_rip3_ports_protect(f.host.ports,1,false) == 0 && st_rip3_ports_switch(f.host.ports,1) == 0);
	for (size_t i = 0; i < sizeof(same)/sizeof(same[0]); i++) {
		CHECK(process(&f,same[i],-1) == 0);
		struct st_rip3_port_info info;
		CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && info.protected == expected[i]);
	}
	CHECK(process(&f,"2s000",-1) == 0);
	CHECK(process(&f,"2s005",-1) == 0); /* Port zero stays unprotectable. */
	CHECK(process(&f,"2s002",-1) == 0 && state(&f,0,false,false) == 0);
	CHECK(process(&f,"2s104",-1) == 0 && state(&f,1,false,false) == 0); /* Leaving zero cannot protect it. */
	CHECK(process(&f,"2s001",-1) == 0 && state(&f,0,false,false) == 0);
	struct st_rip3_port_info info;
	CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && info.image.enabled && !info.image.automatic
	    && info.image.flags == 5 && info.image.area.x0 == 1 && info.image.area.y0 == 1
	    && info.image.area.x1 == 2 && info.image.area.y1 == 2
	    && info.bounds.x0 == 10 && info.bounds.y0 == 20 && info.bounds.x1 == 13 && info.bounds.y1 == 23
	    && info.viewport.x0 == 0 && info.viewport.y0 == 0 && info.viewport.x1 == 3 && info.viewport.y1 == 3);
	destroy(&f);
	return 0;
}

static int
validation(void)
{
	struct fixture f = {0};
	const char *const invalid[] = {"2s","2s1","2s10","2s1?0","2s1000000","2s?00"};
	const char *const unsupported[] = {"2s1000","2s100000","2s100 comment","2s10G",
	    "2s203","2s20C","2s106","2s109","2s701","2s704"};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_protect(f.host.ports,1,true) == 0 && st_rip3_ports_switch(f.host.ports,1) == 0);
	for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
		CHECK(process(&f,invalid[i],ST_RIP3_COMMAND_INVALID) == 0 && state(&f,1,true,false) == 0);
	}
	for (size_t i = 0; i < sizeof(unsupported)/sizeof(unsupported[0]); i++) {
		CHECK(process(&f,unsupported[i],ST_RIP3_COMMAND_UNSUPPORTED) == 0 && state(&f,1,true,false) == 0);
	}
	CHECK(process(&f,"2s700",-1) == 0 && state(&f,0,true,false) == 0);
	CHECK(process(&f,"2s100",-1) == 0 && state(&f,1,true,false) == 0);
	struct st_rip3_ports *ports = f.host.ports;
	f.host.ports = NULL;
	CHECK(process(&f,"2s000",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.ports = ports;
	struct st_rip3_environments *environments = f.host.environments;
	f.host.environments = NULL;
	CHECK(process(&f,"2s000",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.environments = environments;
	CHECK(state(&f,1,true,false) == 0);
	CHECK(process(&f,"J1S",-1) == 0);
	CHECK(process(&f,"2sa00",ST_RIP3_COMMAND_INVALID) == 0); /* UltraNum a=36. */
	CHECK(process(&f,"2s10a",ST_RIP3_COMMAND_UNSUPPORTED) == 0); /* Unknown flag bits. */
	CHECK(process(&f,"2s10A",-1) == 0 && state(&f,1,false,false) == 0);
	CHECK(process(&f,"J10",-1) == 0);
	CHECK(process(&f,"2s10a",-1) == 0); /* MegaNum folds case: a=10. */
	CHECK(process(&f,"2s1--",-1) == 0 && state(&f,1,false,false) == 0);
	const struct st_rip3_rect point = {0,0,0,0};
	CHECK(st_rip3_ports_define(f.host.ports,35,&point,true) == 0);
	CHECK(process(&f,"2sZ00",-1) == 0 && state(&f,35,false,false) == 0);
	CHECK(process(&f,"J1S",-1) == 0);
	CHECK(process(&f,"2sZ00",-1) == 0 && state(&f,35,false,false) == 0);
	CHECK(process(&f,"J10",-1) == 0);
	/* The command does not activate/rewrite an ENV port reference. */
	struct st_rip3_environment environment;
	CHECK(st_rip3_environments_get(f.host.environments,0,&environment) == 0);
	environment.port = 7;
	CHECK(st_rip3_environments_define(f.host.environments,1,&environment) == 0);
	CHECK(st_rip3_environments_select(f.host.environments,1) == 0);
	CHECK(st_rip3_environments_protect(f.host.environments,1,true) == 0);
	CHECK(process(&f,"2s200",-1) == 0 && state(&f,2,false,false) == 0);
	CHECK(st_rip3_environments_get(f.host.environments,1,&environment) == 0 && environment.port == 7);
	destroy(&f);
	return 0;
}

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct fixture f = {0};
	struct st_rip3_stream *stream = NULL;
	struct st_pixel_buffer *capture = NULL;
	struct st_rip3_palette palette;
	const struct st_rip3_rect viewport = {1,1,2,2}, inactive = {30,40,31,41}, point = {0,0,0,0};
	struct st_rip3_clipboard_selection clipboard = {true,35};
	const struct st_rip3_stream_sink sink = {.context=&f,.command=command};
	const uint8_t input[] = "!|2s100|1P0000000|2s200|1P0000000|2s300|1P0000000|2s106|2s109|2s000|1P0000000|2s002\r";
	CHECK(create(&f,canvas) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,1) == 0 && st_rip3_ports_viewport(f.host.ports,&viewport) == 0);
	CHECK(st_rip3_ports_define(f.host.ports,3,&inactive,false) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,3) == 0 && st_rip3_ports_activate(f.host.ports,false) == 0);
	CHECK(st_rip3_ports_define(f.host.ports,35,&point,true) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,35) == 0 && st_rip3_ports_erase(f.host.ports,9) == 0);
	f.host.clipboard = &clipboard;
	CHECK(st_rip3_stream_create(&stream,&sink) == 0);
	for (size_t i = 0; i < sizeof(input)-1; i++) st_rip3_stream_process(stream,input+i,1);
	CHECK(f.errors == 1 && f.kind == ST_RIP3_COMMAND_UNSUPPORTED && state(&f,0,false,false) == 0);
	for (unsigned i = 0; i < 256; i++) palette.rgb[i] = i*UINT32_C(0x010101);
	CHECK(st_rip3_ports_snapshot(f.host.ports,&palette,&capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		uint32_t expected = (x == 0 && y == 0) || (x == 11 && y == 21) ? UINT32_C(0x090909) : 0;
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | expected));
	}
	uint8_t pixel;
	CHECK(st_rip3_ports_read(f.host.ports,2,0,0,&pixel) == 0 && pixel == 9);
	struct st_rip3_port_info info;
	CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && info.viewport.x0 == 1 && info.viewport.y0 == 1
	    && info.viewport.x1 == 2 && info.viewport.y1 == 2);
	CHECK(st_rip3_ports_info(f.host.ports,3,&info) == 0 && !info.viewport_active && info.protected);
	st_pixel_buffer_destroy(capture); st_rip3_stream_destroy(stream); destroy(&f);
	return 0;
}

int
main(void)
{
	CHECK(flags() == 0 && validation() == 0);
	CHECK(scene(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	return 0;
}
