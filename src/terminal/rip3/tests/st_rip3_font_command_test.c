/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_styles.h"
#include "st_rip3_ports.h"
#include "st_rip3_stream.h"
#include "st_pixels.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
	CHECK(seen.errors == (error < 0 ? 0u : 1u));
	if (error >= 0) CHECK((int)seen.kind == error);
	return 0;
}

static int
create(struct st_rip3_command_host *host, enum st_rip3_canvas canvas)
{
	struct st_rip3_style initial = {.drawing_color=12,.background_color=8,.fill_color=2,
	    .pen={.pattern=0xaaaa,.thickness=3,.draw_background=true},
	    .font={3,1,4,32}, .write_mode=ST_RIP3_XOR};
	CHECK(st_rip3_fill_solid(&initial.fill, true) == 0);
	initial.fill.origin = (struct st_rip3_point){3,5};
	CHECK(st_rip3_styles_create(&host->styles, &initial) == 0);
	CHECK(st_rip3_environments_create(&host->environments, canvas) == 0);
	return 0;
}

static bool
same(const struct st_rip3_style *a, const struct st_rip3_style *b, bool font)
{
	return a->drawing_color == b->drawing_color && a->background_color == b->background_color
	    && a->fill_color == b->fill_color && a->pen.pattern == b->pen.pattern
	    && a->pen.thickness == b->pen.thickness && a->pen.draw_background == b->pen.draw_background
	    && memcmp(a->fill.rows, b->fill.rows, 8) == 0
	    && a->fill.origin.x == b->fill.origin.x && a->fill.origin.y == b->fill.origin.y
	    && a->write_mode == b->write_mode && (!font || (a->font.font == b->font.font
	    && a->font.direction == b->font.direction && a->font.magnification == b->font.magnification
	    && a->font.flags == b->font.flags));
}

static void
number(char *pair, unsigned value, unsigned base)
{
	static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#&";
	pair[0] = digits[value/base]; pair[1] = digits[value%base];
}

static int
values(void)
{
	struct st_rip3_command_host host = {0};
	struct st_rip3_style initial, before, after;
	struct st_rip3_environment environment;
	char command[] = "Y00000100";
	CHECK(create(&host, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &initial) == 0);
	for (unsigned base = 36; base <= 64; base += 28) {
		CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
		environment.base_math = base;
		environment.coordinate_size = 5; /* Y fields stay two digits. */
		environment.rgb_bits = 8; /* All Y fields still use the current radix. */
		CHECK(st_rip3_environments_set(host.environments, 0, &environment) == 0);
		for (unsigned font = 0; font <= 10; font++) for (unsigned direction = 0; direction <= 1; direction++)
			for (unsigned size = 1; size <= 10; size++) {
				number(command+1, font, base); number(command+3, direction, base);
				number(command+5, size, base); number(command+7, 0, base);
				CHECK(process(&host, command, -1) == 0);
				CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0);
				CHECK(after.font.font == font && after.font.direction == direction
				    && after.font.magnification == size && after.font.flags == 0);
				CHECK(same(&initial, &after, false));
			}
		for (unsigned flags = 0; flags <= 64; flags++) {
			unsigned vertical = flags & 28;
			bool valid = flags < 64 && (flags & 3) != 3
			    && (vertical == 0 || vertical == 4 || vertical == 8 || vertical == 16);
			CHECK(st_rip3_styles_get(host.styles, 0, &before) == 0);
			number(command+7, flags, base);
			CHECK(process(&host, command, valid ? -1 : ST_RIP3_COMMAND_INVALID) == 0);
			CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0);
			if (valid) CHECK(after.font.flags == flags && same(&initial, &after, false));
			else CHECK(same(&before, &after, true));
		}
	}
	CHECK(process(&host, "Y0000010", -1) == 0); /* Short final flags. */
	CHECK(process(&host, "J10", -1) == 0 && process(&host, "Y----010-", -1) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0 && after.font.font == 0
	    && after.font.direction == 0 && after.font.magnification == 1 && after.font.flags == 0);
	st_rip3_styles_destroy(host.styles);
	st_rip3_environments_destroy(host.environments);
	return 0;
}

static int
recovery(void)
{
	static const char *invalid[] = {"Y", "Y000001", "Y000001000", "Y0000010?",
	    "Y0B000100", "Y00020100", "Y00000000", "Y00000B00", "Y00000103",
	    "Y0000010C", "Y0000010K", "Y0000010O", "Y0000011S"};
	struct st_rip3_command_host host = {0}, missing = {0};
	struct st_rip3_style before, after;
	struct st_rip3_environment environment, saved;
	unsigned current;

	CHECK(create(&host, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &before) == 0);
	for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
		CHECK(process(&host, invalid[i], ST_RIP3_COMMAND_INVALID) == 0);
		CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0 && same(&before, &after, true));
	}
	CHECK(process(&host, "Y00000100 comment", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&missing, "Y00000100", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing.styles = host.styles;
	CHECK(process(&missing, "Y00000100", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing.styles = NULL; missing.environments = host.environments;
	CHECK(process(&missing, "Y00000100", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
	environment.graphics_style = 7; environment.port = 9; environment.palette = 11;
	CHECK(st_rip3_environments_define(host.environments, 1, &environment) == 0);
	CHECK(st_rip3_environments_select(host.environments, 1) == 0);
	CHECK(st_rip3_environments_protect(host.environments, 1, true) == 0);
	saved = environment;
	CHECK(st_rip3_styles_switch(host.styles, 2) == 0);
	CHECK(process(&host, "Y0A010A1E", -1) == 0);
	CHECK(st_rip3_styles_get(host.styles, 2, &after) == 0 && after.font.font == 10
	    && after.font.direction == 1 && after.font.magnification == 10 && after.font.flags == 50);
	CHECK(same(&before, &after, false));
	CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0 && same(&before, &after, true));
	CHECK(st_rip3_styles_current(host.styles, &current) == 0 && current == 2);
	CHECK(st_rip3_environments_get(host.environments, 1, &environment) == 0
	    && environment.graphics_style == saved.graphics_style && environment.port == saved.port
	    && environment.palette == saved.palette && environment.base_math == saved.base_math);
	CHECK(st_rip3_styles_get(host.styles, 2, &before) == 0);
	CHECK(st_rip3_styles_protect(host.styles, 2, true) == 0);
	CHECK(process(&host, "Y00000100", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(st_rip3_styles_get(host.styles, 2, &after) == 0 && same(&before, &after, true));
	CHECK(st_rip3_styles_switch(host.styles, 0) == 0);
	CHECK(process(&host, "Y00000100", -1) == 0);
	st_rip3_styles_destroy(host.styles);
	st_rip3_environments_destroy(host.environments);
	return 0;
}

struct scene { struct st_rip3_command_host *host; unsigned errors; };

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
	st_rip3_commands_process(scene->host, &sink, bytes, length);
}

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	static const char *wire[] = {"!|Y00000100\r", "!|Y0000010X\r", "!|Y00000103|Y0001021\r"};
	uint8_t glyphs[255*8] = {0};
	const uint8_t text[] = {'A',219}; /* Explicit ASCII and CP437 glyph codes. */
	const struct st_rip3_text_position anchors[] = {{20,10},{60,20},{100,50}};
	struct st_rip3_classic_font font = {.canvas=canvas,.number=0};
	struct st_rip3_command_host host = {0};
	struct st_rip3_style style;
	struct scene scene = {&host,0};
	const struct st_rip3_stream_sink sink = {.context=&scene,.command=scene_command};
	struct st_rip3_stream *stream = NULL;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;

	/* Independent 3x8 test cells: A is solid, CP437 219 has a left stem. */
	memset(glyphs+('A'-1)*8, 0xe0, 8);
	memset(glyphs+(219-1)*8, 0x80, 8);
	font.data.bitmap = (struct st_rip3_maf_font){3,8,1,8,glyphs};
	CHECK(create(&host, canvas) == 0);
	CHECK(st_rip3_ports_create(&host.ports, canvas) == 0);
	CHECK(st_rip3_ports_erase(host.ports, 1) == 0);
	CHECK(st_rip3_stream_create(&stream, &sink) == 0);
	CHECK(process(&host, "W0", -1) == 0);
	for (size_t i = 0; i < 3; i++) {
		for (size_t n = 0; n < strlen(wire[i]); n++)
			st_rip3_stream_process(stream, (const uint8_t *)wire[i]+n, 1);
		CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0);
		CHECK(style.font.font == 0);
		CHECK(st_rip3_draw_classic_text(host.ports, &font, &style.font,
		    text, sizeof(text), anchors+i, 12,8,style.write_mode) == 0);
	}
	CHECK(scene.errors == 1);
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		/* Left/top, right/top with one-pixel shadow, vertical/right at 2x. */
		bool ink = x >= 20 && x <= 23 && y >= 10 && y <= 17;
		ink |= x >= 54 && x <= 57 && y >= 20 && y <= 27;
		ink |= x >= 100 && x <= 115 && y >= 55 && y <= 62;
		bool shadow = x >= 55 && x <= 58 && y >= 21 && y <= 28;
		uint32_t expected = ink ? 0xff5555 : shadow ? 0x555555 : 0x0000aa;
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | expected));
	}
	st_pixel_buffer_destroy(capture);
	st_rip3_stream_destroy(stream);
	st_rip3_ports_destroy(host.ports);
	st_rip3_styles_destroy(host.styles);
	st_rip3_environments_destroy(host.environments);
	return 0;
}

int
main(void)
{
	CHECK(values() == 0);
	CHECK(recovery() == 0);
	CHECK(scene(ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600, 800,600) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768, 1024,768) == 0);
	return 0;
}
