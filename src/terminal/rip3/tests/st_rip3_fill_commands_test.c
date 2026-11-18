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
	struct st_rip3_style initial = {.drawing_color=12,.background_color=1,.fill_color=2,
	    .pen={.pattern=0xaaaa,.thickness=3,.draw_background=true},
	    .font={3,1,4,32},.write_mode=ST_RIP3_COPY};
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

static bool
same(const struct st_rip3_style *a, const struct st_rip3_style *b, bool fill)
{
	return a->drawing_color == b->drawing_color && a->background_color == b->background_color
	    && a->pen.pattern == b->pen.pattern && a->pen.thickness == b->pen.thickness && a->line_type == b->line_type
	    && a->pen.draw_background == b->pen.draw_background && a->write_mode == b->write_mode
	    && a->font.font == b->font.font && a->font.direction == b->font.direction
	    && a->font.magnification == b->font.magnification && a->font.flags == b->font.flags
	    && a->fill.origin.x == b->fill.origin.x && a->fill.origin.y == b->fill.origin.y
	    && (!fill || (a->fill_color == b->fill_color && memcmp(a->fill.rows,b->fill.rows,8) == 0));
}

static void
pair(char *result, unsigned value, unsigned base)
{
	static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#&";
	result[0] = digits[value/base]; result[1] = digits[value%base];
}

static int
attributes(void)
{
	struct st_rip3_command_host host = {0};
	struct st_rip3_style initial, before, after;
	struct st_rip3_environment environment;
	char command[] = "s000000000000000003";
	static const char *invalid[] = {"S", "S01", "S0100000", "S0C03", "S010?", "S01074",
	    "s0000000000000000", "s000000000000000003000", "s000000000000007403", "s00000000000000000?",
	    "B", "B000000", "B000000000", "B0000000?"};
	CHECK(create(&host, ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &initial) == 0);
	for (unsigned base = 36; base <= 64; base += 28) {
		CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
		environment.base_math = base; environment.coordinate_size = 5;
		CHECK(st_rip3_environments_set(host.environments, 0, &environment) == 0);
		for (unsigned value = 0; value <= 255; value++) {
			for (size_t i = 0; i < 8; i++) pair(command+1+2*i, value, base);
			CHECK(process(&host, command, -1) == 0);
			CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0 && same(&initial,&after,false));
			CHECK(after.fill_color == 3);
			for (size_t i = 0; i < 8; i++) CHECK(after.fill.rows[i] == value);
		}
		pair(command+15, 256, base);
		CHECK(st_rip3_styles_get(host.styles, 0, &before) == 0);
		CHECK(process(&host, command, ST_RIP3_COMMAND_INVALID) == 0);
		CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0 && same(&before,&after,true));
		CHECK(process(&host, "S01003&", base == 64 ? -1 : ST_RIP3_COMMAND_INVALID) == 0);
		CHECK(process(&host, "S000", -1) == 0);
		CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0 && after.fill_color == 0
		    && same(&initial,&after,false));
		for (size_t i = 0; i < 8; i++) CHECK(after.fill.rows[i] == 0);
		CHECK(process(&host, "S013", -1) == 0);
		CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0 && after.fill_color == 3);
		for (size_t i = 0; i < 8; i++) CHECK(after.fill.rows[i] == 255);
	}
	CHECK(process(&host, "J10", -1) == 0 && process(&host, "n2", -1) == 0);
	const struct st_rip3_rect underlay = {0,0,7,7};
	CHECK(st_rip3_ports_fill(host.ports, &underlay, 0x55, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &before) == 0);
	for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
		CHECK(process(&host, invalid[i], ST_RIP3_COMMAND_INVALID) == 0);
		CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0 && same(&before,&after,true));
		for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++) {
			uint8_t pixel;
			CHECK(st_rip3_ports_read(host.ports, 0, x,y,&pixel) == 0 && pixel == 0x55);
		}
	}
	for (unsigned pattern = 2; pattern <= 11; pattern++) {
		char stock[] = "S0203";
		pair(stock+1, pattern, 36);
		CHECK(st_rip3_styles_get(host.styles, 0, &before) == 0);
		CHECK(process(&host, stock, -1) == 0);
		CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0 && same(&before,&after,false));
	}
	CHECK(process(&host, "S0103 comment", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&host, "s000000000000000003 comment", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&host, "B00000101 comment", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&host, "s----------------3", -1) == 0); /* Local MegaNum zero spelling. */
	CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0 && after.fill_color == 3);
	for (size_t i = 0; i < 8; i++) CHECK(after.fill.rows[i] == 0);
	destroy(&host);
	return 0;
}

struct scene { const struct st_rip3_command_host *host; unsigned errors; };

static void
scene_error(void *context, enum st_rip3_command_diagnostic kind)
{
	struct scene *scene = context;
	(void)kind;
	scene->errors++;
}

static void
scene_command(void *context, const uint8_t *bytes, size_t length)
{
	struct scene *scene = context;
	const struct st_rip3_command_sink sink = {scene,scene_error};
	st_rip3_commands_process(scene->host, &sink, bytes,length);
}

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	static const char *wire =
	    "!|S0103|B08080202|S000F|B0G020K08|S0109|B01010101|B0A0A0AA\r"
	    "!|s3K1S0W0G0804020107|B000K0E0Y|B0U0Y0G0K\r"
	    "!|S0105|B1E1E2S2S|S010F|W0|B140K1A0K|W1|B140M1A0M|W2|B140O1A0O|W3|B140Q1A0Q|W4|B140S1A0S\r"
	    "!|W0|S0C03|S010B|B0Q0A0S0C\r";
	/* Context rows 33..40, columns 12..27: the diagonal samples X mod 8
	 * equal to Y mod 8, independent of the stored viewport/caller phase. */
	static const uint16_t rows[] = {0x0404,0x0202,0x0101,0x8080,0x4040,0x2020,0x1010,0x0808};
	static const uint8_t modes[] = {15,0x5a,0x5f,5,0xaa};
	struct st_rip3_command_host host = {0};
	struct st_rip3_style style;
	struct st_rip3_environment environment;
	struct scene scene = {&host,0};
	const struct st_rip3_stream_sink sink = {.context=&scene,.command=scene_command};
	const struct st_rip3_rect bounds = {10,20,49,59}, view = {2,3,29,30}, underlay = {20,10,23,14};
	struct st_rip3_stream *stream = NULL;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	unsigned current;
	CHECK(create(&host, canvas, width*2,height*2) == 0);
	CHECK(st_rip3_ports_define(host.ports, 1, &bounds, false) == 0);
	CHECK(st_rip3_ports_switch(host.ports, 1) == 0 && st_rip3_ports_viewport(host.ports, &view) == 0);
	CHECK(st_rip3_ports_fill(host.ports, &underlay, 0x55, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_protect(host.ports, 1, true) == 0);
	CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
	environment.graphics_style = 7; environment.port = 9; environment.palette = 11;
	CHECK(st_rip3_environments_define(host.environments, 1, &environment) == 0);
	CHECK(st_rip3_environments_select(host.environments, 1) == 0);
	CHECK(st_rip3_environments_protect(host.environments, 1, true) == 0);
	CHECK(st_rip3_stream_create(&stream, &sink) == 0);
	for (size_t i = 0; i < strlen(wire); i++) st_rip3_stream_process(stream, (const uint8_t *)wire+i, 1);
	CHECK(scene.errors == 1);
	CHECK(st_rip3_palettes_get(host.palettes, 0, &palette) == 0);
	CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		unsigned expected = 0;
		if (x >= 13 && x <= 16 && y >= 24 && y <= 27) expected = 3;
		if (x >= 20 && x <= 22 && y >= 24 && y <= 27) expected = 1;
		if ((x == 12 && y == 23) || (x == 17 && y == 28)) expected = 9;
		if (x >= 12 && x <= 27 && y >= 33 && y <= 40)
			expected = (rows[y-33] & (0x8000u >> (x-12))) != 0 ? 7 : 1;
		if (x >= 37 && x <= 39 && y >= 48 && y <= 50) expected = 5;
		if (x >= 32 && x <= 35 && y >= 33 && y <= 37) expected = modes[y-33];
		if (x >= 25 && x <= 26 && y >= 28 && y <= 29) expected = 11;
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | expected*UINT32_C(0x010101)));
	}
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0 && style.drawing_color == 12
	    && style.background_color == 1 && style.fill_color == 11 && style.pen.pattern == 0xaaaa
	    && style.pen.thickness == 3 && style.font.font == 3 && style.fill.origin.x == 1 && style.fill.origin.y == 2);
	CHECK(st_rip3_environments_get(host.environments, 1, &environment) == 0
	    && environment.graphics_style == 7 && environment.port == 9 && environment.palette == 11);
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
	struct st_rip3_environment environment;
	struct st_rip3_palette palette;
	struct st_rip3_style style;
	struct st_rip3_palettes *bound;
	struct st_pixel_buffer *capture = NULL;
	uint32_t colors[256];
	CHECK(create(&host, canvas, width,height) == 0);
	CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
	environment.rgb_bits = 8;
	CHECK(st_rip3_environments_set(host.environments, 0, &environment) == 0);
	for (unsigned i = 0; i < 256; i++) colors[i] = 0xffffff;
	colors[0] = 0; colors[17] = 0x112233; colors[18] = 0x112235; colors[19] = 0x445566;
	CHECK(st_rip3_palettes_switch(host.palettes, 7) == 0);
	CHECK(st_rip3_palettes_rgb(host.palettes, 0, 8, colors, 256) == 0);
	CHECK(st_rip3_palettes_protect(host.palettes, 7, true) == 0);
	CHECK(process(&host, "s73007300730073004I8q", -1) == 0);
	CHECK(process(&host, "kH5Lc", -1) == 0);
	CHECK(process(&host, "B00000707", -1) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0 && style.fill_color == 0x112234);
	CHECK(process(&host, "S01&&&&", -1) == 0);
	CHECK(process(&host, "S01F", -1) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0 && style.fill_color == 15);
	bound = host.palettes; host.palettes = NULL;
	CHECK(st_rip3_ports_activate(host.ports, false) == 0);
	CHECK(process(&host, "B08000901", -1) == 0);
	CHECK(st_rip3_ports_activate(host.ports, true) == 0);
	CHECK(process(&host, "B08000901", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&host, "W4", -1) == 0 && process(&host, "B0A010A01", -1) == 0);
	host.palettes = bound;
	CHECK(st_rip3_palettes_get(host.palettes, 7, &palette) == 0);
	CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		uint32_t expected = 0;
		if (x <= 7 && y <= 7) expected = y%2 == 0 ? 0x112233 : 0x445566;
		if (x == 10 && y == 1) expected = 0xffffff;
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | expected));
	}
	st_pixel_buffer_destroy(capture);
	destroy(&host);
	return 0;
}

static int
boundaries(void)
{
	struct st_rip3_command_host host = {0}, missing = {0};
	struct st_rip3_style before, after;
	struct st_rip3_environment environment;
	struct st_rip3_ports *other = NULL;
	const struct st_rip3_rect view = {0,0,7,7};
	uint8_t value;
	char command[24];
	CHECK(create(&host, ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(process(&missing, "S013", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing.styles = host.styles;
	CHECK(process(&missing, "s00000000000000003", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing.environments = host.environments;
	CHECK(process(&missing, "S013", -1) == 0);
	CHECK(process(&missing, "B00000101", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(st_rip3_styles_switch(host.styles, 1) == 0);
	CHECK(st_rip3_styles_get(host.styles, 1, &before) == 0);
	CHECK(st_rip3_styles_protect(host.styles, 1, true) == 0);
	CHECK(process(&host, "S0104", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(process(&host, "s00000000000000004", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(st_rip3_styles_get(host.styles, 1, &after) == 0 && same(&before,&after,true));
	CHECK(process(&host, "B01010101", -1) == 0);
	CHECK(st_rip3_ports_read(host.ports, 0, 1,1,&value) == 0 && value == 3);
	CHECK(st_rip3_styles_switch(host.styles, 0) == 0);
	for (unsigned width = 2; width <= 5; width++) {
		CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
		environment.coordinate_size = width;
		CHECK(st_rip3_environments_set(host.environments, 0, &environment) == 0);
		(void)snprintf(command, sizeof(command), "B%0*u%0*u%0*u3", (int)width,1u,(int)width,1u,(int)width,3u);
		CHECK(process(&host, command, -1) == 0);
		CHECK(st_rip3_ports_read(host.ports, 0, 3,3,&value) == 0 && value == 3);
	}
	CHECK(st_rip3_ports_create(&other, ST_RIP3_CANVAS_800_600) == 0);
	missing = host; missing.ports = other;
	CHECK(process(&missing, "B0000100001000033", ST_RIP3_COMMAND_INVALID) == 0);
	st_rip3_ports_destroy(other);
	CHECK(st_rip3_ports_viewport(host.ports, &view) == 0);
	CHECK(process(&host, "J1S", -1) == 0);
	CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
	environment.world_width = environment.world_height = 1;
	CHECK(st_rip3_environments_set(host.environments, 0, &environment) == 0);
	CHECK(process(&host, "B&&&&&&&&&&&&&&&&&&&&", -1) == 0);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(host.ports, 0, x,y,&value) == 0
		    && value == (x >= 1 && x <= 3 && y >= 1 && y <= 3 ? 3 : 0));
	CHECK(process(&host, "B0000000000&&&&&&&&&&", -1) == 0);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(host.ports, 0, x,y,&value) == 0 && value == 3);
	CHECK(process(&host, "S015", -1) == 0 && process(&host, "B&&&&&&&&&&0000000000", -1) == 0);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(host.ports, 0, x,y,&value) == 0 && value == 5);
	CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0);
	after.fill_color = 0xffffff; after.background_color = 7;
	CHECK(st_rip3_fill_solid(&after.fill, false) == 0);
	CHECK(st_rip3_styles_set(host.styles, 0, &after) == 0);
	CHECK(process(&host, "B00000000000000000000", -1) == 0);
	CHECK(st_rip3_ports_read(host.ports, 0, 0,0,&value) == 0 && value == 7);
	after.fill_color = 9; after.background_color = 0xffffff;
	CHECK(st_rip3_fill_solid(&after.fill, true) == 0);
	CHECK(st_rip3_styles_set(host.styles, 0, &after) == 0);
	CHECK(process(&host, "B00000000000000000000", -1) == 0);
	CHECK(st_rip3_ports_read(host.ports, 0, 0,0,&value) == 0 && value == 9);
	memset(after.fill.rows, 0xaa, 8);
	CHECK(st_rip3_styles_set(host.styles, 0, &after) == 0);
	/* Resolve both used inks before painting any pixels. */
	CHECK(process(&host, "B00000000000000000000", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(st_rip3_ports_read(host.ports, 0, 0,0,&value) == 0 && value == 9);
	CHECK(st_rip3_ports_activate(host.ports, false) == 0);
	CHECK(process(&host, "B00000000000000000000", -1) == 0);
	CHECK(st_rip3_ports_activate(host.ports, true) == 0);
	CHECK(process(&host, "W4", -1) == 0 && process(&host, "B00000000000000000000", -1) == 0);
	CHECK(st_rip3_ports_read(host.ports, 0, 0,0,&value) == 0 && value == 246);
	destroy(&host);
	return 0;
}

static int
filled_scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	static const char initial[] = "!|S013|N01|K08080202|K0A0A0A0E\r";
	static const char *commands[] = {"K020K0A0S","K0I0K0Q0S","K0Y0K160S","K02100A18","K0I100Q18"};
	static const uint8_t modes[] = {15,0x5a,0x5f,5,0xaa};
	static const uint8_t border_modes[] = {12,0x59,0x5d,4,0xaa};
	static const struct st_rip3_point origins[] = {{1,10},{9,10},{17,10},{1,18},{9,18}};
	struct st_rip3_command_host host = {0};
	struct st_rip3_style style;
	struct st_rip3_environment environment;
	struct scene scene = {&host,0};
	const struct st_rip3_stream_sink sink = {.context=&scene,.command=scene_command};
	const struct st_rip3_rect bounds = {10,20,49,59}, view = {2,3,29,30};
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
	for (size_t i = 0; i < strlen(initial); i++)
		st_rip3_stream_process(stream, (const uint8_t *)initial+i, 1);
	CHECK(scene.errors == 1 && !borders); /* N remains unsupported, K still runs. */
	borders = true;
	CHECK(process(&host, "S010F", -1) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0);
	style.pen.pattern = 0x8000; style.pen.thickness = 1;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	for (size_t i = 0; i < 5; i++) {
		struct st_rip3_rect underlay = {origins[i].x,origins[i].y,origins[i].x+4,origins[i].y+4};
		char write[] = "W0";
		write[1] += (char)i;
		CHECK(process(&host, write, -1) == 0);
		if (i == 4) {
			CHECK(st_rip3_styles_switch(host.styles, 1) == 0);
			CHECK(st_rip3_styles_protect(host.styles, 1, true) == 0);
		}
		CHECK(st_rip3_ports_fill(host.ports, &underlay, 0x55, ST_RIP3_COPY) == 0);
		CHECK(process(&host, commands[i], -1) == 0);
	}
	CHECK(process(&host, "K1E021E0A", -1) == 0); /* Degenerate NOT border only. */
	CHECK(process(&host, "K1E0E1E0E", -1) == 0);
	CHECK(st_rip3_styles_switch(host.styles, 0) == 0);
	CHECK(process(&host, "W0", -1) == 0);
	CHECK(process(&host, "K1E1E2828", -1) == 0); /* Clip, without moving far borders inward. */
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0);
	style.pen.thickness = 3;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	CHECK(process(&host, "K1E0U1E10", -1) == 0);
	CHECK(st_rip3_palettes_get(host.palettes, 0, &palette) == 0);
	CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		unsigned expected = 0;
		if (x >= 13 && x <= 15 && y >= 24 && y <= 26) expected = 3;
		for (size_t i = 0; i < 5; i++) {
			uint32_t left = 12+(uint32_t)origins[i].x, top = 23+(uint32_t)origins[i].y;
			if (x >= left && x <= left+4 && y >= top && y <= top+4)
				expected = x == left || x == left+4 || y == top || y == top+4 ? border_modes[i] : modes[i];
		}
		if (x == 37 && ((y >= 24 && y <= 28) || y == 30)) expected = 255;
		if (x >= 37 && x <= 39 && y >= 48 && y <= 50)
			expected = x == 37 || y == 48 ? 12 : 15;
		if (x >= 36 && x <= 38 && y >= 37 && y <= 42) expected = 12;
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | expected*UINT32_C(0x010101)));
	}
	CHECK(st_rip3_environments_get(host.environments, 1, &environment) == 0
	    && environment.graphics_style == 7 && environment.port == 9 && environment.palette == 11);
	CHECK(st_rip3_styles_current(host.styles, &current) == 0 && current == 0);
	CHECK(st_rip3_ports_current(host.ports, &current) == 0 && current == 1 && borders);
	st_pixel_buffer_destroy(capture);
	st_rip3_stream_destroy(stream);
	destroy(&host);
	return 0;
}

static int
filled_rgb(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_command_host host = {0};
	struct st_rip3_environment environment;
	struct st_rip3_style style;
	struct st_rip3_palettes *bound;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	uint32_t colors[256];
	bool borders = true;
	CHECK(create(&host, canvas, width,height) == 0);
	host.borders = &borders;
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0);
	style.pen.thickness = 1;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
	environment.rgb_bits = 8;
	CHECK(st_rip3_environments_set(host.environments, 0, &environment) == 0);
	for (unsigned i = 0; i < 256; i++) colors[i] = 0xffffff;
	colors[0] = 0; colors[17] = 0x112233; colors[18] = 0x112235; colors[19] = 0x445566;
	CHECK(st_rip3_palettes_rgb(host.palettes, 0, 8, colors, 256) == 0);
	CHECK(process(&host, "s73007300730073004I8q", -1) == 0);
	CHECK(process(&host, "cH5Lc", -1) == 0 && process(&host, "k4I8r", -1) == 0);
	CHECK(process(&host, "K01010606", -1) == 0);
	bound = host.palettes; host.palettes = NULL;
	CHECK(process(&host, "W4", -1) == 0);
	CHECK(process(&host, "K08010D06", -1) == 0); /* NOT fill and border ignore both inks. */
	borders = false;
	CHECK(process(&host, "K0F010I04", -1) == 0); /* NOT fill needs neither ink. */
	CHECK(process(&host, "W0", -1) == 0);
	CHECK(process(&host, "K0F060F09", -1) == 0); /* No interior, no border, no lookup. */
	host.borders = NULL;
	CHECK(st_rip3_ports_activate(host.ports, false) == 0);
	CHECK(process(&host, "K00000101", -1) == 0);
	CHECK(st_rip3_ports_activate(host.ports, true) == 0);
	CHECK(process(&host, "K00000101", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	host.borders = &borders; borders = true; host.palettes = bound;
	CHECK(process(&host, "W4", -1) == 0 && process(&host, "K08010D06", -1) == 0);
	CHECK(st_rip3_palettes_get(host.palettes, 0, &palette) == 0);
	CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		uint32_t expected = 0;
		if (x >= 1 && x <= 6 && y >= 1 && y <= 6)
			expected = x == 1 || x == 6 || y == 1 || y == 6 ? 0x445566 : y%2 == 0 ? 0x112233 : 0x112235;
		if (x >= 8 && x <= 13 && y >= 1 && y <= 6)
			expected = 0; /* Two complete NOT operations restore the original region. */
		if (x >= 15 && x <= 17 && y >= 1 && y <= 3) expected = 0xffffff;
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | expected));
	}
	st_pixel_buffer_destroy(capture);
	destroy(&host);
	return 0;
}

static int
filled_boundaries(void)
{
	struct st_rip3_command_host host = {0}, missing;
	struct st_rip3_style style;
	struct st_rip3_environment environment;
	struct st_rip3_ports *other = NULL;
	const struct st_rip3_rect area = {0,0,7,7};
	static const char *invalid[] = {"K", "K000000", "K000000000", "K0000040?"};
	uint8_t pixel;
	bool borders = true;
	char command[24];
	CHECK(create(&host, ST_RIP3_CANVAS_640_480, 640,480) == 0);
	host.borders = &borders;
	CHECK(st_rip3_ports_fill(host.ports, &area, 0x55, ST_RIP3_COPY) == 0);
	for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++)
		CHECK(process(&host, invalid[i], ST_RIP3_COMMAND_INVALID) == 0);
	missing = host; missing.borders = NULL;
	CHECK(process(&missing, "K01010404", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&host, "K01010404 comment", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0);
	style.pen.thickness = 1;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	st_rip3_drawing_test_fail_calloc = true;
	CHECK(process(&host, "K01010404", ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0
	    && !st_rip3_drawing_test_fail_calloc);
	st_rip3_drawing_test_fail_calloc = true;
	CHECK(process(&host, "K04040404", ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0
	    && !st_rip3_drawing_test_fail_calloc);
	style.drawing_color = 0xffffff; /* Border fails after the fill ink could resolve. */
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	CHECK(process(&host, "K01010404", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	style.drawing_color = 12; style.fill_color = 0xffffff;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	CHECK(process(&host, "K01010404", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	style.fill_color = 3; style.background_color = 0xffffff;
	memset(style.fill.rows, 0xaa, 8);
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	CHECK(process(&host, "K01010404", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(host.ports, 0, x,y,&pixel) == 0 && pixel == 0x55);
	borders = false;
	CHECK(process(&host, "K04040404", -1) == 0); /* No fill, no border, no ink lookup. */
	CHECK(process(&host, "S013", -1) == 0);
	for (unsigned base = 36; base <= 64; base += 28) for (unsigned width = 2; width <= 5; width++) {
		CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
		environment.base_math = base; environment.coordinate_size = width;
		CHECK(st_rip3_environments_set(host.environments, 0, &environment) == 0);
		(void)snprintf(command, sizeof(command), "K%0*u%0*u%0*u3", (int)width,1u,(int)width,1u,(int)width,3u);
		CHECK(process(&host, command, -1) == 0);
		CHECK(st_rip3_ports_read(host.ports, 0, 2,2,&pixel) == 0 && pixel == 3);
		CHECK(st_rip3_ports_read(host.ports, 0, 3,3,&pixel) == 0 && pixel == 0x55);
	}
	CHECK(process(&host, "n2", -1) == 0 && process(&host, "K0a010&03", -1) == 0);
	CHECK(st_rip3_ports_read(host.ports, 0, 36,1,&pixel) == 0 && pixel == 3);
	CHECK(st_rip3_ports_read(host.ports, 0, 62,2,&pixel) == 0 && pixel == 3);
	CHECK(st_rip3_ports_read(host.ports, 0, 63,2,&pixel) == 0 && pixel == 0);
	CHECK(process(&host, "n5", -1) == 0);
	CHECK(st_rip3_ports_create(&other, ST_RIP3_CANVAS_800_600) == 0);
	missing = host; missing.ports = other;
	CHECK(process(&missing, "K0000100001000033", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(st_rip3_ports_read(other, 0, 1,1,&pixel) == 0 && pixel == 0);
	st_rip3_ports_destroy(other);
	CHECK(st_rip3_ports_viewport(host.ports, &area) == 0);
	CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0);
	CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
	environment.world_width = environment.world_height = 1;
	CHECK(st_rip3_environments_set(host.environments, 0, &environment) == 0);
	CHECK(process(&host, "K&&&&&&&&&&&&&&&&&&&&", -1) == 0);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(host.ports, 0, x,y,&pixel) == 0 && pixel == 0x55);
	CHECK(process(&host, "K0000000000&&&&&&&&&&", -1) == 0);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(host.ports, 0, x,y,&pixel) == 0 && pixel == 3);
	CHECK(process(&host, "S015", -1) == 0);
	CHECK(process(&host, "K&&&&&&&&&&0000000000", -1) == 0);
	borders = true;
	CHECK(process(&host, "K&&&&&&&&&&0000000000", -1) == 0);
	for (int32_t y = 0; y < 8; y++) for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(host.ports, 0, x,y,&pixel) == 0 && pixel == (x == 0 || y == 0 ? 12 : 5));
	destroy(&host);
	return 0;
}

static int
stock(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	/* Literal pictures from the pinned table, independent of the byte masks. */
	static const unsigned presets[] = {3,6,8,9,10};
	static const char *pictures[] = {
	    ".......#" "......#." ".....#.." "....#..." "...#...." "..#....." ".#......" "#.......",
	    "#.#..#.#" "##.#..#." ".##.#..#" "#.##.#.." ".#.##.#." "..#.##.#" "#..#.##." ".#..#.##",
	    "#......#" ".#....#." "..#..#.." "...##..." "...##..." "..#..#.." ".#....#." "#......#",
	    "##..##.." "..##..##" "##..##.." "..##..##" "##..##.." "..##..##" "##..##.." "..##..##",
	    "#......." "........" "....#..." "........" "#......." "........" "....#..." "........",
	};
	struct st_rip3_command_host host = {0};
	struct st_rip3_style style;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	CHECK(create(&host, canvas, width,height) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0);
	style.fill.origin = (struct st_rip3_point){0,0};
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	for (size_t i = 0; i < 5; i++) {
		char setting[] = "S0307", bar[] = "B00000707";
		CHECK(strlen(pictures[i]) == 64);
		pair(setting+1, presets[i], 36);
		pair(bar+1, (unsigned)i*16, 36); pair(bar+5, (unsigned)i*16+7, 36);
		CHECK(process(&host, setting, -1) == 0 && process(&host, bar, -1) == 0);
	}
	CHECK(st_rip3_palettes_get(host.palettes, 0, &palette) == 0);
	CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		unsigned expected = 0;
		if (y < 8 && x/16 < 5 && x%16 < 8)
			expected = pictures[x/16][y*8+x%16] == '#' ? 7 : 1;
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | expected*UINT32_C(0x010101)));
	}
	st_pixel_buffer_destroy(capture);
	destroy(&host);
	return 0;
}

static int
default_world(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, char kind,
    uint32_t left, uint32_t right, uint32_t top, uint32_t bottom)
{
	struct st_rip3_command_host host = {0};
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	bool borders = false;
	char command[] = "B0A0A0K0K";
	CHECK(create(&host, canvas, 640,350) == 0);
	host.borders = &borders; command[0] = kind;
	CHECK(process(&host, "S013", -1) == 0 && process(&host, command, -1) == 0);
	CHECK(st_rip3_palettes_get(host.palettes, 0, &palette) == 0);
	CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		bool on = x >= left && x <= right && y >= top && y <= bottom;
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | (on ? UINT32_C(0x030303) : 0)));
	}
	st_pixel_buffer_destroy(capture);
	destroy(&host);
	return 0;
}

int
main(void)
{
	CHECK(attributes() == 0);
	CHECK(scene(ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600, 800,600) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768, 1024,768) == 0);
	CHECK(rgb(ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(rgb(ST_RIP3_CANVAS_800_600, 800,600) == 0);
	CHECK(rgb(ST_RIP3_CANVAS_1024_768, 1024,768) == 0);
	CHECK(boundaries() == 0);
	CHECK(stock(ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(stock(ST_RIP3_CANVAS_800_600, 800,600) == 0);
	CHECK(stock(ST_RIP3_CANVAS_1024_768, 1024,768) == 0);
	CHECK(filled_scene(ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(filled_scene(ST_RIP3_CANVAS_800_600, 800,600) == 0);
	CHECK(filled_scene(ST_RIP3_CANVAS_1024_768, 1024,768) == 0);
	CHECK(filled_rgb(ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(filled_rgb(ST_RIP3_CANVAS_800_600, 800,600) == 0);
	CHECK(filled_rgb(ST_RIP3_CANVAS_1024_768, 1024,768) == 0);
	CHECK(filled_boundaries() == 0);
	CHECK(default_world(ST_RIP3_CANVAS_640_480, 640,480, 'B', 10,20,13,27) == 0);
	CHECK(default_world(ST_RIP3_CANVAS_800_600, 800,600, 'B', 12,25,17,34) == 0);
	CHECK(default_world(ST_RIP3_CANVAS_1024_768, 1024,768, 'B', 16,32,21,43) == 0);
	CHECK(default_world(ST_RIP3_CANVAS_640_480, 640,480, 'K', 10,19,13,26) == 0);
	CHECK(default_world(ST_RIP3_CANVAS_800_600, 800,600, 'K', 12,24,17,33) == 0);
	CHECK(default_world(ST_RIP3_CANVAS_1024_768, 1024,768, 'K', 16,31,21,42) == 0);
	return 0;
}
