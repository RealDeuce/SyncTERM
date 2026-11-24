/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_wallpaper.h"
#include "st_rip3_bitmap_stamp.h"
#include "st_rip3_ports.h"
#include "io_error.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern bool st_rip3_wallpaper_test_fail_allocate;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr,"check failed at line %d: %s\n",__LINE__,#expression); \
		return 1; \
	} \
} while (0)

static int
scene(bool stagger)
{
	struct st_rip3_ports *ports = NULL;
	const struct st_rip3_rect bounds = {10,20,19,28}, viewport = {2,1,7,5};
	const struct st_rip3_bitmap_destination seed = {2,3,4,4};
	struct st_rip3_port_info before, after;
	/* Authored 3x2 tile 1,2,3 / 4,5,6; phase extends above and left.
	 * Odd rows shift left one pixel, relative to the seed row. */
	const uint8_t plain[2][3] = {{5,6,4},{2,3,1}};
	const uint8_t shifted[4][3] = {{5,6,4},{3,1,2},{6,4,5},{2,3,1}};
	CHECK(st_rip3_ports_create(&ports,ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports,1,&bounds,false) == 0 && st_rip3_ports_switch(ports,1) == 0);
	CHECK(st_rip3_ports_viewport(ports,&viewport) == 0 && st_rip3_ports_erase(ports,85) == 0);
	for (int32_t y = 0; y < 2; y++) for (int32_t x = 0; x < 3; x++)
		CHECK(st_rip3_ports_plot(ports,2+x,3+y,(uint8_t)(y*3+x+1),ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_protect(ports,1,true) == 0 && st_rip3_ports_info(ports,1,&before) == 0);
	CHECK(st_rip3_wallpaper(ports,&seed,stagger,NULL) == 0);
	CHECK(st_rip3_ports_info(ports,1,&after) == 0 && memcmp(&before,&after,sizeof(before)) == 0);
	unsigned current; CHECK(st_rip3_ports_current(ports,&current) == 0 && current == 1);
	/* Check the whole screen, including untouched pixels outside the viewport. */
	for (int32_t y = 0; y < 480; y++) for (int32_t x = 0; x < 640; x++) {
		uint8_t actual, expected = 0;
		if (x >= 12 && x <= 17 && y >= 21 && y <= 25)
			expected = stagger ? shifted[(y-21)%4][(x-12)%3] : plain[(y-21)%2][(x-12)%3];
		CHECK(st_rip3_ports_read(ports,0,x,y,&actual) == 0 && actual == expected);
	}
	st_rip3_ports_destroy(ports); return 0;
}

static int
physical_backing(void)
{
	const struct {
		uint32_t width, height;
		struct st_rip3_rect viewport;
		struct st_rip3_bitmap_destination seed;
		bool stagger, offscreen;
		const char *expected;
	} cases[] = {
		{6,1,{0,0,7,0},{4,0,7,0},false,false,"ABGGAB"},
		{6,3,{0,0,7,2},{4,1,7,1},true,true,"GGABGGABGGABGGABGG"},
		{6,1,{0,0,7,0},{6,0,7,0},false,false,"GGGGGG"},
		{4,3,{0,0,3,3},{2,2,3,3},false,true,"ABABGGGGABAB"},
	};
	for (size_t i = 0; i < sizeof(cases)/sizeof(*cases); i++) {
		struct st_rip3_ports *ports = NULL;
		CHECK(st_rip3_ports_create_device(&ports,cases[i].width,cases[i].height) == 0);
		unsigned port = cases[i].offscreen ? 1 : 0;
		if (port != 0) {
			const struct st_rip3_rect bounds = {0,0,(int32_t)cases[i].width-1,(int32_t)cases[i].height-1};
			CHECK(st_rip3_ports_define(ports,port,&bounds,true) == 0);
			CHECK(st_rip3_ports_switch(ports,port) == 0);
		}
		CHECK(st_rip3_ports_viewport_device(ports,&cases[i].viewport) == 0);
		CHECK(st_rip3_ports_erase(ports,'G') == 0);
		const struct st_rip3_rect from = {0,0,0,0}, to = {1,0,1,0};
		struct st_rip3_port_copy_pair before, after;
		CHECK(st_rip3_ports_copy(ports,port,&from,port,&to,ST_RIP3_TRANSFER_COPY) == 0);
		CHECK(st_rip3_ports_copy_pair(ports,port,&before) == 0 && before.set);
		int32_t left = (int32_t)cases[i].seed.x0, top = (int32_t)cases[i].seed.y0;
		bool backed = (uint32_t)left < cases[i].width;
		if (backed) {
			CHECK(st_rip3_ports_plot(ports,left,top,'A',ST_RIP3_COPY) == 0);
			CHECK(st_rip3_ports_plot(ports,left+1,top,'B',ST_RIP3_COPY) == 0);
		}
		/* An entirely unbacked seed succeeds without allocating a patch. */
		st_rip3_wallpaper_test_fail_allocate = !backed;
		CHECK(st_rip3_wallpaper(ports,&cases[i].seed,cases[i].stagger,NULL) == 0);
		CHECK(st_rip3_wallpaper_test_fail_allocate == !backed);
		st_rip3_wallpaper_test_fail_allocate = false;
		CHECK(st_rip3_ports_copy_pair(ports,port,&after) == 0 && after.set);
		CHECK(memcmp(&before.source,&after.source,sizeof(before.source)) == 0);
		CHECK(memcmp(&before.destination,&after.destination,sizeof(before.destination)) == 0);
		for (uint32_t y = 0; y < cases[i].height; y++) for (uint32_t x = 0; x < cases[i].width; x++) {
			uint8_t actual;
			CHECK(st_rip3_ports_read(ports,port,(int32_t)x,(int32_t)y,&actual) == 0);
			CHECK(actual == (uint8_t)cases[i].expected[y*cases[i].width+x]);
		}
		st_rip3_ports_destroy(ports);
	}
	return 0;
}

static int
edges(void)
{
	struct st_rip3_ports *ports = NULL;
	const struct st_rip3_rect viewport = {0,0,5,4};
	const struct st_rip3_bitmap_destination seed = {3,2,10,10};
	/* Far-edge clipping makes this a 3x3 tile, not the requested 8x9. */
	const uint8_t expected[5][6] = {{4,5,6,4,5,6},{7,8,9,7,8,9},
	    {1,2,3,1,2,3},{4,5,6,4,5,6},{7,8,9,7,8,9}};
	CHECK(st_rip3_ports_create(&ports,ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_viewport(ports,&viewport) == 0 && st_rip3_ports_erase(ports,85) == 0);
	for (int32_t y = 0; y < 3; y++) for (int32_t x = 0; x < 3; x++)
		CHECK(st_rip3_ports_plot(ports,3+x,2+y,(uint8_t)(3*y+x+1),ST_RIP3_COPY) == 0);
	st_rip3_wallpaper_test_fail_allocate = true;
	CHECK(st_rip3_wallpaper(ports,&seed,false,NULL) == IO_ERROR_OUT_OF_MEMORY && !st_rip3_wallpaper_test_fail_allocate);
	for (int32_t y = 0; y < 5; y++) for (int32_t x = 0; x < 6; x++) {
		uint8_t actual, original = x >= 3 && y >= 2 ? (uint8_t)(3*(y-2)+x-2) : 85;
		CHECK(st_rip3_ports_read(ports,0,x,y,&actual) == 0 && actual == original);
	}
	const struct st_rip3_bitmap_destination invalid[] = {{-1,0,1,1},{0,-1,1,1},{6,0,7,1},{0,5,1,6},{1,0,0,1}};
	for (size_t i = 0; i < sizeof(invalid)/sizeof(*invalid); i++) CHECK(st_rip3_wallpaper(ports,invalid+i,false,NULL) != 0);
	CHECK(st_rip3_wallpaper(ports,&seed,false,NULL) == 0);
	for (int32_t y = 0; y < 5; y++) for (int32_t x = 0; x < 6; x++) {
		uint8_t actual; CHECK(st_rip3_ports_read(ports,0,x,y,&actual) == 0 && actual == expected[y][x]);
	}
	/* Covering the viewport needs no staging. Inactive ports likewise no-op. */
	const struct st_rip3_bitmap_destination full = {0,0,20,20};
	st_rip3_wallpaper_test_fail_allocate = true;
	CHECK(st_rip3_wallpaper(ports,&full,true,NULL) == 0 && st_rip3_wallpaper_test_fail_allocate);
	CHECK(st_rip3_ports_activate(ports,false) == 0);
	CHECK(st_rip3_wallpaper(ports,&seed,true,NULL) == 0 && st_rip3_wallpaper_test_fail_allocate);
	st_rip3_wallpaper_test_fail_allocate = false;
	st_rip3_ports_destroy(ports); return 0;
}

int
main(void)
{
	if (scene(false) || scene(true) || edges() || physical_backing()) return 1;
	struct st_rip3_ports *ports = NULL;
	const struct st_rip3_rect viewport = {0,0,5,4};
	const struct st_rip3_bitmap_destination even = {1,1,4,2}, single = {2,3,2,3};
	const uint8_t expected[5][6] = {{6,7,8,5,6,7},{4,1,2,3,4,1},
	    {8,5,6,7,8,5},{2,3,4,1,2,3},{6,7,8,5,6,7}};
	CHECK(st_rip3_ports_create(&ports,ST_RIP3_CANVAS_640_480) == 0 && st_rip3_ports_viewport(ports,&viewport) == 0);
	for (int32_t y = 0; y < 2; y++) for (int32_t x = 0; x < 4; x++)
		CHECK(st_rip3_ports_plot(ports,1+x,1+y,(uint8_t)(4*y+x+1),ST_RIP3_COPY) == 0);
	CHECK(st_rip3_wallpaper(ports,&even,true,NULL) == 0);
	for (int32_t y = 0; y < 5; y++) for (int32_t x = 0; x < 6; x++) {
		uint8_t actual; CHECK(st_rip3_ports_read(ports,0,x,y,&actual) == 0 && actual == expected[y][x]);
	}
	CHECK(st_rip3_ports_plot(ports,2,3,73,ST_RIP3_COPY) == 0 && st_rip3_wallpaper(ports,&single,true,NULL) == 0);
	for (int32_t y = 0; y < 5; y++) for (int32_t x = 0; x < 6; x++) {
		uint8_t actual; CHECK(st_rip3_ports_read(ports,0,x,y,&actual) == 0 && actual == 73);
	}
	st_rip3_ports_destroy(ports); return 0;
}
