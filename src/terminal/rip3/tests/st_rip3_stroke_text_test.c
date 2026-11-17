/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_stroke_text.h"
#include "st_rip3_ports.h"
#include "io_error.h"
#include "st_pixels.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

/* LITT A from the reference's literal MOVE/DRAW example, baseline (2,9).
 * MOVEs never paint. The repeated right stem still applies write mode once. */
static const char *const letter_a[] = {
	"............", "............", "............", "....#.......",
	"...#.#......", "..#...#.....", "..#####.....", "..#...#.....",
	"..#...#.....", "..#...#.....", "............", "............",
};

static int
expect(struct st_rip3_ports *ports, const char *const rows[12], uint8_t on, uint8_t off)
{
	for (int32_t y = 0; y < 12; y++) {
		for (int32_t x = 0; x < 12; x++) {
			uint8_t value, expected = rows[y][x] == '#' ? on : off;

			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			if (value != expected) {
				fprintf(stderr, "at %d,%d: got %u, expected %u\n", x, y, value, expected);
				return 1;
			}
		}
	}
	return 0;
}

static int
raster(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_chr font;
	struct st_rip3_chr_text_metrics metrics;
	const struct st_rip3_rect bounds = {0,0,11,11};
	const struct st_rip3_text_position origin = {2,9};
	const struct st_rip3_chr_text_matrix matrix = {1,0,0,-1};
	const uint8_t a = 'A';
	const uint8_t results[] = {7,14,15,1,246};

	CHECK(st_rip3_chr_builtin(&font, 2) == 0);
	CHECK(st_rip3_chr_text_measure(&font, (const uint8_t *)"A I", 3, &metrics) == 0);
	CHECK(metrics.advance == 17 && metrics.height == 9);
	CHECK(st_rip3_chr_text_measure(&font, NULL, 0, &metrics) == 0);
	CHECK(metrics.advance == 0 && metrics.height == 9);
	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	for (unsigned mode = ST_RIP3_COPY; mode <= ST_RIP3_NOT; mode++) {
		CHECK(st_rip3_ports_erase(ports, 9) == 0);
		CHECK(st_rip3_draw_chr_text(ports, &font, &a, 1, &origin, &matrix,
		    false, 7, 3, (enum st_rip3_write_mode)mode) == 0);
		CHECK(expect(ports, letter_a, results[mode], 9) == 0);
	}
	CHECK(st_rip3_ports_erase(ports, 9) == 0);
	CHECK(st_rip3_draw_chr_text(ports, &font, &a, 1, &origin, &matrix,
	    true, 7, 3, ST_RIP3_XOR) == 0);
	for (int32_t y = 0; y < 12; y++) {
		for (int32_t x = 0; x < 12; x++) {
			uint8_t value;
			uint8_t expected = letter_a[y][x] == '#' ? 14
			    : x > 0 && y > 0 && letter_a[y - 1][x - 1] == '#' ? 10 : 9;

			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == expected);
		}
	}
	/* A space contributes advance but no ink. The I center stem is x=1 in
	 * the original glyph. Its accumulated unit x=(6+6+1)*3/5+2=9.8,
	 * so column 10 is painted, column 9 is clear away from its end bars. */
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_draw_chr_text(ports, &font, (const uint8_t *)"A I", 3,
	    &origin, &(struct st_rip3_chr_text_matrix){0.6,0,0,-1},
	    false, 7, 3, ST_RIP3_COPY) == 0);
	for (int32_t y = 4; y <= 8; y++) {
		uint8_t value;

		CHECK(st_rip3_ports_read(ports, 1, 10, y, &value) == 0 && value == 7);
		CHECK(st_rip3_ports_read(ports, 1, 9, y, &value) == 0 && value == 0);
	}
	{
		const uint8_t missing[] = {'A',0,'I'};

		CHECK(st_rip3_chr_text_measure(&font, missing, 3, &metrics) == 0);
		CHECK(metrics.advance == 11 && metrics.height == 9);
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
transforms(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_chr font;
	const struct st_rip3_rect bounds = {0,0,11,11};
	const uint8_t a = 'A';
	const char *const vertical[] = {
		"............", "............", "............", "............",
		"............", "............", "......#####.", ".....#.#....",
		"....#..#....", ".....#.#....", "......#####.", "............",
	};

	CHECK(st_rip3_chr_builtin(&font, 2) == 0);
	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_800_600) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_draw_chr_text(ports, &font, &a, 1, &(struct st_rip3_text_position){10,10},
	    &(struct st_rip3_chr_text_matrix){0,-1,-1,0}, false, 7, 3, ST_RIP3_XOR) == 0);
	CHECK(expect(ports, vertical, 7, 0) == 0);
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_draw_chr_text(ports, &font, &a, 1, &(struct st_rip3_text_position){2,9},
	    &(struct st_rip3_chr_text_matrix){1,0.5,0,-1}, false, 7, 3, ST_RIP3_COPY) == 0);
	{
		uint8_t value;

		CHECK(st_rip3_ports_read(ports, 1, 7, 3, &value) == 0 && value == 7);
		CHECK(st_rip3_ports_read(ports, 1, 5, 8, &value) == 0 && value == 0);
		CHECK(st_rip3_ports_read(ports, 1, 9, 5, &value) == 0 && value == 0);
	}
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_draw_chr_text(ports, &font, &a, 1, &(struct st_rip3_text_position){0,2},
	    &(struct st_rip3_chr_text_matrix){1,0,0,-1}, false, 7, 3, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 12; y++) {
		for (int32_t x = 0; x < 12; x++) {
			uint8_t value;

			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == ((x == 0 || x == 4) && y <= 2 ? 7 : 0));
		}
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static void
put_word(uint8_t *bytes, unsigned value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

static int
zero_advance_and_shadow_clip(void)
{
	uint8_t bytes[153] = {0};
	struct st_rip3_chr font;
	struct st_rip3_chr_text_metrics metrics;
	struct st_rip3_ports *ports = NULL;
	const uint8_t text[] = {'A','A'};
	const struct st_rip3_rect bounds = {0,0,5,5};
	const struct st_rip3_chr_text_matrix matrix = {1,0,0,-1};

	/* One zero-advance glyph: MOVE(0,0), DRAW(0,2), END. It has no final
	 * MOVE to establish an advance; the width table alone determines it. */
	memcpy(bytes, "PK\010\010TEST\032", 9);
	put_word(bytes + 9, 128); memcpy(bytes + 11, "TEST", 4);
	put_word(bytes + 15, 25); bytes[17] = bytes[19] = 1;
	bytes[128] = '+'; put_word(bytes + 129, 1); bytes[132] = 'A';
	put_word(bytes + 133, 19); bytes[136] = 2;
	bytes[147] = 0x80; bytes[149] = 0x80; bytes[150] = 0x82;
	CHECK(st_rip3_chr_open(&font, bytes, sizeof(bytes)) == 0);
	CHECK(st_rip3_chr_text_measure(&font, text, 2, &metrics) == 0);
	CHECK(metrics.advance == 0 && metrics.height == 2);
	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_draw_chr_text(ports, &font, text, 2, &(struct st_rip3_text_position){3,4},
	    &matrix, false, 7, 3, ST_RIP3_XOR) == 0);
	for (int32_t y = 0; y < 6; y++) {
		for (int32_t x = 0; x < 6; x++) {
			uint8_t value;

			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (x == 3 && y >= 2 && y <= 4 ? 7 : 0));
		}
	}
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_draw_chr_text(ports, &font, text, 2, &(struct st_rip3_text_position){-1,2},
	    &matrix, true, 7, 3, ST_RIP3_XOR) == 0);
	for (int32_t y = 0; y < 6; y++) {
		for (int32_t x = 0; x < 6; x++) {
			uint8_t value;

			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (x == 0 && y >= 1 && y <= 3 ? 3 : 0));
		}
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
errors_and_viewport(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_chr font, bold, invalid;
	struct st_rip3_chr_text_metrics metrics = {77,88};
	const struct st_rip3_rect bounds = {10,20,21,31};
	const struct st_rip3_rect view = {1,1,10,10};
	const struct st_rip3_text_position origin = {1,8};
	const struct st_rip3_chr_text_matrix matrix = {1,0,0,-1};
	const uint8_t a = 'A';
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;

	CHECK(st_rip3_chr_builtin(&font, 2) == 0);
	CHECK(st_rip3_chr_builtin(&bold, 10) == 0);
	CHECK(st_rip3_chr_text_measure(&bold, (const uint8_t *)"A:", 2, &metrics) == 0);
	CHECK(metrics.advance == 49 && metrics.height == 60);
	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_1024_768) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ports_viewport(ports, &view) == 0);
	CHECK(st_rip3_ports_erase(ports, 3) == 0);
	CHECK(st_rip3_draw_chr_text(ports, &bold, (const uint8_t *)"A:", 2, &origin,
	    &(struct st_rip3_chr_text_matrix){0.1,0,0,-0.1}, false,
	    7, 0, ST_RIP3_COPY) == IO_ERROR_NOT_SUPPORTED);
	CHECK(st_rip3_draw_chr_text(ports, &font, &a, 1, &origin,
	    &(struct st_rip3_chr_text_matrix){NAN,0,0,-1}, false,
	    7, 0, ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_chr_text(ports, &font, &a, 1, &origin,
	    &(struct st_rip3_chr_text_matrix){(double)INT32_MAX+1,0,0,-1}, false,
	    7, 0, ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_chr_text(ports, &font, NULL, 1, &origin, &matrix,
	    false, 7, 0, ST_RIP3_COPY) == IO_ERROR_INVALID_ARGUMENT);
	invalid = font;
	invalid.capital = INT32_MAX; invalid.descender = INT32_MIN;
	metrics = (struct st_rip3_chr_text_metrics){77,88};
	CHECK(st_rip3_chr_text_measure(&invalid, &a, 1, &metrics) == IO_ERROR_INVALID_FORMAT);
	CHECK(metrics.advance == 77 && metrics.height == 88);
	CHECK(st_rip3_draw_chr_text(ports, &font, NULL, 0, &origin, &matrix,
	    false, 7, 0, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_protect(ports, 1, true) == 0);
	CHECK(st_rip3_draw_chr_text(ports, &font, &a, 1, &origin, &matrix,
	    false, 7, 0, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_protect(ports, 1, false) == 0);
	CHECK(st_rip3_ports_activate(ports, false) == 0);
	CHECK(st_rip3_draw_chr_text(ports, &bold, (const uint8_t *)"A:", 2,
	    &origin, &matrix, false, 7, 0, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < 768; y++) {
		for (uint32_t x = 0; x < 1024; x++) {
			uint32_t expected = ST_PIXEL_COLOR_RGB_TAG;

			if (x >= 11 && x <= 20 && y >= 21 && y <= 30)
				expected = ST_PIXEL_COLOR_RGB_VALUE(0,170,170);
			if (x >= 10 && x < 22 && y >= 20 && y < 32
			    && letter_a[y - 20][x - 10] == '#')
				expected = ST_PIXEL_COLOR_RGB_VALUE(170,170,170);
			CHECK(capture->primary[(size_t)y * 1024 + x] == expected);
		}
	}
	st_pixel_buffer_destroy(capture);
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
wide_positions(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	/* One authored 4x4 rectangular CHR glyph; no renderer/encoder builds it. */
	uint8_t bytes[159] = {0};
	const uint8_t program[] = {0x80,0,0x84,0x80,0x84,0x84,0x80,0x84,0x80,0x80,0,0};
	struct st_rip3_chr font;
	memcpy(bytes,"PK\010\010TEST\032",9); put_word(bytes+9,128); memcpy(bytes+11,"TEST",4);
	put_word(bytes+15,31); bytes[17] = bytes[19] = 1;
	bytes[128] = '+'; put_word(bytes+129,1); bytes[132] = 'A'; put_word(bytes+133,19);
	bytes[136] = bytes[146] = 4; memcpy(bytes+147,program,sizeof(program));
	CHECK(st_rip3_chr_open(&font,bytes,sizeof(bytes)) == 0);
	struct st_rip3_ports *ports = NULL;
	const struct st_rip3_rect bounds = {10,20,19,28}, viewport = {1,1,8,6};
	struct st_rip3_text_position origin = {4.0*INT32_MAX+2.75,4};
	const struct st_rip3_chr_text_matrix matrix = {-(double)INT32_MAX,0,0,-1};
	const uint8_t character = 'A';
	const char *const expected[] = {"...#####","...#ssss","...#s...","...#s...","...#####","....ssss"};
	CHECK(st_rip3_ports_create(&ports,canvas) == 0 && st_rip3_ports_define(ports,1,&bounds,false) == 0
	    && st_rip3_ports_switch(ports,1) == 0 && st_rip3_ports_viewport(ports,&viewport) == 0
	    && st_rip3_ports_erase(ports,9) == 0 && st_rip3_ports_protect(ports,1,true) == 0);
	CHECK(st_rip3_draw_chr_text(ports,&font,&character,1,&origin,&matrix,true,7,3,ST_RIP3_COPY) == 0);
	CHECK(origin.x == 4.0*INT32_MAX+2.75 && origin.y == 4);
	const struct st_rip3_chr_text_matrix ordinary = {1,0,0,-1};
	origin = (struct st_rip3_text_position){1099511627776.0,1099511627776.0};
	CHECK(st_rip3_draw_chr_text(ports,&font,&character,1,&origin,&ordinary,true,7,3,ST_RIP3_COPY) == 0);
	const struct st_rip3_text_position invalid[] = {{NAN,0},{0,INFINITY},{1024.0*INT32_MAX+1,0}};
	for (size_t i = 0; i < 3; i++)
		CHECK(st_rip3_draw_chr_text(ports,&font,&character,1,invalid+i,&ordinary,false,7,3,ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	struct st_rip3_palette palette; struct st_pixel_buffer *capture = NULL;
	CHECK(st_rip3_palette_defaults(&palette) == 0 && st_rip3_ports_snapshot(ports,&palette,&capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		uint8_t index = 0;
		if (x >= 11 && x <= 18 && y >= 21 && y <= 26) {
			char value = expected[y-21][x-11]; index = value == '#' ? 7 : value == 's' ? 3 : 9;
		}
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG|palette.rgb[index]));
	}
	st_pixel_buffer_destroy(capture); st_rip3_ports_destroy(ports); return 0;
}

int
main(void)
{
	CHECK(wide_positions(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(wide_positions(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(wide_positions(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	CHECK(raster() == 0);
	CHECK(transforms() == 0);
	CHECK(zero_advance_and_shadow_clip() == 0);
	CHECK(errors_and_viewport() == 0);
	return 0;
}
