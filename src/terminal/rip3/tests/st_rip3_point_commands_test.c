/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "st_pixels.h"
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_rip3_stream.h"
#include "st_rip3_styles.h"

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

struct fixture {
	struct st_rip3_command_host     host;
	uint64_t                        budget;
	unsigned                        errors;
	enum st_rip3_command_diagnostic kind;
};

static void
diagnostic(void *context, enum st_rip3_command_diagnostic kind)
{
	struct fixture *f = context;
	f->errors++;
	f->kind = kind;
}

static void
command(void *context, const uint8_t *bytes, size_t length)
{
	struct fixture                   *f    = context;
	const struct st_rip3_command_sink sink = {f, diagnostic};
	st_rip3_commands_process(&f->host, &sink, bytes, length);
}

static int
process(struct fixture *f, const char *bytes, int expected)
{
	unsigned before = f->errors;
	command(f, (const uint8_t *)bytes, strlen(bytes));
	CHECK(f->errors - before == (expected < 0 ? 0u : 1u));
	if (expected >= 0)
		CHECK((int)f->kind == expected);
	return 0;
}

static int
wire(struct fixture *f, struct st_rip3_stream *stream, const char *bytes)
{
	unsigned before = f->errors;
	for (size_t i = 0; i < strlen(bytes); i++)
		st_rip3_stream_process(stream, (const uint8_t *)bytes + i, 1);
	CHECK(f->errors == before);
	return 0;
}

static int
create(struct fixture *f, enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_style initial = {
	    .drawing_color    = 3,
	    .background_color = 1,
	    .fill_color       = 2,
	    .pen              = {.pattern = 0xffff, .thickness = 1},
	    .font             = {0, 0, 1, 0},
	    .write_mode       = ST_RIP3_COPY
        };
	struct st_rip3_environment env;
	uint32_t                   colors[256];
	*f                             = (struct fixture) {.budget = 200000};
	f->host.offscreen_pixel_budget = &f->budget;
	CHECK(st_rip3_styles_create(&f->host.styles, &initial) == 0);
	CHECK(st_rip3_ports_create(&f->host.ports, canvas) == 0);
	CHECK(st_rip3_palettes_create(&f->host.palettes) == 0);
	CHECK(st_rip3_environments_create(&f->host.environments, canvas) == 0);
	CHECK(st_rip3_environments_get(f->host.environments, 0, &env) == 0);
	env.world_width  = 2 * width;
	env.world_height = 2 * height;
	CHECK(st_rip3_environments_set(f->host.environments, 0, &env) == 0);
	for (unsigned i = 0; i < 256; i++)
		colors[i] = i * UINT32_C(0x010101);
	CHECK(st_rip3_palettes_rgb(f->host.palettes, 0, 8, colors, 256) == 0);
	return 0;
}

static void
destroy(struct fixture *f)
{
	st_rip3_styles_destroy(f->host.styles);
	st_rip3_ports_destroy(f->host.ports);
	st_rip3_palettes_destroy(f->host.palettes);
	st_rip3_environments_destroy(f->host.environments);
}

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct fixture                   f;
	struct st_rip3_stream           *stream = NULL;
	const struct st_rip3_stream_sink sink   = {.context = &f, .command = command, .diagnostic = diagnostic};
	struct st_rip3_environment       env;
	struct st_rip3_port_info         info;
	struct st_rip3_style             style;
	struct st_rip3_palette           palette;
	struct st_pixel_buffer          *capture = NULL;
	const struct st_rip3_rect        whole   = {0, 0, 2047, 2047};
	unsigned                         current;
	CHECK(create(&f, canvas, width, height) == 0);
	CHECK(st_rip3_stream_create(&stream, &sink, f.host.environments) == 0);
	CHECK(st_rip3_ports_fill(f.host.ports, &whole, 1, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_environments_get(f.host.environments, 0, &env) == 0);
	env.port           = 9;
	env.graphics_style = 7;
	env.palette        = 8;
	CHECK(st_rip3_environments_define(f.host.environments, 1, &env) == 0);
	CHECK(st_rip3_environments_select(f.host.environments, 1) == 0);
	CHECK(st_rip3_environments_protect(f.host.environments, 1, true) == 0);
	/* Default subport viewport starts at (25,25). j offsets once; X does not.
	 * The replacement v stores (40,40) directly, not port origin + (40,40). */
	CHECK(wire(&f, stream, "!|2P11E1E6Y6Y00020000|=00000003|c4|X0202|j0202|c6|X1S1S\r") == 0);
	CHECK(wire(&f, stream, "!|v28283A3A|c5|j0202|X0202|c7|X2G2G|=140XPM04|c8|j0K0K\r") == 0);
	CHECK(wire(&f, stream, "!|=00000074|cA|j0U0U|cB|X3636|cC\r") == 0);
	CHECK(st_rip3_ports_protect(f.host.ports, 1, true) == 0);
	{
		struct st_rip3_style prepared;
		CHECK(st_rip3_styles_get(f.host.styles, 0, &prepared) == 0
		      && st_rip3_styles_define(f.host.styles, 1, &prepared) == 0);
	}
	CHECK(st_rip3_styles_switch(f.host.styles, 1) == 0);
	CHECK(st_rip3_styles_protect(f.host.styles, 1, true) == 0);
	CHECK(wire(&f, stream, "!|j0C0C\r") == 0); /* Protection locks attributes, not content. */
	CHECK(st_rip3_styles_switch(f.host.styles, 0) == 0);
	CHECK(wire(&f, stream, "!|2P300005K5K00030000\r") == 0);
	CHECK(st_rip3_ports_fill(f.host.ports, &whole, 2, ST_RIP3_COPY) == 0);
	CHECK(wire(&f, stream, "!|=00000001|c3|j0202|c4|X0404|v1O1O3C3C|c5|j0202|c7|X0202|c6|X1S1U\r") == 0);
	CHECK(wire(&f, stream, "!|v00005I5I\r") == 0); /* Full 100x100 source viewport. */
	/* Assemble screen and offscreen observations into one synchronous capture.
	 * Half-open COPY destination [220,100,320,200), full source shorthand. */
	CHECK(wire(&f, stream, "!|2C3000000000C85KHSB40\r") == 0);
	CHECK(st_rip3_palettes_get(f.host.palettes, 0, &palette) == 0);
	CHECK(st_rip3_ports_snapshot(f.host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++)
		for (uint32_t x = 0; x < width; x++) {
			unsigned expected = 1;
			if (x >= 25 && x <= 27 && y >= 25 && y <= 27)
				expected = 4;
			if (x == 32 && y == 32)
				expected = 6;
			if (x >= 40 && x <= 42 && y >= 40 && y <= 42)
				expected = 5;
			if (x == 44 && y == 44)
				expected = 7;
			if (x >= 48 && x <= 52 && y >= 48 && y <= 52)
				expected = 8;
			if (x == 55 && y == 55)
				expected = 10;
			if (x == 57 && y == 57)
				expected = 11;
			if (x == 46 && y == 46)
				expected = 12;
			if (x >= 220 && x < 320 && y >= 100 && y < 200) {
				expected = 2;
				if (x == 221 && y == 101)
					expected = 3;
				if (x == 222 && y == 102)
					expected = 4;
				if (x == 251 && y == 131)
					expected = 5;
				if (x == 252 && y == 133)
					expected = 6;
			}
			CHECK(capture->primary[(size_t)y * width + x]
			      == (ST_PIXEL_COLOR_RGB_TAG | expected * UINT32_C(0x010101)));
		}
	CHECK(st_rip3_ports_current(f.host.ports, &current) == 0 && current == 3);
	CHECK(st_rip3_ports_info(f.host.ports, 1, &info) == 0 && info.protected && info.viewport_active
	      && info.viewport.x0 == 15 && info.viewport.y0 == 15 && info.viewport.x1 == 34
	      && info.viewport.y1 == 34);
	CHECK(st_rip3_styles_get(f.host.styles, 1, &style) == 0 && style.drawing_color == 12
	      && style.pen.thickness == 1);
	CHECK(st_rip3_environments_get(f.host.environments, 1, &env) == 0 && env.port == 9
	      && env.graphics_style == 7 && env.palette == 8);
	st_pixel_buffer_destroy(capture);
	st_rip3_stream_destroy(stream);
	destroy(&f);
	return 0;
}

static void
field(char *bytes, uint64_t value, unsigned width, unsigned radix)
{
	static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#&";
	while (width != 0) {
		bytes[--width] = digits[value % radix];
		value /= radix;
	}
}

static int
viewport_edges(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct fixture                   f;
	struct st_rip3_stream           *stream = NULL;
	const struct st_rip3_stream_sink sink   = {.context = &f, .command = command, .diagnostic = diagnostic};
	struct st_rip3_palette           palette;
	struct st_pixel_buffer          *capture = NULL;
	struct st_rip3_port_info         info;
	const struct st_rip3_rect        whole = {0, 0, 2047, 2047};
	CHECK(create(&f, canvas, width, height) == 0);
	CHECK(st_rip3_ports_fill(f.host.ports, &whole, 1, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_stream_create(&stream, &sink, f.host.environments) == 0);
	/* Viewport DEVICE [10,20)x[10,20) lies outside the declared screen port
	 * [25,35)x[25,35). j still reaches the underlying screen there, and its
	 * square clips at the viewport's own near/far edges, not at port bounds. */
	CHECK(wire(&f, stream, "!|2P11E1E1Y1Y2|v0K0K1212|=00000002|c3|j0202|c4|j0I0I|c5|X0K0K|c6|X1414|X0202\r")
	      == 0);
	CHECK(st_rip3_ports_info(f.host.ports, 1, &info) == 0 && info.viewport.x0 == -15 && info.viewport.y0 == -15
	      && info.viewport.x1 == -6 && info.viewport.y1 == -6);
	CHECK(st_rip3_palettes_get(f.host.palettes, 0, &palette) == 0);
	CHECK(st_rip3_ports_snapshot(f.host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++)
		for (uint32_t x = 0; x < width; x++) {
			unsigned expected = 1;
			if (x >= 10 && x <= 12 && y >= 10 && y <= 12)
				expected = 3;
			if (x >= 18 && x <= 19 && y >= 18 && y <= 19)
				expected = 4;
			if (x == 10 && y == 10)
				expected = 5;
			CHECK(capture->primary[(size_t)y * width + x]
			      == (ST_PIXEL_COLOR_RGB_TAG | expected * UINT32_C(0x010101)));
		}
	st_pixel_buffer_destroy(capture);
	st_rip3_stream_destroy(stream);
	destroy(&f);
	return 0;
}

static int
sizes_and_clipping(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	static const unsigned      thicknesses[] = {1, 2, 3, 4, 5, 255, 256, 257};
	static const unsigned      radii[]       = {0, 1, 1, 2, 2, 127, 0, 0};
	struct fixture             f;
	struct st_rip3_environment env;
	struct st_rip3_palette     palette;
	struct st_pixel_buffer    *capture = NULL;
	const struct st_rip3_rect  whole   = {0, 0, 2047, 2047};
	char                       bytes[12], setting[10] = "=140XPM01";
	CHECK(create(&f, canvas, width, height) == 0);
	CHECK(st_rip3_environments_get(f.host.environments, 0, &env) == 0);
	for (unsigned radix = 36; radix <= 64; radix += 28)
		for (unsigned xy = 2; xy <= 5; xy++) {
			env.coordinate_size = xy;
			env.base_math       = radix;
			CHECK(st_rip3_environments_set(f.host.environments, 0, &env) == 0);
			for (size_t t = 0; t < sizeof(thicknesses) / sizeof(thicknesses[0]); t++) {
				CHECK(st_rip3_ports_fill(f.host.ports, &whole, 1, ST_RIP3_COPY) == 0);
				field(setting + 3, 43690, 4, radix);
				field(setting + 7, thicknesses[t], 2, radix);
				CHECK(process(&f, setting, -1) == 0);
				/* j at (160,160); X at (400,160), each mapped from 2:1 WORLD.
			 * X stays one pixel even with a 255-pixel stored thickness. */
				bytes[0] = 'j';
				field(bytes + 1, 320, xy, radix);
				field(bytes + 1 + xy, 320, xy, radix);
				bytes[1 + 2 * xy] = 0;
				CHECK(process(&f, bytes, -1) == 0);
				bytes[0] = 'X';
				field(bytes + 1, 800, xy, radix);
				CHECK(process(&f, bytes, -1) == 0);
				CHECK(st_rip3_palettes_get(f.host.palettes, 0, &palette) == 0);
				CHECK(st_rip3_ports_snapshot(f.host.ports, &palette, &capture) == 0);
				for (uint32_t y = 0; y < height; y++)
					for (uint32_t x = 0; x < width; x++) {
						unsigned r = radii[t];
						bool     on =
						    (x >= 160 - r && x <= 160 + r && y >= 160 - r && y <= 160 + r)
						    || (x == 400 && y == 160);
						CHECK(capture->primary[(size_t)y * width + x]
						      == (ST_PIXEL_COLOR_RGB_TAG
						          | (on ? UINT32_C(0x030303) : UINT32_C(0x010101))));
					}
				st_pixel_buffer_destroy(capture);
				capture = NULL;
			}
		}
	/* Keep enormous projected points wide until clipping. They must not wrap
	 * into the device or allocate work proportional to the stored viewport. */
	env.coordinate_size = 5;
	env.base_math       = 64;
	env.world_width = env.world_height = 1;
	CHECK(st_rip3_environments_set(f.host.environments, 0, &env) == 0);
	CHECK(process(&f, "X&&&&&&&&&&", -1) == 0 && process(&f, "j&&&&&&&&&&", -1) == 0);
	const struct st_rip3_rect edge = {(int32_t)width - 1, (int32_t)height - 1, INT32_MAX, INT32_MAX};
	CHECK(st_rip3_ports_viewport_device(f.host.ports, &edge) == 0);
	CHECK(process(&f, "=140Agg3&", -1) == 0); /* Custom AAAA and byte 255 in base64. */
	CHECK(process(&f, "j0000000000", -1) == 0);
	uint8_t last = 0;
	CHECK(
	    st_rip3_ports_read(f.host.ports, 0, (int32_t)width - 1, (int32_t)height - 1, &last) == 0 && last == 3);
	CHECK(st_rip3_ports_snapshot(f.host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++)
		for (uint32_t x = 0; x < width; x++) {
			bool on = (x == 160 && y == 160) || (x == 400 && y == 160)
			          || (x == width - 1 && y == height - 1);
			CHECK(capture->primary[(size_t)y * width + x]
			      == (ST_PIXEL_COLOR_RGB_TAG | (on ? UINT32_C(0x030303) : UINT32_C(0x010101))));
		}
	st_pixel_buffer_destroy(capture);
	CHECK(st_rip3_ports_activate(f.host.ports, false) == 0);
	CHECK(process(&f, "W1", -1) == 0 && process(&f, "j0000000000", -1) == 0);
	destroy(&f);
	return 0;
}

static int
nonuniform_mapping(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, unsigned jx, unsigned jy,
    unsigned xx)
{
	struct fixture             f;
	struct st_rip3_environment env;
	struct st_rip3_palette     palette;
	struct st_pixel_buffer    *capture = NULL;
	const struct st_rip3_rect  whole   = {0, 0, 2047, 2047};

	const struct {
		unsigned jx, y, xx;
	} positions[] = {
	    {jx, jy, xx},
            {5,  20, 10}
        };

	CHECK(create(&f, canvas, width, height) == 0);
	CHECK(process(&f, "fHS9Q", -1) == 0); /* Compatibility world 640x350. */
	for (unsigned i = 0; i < 2; i++) {
		if (i == 1) {
			CHECK(st_rip3_environments_get(f.host.environments, 0, &env) == 0);
			env.world_width  = 2 * width;
			env.world_height = height / 2;
			CHECK(st_rip3_environments_set(f.host.environments, 0, &env) == 0);
		}
		CHECK(st_rip3_ports_fill(f.host.ports, &whole, 1, ST_RIP3_COPY) == 0);
		CHECK(process(&f, "=00000003", -1) == 0 && process(&f, "c3", -1) == 0);
		CHECK(
		    process(&f, "j0A0A", -1) == 0 && process(&f, "c4", -1) == 0 && process(&f, "X0K0A", -1) == 0);
		CHECK(st_rip3_palettes_get(f.host.palettes, 0, &palette) == 0);
		CHECK(st_rip3_ports_snapshot(f.host.ports, &palette, &capture) == 0);
		for (uint32_t y = 0; y < height; y++)
			for (uint32_t x = 0; x < width; x++) {
				unsigned expected = 1;
				if (x >= positions[i].jx - 1 && x <= positions[i].jx + 1 && y >= positions[i].y - 1
				    && y <= positions[i].y + 1)
					expected = 3;
				if (x == positions[i].xx && y == positions[i].y)
					expected = 4;
				CHECK(capture->primary[(size_t)y * width + x]
				      == (ST_PIXEL_COLOR_RGB_TAG | expected * UINT32_C(0x010101)));
			}
		st_pixel_buffer_destroy(capture);
		capture = NULL;
	}
	destroy(&f);
	return 0;
}

static int
validation(void)
{
	static const char *const   invalid[] = {"X", "j", "X00", "j00", "X000 comment", "j00 00", "X000?", "j000?",
	    "X00\2020", "j00\303\251"};
	struct fixture             f, missing = {0};
	struct st_rip3_environment env;
	uint32_t                   pixel = 0;
	CHECK(create(&f, ST_RIP3_CANVAS_640_480, 640, 480) == 0);
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		CHECK(process(&f, invalid[i], ST_RIP3_COMMAND_INVALID) == 0);
		CHECK(process(&f, "j0202", -1) == 0);
	}
	CHECK(process(&f, "X0202 comment", -1) == 0);
	CHECK(process(&f, "X022", -1) == 0 && process(&f, "j022", -1) == 0);
	CHECK(process(&missing, "X0202", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing.host.environments = f.host.environments;
	missing.host.styles       = f.host.styles;
	CHECK(process(&missing, "j0202", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(st_rip3_environments_get(f.host.environments, 0, &env) == 0);
	env.rgb_bits = 8;
	CHECK(st_rip3_environments_set(f.host.environments, 0, &env) == 0);
	CHECK(process(&f, "c0G41", -1) == 0); /* UltraNum 0x010101. */
	CHECK(process(&f, "X0404", -1) == 0);
	CHECK(st_rip3_ports_read_color(f.host.ports, 0, 2, 2, false, &pixel) == 0
	      && pixel == (ST_PIXEL_COLOR_RGB_TAG | 0x010101));
	CHECK(process(&f, "j0606", -1) == 0);
	CHECK(st_rip3_ports_read_color(f.host.ports, 0, 3, 3, false, &pixel) == 0
	      && pixel == (ST_PIXEL_COLOR_RGB_TAG | 0x010101));
	struct st_rip3_palettes *palette = f.host.palettes;
	f.host.palettes                  = NULL;
	CHECK(process(&f, "j0404", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	const struct st_rip3_rect outside = {700, 500, 710, 510};
	CHECK(st_rip3_ports_viewport_device(f.host.ports, &outside) == 0);
	CHECK(process(&f, "X0404", -1) == 0 && process(&f, "j0404", -1) == 0);
	f.host.palettes = palette;
	destroy(&f);
	return 0;
}

static int
composition_vectors(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	/* Published PR #77 vectors: source 3C, destination A5. Literal results
	 * distinguish a single contribution per thick point from repeated writes. */
	static const unsigned     once[]       = {0x3c, 0x99, 0xbd, 0x24, 0x5a};
	static const unsigned     twice[]      = {0x3c, 0xa5, 0xbd, 0x24, 0xa5};
	static const unsigned     background[] = {0x66, 0xc3, 0xe7, 0x24, 0x5a};
	struct fixture            f;
	struct st_rip3_style      style;
	struct st_rip3_port_info  port;
	struct st_rip3_palette    palette;
	struct st_pixel_buffer   *capture = NULL;
	const struct st_rip3_rect area = {0, 0, 119, 51}, copy = {160, 0, 279, 51};
	CHECK(create(&f, canvas, width, height) == 0);
	CHECK(st_rip3_ports_define(f.host.ports, 3, &area, true) == 0);
	for (unsigned target = 0; target <= 3; target += 3) {
		CHECK(st_rip3_ports_switch(f.host.ports, target) == 0);
		CHECK(st_rip3_ports_fill(f.host.ports, &area, 0xa5, ST_RIP3_COPY) == 0);
		CHECK(st_rip3_styles_get(f.host.styles, 0, &style) == 0);
		style.drawing_color       = 0x3c;
		style.background_color    = 0x66;
		style.pen.pattern         = 1;
		style.pen.thickness       = 4;
		style.pen.draw_background = true;
		CHECK(st_rip3_styles_set(f.host.styles, 0, &style) == 0);
		for (unsigned mode = 0; mode < 5; mode++) {
			char setting[] = "W0", bytes[6];
			setting[1] += (char)mode;
			CHECK(process(&f, setting, -1) == 0);
			for (unsigned row = 0; row < 2; row++)
				for (unsigned point = 0; point < 2; point++) {
					unsigned x = mode * 24 + (point ? 14 : 5), y = row ? 16 : 6;
					bytes[0] = point ? 'j' : 'X';
					field(bytes + 1, x * 2, 2, 36);
					field(bytes + 3, y * 2, 2, 36);
					bytes[5] = 0;
					CHECK(process(&f, bytes, -1) == 0);
					if (row)
						CHECK(process(&f, bytes, -1) == 0);
				}
		}
		for (unsigned mode = 0; mode < 5; mode++) {
			CHECK(st_rip3_styles_get(f.host.styles, 0, &style) == 0);
			style.write_mode       = (enum st_rip3_write_mode)mode;
			style.fill_color       = 0x3c;
			style.background_color = 0x66;
			style.drawing_color    = 0x0f;
			style.pen.thickness    = 1;
			memset(style.fill.rows, 0xaa, 8);
			for (unsigned outlined = 0; outlined < 2; outlined++) {
				style.borders = outlined != 0;
				CHECK(st_rip3_styles_set(f.host.styles, 0, &style) == 0);
				for (unsigned rounded = 0; rounded < 2; rounded++) {
					unsigned x = mode * 24 + (rounded ? 14 : 2), y = outlined ? 40 : 28;
					char     bytes[12];
					bytes[0] = rounded ? 'u' : 'K';
					field(bytes + 1, x * 2, 2, 36);
					field(bytes + 3, y * 2, 2, 36);
					field(bytes + 5, (x + 6) * 2, 2, 36);
					field(bytes + 7, (y + 8) * 2, 2, 36);
					if (rounded) {
						bytes[9]  = '0';
						bytes[10] = '0';
						bytes[11] = 0;
					}
					else
						bytes[9] = 0;
					CHECK(process(&f, bytes, -1) == 0);
				}
			}
		}
		CHECK(st_rip3_ports_info(f.host.ports, target, &port) == 0 && port.cursor.x == 0
		      && port.cursor.y == 0);
	}
	CHECK(st_rip3_ports_copy(f.host.ports, 3, NULL, 0, &copy, ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_palettes_get(f.host.palettes, 0, &palette) == 0
	      && st_rip3_ports_snapshot(f.host.ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++)
		for (uint32_t x = 0; x < width; x++) {
			unsigned expected = 0;
			if (y < 52 && (x < 120 || (x >= 160 && x < 280))) {
				unsigned u = x >= 160 ? x - 160 : x, mode = u / 24, local = u % 24;
				expected = 0xa5;
				if ((local == 5 && y == 6) || (local >= 12 && local <= 16 && y >= 4 && y <= 8))
					expected = once[mode];
				if ((local == 5 && y == 16) || (local >= 12 && local <= 16 && y >= 14 && y <= 18))
					expected = twice[mode];
				for (unsigned outlined = 0; outlined < 2; outlined++)
					for (unsigned rounded = 0; rounded < 2; rounded++) {
						unsigned left = rounded ? 14 : 2, top = outlined ? 40 : 28;
						if (local >= left && local < left + 6 && y >= top && y < top + 8)
							expected = u % 2 == 0 ? once[mode] : background[mode];
						if (outlined && local >= left && local <= left + 6 && y >= top
						    && y <= top + 8
						    && (local == left || local == left + 6 || y == top
						        || y == top + 8))
							expected = 0x0f;
					}
			}
			CHECK(capture->primary[(size_t)y * width + x]
			      == (ST_PIXEL_COLOR_RGB_TAG | expected * UINT32_C(0x010101)));
		}
	st_pixel_buffer_destroy(capture);
	/* Destination NOT neither resolves source ink nor requires a palette. */
	struct st_rip3_palettes *bound = f.host.palettes;
	f.host.palettes                = NULL;
	struct st_rip3_environment env;
	CHECK(st_rip3_environments_get(f.host.environments, 0, &env) == 0);
	env.rgb_bits = 8;
	CHECK(st_rip3_environments_set(f.host.environments, 0, &env) == 0);
	CHECK(st_rip3_styles_get(f.host.styles, 0, &style) == 0);
	style.write_mode    = ST_RIP3_NOT;
	style.drawing_color = style.background_color = 0xffffff;
	CHECK(st_rip3_styles_set(f.host.styles, 0, &style) == 0);
	CHECK(process(&f, "X0202", -1) == 0 && process(&f, "j0K0K", -1) == 0);
	f.host.palettes = bound;
	destroy(&f);
	return 0;
}

int
main(void)
{
	CHECK(scene(ST_RIP3_CANVAS_640_480, 640, 480) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600, 800, 600) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768, 1024, 768) == 0);
	CHECK(viewport_edges(ST_RIP3_CANVAS_640_480, 640, 480) == 0);
	CHECK(viewport_edges(ST_RIP3_CANVAS_800_600, 800, 600) == 0);
	CHECK(viewport_edges(ST_RIP3_CANVAS_1024_768, 1024, 768) == 0);
	CHECK(sizes_and_clipping(ST_RIP3_CANVAS_640_480, 640, 480) == 0);
	CHECK(sizes_and_clipping(ST_RIP3_CANVAS_800_600, 800, 600) == 0);
	CHECK(sizes_and_clipping(ST_RIP3_CANVAS_1024_768, 1024, 768) == 0);
	CHECK(nonuniform_mapping(ST_RIP3_CANVAS_640_480, 640, 480, 10, 13, 20) == 0);
	CHECK(nonuniform_mapping(ST_RIP3_CANVAS_800_600, 800, 600, 12, 17, 25) == 0);
	CHECK(nonuniform_mapping(ST_RIP3_CANVAS_1024_768, 1024, 768, 16, 21, 32) == 0);
	CHECK(composition_vectors(ST_RIP3_CANVAS_640_480, 640, 480) == 0);
	CHECK(composition_vectors(ST_RIP3_CANVAS_800_600, 800, 600) == 0);
	CHECK(composition_vectors(ST_RIP3_CANVAS_1024_768, 1024, 768) == 0);
	CHECK(validation() == 0);
	return 0;
}
