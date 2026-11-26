/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filesystem.h"
#include "image_area_fixture.h"
#include "path.h"
#include "st_pixels.h"
#include "st_rip3_bitmap_stamp.h"
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_number.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_rip3_resources.h"
#include "st_rip3_stream.h"

extern bool st_rip3_bitmap_stamp_test_fail_allocate;
extern bool st_rip3_wallpaper_test_fail_allocate;
extern bool st_rip3_clipboard_test_fail_allocate;
extern bool st_rip3_clipboard_test_fail_copy;
extern bool st_rip3_bitmap_file_test_fail_allocate;
extern bool st_rip3_bitmap_file_test_fail_decode;

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

struct fixture {
	struct st_rip3_command_host        host;
	struct st_rip3_resource_paths      paths;
	struct st_rip3_bitmap_error_sink   feedback;
	struct st_rip3_clipboard_selection selection;
	struct st_rip3_bitmap_destination  last_area;
	unsigned                           errors, prompts, last_port;
	enum st_rip3_command_diagnostic    kind;
	bool                               area_present, natural_size, overflow;
	char                               last_name[80];
};

static void
diagnostic(void *context, enum st_rip3_command_diagnostic kind)
{
	struct fixture *f = context;
	f->errors++;
	f->kind = kind;
}

static void
show_error(void *context, const char *name, unsigned port, const struct st_rip3_bitmap_destination *area,
    bool natural_size)
{
	struct fixture *f = context;
	f->prompts++;
	f->last_port    = port;
	f->area_present = area != NULL;
	f->natural_size = natural_size;
	if (area != NULL)
		f->last_area = *area;
	if (strlen(name) >= sizeof(f->last_name)) {
		f->overflow = true;
		return;
	}
	memcpy(f->last_name, name, strlen(name) + 1);
}

static void
command(void *context, const uint8_t *bytes, size_t length)
{
	struct fixture                   *f    = context;
	const struct st_rip3_command_sink sink = {f, diagnostic};
	st_rip3_commands_process(&f->host, &sink, bytes, length);
}

static int
process(struct fixture *f, const char *text, int error)
{
	unsigned previous = f->errors;
	command(f, (const uint8_t *)text, strlen(text));
	CHECK(f->errors == previous + (error < 0 ? 0u : 1u));
	if (error >= 0)
		CHECK((int)f->kind == error);
	CHECK(!f->overflow);
	return 0;
}

static void
form(char body[160], const char *coordinates, char duplicate, unsigned mode, const char *flags,
    const char *transparent, const char *name)
{
	(void)snprintf(body, 160, "1b%s%c%u%s%s0000%s", coordinates, duplicate, mode, flags, transparent, name);
}

static void
put16(uint8_t *p, unsigned value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
}

static void
put32(uint8_t *p, uint32_t value)
{
	put16(p, value & 65535);
	put16(p + 2, value >> 16);
}

static int
asset(const char *directory, const char *name, bool rgb, bool compressed)
{
	/* Independently authored 3x2 BMP: top 3/12/48, bottom 192/0/255. */
	uint8_t       bytes[1102] = {0};
	const uint8_t codes[]     = {3, 12, 48, 192, 0, 255};
	size_t        offset = rgb ? 54 : 1078, stride = rgb ? 12 : 4, size = offset + stride * 2;
	bytes[0] = 'B';
	bytes[1] = 'M';
	put32(bytes + 2, (uint32_t)size);
	put32(bytes + 10, (uint32_t)offset);
	put32(bytes + 14, 40);
	put32(bytes + 18, 3);
	put32(bytes + 22, 2);
	put16(bytes + 26, 1);
	put16(bytes + 28, rgb ? 24 : 8);
	put32(bytes + 30, compressed ? 1 : 0);
	if (!rgb)
		for (unsigned i = 0; i < 256; i++)
			bytes[54 + 4 * i] = bytes[55 + 4 * i] = bytes[56 + 4 * i] = (uint8_t)i;
	for (unsigned y = 0; y < 2; y++)
		for (unsigned x = 0; x < 3; x++) {
			uint8_t code = codes[(1 - y) * 3 + x];
			if (rgb)
				bytes[offset + y * stride + 3 * x] = bytes[offset + y * stride + 3 * x + 1] =
				    bytes[offset + y * stride + 3 * x + 2] = code;
			else
				bytes[offset + y * stride + x] = code;
		}
	char *path = NULL;
	CHECK(path_join_alloc(&path, directory, name) == 0 && fs_replace_file(path, bytes, size) == 0);
	free(path);
	return 0;
}

static uint32_t
adopted_color(unsigned index, unsigned depth)
{
	/* Deliberately duplicate distinct palette indexes, including both 1-bit
	 * entries. Drawing retains their indexes; key classification can collapse equal RGB. */
	if (depth == 1)
		return UINT32_C(0x4a9bc2);
	if (index == 2)
		index = 1;
	return ((index * 37u & 255u) << 16) | ((index * 73u & 255u) << 8) | (index * 109u & 255u);
}

static int
adoption_asset(const char *directory, unsigned depth)
{
	uint8_t       bytes[1100] = {0};
	const uint8_t codes[] = {1, 2, 3, 4, 0, 1}, mono[] = {1, 0, 1, 0, 1, 0};
	unsigned      count  = depth == 1 ? 2 : depth == 4 ? 5 : 256;
	size_t        offset = 54 + count * 4, size = offset + 8;
	char          name[20];
	char         *path = NULL;
	(void)snprintf(name, sizeof(name), "Adopt%u.BMP", depth);
	bytes[0] = 'B';
	bytes[1] = 'M';
	put32(bytes + 2, (uint32_t)size);
	put32(bytes + 10, (uint32_t)offset);
	put32(bytes + 14, 40);
	put32(bytes + 18, 3);
	put32(bytes + 22, 2);
	put16(bytes + 26, 1);
	put16(bytes + 28, depth);
	put32(bytes + 46, count);
	for (unsigned i = 0; i < count; i++) {
		uint32_t rgb      = adopted_color(i, depth);
		bytes[54 + i * 4] = (uint8_t)rgb;
		bytes[55 + i * 4] = (uint8_t)(rgb >> 8);
		bytes[56 + i * 4] = (uint8_t)(rgb >> 16);
	}
	for (unsigned y = 0; y < 2; y++)
		for (unsigned x = 0; x < 3; x++) {
			uint8_t code = depth == 1 ? mono[(1 - y) * 3 + x] : codes[(1 - y) * 3 + x];
			if (depth == 1)
				bytes[offset + y * 4] |= (uint8_t)(code << (7 - x));
			else if (depth == 4)
				bytes[offset + y * 4 + x / 2] |= (uint8_t)(code << (x % 2 == 0 ? 4 : 0));
			else
				bytes[offset + y * 4 + x] = code;
		}
	CHECK(path_join_alloc(&path, directory, name) == 0 && fs_replace_file(path, bytes, size) == 0);
	free(path);
	return 0;
}

static int
tall_asset(const char *directory)
{
	uint8_t bytes[1218] = {0};
	bytes[0]            = 'B';
	bytes[1]            = 'M';
	put32(bytes + 2, sizeof(bytes));
	put32(bytes + 10, 1078);
	put32(bytes + 14, 40);
	put32(bytes + 18, 3);
	put32(bytes + 22, 35);
	put16(bytes + 26, 1);
	put16(bytes + 28, 8);
	for (unsigned i = 0; i < 256; i++)
		bytes[54 + 4 * i] = bytes[55 + 4 * i] = bytes[56 + 4 * i] = (uint8_t)i;
	/* Three independently authored horizontal bands: 10,15,10 rows. */
	for (unsigned y = 0; y < 35; y++)
		for (unsigned x = 0; x < 3; x++)
			bytes[1078 + (34 - y) * 4 + x] = y < 10 ? 3 : y < 25 ? 12 : 48;
	char *path = NULL;
	CHECK(path_join_alloc(&path, directory, "Tall.BMP") == 0
	      && fs_replace_file(path, bytes, sizeof(bytes)) == 0);
	free(path);
	return 0;
}

static void
icon(char body[160], const char *coordinates, unsigned mode, unsigned clipboard, unsigned scale, const char *name)
{
	(void)snprintf(body, 160, "1I%s%02u%u%u0%s", coordinates, mode, clipboard, scale, name);
}

static int
create(struct fixture *f, enum st_rip3_canvas canvas, uint32_t width, uint32_t height, const char *directory)
{
	const struct st_rip3_rect bounds = {10, 20, 61, 40}, viewport = {2, 1, 49, 18};
	uint32_t                  colors[256];
	CHECK(st_rip3_environments_create(&f->host.environments, canvas) == 0);
	CHECK(
	    st_rip3_ports_create(&f->host.ports, canvas) == 0 && st_rip3_palettes_create(&f->host.palettes) == 0);
	CHECK(st_rip3_palettes_switch(f->host.palettes, 1) == 0);
	for (unsigned i = 0; i < 256; i++)
		colors[i] = i * UINT32_C(0x010101);
	CHECK(st_rip3_palettes_rgb(f->host.palettes, 0, 8, colors, 256) == 0
	      && st_rip3_palettes_protect(f->host.palettes, 1, true) == 0);
	CHECK(st_rip3_ports_define(f->host.ports, 1, &bounds, false) == 0
	      && st_rip3_ports_switch(f->host.ports, 1) == 0);
	CHECK(
	    st_rip3_ports_viewport(f->host.ports, &viewport) == 0 && st_rip3_ports_erase(f->host.ports, 85) == 0);
	const struct st_rip3_rect disabled = {0, 0, 0, 0};
	CHECK(rip3_test_image_area(f->host.ports, 1, &disabled, 15) == 0
	      && st_rip3_ports_protect(f->host.ports, 1, true) == 0);
	struct st_rip3_environment environment;
	CHECK(st_rip3_environments_get(f->host.environments, 0, &environment) == 0);
	environment.world_width  = width;
	environment.world_height = height;
	environment.port         = 9;
	environment.palette      = 7;
	CHECK(st_rip3_environments_set(f->host.environments, 0, &environment) == 0);
	f->paths.host         = directory;
	f->host.resources     = &f->paths;
	f->feedback           = (struct st_rip3_bitmap_error_sink) {f, show_error};
	f->host.bitmap_errors = &f->feedback;
	return 0;
}

static void
destroy(struct fixture *f)
{
	st_rip3_environments_destroy(f->host.environments);
	st_rip3_ports_destroy(f->host.ports);
	st_rip3_palettes_destroy(f->host.palettes);
}

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, const char *directory)
{
	struct fixture         f      = {0};
	struct st_rip3_stream *stream = NULL;
	char                   body[160];
	const char *const      positions[]          = {"00000000", "05000000", "0A000000", "0F000000", "0K000000"};
	const uint8_t          expected_modes[5][6] = {
	    {3,   12,  48,  192, 0,   255},
            {86,  89,  101, 149, 85,  170},
            {87,  93,  117, 213, 85,  255},
	    {1,   4,   16,  64,  0,   85 },
            {252, 243, 207, 63,  255, 0  }
        };
	const uint8_t stretched[] = {3, 3, 12, 12, 48, 48, 3, 3, 12, 12, 48, 48, 192, 192, 0, 0, 255, 255, 192,
	    192, 0, 0, 255, 255};
	CHECK(create(&f, canvas, width, height, directory) == 0);
	const struct st_rip3_stream_sink sink = {.context = &f, .command = command, .diagnostic = diagnostic};
	CHECK(st_rip3_stream_create(&stream, &sink, f.host.environments) == 0);
	const uint8_t opening[] = "!|";
	st_rip3_stream_process(stream, opening, 2);
	for (unsigned mode = 0; mode < 5; mode++) {
		form(body, positions[mode], 'Z', mode, "00", "ZZ", "tILE");
		for (size_t i = 0; i < strlen(body); i++)
			st_rip3_stream_process(stream, (const uint8_t *)body + i, 1);
		const uint8_t separator = '|';
		st_rip3_stream_process(stream, &separator, 1);
	}
	form(body, "00030607", '1', 0, "01", "00", "Tile.BMP"); /* Same explicit duplicate is ignored. */
	CHECK(process(&f, body, -1) == 0);
	form(body, "0A030000", '0', 0, "08", "0C", "Tile");
	CHECK(process(&f, body, -1) == 0);
	form(body, "0F030000", '0', 4, "0A", "0C", "Color.BMP");
	CHECK(process(&f, body, -1) == 0);
	form(body, "1A000000", '0', 0, "00", "00", "Tile");
	CHECK(process(&f, body, -1) == 0); /* x=46, edge clips last source column. */
	form(body, "0009010D", '0', 0, "00", "00", "Tile");
	CHECK(process(&f, body, -1) == 0);
	CHECK(f.errors == 0 && f.prompts == 0);
	struct st_rip3_palette  palette;
	struct st_pixel_buffer *capture = NULL;
	CHECK(st_rip3_palettes_get(f.host.palettes, 1, &palette) == 0
	      && st_rip3_ports_snapshot(f.host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++)
		for (uint32_t x = 0; x < width; x++) {
			uint8_t expected = 0;
			if (x >= 12 && x <= 59 && y >= 21 && y <= 38) {
				uint32_t vx = x - 12, vy = y - 21;
				expected = 85;
				for (unsigned mode = 0; mode < 5; mode++)
					if (vx >= mode * 5 && vx <= mode * 5 + 2 && vy < 2)
						expected = expected_modes[mode][vy * 3 + vx - mode * 5];
				if (vx < 6 && vy >= 3 && vy <= 6)
					expected = stretched[(vy - 3) * 6 + vx];
				if (vx >= 10 && vx <= 12 && vy >= 3 && vy <= 4) {
					expected = expected_modes[0][(vy - 3) * 3 + vx - 10];
					if (vx == 11 && vy == 3)
						expected = 85;
				}
				if (vx >= 15 && vx <= 17 && vy >= 3 && vy <= 4) {
					expected = expected_modes[4][(vy - 3) * 3 + vx - 15];
					if (vx == 16 && vy == 3)
						expected = 85;
				}
				if (vx == 0 && vy >= 9 && vy <= 12)
					expected = vy <= 10 ? 12 : 0;
				if (vx >= 46 && vy < 2)
					expected = expected_modes[0][vy * 3 + vx - 46];
			}
			CHECK(capture->primary[(size_t)y * width + x]
			      == (ST_PIXEL_COLOR_RGB_TAG | palette.rgb[expected]));
		}
	unsigned                   current;
	struct st_rip3_port_info   info;
	struct st_rip3_environment environment;
	CHECK(st_rip3_ports_current(f.host.ports, &current) == 0 && current == 1);
	CHECK(st_rip3_ports_info(f.host.ports, 1, &info) == 0 && info.protected && !info.image.enabled
	      && info.image.flags == 15);
	CHECK(st_rip3_palettes_current(f.host.palettes, &current) == 0 && current == 1);
	CHECK(st_rip3_environments_get(f.host.environments, 0, &environment) == 0 && environment.port == 9
	      && environment.palette == 7);
	st_pixel_buffer_destroy(capture);
	st_rip3_stream_destroy(stream);
	destroy(&f);
	return 0;
}

static int
adoption(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, unsigned depth, const char *directory)
{
	struct fixture         f = {0};
	struct st_rip3_palette before, actual;
	struct st_rip3_bitmap *capture = NULL;
	char                   name[20], body[160];
	const char *const      positions[] = {"00000000", "05000000", "0A000000", "0F000000", "0K000000"};
	const uint8_t          modes[5][6] = {
	    {1,   2,   3,   4,   0,   1  },
            {84,  87,  86,  81,  85,  84 },
            {85,  87,  87,  85,  85,  85 },
            {1,   0,   1,   4,   0,   1  },
	    {254, 253, 252, 251, 255, 254}
        };
	const uint8_t mono[5][6] = {
	    {1,   0,   1,   0,   1,   0  },
            {84,  85,  84,  85,  84,  85 },
            {85,  85,  85,  85,  85,  85 },
            {1,   0,   1,   0,   1,   0  },
	    {254, 255, 254, 255, 254, 255}
        };
	const uint8_t (*expected_modes)[6] = depth == 1 ? mono : modes;
	unsigned      count                = depth == 1 ? 2 : depth == 4 ? 5 : 256;
	CHECK(create(&f, canvas, width, height, directory) == 0);
	(void)snprintf(name, sizeof(name), "Adopt%u.BMP", depth);
	CHECK(st_rip3_palettes_get(f.host.palettes, 1, &before) == 0);
	form(body, positions[0], '0', 0, "02", "00", name);
	CHECK(process(&f, body, ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && f.prompts == 1);
	CHECK(st_rip3_palettes_get(f.host.palettes, 1, &actual) == 0
	      && memcmp(&before, &actual, sizeof(before)) == 0);
	CHECK(st_rip3_palettes_protect(f.host.palettes, 1, false) == 0);
	st_rip3_bitmap_stamp_test_fail_allocate = true;
	CHECK(
	    process(&f, body, ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_bitmap_stamp_test_fail_allocate);
	CHECK(st_rip3_palettes_get(f.host.palettes, 1, &actual) == 0
	      && memcmp(&before, &actual, sizeof(before)) == 0);
	uint8_t unchanged;
	CHECK(st_rip3_ports_read(f.host.ports, 1, 2, 1, &unchanged) == 0 && unchanged == 85);
	for (unsigned mode = 0; mode < 5; mode++) {
		form(body, positions[mode], '0', mode, "02", "00", name);
		CHECK(process(&f, body, -1) == 0);
	}
	form(body, "00030000", '0', 0, "0A", "01", name);
	CHECK(process(&f, body, -1) == 0);
	form(body, "0A030000", '0', 0, "06", "00", name);
	CHECK(process(&f, body, -1) == 0);
	form(body, "0F030000", '0', 4, "0E", "01", name);
	CHECK(process(&f, body, -1) == 0);
	CHECK(f.prompts == 2);
	CHECK(st_rip3_palettes_get(f.host.palettes, 1, &actual) == 0);
	for (unsigned i = 0; i < 256; i++)
		CHECK(actual.rgb[i] == (i < count ? adopted_color(i, depth) : before.rgb[i]));
	/* One owned capture holds both the index plane and its RGB palette,
	 * including background pixels recoloured by palette adoption. */
	CHECK(st_rip3_bitmap_capture(&capture, f.host.ports, 0, NULL, &actual) == 0);
	CHECK(capture->pixels->width == width && capture->pixels->height == height);
	for (uint32_t y = 0; y < height; y++)
		for (uint32_t x = 0; x < width; x++) {
			uint8_t expected = 0;
			if (x >= 12 && x <= 59 && y >= 21 && y <= 38) {
				uint32_t vx = x - 12, vy = y - 21;
				expected = 85;
				for (unsigned mode = 0; mode < 5; mode++)
					if (vx >= mode * 5 && vx <= mode * 5 + 2 && vy < 2)
						expected = expected_modes[mode][vy * 3 + vx - mode * 5];
				if (vx < 3 && vy >= 3 && vy <= 4) {
					expected = expected_modes[0][(vy - 3) * 3 + vx];
					if (expected == 1 || (depth != 1 && expected == 2))
						expected = 85;
				}
				if (vx >= 10 && vx <= 12 && vy >= 3 && vy <= 4)
					expected = expected_modes[0][(vy - 3) * 3 + vx - 10];
				if (vx >= 15 && vx <= 17 && vy >= 3 && vy <= 4) {
					uint32_t at = (vy - 3) * 3 + vx - 15;
					expected    = (expected_modes[0][at] == 1
					                  || (depth != 1 && expected_modes[0][at] == 2))
					                  ? 85
					                  : expected_modes[4][at];
				}
			}
			CHECK(capture->pixels->primary[(size_t)y * width + x] == expected);
			CHECK(capture->palette[expected]
			      == (expected < count ? adopted_color(expected, depth) : before.rgb[expected]));
		}
	unsigned                   number;
	struct st_rip3_port_info   info;
	struct st_rip3_environment environment;
	CHECK(st_rip3_ports_current(f.host.ports, &number) == 0 && number == 1);
	CHECK(st_rip3_ports_info(f.host.ports, 1, &info) == 0 && info.protected && !info.image.enabled
	      && info.image.flags == 15);
	CHECK(st_rip3_palettes_current(f.host.palettes, &number) == 0 && number == 1);
	CHECK(st_rip3_environments_get(f.host.environments, 0, &environment) == 0 && environment.palette == 7
	      && environment.port == 9);
	st_rip3_bitmap_destroy(capture);
	destroy(&f);
	return 0;
}

static int
dithering(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, const char *directory)
{
	struct fixture         f           = {0};
	uint32_t               colors[256] = {0};
	struct st_rip3_palette palette;
	struct st_rip3_bitmap *capture = NULL;
	char                   body[160];
	/* Literal black/white quantization result for the authored 3x2 BMP
	 * stretched to 8x8. This is an implementation-policy oracle, not a
	 * historical RIPtel pixel capture. */
	const char *const expected[] = {"10000000", "00000000", "00000010", "00000000", "01000111", "11100111",
	    "01000111", "11100111"};
	CHECK(create(&f, canvas, width, height, directory) == 0);
	colors[1] = 0xffffff;
	CHECK(st_rip3_palettes_protect(f.host.palettes, 1, false) == 0
	      && st_rip3_palettes_rgb(f.host.palettes, 0, 8, colors, 256) == 0
	      && st_rip3_palettes_protect(f.host.palettes, 1, true) == 0);
	form(body, "00000808", '0', 0, "04", "00", "Tile");
	CHECK(process(&f, body, -1) == 0);
	/* True-colour ignores adoption but still dithers; palette protection
	 * must not turn this into an attempted table edit. */
	form(body, "0A000I08", '0', 0, "06", "00", "Color.BMP");
	CHECK(process(&f, body, -1) == 0);
	form(body, "000A080I", '0', 0, "0C", "0C", "Tile");
	CHECK(process(&f, body, -1) == 0);
	form(body, "0A0A0I0I", '0', 4, "0E", "00", "Color.BMP");
	CHECK(process(&f, body, -1) == 0);
	CHECK(f.errors == 0 && f.prompts == 0 && st_rip3_palettes_get(f.host.palettes, 1, &palette) == 0);
	CHECK(memcmp(palette.rgb, colors, sizeof(colors)) == 0);
	CHECK(st_rip3_bitmap_capture(&capture, f.host.ports, 0, NULL, &palette) == 0);
	for (uint32_t y = 0; y < height; y++)
		for (uint32_t x = 0; x < width; x++) {
			uint8_t value = 0;
			if (x >= 12 && x <= 59 && y >= 21 && y <= 38) {
				uint32_t vx = x - 12, vy = y - 21;
				value = 85;
				if (vx < 8 && vy < 8)
					value = (uint8_t)(expected[vy][vx] - '0');
				if (vx >= 10 && vx <= 17 && vy < 8)
					value = (uint8_t)(expected[vy][vx - 10] - '0');
				if (vx < 8 && vy >= 10 && vy <= 17) {
					value = (uint8_t)(expected[vy - 10][vx] - '0');
				}
				if (vx >= 10 && vx <= 17 && vy >= 10 && vy <= 17)
					value = vx >= 13 && vx <= 14 && vy >= 14    ? 85
					        : expected[vy - 10][vx - 10] == '0' ? 255
					                                            : 254;
			}
			CHECK(capture->pixels->primary[(size_t)y * width + x] == value);
		}
	st_rip3_bitmap_destroy(capture);
	destroy(&f);
	return 0;
}

static int
duplicate_contents(struct fixture *f, unsigned number, uint32_t width, uint32_t height, const uint8_t *expected)
{
	struct st_rip3_port_info info;
	struct st_rip3_palette   palette;
	struct st_rip3_bitmap   *capture = NULL;
	CHECK(st_rip3_ports_info(f->host.ports, number, &info) == 0 && info.defined && info.offscreen
	      && info.viewport_active && info.bounds.x0 == 0 && info.bounds.y0 == 0
	      && info.bounds.x1 == (int32_t)width - 1 && info.bounds.y1 == (int32_t)height - 1
	      && memcmp(&info.bounds, &info.viewport, sizeof(info.bounds)) == 0 && info.image.automatic
	      && info.image.enabled && info.image.flags == 0);
	CHECK(st_rip3_palettes_get(f->host.palettes, 1, &palette) == 0);
	CHECK(st_rip3_bitmap_capture(&capture, f->host.ports, number, NULL, &palette) == 0);
	CHECK(capture->pixels->width == width && capture->pixels->height == height);
	for (size_t i = 0; i < (size_t)width * height; i++)
		CHECK(capture->pixels->primary[i] == expected[i]);
	st_rip3_bitmap_destroy(capture);
	return 0;
}

static int
duplicate_pixels(struct fixture *f, unsigned number, uint32_t width, uint32_t height, const uint8_t *expected)
{
	struct st_rip3_port_info info;
	CHECK(st_rip3_ports_info(f->host.ports, number, &info) == 0 && !info.protected);
	return duplicate_contents(f, number, width, height, expected);
}

static int
duplication(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, const char *directory)
{
	struct fixture f      = {0};
	uint64_t       budget = UINT64_C(1000000);
	char           body[160];
	const uint8_t  modes[5][6] = {
	    {3,   12,  48,  192, 0,   255},
            {86,  89,  101, 149, 85,  170},
            {87,  93,  117, 213, 85,  255},
	    {1,   4,   16,  64,  0,   85 },
            {252, 243, 207, 63,  255, 0  }
        };
	CHECK(create(&f, canvas, width, height, directory) == 0);
	struct st_rip3_clipboard_selection clipboard = {.set = true, .port = 9};
	f.host.clipboard                             = &clipboard;
	f.host.offscreen_pixel_budget                = &budget;
	for (unsigned mode = 0; mode < 5; mode++) {
		CHECK(st_rip3_ports_erase(f.host.ports, 85) == 0);
		if (mode == 1) {
			const struct st_rip3_rect small = {1, 0, 1, 1}, disabled = {0, 0, 0, 0};
			CHECK(st_rip3_ports_switch(f.host.ports, 2) == 0
			      && st_rip3_ports_viewport(f.host.ports, &small) == 0
			      && st_rip3_ports_activate(f.host.ports, false) == 0
			      && rip3_test_image_area(f.host.ports, 2, &disabled, 15) == 0
			      && st_rip3_ports_switch(f.host.ports, 1) == 0);
		}
		if (mode == 2) {
			const struct st_rip3_rect overlap = {12, 21, 16, 25};
			CHECK(st_rip3_ports_define(f.host.ports, 2, &overlap, false) == 0
			      && st_rip3_ports_plot(f.host.ports, 4, 4, 77, ST_RIP3_COPY) == 0);
		}
		form(body, "00000000", '2', mode, "01", "00", "Tile");
		CHECK(process(&f, body, -1) == 0);
		CHECK(duplicate_pixels(&f, 2, 3, 2, modes[mode]) == 0);
		for (int32_t y = 0; y < 2; y++)
			for (int32_t x = 0; x < 3; x++) {
				uint8_t actual;
				CHECK(st_rip3_ports_read(f.host.ports, 1, x + 2, y + 1, &actual) == 0
				      && actual == modes[mode][y * 3 + x]);
			}
		if (mode == 2) {
			uint8_t actual;
			CHECK(st_rip3_ports_read(f.host.ports, 1, 6, 5, &actual) == 0 && actual == 77);
		}
	}
	CHECK(st_rip3_ports_erase(f.host.ports, 85) == 0);
	form(body, "1A000000", '3', 4, "09", "0C", "Tile");
	CHECK(process(&f, body, -1) == 0);
	const uint8_t clipped[] = {252, 85, 85, 63, 255, 255};
	CHECK(duplicate_pixels(&f, 3, 3, 2, clipped) == 0);
	form(body, "1A0H0000", '3', 4, "09", "0C", "Tile");
	CHECK(process(&f, body, -1) == 0);
	const uint8_t bottom[] = {252, 85, 85, 252, 85, 85};
	CHECK(duplicate_pixels(&f, 3, 3, 2, bottom) == 0);
	form(body, "00030607", '3', 0, "09", "0C", "Tile");
	CHECK(process(&f, body, -1) == 0);
	const uint8_t stretched[] = {3, 3, 85, 85, 48, 48, 3, 3, 85, 85, 48, 48, 192, 192, 0, 0, 255, 255, 192,
	    192, 0, 0, 255, 255};
	CHECK(duplicate_pixels(&f, 3, 6, 4, stretched) == 0);
	uint32_t colors[256] = {0};
	colors[1]            = 0xffffff;
	CHECK(st_rip3_palettes_protect(f.host.palettes, 1, false) == 0
	      && st_rip3_palettes_rgb(f.host.palettes, 0, 8, colors, 256) == 0);
	form(body, "00000808", '4', 0, "05", "00", "Tile");
	CHECK(process(&f, body, -1) == 0);
	const char *const rows[] = {"10000000", "00000000", "00000010", "00000000", "01000111", "11100111",
	    "01000111", "11100111"};
	uint8_t           dithered[64];
	for (size_t y = 0; y < 8; y++)
		for (size_t x = 0; x < 8; x++)
			dithered[y * 8 + x] = (uint8_t)(rows[y][x] - '0');
	CHECK(duplicate_pixels(&f, 4, 8, 8, dithered) == 0);
	form(body, "00000000", '5', 0, "03", "00", "Adopt8.BMP");
	CHECK(process(&f, body, -1) == 0);
	const uint8_t adopted[] = {1, 2, 3, 4, 0, 1};
	CHECK(duplicate_pixels(&f, 5, 3, 2, adopted) == 0 && f.errors == 0 && f.prompts == 0);
	CHECK(clipboard.set && clipboard.port == 9);
	destroy(&f);
	return 0;
}

static int
duplicate_failures(const char *directory)
{
	struct fixture            f = {0};
	char                      body[160];
	uint64_t                  budget = UINT64_C(1000000);
	struct st_rip3_palette    before, after;
	const struct st_rip3_rect bounds     = {0, 0, 2, 1};
	const uint8_t             original[] = {42, 42, 42, 42, 42, 42}, clear[] = {0, 0, 0, 0, 0, 0};
	CHECK(create(&f, ST_RIP3_CANVAS_640_480, 640, 480, directory) == 0);
	f.host.clipboard              = &f.selection;
	f.host.offscreen_pixel_budget = &budget;
	f.selection                   = (struct st_rip3_clipboard_selection) {true, 2};
	CHECK(st_rip3_ports_define(f.host.ports, 2, &bounds, true) == 0
	      && st_rip3_ports_switch(f.host.ports, 2) == 0 && st_rip3_ports_erase(f.host.ports, 42) == 0
	      && st_rip3_ports_switch(f.host.ports, 1) == 0);
	CHECK(st_rip3_palettes_protect(f.host.palettes, 1, false) == 0
	      && st_rip3_palettes_get(f.host.palettes, 1, &before) == 0);
	form(body, "00000000", '2', 0, "0J", "00", "Adopt8.BMP");
	st_rip3_bitmap_file_test_fail_allocate = true;
	CHECK(process(&f, body, ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_bitmap_file_test_fail_allocate);
	st_rip3_bitmap_stamp_test_fail_allocate = true;
	CHECK(
	    process(&f, body, ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_bitmap_stamp_test_fail_allocate);
	CHECK(duplicate_pixels(&f, 2, 3, 2, original) == 0 && f.selection.set && f.selection.port == 2);
	CHECK(
	    st_rip3_palettes_get(f.host.palettes, 1, &after) == 0 && memcmp(&before, &after, sizeof(before)) == 0);
	uint8_t value;
	CHECK(st_rip3_ports_read(f.host.ports, 1, 2, 1, &value) == 0 && value == 85);
	/* Protected explicit rejection is after initial painting/adoption and
	 * before wallpaper; it retains target, pointer and current selection. */
	CHECK(st_rip3_ports_protect(f.host.ports, 2, true) == 0);
	CHECK(process(&f, body, ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && f.prompts == 2);
	CHECK(duplicate_contents(&f, 2, 3, 2, original) == 0 && f.selection.set && f.selection.port == 2);
	struct st_rip3_port_info protected_info;
	CHECK(st_rip3_ports_info(f.host.ports, 2, &protected_info) == 0 && protected_info.protected);
	CHECK(st_rip3_palettes_get(f.host.palettes, 1, &after) == 0 && after.rgb[1] == adopted_color(1, 8));
	CHECK(st_rip3_ports_read(f.host.ports, 1, 2, 1, &value) == 0 && value == 1);
	CHECK(st_rip3_ports_read(f.host.ports, 1, 12, 11, &value) == 0 && value == 85);
	CHECK(st_rip3_ports_protect(f.host.ports, 2, false) == 0);
	st_rip3_clipboard_test_fail_allocate = true;
	CHECK(process(&f, body, ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_clipboard_test_fail_allocate);
	struct st_rip3_port_info info;
	unsigned                 current;
	CHECK(st_rip3_ports_info(f.host.ports, 2, &info) == 0 && !info.defined && !f.selection.set);
	CHECK(st_rip3_ports_current(f.host.ports, &current) == 0 && current == 1);
	CHECK(st_rip3_ports_read(f.host.ports, 1, 12, 11, &value) == 0 && value == 2); /* wallpaper continued */
	st_rip3_clipboard_test_fail_copy = true;
	CHECK(process(&f, body, -1) == 0 && !st_rip3_clipboard_test_fail_copy
	      && duplicate_pixels(&f, 2, 3, 2, clear) == 0);
	form(body, "1E000000", '2', 0, "03", "00", "Adopt8.BMP");
	CHECK(process(&f, body, -1) == 0 && duplicate_pixels(&f, 2, 3, 2, clear) == 0 && f.prompts == 2);
	form(body, "00000000", '2', 0, "03", "00", "Adopt8.BMP");
	CHECK(process(&f, body, -1) == 0);
	const uint8_t copied[] = {1, 2, 3, 4, 0, 1};
	CHECK(duplicate_pixels(&f, 2, 3, 2, copied) == 0);
	CHECK(st_rip3_ports_protect(f.host.ports, 1, false) == 0
	      && st_rip3_ports_define(f.host.ports, 1, &bounds, true) == 0
	      && st_rip3_ports_erase(f.host.ports, 85) == 0 && st_rip3_ports_protect(f.host.ports, 1, true) == 0);
	form(body, "00000000", '3', 0, "01", "00", "Adopt8.BMP");
	CHECK(process(&f, body, -1) == 0);
	const uint8_t mapped[] = {1, 1, 3, 4, 0, 1};
	CHECK(duplicate_pixels(&f, 3, 3, 2, mapped) == 0);
	destroy(&f);
	return 0;
}

static int
icons(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, const char *directory)
{
	struct fixture    f = {0};
	char              body[160];
	const char *const positions[] = {"0000", "0500", "0A00", "0F00", "0K00"};
	const uint8_t     modes[5][6] = {
	    {3,   12,  48,  192, 0,   255},
            {86,  89,  101, 149, 85,  170},
            {87,  93,  117, 213, 85,  255},
	    {1,   4,   16,  64,  0,   85 },
            {252, 243, 207, 63,  255, 0  }
        };
	const uint8_t  scaled_640[]  = {3, 12, 48, 192, 0, 255};
	const uint8_t  scaled_800[]  = {3, 12, 48, 192, 0, 255, 192, 0, 255};
	const uint8_t  scaled_1024[] = {3, 12, 12, 48, 3, 12, 12, 48, 192, 0, 0, 255, 192, 0, 0, 255};
	const uint8_t *scaled        = width == 640 ? scaled_640 : width == 800 ? scaled_800 : scaled_1024;
	unsigned scaled_width = width == 1024 ? 4 : 3, scaled_height = width == 640 ? 2 : width == 800 ? 3 : 4;
	unsigned tall_height             = width == 640 ? 48 : width == 800 ? 60 : 76;
	unsigned band                    = width == 640 ? 14 : width == 800 ? 17 : 22;
	const struct st_rip3_rect bounds = {10, 20, 89, 139}, viewport = {2, 1, 65, 100}, disabled = {0, 0, 0, 0};
	CHECK(create(&f, canvas, width, height, directory) == 0);
	CHECK(st_rip3_ports_protect(f.host.ports, 1, false) == 0
	      && st_rip3_ports_define(f.host.ports, 1, &bounds, false) == 0
	      && st_rip3_ports_viewport(f.host.ports, &viewport) == 0 && st_rip3_ports_erase(f.host.ports, 85) == 0
	      && rip3_test_image_area(f.host.ports, 1, &disabled, 15) == 0
	      && st_rip3_ports_protect(f.host.ports, 1, true) == 0);
	f.selection                             = (struct st_rip3_clipboard_selection) {true, 9};
	f.host.clipboard                        = &f.selection;
	struct st_rip3_stream           *stream = NULL;
	const struct st_rip3_stream_sink sink   = {.context = &f, .command = command, .diagnostic = diagnostic};
	CHECK(st_rip3_stream_create(&stream, &sink, f.host.environments) == 0);
	st_rip3_stream_process(stream, (const uint8_t *)"!|", 2);
	for (unsigned mode = 0; mode < 5; mode++) {
		icon(body, positions[mode], mode, 0, 0, "tiLE");
		for (size_t i = 0; i < strlen(body); i++)
			st_rip3_stream_process(stream, (const uint8_t *)body + i, 1);
		st_rip3_stream_process(stream, (const uint8_t *)"|", 1);
	}
	icon(body, "0004", 0, 0, 1, "Tile.BMP");
	CHECK(process(&f, body, -1) == 0);
	icon(body, "0504", 0, 0, 1, "Color.BMP");
	CHECK(process(&f, body, -1) == 0);
	CHECK(process(&f, "1I001101010button", -1) == 0);
	struct st_rip3_environment environment;
	CHECK(st_rip3_environments_get(f.host.environments, 0, &environment) == 0);
	environment.world_width  = width / 2;
	environment.world_height = height / 2;
	CHECK(st_rip3_environments_define(f.host.environments, 2, &environment) == 0
	      && st_rip3_environments_select(f.host.environments, 2) == 0
	      && st_rip3_environments_protect(f.host.environments, 2, true) == 0);
	icon(body, "050A", 0, 0, 1, "Tall");
	CHECK(process(&f, body, -1) == 0);
	icon(body, "0A0A", 0, 0, 0, "Tall");
	CHECK(process(&f, body, -1) == 0);
	/* Verbatim clips the last source column at the viewport edge. */
	icon(body, "0W1E", 4, 0, 0, "Tile");
	CHECK(process(&f, body, -1) == 0); /* device origin 64,100: outside. */
	icon(body, "0V1D", 4, 0, 0, "Tile");
	CHECK(process(&f, body, -1) == 0); /* device origin 62,98: 2x2 visible. */
	CHECK(st_rip3_environments_select(f.host.environments, 0) == 0);
	icon(body, "1R2R", 4, 0, 0, "Tile");
	CHECK(process(&f, body, -1) == 0); /* One visible corner pixel. */
	CHECK(st_rip3_environments_select(f.host.environments, 2) == 0);
	CHECK(f.errors == 0 && f.prompts == 0 && f.selection.set && f.selection.port == 9);
	struct st_rip3_palette palette;
	struct st_rip3_bitmap *capture = NULL;
	CHECK(st_rip3_palettes_get(f.host.palettes, 1, &palette) == 0
	      && st_rip3_bitmap_capture(&capture, f.host.ports, 0, NULL, &palette) == 0);
	for (uint32_t y = 0; y < height; y++)
		for (uint32_t x = 0; x < width; x++) {
			uint8_t value = 0;
			if (x >= 12 && x <= 75 && y >= 21 && y <= 120) {
				uint32_t vx = x - 12, vy = y - 21;
				value = 85;
				for (unsigned mode = 0; mode < 5; mode++)
					if (vx >= mode * 5 && vx <= mode * 5 + 2 && vy < 2)
						value = modes[mode][vy * 3 + vx - mode * 5];
				if (vx < scaled_width && vy >= 4 && vy < 4 + scaled_height)
					value = scaled[(vy - 4) * scaled_width + vx];
				if (vx >= 5 && vx < 5 + scaled_width && vy >= 4 && vy < 4 + scaled_height)
					value = scaled[(vy - 4) * scaled_width + vx - 5];
				if (vx < scaled_width && vy >= 37 && vy < 37 + scaled_height)
					value = scaled[(vy - 37) * scaled_width + vx] ^ 85;
				if (vx >= 10 && vx < 10 + scaled_width && vy >= 20 && vy < 20 + tall_height)
					value = vy < 20 + band ? 3 : vy < 20 + tall_height - band ? 12 : 48;
				if (vx >= 20 && vx <= 22 && vy >= 20 && vy < 55)
					value = vy < 30 ? 3 : vy < 45 ? 12 : 48;
				if (vx >= 62 && vy >= 98)
					value = modes[4][(vy - 98) * 3 + vx - 62];
				if (vx == 63 && vy == 99)
					value = 252;
			}
			CHECK(capture->pixels->primary[(size_t)y * width + x] == value);
		}
	unsigned                 number;
	struct st_rip3_port_info info;
	CHECK(st_rip3_ports_current(f.host.ports, &number) == 0 && number == 1
	      && st_rip3_ports_info(f.host.ports, 1, &info) == 0 && info.protected && !info.image.enabled
	      && info.image.flags == 15);
	CHECK(st_rip3_environments_current(f.host.environments, &number) == 0 && number == 2);
	st_rip3_bitmap_destroy(capture);
	st_rip3_stream_destroy(stream);
	destroy(&f);
	return 0;
}

static int
icon_failures(const char *directory)
{
	struct fixture f = {0};
	char           body[160];
	CHECK(create(&f, ST_RIP3_CANVAS_640_480, 640, 480, directory) == 0);
	f.selection      = (struct st_rip3_clipboard_selection) {true, 9};
	f.host.clipboard = &f.selection;
	icon(body, "0403", 0, 0, 0, "Missing");
	CHECK(process(&f, body, ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(f.prompts == 1 && f.selection.set && f.selection.port == 9 && f.area_present && f.natural_size
	      && f.last_area.x0 == 4 && f.last_area.y0 == 3 && f.last_port == 1
	      && strcmp(f.last_name, "Missing") == 0);
	icon(body, "0000", 0, 0, 1, "Compressed.BMP");
	CHECK(process(&f, body, ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(f.prompts == 2);
	f.selection = (struct st_rip3_clipboard_selection) {true, 9};
	icon(body, "0000", 0, 0, 0, "../Tile");
	CHECK(process(&f, body, ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	icon(body, "0000", 0, 0, 0, "\xff");
	CHECK(process(&f, body, ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(f.prompts == 2 && f.selection.set);
	const char *const invalid[] = {"1I", "1I0000", "1I000000000"};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
		CHECK(process(&f, invalid[i], ST_RIP3_COMMAND_INVALID) == 0);
	icon(body, "0000", 5, 0, 0, "Tile");
	CHECK(process(&f, body, ST_RIP3_COMMAND_INVALID) == 0);
	icon(body, "0000", 0, 2, 0, "Tile");
	CHECK(process(&f, body, ST_RIP3_COMMAND_INVALID) == 0);
	icon(body, "0000", 0, 0, 2, "Tile");
	CHECK(process(&f, body, ST_RIP3_COMMAND_INVALID) == 0);
	icon(body, "0000", 0, 0, 0, "Tile");
	body[10] = '?';
	CHECK(process(&f, body, ST_RIP3_COMMAND_INVALID) == 0);
	icon(body, "0000", 0, 1, 0, "Tile");
	CHECK(process(&f, body, ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	icon(body, "0403", 0, 0, 0, "Tile.BMP");
	st_rip3_bitmap_file_test_fail_allocate = true;
	CHECK(process(&f, body, ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_bitmap_file_test_fail_allocate);
	st_rip3_bitmap_file_test_fail_decode = true;
	CHECK(process(&f, body, ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_bitmap_file_test_fail_decode);
	st_rip3_bitmap_stamp_test_fail_allocate = true;
	CHECK(
	    process(&f, body, ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_bitmap_stamp_test_fail_allocate);
	CHECK(f.prompts == 5 && f.selection.set && f.selection.port == 9);
	CHECK(f.area_present && f.natural_size && f.last_area.x0 == 4 && f.last_area.y0 == 3);
	uint8_t actual;
	CHECK(st_rip3_ports_read(f.host.ports, 1, 2, 1, &actual) == 0 && actual == 85);
	const struct st_rip3_bitmap_error_sink *feedback = f.host.bitmap_errors;
	f.host.bitmap_errors                             = NULL;
	st_rip3_bitmap_file_test_fail_allocate           = true;
	CHECK(process(&f, body, ST_RIP3_COMMAND_UNSUPPORTED) == 0 && st_rip3_bitmap_file_test_fail_allocate);
	st_rip3_bitmap_file_test_fail_allocate = false;
	f.host.bitmap_errors                   = feedback;
	f.feedback.show                        = NULL;
	CHECK(process(&f, body, ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	f.feedback.show                         = show_error;
	struct st_rip3_stream           *stream = NULL;
	const struct st_rip3_stream_sink sink   = {.context = &f, .command = command, .diagnostic = diagnostic};
	CHECK(st_rip3_stream_create(&stream, &sink, f.host.environments) == 0);
	const char recovery[] = "!|1I|1I.......Missing|1I..Z...9Tile|";
	unsigned   previous   = f.errors;
	for (size_t i = 0; i < sizeof(recovery) - 1; i++)
		st_rip3_stream_process(stream, (const uint8_t *)recovery + i, 1);
	CHECK(f.errors == previous + 2 && f.prompts == 6 && f.selection.set && f.selection.port == 9);
	CHECK(st_rip3_ports_read(f.host.ports, 1, 2, 1, &actual) == 0 && actual == 3);
	st_rip3_stream_destroy(stream);
	CHECK(st_rip3_ports_protect(f.host.ports, 1, false) == 0
	      && st_rip3_ports_activate(f.host.ports, false) == 0);
	struct st_rip3_palettes *palettes = f.host.palettes;
	f.host.palettes                   = NULL;
	f.host.resources                  = NULL;
	f.host.bitmap_errors              = NULL;
	icon(body, "0000", 0, 1, 1, "Missing");
	CHECK(process(&f, body, -1) == 0 && f.prompts == 6);
	f.host.palettes = palettes;
	destroy(&f);
	return 0;
}

static int
numeric(const char *directory)
{
	for (unsigned radix = 36; radix <= 64; radix += 28)
		for (size_t width = 2; width <= 5; width++) {
			struct fixture             f = {0};
			struct st_rip3_environment environment;
			char                       coordinates[21] = {0}, body[160];
			uint8_t                    actual;
			CHECK(create(&f, ST_RIP3_CANVAS_640_480, 640, 480, directory) == 0);
			CHECK(st_rip3_environments_get(f.host.environments, 0, &environment) == 0);
			environment.base_math       = radix;
			environment.coordinate_size = (unsigned)width;
			environment.world_width     = 320;
			environment.world_height    = 240;
			CHECK(st_rip3_environments_define(f.host.environments, 2, &environment) == 0
			      && st_rip3_environments_select(f.host.environments, 2) == 0
			      && st_rip3_environments_protect(f.host.environments, 2, true) == 0);
			const uint64_t native[] = {2, 3, 0, 0}, scaled[] = {10, 3, 12, 4};
			const uint8_t  expected[] = {3, 12, 12, 48, 85, 192, 0, 0, 255, 85, 85, 85, 85, 85, 85};
			for (size_t i = 0; i < 4; i++)
				CHECK(
				    st_rip3_number_encode(coordinates + i * width, width, native[i], width, radix)
				    == 0);
			/* Unused duplicate/transparency fields are ignored even above 35/255. */
			form(body, coordinates, radix == 64 ? '&' : 'Z', 0, "00", radix == 64 ? "&&" : "ZZ",
			    "Tile");
			CHECK(process(&f, body, -1) == 0);
			for (int32_t y = 0; y < 3; y++)
				for (int32_t x = 0; x < 4; x++) {
					const uint8_t native_pixels[] = {3, 12, 48, 192, 0, 255};
					CHECK(st_rip3_ports_read(f.host.ports, 1, 6 + x, 7 + y, &actual) == 0);
					CHECK(actual == (x < 3 && y < 2 ? native_pixels[y * 3 + x] : 85));
				}
			for (size_t i = 0; i < 4; i++)
				CHECK(
				    st_rip3_number_encode(coordinates + i * width, width, scaled[i], width, radix)
				    == 0);
			form(body, coordinates, '0', 0, "00", "00", "Tile");
			CHECK(process(&f, body, -1) == 0);
			for (int32_t y = 0; y < 3; y++)
				for (int32_t x = 0; x < 5; x++) {
					CHECK(st_rip3_ports_read(f.host.ports, 1, 22 + x, 7 + y, &actual) == 0
					      && actual == expected[y * 5 + x]);
				}
			if (radix == 64) {
				form(body, coordinates, '&', 0, "01", "00", "Tile");
				CHECK(process(&f, body, -1) == 0); /* Duplicate >35 is ignored. */
				form(body, coordinates, '&', 0, "0H", "00", "Tile");
				CHECK(process(&f, body, -1) == 0);
				CHECK(st_rip3_ports_read(f.host.ports, 1, 2, 1, &actual) == 0 && actual == 3);
			}
			char icon_coordinates[11] = {0};
			CHECK(st_rip3_number_encode(icon_coordinates, width, 2, width, radix) == 0
			      && st_rip3_number_encode(icon_coordinates + width, width, 6, width, radix) == 0);
			icon(body, icon_coordinates, 0, 0, 0, "Tile");
			CHECK(process(&f, body, -1) == 0);
			const uint8_t icon_pixels[] = {3, 12, 48, 192, 0, 255};
			for (int32_t y = 0; y < 2; y++)
				for (int32_t x = 0; x < 3; x++)
					CHECK(st_rip3_ports_read(f.host.ports, 1, 6 + x, 13 + y, &actual) == 0
					      && actual == icon_pixels[y * 3 + x]);
			struct st_rip3_environment_info info;
			CHECK(st_rip3_environments_info(f.host.environments, 2, &info) == 0 && info.protected);
			destroy(&f);
		}
	struct fixture             f = {0};
	struct st_rip3_environment environment;
	char                       body[160];
	CHECK(create(&f, ST_RIP3_CANVAS_640_480, 640, 480, directory) == 0);
	CHECK(st_rip3_environments_get(f.host.environments, 0, &environment) == 0);
	environment.base_math       = 64;
	environment.coordinate_size = 5;
	environment.world_width     = 1;
	environment.world_height    = 1;
	CHECK(st_rip3_environments_set(f.host.environments, 0, &environment) == 0);
	form(body, "0000100001&&&&&&&&&&", '0', 0, "00", "00", "Missing");
	CHECK(process(&f, body, ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && f.prompts == 1);
	CHECK(f.last_area.x0 == 640 && f.last_area.y0 == 480 && f.last_area.x1 == INT64_C(687194766719)
	      && f.last_area.y1 == INT64_C(515396075039));
	form(body, "0000000000&&&&&&&&&&", '0', 0, "00", "00", "Tile");
	CHECK(process(&f, body, -1) == 0);
	for (int32_t y = 1; y <= 18; y++)
		for (int32_t x = 2; x <= 49; x++) {
			uint8_t actual;
			CHECK(st_rip3_ports_read(f.host.ports, 1, x, y, &actual) == 0 && actual == 3);
		}
	destroy(&f);
	return 0;
}

static int
errors(const char *directory)
{
	struct fixture f = {0};
	char           body[160];
	CHECK(create(&f, ST_RIP3_CANVAS_640_480, 640, 480, directory) == 0);
	form(body, "01020304", '0', 0, "00", "00", "Missing.BMP");
	CHECK(process(&f, body, ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && f.prompts == 1 && f.area_present);
	CHECK(strcmp(f.last_name, "Missing.BMP") == 0 && f.last_port == 1 && f.last_area.x0 == 1
	      && f.last_area.y0 == 2 && f.last_area.x1 == 2 && f.last_area.y1 == 3);
	form(body, "01020000", '0', 0, "00", "00", "Missing");
	CHECK(process(&f, body, ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && f.prompts == 2 && f.area_present
	      && f.natural_size && f.last_area.x0 == 1 && f.last_area.y0 == 2);
	form(body, "00000000", '0', 0, "00", "00", "Compressed");
	CHECK(process(&f, body, ST_RIP3_COMMAND_UNSUPPORTED) == 0 && f.prompts == 3);
	const char *const unsupported[] = {"1S", "ZZ"};
	for (size_t i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); i++) {
		form(body, "00000000", '0', 0, unsupported[i], "00", "Tile");
		CHECK(process(&f, body, ST_RIP3_COMMAND_INVALID) == 0 && f.prompts == 3);
	}
	form(body, "00000000", '0', 0, "00", "00", "../Tile");
	CHECK(process(&f, body, ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && f.prompts == 3);
	const char *const invalid[] = {"1b", "1b0000", "1b000000000000000000"};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
		CHECK(process(&f, invalid[i], ST_RIP3_COMMAND_INVALID) == 0);
	form(body, "00000000", '0', 5, "00", "00", "Tile");
	CHECK(process(&f, body, ST_RIP3_COMMAND_INVALID) == 0);
	form(body, "00000000", '0', 0, "00", "00", "");
	CHECK(process(&f, body, ST_RIP3_COMMAND_INVALID) == 0);
	form(body, "00000000", '0', 0, "00", "00", "Tile");
	body[19] = '?';
	CHECK(process(&f, body, ST_RIP3_COMMAND_INVALID) == 0);
	form(body, "02000101", '0', 0, "00", "00", "Tile");
	CHECK(process(&f, body, -1) == 0);
	form(body, "00000000", '0', 0, "08", "ZZ", "Tile");
	CHECK(process(&f, body, ST_RIP3_COMMAND_INVALID) == 0);
	form(body, "00000000", '0', 0, "00", "00", "Tile");
	st_rip3_bitmap_file_test_fail_allocate = true;
	CHECK(process(&f, body, ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_bitmap_file_test_fail_allocate);
	st_rip3_bitmap_file_test_fail_decode = true;
	CHECK(process(&f, body, ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_bitmap_file_test_fail_decode);
	st_rip3_bitmap_stamp_test_fail_allocate = true;
	CHECK(
	    process(&f, body, ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_bitmap_stamp_test_fail_allocate);
	CHECK(f.prompts == 6);
	uint8_t actual;
	CHECK(st_rip3_ports_read(f.host.ports, 1, 2, 1, &actual) == 0 && actual == 85);
	/* The real framer must deliver a valid load after both syntax and file errors. */
	struct st_rip3_stream           *stream = NULL;
	const struct st_rip3_stream_sink sink   = {.context = &f, .command = command, .diagnostic = diagnostic};
	CHECK(st_rip3_stream_create(&stream, &sink, f.host.environments) == 0);
	const char recovery[] = "!|1b|1b.........Missing|1b........000ZTile|";
	unsigned   previous   = f.errors;
	for (size_t i = 0; i < sizeof(recovery) - 1; i++)
		st_rip3_stream_process(stream, (const uint8_t *)recovery + i, 1);
	CHECK(f.errors == previous + 2 && f.prompts == 7);
	CHECK(st_rip3_ports_read(f.host.ports, 1, 2, 1, &actual) == 0 && actual == 3);
	st_rip3_stream_destroy(stream);
	const struct st_rip3_bitmap_error_sink *feedback = f.host.bitmap_errors;
	f.host.bitmap_errors                             = NULL;
	CHECK(process(&f, body, ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	f.host.bitmap_errors = feedback;
	CHECK(st_rip3_ports_protect(f.host.ports, 1, false) == 0
	      && st_rip3_ports_activate(f.host.ports, false) == 0);
	struct st_rip3_palettes *palettes = f.host.palettes;
	f.host.resources                  = NULL;
	f.host.palettes                   = NULL;
	f.host.bitmap_errors              = NULL;
	CHECK(process(&f, body, -1) == 0 && f.prompts == 7);
	form(body, "00000000", '0', 0, "0F", "00", "Missing");
	CHECK(process(&f, body, -1) == 0 && f.prompts == 7);
	/* Destruction follows after ignored input, with no outstanding callback ownership. */
	f.host.palettes = palettes;
	destroy(&f);
	return 0;
}

static int
duplicate_routes(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, const char *directory)
{
	struct fixture           f = {0};
	char                     body[160];
	uint64_t                 budget = UINT64_C(1000000);
	struct st_rip3_port_info info;
	unsigned                 current;
	uint8_t                  value;
	CHECK(create(&f, canvas, width, height, directory) == 0);
	f.host.clipboard              = &f.selection;
	f.host.offscreen_pixel_budget = &budget;
	form(body, "00000000", '0', 0, "0H", "00", "Tile"); /* capture, then wallpaper */
	CHECK(process(&f, body, -1) == 0 && f.selection.set && f.selection.port == 2);
	const uint8_t stretched[] = {3, 12, 12, 48, 192, 0, 0, 255, 192, 0, 0, 255};
	CHECK(duplicate_pixels(&f, 2, 4, 3, stretched) == 0);
	CHECK(st_rip3_ports_read(f.host.ports, 1, 6, 4, &value) == 0 && value == 0);
	CHECK(st_rip3_ports_current(f.host.ports, &current) == 0 && current == 1);
	CHECK(st_rip3_ports_erase(f.host.ports, 85) == 0 && st_rip3_ports_protect(f.host.ports, 2, true) == 0);
	CHECK(
	    process(&f, body, ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && f.selection.set && f.selection.port == 2);
	CHECK(duplicate_contents(&f, 2, 4, 3, stretched) == 0);
	CHECK(st_rip3_ports_read(f.host.ports, 1, 6, 4, &value) == 0
	      && value == 0); /* clipboard rejection still wallpapers */
	CHECK(st_rip3_ports_protect(f.host.ports, 2, false) == 0);
	/* Same-current explicit skips only duplication, even without bindings. */
	f.host.clipboard              = NULL;
	f.host.offscreen_pixel_budget = NULL;
	CHECK(st_rip3_ports_erase(f.host.ports, 85) == 0);
	form(body, "00000000", '1', 0, "1D", "00", "Tile");
	CHECK(process(&f, body, -1) == 0);
	CHECK(st_rip3_ports_read(f.host.ports, 1, 12, 11, &value) == 0 && value == 48);
	CHECK(duplicate_pixels(&f, 2, 4, 3, stretched) == 0);
	/* 32 alone accepts but does not wallpaper or access duplicate bindings. */
	CHECK(st_rip3_ports_erase(f.host.ports, 85) == 0);
	form(body, "00000000", '0', 0, "0W", "00", "Tile");
	CHECK(process(&f, body, -1) == 0);
	CHECK(st_rip3_ports_read(f.host.ports, 1, 12, 11, &value) == 0 && value == 85);
	/* Deleting an undefined explicit target does not clear a dangling pointer. */
	f.host.clipboard              = &f.selection;
	f.host.offscreen_pixel_budget = &budget;
	/* The documented outside 4x2 image still replaces explicit targets with
	 * 4x2 clear pixels, and the clipboard route allocates 5x3 clear pixels. */
	const uint8_t clear_explicit[8] = {0}, clear_clipboard[15] = {0};
	form(body, "1E001I02", '4', 0, "01", "00", "Tile");
	CHECK(process(&f, body, -1) == 0);
	CHECK(duplicate_pixels(&f, 4, 4, 2, clear_explicit) == 0);
	f.selection = (struct st_rip3_clipboard_selection) {true, 2};
	form(body, "1E001I02", '0', 0, "01", "00", "Tile");
	CHECK(process(&f, body, -1) == 0);
	CHECK(duplicate_pixels(&f, 2, 5, 3, clear_clipboard) == 0 && f.selection.set && f.selection.port == 2);
	form(body, "0A030A05", '0', 0, "0H", "00", "Tile");
	CHECK(process(&f, body, -1) == 0 && duplicate_pixels(&f, 2, 1, 3, clear_clipboard) == 0);
	struct st_rip3_port_copy_pair pair;
	CHECK(st_rip3_ports_copy_pair(f.host.ports, 2, &pair) == 0 && !pair.set);
	f.selection = (struct st_rip3_clipboard_selection) {true, 3};
	form(body, "00000000", '3', 0, "01", "00", "Tile");
	CHECK(process(&f, body, -1) == 0);
	const uint8_t tile[] = {3, 12, 48, 192, 0, 255};
	CHECK(duplicate_pixels(&f, 3, 3, 2, tile) == 0 && f.selection.set && f.selection.port == 3);
	/* A failed wallpaper snapshot retains both the initial draw and duplicate. */
	CHECK(st_rip3_ports_erase(f.host.ports, 85) == 0);
	st_rip3_wallpaper_test_fail_allocate = true;
	form(body, "00000000", '3', 0, "0H", "00", "Tile");
	CHECK(process(&f, body, ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_wallpaper_test_fail_allocate);
	CHECK(duplicate_pixels(&f, 3, 3, 2, tile) == 0 && !f.selection.set);
	CHECK(st_rip3_ports_read(f.host.ports, 1, 2, 1, &value) == 0 && value == 3);
	CHECK(st_rip3_ports_read(f.host.ports, 1, 12, 11, &value) == 0 && value == 85);
	/* A post-deletion budget failure has the same wallpaper continuation. */
	f.selection = (struct st_rip3_clipboard_selection) {true, 3};
	budget      = 0;
	CHECK(process(&f, body, ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0);
	CHECK(st_rip3_ports_info(f.host.ports, 3, &info) == 0 && !info.defined && !f.selection.set);
	CHECK(st_rip3_ports_read(f.host.ports, 1, 12, 11, &value) == 0 && value == 12);
	CHECK(f.prompts == 0);
	/* A protected screen target shares the initial draw, but retains its
	 * definition, selection and pointer and prevents later wallpaper. */
	const struct st_rip3_rect screen_target = {12, 21, 14, 22};
	CHECK(st_rip3_ports_erase(f.host.ports, 85) == 0
	      && st_rip3_ports_define(f.host.ports, 3, &screen_target, false) == 0);
	CHECK(st_rip3_ports_protect(f.host.ports, 3, true) == 0);
	f.selection = (struct st_rip3_clipboard_selection) {true, 3};
	CHECK(
	    process(&f, body, ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && f.selection.set && f.selection.port == 3);
	CHECK(st_rip3_ports_info(f.host.ports, 3, &info) == 0 && info.protected && !info.offscreen
	      && memcmp(&info.bounds, &screen_target, sizeof(screen_target)) == 0);
	CHECK(st_rip3_ports_read(f.host.ports, 3, 0, 0, &value) == 0 && value == 3);
	CHECK(st_rip3_ports_read(f.host.ports, 1, 12, 11, &value) == 0 && value == 85);
	CHECK(st_rip3_ports_current(f.host.ports, &current) == 0 && current == 1);
	destroy(&f);
	return 0;
}

static int
clipboard_retarget(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, const char *directory)
{
	struct fixture            f = {0};
	char                      body[160];
	uint64_t                  budget      = UINT64_C(1000000);
	const struct st_rip3_rect screen_view = {30, 40, 35, 44}, offscreen = {0, 0, 5, 4};
	CHECK(create(&f, canvas, width, height, directory) == 0);
	f.host.clipboard              = &f.selection;
	f.host.offscreen_pixel_budget = &budget;
	CHECK(
	    st_rip3_ports_switch(f.host.ports, 0) == 0 && st_rip3_ports_viewport(f.host.ports, &screen_view) == 0);
	for (int32_t y = 0; y < 2; y++)
		for (int32_t x = 0; x < 3; x++)
			CHECK(st_rip3_ports_plot(f.host.ports, x, y, (uint8_t)(7 + y * 3 + x), ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_protect(f.host.ports, 1, false) == 0
	      && st_rip3_ports_define(f.host.ports, 1, &offscreen, true) == 0
	      && st_rip3_ports_switch(f.host.ports, 1) == 0 && st_rip3_ports_erase(f.host.ports, 85) == 0);
	f.selection = (struct st_rip3_clipboard_selection) {true, 1};
	/* Initial Tile is painted to offscreen 1, which capture then deletes.
	 * The new clipboard reads port zero; wallpaper must also use zero. */
	form(body, "00000000", '0', 0, "0H", "00", "Tile");
	CHECK(process(&f, body, -1) == 0);
	const uint8_t captured[] = {7, 8, 8, 9, 10, 11, 11, 12, 10, 11, 11, 12};
	CHECK(duplicate_pixels(&f, 1, 4, 3, captured) == 0 && f.selection.set && f.selection.port == 1);
	unsigned current;
	CHECK(st_rip3_ports_current(f.host.ports, &current) == 0 && current == 0);
	struct st_rip3_palette    palette;
	struct st_rip3_bitmap    *capture = NULL;
	const struct st_rip3_rect full    = {0, 0, (int32_t)width - 1, (int32_t)height - 1};
	CHECK(st_rip3_ports_define(f.host.ports, 35, &full, false) == 0);
	CHECK(st_rip3_palettes_get(f.host.palettes, 1, &palette) == 0
	      && st_rip3_bitmap_capture(&capture, f.host.ports, 35, NULL, &palette) == 0);
	CHECK(capture->pixels->width == width && capture->pixels->height == height);
	for (uint32_t y = 0; y < height; y++)
		for (uint32_t x = 0; x < width; x++) {
			unsigned expected = 0;
			if (x >= 12 && x <= 59 && y >= 21 && y <= 38)
				expected = 85; /* original fixture */
			if (x >= 30 && x <= 35 && y >= 40 && y <= 44)
				expected = 7 + 3 * ((y - 40) % 2) + (x - 30) % 3;
			CHECK(capture->pixels->primary[(size_t)y * width + x] == expected);
		}
	st_rip3_bitmap_destroy(capture);
	CHECK(st_rip3_ports_switch(f.host.ports, 1) == 0);
	st_rip3_clipboard_test_fail_allocate = true;
	form(body, "00000000", '0', 0, "1D", "00", "Tile");
	CHECK(process(&f, body, ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_clipboard_test_fail_allocate);
	struct st_rip3_port_info info;
	CHECK(st_rip3_ports_info(f.host.ports, 1, &info) == 0 && !info.defined && !f.selection.set);
	CHECK(st_rip3_ports_current(f.host.ports, &current) == 0 && current == 0 && f.prompts == 0);
	uint8_t value;
	CHECK(st_rip3_ports_read(f.host.ports, 0, 30, 42, &value) == 0 && value == 8); /* stagger on zero */
	destroy(&f);
	return 0;
}

static int
flag_range(const char *directory)
{
	struct fixture f = {0};
	char           body[160], flags[3] = {0};
	CHECK(create(&f, ST_RIP3_CANVAS_640_480, 640, 480, directory) == 0);
	CHECK(st_rip3_palettes_protect(f.host.palettes, 1, false) == 0);
	for (unsigned radix = 36; radix <= 64; radix += 28) {
		struct st_rip3_environment env;
		CHECK(st_rip3_environments_get(f.host.environments, 0, &env) == 0);
		env.base_math = radix;
		CHECK(st_rip3_environments_set(f.host.environments, 0, &env) == 0);
		for (unsigned value = 0; value < 64; value++) {
			CHECK(st_rip3_number_encode(flags, 2, value, 2, radix) == 0);
			form(body, "00000000", '1', 0, flags, "0C", "Tile");
			CHECK(process(&f, body, -1) == 0);
		}
		/* Invalid flags precede filename access and every visible side effect. */
		const unsigned invalid[] = {64, 65, radix * radix - 1};
		for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
			CHECK(st_rip3_number_encode(flags, 2, invalid[i], 2, radix) == 0);
			st_rip3_bitmap_file_test_fail_allocate = true;
			form(body, "00000000", '0', 0, flags, "00", "Missing");
			CHECK(process(&f, body, ST_RIP3_COMMAND_INVALID) == 0
			      && st_rip3_bitmap_file_test_fail_allocate);
			st_rip3_bitmap_file_test_fail_allocate = false;
		}
	}
	CHECK(f.prompts == 0);
	destroy(&f);
	return 0;
}

static int
wallpaper_scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, const char *directory)
{
	for (unsigned stagger = 0; stagger < 2; stagger++) {
		struct fixture f = {0};
		char           body[160];
		uint64_t       budget = UINT64_C(1000000);
		CHECK(create(&f, canvas, width, height, directory) == 0);
		f.host.clipboard              = &f.selection;
		f.host.offscreen_pixel_budget = &budget;
		f.selection                   = (struct st_rip3_clipboard_selection) {true, 9};
		const struct st_rip3_rect old = {0, 0, 4, 3};
		CHECK(st_rip3_ports_define(f.host.ports, 9, &old, true) == 0);
		/* XOR + keyed background form the seed once. Explicit duplication
		 * captures it before wallpaper, and deleting its clipboard assignment
		 * clears that pointer without assigning the explicit replacement. */
		form(body, "02030000", '9', 1, stagger ? "1L" : "0P", "0C", "Tile");
		struct st_rip3_stream           *stream = NULL;
		const struct st_rip3_stream_sink sink   = {.context = &f,
		    .command                                      = command,
		    .diagnostic                                   = diagnostic};
		CHECK(st_rip3_stream_create(&stream, &sink, f.host.environments) == 0);
		st_rip3_stream_process(stream, (const uint8_t *)"!|", 2);
		for (size_t i = 0; i < strlen(body); i++)
			st_rip3_stream_process(stream, (const uint8_t *)body + i, 1);
		st_rip3_stream_process(stream, (const uint8_t *)"|", 1);
		st_rip3_stream_destroy(stream);
		CHECK(f.errors == 0 && f.prompts == 0 && !f.selection.set);
		const uint8_t seed[] = {86, 85, 101, 149, 85, 170};
		CHECK(duplicate_pixels(&f, 9, 3, 2, seed) == 0);
		struct st_rip3_palette palette;
		struct st_rip3_bitmap *capture = NULL;
		CHECK(st_rip3_palettes_get(f.host.palettes, 1, &palette) == 0
		      && st_rip3_bitmap_capture(&capture, f.host.ports, 0, NULL, &palette) == 0);
		const uint8_t plain[2][3] = {
		    {85, 170, 149},
                    {85, 101, 86 }
                };
		const uint8_t shifted[4][3] = {
		    {85,  170, 149},
                    {101, 86,  85 },
                    {170, 149, 85 },
                    {85,  101, 86 }
                };
		for (uint32_t y = 0; y < height; y++)
			for (uint32_t x = 0; x < width; x++) {
				unsigned expected = 0;
				if (x >= 12 && x <= 59 && y >= 21 && y <= 38)
					expected = stagger ? shifted[(y - 21) % 4][(x - 12) % 3]
					                   : plain[(y - 21) % 2][(x - 12) % 3];
				CHECK(capture->pixels->primary[(size_t)y * width + x] == expected);
			}
		for (unsigned i = 0; i < 256; i++)
			CHECK(capture->palette[i] == i * UINT32_C(0x010101));
		st_rip3_bitmap_destroy(capture);
		destroy(&f);
	}
	return 0;
}

static int
key_asset(const char *directory, unsigned depth, bool translation)
{
	uint8_t  bytes[1100] = {0};
	char     name[24];
	char    *path   = NULL;
	unsigned count  = depth == 1 ? 2 : depth <= 8 ? 4 : 0;
	size_t   offset = 54 + count * 4, stride = depth <= 8 ? 4 : depth == 16 ? 8 : depth == 24 ? 12 : 16;
	size_t   size = offset + stride;
	bytes[0]      = 'B';
	bytes[1]      = 'M';
	put32(bytes + 2, (uint32_t)size);
	put32(bytes + 10, (uint32_t)offset);
	put32(bytes + 14, 40);
	put32(bytes + 18, 4);
	put32(bytes + 22, 1);
	put16(bytes + 26, 1);
	put16(bytes + 28, depth);
	put32(bytes + 46, count);
	const uint32_t colors[]     = {0xffffff, 0, 0xffffff, 0xfefefe},
		       translated[] = {0x070707, 0, 0x070708, 0xffffff};
	for (unsigned i = 0; i < count; i++) {
		uint32_t color    = translation ? translated[i] : colors[i];
		bytes[54 + i * 4] = (uint8_t)color;
		bytes[55 + i * 4] = (uint8_t)(color >> 8);
		bytes[56 + i * 4] = (uint8_t)(color >> 16);
	}
	if (depth == 1)
		bytes[offset] = 0x50;
	else if (depth == 4) {
		bytes[offset]     = 0x01;
		bytes[offset + 1] = 0x23;
	}
	else if (depth == 8) {
		bytes[offset + 1] = 1;
		bytes[offset + 2] = 2;
		bytes[offset + 3] = 3;
	}
	else if (depth == 16) {
		const uint8_t row[] = {0x10, 0x42, 0, 0x80, 0xff, 0xff, 0x42, 0x08};
		memcpy(bytes + offset, row, sizeof(row));
	}
	else {
		const uint32_t rgb[] = {0x808080, 0x808081, 0, 0xffffff};
		for (unsigned i = 0; i < 4; i++) {
			size_t at     = offset + i * (depth / 8);
			bytes[at]     = (uint8_t)rgb[i];
			bytes[at + 1] = (uint8_t)(rgb[i] >> 8);
			bytes[at + 2] = (uint8_t)(rgb[i] >> 16);
			if (depth == 32)
				bytes[at + 3] = (uint8_t)(i * 85); /* Not alpha. */
		}
	}
	(void)snprintf(name, sizeof(name), translation ? "Translate.BMP" : "Key%u.BMP", depth);
	CHECK(path_join_alloc(&path, directory, name) == 0 && fs_replace_file(path, bytes, size) == 0);
	free(path);
	path = NULL;
	if (!translation) {
		(void)snprintf(name, sizeof(name), "Mask%u.BMM", depth);
		CHECK(path_join_alloc(&path, directory, name) == 0 && fs_replace_file(path, bytes, size) == 0);
		free(path);
	}
	return 0;
}

static int
keyed_depths(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, const char *directory)
{
	struct fixture f = {0};
	char           body[160], coordinates[9] = {0}, name[24];
	uint64_t       budget     = UINT64_C(1000000);
	const unsigned depths[]   = {1, 4, 8, 16, 24, 32};
	const uint8_t  mono[5][4] = {
	    {255, 85, 255, 85},
            {170, 85, 170, 85},
            {255, 85, 255, 85},
            {85,  85, 85,  85},
            {0,   85, 0,   85}
        };
	const uint8_t indexed[5][4] = {
	    {85, 0,   85, 254},
            {85, 85,  85, 171},
            {85, 85,  85, 255},
            {85, 0,   85, 84 },
            {85, 255, 85, 1  }
        };
	const uint8_t rgb[5][4] = {
	    {85, 128, 0,   255},
            {85, 213, 85,  170},
            {85, 213, 85,  255},
            {85, 0,   0,   85 },
            {85, 127, 255, 0  }
        };
	const uint8_t rgb32[5][4] = {
	    {128, 128, 0,   255},
            {213, 213, 85,  170},
            {213, 213, 85,  255},
            {0,   0,   0,   85 },
            {127, 127, 255, 0  }
        };
	CHECK(create(&f, canvas, width, height, directory) == 0);
	f.host.clipboard              = &f.selection;
	f.host.offscreen_pixel_budget = &budget;
	f.selection                   = (struct st_rip3_clipboard_selection) {true, 9};
	const struct st_rip3_rect old = {0, 0, 2, 1};
	CHECK(st_rip3_ports_define(f.host.ports, 9, &old, true) == 0 && st_rip3_ports_switch(f.host.ports, 9) == 0
	      && st_rip3_ports_erase(f.host.ports, 42) == 0 && st_rip3_ports_switch(f.host.ports, 1) == 0);
	for (size_t d = 0; d < sizeof(depths) / sizeof(*depths); d++)
		for (unsigned mode = 0; mode < 5; mode++) {
			CHECK(st_rip3_number_encode(coordinates, 2, mode * 6, 2, 36) == 0
			      && st_rip3_number_encode(coordinates + 2, 2, d * 2, 2, 36) == 0);
			memcpy(coordinates + 4, "0000", 4);
			(void)snprintf(name, sizeof(name), mode % 2 == 0 ? "Key%u.BMP" : "Mask%u.BMM", depths[d]);
			form(body, coordinates, '9', mode, depths[d] == 16 ? "0P" : "08",
			    depths[d] >= 24 ? "3K" : "73", name);
			CHECK(process(&f, body, depths[d] == 16 ? ST_RIP3_COMMAND_UNSUPPORTED : -1) == 0);
		}
	/* Keyed RGB555 errors skip duplicate/wallpaper and retain the clipboard.
	 * A later byte-split valid command must still paint. */
	CHECK(f.prompts == 5 && f.selection.set && f.selection.port == 9);
	const uint8_t prior[] = {42, 42, 42, 42, 42, 42};
	CHECK(duplicate_pixels(&f, 9, 3, 2, prior) == 0);
	struct st_rip3_stream           *stream = NULL;
	const struct st_rip3_stream_sink sink   = {.context = &f, .command = command, .diagnostic = diagnostic};
	CHECK(st_rip3_stream_create(&stream, &sink, f.host.environments) == 0);
	st_rip3_stream_process(stream, (const uint8_t *)"!|", 2);
	for (unsigned depth = 16; depth <= 32; depth += 16) {
		(void)snprintf(name, sizeof(name), "Key%u.BMP", depth);
		form(body, "000E0000", '9', 0, depth == 16 ? "0P" : "08", "3K", name);
		for (size_t i = 0; i < strlen(body); i++)
			st_rip3_stream_process(stream, (const uint8_t *)body + i, 1);
		st_rip3_stream_process(stream, (const uint8_t *)"|", 1);
	}
	st_rip3_stream_destroy(stream);
	CHECK(f.prompts == 6 && f.errors == 6 && f.selection.set && f.selection.port == 9);
	CHECK(duplicate_pixels(&f, 9, 3, 2, prior) == 0);
	struct st_rip3_palette palette;
	struct st_rip3_bitmap *capture = NULL;
	CHECK(st_rip3_palettes_get(f.host.palettes, 1, &palette) == 0
	      && st_rip3_bitmap_capture(&capture, f.host.ports, 0, NULL, &palette) == 0);
	CHECK(capture->pixels->width == width && capture->pixels->height == height);
	for (uint32_t y = 0; y < height; y++)
		for (uint32_t x = 0; x < width; x++) {
			unsigned expected = 0;
			if (x >= 12 && x <= 59 && y >= 21 && y <= 38) {
				uint32_t vx = x - 12, vy = y - 21;
				expected = 85;
				if (vy <= 10 && vy % 2 == 0 && vx < 28 && vx % 6 < 4) {
					unsigned depth = depths[vy / 2];
					if (depth != 16)
						expected = depth == 1    ? mono[vx / 6][vx % 6]
						           : depth == 24 ? rgb[vx / 6][vx % 6]
						           : depth == 32 ? rgb32[vx / 6][vx % 6]
						                         : indexed[vx / 6][vx % 6];
				}
				if (vy == 14 && vx < 4)
					expected = rgb32[0][vx];
			}
			CHECK(capture->pixels->primary[(size_t)y * width + x] == expected);
		}
	CHECK(memcmp(capture->palette, palette.rgb, sizeof(palette.rgb)) == 0);
	st_rip3_bitmap_destroy(capture);
	form(body, "00000000", '0', 0, "08", "00", "MissingMask.BMM");
	CHECK(process(&f, body, ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && f.prompts == 7
	      && strcmp(f.last_name, "MissingMask.BMM") == 0 && f.selection.set && f.selection.port == 9);
	/* A collapsed fit does not disable source-format checks or the marker. */
	form(body, "0A030A05", '9', 0, "0P", "00", "Key16.BMP");
	CHECK(process(&f, body, ST_RIP3_COMMAND_UNSUPPORTED) == 0 && f.prompts == 8 && f.natural_size
	      && f.last_area.x0 == 10 && f.last_area.y0 == 3);
	CHECK(duplicate_pixels(&f, 9, 3, 2, prior) == 0);
	/* Ordinary RGB555 succeeds through the same file loader. */
	form(body, "000G0000", '0', 0, "00", "ZZ", "Key16.BMP");
	CHECK(process(&f, body, -1) == 0);
	const uint8_t ordinary[] = {131, 0, 255, 16};
	for (int32_t x = 0; x < 4; x++) {
		uint8_t actual;
		CHECK(st_rip3_ports_read(f.host.ports, 1, 2 + x, 17, &actual) == 0 && actual == ordinary[x]);
	}
	icon(body, "060G", 0, 0, 0, "Key16.BMP");
	CHECK(process(&f, body, -1) == 0);
	icon(body, "0C0G", 0, 0, 0, "Key32.BMP");
	CHECK(process(&f, body, -1) == 0);
	for (int32_t x = 0; x < 4; x++) {
		uint8_t actual;
		CHECK(st_rip3_ports_read(f.host.ports, 1, 8 + x, 17, &actual) == 0 && actual == ordinary[x]);
		CHECK(st_rip3_ports_read(f.host.ports, 1, 14 + x, 17, &actual) == 0 && actual == rgb32[0][x]);
	}
	destroy(&f);
	return 0;
}

static int
translated_key_load(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, const char *directory)
{
	struct fixture f = {0};
	char           body[160];
	CHECK(create(&f, canvas, width, height, directory) == 0
	      && st_rip3_palettes_protect(f.host.palettes, 1, false) == 0);
	uint64_t budget               = UINT64_C(1000000);
	f.host.clipboard              = &f.selection;
	f.host.offscreen_pixel_budget = &budget;
	f.selection                   = (struct st_rip3_clipboard_selection) {true, 9};
	const struct st_rip3_rect old = {0, 0, 2, 1};
	CHECK(st_rip3_ports_define(f.host.ports, 9, &old, true) == 0);
	uint32_t duplicate = 0x070707;
	CHECK(st_rip3_palettes_rgb(f.host.palettes, 255, 8, &duplicate, 1) == 0);
	/* Raw255 translates to7 before source mapping. File indices0 and2 map
	 * to7; a key below236 remains a direct index. */
	form(body, "00000000", '9', 0, "09", "73", "Translate.BMP");
	CHECK(process(&f, body, -1) == 0);
	form(body, "00020000", '0', 0, "08", "5K", "Translate.BMP");
	CHECK(process(&f, body, -1) == 0);
	const uint8_t keyed[] = {85, 0, 85, 254}, direct[] = {7, 0, 7, 254};
	CHECK(duplicate_pixels(&f, 9, 4, 1, keyed) == 0 && !f.selection.set);
	for (int32_t x = 0; x < 4; x++) {
		uint8_t actual;
		CHECK(st_rip3_ports_read(f.host.ports, 1, 2 + x, 1, &actual) == 0 && actual == keyed[x]);
		CHECK(st_rip3_ports_read(f.host.ports, 1, 2 + x, 3, &actual) == 0 && actual == direct[x]);
	}
	/* Translation precedes adoption. After adopting entries0..3, RGB7 first
	 * matches entry0; translating afterward would key out the first pixel. */
	form(body, "00040000", '0', 0, "0A", "73", "Translate.BMP");
	CHECK(process(&f, body, -1) == 0);
	for (int32_t x = 0; x < 4; x++) {
		uint8_t actual;
		CHECK(st_rip3_ports_read(f.host.ports, 1, 2 + x, 5, &actual) == 0 && actual == x);
	}
	CHECK(f.prompts == 0 && f.errors == 0);
	destroy(&f);
	return 0;
}

int
main(int argc, char **argv)
{
	CHECK(argc == 2 && fs_create_directories(argv[1]) == 0);
	CHECK(asset(argv[1], "Tile.BMP", false, false) == 0 && asset(argv[1], "Color.BMP", true, false) == 0);
	CHECK(asset(argv[1], "Compressed.BMP", false, true) == 0);
	const unsigned keyed[] = {1, 4, 8, 16, 24, 32};
	for (size_t i = 0; i < sizeof(keyed) / sizeof(*keyed); i++)
		CHECK(key_asset(argv[1], keyed[i], false) == 0);
	CHECK(key_asset(argv[1], 8, true) == 0);
	char *companion = NULL;
	CHECK(path_join_alloc(&companion, argv[1], "Key8.BMM") == 0
	      && fs_replace_file(companion, "bad companion", 13) == 0);
	free(companion); /* Unreferenced companion cannot affect Key8.BMP loads. */
	CHECK(keyed_depths(ST_RIP3_CANVAS_640_480, 640, 480, argv[1]) == 0);
	CHECK(keyed_depths(ST_RIP3_CANVAS_800_600, 800, 600, argv[1]) == 0);
	CHECK(keyed_depths(ST_RIP3_CANVAS_1024_768, 1024, 768, argv[1]) == 0);
	CHECK(translated_key_load(ST_RIP3_CANVAS_640_480, 640, 480, argv[1]) == 0);
	CHECK(translated_key_load(ST_RIP3_CANVAS_800_600, 800, 600, argv[1]) == 0);
	CHECK(translated_key_load(ST_RIP3_CANVAS_1024_768, 1024, 768, argv[1]) == 0);
	CHECK(wallpaper_scene(ST_RIP3_CANVAS_640_480, 640, 480, argv[1]) == 0);
	CHECK(wallpaper_scene(ST_RIP3_CANVAS_800_600, 800, 600, argv[1]) == 0);
	CHECK(wallpaper_scene(ST_RIP3_CANVAS_1024_768, 1024, 768, argv[1]) == 0);
	CHECK(duplicate_routes(ST_RIP3_CANVAS_640_480, 640, 480, argv[1]) == 0);
	CHECK(duplicate_routes(ST_RIP3_CANVAS_800_600, 800, 600, argv[1]) == 0);
	CHECK(duplicate_routes(ST_RIP3_CANVAS_1024_768, 1024, 768, argv[1]) == 0);
	CHECK(clipboard_retarget(ST_RIP3_CANVAS_640_480, 640, 480, argv[1]) == 0);
	CHECK(clipboard_retarget(ST_RIP3_CANVAS_800_600, 800, 600, argv[1]) == 0);
	CHECK(clipboard_retarget(ST_RIP3_CANVAS_1024_768, 1024, 768, argv[1]) == 0);
	CHECK(flag_range(argv[1]) == 0);
	CHECK(scene(ST_RIP3_CANVAS_640_480, 640, 480, argv[1]) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600, 800, 600, argv[1]) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768, 1024, 768, argv[1]) == 0);
	for (unsigned depth = 1; depth <= 8; depth = depth == 1 ? 4 : depth * 2) {
		CHECK(adoption_asset(argv[1], depth) == 0);
		CHECK(adoption(ST_RIP3_CANVAS_640_480, 640, 480, depth, argv[1]) == 0);
		CHECK(adoption(ST_RIP3_CANVAS_800_600, 800, 600, depth, argv[1]) == 0);
		CHECK(adoption(ST_RIP3_CANVAS_1024_768, 1024, 768, depth, argv[1]) == 0);
	}
	CHECK(dithering(ST_RIP3_CANVAS_640_480, 640, 480, argv[1]) == 0);
	CHECK(dithering(ST_RIP3_CANVAS_800_600, 800, 600, argv[1]) == 0);
	CHECK(dithering(ST_RIP3_CANVAS_1024_768, 1024, 768, argv[1]) == 0);
	CHECK(duplication(ST_RIP3_CANVAS_640_480, 640, 480, argv[1]) == 0);
	CHECK(duplication(ST_RIP3_CANVAS_800_600, 800, 600, argv[1]) == 0);
	CHECK(duplication(ST_RIP3_CANVAS_1024_768, 1024, 768, argv[1]) == 0);
	CHECK(tall_asset(argv[1]) == 0 && asset(argv[1], "BUTTON.BMP", false, false) == 0);
	CHECK(icons(ST_RIP3_CANVAS_640_480, 640, 480, argv[1]) == 0);
	CHECK(icons(ST_RIP3_CANVAS_800_600, 800, 600, argv[1]) == 0);
	CHECK(icons(ST_RIP3_CANVAS_1024_768, 1024, 768, argv[1]) == 0);
	CHECK(errors(argv[1]) == 0 && numeric(argv[1]) == 0 && duplicate_failures(argv[1]) == 0
	      && icon_failures(argv[1]) == 0);
	return 0;
}
