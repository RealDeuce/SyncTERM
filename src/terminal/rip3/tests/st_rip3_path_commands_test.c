/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_styles.h"
#include "st_rip3_ports.h"
#include "st_rip3_palettes.h"
#include "st_rip3_stream.h"
#include "st_pixels.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern bool st_rip3_path_test_fail_malloc;
extern bool st_rip3_drawing_test_fail_calloc;

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
	seen->errors++; seen->kind = kind;
}

static int
process(const struct st_rip3_command_host *host, const char *command, int error)
{
	struct observation seen = {0};
	const struct st_rip3_command_sink sink = {&seen,diagnostic};
	st_rip3_commands_process(host, &sink, (const uint8_t *)command, strlen(command));
	if (seen.errors != (error < 0 ? 0u : 1u) || (error >= 0 && (int)seen.kind != error))
		fprintf(stderr, "%s: errors %u, kind %d, expected %d\n", command, seen.errors, (int)seen.kind, error);
	CHECK(seen.errors == (error < 0 ? 0u : 1u));
	if (error >= 0) CHECK((int)seen.kind == error);
	return 0;
}

static int
create(struct st_rip3_command_host *host, enum st_rip3_canvas canvas,
    uint32_t world_width, uint32_t world_height)
{
	struct st_rip3_style initial = {.drawing_color=12,.background_color=3,.fill_color=2,
	    .pen={.pattern=0xffff,.thickness=1}, .font={0,0,1,0}, .write_mode=ST_RIP3_COPY};
	struct st_rip3_environment environment;
	uint32_t colors[256];
	CHECK(st_rip3_fill_solid(&initial.fill, true) == 0);
	CHECK(st_rip3_styles_create(&host->styles, &initial) == 0);
	CHECK(st_rip3_ports_create(&host->ports, canvas) == 0);
	CHECK(st_rip3_palettes_create(&host->palettes) == 0);
	CHECK(st_rip3_environments_create(&host->environments, canvas) == 0);
	CHECK(st_rip3_environments_get(host->environments, 0, &environment) == 0);
	environment.world_width = world_width; environment.world_height = world_height;
	environment.port = 9; environment.graphics_style = 29; environment.palette = 15;
	CHECK(st_rip3_environments_set(host->environments, 0, &environment) == 0);
	for (unsigned i = 0; i < 256; i++) colors[i] = i*UINT32_C(0x010101);
	CHECK(st_rip3_palettes_rgb(host->palettes, 0, 8, colors, 256) == 0);
	return 0;
}

static void
destroy(struct st_rip3_command_host *host)
{
	st_rip3_styles_destroy(host->styles); st_rip3_ports_destroy(host->ports);
	st_rip3_palettes_destroy(host->palettes); st_rip3_environments_destroy(host->environments);
}

static void
number(char *result, size_t width, unsigned value, unsigned base)
{
	static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#&";
	while (width != 0) { result[--width] = digits[value%base]; value /= base; }
}

static void
command(char *result, char kind, size_t count, const unsigned (*points)[2],
    unsigned width, unsigned base)
{
	result[0] = kind; number(result+1, 2,(unsigned)count,base);
	for (size_t i = 0; i < count; i++) {
		number(result+3+2*i*width, width,points[i][0],base);
		number(result+3+(2*i+1)*width, width,points[i][1],base);
	}
	result[3+2*count*width] = 0;
}

struct scene { const struct st_rip3_command_host *host; unsigned errors; };

static void
scene_error(void *context, enum st_rip3_command_diagnostic kind)
{
	struct scene *scene = context;
	(void)kind; scene->errors++;
}

static void
scene_command(void *context, const uint8_t *bytes, size_t length)
{
	struct scene *scene = context;
	const struct st_rip3_command_sink sink = {scene,scene_error};
	st_rip3_commands_process(scene->host, &sink, bytes,length);
}

static void
wire(struct st_rip3_stream *stream, const char *text)
{
	size_t length = strlen(text);
	for (size_t i = 0; i < length; i++) st_rip3_stream_process(stream, (const uint8_t *)text+i, 1);
}

/* F/B name pen foreground/background; '.' is untouched. Authored path maps
 * retain major-axis phase across all four edges and omit l's closing edge. */
static const char *const paths[4][8] = {
	{"........",".FFFFFF.",".F....F.",".F....F.",".F....F.",".F....F.",".FFFFFF.","........"},
	{"........",".FFFFFF.","......F.","......F.","......F.","......F.",".FFFFFF.","........"},
	{"........",".FBFBFB.",".B....F.",".F....B.",".B....F.",".F....B.",".BFBFBF.","........"},
	{"........",".FBFBFB.","......F.","......B.","......F.","......B.",".BFBFBF.","........"},
};

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	static const unsigned foreground[] = {12,0x59,0x5d,4,0xaa};
	static const unsigned background[] = {3,0x56,0x57,1,0xaa};
	struct st_rip3_command_host host = {0};
	struct st_rip3_environment environment;
	struct st_rip3_style style;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	struct scene seen = {&host,0};
	const struct st_rip3_stream_sink sink = {.context=&seen,.command=scene_command};
	struct st_rip3_stream *stream = NULL;
	const struct st_rip3_rect bounds = {10,20,79,89}, view = {2,3,53,64};
	unsigned current;
	CHECK(create(&host, canvas, width*2,height*2) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0);
	for (unsigned row = 0; row < 6; row++) for (unsigned mode = 0; mode < 5; mode++) {
		style.write_mode = (enum st_rip3_write_mode)mode;
		style.pen.pattern = row < 2 ? 0xffff : 0xaaaa; style.pen.draw_background = row < 4;
		CHECK(st_rip3_styles_define(host.styles, row*5+mode+1, &style) == 0);
		CHECK(st_rip3_styles_protect(host.styles, row*5+mode+1, true) == 0);
	}
	CHECK(st_rip3_ports_define(host.ports, 1, &bounds, false) == 0);
	CHECK(st_rip3_ports_switch(host.ports, 1) == 0 && st_rip3_ports_viewport(host.ports, &view) == 0);
	CHECK(st_rip3_ports_protect(host.ports, 1, true) == 0);
	CHECK(st_rip3_stream_create(&stream, &sink) == 0);
	CHECK(st_rip3_palettes_get(host.palettes, 0, &palette) == 0);
	for (unsigned base = 36; base <= 64; base += 28) for (unsigned coordinate = 2; coordinate <= 5; coordinate++) {
		CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
		environment.base_math = base; environment.coordinate_size = coordinate;
		CHECK(st_rip3_environments_set(host.environments, 0, &environment) == 0);
		CHECK(st_rip3_ports_erase(host.ports, 0) == 0);
		for (unsigned row = 0; row < 6; row++) for (unsigned mode = 0; mode < 5; mode++) {
			const unsigned points[4][2] = {{mode*20+2,row*20+2},{mode*20+12,row*20+2},
			    {mode*20+12,row*20+12},{mode*20+2,row*20+12}};
			const struct st_rip3_rect underlay = {(int32_t)mode*10,(int32_t)row*10,
			    (int32_t)mode*10+7,(int32_t)row*10+7};
			char shape[64];
			CHECK(st_rip3_styles_switch(host.styles, row*5+mode+1) == 0);
			CHECK(st_rip3_ports_fill(host.ports, &underlay, 0x55, ST_RIP3_COPY) == 0);
			command(shape, row%2 == 0 ? 'P' : 'l', 4,points,coordinate,base);
			/* Shorten the final Y field without changing its numeric value. */
			if (row == 0) {
				size_t last = strlen(shape)-coordinate;
				shape[last] = 'C'; shape[last+1] = 0;
			}
			wire(stream, "!|"); wire(stream, shape); wire(stream, "\r");
		}
		CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
		for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
			unsigned expected = 0;
			if (x >= 12 && x <= 59 && y >= 23 && y <= 80) {
				unsigned col = (x-12)/10, row = (y-23)/10, u = (x-12)%10, v = (y-23)%10;
				if (col < 5 && row < 6 && u < 8 && v < 8) {
					char pixel = paths[row < 4 ? row : row-2][v][u];
					expected = pixel == 'F' ? foreground[col] : pixel == 'B' && row < 4 ? background[col] : 0x55;
				}
			}
			if (capture->primary[(size_t)y*width+x] != (ST_PIXEL_COLOR_RGB_TAG | expected*UINT32_C(0x010101))) {
				fprintf(stderr,"scene %u/%u at %u,%u expected %u\n",base,coordinate,x,y,expected);
				CHECK(false);
			}
		}
		st_pixel_buffer_destroy(capture); capture = NULL;
	}
	CHECK(seen.errors == 0);
	CHECK(st_rip3_ports_current(host.ports, &current) == 0 && current == 1);
	CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0
	    && environment.port == 9 && environment.graphics_style == 29 && environment.palette == 15);
	for (unsigned row = 0; row < 6; row++) for (unsigned mode = 0; mode < 5; mode++) {
		CHECK(st_rip3_styles_get(host.styles, row*5+mode+1, &style) == 0);
		CHECK(style.pen.pattern == (row < 2 ? 0xffff : 0xaaaa) && style.pen.draw_background == (row < 4)
		    && style.write_mode == (enum st_rip3_write_mode)mode);
	}
	st_rip3_stream_destroy(stream); destroy(&host);
	return 0;
}

static int
unchanged(struct st_rip3_ports *ports, uint8_t color)
{
	uint8_t value;
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(ports, 0, x,y,&value) == 0 && value == color);
	return 0;
}

static int
boundaries(void)
{
	static const char *const invalid[] = {"P", "P0", "P?4", "P0401010601060601?6",
	    "P0401010601060601", "P0401010601060601060", "l020101060?"};
	static const char *const unsupported[] = {"P00", "P010101", "P0201010606", "l010101",
	    "P040101060106060106 comment"};
	struct st_rip3_command_host host = {0}, missing;
	struct st_rip3_environment environment;
	struct st_rip3_style style;
	const struct st_rip3_rect view = {0,0,7,7};
	struct st_rip3_stream *stream = NULL;
	struct scene seen = {&host,0};
	const struct st_rip3_stream_sink sink = {.context=&seen,.command=scene_command};
	uint8_t value;
	CHECK(create(&host, ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(st_rip3_ports_viewport(host.ports, &view) == 0 && st_rip3_ports_erase(host.ports, 0x55) == 0);
	for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) CHECK(process(&host, invalid[i], ST_RIP3_COMMAND_INVALID) == 0);
	for (size_t i = 0; i < sizeof(unsupported)/sizeof(unsupported[0]); i++) CHECK(process(&host, unsupported[i], ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(unchanged(host.ports, 0x55) == 0);
	st_rip3_path_test_fail_malloc = true;
	CHECK(process(&host, invalid[3], ST_RIP3_COMMAND_INVALID) == 0 && st_rip3_path_test_fail_malloc);
	CHECK(process(&host, "P040101060106060106", ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_path_test_fail_malloc);
	CHECK(unchanged(host.ports, 0x55) == 0);
	st_rip3_drawing_test_fail_calloc = true;
	CHECK(process(&host, "P040101060106060106", ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_drawing_test_fail_calloc);
	CHECK(unchanged(host.ports, 0x55) == 0);
	st_rip3_path_test_fail_malloc = true;
	CHECK(process(&host, "l040101060106060106", ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_path_test_fail_malloc);
	CHECK(unchanged(host.ports, 0x55) == 0);
	st_rip3_drawing_test_fail_calloc = true;
	CHECK(process(&host, "l040101060106060106", ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_drawing_test_fail_calloc);
	CHECK(unchanged(host.ports, 0x55) == 0);
	CHECK(st_rip3_ports_activate(host.ports, false) == 0);
	st_rip3_path_test_fail_malloc = true; st_rip3_drawing_test_fail_calloc = true;
	CHECK(process(&host, "P040101060106060106", -1) == 0);
	CHECK(process(&host, "l040101060106060106", -1) == 0);
	CHECK(process(&host, invalid[3], ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(st_rip3_path_test_fail_malloc && st_rip3_drawing_test_fail_calloc);
	st_rip3_path_test_fail_malloc = false; st_rip3_drawing_test_fail_calloc = false;
	CHECK(st_rip3_ports_activate(host.ports, true) == 0);
	missing = host; missing.ports = NULL;
	CHECK(process(&missing, "P040101060106060106", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing = host; missing.styles = NULL;
	CHECK(process(&missing, "l0201010606", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing = host; missing.environments = NULL;
	CHECK(process(&missing, "P040101060106060106", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	{
		const struct st_rip3_rect clipped = {0,0,3,3};
		CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0);
		style.pen.pattern = 0xaaaa; style.pen.draw_background = true;
		CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
		CHECK(st_rip3_ports_viewport(host.ports, &clipped) == 0);
		CHECK(process(&host, "P040101060106060106", -1) == 0);
		for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++) {
			char point = x < 4 && y < 4 ? paths[2][y][x] : '.';
			CHECK(st_rip3_ports_read(host.ports, 0, x,y,&value) == 0
			    && value == (point == 'F' ? 12 : point == 'B' ? 3 : 0x55));
		}
		style.pen.pattern = 0xffff; style.pen.draw_background = false;
		CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
		CHECK(st_rip3_ports_viewport(host.ports, &view) == 0);
		CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0);
	}
	CHECK(st_rip3_stream_create(&stream, &sink) == 0);
	wire(stream,"!|P0401010601060601?6|P00|l020101061\r");
	CHECK(seen.errors == 2);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(host.ports, 0, x,y,&value) == 0 && value == (y == 1 && x >= 1 && x <= 6 ? 12 : 0x55));
	CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
	environment.base_math = 64; environment.coordinate_size = 5;
	environment.world_width = environment.world_height = 1;
	CHECK(st_rip3_environments_set(host.environments, 0, &environment) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0);
	style.write_mode = ST_RIP3_XOR;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	{
		const unsigned points[][2] = {{0,0},{1073741822,536870911},{0,0}};
		char shape[64];
		command(shape,'P',3,points,5,64);
		CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0 && process(&host, shape,-1) == 0);
		for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++) {
			int32_t distance = 8*y-3*x;
			CHECK(st_rip3_ports_read(host.ports, 0, x,y,&value) == 0 && value == (distance >= -4 && distance <= 4 ? 0x59 : 0x55));
		}
	}
	st_rip3_stream_destroy(stream); destroy(&host);
	return 0;
}

static int
attributes(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_command_host host = {0};
	struct st_rip3_environment environment;
	struct st_rip3_style style;
	struct st_rip3_palette palette;
	struct st_rip3_palettes *bound;
	struct st_pixel_buffer *capture = NULL;
	const struct st_rip3_rect view = {0,0,7,7};
	uint32_t colors[256];
	uint8_t value;
	CHECK(create(&host, canvas,width,height) == 0);
	CHECK(st_rip3_ports_viewport(host.ports, &view) == 0);
	CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
	environment.rgb_bits = 8;
	CHECK(st_rip3_environments_set(host.environments, 0, &environment) == 0);
	for (unsigned i = 0; i < 256; i++) colors[i] = 0xffffff;
	colors[0] = 0; colors[17] = 0x112233; colors[18] = 0x112235; colors[19] = 0x445566;
	colors[85] = 0x555555; colors[170] = 0xaabbcc;
	CHECK(st_rip3_palettes_switch(host.palettes, 7) == 0);
	CHECK(st_rip3_palettes_rgb(host.palettes, 0,8,colors,256) == 0);
	CHECK(st_rip3_palettes_protect(host.palettes, 7,true) == 0);
	CHECK(st_rip3_palettes_get(host.palettes, 7, &palette) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0);
	style.pen.pattern = 0xaaaa; style.pen.draw_background = true;
	style.drawing_color = 0x112234; style.background_color = 0x445566;
	for (unsigned kind = 0; kind < 2; kind++) for (unsigned mode = 0; mode < 2; mode++) {
		style.write_mode = mode == 0 ? ST_RIP3_COPY : ST_RIP3_NOT;
		CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
		CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0);
		bound = host.palettes;
		if (mode != 0) host.palettes = NULL;
		CHECK(process(&host, kind == 0 ? "P040101060106060106" : "l040101060106060106", -1) == 0);
		host.palettes = bound;
		CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
		for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
			unsigned index = 0;
			if (x < 8 && y < 8) {
				char pixel = paths[kind+2][y][x];
				index = pixel == '.' ? 85 : mode != 0 ? 170 : pixel == 'F' ? 17 : 19;
			}
			CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | colors[index]));
		}
		st_pixel_buffer_destroy(capture); capture = NULL;
	}
	environment.rgb_bits = 0;
	CHECK(st_rip3_environments_set(host.environments, 0, &environment) == 0);
	style.write_mode = ST_RIP3_COPY; style.drawing_color = 12; style.background_color = 0xffffff;
	for (unsigned unused = 0; unused < 2; unused++) {
		style.pen.pattern = unused == 0 ? 0xffff : 0xaaaa;
		style.pen.draw_background = unused == 0;
		CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
		CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0);
		CHECK(process(&host, "P040101060106060106", -1) == 0);
		for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
			CHECK(st_rip3_ports_read(host.ports, 0,x,y,&value) == 0
			    && value == (paths[unused == 0 ? 0 : 2][y][x] == 'F' ? 12 : 0x55));
	}
	style.pen.draw_background = true;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0);
	st_rip3_path_test_fail_malloc = true;
	CHECK(process(&host, "P040101060106060106", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(st_rip3_path_test_fail_malloc && unchanged(host.ports, 0x55) == 0);
	style.drawing_color = 0xffffff; style.background_color = 3;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	CHECK(process(&host, "l040101060106060106", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(st_rip3_path_test_fail_malloc && unchanged(host.ports, 0x55) == 0);
	st_rip3_path_test_fail_malloc = false;
	destroy(&host);
	return 0;
}

static int
shapes(void)
{
	struct st_rip3_command_host host = {0};
	struct st_rip3_style style;
	const struct st_rip3_rect view = {0,0,7,7};
	const unsigned bowtie[][2] = {{1,1},{5,5},{1,5},{5,1},{1,1}};
	const unsigned elbow[][2] = {{1,1},{5,1},{5,5}};
	const char *const expected[] = {"........",".FFFFF..","..F.F...","...F....","..F.F...",".FFFFF..","........","........"};
	char shape[64];
	uint8_t value;
	CHECK(create(&host, ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(st_rip3_ports_viewport(host.ports, &view) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0);
	style.write_mode = ST_RIP3_XOR;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	for (unsigned variant = 0; variant < 3; variant++) {
		command(shape, variant == 1 ? 'l' : 'P', variant == 2 ? 5 : 4,bowtie,2,36);
		CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0 && process(&host, shape,-1) == 0);
		for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++) {
			bool on = expected[y][x] == 'F';
			if (variant == 1 && y == 1 && x > 1 && x < 5) on = false;
			CHECK(st_rip3_ports_read(host.ports, 0,x,y,&value) == 0 && value == (on ? 0x59 : 0x55));
		}
	}
	style.pen.thickness = 3;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	command(shape,'l',3,elbow,2,36);
	CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0 && process(&host, shape,-1) == 0);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++) {
		int32_t dx = x < 1 ? 1-x : x > 5 ? x-5 : 0;
		int32_t dy = y < 1 ? 1-y : y > 5 ? y-5 : 0;
		bool on = 4*(dx*dx+(y-1)*(y-1)) <= 9 || 4*((x-5)*(x-5)+dy*dy) <= 9;
		CHECK(st_rip3_ports_read(host.ports, 0,x,y,&value) == 0 && value == (on ? 0x59 : 0x55));
	}
	destroy(&host);
	return 0;
}

/* Integer distance to a closed segment, for unit-width solid strokes at
 * integer pixel addresses. No decoder or rendering helper supplies this oracle. */
static bool
on_segment(int32_t x, int32_t y, const struct st_rip3_point *a,
    const struct st_rip3_point *b)
{
	int64_t dx = (int64_t)b->x-a->x, dy = (int64_t)b->y-a->y;
	int64_t px = (int64_t)x-a->x, py = (int64_t)y-a->y;
	int64_t length = dx*dx+dy*dy, dot = px*dx+py*dy;
	if (dot <= 0) return 4*(px*px+py*py) <= 1;
	if (dot >= length) {
		px = (int64_t)x-b->x; py = (int64_t)y-b->y;
		return 4*(px*px+py*py) <= 1;
	}
	int64_t cross = px*dy-py*dx;
	return 4*cross*cross <= length;
}

static int
examples(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	/* Pinned chapter examples, independently decoded from base-36 pairs. */
	static const char *const wire[] = {
	    "l0E0A20462046244B244B9G469G469K0A9K0A9G059G05240A240A200A20",
	    "P0CLQ72PM72PM9AOI86OIC2PMAYPMD6LQD6LQAYMUC2MU86LQ9A",
	};
	static const unsigned world[2][14][2] = {
	    {{10,72},{150,72},{150,76},{155,76},{155,340},{150,340},{150,344},
	     {10,344},{10,340},{5,340},{5,76},{10,76},{10,72},{10,72}},
	    {{782,254},{922,254},{922,334},{882,294},{882,434},{922,394},
	     {922,474},{782,474},{782,394},{822,434},{822,294},{782,334}},
	};
	const size_t count[] = {14,12};
	struct st_rip3_command_host host = {0};
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	CHECK(create(&host, canvas,1280,960) == 0);
	CHECK(st_rip3_palettes_get(host.palettes, 0, &palette) == 0);
	for (size_t example = 0; example < 2; example++) {
		struct st_rip3_point projected[14];
		for (size_t i = 0; i < count[example]; i++) {
			projected[i].x = (int32_t)(world[example][i][0]*width/1280);
			projected[i].y = (int32_t)(world[example][i][1]*height/960);
		}
		CHECK(st_rip3_ports_erase(host.ports, 0) == 0);
		CHECK(process(&host, wire[example], -1) == 0);
		CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
		for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
			bool on = false;
			size_t segments = example == 0 ? count[example]-1 : count[example];
			for (size_t i = 0; i < segments && !on; i++)
				on = on_segment((int32_t)x,(int32_t)y,projected+i,projected+(i+1)%count[example]);
			CHECK(capture->primary[(size_t)y*width+x]
			    == (ST_PIXEL_COLOR_RGB_TAG | (on ? UINT32_C(0x0c0c0c) : 0)));
		}
		st_pixel_buffer_destroy(capture); capture = NULL;
	}
	destroy(&host);
	return 0;
}

static int
counts(unsigned base, size_t count)
{
	struct st_rip3_command_host host = {0};
	struct st_rip3_environment environment;
	struct st_rip3_style style;
	struct st_rip3_stream *stream = NULL;
	struct scene seen = {&host,0};
	const struct st_rip3_stream_sink sink = {.context=&seen,.command=scene_command};
	char *shape = malloc(4+count*10);
	const struct st_rip3_rect view = {0,0,7,7};
	uint8_t value;
	CHECK(shape != NULL && create(&host, ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(st_rip3_ports_viewport(host.ports, &view) == 0 && st_rip3_ports_erase(host.ports, 0x55) == 0);
	CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
	environment.base_math = base; environment.coordinate_size = 5;
	CHECK(st_rip3_environments_set(host.environments, 0, &environment) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0);
	style.write_mode = ST_RIP3_XOR;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	shape[0] = 'P'; number(shape+1,2,(unsigned)count,base);
	for (size_t i = 0; i < count*2; i++) number(shape+3+i*5,5,3,base);
	shape[3+count*10] = 0;
	CHECK(st_rip3_stream_create(&stream, &sink) == 0);
	wire(stream,"!|"); wire(stream,shape); wire(stream,"\r");
	CHECK(seen.errors == 0);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(host.ports, 0, x,y,&value) == 0 && value == (x == 3 && y == 3 ? 0x59 : 0x55));
	st_rip3_stream_destroy(stream); free(shape); destroy(&host);
	return 0;
}

int
main(void)
{
	CHECK(scene(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	CHECK(examples(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(examples(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(examples(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	CHECK(boundaries() == 0);
	CHECK(shapes() == 0);
	CHECK(attributes(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(attributes(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(attributes(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	CHECK(counts(36,1295) == 0);
	CHECK(counts(64,4095) == 0);
	return 0;
}
