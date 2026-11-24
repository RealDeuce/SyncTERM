/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "image_area_fixture.h"
#include "st_rip3_commands.h"
#include "st_rip3_bitmap.h"
#include "st_rip3_environments.h"
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

extern bool st_rip3_bitmap_file_test_fail_allocate, st_rip3_bitmap_file_test_fail_decode;
extern bool st_rip3_bitmap_stamp_test_fail_allocate;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr,"check failed at line %d: %s\n",__LINE__,#expression); \
		return 1; \
	} \
} while (0)

enum pointer_state { ABSENT, UNDEFINED, OFFSCREEN, OFFSCREEN_PROTECTED,
	ONSCREEN, ONSCREEN_PROTECTED, CURRENT, CURRENT_PROTECTED, UNBOUND };

struct fixture {
	struct st_rip3_command_host host;
	struct st_rip3_resource_paths paths;
	struct st_rip3_bitmap_error_sink feedback;
	struct st_rip3_clipboard_selection selection, original_selection;
	struct st_rip3_port_info original[36];
	struct st_rip3_environment environment;
	bool offscreen, placeholder_fails, bad_callback;
	unsigned errors, placeholders;
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
	unsigned before = f->errors;
	command(f,(const uint8_t *)body,strlen(body));
	CHECK(f->errors == before+(error < 0 ? 0u : 1u));
	if (error >= 0) CHECK((int)f->kind == error);
	return 0;
}

static void
show(void *context, const char *name, unsigned port, const struct st_rip3_bitmap_destination *area,
    bool natural_size)
{
	struct fixture *f = context; unsigned current;
	f->placeholders++;
	if (name == NULL || name[0] == 0 || area == NULL || !natural_size || port != 1
	    || st_rip3_ports_current(f->host.ports,&current) != 0 || current != 1
	    || f->selection.set != f->original_selection.set || f->selection.port != f->original_selection.port)
		f->bad_callback = true;
	/* A controlled placeholder proves that failure UI may change current
	 * pixels, including an aliased screen clipboard, without changing state. */
	if (!f->placeholder_fails && st_rip3_ports_plot(f->host.ports,1,1,6,ST_RIP3_COPY) != 0)
		f->bad_callback = true;
}

static bool
same_rect(const struct st_rip3_rect *a, const struct st_rip3_rect *b)
{
	return a->x0 == b->x0 && a->y0 == b->y0 && a->x1 == b->x1 && a->y1 == b->y1;
}

static int
state(struct fixture *f, unsigned radix)
{
	unsigned current; struct st_rip3_environment env;
	CHECK(!f->bad_callback && f->selection.set == f->original_selection.set && f->selection.port == f->original_selection.port);
	CHECK(st_rip3_ports_current(f->host.ports,&current) == 0 && current == 1);
	for (unsigned i = 0; i < 36; i++) {
		struct st_rip3_port_info info; const struct st_rip3_port_info *old = f->original+i;
		CHECK(st_rip3_ports_info(f->host.ports,i,&info) == 0);
		CHECK(info.defined == old->defined && info.offscreen == old->offscreen
		    && info.protected == old->protected && info.viewport_active == old->viewport_active
		    && same_rect(&info.bounds,&old->bounds) && same_rect(&info.viewport,&old->viewport)
		    && info.image.automatic == old->image.automatic && info.image.enabled == old->image.enabled
		    && info.image.flags == old->image.flags && same_rect(&info.image.area,&old->image.area));
	}
	CHECK(st_rip3_environments_current(f->host.environments,&current) == 0 && current == 2
	    && st_rip3_environments_get(f->host.environments,2,&env) == 0);
	CHECK(env.base_math == radix && env.port == 9 && env.palette == 7 && env.graphics_style == 6);
	CHECK(env.world_width == f->environment.world_width && env.world_height == f->environment.world_height
	    && env.coordinate_size == f->environment.coordinate_size && env.rgb_bits == f->environment.rgb_bits
	    && env.mouse_pointer == f->environment.mouse_pointer
	    && env.button_style == f->environment.button_style && env.text_window == f->environment.text_window);
	CHECK(st_rip3_palettes_current(f->host.palettes,&current) == 0 && current == 3);
	struct st_rip3_palette_info palette;
	CHECK(st_rip3_palettes_info(f->host.palettes,3,&palette) == 0 && palette.defined && palette.protected);
	return 0;
}

static int
capture(struct fixture *f, uint32_t width, uint32_t height, bool marker, bool loaded)
{
	struct st_rip3_palette palette; struct st_rip3_bitmap *image = NULL;
	CHECK(st_rip3_palettes_get(f->host.palettes,3,&palette) == 0
	    && st_rip3_bitmap_capture(&image,f->host.ports,0,NULL,&palette) == 0);
	CHECK(image->pixels->width == width && image->pixels->height == height);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		unsigned expected = !f->offscreen && marker && x == 13 && y == 22 ? 6
		    : !f->offscreen && loaded && x == 12 && y == 21 ? 1 : 85;
		if (image->pixels->primary[(size_t)y*width+x] != expected)
			fprintf(stderr,"scene (%u,%u): got %u, expected %u\n",x,y,image->pixels->primary[(size_t)y*width+x],expected);
		CHECK(image->pixels->primary[(size_t)y*width+x] == expected);
	}
	for (unsigned i = 0; i < 256; i++) CHECK(image->palette[i] == (i == 1 ? 0x112233 : i*UINT32_C(0x010101)));
	st_rip3_bitmap_destroy(image); image = NULL;
	if (f->offscreen) {
		CHECK(st_rip3_bitmap_capture(&image,f->host.ports,1,NULL,&palette) == 0);
		CHECK(image->pixels->width == 16 && image->pixels->height == 16);
		for (unsigned y = 0; y < 16; y++) for (unsigned x = 0; x < 16; x++) {
			unsigned expected = marker && x == 1 && y == 1 ? 6 : loaded && x == 0 && y == 0 ? 1 : 85;
			CHECK(image->pixels->primary[y*16+x] == expected);
		}
		st_rip3_bitmap_destroy(image); image = NULL;
	}
	if (f->original[9].defined && f->original[9].offscreen) {
		CHECK(st_rip3_bitmap_capture(&image,f->host.ports,9,NULL,&palette) == 0);
		for (size_t i = 0; i < (size_t)image->pixels->width*image->pixels->height; i++) CHECK(image->pixels->primary[i] == 60);
		st_rip3_bitmap_destroy(image);
	}
	return 0;
}

static int
create(struct fixture *f, enum st_rip3_canvas canvas, uint32_t width, uint32_t height,
    const char *directory, enum pointer_state pointer, bool offscreen, bool placeholder_fails)
{
	const struct st_rip3_rect screen = {10,20,29,39}, bitmap = {0,0,19,19}, view = {2,1,17,16};
	const struct st_rip3_rect target_screen = {13,22,20,29}, target_bitmap = {0,0,4,3}, area = {1,1,2,2};
	CHECK(st_rip3_environments_create(&f->host.environments,canvas) == 0 && st_rip3_ports_create(&f->host.ports,canvas) == 0
	    && st_rip3_palettes_create(&f->host.palettes) == 0);
	CHECK(st_rip3_ports_erase(f->host.ports,85) == 0 && st_rip3_ports_define(f->host.ports,1,offscreen ? &bitmap : &screen,offscreen) == 0
	    && st_rip3_ports_switch(f->host.ports,1) == 0 && st_rip3_ports_erase(f->host.ports,85) == 0
	    && st_rip3_ports_viewport(f->host.ports,&view) == 0 && rip3_test_image_area(f->host.ports,1,&area,15) == 0);
	bool target_offscreen = pointer == OFFSCREEN || pointer == OFFSCREEN_PROTECTED;
	if (target_offscreen || pointer == ONSCREEN || pointer == ONSCREEN_PROTECTED) {
		CHECK(st_rip3_ports_define(f->host.ports,9,target_offscreen ? &target_bitmap : &target_screen,target_offscreen) == 0);
		if (target_offscreen) CHECK(st_rip3_ports_switch(f->host.ports,9) == 0 && st_rip3_ports_erase(f->host.ports,60) == 0);
		CHECK(rip3_test_image_area(f->host.ports,9,&area,8) == 0 && st_rip3_ports_switch(f->host.ports,1) == 0);
	}
	if (pointer == OFFSCREEN_PROTECTED || pointer == ONSCREEN_PROTECTED) CHECK(st_rip3_ports_protect(f->host.ports,9,true) == 0);
	if (pointer == CURRENT_PROTECTED) CHECK(st_rip3_ports_protect(f->host.ports,1,true) == 0);
	f->selection = (struct st_rip3_clipboard_selection){pointer != ABSENT,pointer == CURRENT || pointer == CURRENT_PROTECTED ? 1u : 9u};
	f->original_selection = f->selection;
	if (pointer != UNBOUND) f->host.clipboard = &f->selection;
	uint32_t colors[256]; for (unsigned i = 0; i < 256; i++) colors[i] = i*UINT32_C(0x010101); colors[1] = 0x112233;
	CHECK(st_rip3_palettes_switch(f->host.palettes,3) == 0 && st_rip3_palettes_rgb(f->host.palettes,0,8,colors,256) == 0
	    && st_rip3_palettes_protect(f->host.palettes,3,true) == 0);
	CHECK(st_rip3_environments_get(f->host.environments,0,&f->environment) == 0);
	f->environment.world_width = width; f->environment.world_height = height;
	f->environment.port = 9; f->environment.palette = 7; f->environment.graphics_style = 6;
	CHECK(st_rip3_environments_define(f->host.environments,2,&f->environment) == 0
	    && st_rip3_environments_select(f->host.environments,2) == 0);
	f->offscreen = offscreen; f->placeholder_fails = placeholder_fails;
	f->paths.host = directory; f->host.resources = &f->paths;
	f->feedback = (struct st_rip3_bitmap_error_sink){f,show}; f->host.bitmap_errors = &f->feedback;
	for (unsigned i = 0; i < 36; i++) CHECK(st_rip3_ports_info(f->host.ports,i,f->original+i) == 0);
	return 0;
}

static void
destroy(struct fixture *f)
{
	st_rip3_environments_destroy(f->host.environments); st_rip3_ports_destroy(f->host.ports); st_rip3_palettes_destroy(f->host.palettes);
}

static int
matrix(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, const char *directory)
{
	for (unsigned offscreen = 0; offscreen < 2; offscreen++) for (unsigned pointer = ABSENT; pointer <= UNBOUND; pointer++)
		for (unsigned clipboard = 0; clipboard < 2; clipboard++) for (unsigned fail = 0; fail < 2; fail++) {
			struct fixture f = {0}; char body[80];
			CHECK(create(&f,canvas,width,height,directory,(enum pointer_state)pointer,offscreen != 0,fail != 0) == 0);
			(void)snprintf(body,sizeof(body),"1I000000%u00Absent",clipboard);
			CHECK(process(&f,body,ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && f.placeholders == 1);
			CHECK(state(&f,36) == 0 && capture(&f,width,height,fail == 0,false) == 0);
			destroy(&f);
		}
	return 0;
}

static int
failures(const char *directory)
{
	struct fixture f = {0}; char body[80];
	CHECK(create(&f,ST_RIP3_CANVAS_640_480,640,480,directory,OFFSCREEN_PROTECTED,false,false) == 0);
	for (unsigned clipboard = 0; clipboard < 2; clipboard++) {
		(void)snprintf(body,sizeof(body),"1I000000%u00Broken.BMP",clipboard);
		CHECK(process(&f,body,ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && state(&f,36) == 0);
	}
	CHECK(f.placeholders == 2);
	bool *const faults[] = {&st_rip3_bitmap_file_test_fail_allocate,&st_rip3_bitmap_file_test_fail_decode,&st_rip3_bitmap_stamp_test_fail_allocate};
	for (size_t i = 0; i < sizeof(faults)/sizeof(faults[0]); i++) {
		*faults[i] = true; unsigned before = f.placeholders;
		CHECK(process(&f,"1I000000000Pixel.BMP",ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !*faults[i]);
		CHECK(f.placeholders == before+1 && state(&f,36) == 0);
	}
	CHECK(process(&f,"1I000000100Pixel.BMP",ST_RIP3_COMMAND_UNSUPPORTED) == 0 && f.placeholders == 5);
	CHECK(process(&f,"1I000000000../Pixel.BMP",ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && f.placeholders == 5);
	CHECK(process(&f,"1I000000000\xff.BMP",ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && f.placeholders == 5);
	CHECK(capture(&f,640,480,true,false) == 0);
	struct st_rip3_stream *stream = NULL;
	const struct st_rip3_stream_sink sink = {.context=&f,.command=command};
	CHECK(st_rip3_stream_create(&stream,&sink) == 0);
	const char wire[] = "!|1I|1I000000100Absent|1I000000000Pixel.BMP|J1S\r";
	unsigned before = f.errors;
	for (size_t i = 0; i < sizeof(wire)-1; i++) st_rip3_stream_process(stream,(const uint8_t *)wire+i,1);
	CHECK(f.errors == before+2 && f.placeholders == 6 && state(&f,64) == 0 && capture(&f,640,480,true,true) == 0);
	st_rip3_stream_destroy(stream); destroy(&f); return 0;
}

static int
capture_after_load(const char *directory, enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	for (unsigned offscreen = 0; offscreen < 2; offscreen++) for (unsigned pointer = ABSENT; pointer <= CURRENT_PROTECTED; pointer++) {
		struct fixture f = {0}; uint64_t budget = UINT64_C(1000000);
		CHECK(create(&f,canvas,width,height,directory,(enum pointer_state)pointer,offscreen != 0,false) == 0);
		f.host.offscreen_pixel_budget = &budget;
		bool protected = pointer == OFFSCREEN_PROTECTED || pointer == ONSCREEN_PROTECTED || pointer == CURRENT_PROTECTED;
		CHECK(process(&f,"1I000000100Pixel.BMP",protected ? ST_RIP3_COMMAND_OPERATION_FAILED : -1) == 0 && f.placeholders == 0);
		if (protected) {
			CHECK(state(&f,36) == 0 && capture(&f,width,height,false,true) == 0);
		}
		else {
			unsigned target = pointer == ABSENT ? 2 : pointer == CURRENT ? 1 : 9;
			unsigned selected; struct st_rip3_port_info info; struct st_rip3_palette palette; struct st_rip3_bitmap *image = NULL;
			CHECK(f.selection.set && f.selection.port == target && st_rip3_ports_current(f.host.ports,&selected) == 0
			    && selected == (pointer == CURRENT ? 0u : 1u));
			CHECK(st_rip3_ports_info(f.host.ports,target,&info) == 0 && info.defined && info.offscreen && info.bounds.x1 == 1 && info.bounds.y1 == 1);
			CHECK(st_rip3_palettes_get(f.host.palettes,3,&palette) == 0 && st_rip3_bitmap_capture(&image,f.host.ports,target,NULL,&palette) == 0);
			for (unsigned i = 0; i < 4; i++) CHECK(image->pixels->primary[i] == (pointer == CURRENT ? 85u : 1u));
			st_rip3_bitmap_destroy(image);
		}
		destroy(&f);
	}
	/* Capture allocation failure happens after successful paint, with no
	 * missing-file placeholder or restoration of the deleted old clipboard. */
	struct fixture f = {0}; uint64_t budget = 0;
	CHECK(create(&f,ST_RIP3_CANVAS_640_480,640,480,directory,OFFSCREEN,false,false) == 0);
	f.host.offscreen_pixel_budget = &budget;
	CHECK(process(&f,"1I000000100Pixel.BMP",ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && f.placeholders == 0 && !f.selection.set);
	struct st_rip3_port_info info; uint8_t pixel;
	CHECK(st_rip3_ports_info(f.host.ports,9,&info) == 0 && !info.defined
	    && st_rip3_ports_read(f.host.ports,1,2,1,&pixel) == 0 && pixel == 1);
	destroy(&f);
	/* A renderer allocation failure happens before GET_IMAGE: protected
	 * target rejection and destructive replacement must never be reached. */
	f = (struct fixture){0}; budget = UINT64_C(1000000);
	CHECK(create(&f,ST_RIP3_CANVAS_640_480,640,480,directory,OFFSCREEN_PROTECTED,false,false) == 0);
	f.host.offscreen_pixel_budget = &budget; st_rip3_bitmap_stamp_test_fail_allocate = true;
	CHECK(process(&f,"1I000000100Pixel.BMP",ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_bitmap_stamp_test_fail_allocate);
	CHECK(f.placeholders == 1 && state(&f,36) == 0 && capture(&f,640,480,true,false) == 0);
	destroy(&f);
	/* A fully clipped ordinary stamp still returns its original rectangle:
	 * GET_IMAGE replaces the old clipboard with a new clear 2x2 bitmap. */
	f = (struct fixture){0};
	CHECK(create(&f,canvas,width,height,directory,OFFSCREEN,false,false) == 0);
	f.host.offscreen_pixel_budget = &budget;
	CHECK(process(&f,"1IZZZZ00100Pixel.BMP",-1) == 0 && f.placeholders == 0);
	struct st_rip3_palette palette; struct st_rip3_bitmap *image = NULL;
	CHECK(st_rip3_palettes_get(f.host.palettes,3,&palette) == 0 && st_rip3_bitmap_capture(&image,f.host.ports,9,NULL,&palette) == 0);
	CHECK(image->pixels->width == 2 && image->pixels->height == 2);
	for (unsigned i = 0; i < 4; i++) CHECK(image->pixels->primary[i] == 0);
	st_rip3_bitmap_destroy(image); destroy(&f); return 0;
}

int
main(int argc, char **argv)
{
	CHECK(argc == 2 && fs_create_directories(argv[1]) == 0);
	/* Independently authored 1x1 Windows BMP: 14+40 byte headers and one
	 * padded BGR row. Named offsets keep the payload at byte 54 explicit. */
	const uint8_t bmp[58] = {[0]='B',[1]='M',[2]=58,[10]=54,[14]=40,
	    [18]=1,[22]=1,[26]=1,[28]=24,[34]=4,[54]=0x33,[55]=0x22,[56]=0x11};
	char *path = NULL;
	CHECK(path_join_alloc(&path,argv[1],"Pixel.BMP") == 0 && fs_replace_file(path,bmp,sizeof(bmp)) == 0); free(path);
	CHECK(path_join_alloc(&path,argv[1],"Broken.BMP") == 0 && fs_replace_file(path,"bad",3) == 0); free(path);
	CHECK(matrix(ST_RIP3_CANVAS_640_480,640,480,argv[1]) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_800_600,800,600,argv[1]) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_1024_768,1024,768,argv[1]) == 0);
	CHECK(failures(argv[1]) == 0);
	CHECK(capture_after_load(argv[1],ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(capture_after_load(argv[1],ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(capture_after_load(argv[1],ST_RIP3_CANVAS_1024_768,1024,768) == 0); return 0;
}
