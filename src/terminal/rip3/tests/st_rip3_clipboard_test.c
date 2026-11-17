/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_clipboard.h"
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_number.h"
#include "st_rip3_ports.h"
#include "st_rip3_stream.h"
#include "st_rip3_bitmap.h"
#include "st_pixels.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern bool st_rip3_clipboard_test_fail_allocate, st_rip3_clipboard_test_fail_copy;
extern bool st_rip3_clipboard_test_observed_selection;
extern struct st_rip3_clipboard_selection *st_rip3_clipboard_test_selection;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr,"check failed at line %d: %s\n",__LINE__,#expression); \
		return 1; \
	} \
} while (0)

struct fixture {
	struct st_rip3_command_host host;
	struct st_rip3_clipboard_selection selection;
	uint64_t budget;
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
	unsigned before = f->errors; command(f,(const uint8_t *)body,strlen(body));
	CHECK(f->errors == before+(error < 0 ? 0u : 1u));
	if (error >= 0) CHECK((int)f->kind == error);
	return 0;
}

static int
create(struct fixture *f, enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	CHECK(st_rip3_ports_create(&f->host.ports,canvas) == 0 && st_rip3_environments_create(&f->host.environments,canvas) == 0);
	CHECK(st_rip3_ports_erase(f->host.ports,85) == 0);
	struct st_rip3_environment env;
	CHECK(st_rip3_environments_get(f->host.environments,0,&env) == 0);
	env.world_width = width; env.world_height = height; env.port = 35; env.palette = 34;
	CHECK(st_rip3_environments_define(f->host.environments,2,&env) == 0 && st_rip3_environments_select(f->host.environments,2) == 0);
	f->budget = UINT64_C(1000000); f->host.offscreen_pixel_budget = &f->budget; f->host.clipboard = &f->selection;
	return 0;
}

static void
destroy(struct fixture *f)
{
	st_rip3_ports_destroy(f->host.ports); st_rip3_environments_destroy(f->host.environments);
	st_rip3_clipboard_test_selection = NULL;
}

static int
pattern(struct fixture *f, unsigned port, unsigned bias)
{
	CHECK(st_rip3_ports_switch(f->host.ports,port) == 0);
	for (int32_t y = 1; y <= 3; y++) for (int32_t x = 1; x <= 3; x++)
		CHECK(st_rip3_ports_plot(f->host.ports,x,y,(uint8_t)(bias+3*(y-1)+x),ST_RIP3_COPY) == 0);
	return 0;
}

static int
bitmap(struct fixture *f, unsigned port, uint32_t width, uint32_t height, unsigned bias, bool clear)
{
	struct st_rip3_palette palette; struct st_rip3_bitmap *capture = NULL; struct st_rip3_port_info info;
	CHECK(st_rip3_ports_info(f->host.ports,port,&info) == 0 && info.defined && info.offscreen && !info.protected && info.viewport_active);
	CHECK(info.bounds.x0 == 0 && info.bounds.y0 == 0 && info.bounds.x1 == (int32_t)width-1 && info.bounds.y1 == (int32_t)height-1);
	CHECK(st_rip3_palette_defaults(&palette) == 0 && st_rip3_bitmap_capture(&capture,f->host.ports,port,NULL,&palette) == 0);
	CHECK(capture->pixels->width == width && capture->pixels->height == height);
	/* 3x3 -> 4x4 nearest-center policy; the driver evidence proves stretch
	 * geometry, not historical Windows interpolation. These are authored
	 * geometric/index expectations for this implementation. */
	const uint8_t expected[16] = {1,2,2,3,4,5,5,6,4,5,5,6,7,8,8,9};
	for (size_t i = 0; i < (size_t)width*height; i++) CHECK(capture->pixels->primary[i] == (clear ? 0u : bias+expected[i]));
	for (unsigned i = 0; i < 256; i++) CHECK(capture->palette[i] == palette.rgb[i]);
	st_rip3_bitmap_destroy(capture); return 0;
}

static int
current(struct fixture *f, unsigned number)
{
	unsigned actual; CHECK(st_rip3_ports_current(f->host.ports,&actual) == 0 && actual == number); return 0;
}

static int
selection(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct fixture f = {0}; const struct st_rip3_rect old = {0,0,4,3};
	const struct st_rip3_clipboard_area area = {1,1,4,4};
	CHECK(create(&f,canvas,width,height) == 0 && pattern(&f,0,0) == 0);
	CHECK(st_rip3_ports_define(f.host.ports,7,&old,true) == 0 && pattern(&f,7,40) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,0) == 0);
	st_rip3_clipboard_test_selection = &f.selection;
	CHECK(st_rip3_clipboard_capture(f.host.ports,&f.selection,&area,f.budget) == 0);
	CHECK(f.selection.set && f.selection.port == 1 && current(&f,0) == 0 && st_rip3_clipboard_test_observed_selection);
	CHECK(bitmap(&f,1,4,4,0,false) == 0);
	st_rip3_clipboard_test_fail_copy = true;
	CHECK(process(&f,"1C010103030",-1) == 0 && !st_rip3_clipboard_test_fail_copy && bitmap(&f,1,4,4,0,true) == 0);
	uint8_t sample; CHECK(st_rip3_ports_read(f.host.ports,7,1,1,&sample) == 0 && sample == 41);
	/* Without a pointer, an existing current offscreen port is a source,
	 * never preferred to the first unused slot or treated as a no-op. */
	f.selection = (struct st_rip3_clipboard_selection){0};
	CHECK(st_rip3_ports_delete(f.host.ports,1) == 0 && st_rip3_ports_switch(f.host.ports,7) == 0);
	CHECK(st_rip3_clipboard_capture(f.host.ports,&f.selection,&area,f.budget) == 0
	    && f.selection.port == 1 && current(&f,7) == 0 && bitmap(&f,1,4,4,40,false) == 0);
	/* Assigned target is reused even with a lower unused slot. */
	CHECK(st_rip3_ports_delete(f.host.ports,1) == 0 && st_rip3_ports_switch(f.host.ports,0) == 0);
	f.selection = (struct st_rip3_clipboard_selection){true,7};
	CHECK(st_rip3_clipboard_capture(f.host.ports,&f.selection,&area,16) == 0 && f.selection.port == 7 && bitmap(&f,7,4,4,0,false) == 0);
	struct st_rip3_port_info info; CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && !info.defined);
	/* Protected assigned target rejects before allocating/deleting, including
	 * when it is itself the current source. Slot zero is also undeletable. */
	CHECK(st_rip3_ports_protect(f.host.ports,7,true) == 0);
	CHECK(st_rip3_clipboard_capture(f.host.ports,&f.selection,&area,0) == IO_ERROR_PERMISSION_DENIED && current(&f,0) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,7) == 0);
	CHECK(st_rip3_clipboard_capture(f.host.ports,&f.selection,&area,0) == IO_ERROR_PERMISSION_DENIED
	    && f.selection.set && f.selection.port == 7 && current(&f,7) == 0);
	CHECK(st_rip3_ports_read(f.host.ports,7,0,0,&sample) == 0 && sample == 1);
	f.selection = (struct st_rip3_clipboard_selection){true,0};
	CHECK(st_rip3_clipboard_capture(f.host.ports,&f.selection,&area,f.budget) == IO_ERROR_PERMISSION_DENIED && current(&f,7) == 0 && f.selection.port == 0);
	/* Same-source replacement deliberately loses the old source and copies
	 * from screen zero after deletion changes the live current selection. */
	f.selection = (struct st_rip3_clipboard_selection){true,7};
	CHECK(st_rip3_ports_protect(f.host.ports,7,false) == 0 && pattern(&f,7,40) == 0);
	CHECK(st_rip3_clipboard_capture(f.host.ports,&f.selection,&area,f.budget) == 0 && current(&f,0) == 0 && bitmap(&f,7,4,4,0,false) == 0);
	destroy(&f); return 0;
}

static int
failures(void)
{
	const struct st_rip3_rect old = {0,0,4,3}; const struct st_rip3_clipboard_area area = {1,1,4,4};
	for (unsigned assigned = 0; assigned < 2; assigned++) for (unsigned same = 0; same < 2; same++)
		for (unsigned budget_failure = 0; budget_failure < 2; budget_failure++) {
			struct fixture f = {0}; CHECK(create(&f,ST_RIP3_CANVAS_640_480,640,480) == 0);
			CHECK(st_rip3_ports_define(f.host.ports,7,&old,true) == 0 && pattern(&f,7,40) == 0);
			CHECK(st_rip3_ports_switch(f.host.ports,same ? 7 : 0) == 0);
			f.selection = (struct st_rip3_clipboard_selection){assigned != 0,7};
			st_rip3_clipboard_test_fail_allocate = !budget_failure;
			CHECK(st_rip3_clipboard_capture(f.host.ports,&f.selection,&area,budget_failure ? assigned ? 15 : 35 : f.budget) == IO_ERROR_OUT_OF_MEMORY);
			CHECK(!st_rip3_clipboard_test_fail_allocate && !f.selection.set && f.selection.port == 0 && current(&f,assigned && same ? 0 : same ? 7 : 0) == 0);
			struct st_rip3_port_info info; CHECK(st_rip3_ports_info(f.host.ports,assigned ? 7 : 1,&info) == 0 && !info.defined);
			if (!assigned) { uint8_t sample; CHECK(st_rip3_ports_read(f.host.ports,7,1,1,&sample) == 0 && sample == 41); }
			destroy(&f);
		}
	/* Full tables fail without allocation/deletion, but an assigned target
	 * still works. A protected offscreen entry does not bias free-slot scan. */
	struct fixture f = {0}; CHECK(create(&f,ST_RIP3_CANVAS_640_480,640,480) == 0);
	for (unsigned i = 1; i < 36; i++) CHECK(st_rip3_ports_define(f.host.ports,i,&old,true) == 0);
	CHECK(st_rip3_ports_protect(f.host.ports,7,true) == 0);
	CHECK(st_rip3_clipboard_capture(f.host.ports,&f.selection,&area,f.budget) == IO_ERROR_NO_SPACE && !f.selection.set && current(&f,0) == 0);
	for (unsigned i = 1; i < 36; i++) { struct st_rip3_port_info info; CHECK(st_rip3_ports_info(f.host.ports,i,&info) == 0 && info.defined); }
	f.selection = (struct st_rip3_clipboard_selection){true,9};
	CHECK(st_rip3_clipboard_capture(f.host.ports,&f.selection,&area,f.budget) == 0 && f.selection.port == 9);
	CHECK(st_rip3_ports_protect(f.host.ports,7,false) == 0); f.selection.port = 7;
	CHECK(st_rip3_clipboard_capture(f.host.ports,&f.selection,&area,f.budget) == 0 && f.selection.port == 7);
	CHECK(st_rip3_ports_protect(f.host.ports,7,true) == 0);
	CHECK(st_rip3_ports_delete(f.host.ports,1) == 0); f.selection.set = false;
	CHECK(st_rip3_clipboard_capture(f.host.ports,&f.selection,&area,f.budget) == 0 && f.selection.port == 1);
	/* Downstream copy failure and completely outside source geometry retain
	 * the new clear allocation and report success, destroying the old target. */
	st_rip3_clipboard_test_fail_copy = true;
	CHECK(st_rip3_clipboard_capture(f.host.ports,&f.selection,&area,f.budget) == 0
	    && !st_rip3_clipboard_test_fail_copy && bitmap(&f,1,4,4,0,true) == 0);
	const struct st_rip3_clipboard_area outside = {10000,10000,10003,10003};
	CHECK(st_rip3_clipboard_capture(f.host.ports,&f.selection,&outside,f.budget) == 0 && bitmap(&f,1,4,4,0,true) == 0);
	/* Low-WORD arithmetic is observable even for huge outside rectangles. */
	const struct st_rip3_clipboard_area wrapped = {10000,10000,75537,75537};
	CHECK(st_rip3_clipboard_capture(f.host.ports,&f.selection,&wrapped,f.budget) == 0 && bitmap(&f,1,2,2,0,true) == 0);
	const struct st_rip3_clipboard_area zero = {0,0,65535,65535};
	CHECK(st_rip3_clipboard_capture(f.host.ports,&f.selection,&zero,f.budget) == IO_ERROR_NOT_SUPPORTED && bitmap(&f,1,2,2,0,true) == 0);
	destroy(&f); return 0;
}

static int
wire(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct fixture f = {0}; CHECK(create(&f,canvas,width,height) == 0 && pattern(&f,0,0) == 0);
	struct st_rip3_stream *stream = NULL; const struct st_rip3_stream_sink sink = {.context=&f,.command=command};
	CHECK(st_rip3_stream_create(&stream,&sink) == 0);
	const char input[] = "!|1C010103030|1C03030101\r";
	for (size_t i = 0; i < sizeof(input)-1; i++) st_rip3_stream_process(stream,(const uint8_t *)input+i,1);
	CHECK(f.errors == 0 && f.selection.set && f.selection.port == 1 && bitmap(&f,1,4,4,0,false) == 0);
	CHECK(process(&f,"1C0101033",-1) == 0 && bitmap(&f,1,4,4,0,false) == 0); /* Short final coordinate. */
	CHECK(process(&f,"1C010103031",ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&f,"1C0101030300",ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&f,"1C010?03030",ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(bitmap(&f,1,4,4,0,false) == 0);
	f.budget = 0;
	CHECK(process(&f,"1C010103030",ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !f.selection.set);
	f.budget = UINT64_C(1000000);
	CHECK(process(&f,"1C010103030",-1) == 0 && bitmap(&f,1,4,4,0,false) == 0);
	struct st_rip3_palette palette; struct st_rip3_bitmap *capture = NULL;
	CHECK(st_rip3_palette_defaults(&palette) == 0 && st_rip3_bitmap_capture(&capture,f.host.ports,0,NULL,&palette) == 0);
	CHECK(capture->pixels->width == width && capture->pixels->height == height);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		unsigned expected = x >= 1 && x <= 3 && y >= 1 && y <= 3 ? 3*(y-1)+x : 85;
		CHECK(capture->pixels->primary[(size_t)y*width+x] == expected);
	}
	for (unsigned i = 0; i < 256; i++) CHECK(capture->palette[i] == palette.rgb[i]);
	st_rip3_bitmap_destroy(capture);
	CHECK(st_rip3_ports_activate(f.host.ports,false) == 0);
	f.host.clipboard = NULL; f.host.offscreen_pixel_budget = NULL;
	CHECK(process(&f,"1C010103030",-1) == 0);
	CHECK(st_rip3_ports_activate(f.host.ports,true) == 0);
	CHECK(process(&f,"1C010103030",ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	st_rip3_stream_destroy(stream); destroy(&f); return 0;
}

static int
numeric(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	for (unsigned radix = 36; radix <= 64; radix += 28) for (unsigned field_width = 2; field_width <= 5; field_width++) {
		struct fixture f = {0}; struct st_rip3_environment env;
		const struct st_rip3_rect bounds = {0,0,7,5}, view = {2,1,5,4};
		CHECK(create(&f,canvas,width,height) == 0);
		CHECK(st_rip3_ports_define(f.host.ports,7,&bounds,true) == 0 && st_rip3_ports_switch(f.host.ports,7) == 0
		    && st_rip3_ports_viewport(f.host.ports,&view) == 0 && pattern(&f,7,40) == 0 && st_rip3_ports_protect(f.host.ports,7,true) == 0);
		CHECK(st_rip3_environments_get(f.host.environments,2,&env) == 0);
		env.world_width = 20*width; env.world_height = 20*height; env.base_math = radix; env.coordinate_size = field_width;
		CHECK(st_rip3_environments_define(f.host.environments,2,&env) == 0);
		char body[24] = {'1','C'};
		const uint64_t fields[] = {20,20,60,60};
		for (unsigned i = 0; i < 4; i++) CHECK(st_rip3_number_encode(body+2+i*field_width,field_width,fields[i],field_width,radix) == 0);
		body[2+4*field_width] = '0'; body[3+4*field_width] = 0;
		CHECK(process(&f,body,-1) == 0 && current(&f,7) == 0 && bitmap(&f,1,4,4,40,false) == 0);
		CHECK(st_rip3_environments_get(f.host.environments,2,&env) == 0 && env.port == 35 && env.palette == 34);
		destroy(&f);
	}
	return 0;
}

static int
clipping(void)
{
	struct fixture f = {0}; const struct st_rip3_clipboard_area area = {-1,-1,2,2};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480,640,480) == 0 && pattern(&f,0,0) == 0);
	CHECK(st_rip3_clipboard_capture(f.host.ports,&f.selection,&area,f.budget) == 0);
	struct st_rip3_palette palette; struct st_rip3_bitmap *capture = NULL;
	CHECK(st_rip3_palette_defaults(&palette) == 0 && st_rip3_bitmap_capture(&capture,f.host.ports,1,NULL,&palette) == 0);
	const uint8_t expected[16] = {85,85,85,85,85,85,85,85,85,85,1,1,85,85,1,1};
	CHECK(capture->pixels->width == 4 && capture->pixels->height == 4);
	for (unsigned i = 0; i < 16; i++) CHECK(capture->pixels->primary[i] == expected[i]);
	st_rip3_bitmap_destroy(capture); destroy(&f); return 0;
}

int
main(void)
{
	CHECK(selection(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(selection(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(selection(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	CHECK(failures() == 0);
	CHECK(clipping() == 0);
	CHECK(wire(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(wire(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(wire(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	CHECK(numeric(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(numeric(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(numeric(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	return 0;
}
