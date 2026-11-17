/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_ports.h"
#include "st_rip3_stream.h"
#include "st_rip3_backup.h"
#include "st_rip3_bitmap.h"
#include "st_rip3_bitmap_stamp.h"
#include "st_rip3_fill.h"
#include "st_rip3_image.h"
#include "st_rip3_wallpaper.h"
#include "io_error.h"
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

extern bool st_rip3_copy_test_fail_malloc, st_rip3_image_test_fail_allocate;

struct fixture {
	struct st_rip3_command_host host;
	unsigned errors;
	enum st_rip3_command_diagnostic kind;
};

static void
diagnostic(void *context, enum st_rip3_command_diagnostic kind)
{
	struct fixture *f = context;
	f->kind = kind;
	f->errors++;
}

static void
command(void *context, const uint8_t *bytes, size_t length)
{
	struct fixture *f = context;
	const struct st_rip3_command_sink sink = {f,diagnostic};
	st_rip3_commands_process(&f->host,&sink,bytes,length);
}

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct fixture f = {0};
	struct st_rip3_environment environment;
	struct st_rip3_port_info info;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	struct st_rip3_stream *stream = NULL;
	const struct st_rip3_stream_sink sink = {.context=&f,.command=command};
	const struct st_rip3_rect port = {25,25,224,224}, point = {1,1,1,1};
	const uint8_t input[] = "!|v28283A3A\r"; /* WORLD (80,80)..(118,118). */
	CHECK(st_rip3_ports_create(&f.host.ports,canvas) == 0);
	CHECK(st_rip3_environments_create(&f.host.environments,canvas) == 0);
	CHECK(st_rip3_environments_get(f.host.environments,0,&environment) == 0);
	environment.world_width = 2*width; environment.world_height = 2*height;
	CHECK(st_rip3_environments_set(f.host.environments,0,&environment) == 0);
	CHECK(st_rip3_ports_define(f.host.ports,1,&port,false) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,1) == 0);
	CHECK(st_rip3_stream_create(&stream,&sink) == 0);
	for (size_t i = 0; i < sizeof(input)-1; i++) st_rip3_stream_process(stream,input+i,1);
	CHECK(f.errors == 0);
	CHECK(st_rip3_ports_fill(f.host.ports,&point,7,ST_RIP3_COPY) == 0);
	for (unsigned i = 0; i < 256; i++) palette.rgb[i] = i*UINT32_C(0x010101);
	CHECK(st_rip3_ports_snapshot(f.host.ports,&palette,&capture) == 0);
	/* Original producer stores DEVICE [40,40,60,60), then drawing uses its
	 * origin directly. The port's (25,25) must not be added a second time. */
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++)
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG
		    | (x == 41 && y == 41 ? UINT32_C(0x070707) : 0)));
	CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0);
	CHECK(info.viewport.x0 == 15 && info.viewport.y0 == 15
	    && info.viewport.x1 == 34 && info.viewport.y1 == 34);
	st_pixel_buffer_destroy(capture); st_rip3_stream_destroy(stream);
	st_rip3_environments_destroy(f.host.environments); st_rip3_ports_destroy(f.host.ports);
	return 0;
}

static int
create(struct fixture *f, enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_environment environment;
	const struct st_rip3_rect port = {25,25,224,224};
	CHECK(st_rip3_ports_create(&f->host.ports,canvas) == 0);
	CHECK(st_rip3_environments_create(&f->host.environments,canvas) == 0);
	CHECK(st_rip3_environments_get(f->host.environments,0,&environment) == 0);
	environment.world_width = 2*width; environment.world_height = 2*height;
	CHECK(st_rip3_environments_set(f->host.environments,0,&environment) == 0);
	CHECK(st_rip3_ports_define(f->host.ports,1,&port,false) == 0);
	CHECK(st_rip3_ports_switch(f->host.ports,1) == 0);
	return 0;
}

static void
destroy(struct fixture *f)
{
	st_rip3_environments_destroy(f->host.environments); st_rip3_ports_destroy(f->host.ports);
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

static int
viewport(struct fixture *f, const uint32_t fields[4], unsigned width, unsigned radix)
{
	static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#&";
	struct st_rip3_environment environment;
	char body[22] = {'v'};
	CHECK(st_rip3_environments_get(f->host.environments,0,&environment) == 0);
	environment.coordinate_size = width; environment.base_math = radix;
	CHECK(st_rip3_environments_set(f->host.environments,0,&environment) == 0);
	for (size_t i = 0; i < 4; i++) {
		uint32_t value = fields[i];
		for (unsigned j = 0; j < width; j++) {
			body[1+i*width+width-1-j] = digits[value%radix]; value /= radix;
		}
		CHECK(value == 0);
	}
	body[1+4*width] = 0;
	return process(f,body,-1);
}

static void
gray(struct st_rip3_palette *palette)
{
	for (unsigned i = 0; i < 256; i++) palette->rgb[i] = i*UINT32_C(0x010101);
}

static int
consumer_capture(struct fixture *f, uint32_t width, uint32_t height, bool painted)
{
	const uint8_t tile[] = {1,2,4,8};
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	gray(&palette);
	CHECK(st_rip3_ports_snapshot(f->host.ports,&palette,&capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		unsigned expected = x == 41 && y == 41 ? 7 : 0;
		if (x >= 2 && x <= 7 && y >= 3 && y <= 8) expected = tile[((y-3)%2)*2+(x-2)%2];
		if (x >= 100 && x <= 101 && y >= 100 && y <= 101) expected = tile[(y-100)*2+x-100];
		if (painted && x >= 10 && x <= 15 && y >= 3 && y <= 8) expected = x < 13 ? 2 : 4;
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG|expected*UINT32_C(0x010101)));
	}
	st_pixel_buffer_destroy(capture);
	return 0;
}

static int
consumers(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct fixture f = {0};
	struct st_rip3_port_info info;
	struct st_rip3_palette palette;
	struct st_rip3_backup *backup = NULL;
	const struct st_rip3_rect destination = {100,100,105,105};
	const struct st_rip3_bitmap_destination seed = {0,0,1,1};
	const uint32_t small[] = {4,6,14,16}, image_area[] = {20,6,30,16};
	const uint8_t tile[] = {1,2,4,8};
	uint32_t rgb[] = {ST_PIXEL_COLOR_RGB_TAG|UINT32_C(0x020202),ST_PIXEL_COLOR_RGB_TAG|UINT32_C(0x040404)};
	struct st_pixel_buffer image = {.width=2,.height=1,.primary=rgb};
	CHECK(create(&f,canvas,width,height) == 0); gray(&palette);
	CHECK(process(&f,"v28283A3A",-1) == 0);
	CHECK(st_rip3_ports_plot(f.host.ports,1,1,7,ST_RIP3_COPY) == 0);
	CHECK(viewport(&f,small,2,36) == 0);
	CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && info.viewport.x0 == -23 && info.viewport.y0 == -22
	    && info.viewport.x1 == -18 && info.viewport.y1 == -17 && info.clip.x1 == 5 && info.clip.y1 == 5);
	for (int32_t y = 0; y < 2; y++) for (int32_t x = 0; x < 2; x++)
		CHECK(st_rip3_ports_plot(f.host.ports,x,y,tile[2*y+x],ST_RIP3_COPY) == 0);
	struct st_rip3_bitmap *bitmap = NULL;
	CHECK(st_rip3_bitmap_capture(&bitmap,f.host.ports,1,NULL,&palette) == 0);
	CHECK(bitmap->pixels->width == 6 && bitmap->pixels->height == 6);
	for (unsigned y = 0; y < 6; y++) for (unsigned x = 0; x < 6; x++)
		CHECK(bitmap->pixels->primary[6*y+x] == (x < 2 && y < 2 ? tile[2*y+x] : 0u));
	st_rip3_bitmap_destroy(bitmap); bitmap = NULL;
	CHECK(st_rip3_ports_activate(f.host.ports,false) == 0);
	CHECK(st_rip3_bitmap_capture_stored_viewport(&bitmap,f.host.ports,1,NULL,&palette) == 0);
	CHECK(bitmap->pixels->primary[0] == 1 && bitmap->pixels->primary[7] == 8);
	st_rip3_bitmap_destroy(bitmap);
	CHECK(st_rip3_ports_activate(f.host.ports,true) == 0);
	CHECK(st_rip3_ports_copy(f.host.ports,1,NULL,0,&destination,ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_wallpaper(f.host.ports,&seed,false) == 0);
	CHECK(viewport(&f,image_area,2,36) == 0);
	CHECK(st_rip3_ports_image_style(f.host.ports,1,NULL,4) == 0);
	st_rip3_image_test_fail_allocate = true;
	CHECK(st_rip3_draw_image(f.host.ports,&image,NULL,0,&palette) == IO_ERROR_OUT_OF_MEMORY);
	CHECK(!st_rip3_image_test_fail_allocate && consumer_capture(&f,width,height,false) == 0);
	CHECK(st_rip3_draw_image(f.host.ports,&image,NULL,0,&palette) == 0);
	CHECK(st_rip3_ports_protect(f.host.ports,1,true) == 0);
	CHECK(st_rip3_ports_backup_create(&backup) == 0);
	CHECK(st_rip3_backup_save(backup,ST_RIP3_BACKUP_BASE,0,f.host.ports) == 0);
	CHECK(process(&f,"v00000000",ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(st_rip3_ports_protect(f.host.ports,1,false) == 0);
	const struct st_rip3_rect other = {700,20,710,30};
	CHECK(st_rip3_ports_viewport_device(f.host.ports,&other) == 0);
	CHECK(st_rip3_ports_activate(f.host.ports,false) == 0);
	CHECK(st_rip3_ports_image_style(f.host.ports,1,NULL,0) == 0);
	CHECK(st_rip3_ports_protect(f.host.ports,1,true) == 0);
	CHECK(st_rip3_backup_restore(backup,ST_RIP3_BACKUP_BASE,0,f.host.ports) == 0);
	CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && info.protected && info.viewport_active
	    && info.viewport.x0 == -15 && info.viewport.y0 == -22 && info.viewport.x1 == -10 && info.viewport.y1 == -17
	    && info.image.automatic && info.image.flags == 4 && info.clip.x1 == 5 && info.clip.y1 == 5);
	CHECK(st_rip3_ports_protect(f.host.ports,1,false) == 0);
	/* Positive WORLD fields round to the zero sentinel and retain geometry. */
	CHECK(process(&f,"v00000001",-1) == 0);
	CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && !info.viewport_active && info.viewport.x0 == -15);
	CHECK(st_rip3_ports_activate(f.host.ports,true) == 0);
	CHECK(process(&f,"v0000000",-1) == 0);
	CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && !info.viewport_active && info.image.flags == 4);
	CHECK(consumer_capture(&f,width,height,true) == 0);
	st_rip3_backup_destroy(backup); destroy(&f);
	return 0;
}

static int
physical_capture(struct fixture *f, uint32_t width, uint32_t height, unsigned full)
{
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	gray(&palette);
	CHECK(st_rip3_ports_snapshot(f->host.ports,&palette,&capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		unsigned expected = full;
		if (full == 0) {
			if (x >= width-2 && (y == 4 || y == 6 || y == 8)) expected = 2;
			if (x >= width-2 && y == 12) expected = 9;
			if ((x == 10 || x == 11) && y == 20) expected = 2;
			if (x >= 12 && x <= 16 && y == 20) expected = 0x55;
			if (x >= width-2 && y >= 30 && y <= 38 && y%2 == 0) {
				const unsigned modes[] = {2,0x57,0x57,0,0xfd};
				expected = modes[(y-30)/2];
			}
		}
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG|expected*UINT32_C(0x010101)));
	}
	st_pixel_buffer_destroy(capture); return 0;
}

static int
physical(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct fixture f = {0};
	struct st_rip3_port_info info;
	struct st_rip3_palette palette;
	const struct st_rip3_rect source = {0,0,1,0};
	const struct st_rip3_bitmap_destination destination = {0,0,9,0}, seed = {0,0,0,0};
	uint32_t rgb[] = {ST_PIXEL_COLOR_RGB_TAG|UINT32_C(0x020202),ST_PIXEL_COLOR_RGB_TAG|UINT32_C(0x040404)};
	uint32_t codes[] = {0,1}, source_palette[] = {0x020202,0x040404};
	struct st_pixel_buffer image = {.width=2,.height=1,.primary=rgb};
	struct st_pixel_buffer indexed = {.width=2,.height=1,.primary=codes};
	struct st_rip3_bitmap bitmap = {.pixels=&indexed,.palette={0x020202,0x040404},.palette_size=2,.bit_depth=8};
	CHECK(create(&f,canvas,width,height) == 0); gray(&palette);
	CHECK(st_rip3_ports_define(f.host.ports,2,&source,true) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,2) == 0);
	CHECK(st_rip3_ports_plot(f.host.ports,0,0,2,ST_RIP3_COPY) == 0 && st_rip3_ports_plot(f.host.ports,1,0,4,ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,1) == 0);
	CHECK(st_rip3_ports_image_style(f.host.ports,1,NULL,4) == 0);
	uint32_t fields[] = {2*(width-2),8,2*(width+7),8};
	CHECK(viewport(&f,fields,3,36) == 0);
	CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && info.clip.x1 == 1 && info.clip.y1 == 0
	    && info.viewport.x1-info.viewport.x0 == 9);
	CHECK(st_rip3_draw_image(f.host.ports,&image,NULL,0,&palette) == 0);
	fields[1] = fields[3] = 12;
	CHECK(viewport(&f,fields,3,36) == 0);
	CHECK(st_rip3_draw_bitmap(f.host.ports,&bitmap,&destination,&palette,ST_RIP3_BITMAP_MATCH,ST_RIP3_TRANSFER_COPY,NULL) == 0);
	fields[1] = fields[3] = 16;
	CHECK(viewport(&f,fields,3,36) == 0);
	CHECK(st_rip3_ports_copy(f.host.ports,2,NULL,1,NULL,ST_RIP3_TRANSFER_COPY) == 0);
	fields[1] = fields[3] = 20;
	CHECK(viewport(&f,fields,3,36) == 0);
	CHECK(st_rip3_ports_image_style(f.host.ports,1,NULL,5) == 0);
	CHECK(st_rip3_draw_image(f.host.ports,&image,NULL,0,&palette) == 0); /* Aspect fit is entirely outside physical output. */
	fields[1] = fields[3] = 24;
	CHECK(viewport(&f,fields,3,36) == 0);
	CHECK(st_rip3_ports_plot(f.host.ports,0,0,9,ST_RIP3_COPY) == 0);
	CHECK(st_rip3_wallpaper(f.host.ports,&seed,false) == 0);
	/* Every transfer mode samples against the ten-pixel requested target,
	 * rather than refitting the source to its two physical output pixels. */
	for (unsigned mode = 0; mode < 5; mode++) {
		fields[1] = fields[3] = 60+4*mode;
		CHECK(viewport(&f,fields,3,36) == 0);
		CHECK(st_rip3_ports_erase(f.host.ports,0x55) == 0);
		CHECK(st_rip3_ports_copy(f.host.ports,2,NULL,1,NULL,(enum st_rip3_transfer_mode)mode) == 0);
	}
	/* Physical source clipping preserves the original 10-to-6 mapping. */
	CHECK(st_rip3_ports_switch(f.host.ports,2) == 0);
	const struct st_rip3_rect virtual_source = {0,0,9,0}, target = {10,20,15,20};
	CHECK(st_rip3_ports_viewport_device(f.host.ports,&virtual_source) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,0) == 0);
	CHECK(st_rip3_ports_fill(f.host.ports,&target,0x55,ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_copy(f.host.ports,2,NULL,0,&target,ST_RIP3_TRANSFER_COPY) == 0);
	const struct st_rip3_rect shifted = {11,20,16,20};
	st_rip3_copy_test_fail_malloc = true;
	CHECK(st_rip3_ports_copy(f.host.ports,0,&target,0,&shifted,ST_RIP3_TRANSFER_COPY) == IO_ERROR_OUT_OF_MEMORY);
	CHECK(!st_rip3_copy_test_fail_malloc);
	CHECK(st_rip3_ports_copy(f.host.ports,0,&target,0,&shifted,ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_ports_switch(f.host.ports,1) == 0);
	fields[0] = 2*(width+10); fields[2] = 2*(width+20);
	CHECK(viewport(&f,fields,3,36) == 0);
	CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && info.viewport_active && info.clip.x1 == -1);
	CHECK(st_rip3_ports_image_style(f.host.ports,1,NULL,4) == 0);
	CHECK(st_rip3_draw_image(f.host.ports,&image,NULL,0,&palette) == 0);
	CHECK(st_rip3_draw_bitmap(f.host.ports,&bitmap,&destination,&palette,ST_RIP3_BITMAP_MATCH,ST_RIP3_TRANSFER_COPY,NULL) == 0);
	struct st_rip3_bitmap marker = {0}, *captured = &marker;
	CHECK(st_rip3_bitmap_capture_stored_viewport(&captured,f.host.ports,1,NULL,&palette) == IO_ERROR_NOT_SUPPORTED && captured == &marker);
	CHECK(physical_capture(&f,width,height,0) == 0);
	/* An enormous stored rectangle must not request an enormous staging
	 * bitmap or change the sampling scale to the visible screen dimensions. */
	const struct st_rip3_rect huge = {0,0,INT32_MAX,INT32_MAX};
	const struct st_rip3_fill fill = {.rows={255,255,255,255,255,255,255,255}};
	CHECK(st_rip3_ports_viewport_device(f.host.ports,&huge) == 0);
	CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && info.clip.x1 == (int32_t)width-1 && info.clip.y1 == (int32_t)height-1);
	CHECK(st_rip3_draw_bar(f.host.ports,&huge,&fill,9,0,ST_RIP3_COPY) == 0);
	st_rip3_image_test_fail_allocate = true;
	CHECK(st_rip3_draw_image(f.host.ports,&indexed,source_palette,2,&palette) == IO_ERROR_OUT_OF_MEMORY);
	CHECK(!st_rip3_image_test_fail_allocate && physical_capture(&f,width,height,9) == 0);
	CHECK(st_rip3_draw_image(f.host.ports,&indexed,source_palette,2,&palette) == 0);
	CHECK(physical_capture(&f,width,height,2) == 0);
	const struct st_rip3_bitmap_destination huge_destination = {0,0,INT32_MAX,INT32_MAX};
	CHECK(st_rip3_draw_bitmap(f.host.ports,&bitmap,&huge_destination,&palette,ST_RIP3_BITMAP_MATCH,ST_RIP3_TRANSFER_COPY,NULL) == 0);
	CHECK(st_rip3_ports_copy(f.host.ports,2,NULL,1,NULL,ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(physical_capture(&f,width,height,2) == 0);
	destroy(&f); return 0;
}

static int
fields_and_state(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct fixture f = {0};
	struct st_rip3_port_info info;
	const struct st_rip3_rect image = {1,2,7,8};
	const uint32_t corners[4][4] = {{80,80,118,118},{118,80,80,118},{80,118,118,80},{118,118,80,80}};
	CHECK(create(&f,canvas,width,height) == 0);
	CHECK(st_rip3_ports_image_style(f.host.ports,1,&image,17) == 0);
	for (unsigned radix = 36; radix <= 64; radix += 28) for (unsigned count = 2; count <= 5; count++) {
		for (unsigned order = 0; order < 4; order++) {
			CHECK(viewport(&f,corners[order],count,radix) == 0);
			CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && info.viewport_active
			    && info.viewport.x0 == 15 && info.viewport.y0 == 15 && info.viewport.x1 == 34 && info.viewport.y1 == 34
			    && info.clip.x1 == 19 && info.clip.y1 == 19 && !info.image.automatic && info.image.flags == 17
			    && info.image.area.x0 == 1 && info.image.area.y0 == 2 && info.image.area.x1 == 7 && info.image.area.y1 == 8);
		}
	}
	const uint32_t full[] = {0,0,2*width,2*height};
	CHECK(viewport(&f,full,3,36) == 0);
	CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0
	    && info.viewport.x0 == -25 && info.viewport.y0 == -25
	    && info.viewport.x1 == (int32_t)width-25 && info.viewport.y1 == (int32_t)height-25
	    && info.clip.x1 == (int32_t)width-1 && info.clip.y1 == (int32_t)height-1);
	if (canvas == ST_RIP3_CANVAS_640_480) {
		CHECK(process(&f,"n2",-1) == 0 && process(&f,"v0000ZKQO",-1) == 0);
		CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && info.viewport.x1 == 615 && info.viewport.y1 == 455);
	}
	CHECK(viewport(&f,corners[0],5,64) == 0);
	/* Wide results beyond the current internal coordinate plane reject
	 * nonfatally; they must neither saturate a stored rectangle nor wrap it. */
	struct st_rip3_environment environment;
	CHECK(st_rip3_environments_get(f.host.environments,0,&environment) == 0);
	environment.world_width = environment.world_height = 1;
	CHECK(st_rip3_environments_set(f.host.environments,0,&environment) == 0);
	CHECK(process(&f,"v0000000000&&&&&&&&&&",ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && info.viewport.x0 == 15 && info.viewport.x1 == 34 && info.image.flags == 17);
	const uint32_t zero[] = {0,0,0,0};
	CHECK(viewport(&f,zero,2,36) == 0);
	CHECK(st_rip3_ports_info(f.host.ports,1,&info) == 0 && !info.viewport_active && info.viewport.x0 == 15);
	destroy(&f); return 0;
}

int
main(void)
{
	CHECK(scene(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	CHECK(consumers(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(consumers(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(consumers(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	CHECK(physical(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(physical(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(physical(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	CHECK(fields_and_state(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(fields_and_state(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(fields_and_state(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	return 0;
}
