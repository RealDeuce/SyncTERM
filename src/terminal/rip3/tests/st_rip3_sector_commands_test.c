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

extern bool     st_rip3_drawing_test_fail_calloc;
extern unsigned st_rip3_ellipse_test_realloc_countdown;
extern unsigned st_rip3_drawing_test_calloc_countdown;

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

/* Independent radius-three, unit-width annulus at integer pixel addresses. */
static const char *const circle[] = {
    "..###...",
    ".#...#..",
    "#.....#.",
    "#.....#.",
    "#.....#.",
    ".#...#..",
    "..###...",
    "........",
};

struct observation {
	unsigned                        errors;
	enum st_rip3_command_diagnostic kind;
};

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
	struct observation                seen = {0};
	const struct st_rip3_command_sink sink = {&seen, diagnostic};
	st_rip3_commands_process(host, &sink, (const uint8_t *)command, strlen(command));
	if (seen.errors != (error < 0 ? 0u : 1u) || (error >= 0 && (int)seen.kind != error))
		fprintf(stderr, "%s: errors %u, kind %d, expected %d\n", command, seen.errors, (int)seen.kind,
		    error);
	CHECK(seen.errors == (error < 0 ? 0u : 1u));
	if (error >= 0)
		CHECK((int)seen.kind == error);
	return 0;
}

static int
create(struct st_rip3_command_host *host, enum st_rip3_canvas canvas, uint32_t world_width, uint32_t world_height)
{
	struct st_rip3_style initial = {
	    .drawing_color    = 12,
	    .background_color = 1,
	    .fill_color       = 7,
	    .pen              = {.pattern = 0xffff, .thickness = 1, .draw_background = true},
	    .font             = {0, 0, 1, 0},
	    .write_mode       = ST_RIP3_COPY
        };
	struct st_rip3_environment environment;
	uint32_t                   colors[256];
	CHECK(st_rip3_fill_solid(&initial.fill, true) == 0);
	memcpy(initial.fill.rows, (const uint8_t[]) {170, 85, 170, 85, 170, 85, 170, 85}, 8);
	CHECK(st_rip3_styles_create(&host->styles, &initial) == 0);
	CHECK(st_rip3_environments_create(&host->environments, canvas) == 0);
	CHECK(st_rip3_environments_get(host->environments, 0, &environment) == 0);
	environment.world_width  = world_width;
	environment.world_height = world_height;
	CHECK(st_rip3_environments_set(host->environments, 0, &environment) == 0);
	CHECK(st_rip3_ports_create(&host->ports, canvas) == 0);
	CHECK(st_rip3_palettes_create(&host->palettes) == 0);
	for (unsigned i = 0; i < 256; i++)
		colors[i] = i * UINT32_C(0x010101);
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
	while (width != 0) {
		result[--width] = digits[value % base];
		value /= base;
	}
}

static void
command(char result[32], char kind, unsigned width, unsigned base, unsigned x, unsigned y, unsigned rx,
    unsigned ry)
{
	size_t position = 1;
	result[0]       = kind;
	number(result + position, width, x, base);
	position += width;
	number(result + position, width, y, base);
	position += width;
	if (kind == 'I' || kind == 'i' || kind == '_') {
		number(result + position, 2, 0, base);
		number(result + position + 2, 2, 90, base);
		position += 4;
	}
	number(result + position, width, rx, base);
	position += width;
	if (kind == 'i' || kind == '_') {
		number(result + position, width, ry, base);
		position += width;
	}
	result[position] = 0;
}

struct scene {
	const struct st_rip3_command_host *host;
	unsigned                           errors;
};

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
	struct scene                     *scene = context;
	const struct st_rip3_command_sink sink  = {scene, scene_error};
	st_rip3_commands_process(scene->host, &sink, bytes, length);
}

static void
wire(struct st_rip3_stream *stream, const char *text)
{
	for (size_t i = 0; i < strlen(text); i++)
		st_rip3_stream_process(stream, (const uint8_t *)text + i, 1);
}

/* Pictures use a radius-three annulus, literal cardinal closing edges and
 * pixel-center circle/half-plane inequalities, not a renderer-generated image. */
static int
matrix(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, bool offscreen)
{
	static const struct {
		char     kind;
		unsigned first, last, rx, ry, picture;
	} cases[] = {
	    {'I', 0,   90,  3, 3, 0},
	    {'i', 0,   90,  3, 3, 0},
	    {'_', 0,   90,  3, 3, 1},
	    {'I', 270, 90,  3, 3, 2},
	    {'I', 0,   360, 3, 3, 3},
	    {'_', 0,   360, 3, 3, 4},
	    {'I', 0,   1,   3, 3, 5},
	    {'_', 0,   1,   3, 3, 6},
	    {'I', 450, 810, 3, 3, 7},
	    {'i', 45,  45,  1, 3, 8},
	    {'_', 90,  0,   3, 3, 9},
	};

	/* PR #77 logical 8-bit vectors: D=A5, Fill=3C, Back=66, Draw=0F. */
	static const unsigned            fill_ink[] = {0x3c, 0x99, 0xbd, 0x24, 0x5a, 0x3c};
	static const unsigned            back_ink[] = {0x66, 0xc3, 0xe7, 0x24, 0x5a, 0x66};
	struct st_rip3_command_host      host       = {0};
	struct st_rip3_environment       env;
	struct st_rip3_style             style;
	struct st_rip3_palette           palette;
	struct st_pixel_buffer          *capture = NULL;
	struct scene                     scene   = {&host, 0};
	const struct st_rip3_stream_sink sink    = {.context = &scene,
	    .command                                      = scene_command,
	    .diagnostic                                   = scene_error};
	struct st_rip3_stream           *stream  = NULL;
	const struct st_rip3_rect        screen = {10, 20, 137, 155}, bitmap = {0, 0, 127, 135};
	const struct st_rip3_rect        view = {2, 3, 121, 130}, copy = {250, 101, 369, 228};
	bool                             borders = false;
	CHECK(create(&host, canvas, width, height) == 0);
	CHECK(process(&host, borders ? "N01" : "N00", -1) == 0);
	CHECK(st_rip3_ports_define(host.ports, 1, offscreen ? &bitmap : &screen, offscreen) == 0);
	CHECK(st_rip3_ports_switch(host.ports, 1) == 0 && st_rip3_ports_viewport(host.ports, &view) == 0);
	CHECK(st_rip3_ports_protect(host.ports, 1, true) == 0);
	CHECK(st_rip3_stream_create(&stream, &sink, host.environments) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0);
	CHECK(st_rip3_environments_get(host.environments, 0, &env) == 0);
	for (unsigned scale = 1; scale <= 2; scale++)
		for (unsigned base = 36; base <= 64; base += 28)
			for (unsigned xy = 2; xy <= 5; xy++) {
				env.world_width     = width * scale;
				env.world_height    = height * scale * 2;
				env.base_math       = base;
				env.coordinate_size = xy;
				CHECK(st_rip3_ports_erase(host.ports, 0) == 0);
				for (unsigned row = 0; row < 12; row++) {
					bool rgb = row % 6 == 5;
					borders  = row >= 6;
					CHECK(process(&host, borders ? "N01" : "N00", -1) == 0);
					env.rgb_bits = rgb ? 8 : 0;
					CHECK(st_rip3_environments_set(host.environments, 0, &env) == 0);
					style.write_mode          = (enum st_rip3_write_mode)(rgb ? 0 : row % 6);
					style.drawing_color       = rgb ? 0x0f0f0f : 0x0f;
					style.fill_color          = rgb ? 0x3c3c3c : 0x3c;
					style.background_color    = rgb ? 0x666666 : 0x66;
					style.pen.draw_background = row % 2 != 0;
					style.borders             = borders;
					CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
					for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
						const struct st_rip3_rect cell = {(int32_t)c * 10,
						    (int32_t)row * 10, (int32_t)c * 10 + 7, (int32_t)row * 10 + 7};
						char                      shape[32];
						/* Filled sector borders are solid COPY even with a requested
				 * custom mask and either background-gap flag. */
						static const uint16_t patterns[] = {0xffff, 0xaaaa, 1, 0x3333};
						style.pen.pattern                = patterns[c % 4];
						style.line_type                  = c % 5;
						CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
						CHECK(st_rip3_ports_fill(host.ports, &cell, 0xa5, ST_RIP3_COPY)
						      == 0);
						command(shape, cases[c].kind, xy, base, (c * 10 + 3) * scale,
						    (row * 10 + 3) * scale * 2, cases[c].rx * scale,
						    cases[c].ry * scale * 2);
						number(shape + 1 + 2 * xy, 2, cases[c].first, base);
						number(shape + 3 + 2 * xy, 2, cases[c].last, base);
						if (c % 2 != 0) {
							size_t pos = strlen(shape) - xy;
							number(shape + pos, 1,
							    (cases[c].kind == 'I' ? cases[c].rx : cases[c].ry * 2)
							        * scale,
							    base);
							shape[pos + 1] = 0;
						}
						wire(stream, "!|");
						wire(stream, shape);
						wire(stream, "\r");
					}
				}
				CHECK(scene.errors == 0);
				if (offscreen)
					CHECK(st_rip3_ports_copy(host.ports, 1, NULL, 0, &copy,
						  ST_RIP3_TRANSFER_COPY)
					      == 0);
				CHECK(st_rip3_palettes_get(host.palettes, 0, &palette) == 0);
				CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
				unsigned left = offscreen ? 250 : 12, top = offscreen ? 101 : 23;
				/* Fill phase belongs to the source drawing context, even when the
		 * finished bitmap is copied to a differently phased screen position. */
				unsigned phase_x = offscreen ? 2 : 12, phase_y = offscreen ? 3 : 23;
				for (uint32_t y = 0; y < height; y++)
					for (uint32_t x = 0; x < width; x++) {
						unsigned expected = 0;
						if (x >= left && x < left + 110 && y >= top && y < top + 120) {
							unsigned c = (x - left) / 10, row = (y - top) / 10,
								 u = (x - left) % 10, v = (y - top) % 10;
							if (u < 8 && v < 8) {
								int      dx = 2 * (int)u - 5, dy = 2 * (int)v - 5;
								unsigned picture  = cases[c].picture;
								bool     interior = dx * dx + dy * dy < 36;
								if (picture == 0)
									interior = interior && u >= 3 && v < 3;
								else if (picture == 1)
									interior = interior && (int)u - (int)v > 3;
								else if (picture == 2)
									interior = interior && u >= 3;
								else if (picture == 9)
									interior =
									    interior && (int)u - (int)v <= 3;
								else if (picture != 5 && picture != 6)
									interior = false;
								bool line =
								    picture == 3   ? v == 3 && u >= 3 && u <= 6
								    : picture == 7 ? u == 3 && v <= 3
								    : picture == 8 ? u + v == 6 && u >= 3 && u <= 5
								                   : false;
								bool outline =
								    picture == 0
								        ? (circle[v][u] == '#' && u >= 3 && v <= 3)
								              || (u == 3 && v <= 3)
								              || (v == 3 && u >= 3 && u <= 6)
								    : picture == 1
								        ? (circle[v][u] == '#' && u >= 3 && v <= 3)
								              || ((int)u - (int)v == 3 && u >= 3
								                  && u <= 6)
								    : picture == 2
								        ? (circle[v][u] == '#' && u >= 3)
								              || (u == 3 && v <= 6)
								    : picture == 5
								        ? circle[v][u] == '#'
								              || (v == 3 && u >= 3 && u <= 6)
								    : picture == 6 ? circle[v][u] == '#'
								    : picture == 9 ? (circle[v][u] == '#'
								                         && (u <= 3 || v >= 3))
								                         || ((int)u - (int)v == 3
								                             && u >= 3 && u <= 6)
								                   : false;
								expected = 0xa5;
								if (interior)
									expected =
									    ((phase_x + x - left + phase_y + y
									         - top) % 2
									                == 0
									            ? fill_ink
									            : back_ink)[row % 6];
								if (row >= 6 && line)
									expected = 0x0f;
								if (row >= 6 && outline)
									expected = 0x0f;
							}
						}
						if (capture->primary[(size_t)y * width + x]
						    != (ST_PIXEL_COLOR_RGB_TAG | expected * UINT32_C(0x010101))) {
							fprintf(stderr,
							    "scene %u/%u/%u off=%d pixel %u,%u got %x expected %x\n",
							    scale, base, xy, offscreen, x, y,
							    capture->primary[(size_t)y * width + x], expected);
							return 1;
						}
					}
				st_pixel_buffer_destroy(capture);
				capture = NULL;
			}
	st_rip3_stream_destroy(stream);
	destroy(&host);
	return 0;
}

static int
unchanged(const struct st_rip3_command_host *host)
{
	uint8_t value;
	for (int32_t y = 0; y < 8; y++)
		for (int32_t x = 0; x < 8; x++)
			CHECK(st_rip3_ports_read(host->ports, 0, x, y, &value) == 0 && value == 0x55);
	return 0;
}

static int
boundaries(void)
{
	static const char *const    invalid[] = {"I", "i", "_", "I0303002I", "I0303002I0?", "i0303002I030?",
	    "_0303??2I0303", "I0303002I0 comment"};
	struct st_rip3_command_host host      = {0}, missing;
	struct st_rip3_style        original, style;
	struct st_rip3_environment  env;
	const struct st_rip3_rect   area    = {0, 0, 7, 7};
	bool                        borders = true;
	CHECK(create(&host, ST_RIP3_CANVAS_640_480, 640, 480) == 0);
	CHECK(process(&host, borders ? "N01" : "N00", -1) == 0);
	CHECK(st_rip3_ports_viewport(host.ports, &area) == 0 && st_rip3_ports_erase(host.ports, 0x55) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &original) == 0);
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
		CHECK(process(&host, invalid[i], ST_RIP3_COMMAND_INVALID) == 0);
	missing        = host;
	missing.styles = NULL;
	CHECK(process(&missing, "I0303002I03", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing        = host;
	missing.styles = NULL;
	CHECK(process(&missing, "i0303002I0303", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing       = host;
	missing.ports = NULL;
	CHECK(process(&missing, "_0303002I0303", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	for (const char *kind = "Ii_"; *kind != 0; kind++) {
		char shape[32];
		command(shape, *kind, 2, 36, 3, 3, 3, 3);
		for (unsigned allocation = 1; allocation <= 2; allocation++) {
			st_rip3_ellipse_test_realloc_countdown = allocation;
			CHECK(process(&host, shape, ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0
			      && st_rip3_ellipse_test_realloc_countdown == 0);
			CHECK(unchanged(&host) == 0);
		}
		st_rip3_drawing_test_fail_calloc = true;
		CHECK(process(&host, shape, ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0
		      && !st_rip3_drawing_test_fail_calloc);
		CHECK(unchanged(&host) == 0);
	}
	/* Full Pie prepares a separate radius before collecting its ellipse.
	 * Fail either owned mask and each mesh allocation before any fill occurs. */
	for (unsigned allocation = 1; allocation <= 2; allocation++) {
		st_rip3_drawing_test_calloc_countdown = allocation;
		CHECK(process(&host, "I0303000103", ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0);
		CHECK(st_rip3_drawing_test_calloc_countdown == 0 && unchanged(&host) == 0);
		st_rip3_ellipse_test_realloc_countdown = allocation;
		CHECK(process(&host, "i030300010303", ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0);
		CHECK(st_rip3_ellipse_test_realloc_countdown == 0 && unchanged(&host) == 0);
	}
	st_rip3_drawing_test_fail_calloc = true;
	CHECK(process(&host, "I030300A003", ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0);
	CHECK(!st_rip3_drawing_test_fail_calloc && unchanged(&host) == 0);
	style               = original;
	style.drawing_color = 0xffffff;
	style.borders       = borders;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	CHECK(process(&host, "I0303002I03", ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && unchanged(&host) == 0);
	style            = original;
	style.fill_color = 0xffffff;
	style.borders    = borders;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	CHECK(process(&host, "_0303002I0303", ST_RIP3_COMMAND_OPERATION_FAILED) == 0 && unchanged(&host) == 0);
	/* PR #77: requested line masks do not affect solid filled borders,
	 * and a non-COPY fill does not change their COPY operation. */
	style             = original;
	style.pen.pattern = 0xaaaa;
	style.borders     = borders;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	CHECK(process(&host, "I0303002I03", -1) == 0);
	uint8_t value;
	CHECK(st_rip3_ports_read(host.ports, 0, 3, 3, &value) == 0 && value == 12);
	CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0);
	style            = original;
	style.write_mode = ST_RIP3_XOR;
	style.borders    = borders;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	CHECK(process(&host, "i0303002I0303", -1) == 0);
	CHECK(st_rip3_ports_read(host.ports, 0, 3, 3, &value) == 0 && value == 12);
	CHECK(st_rip3_ports_read(host.ports, 0, 4, 2, &value) == 0 && value == 0x52);
	CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0);
	/* Equal chord does not consume pen, fill ink, N or storage. */
	style            = original;
	style.fill_color = style.drawing_color = 0xffffff;
	style.pen.pattern                      = 1;
	style.borders                          = borders;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	missing                                = host;
	missing.palettes                       = NULL;
	st_rip3_drawing_test_fail_calloc       = true;
	st_rip3_ellipse_test_realloc_countdown = 1;
	CHECK(process(&missing, "_030300A00303", -1) == 0);
	CHECK(st_rip3_drawing_test_fail_calloc && st_rip3_ellipse_test_realloc_countdown == 1
	      && unchanged(&host) == 0);
	/* N0 equal-endpoint pies have no fill area or outline. They consume no
	 * ink, palette, coverage storage or mesh allocation on an active port. */
	style.borders = false;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	CHECK(process(&missing, "I030300A003", -1) == 0 && process(&missing, "i030300A00303", -1) == 0);
	CHECK(st_rip3_drawing_test_fail_calloc && st_rip3_ellipse_test_realloc_countdown == 1
	      && unchanged(&host) == 0);
	CHECK(st_rip3_ports_activate(host.ports, false) == 0);
	CHECK(process(&missing, "i0303002I0303", -1) == 0 && unchanged(&host) == 0);
	CHECK(st_rip3_drawing_test_fail_calloc && st_rip3_ellipse_test_realloc_countdown == 1);
	st_rip3_drawing_test_fail_calloc       = false;
	st_rip3_ellipse_test_realloc_countdown = 0;
	CHECK(st_rip3_ports_activate(host.ports, true) == 0);
	original.borders = borders;
	CHECK(st_rip3_styles_set(host.styles, 0, &original) == 0);
	struct scene                     scene  = {&host, 0};
	const struct st_rip3_stream_sink sink   = {.context = &scene,
	    .command                                      = scene_command,
	    .diagnostic                                   = scene_error};
	struct st_rip3_stream           *stream = NULL;
	CHECK(st_rip3_stream_create(&stream, &sink, host.environments) == 0);
	wire(stream, "!|I0303002I0?|W1|I030300A003|W0|_030300A00303|I030300A003\r");
	CHECK(scene.errors == 1);
	for (int32_t y = 0; y < 8; y++)
		for (int32_t x = 0; x < 8; x++)
			CHECK(st_rip3_ports_read(host.ports, 0, x, y, &value) == 0
			      && value == (y == 3 && x >= 3 && x <= 6 ? 12 : 0x55));
	st_rip3_stream_destroy(stream);
	/* Preserve wide geometry until clipping: these equal-angle radial
	 * endpoints are huge and only the center-to-left line is visible. */
	CHECK(st_rip3_environments_get(host.environments, 0, &env) == 0);
	env.base_math       = 64;
	env.coordinate_size = 5;
	env.world_width = env.world_height = 1;
	CHECK(st_rip3_environments_set(host.environments, 0, &env) == 0);
	char shape[32];
	command(shape, 'I', 5, 64, 1073741823, 0, 1073741822, 0);
	number(shape + 11, 2, 180, 64);
	number(shape + 13, 2, 180, 64);
	CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0);
	CHECK(process(&host, shape, -1) == 0 && unchanged(&host) == 0);
	command(shape, 'I', 5, 64, 1073741823, 0, 1073741823, 0);
	number(shape + 11, 2, 180, 64);
	number(shape + 13, 2, 180, 64);
	CHECK(process(&host, shape, -1) == 0);
	for (int32_t y = 0; y < 8; y++)
		for (int32_t x = 0; x < 8; x++)
			CHECK(
			    st_rip3_ports_read(host.ports, 0, x, y, &value) == 0 && value == (y == 0 ? 12 : 0x55));
	destroy(&host);
	return 0;
}

int
main(void)
{
	CHECK(boundaries() == 0);
	CHECK(matrix(ST_RIP3_CANVAS_640_480, 640, 480, false) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_640_480, 640, 480, true) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_800_600, 800, 600, false) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_800_600, 800, 600, true) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_1024_768, 1024, 768, false) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_1024_768, 1024, 768, true) == 0);
	return 0;
}
