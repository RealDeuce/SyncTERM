/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_styles.h"
#include "st_rip3_ports.h"
#include "st_rip3_palettes.h"
#include "st_rip3_stream.h"
#include "st_pixels.h"
#include "io_error.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
		fprintf(stderr, "%s: errors %u, kind %d, expected %d\n", command, seen.errors, (int)seen.kind, error);
	CHECK(seen.errors == (error < 0 ? 0u : 1u));
	if (error >= 0) CHECK((int)seen.kind == error);
	return 0;
}

static int
create(struct st_rip3_command_host *host, enum st_rip3_canvas canvas, uint32_t world_width, uint32_t world_height)
{
	struct st_rip3_style initial = {.drawing_color=3,.background_color=1,.fill_color=2,
	    .pen={.pattern=0xffff,.thickness=1}, .font={0,0,1,0}, .write_mode=ST_RIP3_COPY};
	struct st_rip3_environment env;
	uint32_t colors[256];
	CHECK(st_rip3_fill_solid(&initial.fill, true) == 0);
	CHECK(st_rip3_styles_create(&host->styles, &initial) == 0);
	CHECK(st_rip3_ports_create(&host->ports, canvas) == 0);
	CHECK(st_rip3_palettes_create(&host->palettes) == 0);
	CHECK(st_rip3_environments_create(&host->environments, canvas) == 0);
	CHECK(st_rip3_environments_get(host->environments, 0, &env) == 0);
	env.world_width = world_width; env.world_height = world_height;
	CHECK(st_rip3_environments_set(host->environments, 0, &env) == 0);
	for (unsigned i = 0; i < 256; i++) colors[i] = i*UINT32_C(0x010101);
	CHECK(st_rip3_palettes_rgb(host->palettes, 0, 8, colors, 256) == 0);
	return 0;
}

static void
destroy(struct st_rip3_command_host *host)
{
	st_rip3_styles_destroy(host->styles);
	st_rip3_ports_destroy(host->ports);
	st_rip3_palettes_destroy(host->palettes);
	st_rip3_environments_destroy(host->environments);
}

static int
pixel(struct st_rip3_ports *ports, unsigned number, int32_t x, int32_t y, uint8_t expected)
{
	uint8_t actual = 0;
	CHECK(st_rip3_ports_read(ports, number, x,y, &actual) == 0 && actual == expected);
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

static void
wire(struct st_rip3_stream *stream, const char *input)
{
	for (size_t i = 0; i < strlen(input); i++)
		st_rip3_stream_process(stream, (const uint8_t *)input+i, 1);
}

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	static const char *input =
	    "!|c03|W00|R02020A08|c04|R0K0S0C0K|c05|L020G0G0G\r\n"
	    "!|c0F|W0|L0S020Y02|W1|L0S040Y04|W2|L0Y060S06|W3|L0S080Y08|W4|L0S0A0Y0A\r"
	    "!|W0|W05|c07|c03 comment|L00102S10|W1|c08|R040K040O|R0M0K0Q0O|c09|L0101011\r"
	    "!|L00000000?|W0|c000A|k0000|R020A08C\r";
	struct st_rip3_command_host host = {0};
	struct stream_scene scene = {&host,0};
	const struct st_rip3_stream_sink sink = {.context=&scene,.command=scene_command};
	struct st_rip3_stream *stream = NULL;
	struct st_rip3_style style;
	struct st_rip3_environment env;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	const struct st_rip3_rect bounds = {10,20,39,49}, view = {2,3,21,22}, underlay = {14,1,17,5};
	static const uint8_t modes[] = {15,0x5a,0x5f,5,0xaa};
	static const uint8_t patterned_rectangle[3][4] = {{4,1,4,1},{1,0,0,4},{4,1,4,1}};
	unsigned current;

	CHECK(create(&host, canvas, width*2,height*2) == 0);
	CHECK(st_rip3_ports_define(host.ports, 1, &bounds, false) == 0);
	CHECK(st_rip3_ports_switch(host.ports, 1) == 0 && st_rip3_ports_viewport(host.ports, &view) == 0);
	CHECK(st_rip3_ports_fill(host.ports, &underlay, 0x55, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_protect(host.ports, 1, true) == 0);
	CHECK(st_rip3_environments_get(host.environments, 0, &env) == 0);
	env.graphics_style = 7; env.port = 3; env.palette = 9;
	CHECK(st_rip3_environments_define(host.environments, 1, &env) == 0);
	CHECK(st_rip3_environments_select(host.environments, 1) == 0);
	CHECK(st_rip3_environments_protect(host.environments, 1, true) == 0);
	CHECK(st_rip3_stream_create(&stream, &sink) == 0);
	wire(stream, input);
	CHECK(scene.errors == 3);
	{
		struct st_rip3_style prepared;
		CHECK(st_rip3_styles_get(host.styles,0,&prepared)==0
		    && st_rip3_styles_define(host.styles,1,&prepared)==0);
	}
	CHECK(st_rip3_styles_switch(host.styles, 1) == 0);
	wire(stream, "!|=140XPM01\r");
	wire(stream, "!|k1|c4|W0\r");
	CHECK(st_rip3_styles_protect(host.styles, 1, true) == 0);
	wire(stream, "!|c02|L020S0G0S|L0G0W020W|L2S0Y000Y|R0S0K0Y0O\r");
	CHECK(scene.errors == 4);
	CHECK(st_rip3_palettes_get(host.palettes, 0, &palette) == 0);
	CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		unsigned expected = 0;
		if (((x == 13 || x == 17) && y >= 24 && y <= 27)
		    || ((y == 24 || y == 27) && x >= 13 && x <= 17)) expected = 3;
		if (((x == 18 || x == 22) && y >= 33 && y <= 37)
		    || ((y == 33 || y == 37) && x >= 18 && x <= 22)) expected = 4;
		if (x >= 13 && x <= 20 && y == 31) expected = 5;
		if (x >= 26 && x <= 29 && y >= 24 && y <= 28) expected = modes[y-24];
		if (x >= 12 && x <= 31 && y == 41) expected = 7;
		if (x == 14 && y >= 33 && y <= 35) expected = 8;
		if (((x == 23 || x == 25) && y >= 33 && y <= 35)
		    || ((y == 33 || y == 35) && x >= 23 && x <= 25)) expected = 8;
		if (x == 12 && y == 23) expected = 9;
		if (x >= 13 && x <= 16 && y >= 28 && y <= 29) expected = 10;
		if (x >= 13 && x <= 20 && y == 37) expected = (x-13)%2 == 0 ? 4 : 1;
		if (x >= 13 && x <= 20 && y == 39) expected = (20-x)%2 == 0 ? 4 : 1;
		if (x >= 12 && x <= 31 && y == 40) expected = (x-12)%2 == 0 ? 4 : 1;
		if (x >= 26 && x <= 29 && y >= 33 && y <= 35) expected = patterned_rectangle[y-33][x-26];
		if (capture->primary[(size_t)y*width+x] != (ST_PIXEL_COLOR_RGB_TAG | expected*UINT32_C(0x010101)))
			fprintf(stderr, "pixel %" PRIu32 ",%" PRIu32 " actual %08" PRIx32 " expected index %u\n", x,y, capture->primary[(size_t)y*width+x], expected);
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | expected*UINT32_C(0x010101)));
	}
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0 && style.drawing_color == 10
	    && style.background_color == 0 && style.fill_color == 2 && style.pen.pattern == 0xffff
	    && style.write_mode == ST_RIP3_COPY && style.font.magnification == 1);
	CHECK(st_rip3_styles_get(host.styles, 1, &style) == 0 && style.drawing_color == 4 && style.background_color == 1);
	CHECK(st_rip3_environments_get(host.environments, 1, &env) == 0
	    && env.graphics_style == 7 && env.port == 3 && env.palette == 9);
	CHECK(st_rip3_ports_current(host.ports, &current) == 0 && current == 1);
	st_pixel_buffer_destroy(capture);
	st_rip3_stream_destroy(stream);
	destroy(&host);
	return 0;
}

static int
rgb(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_command_host host = {0};
	struct st_rip3_environment env;
	struct st_rip3_style style;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	struct st_rip3_palettes *bound;
	uint32_t colors[256];

	CHECK(create(&host, canvas, width,height) == 0);
	CHECK(st_rip3_environments_get(host.environments, 0, &env) == 0);
	env.rgb_bits = 8;
	CHECK(st_rip3_environments_set(host.environments, 0, &env) == 0);
	CHECK(st_rip3_palettes_switch(host.palettes, 7) == 0);
	for (unsigned i = 0; i < 256; i++) colors[i] = 0xffffff;
	colors[0] = 0; colors[17] = 0x112233; colors[18] = 0x112235; colors[19] = 0x445566;
	CHECK(st_rip3_palettes_rgb(host.palettes, 0, 8, colors, 256) == 0);
	CHECK(st_rip3_palettes_protect(host.palettes, 7, true) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0);
	style.pen.pattern = 0xaaaa; style.pen.draw_background = true;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	CHECK(process(&host, "c4I8q", -1) == 0); /* UltraNum 0x112234, even in base 36. */
	CHECK(process(&host, "kH5Lc", -1) == 0); /* UltraNum 0x445566. */
	CHECK(process(&host, "L01010401", -1) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0
	    && style.drawing_color == 0x112234 && style.background_color == 0x445566);
	bound = host.palettes; host.palettes = NULL;
	CHECK(st_rip3_ports_activate(host.ports, false) == 0);
	st_rip3_drawing_test_fail_calloc = true;
	CHECK(process(&host, "L08010901", -1) == 0 && st_rip3_drawing_test_fail_calloc);
	st_rip3_drawing_test_fail_calloc = false;
	CHECK(st_rip3_ports_activate(host.ports, true) == 0);
	CHECK(process(&host, "L08010901", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&host, "W4", -1) == 0);
	CHECK(process(&host, "L06010601", -1) == 0); /* NOT needs no colour lookup. */
	host.palettes = bound;
	CHECK(process(&host, "c&&&&", -1) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0 && style.drawing_color == 0xffffff);
	CHECK(process(&host, "cF", -1) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0 && style.drawing_color == 15);
	CHECK(st_rip3_palettes_get(host.palettes, 7, &palette) == 0);
	CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		uint32_t expected = 0;
		if (y == 1 && x >= 1 && x <= 4) expected = x%2 ? 0x112233 : 0x445566;
		if (x == 6 && y == 1) expected = 0xffffff;
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | expected));
	}
	st_pixel_buffer_destroy(capture);
	destroy(&host);
	return 0;
}

static int
validation(void)
{
	const char *invalid[] = {"L", "L000000", "L000000000", "L0000000?", "R0000000?",
	    "W", "W000", "W05", "c", "c00000", "c074", "k074", "c0?"};
	const char *unsupported[] = {"W.", "W0.", "c01 comment", "k01 comment", "W0 comment", "L00000101 comment"};
	struct st_rip3_command_host host = {0}, missing = {0};
	struct st_rip3_style style;
	const struct st_rip3_rect area = {0,0,7,7};

	CHECK(create(&host, ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(st_rip3_ports_fill(host.ports, &area, 0x55, ST_RIP3_COPY) == 0);
	for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
		CHECK(process(&host, invalid[i], ST_RIP3_COMMAND_INVALID) == 0);
		CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0 && style.drawing_color == 3
		    && style.background_color == 1 && style.write_mode == ST_RIP3_COPY);
		for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
			CHECK(pixel(host.ports, 0, x,y, 0x55) == 0);
	}
	for (size_t i = 0; i < sizeof(unsupported)/sizeof(unsupported[0]); i++) {
		CHECK(process(&host, unsupported[i], ST_RIP3_COMMAND_UNSUPPORTED) == 0);
		CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0 && style.drawing_color == 3
		    && style.background_color == 1 && style.write_mode == ST_RIP3_COPY);
	}
	CHECK(process(&missing, "W0", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing.environments = host.environments;
	CHECK(process(&missing, "c01", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&missing, "L00000101", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing.styles = host.styles;
	CHECK(process(&missing, "L00000101", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&missing, "W0", -1) == 0);
	{
		struct st_rip3_style prepared;
		CHECK(st_rip3_styles_get(host.styles,0,&prepared)==0
		    && st_rip3_styles_define(host.styles,1,&prepared)==0);
	}
	CHECK(st_rip3_styles_switch(host.styles, 1) == 0 && st_rip3_styles_protect(host.styles, 1, true) == 0);
	CHECK(process(&host, "W1", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(process(&host, "c01", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(process(&host, "k02", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	st_rip3_drawing_test_fail_calloc = true;
	CHECK(process(&host, "L00000700", ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_drawing_test_fail_calloc);
	st_rip3_drawing_test_fail_calloc = true;
	CHECK(process(&host, "R00000707", ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_drawing_test_fail_calloc);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(pixel(host.ports, 0, x,y, 0x55) == 0);
	CHECK(process(&host, "R00000707", -1) == 0);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(pixel(host.ports, 0, x,y, x == 0 || y == 0 || x == 7 || y == 7 ? 3 : 0x55) == 0);
	CHECK(st_rip3_ports_activate(host.ports, false) == 0);
	st_rip3_drawing_test_fail_calloc = true;
	CHECK(process(&host, "L00000700", -1) == 0 && st_rip3_drawing_test_fail_calloc);
	st_rip3_drawing_test_fail_calloc = false;
	CHECK(st_rip3_ports_activate(host.ports, true) == 0);
	{
		struct st_rip3_palettes *bound = host.palettes;
		host.palettes = NULL;
		CHECK(process(&host, "L09090909", -1) == 0 && pixel(host.ports, 0, 9,9, 3) == 0);
		host.palettes = bound;
	}
	{
		struct st_rip3_command_host wrong = host;
		CHECK(st_rip3_ports_create(&wrong.ports, ST_RIP3_CANVAS_800_600) == 0);
		CHECK(process(&wrong, "L00000101", ST_RIP3_COMMAND_INVALID) == 0);
		CHECK(pixel(wrong.ports, 0, 0,0, 0) == 0);
		st_rip3_ports_destroy(wrong.ports);
	}
	destroy(&host);
	return 0;
}

static int
line_styles(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	static const unsigned widths[] = {0,1,2,3,4,5,255,256,257,511,512,1295,4095};
	static const unsigned device[] = {1,1,2,3,4,5,255,1,1,255,1,15,255};
	static const unsigned scaled[7][13] = {
		{1,1,1,1,2,2,127,128,128,255,1,135,255},
		{1,1,2,3,4,5,255,1,1,255,1,15,255},
		{2,2,4,6,8,10,254,1,2,254,1,30,254},
		{1,1,1,1,1,1,85,85,85,170,170,175,85},
		{128,128,1,128,1,128,128,1,128,128,1,128,128},
		{32,32,64,96,128,160,224,1,32,224,1,224,224},
		{1,1,1,1,1,1,1,1,1,1,1,1,1},
	};
	const struct {
		unsigned world_width, world_height;
		const unsigned *scaled;
	} profiles[] = {
		{2*width,2*height,scaled[0]},
		{width,350,scaled[1]},
		{width/2,2*height,scaled[2]},
		{3*width,height/2,scaled[3]},
		{1,height,scaled[4+(unsigned)canvas]},
	};
	static const uint16_t patterns[] = {0xffff,0x3333,0x1e3f,0x1f1f,0xaaaa};
	static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#&";
	static const char *const invalid[] = {"=", "=000000", "=000000000", "=40000003", "=05000003", "=04000003", "=0400000?", "=04ZZZZ03"};
	struct st_rip3_command_host host = {0}, missing;
	struct st_rip3_style style;
	struct st_rip3_environment env;
	char bytes[10] = "=00000000";
	CHECK(create(&host,canvas,width,height) == 0);
	CHECK(st_rip3_environments_get(host.environments,0,&env) == 0);
	for (unsigned radix = 36; radix <= 64; radix += 28) for (unsigned xy = 2; xy <= 5; xy++) {
		env.base_math = radix; env.coordinate_size = xy;
		for (size_t p = 0; p < sizeof(profiles)/sizeof(profiles[0]); p++) {
			env.world_width = profiles[p].world_width; env.world_height = profiles[p].world_height;
			CHECK(st_rip3_environments_set(host.environments,0,&env) == 0);
			for (unsigned flags = 0; flags < 4; flags++) for (size_t w = 0; w < sizeof(widths)/sizeof(widths[0]); w++) {
				if (widths[w] >= radix*radix) continue;
				bytes[1] = digits[flags]; bytes[7] = digits[widths[w]/radix]; bytes[8] = digits[widths[w]%radix];
				CHECK(process(&host,bytes,-1) == 0);
				CHECK(st_rip3_styles_get(host.styles,0,&style) == 0);
				CHECK(style.pen.pattern == 0xffff && style.line_type == 0 && style.pen.draw_background == (flags == 1));
				CHECK(style.pen.thickness == (flags&2 ? profiles[p].scaled[w] : device[w]));
				CHECK(style.drawing_color == 3 && style.background_color == 1 && style.fill_color == 2
				    && style.write_mode == ST_RIP3_COPY && style.font.magnification == 1);
			}
		}
	}
	env.base_math = 36; env.world_width = width; env.world_height = height;
	CHECK(st_rip3_environments_set(host.environments,0,&env) == 0);
	for (unsigned s = 0; s < 5; s++) {
		memcpy(bytes,"=000XPM03",10); bytes[2] = digits[s];
		CHECK(process(&host,bytes,-1) == 0);
		CHECK(st_rip3_styles_get(host.styles,0,&style) == 0 && style.pen.pattern == patterns[s] && style.line_type == s
		    && style.pen.thickness == 3 && !style.pen.draw_background);
	}
	CHECK(process(&host,"=0000003",-1) == 0); /* Only the final thickness shortens. */
	CHECK(process(&host,"=00ZZZZ03",-1) == 0); /* Stock patterns ignore user_pat. */
	CHECK(process(&host,"=------03",-1) == 0); /* MegaNum dash-as-zero. */
	for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
		CHECK(process(&host,invalid[i],ST_RIP3_COMMAND_INVALID) == 0);
		CHECK(st_rip3_styles_get(host.styles,0,&style) == 0 && style.pen.pattern == 0xffff
		    && style.pen.thickness == 3 && !style.pen.draw_background);
	}
	CHECK(process(&host,"=00000003 comment",ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing = host; missing.ports = NULL; /* Attribute edits need no PORT binding. */
	CHECK(process(&missing,"=20000003",-1) == 0);
	missing.environments = NULL;
	CHECK(process(&missing,"=00000003",ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	{
		struct st_rip3_style prepared;
		CHECK(st_rip3_styles_get(host.styles,0,&prepared)==0
		    && st_rip3_styles_define(host.styles,1,&prepared)==0);
	}
	CHECK(st_rip3_styles_switch(host.styles,1) == 0);
	CHECK(st_rip3_styles_protect(host.styles,1,true) == 0);
	CHECK(process(&host,"=141BLC01",ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(st_rip3_styles_get(host.styles,1,&style) == 0 && style.pen.pattern == 0xffff && style.pen.thickness == 3);
	destroy(&host);
	return 0;
}

static int
widths(void)
{
	struct st_rip3_command_host host = {0};
	const struct st_rip3_rect clear = {0,0,7,7};
	char command[64];
	CHECK(create(&host, ST_RIP3_CANVAS_640_480, 640,480) == 0);
	for (unsigned width = 2; width <= 5; width++) {
		CHECK(st_rip3_ports_fill(host.ports, &clear, 0, ST_RIP3_COPY) == 0);
		(void)snprintf(command, sizeof(command), "n%u", width);
		CHECK(process(&host, command, -1) == 0);
		(void)snprintf(command, sizeof(command), "L%0*u%0*u%0*u1", (int)width,1u,(int)width,1u,(int)width,3u);
		CHECK(process(&host, command, -1) == 0);
		for (int32_t x = 1; x <= 3; x++) CHECK(pixel(host.ports, 0, x,1, 3) == 0);
		(void)snprintf(command, sizeof(command), "R%0*u%0*u%0*u2", (int)width,4u,(int)width,4u,(int)width,2u);
		CHECK(process(&host, command, -1) == 0);
		CHECK(pixel(host.ports, 0, 2,3, 3) == 0 && pixel(host.ports, 0, 3,3, 0) == 0 && pixel(host.ports, 0, 4,3, 3) == 0);
	}
	CHECK(process(&host, "n2", -1) == 0 && process(&host, "J1S", -1) == 0);
	CHECK(process(&host, "c000&", -1) == 0 && process(&host, "k0a", -1) == 0);
	CHECK(process(&host, "L0a010#01", -1) == 0);
	for (int32_t x = 36; x <= 62; x++) CHECK(pixel(host.ports, 0, x,1, 63) == 0);
	CHECK(process(&host, "R0&040a02", -1) == 0);
	CHECK(pixel(host.ports, 0, 36,3, 63) == 0 && pixel(host.ports, 0, 63,3, 63) == 0 && pixel(host.ports, 0, 37,3, 0) == 0);
	CHECK(process(&host, "c003&", -1) == 0 && process(&host, "c040", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&host, "c0@", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&host, "c-", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&host, "J10", -1) == 0 && process(&host, "c-", -1) == 0 && process(&host, "W--", -1) == 0);
	CHECK(process(&host, "c03", -1) == 0);
	CHECK(process(&host, "=00000003", -1) == 0);
	CHECK(process(&host, "L20102810", -1) == 0);
	CHECK(pixel(host.ports, 0, 72,35, 3) == 0 && pixel(host.ports, 0, 72,37, 3) == 0);
	CHECK(pixel(host.ports, 0, 72,34, 0) == 0 && pixel(host.ports, 0, 71,36, 3) == 0
	    && pixel(host.ports, 0, 70,36, 0) == 0);
	destroy(&host);
	return 0;
}

static int
default_world(enum st_rip3_canvas canvas, uint32_t width, uint32_t height,
    uint32_t left, uint32_t right, uint32_t line_y, uint32_t top, uint32_t bottom)
{
	struct st_rip3_command_host host = {0};
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;

	CHECK(create(&host, canvas, 640,350) == 0);
	CHECK(process(&host, "L0A0A0K0A", -1) == 0);
	CHECK(process(&host, "R0A0K0K0U", -1) == 0);
	CHECK(st_rip3_palettes_get(host.palettes, 0, &palette) == 0);
	CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		bool on = (y == line_y && x >= left && x <= right)
		    || ((y == top || y == bottom) && x >= left && x <= right)
		    || ((x == left || x == right) && y >= top && y <= bottom);
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | (on ? UINT32_C(0x030303) : 0)));
	}
	st_pixel_buffer_destroy(capture);
	destroy(&host);
	return 0;
}

static int
erase_scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, bool direct)
{
	struct st_rip3_command_host host = {0};
	struct st_rip3_environment environment;
	struct st_rip3_style style;
	struct st_rip3_port_info before, after;
	struct st_rip3_palette palette;
	struct st_rip3_palettes *bound;
	struct st_pixel_buffer *capture = NULL;
	struct stream_scene scene = {&host,0};
	const struct st_rip3_stream_sink sink = {.context=&scene,.command=scene_command};
	struct st_rip3_stream *stream = NULL;
	const struct st_rip3_rect bounds = {10,20,39,49}, view = {2,3,21,22}, mark = {0,0,25,5};
	unsigned current;
	CHECK(create(&host, canvas, width*2,height*2) == 0);
	CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0);
	CHECK(st_rip3_ports_define(host.ports, 2, &bounds, false) == 0);
	CHECK(st_rip3_ports_switch(host.ports, 2) == 0);
	CHECK(st_rip3_ports_fill(host.ports, &mark, 12, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_protect(host.ports, 2, true) == 0);
	CHECK(st_rip3_ports_define(host.ports, 1, &bounds, false) == 0);
	CHECK(st_rip3_ports_switch(host.ports, 1) == 0 && st_rip3_ports_viewport(host.ports, &view) == 0);
	CHECK(st_rip3_ports_protect(host.ports, 1, true) == 0);
	CHECK(st_rip3_ports_info(host.ports, 1, &before) == 0);
	{
		struct st_rip3_style prepared;
		CHECK(st_rip3_styles_get(host.styles,0,&prepared)==0
		    && st_rip3_styles_define(host.styles,1,&prepared)==0);
	}
	CHECK(st_rip3_styles_switch(host.styles, 1) == 0);
	CHECK(st_rip3_styles_get(host.styles, 1, &style) == 0);
	style.background_color = direct ? 0x112234 : 7;
	style.drawing_color = style.fill_color = 0xffffff;
	style.write_mode = ST_RIP3_NOT;
	CHECK(st_rip3_fill_solid(&style.fill, false) == 0);
	CHECK(st_rip3_styles_set(host.styles, 1, &style) == 0);
	CHECK(st_rip3_styles_protect(host.styles, 1, true) == 0);
	CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
	environment.base_math = 64; environment.coordinate_size = 5;
	environment.world_width = environment.world_height = 1;
	environment.graphics_style = 7; environment.port = 9; environment.palette = 11;
	environment.rgb_bits = direct ? 8 : 0;
	CHECK(st_rip3_environments_define(host.environments, 1, &environment) == 0);
	CHECK(st_rip3_environments_select(host.environments, 1) == 0);
	CHECK(st_rip3_environments_protect(host.environments, 1, true) == 0);
	if (direct) {
		uint32_t colors[256];
		for (unsigned i = 0; i < 256; i++) colors[i] = i*UINT32_C(0x010101);
		colors[7] = 0x112233; colors[8] = 0x112235;
		CHECK(st_rip3_palettes_switch(host.palettes, 7) == 0);
		CHECK(st_rip3_palettes_rgb(host.palettes, 0, 8, colors, 256) == 0);
		CHECK(st_rip3_palettes_protect(host.palettes, 7, true) == 0);
	}
	bound = host.palettes; host.palettes = NULL;
	CHECK(process(&host, "E0", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(pixel(host.ports, 2, 2,3,12) == 0);
	if (direct) {
		CHECK(process(&host, "E", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
		CHECK(pixel(host.ports, 2, 2,3,12) == 0);
	}
	CHECK(st_rip3_ports_protect(host.ports, 1, false) == 0);
	CHECK(st_rip3_ports_activate(host.ports, false) == 0);
	CHECK(st_rip3_ports_protect(host.ports, 1, true) == 0);
	st_rip3_drawing_test_fail_calloc = true;
	CHECK(process(&host, "E", -1) == 0 && st_rip3_drawing_test_fail_calloc);
	CHECK(st_rip3_ports_protect(host.ports, 1, false) == 0);
	CHECK(st_rip3_ports_activate(host.ports, true) == 0);
	CHECK(st_rip3_ports_protect(host.ports, 1, true) == 0);
	if (direct) host.palettes = bound;
	CHECK(st_rip3_stream_create(&stream, &sink) == 0);
	wire(stream, "!|E0|E\r");
	CHECK(scene.errors == 1 && st_rip3_drawing_test_fail_calloc);
	st_rip3_drawing_test_fail_calloc = false;
	host.palettes = bound;
	CHECK(st_rip3_palettes_get(host.palettes, direct ? 7 : 0, &palette) == 0);
	CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		uint32_t expected = x >= 10 && x <= 35 && y >= 20 && y <= 25 ? 0x0c0c0c : 0x555555;
		if (x >= 12 && x <= 31 && y >= 23 && y <= 42) expected = direct ? 0x112233 : 0x070707;
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | expected));
	}
	CHECK(st_rip3_styles_get(host.styles, 1, &style) == 0 && style.write_mode == ST_RIP3_NOT
	    && style.background_color == (direct ? 0x112234u : 7u)
	    && style.drawing_color == 0xffffff && style.fill_color == 0xffffff);
	CHECK(st_rip3_environments_get(host.environments, 1, &environment) == 0
	    && environment.graphics_style == 7 && environment.port == 9 && environment.palette == 11
	    && environment.base_math == 64 && environment.coordinate_size == 5 && environment.world_width == 1);
	CHECK(st_rip3_ports_current(host.ports, &current) == 0 && current == 1);
	CHECK(st_rip3_styles_current(host.styles, &current) == 0 && current == 1);
	CHECK(st_rip3_ports_info(host.ports, 1, &after) == 0 && after.protected && after.viewport_active
	    && memcmp(&before.bounds,&after.bounds,sizeof(before.bounds)) == 0
	    && memcmp(&before.viewport,&after.viewport,sizeof(before.viewport)) == 0);
	st_pixel_buffer_destroy(capture); st_rip3_stream_destroy(stream); destroy(&host);
	return 0;
}

static int
erase_boundaries(void)
{
	struct st_rip3_command_host host = {0}, missing = {0};
	struct st_rip3_ports *other = NULL;
	const struct st_rip3_rect bounds = {0,0,7,7}, view = {1,1,6,6};
	CHECK(create(&host, ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(process(&missing, "E", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing.styles = host.styles;
	CHECK(process(&missing, "E", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing.environments = host.environments;
	CHECK(process(&missing, "E", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(st_rip3_ports_create(&other, ST_RIP3_CANVAS_800_600) == 0);
	missing = host; missing.ports = other;
	CHECK(process(&missing, "E", ST_RIP3_COMMAND_INVALID) == 0);
	st_rip3_ports_destroy(other);
	CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0);
	CHECK(st_rip3_ports_define(host.ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(host.ports, 1) == 0 && st_rip3_ports_erase(host.ports, 3) == 0);
	CHECK(st_rip3_ports_viewport(host.ports, &view) == 0);
	CHECK(process(&host, "E0", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&host, "E comment", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&host, "E", -1) == 0);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++) {
		CHECK(pixel(host.ports, 1, x,y,x >= 1 && x <= 6 && y >= 1 && y <= 6 ? 1 : 3) == 0);
		CHECK(pixel(host.ports, 0, x,y,0x55) == 0);
	}
	const struct st_rip3_rect single = {0,0,0,0};
	CHECK(st_rip3_ports_viewport(host.ports, &single) == 0);
	CHECK(process(&host, "E", -1) == 0);
	CHECK(pixel(host.ports, 1, 0,0,1) == 0 && pixel(host.ports, 1, 0,1,3) == 0);
	CHECK(pixel(host.ports, 0, 0,0,0x55) == 0);
	destroy(&host);
	return 0;
}

static int
large_coordinates(void)
{
	struct st_rip3_command_host host = {0};
	CHECK(create(&host, ST_RIP3_CANVAS_640_480, 1,1) == 0);
	CHECK(process(&host, "J1S", -1) == 0 && process(&host, "n5", -1) == 0);
	/* Projected X is twice projected Y: independently expect the stroke
	 * from (0,0) to (1024*640,512*480) to cover exactly slope 3/8. */
	CHECK(process(&host, "L0000000000000G000080", -1) == 0);
	CHECK(pixel(host.ports, 0, 8,3, 3) == 0 && pixel(host.ports, 0, 8,4, 0) == 0);
	CHECK(process(&host, "L0000000000&&&&&&&&&&", -1) == 0);
	CHECK(pixel(host.ports, 0, 4,3, 3) == 0);
	CHECK(process(&host, "R0000000000&&&&&&&&&&", -1) == 0);
	CHECK(pixel(host.ports, 0, 500,0, 3) == 0 && pixel(host.ports, 0, 0,400, 3) == 0);
	{
		struct st_rip3_style style;
		CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0);
		style.pen.pattern = 0xaaaa; style.pen.draw_background = true;
		CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
		CHECK(process(&host, "L&&&&&000000000000000", -1) == 0);
		CHECK(pixel(host.ports, 0, 0,0, 3) == 0 && pixel(host.ports, 0, 1,0, 1) == 0);
	}
	destroy(&host);
	return 0;
}

static int
rgb_precisions(void)
{
	/* Independent packed codes and full-range channel normalization. The
	 * lowercase UltraNum digits distinguish RGB from the global MegaNum base. */
	static const char *const codes[]={"5","R","1T","9z","rL","4KC","LbR","11GC"};
	static const uint32_t colors[]={0xff00ff,0x55aaff,0x246db6,0x2277dd,0x1952ad,0x105131,0x0a64b7,0x04140c};
	struct st_rip3_command_host host={0};
	CHECK(create(&host,ST_RIP3_CANVAS_640_480,640,480)==0);
	CHECK(st_rip3_palettes_rgb(host.palettes,40,8,colors,8)==0);
	for (unsigned radix=36;radix<=64;radix+=28) for (unsigned bits=1;bits<=8;bits++) {
		struct st_rip3_environment environment;
		CHECK(st_rip3_environments_get(host.environments,0,&environment)==0);
		environment.base_math=radix;environment.rgb_bits=bits;
		CHECK(st_rip3_environments_set(host.environments,0,&environment)==0);
		char command[20];
		(void)snprintf(command,sizeof(command),"c%s",codes[bits-1]);
		CHECK(process(&host,command,-1)==0 && process(&host,"X0000",-1)==0);
		CHECK(pixel(host.ports,0,0,0,(uint8_t)(39+bits))==0);
		(void)snprintf(command,sizeof(command),"S01%s",codes[bits-1]);
		CHECK(process(&host,command,-1)==0 && process(&host,"K01010303",-1)==0);
		CHECK(pixel(host.ports,0,2,2,(uint8_t)(39+bits))==0);
		(void)snprintf(command,sizeof(command),"k%s",codes[bits-1]);
		CHECK(process(&host,command,-1)==0 && process(&host,"E",-1)==0);
		CHECK(pixel(host.ports,0,500,400,(uint8_t)(39+bits))==0);
		uint8_t index=17;
		CHECK(st_rip3_palettes_color_bits(host.palettes,bits,0xffffff,&index)==0 && index==255);
	}
	uint8_t unchanged=17;
	CHECK(st_rip3_palettes_color_bits(host.palettes,9,0,&unchanged)==IO_ERROR_OUT_OF_RANGE && unchanged==17);
	CHECK(st_rip3_palettes_color_bits(host.palettes,1,0x1000000,&unchanged)==IO_ERROR_OUT_OF_RANGE && unchanged==17);
	CHECK(st_rip3_palettes_color_bits(NULL,1,0,&unchanged)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_palettes_color_bits(host.palettes,1,0,NULL)==IO_ERROR_INVALID_ARGUMENT);
	destroy(&host);return 0;
}

int
main(void)
{
	CHECK(rgb_precisions()==0);
	CHECK(line_styles(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(line_styles(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(line_styles(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	CHECK(scene(ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600, 800,600) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768, 1024,768) == 0);
	CHECK(rgb(ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(rgb(ST_RIP3_CANVAS_800_600, 800,600) == 0);
	CHECK(rgb(ST_RIP3_CANVAS_1024_768, 1024,768) == 0);
	CHECK(validation() == 0);
	CHECK(widths() == 0);
	CHECK(default_world(ST_RIP3_CANVAS_640_480, 640,480, 10,20,13,27,41) == 0);
	CHECK(default_world(ST_RIP3_CANVAS_800_600, 800,600, 12,25,17,34,51) == 0);
	CHECK(default_world(ST_RIP3_CANVAS_1024_768, 1024,768, 16,32,21,43,65) == 0);
	CHECK(large_coordinates() == 0);
	CHECK(erase_scene(ST_RIP3_CANVAS_640_480, 640,480, false) == 0);
	CHECK(erase_scene(ST_RIP3_CANVAS_800_600, 800,600, false) == 0);
	CHECK(erase_scene(ST_RIP3_CANVAS_1024_768, 1024,768, false) == 0);
	CHECK(erase_scene(ST_RIP3_CANVAS_640_480, 640,480, true) == 0);
	CHECK(erase_scene(ST_RIP3_CANVAS_800_600, 800,600, true) == 0);
	CHECK(erase_scene(ST_RIP3_CANVAS_1024_768, 1024,768, true) == 0);
	CHECK(erase_boundaries() == 0);
	return 0;
}
