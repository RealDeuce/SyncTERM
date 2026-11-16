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
	static const uint8_t input[] =
	    "!|2C1000000002000002020|2C100000000208000A025|2C100000000208000A021|2C10000000020G000I022\r\n"
	    "!|2C10000000020O000Q023|2C10000000020W000Y024\r"
	    "!|2C100000K0K30400140A0|2C1000000014000002020\r"
	    "!|2C1000000005000000000|2C1000000006000000010\r"
	    "!|2C700000A00704000E00000000|2C900000A00A00000A000\r";
	struct st_rip3_command_host host = {0};
	struct stream_scene scene = {&host,0};
	const struct st_rip3_stream_sink sink = {.context=&scene,.command=scene_command};
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
	CHECK(st_rip3_stream_create(&stream, &sink) == 0);
	for (size_t i = 0; i < sizeof(input)-1; i++) st_rip3_stream_process(stream, input+i, 1);
	CHECK(scene.errors == 1); /* Invalid mode was consumed; the following copy ran. */
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
	/* Literal 28 is MegaNum 80; the canonical prose's 72 is reported in #10. */
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
	const char *invalid[] = {"2C", "2C100000101000000101", "2C1000001010000001010000000",
	    "2C100000101000000101?", "2C1000001010000001015", "2C1010000010000001010",
	    "2C1000001010010000010", "2C10000010100000010100000?"};
	const char *unsupported[] = {"2C1000001010010100000", "2C100000101000000101000001",
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
	for (size_t i = 0; i < sizeof(unsupported)/sizeof(unsupported[0]); i++) {
		CHECK(process(&host, unsupported[i], ST_RIP3_COMMAND_UNSUPPORTED) == 0);
		CHECK(row(host.ports, 0, 0,0, baseline, 8) == 0);
	}
	CHECK(process(&missing, "2C1000001010000001010", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing.ports = host.ports;
	CHECK(process(&missing, "2C1000001010000001010", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&host, "2C2000000000010100000", -1) == 0); /* Undefined source, including zero-endpoint branch. */
	CHECK(process(&host, "2C0000001012000001010", -1) == 0); /* Undefined destination. */
	CHECK(process(&host, "2C0000001010ZZZZZZZZ0", -1) == 0);
	CHECK(process(&host, "2C1ZZZZZZZZ0000001010", -1) == 0); /* Wholly external COPY is explicitly a no-op. */
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
	CHECK(process(&host, "2C0000005000020007000", ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0);
	CHECK(!st_rip3_copy_test_fail_malloc && row(host.ports, 0, 0,0, baseline, 8) == 0);
	CHECK(process(&host, "2C0000005000020007000", -1) == 0);
	CHECK(row(host.ports, 0, 0,0, copied, 8) == 0);
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
	CHECK(process(&host, "2CZ0z000&0000A0A0C0A0", -1) == 0);
	CHECK(row(host.ports, 0, 10,10, source, 3) == 0);
	CHECK(process(&host, "2CZ0#000&0000A0B0B0B0", -1) == 0);
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
		/* Independent zero-padded decimal 0/1 fields are identical MegaNums;
		 * no encoder from the implementation builds expected wire input. */
		(void)snprintf(command, sizeof(command), "2C0%0*u%0*u%0*u%0*u0%0*u%0*u%0*u%0*u000000",
		    (int)width,0u,(int)width,0u,(int)width,1u,(int)width,1u,
		    (int)width,2u,(int)width,2u,(int)width,3u,(int)width,3u);
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
large_coordinates(void)
{
	struct st_rip3_command_host host = {0};
	const struct st_rip3_rect source = {0,0,1,1}, destination = {10,20,11,21};
	const uint8_t expected[] = {9,9};

	CHECK(create(&host, ST_RIP3_CANVAS_640_480, 1,1) == 0);
	CHECK(st_rip3_ports_define(host.ports, 1, &source, true) == 0);
	CHECK(st_rip3_ports_define(host.ports, 2, &destination, false) == 0);
	CHECK(st_rip3_ports_switch(host.ports, 1) == 0 && st_rip3_ports_erase(host.ports, 9) == 0);
	CHECK(process(&host, "J1S", -1) == 0);
	CHECK(process(&host, "n5", -1) == 0);
	CHECK(process(&host, "2C1" "0000000000&&&&&&&&&&" "2" "0000000000&&&&&&&&&&" "0", -1) == 0);
	CHECK(row(host.ports, 2, 0,0, expected, 2) == 0 && row(host.ports, 2, 0,1, expected, 2) == 0);
	CHECK(process(&host, "2C1" "&&&&&&&&&&&&&&&&&&&&" "2" "0000000000&&&&&&&&&&" "0", -1) == 0);
	CHECK(row(host.ports, 2, 0,0, expected, 2) == 0 && row(host.ports, 2, 0,1, expected, 2) == 0);
	CHECK(process(&host, "2C1" "0000000000&&&&&&&&&&" "2" "&&&&&&&&&&&&&&&&&&&&" "0", -1) == 0);
	CHECK(row(host.ports, 2, 0,0, expected, 2) == 0 && row(host.ports, 2, 0,1, expected, 2) == 0);
	destroy(&host);
	return 0;
}

int
main(void)
{
	CHECK(scene(ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600, 800,600) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768, 1024,768) == 0);
	CHECK(canonical(ST_RIP3_CANVAS_640_480, 640,480, 40) == 0);
	CHECK(canonical(ST_RIP3_CANVAS_800_600, 800,600, 50) == 0);
	CHECK(canonical(ST_RIP3_CANVAS_1024_768, 1024,768, 64) == 0);
	CHECK(validation() == 0);
	CHECK(widths_and_radix() == 0);
	CHECK(large_coordinates() == 0);
	return 0;
}
