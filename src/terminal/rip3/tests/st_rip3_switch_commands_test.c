/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "image_area_fixture.h"
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_ports.h"
#include "st_rip3_stream.h"
#include "st_pixels.h"
#include "io_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int st_rip3_switch_test_stale(struct st_rip3_ports *ports, unsigned number, bool protect);

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
adopted_examples(void)
{
	const struct {
		const char *body;
		unsigned current;
		bool source_protected, target_protected, failed;
	} cases[] = {
		{"2s106",1,false,false,false}, {"2s109",1,true,true,false},
		{"2s103",1,false,false,false}, {"2s10C",1,false,false,false},
		{"2s704",7,true,false,false}, {"2s701",7,false,true,false},
		{"2s704",0,true,true,true}, {"2s701",0,false,true,true},
	};
	struct fixture f = {0};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480) == 0);
	for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
		struct st_rip3_port_info info;
		CHECK(st_rip3_ports_protect(f.host.ports,1,false) == 0);
		CHECK(st_rip3_ports_info(f.host.ports,7,&info) == 0);
		if (info.defined) {
			CHECK(st_rip3_ports_protect(f.host.ports,7,false) == 0);
			CHECK(st_rip3_ports_delete(f.host.ports,7) == 0);
		}
		CHECK(st_rip3_switch_test_stale(f.host.ports,7,cases[i].failed) == 0);
		CHECK(st_rip3_ports_switch(f.host.ports,1) == 0);
		CHECK(process(&f,cases[i].body,cases[i].failed ? ST_RIP3_COMMAND_OPERATION_FAILED : -1) == 0);
		CHECK(state(&f,cases[i].current,cases[i].source_protected,false) == 0);
		unsigned target = cases[i].current == 1 ? 1 : 7;
		CHECK(st_rip3_ports_info(f.host.ports,target,&info) == 0);
		CHECK(info.defined == !cases[i].failed && info.protected == cases[i].target_protected);
		if (target == 7 && !cases[i].failed) {
			CHECK(!info.offscreen && info.viewport_active && info.bounds.x0 == 0 && info.bounds.y0 == 0
			    && info.bounds.x1 == 639 && info.bounds.y1 == 479
			    && info.viewport.x0 == 0 && info.viewport.y0 == 0
			    && info.viewport.x1 == 639 && info.viewport.y1 == 479);
			CHECK(info.image.automatic && info.image.enabled && info.image.flags == 0
			    && info.image.area.x0 == -1 && info.image.area.y0 == -1
			    && info.image.area.x1 == -1 && info.image.area.y1 == -1);
		}
	}
	destroy(&f);
	return 0;
}

static bool
same_rect(const struct st_rip3_rect *a, const struct st_rip3_rect *b)
{
	return a->x0 == b->x0 && a->y0 == b->y0 && a->x1 == b->x1 && a->y1 == b->y1;
}

static bool
same_info(const struct st_rip3_port_info *a, const struct st_rip3_port_info *b)
{
	return a->defined == b->defined && a->offscreen == b->offscreen
	    && a->protected == b->protected && a->viewport_active == b->viewport_active
	    && same_rect(&a->bounds,&b->bounds) && same_rect(&a->viewport,&b->viewport)
	    && a->image.automatic == b->image.automatic && a->image.enabled == b->image.enabled
	    && a->image.flags == b->image.flags && same_rect(&a->image.area,&b->image.area);
}

/* kind: existing screen, existing bitmap, ordinary absent, stale protected. */
static int
prepare_target(struct st_rip3_ports *ports, unsigned target, unsigned kind)
{
	struct st_rip3_port_info info;
	const struct st_rip3_rect bounds = {2*(int32_t)target,4,2*(int32_t)target+3,7};
	CHECK(st_rip3_ports_info(ports,target,&info) == 0);
	if (target == 0) return 0;
	if (info.defined) {
		CHECK(st_rip3_ports_protect(ports,target,false) == 0);
		if (kind >= 2) CHECK(st_rip3_ports_delete(ports,target) == 0);
	}
	else CHECK(st_rip3_switch_test_stale(ports,target,false) == 0);
	if (kind < 2) CHECK(st_rip3_ports_define(ports,target,&bounds,kind == 1) == 0);
	else CHECK(st_rip3_switch_test_stale(ports,target,kind == 3) == 0);
	return 0;
}

static int
matrix(enum st_rip3_canvas canvas)
{
	const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	struct fixture f = {0};
	struct st_rip3_port_info defaults;
	struct st_rip3_environment environment;
	const struct st_rip3_rect view = {5,7,10,11}, image = {1,2,4,5};
	CHECK(create(&f,canvas) == 0);
	CHECK(st_rip3_ports_info(f.host.ports,0,&defaults) == 0);
	/* Port zero's stored viewport/image are deliberately different from a
	 * fresh port's defaults. Construction uses the physical screen geometry. */
	CHECK(st_rip3_ports_viewport(f.host.ports,&view) == 0);
	CHECK(st_rip3_ports_activate(f.host.ports,false) == 0);
	CHECK(st_rip3_ports_image_style(f.host.ports,0,&image,45) == 0);
	CHECK(st_rip3_environments_get(f.host.environments,0,&environment) == 0);
	environment.world_width = 1; environment.world_height = 1;
	environment.port = 17; environment.palette = 7; environment.coordinate_size = 5;
	CHECK(st_rip3_environments_define(f.host.environments,1,&environment) == 0);
	CHECK(st_rip3_environments_select(f.host.environments,1) == 0);
	CHECK(st_rip3_environments_protect(f.host.environments,1,true) == 0);
	for (unsigned i = 1; i < 36; i++) CHECK(prepare_target(f.host.ports,i,0) == 0);
	for (unsigned source = 0; source < 36; source++) for (unsigned target = 0; target < 36; target++) {
		for (unsigned kind = 0; kind < 4; kind++) {
			if ((target == 0 && kind != 0) || (source == target && kind >= 2)) continue;
			for (unsigned flags = 0; flags < 16; flags++) for (unsigned initial = 0; initial < 4; initial++) {
				bool sp = (initial&1) != 0, dp = (initial&2) != 0;
				if ((source == 0 && sp) || (target == 0 && dp)
				    || (source == target && sp != dp)
				    || (kind == 2 && dp) || (kind == 3 && !dp)) continue;
				CHECK(prepare_target(f.host.ports,target,kind) == 0);
				if (source != 0) CHECK(st_rip3_ports_protect(f.host.ports,source,sp) == 0);
				if (target != 0 && kind < 2) CHECK(st_rip3_ports_protect(f.host.ports,target,dp) == 0);
				CHECK(st_rip3_ports_switch(f.host.ports,source) == 0);
				struct st_rip3_port_info expected[36], actual;
				for (unsigned i = 0; i < 36; i++) CHECK(st_rip3_ports_info(f.host.ports,i,expected+i) == 0);
				unsigned selected = kind == 3 ? 0 : target;
				if (source != 0) expected[source].protected = (flags&8) ? false : (flags&4) ? true : sp;
				if (kind == 2) expected[target] = defaults;
				if (selected != 0) expected[selected].protected = (flags&2) ? false : (flags&1) ? true : expected[selected].protected;
				char body[] = {'2','s',digits[target],'0',digits[flags],0};
				CHECK(process(&f,body,kind == 3 ? ST_RIP3_COMMAND_OPERATION_FAILED : -1) == 0);
				unsigned current;
				CHECK(st_rip3_ports_current(f.host.ports,&current) == 0 && current == selected);
				for (unsigned i = 0; i < 36; i++) {
					CHECK(st_rip3_ports_info(f.host.ports,i,&actual) == 0);
					CHECK(same_info(expected+i,&actual));
				}
			}
			CHECK(prepare_target(f.host.ports,target,0) == 0);
		}
	}
	CHECK(st_rip3_environments_get(f.host.environments,1,&environment) == 0
	    && environment.world_width == 1 && environment.world_height == 1
	    && environment.port == 17 && environment.palette == 7 && environment.coordinate_size == 5);
	destroy(&f);
	return 0;
}

static int
higher_flags(void)
{
	const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#&";
	struct fixture f = {0};
	struct st_rip3_environment environment;
	CHECK(create(&f,ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_environments_get(f.host.environments,0,&environment) == 0);
	for (unsigned radix = 36; radix <= 64; radix += 28) {
		environment.base_math = radix;
		CHECK(st_rip3_environments_set(f.host.environments,0,&environment) == 0);
		for (unsigned target = 1; target <= 2; target++) for (unsigned initial = 0; initial < 4; initial++) {
			bool sp = (initial&1) != 0, dp = (initial&2) != 0;
			if (target == 1 && sp != dp) continue;
			for (unsigned flags = 0; flags < radix*radix; flags++) {
				CHECK(st_rip3_ports_protect(f.host.ports,1,sp) == 0);
				CHECK(st_rip3_ports_protect(f.host.ports,2,dp) == 0);
				CHECK(st_rip3_ports_switch(f.host.ports,1) == 0);
				char body[] = {'2','s',(char)('0'+target),digits[flags/radix],digits[flags%radix],0};
				CHECK(process(&f,body,-1) == 0);
				bool source_final = (flags&8) ? false : (flags&4) ? true : sp;
				bool destination_final = (flags&2) ? false : (flags&1) ? true : target == 1 ? source_final : dp;
				CHECK(state(&f,target,target == 1 ? destination_final : source_final,
				    target == 2 ? destination_final : dp) == 0);
			}
		}
	}
	destroy(&f);
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
	CHECK(rip3_test_image_area(f.host.ports,1,&image,5) == 0);
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
	    && info.image.area.x1 == 4 && info.image.area.y1 == 4
	    && info.bounds.x0 == 10 && info.bounds.y0 == 20 && info.bounds.x1 == 13 && info.bounds.y1 == 23
	    && info.viewport.x0 == 0 && info.viewport.y0 == 0 && info.viewport.x1 == 3 && info.viewport.y1 == 3);
	destroy(&f);
	return 0;
}

static int
validation(void)
{
	struct fixture f = {0};
	const char *const invalid[] = {"2s","2s1","2s10 comment","2s1?0","2s?00","2s10."};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_protect(f.host.ports,1,true) == 0 && st_rip3_ports_switch(f.host.ports,1) == 0);
	for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
		CHECK(process(&f,invalid[i],ST_RIP3_COMMAND_INVALID) == 0 && state(&f,1,true,false) == 0);
	}
	CHECK(process(&f,"2s70",-1) == 0 && state(&f,7,true,false) == 0);
	CHECK(process(&f,"2s1. comment",-1) == 0 && state(&f,1,true,false) == 0);
	CHECK(process(&f,"2s7000",-1) == 0 && state(&f,7,true,false) == 0);
	CHECK(process(&f,"2s100 comment",-1) == 0 && state(&f,1,true,false) == 0);
	struct st_rip3_ports *ports = f.host.ports;
	f.host.ports = NULL;
	CHECK(process(&f,"2s000",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.ports = ports;
	struct st_rip3_environments *environments = f.host.environments;
	f.host.environments = NULL;
	CHECK(process(&f,"2s000",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.environments = environments;
	CHECK(state(&f,1,true,false) == 0);
	CHECK(process(&f,"J1S",-1) == 0);
	CHECK(process(&f,"2sa00",ST_RIP3_COMMAND_INVALID) == 0); /* UltraNum a=36. */
	CHECK(process(&f,"2s10a",-1) == 0 && state(&f,1,true,false) == 0); /* UltraNum a=36; higher bits ignored. */
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
	const uint8_t input[] = "!|2s100|1P0000000|2s200|1P0000000|2s300|1P0000000|2s106|2s109|2s000|1P0000000|2s700|1P0506000|2s?00|2s002\r";
	CHECK(create(&f,canvas) == 0);
	struct st_rip3_environment environment;
	CHECK(st_rip3_environments_get(f.host.environments,0,&environment) == 0);
	environment.world_width = width; environment.world_height = height;
	CHECK(st_rip3_environments_set(f.host.environments,0,&environment) == 0);

	CHECK(st_rip3_ports_switch(f.host.ports,1) == 0 && st_rip3_ports_viewport(f.host.ports,&viewport) == 0);
	CHECK(st_rip3_ports_define(f.host.ports,3,&inactive,false) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,3) == 0 && st_rip3_ports_activate(f.host.ports,false) == 0);
	CHECK(st_rip3_ports_define(f.host.ports,35,&point,true) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,35) == 0 && st_rip3_ports_erase(f.host.ports,9) == 0);
	f.host.clipboard = &clipboard;
	CHECK(st_rip3_stream_create(&stream,&sink) == 0);
	for (size_t i = 0; i < sizeof(input)-1; i++) st_rip3_stream_process(stream,input+i,1);
	CHECK(f.errors == 1 && f.kind == ST_RIP3_COMMAND_INVALID && state(&f,0,true,false) == 0);
	for (unsigned i = 0; i < 256; i++) palette.rgb[i] = i*UINT32_C(0x010101);
	CHECK(st_rip3_ports_snapshot(f.host.ports,&palette,&capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		uint32_t expected = (x == 0 && y == 0) || (x == 5 && y == 6) || (x == 11 && y == 21) ? UINT32_C(0x090909) : 0;
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

static int environment_protocol(void)
{
	struct fixture f={0},missing={0};struct st_rip3_stream *stream=NULL;
	CHECK(process(&missing,"2E00",ST_RIP3_COMMAND_UNSUPPORTED)==0);
	CHECK(create(&f,ST_RIP3_CANVAS_640_480)==0);
	struct st_rip3_environment value;CHECK(st_rip3_environment_defaults(&value)==0);
	value.base_math=64;value.coordinate_size=5;value.rgb_bits=8;value.world_width=1280;value.world_height=960;
	CHECK(st_rip3_environments_define(f.host.environments,5,&value)==0);
	CHECK(process(&f,"2E05",-1)==0);
	unsigned current;CHECK(st_rip3_environments_current(f.host.environments,&current)==0 && current==5);
	CHECK(process(&f,"2E0Z",-1)==0); /* Fixed two base-64 digits, not coordinate width. */
	CHECK(st_rip3_environments_get(f.host.environments,35,&value)==0 && value.coordinate_size==2 && value.base_math==36);
	const char *invalid[]={"2E","2E0","2E000","2E10","2E?0"};
	for (size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) CHECK(process(&f,invalid[i],ST_RIP3_COMMAND_INVALID)==0);
	CHECK(st_rip3_environments_current(f.host.environments,&current)==0 && current==35);
	const char bytes[]="!|2E?0|2E05|2E00\r\n";
	const struct st_rip3_stream_sink sink={.context=&f,.command=command};
	for (size_t split=0;split<sizeof(bytes);split++) {
		CHECK(st_rip3_stream_create(&stream,&sink)==0);
		unsigned errors=f.errors;
		st_rip3_stream_process(stream,(const uint8_t *)bytes,split);
		st_rip3_stream_process(stream,(const uint8_t *)bytes+split,sizeof(bytes)-1-split);
		CHECK(f.errors==errors+1 && st_rip3_environments_current(f.host.environments,&current)==0 && current==0);
		st_rip3_stream_destroy(stream);
	}
	destroy(&f);return 0;
}

int
main(void)
{
	CHECK(environment_protocol()==0);
	CHECK(adopted_examples() == 0);
	CHECK(matrix(ST_RIP3_CANVAS_640_480) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_800_600) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_1024_768) == 0);
	CHECK(higher_flags() == 0);
	CHECK(flags() == 0 && validation() == 0);
	CHECK(scene(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	return 0;
}
