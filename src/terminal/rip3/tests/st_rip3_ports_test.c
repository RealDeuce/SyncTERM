/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_ports.h"
#include "st_rip3_backup.h"
#include "st_rip3_clipboard.h"
#include "st_pixels.h"
#include "io_error.h"

#include <stdint.h>
#include <stdio.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, \
		    #expression); \
		return 1; \
	} \
} while (0)

static int
screen_fill(void)
{
	struct st_rip3_ports *ports=NULL;uint8_t value,damage[12]={0};unsigned current;
	CHECK(st_rip3_ports_create_device(&ports,4,3)==0);
	const struct st_rip3_rect bitmap={0,0,1,1},area={-1,1,2,8},reversed={2,1,1,2};
	CHECK(st_rip3_ports_define(ports,7,&bitmap,true)==0 && st_rip3_ports_switch(ports,7)==0);
	CHECK(st_rip3_ports_activate(ports,false)==0 && st_rip3_ports_protect(ports,7,true)==0);
	CHECK(st_rip3_ports_screen_damage(ports,damage,sizeof(damage))==0);
	CHECK(st_rip3_ports_screen_fill(ports,&area,3)==0);
	for (int32_t y=0;y<3;y++) for (int32_t x=0;x<4;x++) {
		CHECK(st_rip3_ports_read(ports,0,x,y,&value)==0 && value==(y>=1 && x<=2 ? 3 : 0));
		CHECK(damage[y*4+x]==(y>=1 && x<=2 ? 1 : 0));
	}
	for (size_t i=0;i<sizeof(damage);i++) damage[i]=0;
	CHECK(st_rip3_ports_screen_fill(ports,&area,3)==0);
	for (size_t i=0;i<sizeof(damage);i++) CHECK(damage[i]==(i>=4 && i%4<=2 ? 1 : 0));
	CHECK(st_rip3_ports_screen_fill(ports,&reversed,9)!=0);
	CHECK(st_rip3_ports_read(ports,0,1,1,&value)==0 && value==3);
	CHECK(st_rip3_ports_read(ports,7,0,0,&value)==0 && value==0);
	struct st_rip3_port_info info;
	CHECK(st_rip3_ports_current(ports,&current)==0 && current==7 && st_rip3_ports_info(ports,7,&info)==0
	    && info.protected && !info.viewport_active);
	CHECK(st_rip3_ports_screen_fill(NULL,&area,0)==IO_ERROR_INVALID_ARGUMENT);
	st_rip3_ports_destroy(ports);return 0;
}

static int draw_captured(void *context)
{
	struct st_rip3_ports *ports=context;unsigned current;
	if (st_rip3_ports_current(ports,&current)!=0 || current!=0) return IO_ERROR_CONFLICT;
	const struct st_rip3_rect pixel={0,0,0,0};
	int status=st_rip3_ports_fill(ports,&pixel,7,ST_RIP3_COPY);
	return status==0 ? IO_ERROR_OUT_OF_MEMORY : status;
}
static int captured_view(void)
{
	struct st_rip3_ports *ports=NULL;struct st_rip3_port_info original,after;
	CHECK(st_rip3_ports_create_device(&ports,4,3)==0 && st_rip3_ports_info(ports,0,&original)==0);
	const struct st_rip3_rect view={1,1,2,2},bitmap={0,0,1,1};
	CHECK(st_rip3_ports_viewport_device(ports,&view)==0 && st_rip3_ports_activate(ports,false)==0);
	CHECK(st_rip3_ports_define(ports,6,&bitmap,true)==0 && st_rip3_ports_switch(ports,6)==0);
	CHECK(st_rip3_ports_button_view(ports,&original,draw_captured,ports)==IO_ERROR_OUT_OF_MEMORY);
	unsigned current;uint8_t value;
	CHECK(st_rip3_ports_current(ports,&current)==0 && current==6);
	CHECK(st_rip3_ports_info(ports,0,&after)==0 && !after.viewport_active
	    && after.viewport.x0==1 && after.viewport.y0==1 && after.viewport.x1==2 && after.viewport.y1==2);
	CHECK(st_rip3_ports_read(ports,0,0,0,&value)==0 && value==7);
	CHECK(st_rip3_ports_read(ports,0,1,1,&value)==0 && value==0);
	st_rip3_ports_destroy(ports);return 0;
}

static int
layout(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_port_info info;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	struct st_rip3_rect bounds = {10, 20, 14, 24};
	struct st_rip3_rect viewport = {1, 1, 3, 3};
	struct st_rip3_rect all = {INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX};
	struct st_rip3_rect offscreen = {99, 99, 2, 2};
	struct st_rip3_rect dest = {30, 40, 32, 42};
	unsigned current;
	uint8_t value;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_current(ports, &current) == 0 && current == 0);
	CHECK(st_rip3_ports_info(ports, 0, &info) == 0);
	CHECK(info.defined && !info.offscreen && !info.protected && info.viewport_active);
	CHECK(info.bounds.x0 == 0 && info.bounds.y0 == 0
	    && info.bounds.x1 == 639 && info.bounds.y1 == 479);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, false) == 0);
	CHECK(st_rip3_ports_current(ports, &current) == 0 && current == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ports_viewport(ports, &viewport) == 0);
	CHECK(st_rip3_ports_fill(ports, &all, 7, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_plot(ports, 0, 0, 2, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_plot(ports, 2, 2, 4, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_plot(ports, 3, 3, 15, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_plot(ports, INT32_MAX, INT32_MIN, 15, ST_RIP3_COPY) == 0);
	/* A second overlapping screen port observes and overwrites those pixels. */
	bounds = (struct st_rip3_rect){12, 22, 16, 26};
	CHECK(st_rip3_ports_define(ports, 2, &bounds, false) == 0);
	CHECK(st_rip3_ports_read(ports, 2, 0, 0, &value) == 0 && value == 7);
	CHECK(st_rip3_ports_switch(ports, 2) == 0);
	CHECK(st_rip3_ports_plot(ports, 0, 0, 5, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 2, 2, &value) == 0 && value == 5);
	/* Offscreen x0/y0 are ignored. Offscreen drawing is invisible until copy. */
	CHECK(st_rip3_ports_define(ports, 3, &offscreen, true) == 0);
	CHECK(st_rip3_ports_info(ports, 3, &info) == 0);
	CHECK(info.offscreen && info.bounds.x0 == 0 && info.bounds.y0 == 0
	    && info.bounds.x1 == 2 && info.bounds.y1 == 2);
	CHECK(st_rip3_ports_switch(ports, 3) == 0);
	CHECK(st_rip3_ports_erase(ports, 1) == 0);
	CHECK(st_rip3_ports_plot(ports, 1, 1, 6, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_read(ports, 0, 1, 1, &value) == 0 && value == 0);
	CHECK(st_rip3_ports_protect(ports, 3, true) == 0);
	CHECK(st_rip3_ports_plot(ports, 2, 2, 14, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_copy(ports, 3, NULL, 0, &dest, ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_ports_current(ports, &current) == 0 && current == 3);
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	/* One coherent capture verifies the related screen pixels and margins. */
	for (uint32_t y = 0; y < 480; y++) {
		for (uint32_t x = 0; x < 640; x++) {
			uint32_t expected = ST_PIXEL_COLOR_RGB_TAG;

			if (x >= 11 && x <= 13 && y >= 21 && y <= 23)
				expected = ST_PIXEL_COLOR_RGB_VALUE(170, 170, 170);
			if (x == 11 && y == 21)
				expected = ST_PIXEL_COLOR_RGB_VALUE(0, 170, 0);
			if (x == 12 && y == 22)
				expected = ST_PIXEL_COLOR_RGB_VALUE(170, 0, 170);
			if (x == 13 && y == 23)
				expected = ST_PIXEL_COLOR_RGB_VALUE(170, 0, 0);
			if (x >= 30 && x <= 32 && y >= 40 && y <= 42)
				expected = ST_PIXEL_COLOR_RGB_VALUE(0, 0, 170);
			if (x == 31 && y == 41)
				expected = ST_PIXEL_COLOR_RGB_VALUE(170, 85, 0);
			if (x == 32 && y == 42)
				expected = ST_PIXEL_COLOR_RGB_VALUE(255, 255, 85);
			CHECK(capture->primary[(size_t)y * 640 + x] == expected);
		}
	}
	st_pixel_buffer_destroy(capture);
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
lifecycle(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_port_info info;
	struct st_rip3_rect bounds = {0, 0, 2, 2};
	struct st_rip3_rect view = {1, 1, 2, 2};
	unsigned current;
	uint8_t value = 77;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	for (unsigned i = 1; i < 36; i++) {
		CHECK(st_rip3_ports_info(ports, i, &info) == 0 && !info.defined);
		CHECK(st_rip3_ports_define(ports, i, &bounds, true) == 0);
	}
	CHECK(st_rip3_ports_switch(ports, 35) == 0);
	CHECK(st_rip3_ports_viewport(ports, &view) == 0);
	CHECK(st_rip3_ports_activate(ports, false) == 0);
	CHECK(st_rip3_ports_plot(ports, 0, 0, 7, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_read(ports, 35, 1, 1, &value) == 0 && value == 0);
	CHECK(st_rip3_ports_activate(ports, true) == 0);
	CHECK(st_rip3_ports_plot(ports, 0, 0, 7, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_protect(ports, 35, true) == 0);
	CHECK(st_rip3_ports_viewport(ports, &view) == IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_ports_activate(ports, false) == IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_ports_delete(ports, 35) == IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_ports_current(ports, &current) == 0 && current == 35);
	CHECK(st_rip3_ports_erase(ports, 6) == 0);
	CHECK(st_rip3_ports_read(ports, 35, 1, 1, &value) == 0 && value == 6);
	CHECK(st_rip3_ports_read(ports, 35, 0, 0, &value) == 0 && value == 0);
	CHECK(st_rip3_ports_define(ports, 35, &bounds, false) == IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_ports_current(ports, &current) == 0 && current == 0);
	CHECK(st_rip3_ports_switch(ports, 35) == 0);
	CHECK(st_rip3_ports_delete(ports, 0) == 0);
	CHECK(st_rip3_ports_current(ports, &current) == 0 && current == 35);
	for (unsigned i = 1; i < 35; i++)
		CHECK(st_rip3_ports_info(ports, i, &info) == 0 && !info.defined);
	CHECK(st_rip3_ports_reset(ports, false) == 0);
	CHECK(st_rip3_ports_current(ports, &current) == 0 && current == 0);
	CHECK(st_rip3_ports_info(ports, 35, &info) == 0 && info.defined && info.protected);
	CHECK(st_rip3_ports_read(ports, 35, 1, 1, &value) == 0 && value == 6);
	CHECK(st_rip3_ports_switch(ports, 35) == 0);
	CHECK(st_rip3_ports_protect(ports, 35, false) == 0);
	CHECK(st_rip3_ports_delete(ports, 35) == 0);
	CHECK(st_rip3_ports_current(ports, &current) == 0 && current == 0);
	CHECK(st_rip3_ports_switch(ports, 35) == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_ports_define(ports, 35, &bounds, true) == 0);
	CHECK(st_rip3_ports_protect(ports, 35, true) == 0);
	struct st_rip3_clipboard_selection clipboard={.set=true,.port=0};
	st_rip3_ports_bind_clipboard(ports,&clipboard);
	CHECK(st_rip3_ports_reset(ports, true) == 0);
	CHECK(!clipboard.set && st_rip3_ports_clipboard_index(ports)==-1);
	CHECK(st_rip3_ports_info(ports, 35, &info) == 0 && !info.defined && !info.protected);
	CHECK(st_rip3_ports_info(ports, 0, &info) == 0 && info.viewport_active);
	CHECK(info.viewport.x1 == 639 && info.viewport.y1 == 479);
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
copies(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_rect left = {10, 10, 13, 10};
	struct st_rip3_rect right = {11, 10, 14, 10};
	struct st_rip3_rect point = {0, 0, 0, 0};
	const uint8_t expected[] = {1, 1, 2, 4, 8};
	uint8_t value;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_800_600) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &left, false) == 0);
	CHECK(st_rip3_ports_define(ports, 2, &right, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	for (int32_t x = 0; x < 4; x++)
		CHECK(st_rip3_ports_plot(ports, x, 0, (uint8_t)(1 << x), ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_protect(ports, 1, true) == 0);
	CHECK(st_rip3_ports_protect(ports, 2, true) == 0);
	/* Different screen ports alias the SAME bitmap. Copy before overwriting. */
	CHECK(st_rip3_ports_copy(ports, 1, NULL, 2, NULL, ST_RIP3_TRANSFER_COPY) == 0);
	for (int32_t x = 0; x < 5; x++) {
		CHECK(st_rip3_ports_read(ports, 0, x + 10, 10, &value) == 0);
		CHECK(value == expected[x]);
	}
	CHECK(st_rip3_ports_copy(ports, 35, NULL, 0, &point,
	    ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_ports_copy(ports, 1, NULL, 35, NULL,
	    ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_ports_protect(ports, 1, false) == 0);
	CHECK(st_rip3_ports_activate(ports, false) == 0);
	CHECK(st_rip3_ports_copy(ports, 1, NULL, 0, &point,
	    ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_ports_read(ports, 0, 0, 0, &value) == 0 && value == 0);
	CHECK(st_rip3_ports_activate(ports, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 2) == 0);
	CHECK(st_rip3_ports_protect(ports, 2, false) == 0);
	CHECK(st_rip3_ports_activate(ports, false) == 0);
	CHECK(st_rip3_ports_copy(ports, 1, &point, 2, NULL,
	    ST_RIP3_TRANSFER_INVERT) == 0);
	for (int32_t x = 0; x < 5; x++) {
		CHECK(st_rip3_ports_read(ports, 0, x + 10, 10, &value) == 0);
		CHECK(value == expected[x]);
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
boundaries(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_ports *sentinel = (struct st_rip3_ports *)(uintptr_t)1;
	struct st_rip3_port_info info;
	struct st_rip3_rect bounds = {-10, -20, 4, 4};
	struct st_rip3_rect clipped_view = {-1, -2, 10, 20};
	struct st_rip3_rect outside = {1000, 1000, 2000, 2000};
	struct st_rip3_rect invalid = {4, 4, 0, 0};
	struct st_rip3_rect enormous = {0, 0, INT32_MAX, INT32_MAX};
	unsigned current;
	uint8_t value = 77;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_1024_768) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, false) == 0);
	CHECK(st_rip3_ports_info(ports, 1, &info) == 0);
	CHECK(info.bounds.x0 == 0 && info.bounds.y0 == 0
	    && info.bounds.x1 == 4 && info.bounds.y1 == 4);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ports_viewport(ports, &clipped_view) == 0);
	CHECK(st_rip3_ports_info(ports, 1, &info) == 0);
	CHECK(info.viewport.x0 == 0 && info.viewport.y0 == 0
	    && info.viewport.x1 == 4 && info.viewport.y1 == 4);
	CHECK(st_rip3_ports_plot(ports, 0, 0, 7, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &outside, false) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_ports_define(ports, 1, &enormous, true) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_ports_define(ports, 1, &invalid, false) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_ports_define(ports, 0, &bounds, false) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_ports_define(ports, 36, &bounds, false) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_ports_protect(ports, 0, true) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_ports_viewport(ports, &outside) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_ports_viewport(ports, &invalid) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_ports_fill(ports, &invalid, 1, ST_RIP3_COPY) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_ports_copy(ports, 1, &invalid, 0, NULL,
	    ST_RIP3_TRANSFER_COPY) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_ports_current(ports, &current) == 0 && current == 1);
	CHECK(st_rip3_ports_read(ports, 1, 0, 0, &value) == 0 && value == 7);
	CHECK(st_rip3_ports_info(ports, 1, &info) == 0);
	CHECK(info.defined && !info.offscreen && info.viewport_active && !info.protected);
	value = 77;
	CHECK(st_rip3_ports_read(ports, 35, 0, 0, &value) == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_ports_read(ports, 1, -1, 0, &value) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_ports_read(ports, 1, 5, 0, &value) == IO_ERROR_OUT_OF_RANGE);
	CHECK(value == 77);
	CHECK(st_rip3_ports_switch(ports, 35) == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_ports_current(ports, &current) == 0 && current == 0);
	CHECK(st_rip3_ports_switch(ports, 36) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_ports_create(&sentinel, (enum st_rip3_canvas)3) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(sentinel == (struct st_rip3_ports *)(uintptr_t)1);
	CHECK(st_rip3_ports_reset(ports, false) == 0);
	CHECK(st_rip3_ports_read(ports, 0, 0, 0, &value) == 0 && value == 7);
	/* The reference's explicit viewport-origin calculation: screen 25 +
	 * port-relative view 40 + view-relative point 50 = 115, not 125. */
	bounds = (struct st_rip3_rect){25, 25, 225, 225};
	clipped_view = (struct st_rip3_rect){40, 40, 170, 170};
	CHECK(st_rip3_ports_define(ports, 2, &bounds, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 2) == 0);
	CHECK(st_rip3_ports_viewport(ports, &clipped_view) == 0);
	CHECK(st_rip3_ports_plot(ports, 50, 50, 15, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_read(ports, 0, 115, 115, &value) == 0 && value == 15);
	CHECK(st_rip3_ports_read(ports, 0, 125, 125, &value) == 0 && value == 0);
	st_rip3_ports_destroy(ports);
	st_rip3_ports_destroy(NULL);
	return 0;
}

static int
install_bitmap(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_surface *bitmap = NULL;
	const struct st_rip3_rect bounds = {1,2,3,4};
	unsigned current; uint8_t pixel;
	CHECK(st_rip3_ports_create(&ports,ST_RIP3_CANVAS_640_480) == 0
	    && st_rip3_ports_define(ports,1,&bounds,false) == 0
	    && st_rip3_ports_define(ports,2,&bounds,false) == 0
	    && st_rip3_ports_switch(ports,1) == 0);
	CHECK(st_rip3_surface_create(&bitmap,2,1) == 0
	    && st_rip3_surface_plot(bitmap,NULL,0,0,7,ST_RIP3_COPY) == 0
	    && st_rip3_surface_plot(bitmap,NULL,1,0,9,ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_install_bitmap(ports,0,&bitmap) == IO_ERROR_OUT_OF_RANGE && bitmap != NULL);
	CHECK(st_rip3_ports_install_bitmap(ports,36,&bitmap) == IO_ERROR_OUT_OF_RANGE && bitmap != NULL);
	CHECK(st_rip3_ports_protect(ports,2,true) == 0);
	CHECK(st_rip3_ports_install_bitmap(ports,2,&bitmap) == IO_ERROR_PERMISSION_DENIED && bitmap != NULL);
	CHECK(st_rip3_ports_current(ports,&current) == 0 && current == 1);
	CHECK(st_rip3_surface_get(bitmap,1,0,&pixel) == 0 && pixel == 9);
	CHECK(st_rip3_ports_protect(ports,2,false) == 0 && st_rip3_ports_install_bitmap(ports,2,&bitmap) == 0 && bitmap == NULL);
	CHECK(st_rip3_ports_read(ports,2,0,0,&pixel) == 0 && pixel == 7);
	CHECK(st_rip3_ports_read(ports,2,1,0,&pixel) == 0 && pixel == 9);
	CHECK(st_rip3_ports_current(ports,&current) == 0 && current == 1);
	CHECK(st_rip3_ports_install_bitmap(ports,2,&bitmap) == IO_ERROR_INVALID_ARGUMENT);
	st_rip3_ports_destroy(ports); return 0;
}

static int
fixed_clipboard_face(void)
{
	struct st_rip3_ports *ports=NULL;CHECK(st_rip3_ports_create_device(&ports,16,8)==0);
	const struct st_rip3_rect source={0,0,1,0},face={1,0,5,2};
	CHECK(st_rip3_ports_define(ports,1,&source,false)==0);
	CHECK(st_rip3_ports_plot(ports,0,0,7,ST_RIP3_COPY)==0 && st_rip3_ports_plot(ports,1,0,9,ST_RIP3_COPY)==0);
	CHECK(st_rip3_ports_button_clipboard(ports,1,&face)==0);
	uint8_t value;CHECK(st_rip3_ports_read(ports,0,1,0,&value)==0 && value==7);
	CHECK(st_rip3_ports_read(ports,0,2,0,&value)==0 && value==9);
	for (int y=0;y<2;y++) for (int x=1;x<5;x++) if (y!=0 || x>=3)
		CHECK(st_rip3_ports_read(ports,0,x,y,&value)==0 && value==0);
	st_rip3_ports_destroy(ports);return 0;
}

static int
destination_generations(void)
{
	struct st_rip3_ports *ports=NULL;struct st_rip3_backup *saved=NULL;
	CHECK(st_rip3_ports_create_device(&ports,64,48)==0);
	uint64_t screen=st_rip3_ports_generation(ports,0);
	CHECK(screen!=0 && st_rip3_ports_generation(ports,36)==0 && st_rip3_ports_generation(ports,6)==0);
	const struct st_rip3_rect bounds={0,0,15,15},view={1,2,8,9};
	CHECK(st_rip3_ports_define(ports,6,&bounds,true)==0);
	uint64_t original=st_rip3_ports_generation(ports,6);CHECK(original!=0 && original!=screen);
	CHECK(st_rip3_ports_switch(ports,6)==0 && st_rip3_ports_viewport(ports,&view)==0);
	CHECK(st_rip3_ports_protect(ports,6,true)==0 && st_rip3_ports_generation(ports,6)==original);
	CHECK(st_rip3_ports_define(ports,6,&bounds,true)==IO_ERROR_PERMISSION_DENIED && st_rip3_ports_generation(ports,6)==original);
	CHECK(st_rip3_ports_protect(ports,6,false)==0 && st_rip3_ports_define(ports,6,&bounds,true)==0
	    && st_rip3_ports_generation(ports,6)!=original && st_rip3_ports_generation(ports,0)==screen);
	original=st_rip3_ports_generation(ports,6);
	CHECK(st_rip3_ports_backup_create(&saved)==0 && st_rip3_backup_save(saved,ST_RIP3_BACKUP_BASE,0,ports)==0);
	CHECK(st_rip3_ports_delete(ports,6)==0 && st_rip3_ports_generation(ports,6)==0);
	CHECK(st_rip3_backup_restore(saved,ST_RIP3_BACKUP_BASE,0,ports)==0
	    && st_rip3_ports_generation(ports,6)!=original && st_rip3_ports_generation(ports,0)!=screen);
	screen=st_rip3_ports_generation(ports,0);
	CHECK(st_rip3_ports_reset(ports,false)==0 && st_rip3_ports_generation(ports,0)!=screen);
	st_rip3_backup_destroy(saved);st_rip3_ports_destroy(ports);return 0;
}


static int
header_clipboard_delete(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_clipboard_selection clip = {0};
	struct st_rip3_port_info info;
	unsigned current;
	const struct st_rip3_rect bitmap = {0, 0, 7, 3};

	CHECK(st_rip3_ports_create_device(&ports, 64, 48) == 0);
	st_rip3_ports_bind_clipboard(ports, &clip);
	CHECK(st_rip3_ports_define(ports, 5, &bitmap, true) == 0);
	CHECK(st_rip3_ports_select(ports, 7) == 0);

	/* A stale zero assignment must not run the ordinary delete-all path. */
	clip = (struct st_rip3_clipboard_selection){.set=true, .port=0};
	CHECK(st_rip3_ports_header_delete_clipboard(ports) == IO_ERROR_PERMISSION_DENIED);
	CHECK(!clip.set && st_rip3_ports_info(ports, 0, &info) == 0 && info.defined);
	CHECK(st_rip3_ports_info(ports, 5, &info) == 0 && info.defined);
	clip = (struct st_rip3_clipboard_selection){.set=true, .port=35};
	CHECK(st_rip3_ports_header_delete_clipboard(ports) == 0 && !clip.set);
	CHECK(st_rip3_ports_current(ports, &current) == 0 && current == 7);

	clip = (struct st_rip3_clipboard_selection){.set=true, .port=5};
	CHECK(st_rip3_ports_protect(ports, 5, true) == 0);
	CHECK(st_rip3_ports_header_delete_clipboard(ports) == 0 && clip.set && clip.port == 5);
	CHECK(st_rip3_ports_switch(ports, 5) == 0 && st_rip3_ports_protect(ports, 5, false) == 0);
	CHECK(st_rip3_ports_activate(ports, false) == 0);
	CHECK(st_rip3_ports_header_delete_clipboard(ports) == 0 && !clip.set);
	CHECK(st_rip3_ports_current(ports, &current) == 0 && current == 0);
	CHECK(st_rip3_ports_info(ports, 5, &info) == 0 && !info.defined);
	st_rip3_ports_destroy(ports);
	return 0;
}

int
main(void)
{
	CHECK(layout() == 0);
	CHECK(lifecycle() == 0);
	CHECK(copies() == 0);
	CHECK(boundaries() == 0 && install_bitmap() == 0);
	CHECK(screen_fill()==0 && header_clipboard_delete()==0);
	CHECK(captured_view()==0 && destination_generations()==0 && fixed_clipboard_face()==0);
	return 0;
}
