/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "st_pixels.h"
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_rip3_stream.h"
#include "st_rip3_styles.h"
#include "st_rip3_text_commands.h"

extern bool     st_rip3_drawing_test_fail_calloc;
extern bool     st_rip3_path_test_fail_malloc;
extern unsigned st_rip3_path_test_malloc_countdown;
extern unsigned st_rip3_polygon_test_malloc_countdown;
extern unsigned st_rip3_ellipse_test_realloc_countdown;
extern unsigned st_rip3_drawing_test_calloc_countdown;

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

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
		fprintf(stderr, "%.32s: errors %u, kind %d, expected %d\n", command, seen.errors, (int)seen.kind,
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

struct box {
	unsigned x0, y0, x1, y1;
};

static unsigned
boxes(unsigned kind, struct box result[3])
{
	result[0] = (struct box) {1, 1, 7, 7};
	result[1] = (struct box) {3, 3, 5, 5};
	if (kind == 1) {
		result[0] = (struct box) {1, 1, 5, 5};
		result[1] = (struct box) {3, 3, 7, 7};
	}
	if (kind == 2)
		result[1] = result[0];
	if (kind == 3) {
		result[0] = (struct box) {1, 1, 9, 9};
		result[1] = (struct box) {3, 3, 7, 7};
		result[2] = (struct box) {4, 4, 6, 6};
	}
	return kind == 3 ? 3 : 2;
}

static unsigned
phase(const struct box *box, unsigned u, unsigned v, unsigned side, bool reverse)
{
	unsigned w = box->x1 - box->x0, h = box->y1 - box->y0;
	if (!reverse)
		return side == 0   ? u - box->x0
		       : side == 1 ? w + v - box->y0
		       : side == 2 ? w + h + box->x1 - u
		                   : 2 * w + h + box->y1 - v;
	return side == 0   ? 2 * h + w + box->x1 - u
	       : side == 1 ? h + w + box->y1 - v
	       : side == 2 ? h + u - box->x0
	                   : v - box->y0;
}

static void
multi(char *bytes, unsigned kind, unsigned row, unsigned scale, unsigned xy, unsigned base)
{
	struct box rects[3];
	unsigned   count    = boxes(kind, rects);
	size_t     position = 3;
	bytes[0]            = '<';
	number(bytes + 1, 2, count, base);
	for (unsigned c = 0; c < count; c++) {
		unsigned coords[] = {rects[c].x0, rects[c].y0, rects[c].x1, rects[c].y0, rects[c].x1, rects[c].y1,
		    rects[c].x0, rects[c].y1};
		bool     reverse  = (c + kind) % 2 != 0;
		number(bytes + position, c == 0 ? 2 : xy, 4, base);
		position += c == 0 ? 2 : xy;
		for (unsigned i = 0; i < 4; i++) {
			unsigned point = reverse ? (4 - i) % 4 : i;
			number(bytes + position, xy, (coords[2 * point] + kind * 12) * scale, base);
			position += xy;
			number(bytes + position, xy, (coords[2 * point + 1] + row * 12) * scale * 2, base);
			position += xy;
		}
	}
	bytes[position] = 0;
	/* At the last record, only the final Y coordinate may shorten. */
	unsigned final = (rects[count - 1].y0 + row * 12) * scale * 2;
	if ((count - 1 + kind) % 2 == 0)
		final = (rects[count - 1].y1 + row * 12) * scale * 2;
	if (kind % 2 != 0 && final < base) {
		number(bytes + position - xy, 1, final, base);
		bytes[position - xy + 1] = 0;
	}
}

static int
matrix(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, bool offscreen)
{
	static const unsigned            fg[] = {7, 0x52, 0x57, 5, 0xaa, 7}, bg[] = {1, 0x54, 0x55, 1, 0xaa, 1};
	struct st_rip3_command_host      host = {0};
	struct st_rip3_environment       env;
	struct st_rip3_style             style;
	struct st_rip3_palette           palette;
	struct st_pixel_buffer          *capture = NULL;
	struct scene                     scene   = {&host, 0};
	const struct st_rip3_stream_sink sink    = {.context = &scene,
	    .command                                      = scene_command,
	    .diagnostic                                   = scene_error};
	struct st_rip3_stream           *stream  = NULL;
	const struct st_rip3_rect        screen = {10, 20, 89, 339}, bitmap = {0, 0, 79, 319};
	const struct st_rip3_rect        view = {2, 3, 65, 306}, copy = {251, 101, 314, 404};
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
				env.coordinate_size = xy;
				env.base_math       = base;
				CHECK(st_rip3_ports_erase(host.ports, 0) == 0);
				for (unsigned row = 0; row < 24; row++) {
					unsigned mode = row % 6;
					bool     rgb  = mode == 5;
					env.rgb_bits  = rgb ? 8 : 0;
					borders       = row >= 6;
					CHECK(process(&host, borders ? "N01" : "N00", -1) == 0);
					CHECK(st_rip3_environments_set(host.environments, 0, &env) == 0);
					style.write_mode          = (enum st_rip3_write_mode)(rgb ? 0 : mode);
					style.drawing_color       = rgb ? 0x0c0c0c : 12;
					style.fill_color          = rgb ? 0x070707 : 7;
					style.background_color    = rgb ? 0x010101 : 1;
					style.pen.pattern         = row >= 12 ? 0x8000 : 0xffff;
					style.pen.draw_background = row >= 18;
					style.borders             = borders;
					CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
					for (unsigned col = 0; col < 4; col++) {
						const struct st_rip3_rect cell = {(int32_t)col * 12,
						    (int32_t)row * 12, (int32_t)col * 12 + 10,
						    (int32_t)row * 12 + 10};
						char                      bytes[160];
						CHECK(st_rip3_ports_fill(host.ports, &cell, 0x55, ST_RIP3_COPY)
						      == 0);
						multi(bytes, col, row, scale, xy, base);
						wire(stream, "!|");
						wire(stream, bytes);
						wire(stream, "\r");
					}
				}
				CHECK(scene.errors == 0);
				if (offscreen)
					CHECK(st_rip3_ports_copy(host.ports, 1, NULL, 0, &copy,
						  ST_RIP3_TRANSFER_COPY)
					      == 0);
				CHECK(st_rip3_palettes_get(host.palettes, 0, &palette) == 0
				      && st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
				unsigned left = offscreen ? 251 : 12, top = offscreen ? 101 : 23;
				unsigned phase_x = offscreen ? 2 : 12, phase_y = offscreen ? 3 : 23;
				for (uint32_t y = 0; y < height; y++)
					for (uint32_t x = 0; x < width; x++) {
						unsigned expected = 0;
						if (x >= left && x < left + 48 && y >= top && y < top + 288) {
							unsigned col = (x - left) / 12, row = (y - top) / 12,
								 u = (x - left) % 12, v = (y - top) % 12;
							if (u <= 10 && v <= 10) {
								struct box rects[3];
								unsigned   count = boxes(col, rects), parity = 0;
								bool       edge = false, on = false;
								for (unsigned c = 0; c < count; c++) {
									const struct box *b = rects + c;
									parity += u >= b->x0 && u < b->x1
									          && v >= b->y0 && v < b->y1;
									bool sides[] = {v == b->y0 && u >= b->x0
									                    && u <= b->x1,
									    u == b->x1 && v >= b->y0 && v <= b->y1,
									    v == b->y1 && u >= b->x0 && u <= b->x1,
									    u == b->x0 && v >= b->y0
									        && v <= b->y1};
									for (unsigned side = 0; side < 4; side++)
										if (sides[side]) {
											edge = true;
											on |=
											    phase(b, u, v, side,
												(c + col) % 2 != 0)
											        % 16
											    == 0;
										}
								}
								expected = 0x55;
								if (parity % 2 != 0)
									expected =
									    ((phase_x + x - left + phase_y + y
									         - top) % 2
									                == 0
									            ? fg
									            : bg)[row % 6];
								if (row >= 6 && edge
								    && (row < 12 || on || row >= 18))
									expected = row < 12 || on ? 12 : 1;
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
line_pixels(const struct st_rip3_command_host *host, unsigned color)
{
	uint8_t value;
	for (int32_t y = 0; y < 8; y++)
		for (int32_t x = 0; x < 8; x++)
			CHECK(st_rip3_ports_read(host->ports, 0, x, y, &value) == 0
			      && value == (y == 3 && x >= 1 && x <= 6 ? color : 0x55));
	return 0;
}

static size_t
single(char *bytes, char kind, unsigned count, unsigned xy, unsigned base)
{
	bytes[0] = kind;
	number(bytes + 1, 2, count, base);
	size_t pos = 3;
	for (unsigned i = 0; i < count; i++) {
		number(bytes + pos, xy, i % 2 == 0 ? 1 : 6, base);
		pos += xy;
		number(bytes + pos, xy, 3, base);
		pos += xy;
	}
	bytes[pos] = 0;
	return pos;
}

static size_t
many(char *bytes, const unsigned *counts, unsigned count, unsigned xy, unsigned base)
{
	bytes[0] = '<';
	number(bytes + 1, 2, count, base);
	size_t pos = 3;
	for (unsigned c = 0; c < count; c++) {
		number(bytes + pos, c == 0 ? 2 : xy, counts[c], base);
		pos += c == 0 ? 2 : xy;
		for (unsigned i = 0; i < counts[c]; i++) {
			number(bytes + pos, xy, i % 2 == 0 ? 1 : 6, base);
			pos += xy;
			number(bytes + pos, xy, 3, base);
			pos += xy;
		}
	}
	bytes[pos] = 0;
	return pos;
}

static int
counts(void)
{
	static const unsigned       line[] = {12, 0x59, 0x5d, 4, 0xaa};
	struct st_rip3_command_host host   = {0}, missing;
	struct st_rip3_environment  env;
	struct st_rip3_style        style, after;
	struct st_rip3_text_context text    = {.encoding = ST_RIP3_TEXT_UTF8};
	const struct st_rip3_rect   view    = {0, 0, 7, 7};
	bool                        borders = true;
	char                       *bytes   = malloc(70000);
	unsigned                   *sizes   = malloc(1640 * sizeof(*sizes));
	CHECK(bytes != NULL && sizes != NULL);
	CHECK(create(&host, ST_RIP3_CANVAS_640_480, 640, 480) == 0);
	CHECK(process(&host, borders ? "N01" : "N00", -1) == 0);
	host.text = &text;
	CHECK(st_rip3_ports_viewport(host.ports, &view) == 0);
	const struct st_rip3_cursor point = {7, 8};
	struct st_rip3_port_info    cursor_info;
	CHECK(st_rip3_ports_cursor(host.ports, 0, &point) == 0);
	CHECK(st_rip3_environments_get(host.environments, 0, &env) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0);
	style.pen.draw_background = false;
	for (unsigned base = 36; base <= 64; base += 28)
		for (unsigned xy = 2; xy <= 5; xy++) {
			env.base_math       = base;
			env.coordinate_size = xy;
			CHECK(st_rip3_environments_set(host.environments, 0, &env) == 0);
			for (unsigned mode = 0; mode < 5; mode++) {
				style.write_mode = (enum st_rip3_write_mode)mode;
				style.borders    = borders;
				CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
				for (const char *kind = "Plp"; *kind != 0; kind++) {
					CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0);
					missing                          = host;
					missing.styles                   = NULL;
					missing.ports                    = NULL;
					missing.palettes                 = NULL;
					st_rip3_path_test_fail_malloc    = true;
					st_rip3_drawing_test_fail_calloc = true;
					single(bytes, *kind, 0, xy, base);
					CHECK(process(&missing, bytes, ST_RIP3_COMMAND_INVALID) == 0);
					size_t pos = single(bytes, *kind, 1, xy, base);
					/* Short final Y remains fully validated even for a no-op. */
					bytes[pos - xy]     = '3';
					bytes[pos - xy + 1] = 0;
					CHECK(process(&missing, bytes, -1) == 0 && line_pixels(&host, 0x55) == 0);
					CHECK(st_rip3_path_test_fail_malloc && st_rip3_drawing_test_fail_calloc);
					st_rip3_path_test_fail_malloc    = false;
					st_rip3_drawing_test_fail_calloc = false;
					single(bytes, *kind, 2, xy, base);
					CHECK(process(&host, bytes, -1) == 0);
					CHECK(line_pixels(&host, *kind == 'p' ? 12 : line[mode]) == 0);
					CHECK(st_rip3_styles_get(host.styles, 0, &after) == 0
					      && after.pen.pattern == style.pen.pattern
					      && after.pen.thickness == style.pen.thickness
					      && after.line_type == style.line_type
					      && after.write_mode == style.write_mode);
					CHECK(st_rip3_ports_info(host.ports, 0, &cursor_info) == 0
					      && cursor_info.cursor.x == 7 && cursor_info.cursor.y == 8
					      && text.encoding == ST_RIP3_TEXT_UTF8);
				}
			}
			style.borders    = borders;
			style.write_mode = ST_RIP3_COPY;
			CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
			unsigned maximum = base * base - 1;
			for (const char *kind = "Plp"; *kind != 0; kind++) {
				size_t pos = single(bytes, *kind, maximum, xy, base);
				CHECK(bytes[1] == (base == 36 ? 'Z' : '&') && bytes[2] == bytes[1]);
				CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0 && process(&host, bytes, -1) == 0
				      && line_pixels(&host, 12) == 0);
				bytes[pos]     = '0';
				bytes[pos + 1] = 0;
				CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0
				      && process(&host, bytes, ST_RIP3_COMMAND_INVALID) == 0
				      && line_pixels(&host, 0x55) == 0);
			}
			/* Exact reachable aggregate-vertex constructions from the reference. */
			unsigned n;
			if (base == 36 && xy == 2) {
				sizes[0] = sizes[1] = sizes[2] = 1295;
				sizes[3]                       = 209;
				n                              = 4;
			}
			else if (base == 64 && xy == 2) {
				sizes[0] = 4093;
				sizes[1] = 2;
				n        = 2;
			}
			else {
				sizes[0] = 2;
				sizes[1] = 4093;
				n        = 2;
			}
			many(bytes, sizes, n, xy, base);
			CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0 && process(&host, bytes, -1) == 0
			      && line_pixels(&host, 12) == 0);
			sizes[n - 1]++;
			many(bytes, sizes, n, xy, base);
			CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0
			      && process(&host, bytes, ST_RIP3_COMMAND_INVALID) == 0
			      && line_pixels(&host, 0x55) == 0);
			unsigned max_contours = base == 36 ? 1295 : 1638;
			for (unsigned c = 0; c <= max_contours; c++)
				sizes[c] = 2;
			many(bytes, sizes, max_contours, xy, base);
			CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0 && process(&host, bytes, -1) == 0
			      && line_pixels(&host, 12) == 0);
			if (base == 64) {
				many(bytes, sizes, max_contours + 1, xy, base);
				CHECK(st_rip3_ports_erase(host.ports, 0x55) == 0
				      && process(&host, bytes, ST_RIP3_COMMAND_INVALID) == 0
				      && line_pixels(&host, 0x55) == 0);
			}
		}
	free(sizes);
	free(bytes);
	destroy(&host);
	return 0;
}

static int
boundaries(void)
{
	static const char *const    invalid[] = {"<", "<00", "<0000", "<0101", "<01010101", "<010201010?01",
	    "<020201010601", "<0102010106010", "<01020101060?", "<01000101"};
	struct st_rip3_command_host host      = {0}, missing;
	struct st_rip3_environment  env;
	struct st_rip3_style        style;
	const struct st_rip3_rect   view    = {0, 0, 7, 7};
	bool                        borders = true;
	char                        bytes[160];
	CHECK(create(&host, ST_RIP3_CANVAS_640_480, 640, 480) == 0);
	CHECK(process(&host, borders ? "N01" : "N00", -1) == 0);
	CHECK(st_rip3_ports_viewport(host.ports, &view) == 0 && st_rip3_ports_erase(host.ports, 0x55) == 0);
	st_rip3_path_test_fail_malloc = true;
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
		CHECK(process(&host, invalid[i], ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(st_rip3_path_test_fail_malloc && line_pixels(&host, 0x55) == 0);
	st_rip3_path_test_fail_malloc = false;
	multi(bytes, 0, 0, 1, 2, 36); /* Two proper contours. */
	for (unsigned allocation = 1; allocation <= 2; allocation++) {
		st_rip3_path_test_malloc_countdown = allocation;
		CHECK(process(&host, bytes, ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0);
		CHECK(st_rip3_path_test_malloc_countdown == 0 && line_pixels(&host, 0x55) == 0);
		st_rip3_polygon_test_malloc_countdown = allocation;
		CHECK(process(&host, bytes, ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0);
		CHECK(st_rip3_polygon_test_malloc_countdown == 0 && line_pixels(&host, 0x55) == 0);
	}
	st_rip3_drawing_test_fail_calloc = true;
	CHECK(process(&host, bytes, ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0 && !st_rip3_drawing_test_fail_calloc
	      && line_pixels(&host, 0x55) == 0);
	CHECK(process(&host, "<010201010601 comment", ST_RIP3_COMMAND_INVALID) == 0);
	missing        = host;
	missing.styles = NULL;
	CHECK(process(&missing, bytes, ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(st_rip3_ports_activate(host.ports, false) == 0);
	st_rip3_path_test_fail_malloc = true;
	CHECK(process(&host, bytes, -1) == 0 && st_rip3_path_test_fail_malloc);
	st_rip3_path_test_fail_malloc = false;
	CHECK(st_rip3_ports_activate(host.ports, true) == 0);
	CHECK(st_rip3_styles_get(host.styles, 0, &style) == 0);
	style.drawing_color = style.fill_color = style.background_color = 0xffffff;
	style.borders                                                   = borders;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	borders = false;
	CHECK(process(&host, borders ? "N01" : "N00", -1) == 0);
	st_rip3_path_test_fail_malloc = true;
	CHECK(process(&host, "p0201010601", -1) == 0 && st_rip3_path_test_fail_malloc
	      && line_pixels(&host, 0x55) == 0);
	CHECK(process(&host, "<0202010106010201010601", -1) == 0 && st_rip3_path_test_fail_malloc
	      && line_pixels(&host, 0x55) == 0);
	st_rip3_path_test_fail_malloc = false;
	style.drawing_color           = 12;
	style.fill_color              = 7;
	style.background_color        = 1;
	style.borders                 = borders;
	CHECK(st_rip3_styles_set(host.styles, 0, &style) == 0);
	struct scene                     scene  = {&host, 0};
	const struct st_rip3_stream_sink sink   = {.context = &scene,
	    .command                                      = scene_command,
	    .diagnostic                                   = scene_error};
	struct st_rip3_stream           *stream = NULL;
	CHECK(st_rip3_stream_create(&stream, &sink, host.environments) == 0);
	/* Later count is XY-wide: this is the reference's literal XY=3 vector. */
	wire(stream,
	    "!|n3000|<0203001002001002001002002001002001002|<020300100200100200100203001002001002|n2|P00|l010103|l0201030603\r");
	CHECK(scene.errors == 2 && line_pixels(&host, 12) == 0);
	st_rip3_stream_destroy(stream);
	/* Wide affine crossings share one parity calculation across contours. */
	CHECK(st_rip3_environments_get(host.environments, 0, &env) == 0);
	env.coordinate_size = 5;
	env.base_math       = 64;
	env.world_width = env.world_height = 1;
	CHECK(st_rip3_environments_set(host.environments, 0, &env) == 0
	      && st_rip3_ports_erase(host.ports, 0x55) == 0);
	style.borders = borders;
	CHECK(st_rip3_fill_solid(&style.fill, true) == 0 && st_rip3_styles_set(host.styles, 0, &style) == 0);
	const unsigned coords[] = {0, 0, 1073741822, 536870911, 1073741822, 0, 0, 0, 1, 0, 1, 1, 0, 1};
	size_t         pos      = 5;
	memcpy(bytes, "<0203", 5);
	for (unsigned i = 0; i < 14; i++) {
		if (i == 6) {
			number(bytes + pos, 5, 4, 64);
			pos += 5;
		}
		number(bytes + pos, 5, coords[i], 64);
		pos += 5;
	}
	bytes[pos] = 0;
	CHECK(process(&host, bytes, -1) == 0);
	uint8_t value;
	for (int32_t y = 0; y < 8; y++)
		for (int32_t x = 0; x < 8; x++)
			CHECK(st_rip3_ports_read(host.ports, 0, x, y, &value) == 0
			      && value == (8 * (2 * y + 1) < 3 * (2 * x + 1) ? 0x55 : 7));
	destroy(&host);
	return 0;
}

int
main(void)
{
	CHECK(counts() == 0);
	CHECK(boundaries() == 0);
	CHECK(matrix(ST_RIP3_CANVAS_640_480, 640, 480, false) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_640_480, 640, 480, true) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_800_600, 800, 600, false) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_800_600, 800, 600, true) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_1024_768, 1024, 768, false) == 0);
	CHECK(matrix(ST_RIP3_CANVAS_1024_768, 1024, 768, true) == 0);
	return 0;
}
