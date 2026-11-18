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
extern unsigned st_rip3_polygon_test_malloc_countdown;

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

static const char *const border_map[] = {
    "........", ".FBFBFB.", ".B....F.", ".F....B.",
    ".B....F.", ".F....B.", ".BFBFBF.", "........",
};

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	const uint8_t foreground[] = {5,0x50,0x55,5,0xaa}, background[] = {3,0x56,0x57,1,0xaa};
	struct st_rip3_command_host host = {0};
	struct st_rip3_environment environment;
	struct st_rip3_style style;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	struct scene seen = {&host,0};
	const struct st_rip3_stream_sink sink = {.context=&seen,.command=scene_command};
	struct st_rip3_stream *stream = NULL;
	const struct st_rip3_rect bounds = {10,20,79,89}, view = {2,3,53,34};
	const unsigned rows[] = {0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55};
	const struct st_rip3_point origin = {1,2};
	bool borders = false;
	unsigned current;
	CHECK(create(&host,canvas,width*2,height*2) == 0); host.borders = &borders;
	CHECK(st_rip3_styles_get(host.styles,0,&style) == 0);
	CHECK(st_rip3_fill_custom(&style.fill,rows,&origin) == 0); style.fill_color = 5;
	for (unsigned row = 0; row < 3; row++) for (unsigned mode = 0; mode < 5; mode++) {
		style.write_mode = (enum st_rip3_write_mode)mode;
		style.pen.pattern = row == 1 ? 0xaaaa : 0xffff; style.pen.thickness = row == 2 ? 3 : 1;
		style.pen.draw_background = true;
		CHECK(st_rip3_styles_define(host.styles,row*5+mode+1,&style) == 0);
		CHECK(st_rip3_styles_protect(host.styles,row*5+mode+1,true) == 0);
	}
	CHECK(st_rip3_ports_define(host.ports,1,&bounds,false) == 0);
	CHECK(st_rip3_ports_switch(host.ports,1) == 0 && st_rip3_ports_viewport(host.ports,&view) == 0);
	CHECK(st_rip3_ports_protect(host.ports,1,true) == 0);
	CHECK(st_rip3_stream_create(&stream,&sink) == 0);
	CHECK(st_rip3_palettes_get(host.palettes,0,&palette) == 0);
	for (unsigned base = 36; base <= 64; base += 28) for (unsigned coordinate = 2; coordinate <= 5; coordinate++) {
		CHECK(st_rip3_environments_get(host.environments,0,&environment) == 0);
		environment.base_math = base; environment.coordinate_size = coordinate;
		CHECK(st_rip3_environments_set(host.environments,0,&environment) == 0);
		CHECK(st_rip3_ports_erase(host.ports,0) == 0);
		for (unsigned row = 0; row < 3; row++) for (unsigned mode = 0; mode < 5; mode++) {
			const unsigned points[][2] = {{mode*20+2,row*20+2},{mode*20+12,row*20+2},
			    {mode*20+12,row*20+12},{mode*20+2,row*20+12}};
			const struct st_rip3_rect underlay = {(int32_t)mode*10,(int32_t)row*10,(int32_t)mode*10+7,(int32_t)row*10+7};
			char shape[64]; borders = row != 0;
			CHECK(st_rip3_styles_switch(host.styles,row*5+mode+1) == 0);
			CHECK(st_rip3_ports_fill(host.ports,&underlay,0x55,ST_RIP3_COPY) == 0);
			command(shape,'p',4,points,coordinate,base);
			if (row == 0) { size_t last = strlen(shape)-coordinate; shape[last] = 'C'; shape[last+1] = 0; }
			wire(stream,"!|"); wire(stream,shape); wire(stream,"\r");
		}
		CHECK(st_rip3_ports_snapshot(host.ports,&palette,&capture) == 0);
		for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
			unsigned expected = 0;
			if (x >= 12 && x <= 59 && y >= 23 && y <= 50) {
				unsigned col = (x-12)/10,row = (y-23)/10,u = (x-12)%10,v = (y-23)%10;
				if (col < 5 && row < 3 && u < 8 && v < 8) {
					expected = 0x55;
					if (u >= 1 && u < 6 && v >= 1 && v < 6) expected = (u+v)%2 == 1 ? foreground[col] : background[col];
					if (row == 1 && border_map[v][u] != '.') expected = border_map[v][u] == 'F' ? 12 : 3;
					if (row == 2 && !(u >= 3 && u <= 4 && v >= 3 && v <= 4)) expected = 12;
				}
			}
			CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | expected*UINT32_C(0x010101)));
		}
		st_pixel_buffer_destroy(capture); capture = NULL;
	}
	CHECK(seen.errors == 0);
	CHECK(st_rip3_ports_current(host.ports,&current) == 0 && current == 1);
	CHECK(st_rip3_environments_get(host.environments,0,&environment) == 0 && environment.port == 9
	    && environment.graphics_style == 29 && environment.palette == 15);
	st_rip3_stream_destroy(stream); destroy(&host);
	return 0;
}

static int
unchanged(struct st_rip3_ports *ports)
{
	uint8_t value;
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(ports,0,x,y,&value) == 0 && value == 0x55);
	return 0;
}

static int
boundaries(void)
{
	struct st_rip3_command_host host = {0}, missing;
	struct st_rip3_style style;
	struct st_rip3_environment environment;
	const struct st_rip3_rect view = {0,0,7,7};
	struct scene seen = {&host,0};
	const struct st_rip3_stream_sink sink = {.context=&seen,.command=scene_command};
	struct st_rip3_stream *stream = NULL;
	bool borders = true;
	uint8_t value;
	CHECK(create(&host,ST_RIP3_CANVAS_640_480,640,480) == 0); host.borders = &borders;
	CHECK(st_rip3_ports_viewport(host.ports,&view) == 0 && st_rip3_ports_erase(host.ports,0x55) == 0);
	CHECK(process(&host,"p0401010601060601?6",ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&host,"p0401010601060601060",ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&host,"p0201010606",-1) == 0);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(host.ports,0,x,y,&value) == 0 && value == (x == y && x >= 1 && x <= 6 ? 12 : 0x55));
	CHECK(st_rip3_ports_erase(host.ports,0x55) == 0);
	missing = host; missing.borders = NULL;
	CHECK(process(&missing,"p040101060106060106",ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	st_rip3_path_test_fail_malloc = true;
	CHECK(process(&host,"p040101060106060106",ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_path_test_fail_malloc);
	CHECK(unchanged(host.ports) == 0);
	for (unsigned stage = 1; stage <= 2; stage++) {
		st_rip3_polygon_test_malloc_countdown = stage;
		CHECK(process(&host,"p040101060106060106",ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0);
		CHECK(st_rip3_polygon_test_malloc_countdown == 0 && unchanged(host.ports) == 0);
	}
	st_rip3_drawing_test_fail_calloc = true;
	CHECK(process(&host,"p040101060106060106",ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_drawing_test_fail_calloc);
	CHECK(unchanged(host.ports) == 0);
	CHECK(st_rip3_ports_activate(host.ports,false) == 0);
	st_rip3_path_test_fail_malloc = true; st_rip3_polygon_test_malloc_countdown = 1; st_rip3_drawing_test_fail_calloc = true;
	CHECK(process(&missing,"p040101060106060106",-1) == 0);
	CHECK(st_rip3_path_test_fail_malloc && st_rip3_polygon_test_malloc_countdown == 1 && st_rip3_drawing_test_fail_calloc);
	st_rip3_path_test_fail_malloc = false; st_rip3_polygon_test_malloc_countdown = 0; st_rip3_drawing_test_fail_calloc = false;
	CHECK(st_rip3_ports_activate(host.ports,true) == 0);
	CHECK(st_rip3_stream_create(&stream,&sink) == 0);
	borders = false;
	wire(stream,"!|p0401010601060601?6|p00|p040101060106060106\r");
	CHECK(seen.errors == 2);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(host.ports,0,x,y,&value) == 0 && value == (x >= 1 && x < 6 && y >= 1 && y < 6 ? 2 : 0x55));
	/* Repeating the same loop twice cancels its interior by even-odd parity. */
	const unsigned twice[][2] = {{1,1},{6,1},{6,6},{1,6},{1,1},{6,1},{6,6},{1,6}};
	char shape[96]; command(shape,'p',8,twice,2,36);
	CHECK(st_rip3_ports_erase(host.ports,0x55) == 0 && process(&host,shape,-1) == 0);
	CHECK(unchanged(host.ports) == 0);
	CHECK(st_rip3_styles_get(host.styles,0,&style) == 0);
	for (unsigned background_only = 0; background_only < 2; background_only++) {
		for (size_t i = 0; i < 8; i++) style.fill.rows[i] = background_only != 0 ? 0 : 255;
		style.drawing_color = 0xffffff;
		style.fill_color = background_only != 0 ? 0xffffff : 2;
		style.background_color = background_only != 0 ? 3 : 0xffffff;
		CHECK(st_rip3_styles_set(host.styles,0,&style) == 0 && st_rip3_ports_erase(host.ports,0x55) == 0);
		CHECK(process(&host,"p040101060106060106",-1) == 0);
		for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
			CHECK(st_rip3_ports_read(host.ports,0,x,y,&value) == 0
			    && value == (x >= 1 && x < 6 && y >= 1 && y < 6 ? (background_only != 0 ? 3 : 2) : 0x55));
	}
	for (size_t i = 0; i < 8; i++) style.fill.rows[i] = 255;
	style.drawing_color = 12; style.background_color = 3; style.fill_color = 2;
	CHECK(st_rip3_styles_set(host.styles,0,&style) == 0 && st_rip3_ports_erase(host.ports,0x55) == 0);
	CHECK(st_rip3_environments_get(host.environments,0,&environment) == 0);
	environment.base_math = 64; environment.coordinate_size = 5; environment.world_width = environment.world_height = 1;
	CHECK(st_rip3_environments_set(host.environments,0,&environment) == 0);
	CHECK(st_rip3_styles_get(host.styles,0,&style) == 0); style.write_mode = ST_RIP3_XOR;
	CHECK(st_rip3_styles_set(host.styles,0,&style) == 0);
	const unsigned wide[][2] = {{0,0},{1073741822,536870911},{1073741822,0}};
	command(shape,'p',3,wide,5,64);
	CHECK(process(&host,shape,-1) == 0);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(host.ports,0,x,y,&value) == 0 && value == (8*(2*y+1) < 3*(2*x+1) ? 0x57 : 0x55));
	st_rip3_stream_destroy(stream); destroy(&host);
	return 0;
}

static int
rgb(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_command_host host = {0};
	struct st_rip3_environment environment;
	struct st_rip3_style style;
	struct st_rip3_palette palette;
	struct st_rip3_palettes *bound;
	struct st_pixel_buffer *capture = NULL;
	const struct st_rip3_rect view = {0,0,7,7};
	uint32_t colors[256];
	bool borders = false;
	CHECK(create(&host,canvas,width,height) == 0); host.borders = &borders;
	CHECK(st_rip3_ports_viewport(host.ports,&view) == 0);
	CHECK(st_rip3_environments_get(host.environments,0,&environment) == 0); environment.rgb_bits = 8;
	CHECK(st_rip3_environments_set(host.environments,0,&environment) == 0);
	for (unsigned i = 0; i < 256; i++) colors[i] = 0xffffff;
	colors[0] = 0; colors[17] = 0x112233; colors[18] = 0x112235;
	colors[19] = 0x445566; colors[20] = 0x223344; colors[85] = 0x555555; colors[170] = 0xaabbcc;
	CHECK(st_rip3_palettes_switch(host.palettes,7) == 0);
	CHECK(st_rip3_palettes_rgb(host.palettes,0,8,colors,256) == 0);
	CHECK(st_rip3_palettes_protect(host.palettes,7,true) == 0);
	CHECK(st_rip3_palettes_get(host.palettes,7,&palette) == 0);
	CHECK(st_rip3_styles_get(host.styles,0,&style) == 0);
	style.drawing_color = 0x223344; style.background_color = 0x445566; style.fill_color = 0x112234;
	for (size_t i = 0; i < 8; i++) style.fill.rows[i] = i%2 == 0 ? 0xaa : 0x55;
	for (unsigned border = 0; border < 2; border++) for (unsigned mode = 0; mode < 2; mode++) {
		borders = border != 0; style.write_mode = mode == 0 ? ST_RIP3_COPY : ST_RIP3_NOT;
		CHECK(st_rip3_styles_set(host.styles,0,&style) == 0 && st_rip3_ports_erase(host.ports,0x55) == 0);
		bound = host.palettes;
		if (!borders && mode != 0) host.palettes = NULL;
		CHECK(process(&host,"p040101060106060106",-1) == 0);
		host.palettes = bound;
		CHECK(st_rip3_ports_snapshot(host.ports,&palette,&capture) == 0);
		for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
			unsigned index = x < 8 && y < 8 ? 85 : 0;
			if (x >= 1 && x < 6 && y >= 1 && y < 6) index = mode != 0 ? 170 : (x+y)%2 == 0 ? 17 : 19;
			if (borders && ((x >= 1 && x <= 6 && (y == 1 || y == 6)) || (y >= 1 && y <= 6 && (x == 1 || x == 6)))) index = 20;
			CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | colors[index]));
		}
		st_pixel_buffer_destroy(capture); capture = NULL;
	}
	/* COPY borders still require ink in NOT; failure precedes every allocation. */
	CHECK(st_rip3_ports_erase(host.ports,0x55) == 0); host.palettes = NULL;
	st_rip3_path_test_fail_malloc = true;
	CHECK(process(&host,"p040101060106060106",ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(st_rip3_path_test_fail_malloc && unchanged(host.ports) == 0);
	st_rip3_path_test_fail_malloc = false; host.palettes = bound;
	destroy(&host);
	return 0;
}

static int
example(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	/* The pinned p example's eight literal base-36 world vertices. The ray
	 * crossing oracle below uses exact integer arithmetic at pixel centers. */
	const unsigned world[][2] = {{0,236},{51,219},{67,197},{87,178},{113,157},{106,150},{43,162},{0,176}};
	struct st_rip3_point device[8];
	struct st_rip3_command_host host = {0};
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	bool borders = false;
	CHECK(create(&host,canvas,1280,960) == 0); host.borders = &borders;
	for (size_t i = 0; i < 8; i++) device[i] = (struct st_rip3_point){(int32_t)(world[i][0]*width/1280),(int32_t)(world[i][1]*height/960)};
	CHECK(process(&host,"p08006K1F631V5H2F4Y354D2Y46174I004W",-1) == 0);
	CHECK(st_rip3_palettes_get(host.palettes,0,&palette) == 0 && st_rip3_ports_snapshot(host.ports,&palette,&capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		bool inside = false;
		for (size_t i = 0; i < 8; i++) {
			struct st_rip3_point a = device[i], b = device[(i+1)%8];
			if (a.y > b.y) { struct st_rip3_point swap = a; a = b; b = swap; }
			if (a.y <= (int32_t)y && b.y > (int32_t)y) {
				int64_t dx = b.x-a.x, dy = b.y-a.y;
				if (dx*(2*(int64_t)y+1-2*a.y)-dy*(2*(int64_t)x+1-2*a.x) > 0) inside = !inside;
			}
		}
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | (inside ? UINT32_C(0x020202) : 0)));
	}
	st_pixel_buffer_destroy(capture); destroy(&host);
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
	bool borders = true;
	uint8_t value;
	CHECK(shape != NULL && create(&host, ST_RIP3_CANVAS_640_480,640,480) == 0);
	host.borders = &borders;
	CHECK(st_rip3_ports_viewport(host.ports, &view) == 0 && st_rip3_ports_erase(host.ports, 0x55) == 0);
	CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
	environment.base_math = base; environment.coordinate_size = 5;
	CHECK(st_rip3_environments_set(host.environments, 0, &environment) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0);
	style.write_mode = ST_RIP3_XOR;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	shape[0] = 'p'; number(shape+1,2,(unsigned)count,base);
	for (size_t i = 0; i < count*2; i++) number(shape+3+i*5,5,3,base);
	shape[3+count*10] = 0;
	CHECK(st_rip3_stream_create(&stream, &sink) == 0);
	wire(stream,"!|"); wire(stream,shape); wire(stream,"\r");
	CHECK(seen.errors == 0);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(host.ports, 0, x,y,&value) == 0 && value == (x == 3 && y == 3 ? 12 : 0x55));
	st_rip3_stream_destroy(stream); free(shape); destroy(&host);
	return 0;
}

int
main(void)
{
	CHECK(scene(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	CHECK(boundaries() == 0);
	CHECK(rgb(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(rgb(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(rgb(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	CHECK(example(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(example(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(example(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	CHECK(counts(36,1295) == 0);
	CHECK(counts(64,4095) == 0);
	return 0;
}
