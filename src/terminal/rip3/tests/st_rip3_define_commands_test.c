/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_clipboard.h"
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

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr,"check failed at line %d: %s\n",__LINE__,#expression); \
		return 1; \
	} \
} while (0)

extern unsigned st_rip3_define_test_fail_calloc, st_rip3_define_test_allocations;

struct fixture {
	struct st_rip3_command_host host;
	struct st_rip3_clipboard_selection clipboard;
	struct st_rip3_environment environment;
	uint64_t budget;
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
process(struct fixture *f, const char *bytes, int expected)
{
	unsigned before = f->errors;
	command(f,(const uint8_t *)bytes,strlen(bytes));
	CHECK(f->errors-before == (expected < 0 ? 0u : 1u));
	if (expected >= 0) CHECK((int)f->kind == expected);
	return 0;
}

/* Independent wire construction; expectations below use literal DEVICE
 * geometry, never a projection or raster result from the emulator. */
static void
field(char *bytes, uint64_t value, unsigned width, unsigned radix)
{
	static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#&";
	while (width != 0) { bytes[--width] = digits[value%radix]; value /= radix; }
}

static void
definition(char *bytes, const struct fixture *f, unsigned number,
    uint64_t x0, uint64_t y0, uint64_t x1, uint64_t y1, unsigned flags)
{
	const uint64_t values[] = {x0,y0,x1,y1};
	unsigned width = f->environment.coordinate_size, radix = f->environment.base_math;
	memcpy(bytes,"2P",2); field(bytes+2,number,1,radix);
	for (unsigned i = 0; i < 4; i++) field(bytes+3+i*width,values[i],width,radix);
	field(bytes+3+4*width,flags,4,radix);
	memcpy(bytes+7+4*width,"0000",5);
}

static void
copy_to_screen(char *bytes, const struct fixture *f)
{
	const unsigned values[] = {10,10,16,14};
	unsigned width = f->environment.coordinate_size, radix = f->environment.base_math;
	memcpy(bytes,"2C1",3); memset(bytes+3,'0',4*width);
	bytes[3+4*width] = '0';
	for (unsigned i = 0; i < 4; i++) field(bytes+4+(4+i)*width,values[i],width,radix);
	bytes[4+8*width] = '0'; bytes[5+8*width] = 0;
}

static int
create(struct fixture *f, enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	*f = (struct fixture){.budget=UINT64_C(8388608)};
	f->host.clipboard = &f->clipboard; f->host.offscreen_pixel_budget = &f->budget;
	CHECK(st_rip3_ports_create(&f->host.ports,canvas) == 0);
	CHECK(st_rip3_environments_create(&f->host.environments,canvas) == 0);
	CHECK(st_rip3_environments_get(f->host.environments,0,&f->environment) == 0);
	f->environment.world_width = width; f->environment.world_height = height;
	CHECK(st_rip3_environments_set(f->host.environments,0,&f->environment) == 0);
	return 0;
}

static void
destroy(struct fixture *f)
{
	st_rip3_ports_destroy(f->host.ports); st_rip3_environments_destroy(f->host.environments);
}

static bool
rect(const struct st_rip3_rect *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	return r->x0 == x0 && r->y0 == y0 && r->x1 == x1 && r->y1 == y1;
}

static int
matrix(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct fixture f;
	struct st_rip3_port_info info;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	const struct st_rip3_rect whole = {0,0,2047,2047}, old = {30,30,40,40}, image = {1,1,3,3};
	char bytes[64];
	unsigned current;
	uint8_t pixel;
	CHECK(create(&f,canvas,width,height) == 0);
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_define(f.host.ports,2,&old,false) == 0);
	for (unsigned radix = 36; radix <= 64; radix += 28) for (unsigned size = 2; size <= 5; size++) {
		f.environment.base_math = radix; f.environment.coordinate_size = size;
		CHECK(st_rip3_environments_set(f.host.environments,0,&f.environment) == 0);
		for (unsigned flags = 0; flags < 16; flags++) {
			CHECK(st_rip3_ports_protect(f.host.ports,1,false) == 0);
			CHECK(st_rip3_ports_define(f.host.ports,1,&old,false) == 0);
			CHECK(st_rip3_ports_switch(f.host.ports,1) == 0);
			CHECK(st_rip3_ports_activate(f.host.ports,false) == 0);
			CHECK(st_rip3_ports_image_style(f.host.ports,1,&image,63) == 0);
			CHECK(st_rip3_ports_switch(f.host.ports,0) == 0);
			CHECK(st_rip3_ports_fill(f.host.ports,&whole,0,ST_RIP3_COPY) == 0);
			CHECK(st_rip3_ports_switch(f.host.ports,2) == 0);
			f.clipboard = (struct st_rip3_clipboard_selection){true,2};
			definition(bytes,&f,1,2,1,6,4,flags);
			CHECK(process(&f,bytes,-1) == 0);
			CHECK(f.clipboard.set && f.clipboard.port == 2);
			CHECK(st_rip3_ports_current(f.host.ports,&current) == 0 && current == (flags&2 ? 1u : 2u));
			CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0);
			CHECK(info.defined && info.offscreen == ((flags&1) != 0)
			    && info.protected == ((flags&8) != 0) && info.viewport_active == ((flags&4) == 0));
			CHECK(info.image.automatic && info.image.enabled && info.image.flags == 0
			    && rect(&info.image.area,-1,-1,-1,-1));
			CHECK(flags&1 ? rect(&info.bounds,0,0,5,3) : rect(&info.bounds,2,1,5,3));
			CHECK(flags&1 ? rect(&info.viewport,0,0,5,3) : rect(&info.viewport,0,0,3,2));
			CHECK(st_rip3_ports_switch(f.host.ports,1) == 0);
			CHECK(st_rip3_ports_fill(f.host.ports,&whole,4,ST_RIP3_COPY) == 0);
			CHECK(st_rip3_ports_read(f.host.ports,1,0,0,&pixel) == 0 && pixel == (flags&4 ? 0 : 4));
			/* One coherent full-device capture: screen definitions paint exactly
			 * [2,6)x[1,4); offscreen definitions stay invisible until copied. */
			CHECK(st_rip3_ports_snapshot(f.host.ports,&palette,&capture) == 0);
			for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
				bool red = !(flags&5) && x >= 2 && x < 6 && y >= 1 && y < 4;
				CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | (red ? UINT32_C(0xaa0000) : 0)));
			}
			st_pixel_buffer_destroy(capture); capture = NULL;
			if (flags&1) {
				copy_to_screen(bytes,&f);
				CHECK(process(&f,bytes,-1) == 0);
				CHECK(st_rip3_ports_snapshot(f.host.ports,&palette,&capture) == 0);
				for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
					bool red = !(flags&4) && x >= 10 && x < 16 && y >= 10 && y < 14;
					CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | (red ? UINT32_C(0xaa0000) : 0)));
				}
				st_pixel_buffer_destroy(capture); capture = NULL;
			}
		}
	}
	destroy(&f); return 0;
}

static int
extents_and_fields(void)
{
	struct fixture f;
	struct st_rip3_port_info info;
	char bytes[40];
	CHECK(create(&f,ST_RIP3_CANVAS_640_480,1280,960) == 0);
	CHECK(process(&f,"2P10000ZKQO00030000",-1) == 0);
	CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && info.offscreen
	    && rect(&info.bounds,0,0,639,479) && rect(&info.viewport,0,0,639,479));
	CHECK(process(&f,"2P21S1C1S1C00010000",-1) == 0); /* (64,48) ignored; far gives 32x24. */
	CHECK(st_rip3_ports_info(f.host.ports,2,&info) == 0 && rect(&info.bounds,0,0,31,23));
	f.environment.coordinate_size = 3;
	CHECK(st_rip3_environments_set(f.host.environments,0,&f.environment) == 0);
	definition(bytes,&f,3,0,0,2560,1920,1);
	CHECK(process(&f,bytes,-1) == 0);
	CHECK(st_rip3_ports_info(f.host.ports,3,&info) == 0 && rect(&info.bounds,0,0,1279,959));
	definition(bytes,&f,4,1278,958,2560,1920,0);
	CHECK(process(&f,bytes,-1) == 0);
	CHECK(st_rip3_ports_info(f.host.ports,4,&info) == 0 && rect(&info.bounds,639,479,639,479));
	/* Odd projection truncates each boundary once: [1,4)x[1,3). */
	definition(bytes,&f,5,3,3,9,7,0);
	CHECK(process(&f,bytes,-1) == 0);
	CHECK(st_rip3_ports_info(f.host.ports,5,&info) == 0 && rect(&info.bounds,1,1,3,2));
	/* Full flags permit each reserved-prefix length. Short flags permit no
	 * reserved digits; both forms must establish the same six-by-four port. */
	f.environment.world_width = 640; f.environment.world_height = 480;
	CHECK(st_rip3_environments_set(f.host.environments,0,&f.environment) == 0);
	for (unsigned digits = 1; digits <= 8; digits++) {
		definition(bytes,&f,6,2,1,6,4,1);
		if (digits < 4) { field(bytes+15,1,digits,36); bytes[15+digits] = 0; }
		else bytes[15+digits] = 0;
		CHECK(process(&f,bytes,-1) == 0);
		CHECK(st_rip3_ports_info(f.host.ports,6,&info) == 0 && rect(&info.bounds,0,0,5,3));
	}
	destroy(&f); return 0;
}

static int
ranges(void)
{
	static const struct { unsigned x0, y0, x1, y1, flags; int error; } cases[] = {
		{0,0,0,4,1,ST_RIP3_COMMAND_INVALID}, {0,0,6,0,1,ST_RIP3_COMMAND_INVALID},
		{2,1,2,4,0,ST_RIP3_COMMAND_INVALID}, {6,1,2,4,0,ST_RIP3_COMMAND_INVALID},
		{2,4,6,1,0,ST_RIP3_COMMAND_INVALID}, {2,1,6,4,16,ST_RIP3_COMMAND_UNSUPPORTED},
		{640,0,641,1,0,ST_RIP3_COMMAND_UNSUPPORTED},
	};
	struct fixture f;
	struct st_rip3_port_info info;
	char bytes[40];
	unsigned current, allocations;
	CHECK(create(&f,ST_RIP3_CANVAS_640_480,640,480) == 0);
	f.environment.coordinate_size = 5; f.environment.base_math = 64;
	CHECK(st_rip3_environments_set(f.host.environments,0,&f.environment) == 0);
	for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
		definition(bytes,&f,1,cases[i].x0,cases[i].y0,cases[i].x1,cases[i].y1,cases[i].flags);
		allocations = st_rip3_define_test_allocations;
		CHECK(process(&f,bytes,cases[i].error) == 0);
		CHECK(st_rip3_define_test_allocations == allocations);
		CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && !info.defined);
		CHECK(st_rip3_ports_current(f.host.ports,&current) == 0 && current == 0);
		CHECK(!f.clipboard.set);
	}
	definition(bytes,&f,0,0,0,6,4,1);
	CHECK(process(&f,bytes,ST_RIP3_COMMAND_INVALID) == 0);
	definition(bytes,&f,36,0,0,6,4,1);
	CHECK(process(&f,bytes,ST_RIP3_COMMAND_INVALID) == 0);
	definition(bytes,&f,1,0,0,6,4,1); bytes[strlen(bytes)-1] = '1';
	CHECK(process(&f,bytes,ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	/* Ignored near coordinates cannot shrink the allocation, overflow a
	 * projection, or change an initially unset clipboard pointer. */
	definition(bytes,&f,1,1073741823,1073741823,6,4,1);
	CHECK(process(&f,bytes,-1) == 0);
	CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && rect(&info.bounds,0,0,5,3));
	CHECK(!f.clipboard.set);
	definition(bytes,&f,1,0,0,65536,1,1);
	CHECK(process(&f,bytes,-1) == 0);
	CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && rect(&info.bounds,0,0,65535,0));
	f.environment.world_width = f.environment.world_height = 1;
	CHECK(st_rip3_environments_set(f.host.environments,0,&f.environment) == 0);
	definition(bytes,&f,2,0,0,1073741823,1073741823,1);
	allocations = st_rip3_define_test_allocations;
	CHECK(process(&f,bytes,ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(st_rip3_define_test_allocations == allocations);
	CHECK(st_rip3_ports_info(f.host.ports,2,&info) == 0 && !info.defined);
	/* Distinct ordered WORLD boundaries may still produce zero dimensions. */
	f.environment.world_width = 1280; f.environment.world_height = 960;
	CHECK(st_rip3_environments_set(f.host.environments,0,&f.environment) == 0);
	definition(bytes,&f,2,0,0,1,1,1);
	CHECK(process(&f,bytes,ST_RIP3_COMMAND_INVALID) == 0);
	destroy(&f); return 0;
}

static int
failures(void)
{
	struct fixture f;
	struct st_rip3_port_info info;
	struct st_rip3_stream *stream = NULL;
	const struct st_rip3_stream_sink sink = {.context=&f,.command=command};
	const struct st_rip3_rect old = {0,0,3,2}, view = {1,1,2,2}, image = {2,2,3,3};
	char bytes[40];
	unsigned current, allocations;
	uint8_t pixel;
	CHECK(create(&f,ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(st_rip3_ports_define(f.host.ports,1,&old,true) == 0);
	CHECK(st_rip3_ports_define(f.host.ports,2,&old,false) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,1) == 0);
	CHECK(st_rip3_ports_fill(f.host.ports,&old,7,ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_viewport(f.host.ports,&view) == 0);
	CHECK(st_rip3_ports_image_style(f.host.ports,1,&image,63) == 0);
	CHECK(st_rip3_ports_activate(f.host.ports,false) == 0);
	f.clipboard = (struct st_rip3_clipboard_selection){true,1};
	CHECK(st_rip3_ports_protect(f.host.ports,1,true) == 0);
	for (unsigned flags = 0; flags < 16; flags++) {
		CHECK(st_rip3_ports_switch(f.host.ports,2) == 0);
		definition(bytes,&f,1,2,1,6,4,flags);
		f.budget = 0; allocations = st_rip3_define_test_allocations;
		CHECK(process(&f,bytes,ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
		CHECK(st_rip3_define_test_allocations == allocations);
		CHECK(st_rip3_ports_current(f.host.ports,&current) == 0 && current == 0);
		CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && info.protected && !info.viewport_active
		    && rect(&info.bounds,0,0,3,2) && rect(&info.viewport,1,1,2,2)
		    && info.image.flags == 63 && rect(&info.image.area,2,2,3,3));
		CHECK(st_rip3_ports_read(f.host.ports,1,3,2,&pixel) == 0 && pixel == 7);
		CHECK(f.clipboard.set && f.clipboard.port == 1);
	}
	CHECK(st_rip3_ports_protect(f.host.ports,1,false) == 0);
	/* Client policy: admission counts the final table, excluding the replaced
	 * bitmap. A failed admission/allocation retains old state and selection. */
	CHECK(st_rip3_ports_switch(f.host.ports,1) == 0);
	definition(bytes,&f,1,2,1,6,4,15); f.budget = 23;
	CHECK(process(&f,bytes,ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0);
	for (unsigned stage = 1; stage <= 2; stage++) {
		f.budget = 24; st_rip3_define_test_fail_calloc = stage;
		CHECK(process(&f,bytes,ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0);
		CHECK(st_rip3_define_test_fail_calloc == 0);
		CHECK(st_rip3_ports_current(f.host.ports,&current) == 0 && current == 1);
		CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && !info.protected && !info.viewport_active
		    && rect(&info.bounds,0,0,3,2) && info.image.flags == 63);
		CHECK(st_rip3_ports_read(f.host.ports,1,3,2,&pixel) == 0 && pixel == 7);
		CHECK(f.clipboard.set && f.clipboard.port == 1);
	}
	CHECK(process(&f,bytes,-1) == 0);
	CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && info.protected && !info.viewport_active
	    && rect(&info.bounds,0,0,5,3));
	CHECK(st_rip3_ports_read(f.host.ports,1,5,3,&pixel) == 0 && pixel == 0);
	definition(bytes,&f,3,0,0,1,1,1); /* Existing 24 pixels exhaust budget. */
	CHECK(process(&f,bytes,ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0);
	f.budget = 25; CHECK(process(&f,bytes,-1) == 0);
	/* Missing client policy declines allocations internally. A byte-split
	 * bad command is discarded and the next definition still executes. */
	f.host.offscreen_pixel_budget = NULL;
	definition(bytes,&f,4,0,0,1,1,1);
	CHECK(process(&f,bytes,ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(st_rip3_stream_create(&stream,&sink) == 0);
	const char recovery[] = "!|2P4000000001|2P4020106042\r";
	unsigned before = f.errors;
	for (size_t i = 0; i < sizeof(recovery)-1; i++) st_rip3_stream_process(stream,(const uint8_t *)recovery+i,1);
	CHECK(f.errors == before+1);
	CHECK(st_rip3_ports_current(f.host.ports,&current) == 0 && current == 4);
	CHECK(st_rip3_ports_info(f.host.ports,4,&info) == 0 && !info.offscreen && rect(&info.bounds,2,1,5,3));
	st_rip3_stream_destroy(stream); destroy(&f); return 0;
}

int
main(void)
{
	CHECK(matrix(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	CHECK(extents_and_fields() == 0);
	CHECK(ranges() == 0);
	CHECK(failures() == 0);
	return 0;
}
