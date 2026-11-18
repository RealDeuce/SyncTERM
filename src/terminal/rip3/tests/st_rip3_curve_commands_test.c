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
#include <string.h>

extern bool st_rip3_drawing_test_fail_calloc;
extern unsigned st_rip3_ellipse_test_realloc_countdown;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

/* Independent radius-three, unit-width annulus at integer pixel addresses. */
static const char *const circle[] = {
	"..###...", ".#...#..", "#.....#.", "#.....#.",
	"#.....#.", ".#...#..", "..###...", "........",
};

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
create(struct st_rip3_command_host *host, enum st_rip3_canvas canvas, uint32_t world_width, uint32_t world_height)
{
	struct st_rip3_style initial = {.drawing_color=12,.background_color=1,.fill_color=3,
	    .pen={.pattern=0xaaaa,.thickness=1,.draw_background=true},
	    .font={0,0,1,0},.write_mode=ST_RIP3_COPY};
	struct st_rip3_environment environment;
	uint32_t colors[256];
	CHECK(st_rip3_fill_solid(&initial.fill, true) == 0);
	initial.fill.origin = (struct st_rip3_point){1,2};
	CHECK(st_rip3_styles_create(&host->styles, &initial) == 0);
	CHECK(st_rip3_environments_create(&host->environments, canvas) == 0);
	CHECK(st_rip3_environments_get(host->environments, 0, &environment) == 0);
	environment.world_width = world_width; environment.world_height = world_height;
	CHECK(st_rip3_environments_set(host->environments, 0, &environment) == 0);
	CHECK(st_rip3_ports_create(&host->ports, canvas) == 0);
	CHECK(st_rip3_palettes_create(&host->palettes) == 0);
	for (unsigned i = 0; i < 256; i++) colors[i] = i*UINT32_C(0x010101);
	CHECK(st_rip3_palettes_rgb(host->palettes, 0, 8, colors, 256) == 0);
	return 0;
}

static void
destroy(struct st_rip3_command_host *host)
{
	st_rip3_styles_destroy(host->styles);
	st_rip3_environments_destroy(host->environments);
	st_rip3_ports_destroy(host->ports);
	st_rip3_palettes_destroy(host->palettes);
}

static void
number(char *result, size_t width, unsigned value, unsigned base)
{
	static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#&";
	while (width != 0) { result[--width] = digits[value%base]; value /= base; }
}

static void
command(char result[32], char kind, unsigned width, unsigned base,
    unsigned x, unsigned y, unsigned rx, unsigned ry)
{
	size_t position = 1;
	result[0] = kind;
	number(result+position, width, x, base); position += width;
	number(result+position, width, y, base); position += width;
	if (kind == 'A' || kind == 'O' || kind == 'V') {
		number(result+position, 2, 0, base); number(result+position+2, 2, 360, base); position += 4;
	}
	number(result+position, width, rx, base); position += width;
	if (kind == 'o' || kind == 'O' || kind == 'V') {
		number(result+position, width, ry, base); position += width;
	}
	result[position] = 0;
}

static bool
inside(int64_t x, int64_t y, int64_t cx, int64_t cy, int64_t rx, int64_t ry)
{
	int64_t dx = 2*x+1-2*cx, dy = 2*y+1-2*cy;
	return dx*dx*ry*ry + dy*dy*rx*rx < 4*rx*rx*ry*ry;
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
	for (size_t i = 0; i < strlen(text); i++) st_rip3_stream_process(stream, (const uint8_t *)text+i, 1);
}

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	static const char kinds[] = {'A','O','V','C','A'};
	static const unsigned outline_modes[] = {15,0x5a,0x5f,5,0xaa};
	static const unsigned foreground_modes[] = {3,0x56,0x57,1,0xaa};
	static const unsigned background_modes[] = {1,0x54,0x55,1,0xaa};
	struct st_rip3_command_host host = {0};
	struct st_rip3_environment environment;
	struct st_rip3_style style;
	struct scene scene = {&host,0};
	const struct st_rip3_stream_sink sink = {.context=&scene,.command=scene_command};
	const struct st_rip3_rect bounds = {10,20,79,89}, view = {2,3,53,34};
	struct st_rip3_stream *stream = NULL;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	bool borders = false;
	unsigned current;
	CHECK(create(&host, canvas, width*2,height*2) == 0);
	host.borders = &borders;
	CHECK(st_rip3_ports_define(host.ports, 1, &bounds, false) == 0);
	CHECK(st_rip3_ports_switch(host.ports, 1) == 0 && st_rip3_ports_viewport(host.ports, &view) == 0);
	CHECK(st_rip3_ports_protect(host.ports, 1, true) == 0);
	CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
	environment.graphics_style = 7; environment.port = 9; environment.palette = 11;
	CHECK(st_rip3_environments_define(host.environments, 1, &environment) == 0);
	CHECK(st_rip3_environments_select(host.environments, 1) == 0);
	CHECK(st_rip3_environments_protect(host.environments, 1, true) == 0);
	CHECK(st_rip3_stream_create(&stream, &sink) == 0);
	for (unsigned row = 0; row < 3; row++) {
		borders = row == 1;
		if (row == 0) CHECK(process(&host, "c0F", -1) == 0);
		if (row == 1) CHECK(process(&host, "c0C", -1) == 0
		    && process(&host, "s4Q2D4Q2D4Q2D4Q2D03", -1) == 0);
		if (row == 2) CHECK(process(&host, "S013", -1) == 0);
		for (unsigned mode = 0; mode < 5; mode++) {
			struct st_rip3_rect underlay = {(int32_t)mode*10+1,(int32_t)row*10+1,
			    (int32_t)mode*10+8,(int32_t)row*10+8};
			char shape[32], write[] = "W0";
			write[1] += (char)mode;
			CHECK(process(&host, write, -1) == 0);
			if (row == 2 && mode == 4) {
				CHECK(st_rip3_styles_switch(host.styles, 1) == 0);
				CHECK(st_rip3_styles_protect(host.styles, 1, true) == 0);
			}
			CHECK(st_rip3_ports_fill(host.ports, &underlay, 0x55, ST_RIP3_COPY) == 0);
			command(shape, row == 0 ? kinds[mode] : row == 1 ? 'G' : 'o', 2,36,
			    (mode*10+4)*2,(row*10+4)*2,6,row == 2 ? 4 : 6);
			if (row == 0 && mode == 0) wire(stream, "!|O08085K000606|");
			else wire(stream, "!|");
			wire(stream, shape); wire(stream, "\r");
		}
	}
	CHECK(scene.errors == 1);
	CHECK(st_rip3_palettes_get(host.palettes, 0, &palette) == 0);
	CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		unsigned expected = 0;
		if (x >= 13 && x < 61 && y >= 24 && y < 52) {
			unsigned col = (x-13)/10, row = (y-24)/10, u = (x-13)%10, v = (y-24)%10;
			if (col < 5 && row < 3 && u < 8 && v < 8) {
				expected = 0x55;
				if (row == 0 && circle[v][u] == '#') expected = outline_modes[col];
				if (row == 1 && inside(u,v,3,3,3,3))
					expected = ((x+y)%2 == 0 ? foreground_modes : background_modes)[col];
				if (row == 1 && circle[v][u] == '#') expected = 12;
				if (row == 2 && inside(u,v,3,3,3,2)) expected = foreground_modes[col];
			}
		}
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | expected*UINT32_C(0x010101)));
	}
	CHECK(st_rip3_styles_get(host.styles, 1, &style) == 0 && style.pen.pattern == 0xaaaa
	    && style.pen.thickness == 1 && style.background_color == 1 && style.fill_color == 3);
	CHECK(st_rip3_environments_get(host.environments, 1, &environment) == 0
	    && environment.graphics_style == 7 && environment.port == 9 && environment.palette == 11);
	CHECK(st_rip3_ports_current(host.ports, &current) == 0 && current == 1);
	st_pixel_buffer_destroy(capture); st_rip3_stream_destroy(stream); destroy(&host);
	return 0;
}

static int
default_world(enum st_rip3_canvas canvas, uint32_t width, uint32_t height,
    unsigned cx, unsigned cy, unsigned ox, unsigned rx, unsigned ry)
{
	struct st_rip3_command_host host = {0};
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	bool borders = false;
	CHECK(create(&host, canvas, 640,350) == 0); host.borders = &borders;
	CHECK(process(&host, "G2S2S0K", -1) == 0); /* World center 100,100, horizontal radius 20. */
	CHECK(process(&host, "o5K2S0K0K", -1) == 0); /* X/Y world radii scale independently. */
	CHECK(st_rip3_palettes_get(host.palettes, 0, &palette) == 0);
	CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		bool on = inside(x,y,cx,cy,rx,rx) || inside(x,y,ox,cy,rx,ry);
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | (on ? UINT32_C(0x030303) : 0)));
	}
	st_pixel_buffer_destroy(capture); destroy(&host);
	return 0;
}

static int
rgb(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_command_host host = {0};
	struct st_rip3_environment environment;
	struct st_rip3_palettes *bound;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	uint32_t colors[256];
	bool borders = true;
	CHECK(create(&host, canvas, width,height) == 0); host.borders = &borders;
	CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
	environment.rgb_bits = 8;
	CHECK(st_rip3_environments_set(host.environments, 0, &environment) == 0);
	for (unsigned i = 0; i < 256; i++) colors[i] = 0xffffff;
	colors[0] = 0; colors[17] = 0x112233; colors[18] = 0x112235; colors[19] = 0x445566;
	CHECK(st_rip3_palettes_switch(host.palettes, 7) == 0);
	CHECK(st_rip3_palettes_rgb(host.palettes, 0, 8, colors, 256) == 0);
	CHECK(st_rip3_palettes_protect(host.palettes, 7, true) == 0);
	CHECK(process(&host, "c4I8q", -1) == 0 && process(&host, "kH5Lc", -1) == 0);
	CHECK(process(&host, "S014I8r", -1) == 0);
	CHECK(process(&host, "C040403", -1) == 0 && process(&host, "G0E0403", -1) == 0);
	borders = false;
	CHECK(process(&host, "s4Q2D4Q2D4Q2D4Q2D4I8r", -1) == 0);
	CHECK(process(&host, "o0O040302", -1) == 0);
	bound = host.palettes; host.palettes = NULL;
	CHECK(process(&host, "W4", -1) == 0 && process(&host, "o040E0302", -1) == 0);
	CHECK(process(&host, "C0E0E0", -1) == 0);
	CHECK(process(&host, "A180E00A003", -1) == 0);
	borders = true;
	CHECK(process(&host, "G0O0E0", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	host.palettes = bound;
	CHECK(process(&host, "W0", -1) == 0 && process(&host, "G0O0E0", -1) == 0);
	CHECK(process(&host, "A0Y0E00A003", -1) == 0);
	CHECK(st_rip3_palettes_get(host.palettes, 7, &palette) == 0);
	CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		uint32_t expected = 0;
		if (x >= 1 && x <= 8 && y >= 1 && y <= 8 && circle[y-1][x-1] == '#') expected = 0x112233;
		if (inside(x,y,14,4,3,3)) expected = 0x112235;
		if (x >= 11 && x <= 18 && y >= 1 && y <= 8 && circle[y-1][x-11] == '#') expected = 0x112233;
		if (inside(x,y,24,4,3,2)) expected = (x+y)%2 == 0 ? 0x112235 : 0x445566;
		if (inside(x,y,4,14,3,2) || (x == 14 && y == 14)) expected = 0xffffff;
		if (x == 24 && y == 14) expected = 0x112233;
		if (x >= 31 && x <= 38 && y >= 11 && y <= 18 && circle[y-11][x-31] == '#') expected = 0x112233;
		if (x >= 41 && x <= 48 && y >= 11 && y <= 18 && circle[y-11][x-41] == '#') expected = 0xffffff;
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | expected));
	}
	st_pixel_buffer_destroy(capture); destroy(&host);
	return 0;
}

static int
arc_modes(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	static const unsigned ink[] = {15,0x5a,0x5f,5,0xaa};
	struct st_rip3_command_host host = {0};
	struct st_rip3_environment environment;
	struct st_rip3_style style;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	struct scene scene = {&host,0};
	const struct st_rip3_stream_sink sink = {.context=&scene,.command=scene_command};
	struct st_rip3_stream *stream = NULL;
	const struct st_rip3_rect bounds = {10,20,79,89}, view = {2,3,53,34};
	unsigned runs = 0;

	/* Unequal X/Y world scales distinguish a horizontal circle radius from
	 * an ellipse whose Y radius would be projected independently. */
	CHECK(create(&host, canvas, width*2,height*4) == 0);
	CHECK(process(&host, "c0F", -1) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0);
	for (unsigned mode = 0; mode < 5; mode++) {
		style.write_mode = (enum st_rip3_write_mode)mode;
		CHECK(st_rip3_styles_define(host.styles, mode+1, &style) == 0);
		CHECK(st_rip3_styles_protect(host.styles, mode+1, true) == 0);
	}
	CHECK(st_rip3_ports_define(host.ports, 1, &bounds, false) == 0);
	CHECK(st_rip3_ports_switch(host.ports, 1) == 0);
	CHECK(st_rip3_ports_viewport(host.ports, &view) == 0);
	CHECK(st_rip3_ports_protect(host.ports, 1, true) == 0);
	CHECK(st_rip3_stream_create(&stream, &sink) == 0);
	CHECK(st_rip3_palettes_get(host.palettes, 0, &palette) == 0);
	for (unsigned base = 36; base <= 64; base += 28) {
		for (unsigned coordinate = 2; coordinate <= 5; coordinate++) {
			CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
			environment.base_math = base; environment.coordinate_size = coordinate;
			CHECK(st_rip3_environments_set(host.environments, 0, &environment) == 0);
			CHECK(st_rip3_ports_erase(host.ports, 0) == 0);
			for (unsigned mode = 0; mode < 5; mode++) {
				const struct st_rip3_rect underlay = {(int32_t)mode*10+1,1,(int32_t)mode*10+8,8};
				char shape[32];
				CHECK(st_rip3_styles_switch(host.styles, mode+1) == 0);
				CHECK(st_rip3_ports_fill(host.ports, &underlay, 0x55, ST_RIP3_COPY) == 0);
				command(shape, 'A', coordinate,base,(mode*10+4)*2,16,6,0);
				/* A short final XY field is valid; angles remain two digits. */
				if (mode % 2 != 0) {
					size_t last = strlen(shape)-coordinate;
					shape[last] = '6'; shape[last+1] = 0;
				}
				wire(stream, "!|"); wire(stream, shape); wire(stream, "\r");
			}
			CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
			for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
				unsigned expected = 0;
				if (x >= 13 && x <= 60 && y >= 24 && y <= 31) {
					unsigned col = (x-13)/10, u = (x-13)%10, v = y-24;
					if (col < 5 && u < 8) expected = circle[v][u] == '#' ? ink[col] : 0x55;
				}
				CHECK(capture->primary[(size_t)y*width+x]
				    == (ST_PIXEL_COLOR_RGB_TAG | expected*UINT32_C(0x010101)));
			}
			st_pixel_buffer_destroy(capture); capture = NULL;
			runs++;
		}
	}
	CHECK(runs == 8 && scene.errors == 0);
	for (unsigned mode = 0; mode < 5; mode++) {
		CHECK(st_rip3_styles_get(host.styles, mode+1, &style) == 0);
		CHECK(style.pen.pattern == 0xaaaa && style.pen.thickness == 1
		    && style.pen.draw_background && style.background_color == 1
		    && style.write_mode == (enum st_rip3_write_mode)mode);
	}
	st_rip3_stream_destroy(stream); destroy(&host);
	return 0;
}

static int
arc_noops(void)
{
	struct st_rip3_command_host host = {0}, missing;
	struct scene scene = {&host,0};
	const struct st_rip3_stream_sink sink = {.context=&scene,.command=scene_command};
	struct st_rip3_stream *stream = NULL;
	struct st_rip3_environment environment;
	struct st_rip3_style style;
	const struct st_rip3_rect view = {0,0,7,7};
	uint8_t value;
	CHECK(create(&host, ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(st_rip3_ports_viewport(host.ports, &view) == 0);
	CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0);
	style.drawing_color = style.background_color = 0xffffff;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	for (unsigned base = 36; base <= 64; base += 28) {
		const unsigned equal[] = {0,180,360,base*base-1};
		for (unsigned width = 2; width <= 5; width++) {
			CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
			environment.base_math = base; environment.coordinate_size = width;
			CHECK(st_rip3_environments_set(host.environments, 0, &environment) == 0);
			missing = host; missing.styles = NULL; missing.ports = NULL; missing.palettes = NULL;
			for (size_t i = 0; i < sizeof(equal)/sizeof(equal[0]); i++) {
				char shape[32];
				command(shape, 'A', width,base,3,3,3,0);
				number(shape+1+2*width, 2,equal[i],base);
				number(shape+3+2*width, 2,equal[i],base);
				memset(shape+5+2*width, base == 36 ? 'Z' : '&', width);
				st_rip3_ellipse_test_realloc_countdown = 1;
				st_rip3_drawing_test_fail_calloc = true;
				CHECK(process(&host, shape, -1) == 0 && process(&missing, shape, -1) == 0);
				CHECK(st_rip3_ellipse_test_realloc_countdown == 1 && st_rip3_drawing_test_fail_calloc);
				st_rip3_ellipse_test_realloc_countdown = 0;
				st_rip3_drawing_test_fail_calloc = false;
			}
		}
	}
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(host.ports, 0, x,y,&value) == 0 && value == 0x55);
	/* A malformed last radius must be rejected even when angles are equal. */
	CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
	environment.base_math = 36; environment.coordinate_size = 2;
	CHECK(st_rip3_environments_set(host.environments, 0, &environment) == 0);
	CHECK(process(&host, "A030300000?", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&host, "A030300A003", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(process(&host, "A0303000003", -1) == 0);
	CHECK(process(&host, "A03035K0003", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&host, "c0C", -1) == 0);
	CHECK(st_rip3_stream_create(&stream, &sink) == 0);
	wire(stream, "!|A030300000?|A030300A003|A03035K0003|A0303000003\r");
	CHECK(scene.errors == 2);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(host.ports, 0, x,y,&value) == 0
		    && value == (circle[y][x] == '#' ? 12 : 0x55));
	st_rip3_stream_destroy(stream); destroy(&host);
	return 0;
}

static int
boundaries(void)
{
	static const char kinds[] = {'C','G','o','O','V','A'};
	static const char *invalid[] = {"C", "C0000", "C0000000", "G00000?", "o0000000?", "O000000A0000?",
	    "A", "A030300A0", "A030300A0030", "A0303??A003", "A030300A00?", "A030300000?"};
	static const char *unsupported[] = {"O030300000303", "V03035K000303", "O030300K00303", "C030303 comment",
	    "A0303005K03", "A03035K0003", "A030300K003", "A030300A003 comment"};
	struct st_rip3_command_host host = {0}, missing = {0};
	struct st_rip3_environment environment;
	struct st_rip3_style original, style;
	struct st_rip3_ports *other = NULL;
	const struct st_rip3_rect area = {0,0,7,7};
	bool borders = true;
	char shape[32];
	uint8_t pixel;
	CHECK(create(&host, ST_RIP3_CANVAS_640_480, 640,480) == 0); host.borders = &borders;
	CHECK(st_rip3_ports_viewport(host.ports, &area) == 0 && st_rip3_ports_erase(host.ports, 0x55) == 0);
	for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++)
		CHECK(process(&host, invalid[i], ST_RIP3_COMMAND_INVALID) == 0);
	for (size_t i = 0; i < sizeof(unsupported)/sizeof(unsupported[0]); i++)
		CHECK(process(&host, unsupported[i], ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&missing, "C030303", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing = host; missing.styles = NULL;
	CHECK(process(&missing, "C030303", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing = host; missing.environments = NULL;
	CHECK(process(&missing, "C030303", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing = host; missing.ports = NULL;
	CHECK(process(&missing, "C030303", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing = host; missing.borders = NULL;
	CHECK(process(&missing, "G030303", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&missing, "o03030303", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	for (size_t i = 0; i < sizeof(kinds); i++) {
		command(shape, kinds[i], 2,36,3,3,3,3);
		for (unsigned allocation = 1; allocation <= 2; allocation++) {
			st_rip3_ellipse_test_realloc_countdown = allocation;
			CHECK(process(&host, shape, ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0
			    && st_rip3_ellipse_test_realloc_countdown == 0);
		}
		st_rip3_drawing_test_fail_calloc = true;
		CHECK(process(&host, shape, ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_drawing_test_fail_calloc);
	}
	CHECK(st_rip3_styles_get(host.styles, 0, &original) == 0);
	style = original; style.drawing_color = 0xffffff;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	CHECK(process(&host, "C030303", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(process(&host, "G030303", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	style = original; style.fill_color = 0xffffff;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	CHECK(process(&host, "G030303", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	style = original; style.background_color = 0xffffff; memset(style.fill.rows, 0xaa, 8);
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	CHECK(process(&host, "o03030303", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(st_rip3_styles_set(host.styles, 0, &original) == 0);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(host.ports, 0, x,y,&pixel) == 0 && pixel == 0x55);
	CHECK(st_rip3_ports_activate(host.ports, false) == 0);
	st_rip3_drawing_test_fail_calloc = true; st_rip3_ellipse_test_realloc_countdown = 1;
	CHECK(process(&missing, "G030303", -1) == 0 && st_rip3_drawing_test_fail_calloc
	    && st_rip3_ellipse_test_realloc_countdown == 1);
	CHECK(process(&missing, "A030300A003", -1) == 0 && st_rip3_drawing_test_fail_calloc
	    && st_rip3_ellipse_test_realloc_countdown == 1);
	CHECK(st_rip3_ports_activate(host.ports, true) == 0);
	borders = false;
	CHECK(process(&host, "o03030303", -1) == 0 && st_rip3_drawing_test_fail_calloc
	    && st_rip3_ellipse_test_realloc_countdown == 1);
	st_rip3_drawing_test_fail_calloc = false; st_rip3_ellipse_test_realloc_countdown = 0;
	borders = true;
	CHECK(st_rip3_ports_create(&other, ST_RIP3_CANVAS_800_600) == 0);
	missing = host; missing.ports = other;
	CHECK(process(&missing, "C030303", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(st_rip3_ports_read(other, 0, 3,3,&pixel) == 0 && pixel == 0);
	st_rip3_ports_destroy(other);
	for (unsigned base = 36; base <= 64; base += 28) for (unsigned width = 2; width <= 5; width++) {
		CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
		environment.base_math = base; environment.coordinate_size = width;
		CHECK(st_rip3_environments_set(host.environments, 0, &environment) == 0);
		for (size_t i = 0; i < sizeof(kinds); i++) {
			bool filled = kinds[i] == 'G' || kinds[i] == 'o';
			command(shape, kinds[i], width,base,3,3,3,3);
			for (unsigned shortened = 0; shortened < 2; shortened++) {
				if (shortened != 0) {
					size_t position = strlen(shape)-width;
					shape[position] = '3'; shape[position+1] = 0;
				}
				CHECK(st_rip3_ports_erase(host.ports, 0) == 0);
				CHECK(process(&host, shape, -1) == 0);
				for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++) {
					unsigned expected = filled && inside(x,y,3,3,3,3) ? 3 : 0;
					if (circle[y][x] == '#') expected = 12;
					CHECK(st_rip3_ports_read(host.ports, 0, x,y,&pixel) == 0 && pixel == expected);
				}
			}
		}
	}
	CHECK(st_rip3_ports_erase(host.ports, 0) == 0);
	command(shape, 'G', 5,64,0,0,3,3);
	CHECK(process(&host, shape, -1) == 0);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++) {
		unsigned expected = inside(x,y,0,0,3,3) ? 3 : 0;
		int32_t norm = x*x+y*y;
		if (norm >= 7 && norm <= 12) expected = 12;
		CHECK(st_rip3_ports_read(host.ports, 0, x,y,&pixel) == 0 && pixel == expected);
	}
	CHECK(st_rip3_ports_erase(host.ports, 0) == 0);
	command(shape, 'o', 5,64,3,3,3,0);
	CHECK(process(&host, shape, -1) == 0);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(host.ports, 0, x,y,&pixel) == 0 && pixel == (y == 3 && x <= 6 ? 12 : 0));
	CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
	environment.world_width = environment.world_height = 1;
	CHECK(st_rip3_environments_set(host.environments, 0, &environment) == 0);
	CHECK(st_rip3_ports_erase(host.ports, 0) == 0);
	/* Wide center and radius differ by one world unit = 640 device pixels.
	 * Clamping both at INT32_MAX would falsely draw the left tangent at zero. */
	CHECK(process(&host, "G&&&&&00000&&&&#", -1) == 0);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(host.ports, 0, x,y,&pixel) == 0 && pixel == 0);
	CHECK(process(&host, "G&&&&&00000&&&&&", -1) == 0);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(host.ports, 0, x,y,&pixel) == 0 && pixel == (x == 0 ? 12 : 3));
	CHECK(st_rip3_ports_erase(host.ports, 0) == 0);
	command(shape, 'A', 5,64,1073741823,0,1073741822,0);
	CHECK(process(&host, shape, -1) == 0);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(host.ports, 0, x,y,&pixel) == 0 && pixel == 0);
	command(shape, 'A', 5,64,1073741823,0,1073741823,0);
	CHECK(process(&host, shape, -1) == 0);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(host.ports, 0, x,y,&pixel) == 0 && pixel == (x == 0 ? 12 : 0));
	destroy(&host);
	return 0;
}

int
main(void)
{
	CHECK(arc_modes(ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(arc_modes(ST_RIP3_CANVAS_800_600, 800,600) == 0);
	CHECK(arc_modes(ST_RIP3_CANVAS_1024_768, 1024,768) == 0);
	CHECK(arc_noops() == 0);
	CHECK(scene(ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600, 800,600) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768, 1024,768) == 0);
	CHECK(default_world(ST_RIP3_CANVAS_640_480, 640,480, 100,137,200,20,27) == 0);
	CHECK(default_world(ST_RIP3_CANVAS_800_600, 800,600, 125,171,250,25,34) == 0);
	CHECK(default_world(ST_RIP3_CANVAS_1024_768, 1024,768, 160,219,320,32,43) == 0);
	CHECK(rgb(ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(rgb(ST_RIP3_CANVAS_800_600, 800,600) == 0);
	CHECK(rgb(ST_RIP3_CANVAS_1024_768, 1024,768) == 0);
	CHECK(boundaries() == 0);
	return 0;
}
