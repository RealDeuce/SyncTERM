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
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

struct observation { unsigned errors; enum st_rip3_command_diagnostic kind; };

static void
diagnostic(void *context, enum st_rip3_command_diagnostic kind)
{
	struct observation *seen = context;
	seen->errors++;
	seen->kind = kind;
}

static int
process(const struct st_rip3_command_host *host, const char *command, int error)
{
	struct observation seen = {0};
	const struct st_rip3_command_sink sink = {&seen,diagnostic};
	st_rip3_commands_process(host, &sink, (const uint8_t *)command, strlen(command));
	if (seen.errors != (error < 0 ? 0u : 1u) || (error >= 0 && (int)seen.kind != error))
		fprintf(stderr, "command %s: errors %u, kind %d, expected %d\n", command, seen.errors, (int)seen.kind, error);
	CHECK(seen.errors == (error < 0 ? 0u : 1u));
	if (error >= 0) CHECK((int)seen.kind == error);
	return 0;
}

static int
create(struct st_rip3_command_host *host, enum st_rip3_canvas canvas, uint32_t world_width, uint32_t world_height)
{
	struct st_rip3_environment environment;
	CHECK(st_rip3_environments_create(&host->environments, canvas) == 0);
	CHECK(st_rip3_ports_create(&host->ports, canvas) == 0);
	CHECK(st_rip3_environments_get(host->environments, 0, &environment) == 0);
	environment.world_width = world_width; environment.world_height = world_height;
	CHECK(st_rip3_environments_set(host->environments, 0, &environment) == 0);
	return 0;
}

static void
destroy(struct st_rip3_command_host *host)
{
	st_rip3_ports_destroy(host->ports);
	st_rip3_environments_destroy(host->environments);
}

static void
gray_palette(struct st_rip3_palette *palette)
{
	for (unsigned i = 0; i < 256; i++) palette->rgb[i] = i * UINT32_C(0x010101);
}

static int
row(struct st_rip3_ports *ports, unsigned number, int32_t x, int32_t y,
    const uint8_t *expected, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		uint8_t actual = 0;
		CHECK(st_rip3_ports_read(ports, number, x+(int32_t)i, y, &actual) == 0 && actual == expected[i]);
	}
	return 0;
}

struct stream_scene { const struct st_rip3_command_host *host; unsigned errors; };

static void
scene_error(void *context, enum st_rip3_command_diagnostic kind)
{
	struct stream_scene *scene = context;
	(void)kind;
	scene->errors++;
}

static void
scene_command(void *context, const uint8_t *bytes, size_t length)
{
	struct stream_scene *scene = context;
	const struct st_rip3_command_sink sink = {scene,scene_error};
	st_rip3_commands_process(scene->host, &sink, bytes, length);
}

static uint8_t
transferred(uint8_t pixel, unsigned mode)
{
	switch (mode) {
		case 0: return pixel;
		case 1: return pixel^0x55;
		case 2: return pixel|0x55;
		case 3: return pixel&0x55;
		default: return (uint8_t)~pixel;
	}
}

static int
clipping(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	/* Literal geometry/pixel oracles, authored independently of the copier.
	 * 0 denotes retained destination background, never a source pixel here. */
	static const struct {
		const char *source, *destination;
		uint8_t pixels[4];
	} cases[] = {
		{"00000000","01000000",{0,1,0,5}},
		{"00000000","00010000",{0,0,1,2}},
		{"00000000","00000000",{6,8,14,16}},
		{"00000000","01000504",{0,1,0,5}},
		{"00000000","01000404",{0,7,0,15}},
		{"01000504","00000404",{2,3,6,7}},
		{"01000504","00000304",{6,8,14,16}},
		{"00000101","00000000",{1,1,1,1}},
		{"00000000","01010202",{0,0,0,11}},
		{"01010505","00000000",{6,8,14,16}},
	};
	for (unsigned screen_source = 0; screen_source <= 1; screen_source++) {
		for (unsigned mode = 0; mode < 5; mode++) {
			struct st_rip3_command_host host = {0};
			struct st_rip3_palette palette;
			struct st_pixel_buffer *capture = NULL;
			const struct st_rip3_rect source = {8,8,13,13}, source_view = {1,1,4,4};
			const struct st_rip3_rect destination = {30,20,79,25};
			char body[64];
			CHECK(create(&host,canvas,width,height) == 0);
			CHECK(st_rip3_ports_define(host.ports,1,&source,screen_source == 0) == 0);
			CHECK(st_rip3_ports_switch(host.ports,1) == 0);
			CHECK(st_rip3_ports_viewport(host.ports,&source_view) == 0);
			for (int32_t y = 0; y < 4; y++) for (int32_t x = 0; x < 4; x++)
				CHECK(st_rip3_ports_plot(host.ports,x,y,(uint8_t)(1+x+4*y),ST_RIP3_COPY) == 0);
			CHECK(st_rip3_ports_protect(host.ports,1,true) == 0);
			CHECK(st_rip3_ports_define(host.ports,2,&destination,false) == 0);
			for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
				struct st_rip3_rect view = {2+4*(int32_t)i,1,3+4*(int32_t)i,2};
				CHECK(st_rip3_ports_protect(host.ports,2,false) == 0);
				CHECK(st_rip3_ports_switch(host.ports,2) == 0);
				CHECK(st_rip3_ports_viewport(host.ports,&view) == 0);
				CHECK(st_rip3_ports_erase(host.ports,0x55) == 0);
				CHECK(st_rip3_ports_protect(host.ports,2,true) == 0);
				CHECK(st_rip3_ports_switch(host.ports,0) == 0);
				(void)snprintf(body,sizeof(body),"2C1%s2%s%u",cases[i].source,cases[i].destination,mode);
				CHECK(process(&host,body,-1) == 0);
			}
			gray_palette(&palette);
			CHECK(st_rip3_ports_snapshot(host.ports,&palette,&capture) == 0);
			/* One coherent full-device capture holds every related rectangle. */
			for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
				uint8_t expected = 0;
				if (screen_source && x >= 9 && x <= 12 && y >= 9 && y <= 12)
					expected = (uint8_t)(1+x-9+4*(y-9));
				if (x >= 32 && x < 72 && y >= 21 && y <= 22 && (x-32)%4 < 2) {
					size_t i = (x-32)/4, pixel = 2*(y-21)+(x-32)%4;
					expected = cases[i].pixels[pixel] == 0 ? 0x55 : transferred(cases[i].pixels[pixel],mode);
				}
				if (capture->primary[(size_t)y*width+x] != (ST_PIXEL_COLOR_RGB_TAG|expected*UINT32_C(0x010101)))
					fprintf(stderr,"clipping screen %u mode %u at %u,%u: got %08x, expected index %u\n",
					    screen_source,mode,x,y,(unsigned)capture->primary[(size_t)y*width+x],expected);
				CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG|expected*UINT32_C(0x010101)));
			}
			unsigned current;
			struct st_rip3_port_info info;
			CHECK(st_rip3_ports_current(host.ports,&current) == 0 && current == 0);
			CHECK(st_rip3_ports_info(host.ports,1,&info) == 0 && info.protected
			    && info.viewport_active && info.viewport.x0 == 1 && info.viewport.y0 == 1
			    && info.viewport.x1 == 4 && info.viewport.y1 == 4);
			for (int32_t y = 0; y < 4; y++) for (int32_t x = 0; x < 4; x++) {
				uint8_t index;
				CHECK(st_rip3_ports_read(host.ports,1,x+1,y+1,&index) == 0 && index == 1+x+4*y);
			}
			st_pixel_buffer_destroy(capture); destroy(&host);
		}
	}
	return 0;
}

static int
geometry_matrix(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	const char digits[] = "0123456789ABCDEF";
	const struct st_rip3_rect bounds = {0,0,11,3}, screen_bounds = {10,20,21,23};
	for (unsigned offscreen = 0; offscreen <= 1; offscreen++) {
		struct st_rip3_command_host host = {0};
		CHECK(create(&host,canvas,width,height) == 0);
		CHECK(st_rip3_ports_define(host.ports,1,&bounds,true) == 0);
		CHECK(st_rip3_ports_define(host.ports,2,offscreen ? &bounds : &screen_bounds,offscreen != 0) == 0);
		for (unsigned sw = 1; sw <= 8; sw++) {
			struct st_rip3_rect source_view = {2,1,(int32_t)sw+1,1};
			CHECK(st_rip3_ports_protect(host.ports,1,false) == 0);
			CHECK(st_rip3_ports_switch(host.ports,1) == 0);
			CHECK(st_rip3_ports_viewport(host.ports,&source_view) == 0);
			CHECK(st_rip3_ports_protect(host.ports,1,true) == 0);
			for (unsigned dw = 1; dw <= 8; dw++) {
				struct st_rip3_rect destination_view = {1,1,(int32_t)dw,1};
				CHECK(st_rip3_ports_protect(host.ports,2,false) == 0);
				CHECK(st_rip3_ports_switch(host.ports,2) == 0);
				CHECK(st_rip3_ports_viewport(host.ports,&destination_view) == 0);
				CHECK(st_rip3_ports_protect(host.ports,2,true) == 0);
				for (unsigned dx = 0; dx < dw; dx++) {
					for (unsigned form = 0; form < 3; form++) {
						/* The fit fixtures use a uniform source so the expected
						 * coverage makes no claim about Windows sampling. Distinct
						 * unscaled pixels expose accidental refitting after trim. */
						bool fitted = form == 2 || (form == 0 && dx == 0);
						unsigned count = fitted ? dw-dx : sw < dw-dx ? sw : dw-dx;
						CHECK(st_rip3_ports_switch(host.ports,1) == 0);
						for (unsigned x = 0; x < sw; x++)
							CHECK(st_rip3_ports_plot(host.ports,(int32_t)x,0,
							    fitted ? 3 : (uint8_t)(1u<<x),ST_RIP3_COPY) == 0);
						for (unsigned mode = 0; mode < 5; mode++) {
							char body[32];
							CHECK(st_rip3_ports_switch(host.ports,2) == 0);
							CHECK(st_rip3_ports_erase(host.ports,0x55) == 0);
							CHECK(st_rip3_ports_switch(host.ports,0) == 0);
							unsigned end = form == 0 ? 0 : form == 1 ? dx+sw : dw;
							(void)snprintf(body,sizeof(body),"2C10000000020%c000%c0%u%u",
							    digits[dx],digits[end],form == 0 ? 0u : 1u,mode);
							CHECK(process(&host,body,-1) == 0);
							for (unsigned x = 0; x < dw; x++) {
								uint8_t index, expected = 0x55;
								if (x >= dx && x-dx < count)
									expected = transferred(fitted ? 3 : (uint8_t)(1u<<(x-dx)),mode);
								CHECK(st_rip3_ports_read(host.ports,2,1+(int32_t)x,1,&index) == 0 && index == expected);
							}
							unsigned current;
							CHECK(st_rip3_ports_current(host.ports,&current) == 0 && current == 0);
						}
					}
				}
			}
		}
		destroy(&host);
	}
	return 0;
}

static int
converted_sentinels(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_command_host host = {0};
	struct st_rip3_environment environment;
	const struct st_rip3_rect source = {0,0,3,0}, destination = {10,20,11,20};
	const uint8_t pixels[] = {1,2,4,8}, fitted[] = {2,8}, verbatim[] = {0x55,1};
	CHECK(create(&host,canvas,width*2,height*2) == 0);
	CHECK(st_rip3_ports_define(host.ports,1,&source,true) == 0);
	CHECK(st_rip3_ports_define(host.ports,2,&destination,false) == 0);
	CHECK(st_rip3_ports_switch(host.ports,1) == 0);
	for (int32_t x = 0; x < 4; x++) CHECK(st_rip3_ports_plot(host.ports,x,0,pixels[x],ST_RIP3_COPY) == 0);
	CHECK(st_rip3_environments_get(host.environments,0,&environment) == 0);
	for (unsigned radix = 36; radix <= 64; radix += 28) for (unsigned coordinate = 2; coordinate <= 5; coordinate++) {
		environment.base_math = radix; environment.coordinate_size = coordinate;
		CHECK(st_rip3_environments_set(host.environments,0,&environment) == 0);
		for (unsigned form = 0; form < 3; form++) {
			char body[96];
			CHECK(st_rip3_ports_switch(host.ports,2) == 0 && st_rip3_ports_erase(host.ports,0x55) == 0);
			CHECK(st_rip3_ports_switch(host.ports,0) == 0);
			/* Positive raw fields can become the full-source/full-destination
			 * marker, or become zero far fields with a nonzero device origin. */
			(void)snprintf(body,sizeof(body),"2C1%0*u%0*u%0*u%0*u2%0*u%0*u%0*u%0*u0",
			    (int)coordinate,form == 0 ? 1u : 0u,(int)coordinate,0u,
			    (int)coordinate,1u,(int)coordinate,1u,
			    (int)coordinate,form == 2 ? 2u : 0u,(int)coordinate,0u,
			    (int)coordinate,1u,(int)coordinate,1u);
			CHECK(process(&host,body,-1) == 0);
			CHECK(row(host.ports,2,0,0,form == 2 ? verbatim : fitted,2) == 0);
		}
	}
	destroy(&host);
	return 0;
}

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	/* Indexed operands and all transfer results are literal independent bytes.
	 * Mode four must invert SOURCE, unlike drawing-mode destination NOT. */
	static const uint8_t source[] = {3,12,48,192};
	static const uint8_t result[5][4] = {
		{3,12,48,192}, {0x56,0x59,0x65,0x95}, {0x57,0x5d,0x75,0xd5},
		{1,4,16,64}, {0xfc,0xf3,0xcf,0x3f},
	};
	static const uint8_t overlap[] = {1,2,1,2,4,8,16,32};
	/* WORLD is twice DEVICE. Explicit far coordinates below are extent
	 * boundaries; keep the independent historical scene's intended sizes. */
	static const uint8_t input[] =
	    "!|2C1" "00000000" "2" "00000404" "0"
	    "|2C1" "00000000" "2" "08000C04" "5"
	    "|2C1" "00000000" "2" "08000C04" "1"
	    "|2C1" "00000000" "2" "0G000K04" "2\r\n"
	    "!|2C1" "00000000" "2" "0O000S04" "3"
	    "|2C1" "00000000" "2" "0W001004" "4\r"
	    "!|2C1" "00000K0K" "3" "0400140A" "0"
	    "|2C1" "00000202" "4" "00000404" "0\r"
	    "!|2C1" "00000000" "5" "00000000" "0"
	    "|2C1" "00000000" "6" "00000202" "0\r"
	    "!|2C1" "00000000" "2" "02020202" "0"
	    "|2C7" "00000C02" "7" "04000G02" "000000"
	    "|2C9" "00000C02" "A" "00000C02" "0\r";
	struct st_rip3_command_host host = {0};
	struct stream_scene scene = {&host,0};
	const struct st_rip3_stream_sink sink = {.context=&scene,.command=scene_command, .diagnostic=scene_error};
	struct st_rip3_stream *stream = NULL;
	struct st_pixel_buffer *capture = NULL;
	struct st_rip3_palette palette;
	struct st_rip3_port_info info;
	struct st_rip3_environment environment;
	const struct st_rip3_rect source_bounds = {0,0,3,3}, source_view = {1,1,2,2};
	const struct st_rip3_rect bounds[] = {
		{10,20,41,27}, {60,20,67,23}, {80,20,83,23}, {90,20,93,23},
		{100,20,103,23}, {10,40,17,40}, {0,0,0,0}, {10,44,17,44}, {12,44,19,44},
	};
	const struct st_rip3_rect dest_view = {2,1,31,6}, point = {0,0,0,0};
	unsigned current;

	CHECK(create(&host, canvas, width*2, height*2) == 0);
	/* Offscreen sizing is explicit device fixture setup, not 2P wire policy. */
	CHECK(st_rip3_ports_define(host.ports, 1, &source_bounds, true) == 0);
	CHECK(st_rip3_ports_switch(host.ports, 1) == 0);
	CHECK(st_rip3_ports_viewport(host.ports, &source_view) == 0);
	for (int32_t y = 0; y < 2; y++) for (int32_t x = 0; x < 2; x++)
		CHECK(st_rip3_ports_plot(host.ports, x,y, source[y*2+x], ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_protect(host.ports, 1, true) == 0);
	for (unsigned i = 0; i < 9; i++) {
		if (i == 6) continue;
		CHECK(st_rip3_ports_define(host.ports, i+2, bounds+i, false) == 0);
	}
	CHECK(st_rip3_ports_switch(host.ports, 2) == 0);
	CHECK(st_rip3_ports_viewport(host.ports, &dest_view) == 0);
	CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0);
	CHECK(st_rip3_ports_protect(host.ports, 2, true) == 0);
	for (unsigned number = 7; number <= 9; number += 2) {
		CHECK(st_rip3_ports_switch(host.ports, number) == 0);
		for (int32_t x = 0; x < 8; x++)
			CHECK(st_rip3_ports_plot(host.ports, x,0, (uint8_t)(1u << x), ST_RIP3_COPY) == 0);
	}
	CHECK(st_rip3_ports_define(host.ports, 35, &point, true) == 0);
	CHECK(st_rip3_ports_switch(host.ports, 35) == 0);
	CHECK(st_rip3_stream_create(&stream, &sink, host.environments) == 0);
	for (size_t i = 0; i < sizeof(input)-1; i++) st_rip3_stream_process(stream, input+i, 1);
	CHECK(scene.errors == 2); /* Invalid mode and empty geometry were consumed; later copies ran. */
	gray_palette(&palette);
	CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		uint8_t expected = 0;
		if (x >= 12 && x <= 41 && y >= 21 && y <= 26) expected = 0x55;
		for (unsigned mode = 0; mode < 5; mode++) {
			uint32_t left = 12 + mode*4;
			if (x >= left && x <= left+1 && y >= 21 && y <= 22)
				expected = result[mode][(y-21)*2+x-left];
		}
		if (x >= 62 && x <= 67 && y >= 20 && y <= 23)
			expected = source[((y-20)/2)*2 + (x-62)/3];
		if (x >= 80 && x <= 81 && y >= 20 && y <= 21) expected = 3;
		if (x >= 90 && x <= 93 && y >= 20 && y <= 23)
			expected = source[((y-20)/2)*2 + (x-90)/2];
		if (x == 100 && y == 20) expected = 192;
		if (x >= 10 && x <= 17 && (y == 40 || y == 44)) expected = overlap[x-10];
		CHECK(capture->primary[(size_t)y * width+x] == (ST_PIXEL_COLOR_RGB_TAG | expected * UINT32_C(0x010101)));
	}
	CHECK(st_rip3_ports_current(host.ports, &current) == 0 && current == 35);
	CHECK(st_rip3_ports_info(host.ports, 1, &info) == 0 && info.protected
	    && info.viewport.x0 == 1 && info.viewport.y0 == 1 && info.viewport.x1 == 2 && info.viewport.y1 == 2);
	CHECK(st_rip3_ports_info(host.ports, 2, &info) == 0 && info.protected
	    && info.viewport.x0 == 2 && info.viewport.y0 == 1 && info.viewport.x1 == 31 && info.viewport.y1 == 6);
	CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0 && environment.port == 0);
	CHECK(row(host.ports, 1, 1,1, source, 2) == 0 && row(host.ports, 1, 1,2, source+2, 2) == 0);
	st_pixel_buffer_destroy(capture);
	st_rip3_stream_destroy(stream);
	destroy(&host);
	return 0;
}

static int
canonical(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, uint32_t last_x)
{
	struct st_rip3_command_host host = {0};
	struct st_rip3_rect bounds = {0,0,(int32_t)width-1,(int32_t)height-1};
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	CHECK(create(&host, canvas, 1280,960) == 0);
	CHECK(st_rip3_ports_define(host.ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(host.ports, 1) == 0 && st_rip3_ports_erase(host.ports, 7) == 0);
	/* Literal 28 is MegaNum 80, as corrected in the adopted reference. */
	CHECK(process(&host, "2C1XC00ZKQO0000028QO0", -1) == 0);
	gray_palette(&palette);
	CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++)
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | (x <= last_x ? UINT32_C(0x070707) : 0)));
	st_pixel_buffer_destroy(capture);
	destroy(&host);
	return 0;
}

static int
validation(void)
{
	const char *invalid[] = {"2C", "2C100000101000000101", "2C1" "00000101" "0" "00000101" "0" "0000 tail",
	    "2C100000101000000101?", "2C1000001010000001015", "2C10000010100000010100000?",
	    "2C1000001010000001010 comment"};
	struct st_rip3_command_host host = {0}, missing = {0};
	const struct st_rip3_rect bounds = {0,0,7,1};
	const uint8_t baseline[] = {1,2,4,8,16,32,64,128};
	const uint8_t copied[] = {1,2,1,2,4,8,16,32};
	uint8_t value = 0;

	CHECK(create(&host, ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(st_rip3_ports_define(host.ports, 1, &bounds, true) == 0);
	for (int32_t x = 0; x < 8; x++) CHECK(st_rip3_ports_plot(host.ports, x,0, baseline[x], ST_RIP3_COPY) == 0);
	for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
		CHECK(process(&host, invalid[i], ST_RIP3_COMMAND_INVALID) == 0);
		CHECK(row(host.ports, 0, 0,0, baseline, 8) == 0);
	}
	for (unsigned reserved = 0; reserved <= 5; reserved++) {
		char body[48];
		(void)snprintf(body,sizeof(body),"2C1" "00000101" "0" "0A0A0B0B" "0%.*s",(int)reserved,"00000");
		CHECK(process(&host,body,-1) == 0 && row(host.ports,0,0,0,baseline,8) == 0);
	}
	const char *const bad_geometry[] = {"2C1010000010000001010", "2C1000001010010000010",
	    "2C1000001010010101010", "2C1010101010000001010"};
	for (size_t i = 0; i < sizeof(bad_geometry)/sizeof(bad_geometry[0]); i++) {
		CHECK(process(&host,bad_geometry[i],ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
		CHECK(row(host.ports,0,0,0,baseline,8) == 0);
	}
	CHECK(process(&missing, "2C1000001010000001010", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing.ports = host.ports;
	CHECK(process(&missing, "2C1000001010000001010", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&host, "2C2000000000010100000", -1) == 0); /* Undefined source, including zero-endpoint branch. */
	CHECK(process(&host, "2C0000001012000001010", -1) == 0); /* Undefined destination. */
	CHECK(process(&host, "2C0000001010ZYZYZZZZ0", -1) == 0);
	CHECK(process(&host, "2C1ZYZYZZZZ0000001010", -1) == 0); /* Wholly external COPY is explicitly a no-op. */
	CHECK(st_rip3_ports_switch(host.ports, 1) == 0 && st_rip3_ports_activate(host.ports, false) == 0);
	CHECK(process(&host, "2C1000001010000001010", -1) == 0);
	CHECK(row(host.ports, 0, 0,0, baseline, 8) == 0);
	{
		const struct st_rip3_rect target = {100,100,101,101};
		CHECK(st_rip3_ports_define(host.ports, 2, &target, false) == 0);
		CHECK(st_rip3_ports_switch(host.ports, 2) == 0 && st_rip3_ports_activate(host.ports, false) == 0);
		CHECK(process(&host, "2C0000001012000000004", -1) == 0);
		CHECK(st_rip3_ports_read(host.ports, 2, 0,0, &value) == 0 && value == 0);
	}
	/* Overlap allocation failure is atomic and nonfatal. The next command runs. */
	st_rip3_copy_test_fail_malloc = true;
	CHECK(process(&host, "2C0" "00000601" "0" "02000801" "0", ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0);
	CHECK(!st_rip3_copy_test_fail_malloc && row(host.ports, 0, 0,0, baseline, 8) == 0);
	CHECK(process(&host, "2C." "..0601" "." "02.0801" "." "0000Z copied", -1) == 0);
	CHECK(row(host.ports, 0, 0,0, copied, 8) == 0);
	/* Fit-path snapshots obey the same failure transaction as unscaled ones. */
	CHECK(st_rip3_ports_switch(host.ports,0) == 0);
	for (int32_t x = 0; x < 8; x++) CHECK(st_rip3_ports_plot(host.ports,x,0,baseline[x],ST_RIP3_COPY) == 0);
	st_rip3_copy_test_fail_malloc = true;
	CHECK(process(&host,"2C0" "00000801" "0" "02000601" "0",ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0);
	CHECK(!st_rip3_copy_test_fail_malloc && row(host.ports,0,0,0,baseline,8) == 0);
	CHECK(process(&host,"2C0" "00000801" "0" "02000601" "0",-1) == 0);
	const uint8_t fitted[] = {1,2,2,8,32,128,64,128};
	CHECK(row(host.ports,0,0,0,fitted,8) == 0);

	CHECK(st_rip3_ports_read(host.ports, 1, 0,0, &value) == 0 && value == 0);
	destroy(&host);
	return 0;
}

static int
widths_and_radix(void)
{
	struct st_rip3_command_host host = {0};
	const struct st_rip3_rect bounds = {0,0,63,1};
	const uint8_t source[] = {1,2,4};
	char command[96];

	CHECK(create(&host, ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(st_rip3_ports_define(host.ports, 35, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(host.ports, 35) == 0);
	for (int32_t x = 0; x < 3; x++) CHECK(st_rip3_ports_plot(host.ports, 61+x,0, source[x], ST_RIP3_COPY) == 0);
	CHECK(process(&host, "J1S", -1) == 0);
	CHECK(process(&host, "2CZ" "0z001001" "0" "0A0A0D0B" "0", -1) == 0);
	CHECK(row(host.ports, 0, 10,10, source, 3) == 0);
	CHECK(process(&host, "2CZ" "0#001001" "0" "0A0B0C0C" "0", -1) == 0);
	CHECK(row(host.ports, 0, 10,11, source+1, 2) == 0);
	CHECK(process(&host, "2Ca0000010100000001010", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&host, "J10", -1) == 0);
	CHECK(st_rip3_ports_switch(host.ports, 0) == 0);
	{
		const uint8_t pixels[] = {3,12,48,192};
		for (int32_t y = 0; y < 2; y++) for (int32_t x = 0; x < 2; x++)
			CHECK(st_rip3_ports_plot(host.ports, x,y, pixels[y*2+x], ST_RIP3_COPY) == 0);
	}
	for (unsigned width = 2; width <= 5; width++) {
		(void)snprintf(command, sizeof(command), "n%u", width);
		CHECK(process(&host, command, -1) == 0);
		/* Independent zero-padded decimal fields 0..4 are identical MegaNums;
		 * no encoder from the implementation builds expected wire input. */
		(void)snprintf(command, sizeof(command), "2C0%0*u%0*u%0*u%0*u0%0*u%0*u%0*u%0*u000000",
		    (int)width,0u,(int)width,0u,(int)width,2u,(int)width,2u,
		    (int)width,2u,(int)width,2u,(int)width,4u,(int)width,4u);
		CHECK(process(&host, command, -1) == 0);
		{
			const uint8_t first[] = {3,12}, second[] = {48,192};
			CHECK(row(host.ports, 0, 2,2, first, 2) == 0 && row(host.ports, 0, 2,3, second, 2) == 0);
		}
	}
	{
		struct st_rip3_command_host wrong = host;
		uint8_t value = 1;
		CHECK(st_rip3_ports_create(&wrong.ports, ST_RIP3_CANVAS_800_600) == 0);
		CHECK(process(&host, "n2", -1) == 0);
		CHECK(process(&wrong, "2C0000001010000001010", ST_RIP3_COMMAND_INVALID) == 0);
		CHECK(st_rip3_ports_read(wrong.ports, 0, 0,0, &value) == 0 && value == 0);
		st_rip3_ports_destroy(wrong.ports);
	}
	destroy(&host);
	return 0;
}

static int
large_coordinates(enum st_rip3_canvas canvas)
{
	struct st_rip3_command_host host = {0};
	const struct st_rip3_rect source = {0,0,1,1}, destination = {10,20,11,21};
	const uint8_t expected[] = {9,9};

	CHECK(create(&host, canvas, 1,1) == 0);
	CHECK(st_rip3_ports_define(host.ports, 1, &source, true) == 0);
	CHECK(st_rip3_ports_define(host.ports, 2, &destination, false) == 0);
	CHECK(st_rip3_ports_switch(host.ports, 1) == 0 && st_rip3_ports_erase(host.ports, 9) == 0);
	CHECK(process(&host, "J1S", -1) == 0);
	CHECK(process(&host, "n5", -1) == 0);
	CHECK(process(&host, "2C1" "0000000000&&&&&&&&&&" "2" "0000000000&&&&&&&&&&" "0", -1) == 0);
	CHECK(row(host.ports, 2, 0,0, expected, 2) == 0 && row(host.ports, 2, 0,1, expected, 2) == 0);
	CHECK(process(&host, "2C1" "&&&&#" "&&&&#" "&&&&&" "&&&&&" "2" "0000000000&&&&&&&&&&" "0", -1) == 0);
	CHECK(row(host.ports, 2, 0,0, expected, 2) == 0 && row(host.ports, 2, 0,1, expected, 2) == 0);
	CHECK(process(&host, "2C1" "0000000000&&&&&&&&&&" "2" "&&&&#" "&&&&#" "&&&&&" "&&&&&" "0", -1) == 0);
	CHECK(row(host.ports, 2, 0,0, expected, 2) == 0 && row(host.ports, 2, 0,1, expected, 2) == 0);
	/* These projected ends exceed INT32_MAX but differ by one WORLD unit.
	 * Saturating both before comparing extents would incorrectly choose the
	 * equal-size branch. Unequal extents fit all four surviving source pixels;
	 * changing only that final digit selects the distinct unscaled result. */
	const struct st_rip3_rect wide_source = {0,0,3,0}, narrow_destination = {10,20,11,20};
	const uint8_t pixels[] = {1,2,4,8}, fitted[] = {2,8}, unscaled[] = {1,2};
	CHECK(st_rip3_ports_define(host.ports,1,&wide_source,true) == 0);
	CHECK(st_rip3_ports_define(host.ports,2,&narrow_destination,false) == 0);
	CHECK(st_rip3_ports_switch(host.ports,1) == 0);
	for (int32_t x = 0; x < 4; x++) CHECK(st_rip3_ports_plot(host.ports,x,0,pixels[x],ST_RIP3_COPY) == 0);
	CHECK(process(&host,"2C1" "0000000000&&&&&00001" "2" "0000000000&&&&#00001" "0",-1) == 0);
	CHECK(row(host.ports,2,0,0,fitted,2) == 0);
	CHECK(process(&host,"2C1" "0000000000&&&&&00001" "2" "0000000000&&&&&00001" "0",-1) == 0);
	CHECK(row(host.ports,2,0,0,unscaled,2) == 0);

	destroy(&host);
	return 0;
}

int
main(void)
{
	CHECK(geometry_matrix(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(geometry_matrix(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(geometry_matrix(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	CHECK(converted_sentinels(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(converted_sentinels(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(converted_sentinels(ST_RIP3_CANVAS_1024_768,1024,768) == 0);

	CHECK(clipping(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(clipping(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(clipping(ST_RIP3_CANVAS_1024_768,1024,768) == 0);

	CHECK(scene(ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600, 800,600) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768, 1024,768) == 0);
	CHECK(canonical(ST_RIP3_CANVAS_640_480, 640,480, 39) == 0);
	CHECK(canonical(ST_RIP3_CANVAS_800_600, 800,600, 49) == 0);
	CHECK(canonical(ST_RIP3_CANVAS_1024_768, 1024,768, 63) == 0);
	CHECK(validation() == 0);
	CHECK(widths_and_radix() == 0);
	CHECK(large_coordinates(ST_RIP3_CANVAS_640_480) == 0);
	CHECK(large_coordinates(ST_RIP3_CANVAS_800_600) == 0);
	CHECK(large_coordinates(ST_RIP3_CANVAS_1024_768) == 0);
	return 0;
}
