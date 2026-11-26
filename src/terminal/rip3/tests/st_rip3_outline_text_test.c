/* Authored FastFont paths and analytic pixel/advance expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_outline_text.h"
#include "st_rip3_outline_layout.h"
#include "st_rip3_outline_font.h"
#include "st_rip3_font_data.h"
#include "st_rip3_ports.h"
#include "st_rip3_styles.h"
#include "st_pixels.h"
#include "io_error.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

extern size_t st_rip3_outline_test_allocations;
extern size_t st_rip3_outline_test_fail_at;

static void
word(uint8_t *p, unsigned value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
}

static void
dword(uint8_t *p, uint32_t value)
{
	word(p, value & 65535);
	word(p + 2, value >> 16);
}

static void
record(uint8_t *bytes, size_t offset, const uint8_t *path, size_t size)
{
	word(bytes + offset, 1);
	word(bytes + offset + 4, 65536 - 8);
	word(bytes + offset + 6, 8);
	word(bytes + offset + 10, (unsigned)size);
	if (size != 0)
		memcpy(bytes + offset + 14, path, size);
}

static void
fixture(uint8_t bytes[1900])
{
	static const uint8_t signature[] = {0x10,0,2,2,0x2e,0,0x36,0,2,4,0x98,0x44};
	/* Contours deliberately omit their closing edge. */
	static const uint8_t rectangle[] = {
		0,0,0,250,255, 1,6,0,250,255, 1,6,0,0,0, 1,0,0,0,0,
	};
	static const uint8_t counter[] = {
		0,0,0,250,255, 1,6,0,250,255, 1,6,0,0,0, 1,0,0,0,0,
		0,2,0,252,255, 1,4,0,252,255, 1,4,0,254,255, 1,2,0,254,255,
	};
	/* x(t)=8*(3t^2-2t^3), y(t)=-24*t*(1-t); a symmetric cubic arch. */
	static const uint8_t curve[] = {
		0,0,0,0,0, 2,0,0,248,255,8,0,248,255,8,0,0,0, 1,0,0,0,0,
	};
	memset(bytes, 0, 1900);
	memcpy(bytes + 16, signature, sizeof(signature));
	dword(bytes, 1800);
	word(bytes + 30, 8); word(bytes + 32, 65536 - 8);
	word(bytes + 36, 6); word(bytes + 38, 'A');
	word(bytes + 42, 3); word(bytes + 44, 1000); word(bytes + 52, 0x5426);
	bytes[57] = 100; memcpy(bytes + 66, "Solid", 6);
	bytes[102] = 0x10; bytes[103] = 50; bytes[104] = 1;
	memcpy(bytes + 112, "Hollow", 7);
	bytes[149] = 150; memcpy(bytes + 158, "Wide", 5);
	word(bytes + 192, 1); word(bytes + 194, 208);
	word(bytes + 208, 0xf31e); word(bytes + 210, 0x100);
	word(bytes + 220, 1000); word(bytes + 222, 'A'); word(bytes + 224, 6);
	word(bytes + 230, 0); word(bytes + 232, 65536 - 8);
	word(bytes + 234, 8); word(bytes + 236, 0);
	word(bytes + 246, 0x4c); word(bytes + 254, 0x20c);
	const unsigned advances[] = {6,6,8,10,3,0};
	const uint32_t offsets[] = {800-208,850-208,930-208,UINT32_MAX,1000-208,800-208};
	for (size_t i = 0; i < 6; i++) {
		word(bytes + 284 + 2 * i, advances[i]);
		dword(bytes + 732 + 4 * i, offsets[i]);
	}
	record(bytes, 800, rectangle, sizeof(rectangle));
	record(bytes, 850, counter, sizeof(counter));
	record(bytes, 930, curve, sizeof(curve));
	record(bytes, 1000, NULL, 0);
	word(bytes + 1800, 1); word(bytes + 1802, 14);
	word(bytes + 1804, 255); word(bytes + 1806, 300); word(bytes + 1808, 1);
	bytes[1810] = 'A'; bytes[1811] = 'A'; word(bytes + 1812, 65536 - 3);
}

static struct st_rip3_rff_text_style
normal(void)
{
	return (struct st_rip3_rff_text_style){.xx = 1, .yy = 1,
	    .fill_rule = ST_RIP3_OUTLINE_EVEN_ODD, .hollow_thickness = 1};
}

static bool
arch_pixel(int32_t x, int32_t y)
{
	return (y == 2 && x >= 2 && x < 6)
	    || ((y == 3 || y == 4) && x >= 1 && x < 7)
	    || (y >= 5 && y < 8 && x >= 0 && x < 8);
}

static int
measurements(void)
{
	uint8_t bytes[1900];
	struct st_rip3_rff font;
	int32_t advance = 77;
	const uint8_t missing[] = {'A',0,'E'};

	fixture(bytes);
	CHECK(st_rip3_rff_open(&font, bytes, sizeof(bytes)) == 0);
	CHECK(st_rip3_rff_text_measure(&font, NULL, 0, true, &advance) == 0 && advance == 0);
	CHECK(st_rip3_rff_text_measure(&font, (const uint8_t *)"AA", 2, false, &advance) == 0 && advance == 12);
	CHECK(st_rip3_rff_text_measure(&font, (const uint8_t *)"AA", 2, true, &advance) == 0 && advance == 9);
	CHECK(st_rip3_rff_text_measure(&font, (const uint8_t *)"ADA", 3, true, &advance) == 0 && advance == 22);
	CHECK(st_rip3_rff_text_measure(&font, missing, sizeof(missing), true, &advance) == 0 && advance == 9);
	CHECK(st_rip3_rff_text_measure(&font, (const uint8_t *)"FF", 2, true, &advance) == 0 && advance == 0);
	advance = 77;
	word(bytes + 1808, 65535);
	CHECK(st_rip3_rff_text_measure(&font, (const uint8_t *)"AA", 2, true, &advance) == IO_ERROR_INVALID_FORMAT && advance == 77);
	CHECK(st_rip3_rff_text_measure(&font, (const uint8_t *)"AA", 2, false, &advance) == 0 && advance == 12);
	CHECK(st_rip3_rff_text_measure(NULL, NULL, 0, false, &advance) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_rff_text_measure(&font, NULL, 1, false, &advance) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_rff_text_measure(&font, NULL, 0, false, NULL) == IO_ERROR_INVALID_ARGUMENT);
	uint8_t *long_text = malloc(65538);
	CHECK(long_text != NULL);
	memset(long_text, 'A', 65538);
	fixture(bytes);
	word(bytes + 284, 65535);
	advance = 77;
	CHECK(st_rip3_rff_text_measure(&font, long_text, 32768, false, &advance) == 0
	    && advance == INT32_C(2147450880));
	advance = 77;
	CHECK(st_rip3_rff_text_measure(&font, long_text, 32769, false, &advance) == IO_ERROR_OUT_OF_RANGE
	    && advance == 77);
	word(bytes + 284, 0);
	word(bytes + 1812, 32768);
	CHECK(st_rip3_rff_text_measure(&font, long_text, 65537, true, &advance) == 0
	    && advance == INT32_MIN);
	advance = 77;
	CHECK(st_rip3_rff_text_measure(&font, long_text, 65538, true, &advance) == IO_ERROR_OUT_OF_RANGE
	    && advance == 77);
	free(long_text);
	return 0;
}

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	uint8_t bytes[1900];
	struct st_rip3_rff font;
	struct st_rip3_ports *ports = NULL;
	struct st_pixel_buffer *capture = NULL;
	struct st_rip3_palette palette;
	struct st_rip3_rff_text_style style = normal();
	const struct st_rip3_point origins[] = {{2,8},{12,8},{22,8},{32,8},{42,8}};
	const uint8_t texts[] = {'A','B','C','A','A'};

	fixture(bytes);
	CHECK(st_rip3_rff_open(&font, bytes, sizeof(bytes)) == 0);
	CHECK(st_rip3_ports_create(&ports, canvas) == 0);
	CHECK(st_rip3_ports_erase(ports, 1) == 0);
	for (size_t i = 0; i < 5; i++) {
		style.face = i == 3 ? 1 : 0;
		style.shadow = i == 4 ? (struct st_rip3_point){1,1} : (struct st_rip3_point){0,0};
		CHECK(st_rip3_draw_rff_text(ports, &font, &texts[i], 1, &origins[i], &style,
		    12, 8, ST_RIP3_COPY) == 0);
	}
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	CHECK(capture->width == width && capture->height == height);
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			bool ink = x >= 2 && x < 8 && y >= 2 && y < 8;
			ink |= x >= 12 && x < 18 && y >= 2 && y < 8
			    && !(x >= 14 && x < 16 && y >= 4 && y < 6);
			/* Analytic cubic crossings at pixel centers: widths 4,6,6,8,8,8. */
			ink |= arch_pixel((int32_t)x - 22, (int32_t)y);
			ink |= x >= 32 && x <= 35 && y >= 2 && y <= 8
			    && (x == 32 || x == 35 || y == 2 || y == 8);
			ink |= x >= 42 && x < 48 && y >= 2 && y < 8;
			bool shadow = x >= 43 && x < 49 && y >= 3 && y < 9;
			uint32_t expected = ink ? 0xff5555 : shadow ? 0x555555 : 0x0000aa;
			CHECK(capture->primary[(size_t)y * width + x] == (ST_PIXEL_COLOR_RGB_TAG | expected));
		}
	}
	st_pixel_buffer_destroy(capture);
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
unchanged(struct st_rip3_ports *ports)
{
	for (int32_t y = 0; y < 16; y++) {
		for (int32_t x = 0; x < 32; x++) {
			uint8_t value;
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == 5);
		}
	}
	return 0;
}

static int
behavior(void)
{
	uint8_t bytes[1900], original[1900];
	struct st_rip3_rff font;
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_rff_text_style style = normal();
	const struct st_rip3_rect bounds = {0,0,31,15};
	const struct st_rip3_point origin = {2,10};
	uint8_t value;

	fixture(bytes);
	memcpy(original, bytes, sizeof(bytes));
	CHECK(st_rip3_rff_open(&font, bytes, sizeof(bytes)) == 0);
	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ports_protect(ports, 1, true) == 0);
	style.kerning = true;
	for (int mode = ST_RIP3_COPY; mode <= ST_RIP3_NOT; mode++) {
		CHECK(st_rip3_ports_erase(ports, 5) == 0);
		CHECK(st_rip3_draw_rff_text(ports, &font, (const uint8_t *)"AA", 2,
		    &origin, &style, 10, 8, (enum st_rip3_write_mode)mode) == 0);
		for (int32_t y = 0; y < 16; y++) {
			for (int32_t x = 0; x < 32; x++) {
				bool ink = x >= 2 && x < 11 && y >= 4 && y < 10;
				uint8_t drawn = mode == ST_RIP3_COPY ? 10 : mode == ST_RIP3_XOR ? 15
				    : mode == ST_RIP3_OR ? 15 : mode == ST_RIP3_AND ? 0 : 250;
				CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
				CHECK(value == (ink ? drawn : 5));
			}
		}
	}
	CHECK(memcmp(bytes, original, sizeof(bytes)) == 0);
	/* Two identical zero-advance aliases must not cancel each other's parity. */
	CHECK(st_rip3_ports_erase(ports, 5) == 0);
	CHECK(st_rip3_draw_rff_text(ports, &font, (const uint8_t *)"FF", 2,
	    &origin, &style, 10, 8, ST_RIP3_XOR) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 3, 5, &value) == 0 && value == 15);
	CHECK(st_rip3_ports_erase(ports, 5) == 0);
	CHECK(st_rip3_draw_rff_text(ports, &font, (const uint8_t *)"FB", 2,
	    &origin, &style, 10, 8, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 3, 5, &value) == 0 && value == 10);
	CHECK(st_rip3_ports_read(ports, 1, 4, 6, &value) == 0 && value == 10);
	for (int rule = ST_RIP3_OUTLINE_NONZERO; rule <= ST_RIP3_OUTLINE_EVEN_ODD; rule++) {
		style.fill_rule = (enum st_rip3_outline_fill_rule)rule;
		CHECK(st_rip3_ports_erase(ports, 5) == 0);
		CHECK(st_rip3_draw_rff_text(ports, &font, (const uint8_t *)"B", 1,
		    &origin, &style, 10, 8, ST_RIP3_COPY) == 0);
		for (int32_t y = 4; y < 10; y++) {
			for (int32_t x = 2; x < 8; x++) {
				bool hole = rule == ST_RIP3_OUTLINE_EVEN_ODD
				    && x >= 4 && x < 6 && y >= 6 && y < 8;
				CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == (hole ? 5 : 10));
			}
		}
	}
	/* Reverse just the inner contour: nonzero winding now leaves its counter. */
	uint8_t swap[5];
	memcpy(swap, bytes + 889, 5);
	memcpy(bytes + 889, bytes + 899, 5);
	memcpy(bytes + 899, swap, 5);
	CHECK(st_rip3_rff_open(&font, bytes, sizeof(bytes)) == 0);
	style.fill_rule = ST_RIP3_OUTLINE_NONZERO;
	CHECK(st_rip3_ports_erase(ports, 5) == 0);
	CHECK(st_rip3_draw_rff_text(ports, &font, (const uint8_t *)"B", 1,
	    &origin, &style, 10, 8, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 4, 6, &value) == 0 && value == 5);
	/* Coincident same-direction contours group crossings at exactly one x. */
	memcpy(bytes + 884, bytes + 864, 20);
	CHECK(st_rip3_rff_open(&font, bytes, sizeof(bytes)) == 0);
	for (int rule = ST_RIP3_OUTLINE_NONZERO; rule <= ST_RIP3_OUTLINE_EVEN_ODD; rule++) {
		style.fill_rule = (enum st_rip3_outline_fill_rule)rule;
		CHECK(st_rip3_ports_erase(ports, 5) == 0);
		CHECK(st_rip3_draw_rff_text(ports, &font, (const uint8_t *)"B", 1,
		    &origin, &style, 10, 8, ST_RIP3_COPY) == 0);
		CHECK(st_rip3_ports_read(ports, 1, 4, 6, &value) == 0);
		CHECK(value == (rule == ST_RIP3_OUTLINE_NONZERO ? 10 : 5));
		if (rule == ST_RIP3_OUTLINE_EVEN_ODD)
			CHECK(unchanged(ports) == 0);
	}
	fixture(bytes);
	CHECK(st_rip3_rff_open(&font, bytes, sizeof(bytes)) == 0);
	/* Scale raw cumulative positions once: 2 * 6 * 150% * 0.5 = 9 pixels. */
	style = normal(); style.face = 2; style.xx = style.yy = 0.5;
	CHECK(st_rip3_ports_erase(ports, 5) == 0);
	CHECK(st_rip3_draw_rff_text(ports, &font, (const uint8_t *)"AA", 2,
	    &origin, &style, 10, 8, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 16; y++) {
		for (int32_t x = 0; x < 32; x++) {
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (x >= 2 && x < 11 && y >= 7 && y < 10 ? 10 : 5));
		}
	}
	/* Rotate the analytic arch: inverse sample coordinates are u=11-y,
	 * original row=x-4. This tests subpixel vertices after orientation. */
	const struct st_rip3_point rotated = {12,12};
	style = normal(); style.xx = style.yy = 0; style.xy = 1; style.yx = -1;
	CHECK(st_rip3_ports_erase(ports, 5) == 0);
	CHECK(st_rip3_draw_rff_text(ports, &font, (const uint8_t *)"C", 1,
	    &rotated, &style, 10, 8, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 16; y++) {
		for (int32_t x = 0; x < 32; x++) {
			bool ink = arch_pixel(11 - y, x - 4);
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == (ink ? 10 : 5));
		}
	}
	/* Affine shear has independently calculated row spans, six pixels wide. */
	const struct st_rip3_point shear_origin = {10,10};
	const int left[] = {7,8,8,9,9,10};
	style = normal(); style.xy = 0.5;
	CHECK(st_rip3_ports_erase(ports, 5) == 0);
	CHECK(st_rip3_draw_rff_text(ports, &font, (const uint8_t *)"A", 1,
	    &shear_origin, &style, 10, 8, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 16; y++) {
		for (int32_t x = 0; x < 32; x++) {
			bool ink = y >= 4 && y < 10 && x >= left[y - 4] && x < left[y - 4] + 6;
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == (ink ? 10 : 5));
		}
	}
	/* Clipped-away foreground must still cast a shadow into the viewport. */
	const struct st_rip3_point clipped = {-6,0}, hollow_clipped = {-4,-1};
	style = normal(); style.shadow = (struct st_rip3_point){1,1};
	CHECK(st_rip3_ports_erase(ports, 5) == 0);
	CHECK(st_rip3_draw_rff_text(ports, &font, (const uint8_t *)"A", 1,
	    &clipped, &style, 10, 8, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 0, 0, &value) == 0 && value == 8);
	style.face = 1;
	CHECK(st_rip3_ports_erase(ports, 5) == 0);
	CHECK(st_rip3_draw_rff_text(ports, &font, (const uint8_t *)"A", 1,
	    &hollow_clipped, &style, 10, 8, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 0, 0, &value) == 0 && value == 8);
	/* Explicit hollow width, independent of face scaling and the line style. */
	style.shadow = (struct st_rip3_point){0,0}; style.hollow_thickness = 3;
	CHECK(st_rip3_ports_erase(ports, 5) == 0);
	CHECK(st_rip3_draw_rff_text(ports, &font, (const uint8_t *)"A", 1,
	    &shear_origin, &style, 10, 8, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 9, 7, &value) == 0 && value == 10);
	CHECK(st_rip3_ports_erase(ports, 5) == 0);
	style.hollow_thickness = 0;
	CHECK(st_rip3_draw_rff_text(ports, &font, (const uint8_t *)"A", 1,
	    &origin, &style, 10, 8, ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	style = normal(); style.xx = NAN;
	CHECK(st_rip3_draw_rff_text(ports, &font, (const uint8_t *)"A", 1,
	    &origin, &style, 10, 8, ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	style.xx = 1e9;
	CHECK(st_rip3_draw_rff_text(ports, &font, (const uint8_t *)"AC", 2,
	    &origin, &style, 10, 8, ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	style = normal(); style.face = 3;
	CHECK(st_rip3_draw_rff_text(ports, &font, (const uint8_t *)"A", 1,
	    &origin, &style, 10, 8, ST_RIP3_COPY) == IO_ERROR_NOT_FOUND);
	style = normal(); style.fill_rule = (enum st_rip3_outline_fill_rule)99;
	CHECK(st_rip3_draw_rff_text(ports, &font, (const uint8_t *)"A", 1,
	    &origin, &style, 10, 8, ST_RIP3_COPY) == IO_ERROR_INVALID_ARGUMENT);
	style = normal(); style.kerning = true;
	word(bytes + 1808, 65535);
	CHECK(st_rip3_draw_rff_text(ports, &font, (const uint8_t *)"AA", 2,
	    &origin, &style, 10, 8, ST_RIP3_COPY) == IO_ERROR_INVALID_FORMAT);
	CHECK(unchanged(ports) == 0);
	fixture(bytes);
	CHECK(st_rip3_ports_protect(ports, 1, false) == 0);
	CHECK(st_rip3_ports_activate(ports, false) == 0);
	CHECK(st_rip3_draw_rff_text(ports, &font, (const uint8_t *)"A", 1,
	    &origin, &style, 10, 8, ST_RIP3_COPY) == 0);
	CHECK(unchanged(ports) == 0);
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
builtin_faces(void)
{
	struct st_rip3_ports *ports = NULL;
	const struct st_rip3_rect bounds = {0,0,127,47};
	const struct st_rip3_point origin = {2,30};
	const uint8_t text[] = {'A','g',0x82,0xff,0};
	unsigned faces = 0;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	for (size_t i = 0; i < st_rip3_font_asset_count(); i++) {
		const struct st_rip3_font_asset *asset = st_rip3_font_asset_at(i);
		const char *extension = strrchr(asset->filename, '.');
		struct st_rip3_rff font;

		if (extension == NULL || strcmp(extension, ".RFF") != 0)
			continue;
		CHECK(st_rip3_rff_open(&font, asset->data, asset->size) == 0);
		for (size_t f = 0; f < font.styles; f++) {
			struct st_rip3_rff_text_style style = normal();
			bool ink = false;
			int32_t advance;

			style.face = f; style.xx = style.yy = 24.0 / font.units_per_em;
			style.kerning = true;
			CHECK(st_rip3_rff_text_measure(&font, text, sizeof(text), true, &advance) == 0);
			CHECK(advance > 0);
			CHECK(st_rip3_ports_erase(ports, 0) == 0);
			CHECK(st_rip3_draw_rff_text(ports, &font, text, sizeof(text), &origin, &style,
			    14, 8, ST_RIP3_COPY) == 0);
			for (int32_t y = 0; y < 48; y++) {
				for (int32_t x = 0; x < 128; x++) {
					uint8_t pixel;
					CHECK(st_rip3_ports_read(ports, 1, x, y, &pixel) == 0);
					ink |= pixel == 14;
				}
			}
			CHECK(ink);
			faces++;
		}
	}
	CHECK(faces == 80);
	st_rip3_ports_destroy(ports);
	return 0;
}

/* Device rectangles after rotating the authored 6x6 glyph with its 150%
 * horizontal face. These bounds are independent of the string direction. */
static const struct st_rip3_rect turned_bounds[] = {
	{0,-6,8,-1}, {-6,-9,-1,-1}, {-9,0,-1,5}, {0,0,5,8},
};

static bool
inside(int32_t x, int32_t y, const struct st_rip3_rect *bounds)
{
	return x >= bounds->x0 && x <= bounds->x1 && y >= bounds->y0 && y <= bounds->y1;
}

static int
positioned_scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, bool anisotropic)
{
	/* Global bbox 8x8, 150% horizontal face: realized widths are 12 for
	 * upright/half and 8 for quarter turns. P125 adds 3/2 to advance 9.
	 * Double X DPI gives widths 24/16 and adds 6/4, but vertical strings retain
	 * the Y-axis advance 9. Positions remain DEVICE.
	 * Character columns alternate the sign of the stored B=4 displacement. */
	static const struct st_rip3_point direction[] = {{1,0},{0,-1},{-1,0},{0,1}};
	static const struct st_rip3_point baseline[][4] = {
		{{0,4},{-4,0},{0,0},{0,0}},
		{{4,0},{-4,0},{0,0},{0,0}},
		{{0,0},{-4,0},{0,-4},{0,0}},
		{{0,4},{0,0},{0,0},{0,0}},
	};
	uint8_t bytes[1900];
	struct st_rip3_rff font;
	struct st_rip3_frame frame;
	struct st_rip3_ports *ports = NULL;
	struct st_pixel_buffer *capture = NULL;
	struct st_rip3_palette palette;

	fixture(bytes);
	CHECK(st_rip3_rff_open(&font,bytes,sizeof(bytes)) == 0);
	unsigned scale_x = anisotropic ? 2 : 1;
	CHECK(st_rip3_frame_init(&frame,canvas) == 0 && st_rip3_frame_world(&frame,width/scale_x,height) == 0);
	CHECK(st_rip3_ports_create(&ports,canvas) == 0);
	CHECK(st_rip3_ports_erase(ports,1) == 0);
	for (unsigned string = 0; string < 4; string++) {
		for (unsigned character = 0; character < 4; character++) {
			const struct st_rip3_outline_style wire = {.size=1000,.spacing=125,.baseline=4,
			    .flags=0x400|(character%2!=0 ? 0x100 : 0),
			    .string_rotation=900*string,.character_rotation=900*character};
			struct st_rip3_outline_layout layout;
			struct st_rip3_outline_font realized;
			struct st_rip3_text_position positions[2], zero = {48+96*character,48+96*string};
			int32_t advances[2];
			int64_t total;
			int32_t bbox_width;
			CHECK(st_rip3_outline_layout_prepare(&layout,&wire,&frame) == 0);
			CHECK(st_rip3_outline_font_bind(&realized,&font,2,&frame,wire.size,wire.flags,layout.font_orientation) == 0);
			CHECK(st_rip3_outline_font_bbox_width(&bbox_width,&realized) == 0);
			CHECK(st_rip3_outline_font_advances(advances,&realized,wire.string_rotation,(const uint8_t *)"AA",2) == 0);
			CHECK(advances[0] == (anisotropic && string%2==0 ? 18 : 9) && advances[1] == advances[0]);
			CHECK(st_rip3_outline_layout_positions(&layout,&zero,advances,2,bbox_width,positions,&total) == 0);
			CHECK(total == 2*(anisotropic ? (string%2==0 ? 18 : 9)+(character%2!=0 ? 4 : 6)
			    : (character%2!=0 ? 11 : 12)));
			CHECK(st_rip3_draw_outline_positioned(ports,&realized,&layout,(const uint8_t *)"AA",2,
			    positions,12,8,ST_RIP3_COPY) == 0);
		}
	}
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_snapshot(ports,&palette,&capture) == 0);
	CHECK(capture->width == width && capture->height == height);
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			bool ink = false;
			if (x < 384 && y < 384) {
				unsigned string = y/96, character = x/96;
				int32_t u = (int32_t)(x%96)-48-baseline[string][character].x;
				int32_t v = (int32_t)(y%96)-48-baseline[string][character].y;
				struct st_rip3_rect bounds = turned_bounds[character];
				bounds.x0 *= (int32_t)scale_x; bounds.x1 = (bounds.x1+1)*(int32_t)scale_x-1;
				int32_t step = anisotropic ? (string%2==0 ? 18 : 9)+(character%2!=0 ? 4 : 6)
				    : (character%2!=0 ? 11 : 12);
				ink = inside(u,v,&bounds) || inside(u-step*direction[string].x,v-step*direction[string].y,&bounds);
			}
			CHECK(capture->primary[(size_t)y*width+x]
			    == (ST_PIXEL_COLOR_RGB_TAG | (ink ? 0xff5555 : 0x0000aa)));
		}
	}
	st_pixel_buffer_destroy(capture);
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
positioned_shadows(void)
{
	static const struct st_rip3_point shadows[] = {{-11,-11},{11,-11},{-11,11},{11,11}};
	uint8_t bytes[1900], value;
	struct st_rip3_rff font;
	struct st_rip3_ports *ports = NULL;
	const struct st_rip3_rect bounds = {0,0,63,63};
	const struct st_rip3_text_position position = {32,32};
	struct st_rip3_frame frame;

	fixture(bytes);
	CHECK(st_rip3_rff_open(&font,bytes,sizeof(bytes)) == 0);
	CHECK(st_rip3_frame_init(&frame,ST_RIP3_CANVAS_640_480) == 0
	    && st_rip3_frame_world(&frame,640,480) == 0);
	CHECK(st_rip3_ports_create(&ports,ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports,1,&bounds,true) == 0);
	CHECK(st_rip3_ports_switch(ports,1) == 0);
	for (unsigned character = 0; character < 4; character++) {
		for (unsigned direction = 0; direction < 4; direction++) {
			const struct st_rip3_outline_style wire = {.size=1000,.spacing=100,.shadow=11,
			    .flags=direction<<6,.character_rotation=900*character};
			struct st_rip3_outline_layout layout;
			struct st_rip3_outline_font realized;
			CHECK(st_rip3_outline_layout_prepare(&layout,&wire,&frame) == 0);
			CHECK(st_rip3_outline_font_bind(&realized,&font,2,&frame,wire.size,wire.flags,layout.font_orientation) == 0);
			for (int mode = ST_RIP3_COPY; mode <= ST_RIP3_NOT; mode++) {
				CHECK(st_rip3_ports_erase(ports,5) == 0);
				CHECK(st_rip3_draw_outline_positioned(ports,&realized,&layout,(const uint8_t *)"A",1,
				    &position,10,8,(enum st_rip3_write_mode)mode) == 0);
				for (int32_t y = 0; y < 64; y++) {
					for (int32_t x = 0; x < 64; x++) {
						bool ink = inside(x-32,y-32,&turned_bounds[character]);
						bool shade = inside(x-32-shadows[direction].x,y-32-shadows[direction].y,
						    &turned_bounds[character]);
						unsigned source = ink ? 10 : 8;
						unsigned expected = mode == ST_RIP3_COPY ? source
						    : mode == ST_RIP3_XOR ? 5^source : mode == ST_RIP3_OR ? 5|source
						    : mode == ST_RIP3_AND ? 5&source : 250;
						CHECK(st_rip3_ports_read(ports,1,x,y,&value) == 0);
						CHECK(value == (ink || shade ? expected : 5));
					}
				}
			}
		}
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
positioned_behavior(void)
{
	uint8_t bytes[1900], value;
	struct st_rip3_rff font;
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_rff_text_style style = normal();
	struct st_rip3_text_position positions[] = {{2.25,10.25},{12.25,10.25}};
	const struct st_rip3_text_position saved[] = {{2.25,10.25},{12.25,10.25}};
	const struct st_rip3_rect bounds = {0,0,31,15};
	const struct st_rip3_cursor cursor = {103,-17};
	struct st_rip3_port_info info;

	fixture(bytes);
	/* Positioned text uses neither raw advances nor pair kerning. */
	word(bytes+284,65535); word(bytes+1808,65535);
	CHECK(st_rip3_rff_open(&font,bytes,sizeof(bytes)) == 0);
	CHECK(st_rip3_ports_create(&ports,ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports,1,&bounds,true) == 0);
	CHECK(st_rip3_ports_switch(ports,1) == 0);
	CHECK(st_rip3_ports_cursor(ports,1,&cursor) == 0);
	style.face = 2; style.kerning = true;
	CHECK(st_rip3_ports_erase(ports,5) == 0);
	CHECK(st_rip3_draw_rff_positioned(ports,&font,(const uint8_t *)"AA",2,
	    positions,&style,10,8,ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 16; y++) {
		for (int32_t x = 0; x < 32; x++) {
			bool ink = y >= 4 && y < 10 && ((x >= 2 && x < 11) || (x >= 12 && x < 21));
			CHECK(st_rip3_ports_read(ports,1,x,y,&value) == 0 && value == (ink ? 10 : 5));
		}
	}
	CHECK(memcmp(positions,saved,sizeof(positions)) == 0);
	CHECK(st_rip3_ports_info(ports,1,&info) == 0 && info.cursor.x == 103 && info.cursor.y == -17);
	const uint8_t sparse[] = {'A',0,'D','E'};
	const struct st_rip3_text_position sparse_positions[] = {{2,10},{12,10},{18,10},{24,10}};
	CHECK(st_rip3_ports_erase(ports,5) == 0);
	CHECK(st_rip3_draw_rff_positioned(ports,&font,sparse,sizeof(sparse),
	    sparse_positions,&style,10,8,ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 16; y++) {
		for (int32_t x = 0; x < 32; x++) {
			bool ink = x >= 2 && x < 11 && y >= 4 && y < 10;
			CHECK(st_rip3_ports_read(ports,1,x,y,&value) == 0 && value == (ink ? 10 : 5));
		}
	}
	CHECK(st_rip3_ports_erase(ports,5) == 0);
	CHECK(st_rip3_draw_rff_positioned(ports,&font,NULL,0,NULL,&style,10,8,ST_RIP3_COPY) == 0);
	CHECK(st_rip3_draw_rff_positioned(ports,&font,(const uint8_t *)"AA",2,
	    NULL,&style,10,8,ST_RIP3_COPY) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_draw_rff_positioned(ports,&font,NULL,2,
	    positions,&style,10,8,ST_RIP3_COPY) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_draw_rff_positioned(ports,&font,(const uint8_t *)"AA",SIZE_MAX,
	    positions,&style,10,8,ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	positions[1].x = NAN;
	/* Even an empty/missing glyph must not conceal an invalid placement. */
	CHECK(st_rip3_draw_rff_positioned(ports,&font,(const uint8_t *)"AE",2,
	    positions,&style,10,8,ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	positions[1].x = 4.0*INT32_MAX;
	CHECK(st_rip3_draw_rff_positioned(ports,&font,(const uint8_t *)"AA",2,
	    positions,&style,10,8,ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	positions[1].x = 3.5*INT32_MAX;
	style.shadow = (struct st_rip3_point){INT32_MAX,0};
	CHECK(st_rip3_draw_rff_positioned(ports,&font,(const uint8_t *)"AA",2,
	    positions,&style,10,8,ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	CHECK(unchanged(ports) == 0);
	positions[0] = (struct st_rip3_text_position){2,10};
	positions[1] = (struct st_rip3_text_position){5,10};
	style.shadow = (struct st_rip3_point){-2,3};
	st_rip3_outline_test_allocations = 0;
	CHECK(st_rip3_draw_rff_positioned(ports,&font,(const uint8_t *)"AA",2,
	    positions,&style,10,8,ST_RIP3_XOR) == 0);
	size_t allocations = st_rip3_outline_test_allocations;
	CHECK(allocations >= 7);
	for (int32_t y = 0; y < 16; y++) {
		for (int32_t x = 0; x < 32; x++) {
			bool ink = x >= 2 && x < 14 && y >= 4 && y < 10;
			bool shade = x >= 0 && x < 12 && y >= 7 && y < 13;
			CHECK(st_rip3_ports_read(ports,1,x,y,&value) == 0);
			CHECK(value == (ink ? 15 : shade ? 13 : 5));
		}
	}
	for (size_t i = 0; i < allocations; i++) {
		CHECK(st_rip3_ports_erase(ports,5) == 0);
		st_rip3_outline_test_allocations = 0;
		st_rip3_outline_test_fail_at = i;
		CHECK(st_rip3_draw_rff_positioned(ports,&font,(const uint8_t *)"AA",2,
		    positions,&style,10,8,ST_RIP3_XOR) == IO_ERROR_OUT_OF_MEMORY);
		st_rip3_outline_test_fail_at = SIZE_MAX;
		CHECK(unchanged(ports) == 0);
	}
	CHECK(st_rip3_ports_activate(ports,false) == 0);
	st_rip3_outline_test_allocations = 0;
	CHECK(st_rip3_draw_rff_positioned(ports,&font,(const uint8_t *)"AA",2,
	    positions,&style,10,8,ST_RIP3_COPY) == 0);
	CHECK(st_rip3_outline_test_allocations == 0);
	CHECK(unchanged(ports) == 0);
	CHECK(st_rip3_ports_activate(ports,true) == 0);
	const struct st_rip3_rect viewport = {4,3,27,14};
	CHECK(st_rip3_ports_viewport_device(ports,&viewport) == 0);
	/* Shadow survives when its foreground is entirely outside the viewport.
	 * Both positions and displacements stay viewport-relative device values. */
	positions[0] = (struct st_rip3_text_position){-9,6};
	style.shadow = (struct st_rip3_point){11,0};
	CHECK(st_rip3_draw_rff_positioned(ports,&font,(const uint8_t *)"A",1,
	    positions,&style,10,8,ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 16; y++) {
		for (int32_t x = 0; x < 32; x++) {
			bool shade = x >= 6 && x < 15 && y >= 3 && y < 9;
			CHECK(st_rip3_ports_read(ports,1,x,y,&value) == 0 && value == (shade ? 8 : 5));
		}
	}
	CHECK(st_rip3_ports_erase(ports,5) == 0);
	positions[0] = (struct st_rip3_text_position){-4,-1};
	style.face = 1; style.shadow = (struct st_rip3_point){3,2};
	CHECK(st_rip3_draw_rff_positioned(ports,&font,(const uint8_t *)"A",1,
	    positions,&style,10,8,ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 16; y++) {
		for (int32_t x = 0; x < 32; x++) {
			bool shade = (x == 6 && y == 3) || (x >= 4 && x <= 6 && y == 4);
			CHECK(st_rip3_ports_read(ports,1,x,y,&value) == 0 && value == (shade ? 8 : 5));
		}
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
realized_behavior(void)
{
	uint8_t bytes[1900], value;
	struct st_rip3_rff file;
	struct st_rip3_outline_font font, saved;
	struct st_rip3_outline_style wire = {.size=1000,.spacing=100};
	struct st_rip3_outline_layout layout;
	struct st_rip3_frame frame;
	struct st_rip3_ports *ports = NULL;
	const struct st_rip3_rect bounds = {0,0,31,15};
	const struct st_rip3_text_position position = {2,10}, hollow_position = {10,10};
	const struct st_rip3_cursor cursor = {-5,103};
	struct st_rip3_port_info info;

	fixture(bytes);
	CHECK(st_rip3_rff_open(&file,bytes,sizeof(bytes)) == 0);
	CHECK(st_rip3_frame_init(&frame,ST_RIP3_CANVAS_640_480) == 0
	    && st_rip3_frame_world(&frame,640,480) == 0);
	CHECK(st_rip3_outline_layout_prepare(&layout,&wire,&frame) == 0);
	CHECK(st_rip3_outline_font_bind(&font,&file,0,&frame,wire.size,wire.flags,layout.font_orientation) == 0);
	CHECK(st_rip3_ports_create(&ports,ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports,1,&bounds,true) == 0 && st_rip3_ports_switch(ports,1) == 0);
	CHECK(st_rip3_ports_cursor(ports,1,&cursor) == 0);
	CHECK(st_rip3_ports_erase(ports,5) == 0);
	/* The authored inner contour has the same winding as its outer contour.
	 * The realized-font path must select nonzero, independently of the raw
	 * test kernel's optional even/odd fill rule. */
	CHECK(st_rip3_draw_outline_positioned(ports,&font,&layout,(const uint8_t *)"B",1,
	    &position,10,8,ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_read(ports,1,4,6,&value) == 0 && value == 10);
	CHECK(st_rip3_ports_erase(ports,5) == 0);
	memcpy(&saved,&font,sizeof(saved));
	for (unsigned which = 0; which < 8; which++) {
		font = saved;
		switch (which) {
			case 0:font.scale_x=NAN;break; case 1:font.scale_y=-1;break;
			case 2:font.orientation=3;break; case 3:font.points=0;break;
			case 4:font.dpi_x=0;break; case 5:font.dpi_y=0;break;
			case 6:font.flags=0x1000000;break; default:font.scale_x=0;break;
		}
		CHECK(st_rip3_draw_outline_positioned(ports,&font,&layout,(const uint8_t *)"A",1,
		    &position,10,8,ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	}
	font = saved;
	layout.character_turns=4;
	CHECK(st_rip3_draw_outline_positioned(ports,&font,&layout,(const uint8_t *)"A",1,
	    &position,10,8,ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	layout.character_turns=0; layout.font_orientation=1;
	CHECK(st_rip3_draw_outline_positioned(ports,&font,&layout,(const uint8_t *)"A",1,
	    &position,10,8,ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	layout.font_orientation=0;
	CHECK(st_rip3_draw_outline_positioned(ports,NULL,&layout,(const uint8_t *)"A",1,
	    &position,10,8,ST_RIP3_COPY) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_draw_outline_positioned(ports,&font,NULL,(const uint8_t *)"A",1,
	    &position,10,8,ST_RIP3_COPY) == IO_ERROR_INVALID_ARGUMENT);
	/* Every combined ordinary effect request is accepted by the production
	 * bridge. Authored command scenes independently check its ink profile. */
	for (unsigned flags = 1; flags < 16; flags++) {
		CHECK(st_rip3_outline_font_bind(&font,&file,0,&frame,1000,flags,0) == 0);
		st_rip3_outline_test_allocations=0;
		CHECK(st_rip3_draw_outline_positioned(ports,&font,&layout,(const uint8_t *)"A",1,
		    &position,10,8,ST_RIP3_COPY) == 0);
		CHECK(st_rip3_outline_test_allocations != 0 && st_rip3_ports_erase(ports,5)==0);
	}
	CHECK(unchanged(ports) == 0);
	font = saved;
	st_rip3_outline_test_allocations=0; st_rip3_outline_test_fail_at=0;
	CHECK(st_rip3_draw_outline_positioned(ports,&font,&layout,(const uint8_t *)"A",1,
	    &position,10,8,ST_RIP3_COPY) == IO_ERROR_OUT_OF_MEMORY);
	st_rip3_outline_test_fail_at=SIZE_MAX;
	CHECK(unchanged(ports) == 0);
	/* Custom zero strength bytes give ordinary hollow width 1; bold's minimum
	 * produces width 2. At this glyph's left edge only the latter reaches x11. */
	for (unsigned bold = 0; bold < 2; bold++) {
		CHECK(st_rip3_ports_erase(ports,5) == 0);
		CHECK(st_rip3_outline_font_bind(&font,&file,1,&frame,1000,bold,0) == 0);
		CHECK(font.hollow_thickness == bold+1);
		CHECK(st_rip3_draw_outline_positioned(ports,&font,&layout,(const uint8_t *)"A",1,
		    &hollow_position,10,8,ST_RIP3_COPY) == 0);
		CHECK(st_rip3_ports_read(ports,1,10,7,&value) == 0 && value == 10);
		CHECK(st_rip3_ports_read(ports,1,11,7,&value) == 0 && value == (bold ? 10 : 5));
		CHECK(st_rip3_ports_read(ports,1,12,7,&value) == 0 && value == 5);
	}
	/* All allocations for combined effected ink, bands, their union and
	 * displaced shadows complete before any paint. Exercise each boundary. */
	word(bytes+44,16);word(bytes+220,16);CHECK(st_rip3_rff_open(&file,bytes,sizeof(bytes))==0);
	CHECK(st_rip3_outline_font_bind(&font,&file,0,&frame,16,15,0)==0);
	layout.shadow=(struct st_rip3_point){2,1};
	CHECK(st_rip3_ports_erase(ports,5)==0);st_rip3_outline_test_allocations=0;
	CHECK(st_rip3_draw_outline_positioned(ports,&font,&layout,(const uint8_t *)"A",1,&position,10,8,ST_RIP3_COPY)==0);
	size_t allocations=st_rip3_outline_test_allocations;CHECK(allocations>8);
	CHECK(st_rip3_ports_erase(ports,5)==0);
	for (size_t fail=0; fail<allocations; fail++) {
		st_rip3_outline_test_allocations=0;st_rip3_outline_test_fail_at=fail;
		CHECK(st_rip3_draw_outline_positioned(ports,&font,&layout,(const uint8_t *)"A",1,&position,10,8,ST_RIP3_COPY)==IO_ERROR_OUT_OF_MEMORY);
		CHECK(unchanged(ports)==0);
	}
	st_rip3_outline_test_fail_at=SIZE_MAX;
	/* Hollow/italic/bold blank glyph: only solid decoration bands and their
	 * shadows, with no invented glyph ink or extra bold band thickness. */
	CHECK(st_rip3_outline_font_bind(&font,&file,1,&frame,16,15,0)==0
	    && st_rip3_draw_outline_positioned(ports,&font,&layout,(const uint8_t *)"E",1,&hollow_position,10,8,ST_RIP3_XOR)==0);
	for (int y=0; y<16; y++) for (int x=0; x<32; x++) {
		bool ink=(x==10 && y==11)||(x==11 && y==5),shade=(x==12 && y==12)||(x==13 && y==6);
		CHECK(st_rip3_ports_read(ports,1,x,y,&value)==0 && value==(ink ? 15 : shade ? 13 : 5));
	}
	CHECK(st_rip3_ports_erase(ports,5)==0 && st_rip3_draw_outline_positioned(ports,&font,&layout,
	    (const uint8_t *)"@",1,&hollow_position,10,8,ST_RIP3_COPY)==0 && unchanged(ports)==0);
	CHECK(st_rip3_draw_outline_positioned(ports,&font,&layout,(const uint8_t *)"D",1,&hollow_position,10,8,ST_RIP3_COPY)==0
	    && st_rip3_ports_read(ports,1,11,11,&value)==0 && value==10);
	CHECK(st_rip3_ports_info(ports,1,&info) == 0 && info.cursor.x == -5 && info.cursor.y == 103);
	st_rip3_ports_destroy(ports);
	return 0;
}

int
main(void)
{
	CHECK(measurements() == 0);
	CHECK(behavior() == 0);
	CHECK(builtin_faces() == 0);
	CHECK(scene(ST_RIP3_CANVAS_640_480, 640, 480) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600, 800, 600) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768, 1024, 768) == 0);
	CHECK(positioned_behavior() == 0);
	CHECK(realized_behavior() == 0);
	CHECK(positioned_shadows() == 0);
	CHECK(positioned_scene(ST_RIP3_CANVAS_640_480,640,480,false) == 0);
	CHECK(positioned_scene(ST_RIP3_CANVAS_800_600,800,600,false) == 0);
	CHECK(positioned_scene(ST_RIP3_CANVAS_1024_768,1024,768,false) == 0);
	CHECK(positioned_scene(ST_RIP3_CANVAS_640_480,640,480,true) == 0);
	CHECK(positioned_scene(ST_RIP3_CANVAS_800_600,800,600,true) == 0);
	CHECK(positioned_scene(ST_RIP3_CANVAS_1024_768,1024,768,true) == 0);
	return 0;
}
