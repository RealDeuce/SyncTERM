/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_surface.h"
#include "io_error.h"
#include "st_pixels.h"

#include <stdint.h>
#include <stdio.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, \
		    #expression); \
		return 1; \
	} \
} while (0)

static int
expect(const struct st_rip3_surface *surface, const uint8_t *pixels,
    uint32_t width, uint32_t height)
{
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			uint8_t value = 77;

			CHECK(st_rip3_surface_get(surface, (int32_t)x, (int32_t)y,
			    &value) == 0);
			if (value != pixels[y * width + x]) {
				fprintf(stderr, "at %u,%u: got %u, expected %u\n",
				    x, y, value, pixels[y * width + x]);
				return 1;
			}
		}
	}
	return 0;
}

static int
copy_test(void)
{
	struct st_rip3_surface *src = NULL;
	struct st_rip3_surface *dst = NULL;
	struct st_rip3_rect from = {0, 0, 2, 1};
	struct st_rip3_rect to = {0, 0, 5, 3};
	struct st_rip3_rect from_clip = {1, 0, 2, 1};
	struct st_rip3_rect to_clip = {1, 1, 4, 2};
	const uint8_t doubled[] = {
		1, 1, 2, 2, 3, 3,
		1, 1, 2, 2, 3, 3,
		4, 4, 5, 5, 6, 6,
		4, 4, 5, 5, 6, 6,
	};
	/* Both clipped rectangles are rescaled to fit, leaving no blank zone. */
	const uint8_t clipped[] = {
		9, 9, 9, 9, 9, 9,
		9, 2, 2, 3, 3, 9,
		9, 5, 5, 6, 6, 9,
		9, 9, 9, 9, 9, 9,
	};
	const uint8_t clipped_unscaled[] = {
		9, 9, 9, 9, 9, 9,
		9, 1, 2, 3, 9, 9,
		9, 4, 5, 6, 9, 9,
		9, 9, 9, 9, 9, 9,
	};
	const uint8_t unchanged[] = {
		9, 9, 9, 9, 9, 9,
		9, 9, 9, 9, 9, 9,
		9, 9, 9, 9, 9, 9,
		9, 9, 9, 9, 9, 9,
	};

	CHECK(st_rip3_surface_create(&src, 3, 2) == 0);
	CHECK(st_rip3_surface_create(&dst, 6, 4) == 0);
	for (int32_t y = 0; y < 2; y++) {
		for (int32_t x = 0; x < 3; x++)
			CHECK(st_rip3_surface_plot(src, NULL, x, y,
			    (uint8_t)(y * 3 + x + 1), ST_RIP3_COPY) == 0);
	}
	CHECK(st_rip3_surface_copy(dst, NULL, &to, src, NULL, &from,
	    ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(expect(dst, doubled, 6, 4) == 0);
	{
		const struct st_rip3_rect extremes = {
			INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX,
		};

		CHECK(st_rip3_surface_fill(dst, NULL, &to, 9, ST_RIP3_COPY) == 0);
		CHECK(st_rip3_surface_copy(dst, &extremes, &extremes, src,
		    &extremes, &extremes, ST_RIP3_TRANSFER_COPY) == 0);
		CHECK(expect(dst, doubled, 6, 4) == 0);
	}
	CHECK(st_rip3_surface_fill(dst, NULL, &to, 9, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_surface_copy(dst, &to_clip, &to, src, &from_clip, &from,
	    ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(expect(dst, clipped, 6, 4) == 0);
	CHECK(st_rip3_surface_fill(dst, NULL, &to, 9, ST_RIP3_COPY) == 0);
	to = (struct st_rip3_rect){1, 1, 3, 2};
	CHECK(st_rip3_surface_copy(dst, NULL, &to, src, NULL, &from,
	    ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(expect(dst, clipped_unscaled, 6, 4) == 0);
	/* Downsample centers 3->2: first and last columns; 2->1: lower row. */
	to = (struct st_rip3_rect){0, 0, 1, 0};
	CHECK(st_rip3_surface_copy(dst, NULL, &to, src, NULL, &from,
	    ST_RIP3_TRANSFER_COPY) == 0);
	{
		uint8_t value;

		CHECK(st_rip3_surface_get(dst, 0, 0, &value) == 0 && value == 4);
		CHECK(st_rip3_surface_get(dst, 1, 0, &value) == 0 && value == 6);
	}
	to = (struct st_rip3_rect){0, 0, 5, 3};
	CHECK(st_rip3_surface_fill(dst, NULL, &to, 9, ST_RIP3_COPY) == 0);
	from = (struct st_rip3_rect){10, 10, 12, 11};
	CHECK(st_rip3_surface_copy(dst, NULL, &to, src, NULL, &from,
	    ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(expect(dst, unchanged, 6, 4) == 0);
	from = (struct st_rip3_rect){0, 0, 2, 1};
	to = (struct st_rip3_rect){10, 10, 15, 13};
	CHECK(st_rip3_surface_copy(dst, NULL, &to, src, NULL, &from,
	    ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(expect(dst, unchanged, 6, 4) == 0);
	to = (struct st_rip3_rect){0, 0, 5, 3};
	from_clip = (struct st_rip3_rect){1, 1, 0, 0};
	CHECK(st_rip3_surface_copy(dst, NULL, &to, src, &from_clip, &from,
	    ST_RIP3_TRANSFER_COPY) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_surface_copy(dst, &from_clip, &to, src, NULL, &from,
	    ST_RIP3_TRANSFER_COPY) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_surface_copy(dst, NULL, &to, src, NULL, &from_clip,
	    ST_RIP3_TRANSFER_COPY) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_surface_copy(dst, NULL, &to, src, NULL, &from,
	    (enum st_rip3_transfer_mode)5) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(expect(dst, unchanged, 6, 4) == 0);
	st_rip3_surface_destroy(src);
	st_rip3_surface_destroy(dst);
	return 0;
}

static int
transfer_test(void)
{
	struct st_rip3_surface *surface = NULL;
	struct st_rip3_rect source = {0, 0, 0, 0};
	struct st_rip3_rect target = {1, 0, 1, 0};
	const uint8_t result[][2] = {{12, 12}, {12, 6}, {12, 14}, {12, 8}, {12, 243}};

	CHECK(st_rip3_surface_create(&surface, 2, 1) == 0);
	for (unsigned mode = 0; mode < 5; mode++) {
		CHECK(st_rip3_surface_plot(surface, NULL, 0, 0, 12, ST_RIP3_COPY) == 0);
		CHECK(st_rip3_surface_plot(surface, NULL, 1, 0, 10, ST_RIP3_COPY) == 0);
		CHECK(st_rip3_surface_copy(surface, NULL, &target, surface, NULL,
		    &source, (enum st_rip3_transfer_mode)mode) == 0);
		CHECK(expect(surface, result[mode], 2, 1) == 0);
	}
	uint32_t rgb = ST_PIXEL_COLOR_RGB_TAG | UINT32_C(0x112233), value;
	const struct st_pixel_buffer pixel = {.width=1,.height=1,.primary=&rgb};
	const uint32_t inverted = ST_PIXEL_COLOR_RGB_TAG | UINT32_C(0xeeddcc);
	CHECK(st_rip3_surface_restore_area_raw(surface, &pixel, NULL, 0, 0) == 0);
	CHECK(st_rip3_surface_blit(surface, NULL, 1, 0, surface, NULL, &source,
	    ST_RIP3_TRANSFER_INVERT) == 0);
	CHECK(st_rip3_surface_read_color(surface, 1, 0, &value) == 0 && value == inverted);
	CHECK(st_rip3_surface_copy_projected(surface, &source, surface, &target,
	    ST_RIP3_TRANSFER_INVERT) == 0);
	CHECK(st_rip3_surface_read_color(surface, 0, 0, &value) == 0 && value == rgb);
	const uint32_t rgb_results[] = {0x517253, 0x517273, 0x000020};
	uint32_t background = ST_PIXEL_COLOR_RGB_TAG | UINT32_C(0x405060);
	const struct st_pixel_buffer backdrop = {.width=1,.height=1,.primary=&background};
	for (size_t i = 0; i < sizeof(rgb_results) / sizeof(rgb_results[0]); i++) {
		enum st_rip3_transfer_mode mode = (enum st_rip3_transfer_mode)(ST_RIP3_TRANSFER_XOR + i);
		CHECK(st_rip3_surface_restore_area_raw(surface, &backdrop, NULL, 1, 0) == 0);
		CHECK(st_rip3_surface_copy_projected(surface, &target, surface, &source, mode) == 0);
		CHECK(st_rip3_surface_read_color(surface, 1, 0, &value) == 0
		    && value == (ST_PIXEL_COLOR_RGB_TAG | rgb_results[i]));
	}
	const struct st_rip3_palette palette = {.rgb = {[1] = 0xa0b0c0}};
	const uint32_t mixed_results[] = {0xb192f3, 0xb1b2f3, 0x002000};
	const uint32_t indexed = 1;
	uint32_t inputs[2];
	const struct st_pixel_buffer pair = {.width=2,.height=1,.primary=inputs};
	for (unsigned direction = 0; direction < 2; direction++) {
		inputs[direction] = rgb;
		inputs[1-direction] = indexed;
		for (size_t i = 0; i < sizeof(mixed_results) / sizeof(mixed_results[0]); i++) {
			enum st_rip3_transfer_mode mode = (enum st_rip3_transfer_mode)(ST_RIP3_TRANSFER_XOR + i);
			CHECK(st_rip3_surface_restore_raw(surface, &pair) == 0);
			CHECK(st_rip3_surface_copy_projected_colors(surface, &target, surface, &source, mode, &palette) == 0);
			CHECK(st_rip3_surface_read_color(surface, 1, 0, &value) == 0
			    && value == (ST_PIXEL_COLOR_RGB_TAG | mixed_results[i]));
		}
	}
	st_rip3_surface_destroy(surface);
	return 0;
}

static int
alias_test(void)
{
	struct st_rip3_surface *surface = NULL;
	struct st_rip3_rect whole = {0, 0, 3, 3};
	const struct st_rip3_rect sources[] = {
		{0, 0, 2, 2}, {1, 1, 3, 3}, {0, 0, 1, 1}, {1, 1, 1, 1},
	};
	const struct st_rip3_rect targets[] = {
		{1, 1, 3, 3}, {0, 0, 2, 2}, {1, 1, 3, 3}, {0, 0, 3, 3},
	};
	const uint8_t expected[][16] = {
		{1,2,3,4, 5,1,2,3, 9,5,6,7, 13,9,10,11},
		{6,7,8,4, 10,11,12,8, 14,15,16,12, 13,14,15,16},
		{1,2,3,4, 5,1,2,2, 9,5,6,6, 13,5,6,6},
		{6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6},
	};
	CHECK(st_rip3_surface_create(&surface, 4, 4) == 0);
	for (size_t i = 0; i < 4; i++) {
		for (int32_t y = 0; y < 4; y++) {
			for (int32_t x = 0; x < 4; x++)
				CHECK(st_rip3_surface_plot(surface, NULL, x, y,
				    (uint8_t)(1 + x + y * 4), ST_RIP3_COPY) == 0);
		}
		CHECK(st_rip3_surface_copy(surface, &whole, &targets[i], surface,
		    &whole, &sources[i], ST_RIP3_TRANSFER_COPY) == 0);
		CHECK(expect(surface, expected[i], 4, 4) == 0);
	}
	/* Combining an overlapping copy must also use the original source. */
	for (int32_t x = 0; x < 4; x++)
		CHECK(st_rip3_surface_plot(surface, NULL, x, 0, (uint8_t)(1 << x),
		    ST_RIP3_COPY) == 0);
	{
		struct st_rip3_rect src = {0, 0, 2, 0};
		struct st_rip3_rect dst = {1, 0, 3, 0};
		const uint8_t xor_expected[] = {1, 3, 6, 12};

		CHECK(st_rip3_surface_copy(surface, NULL, &dst, surface, NULL,
		    &src, ST_RIP3_TRANSFER_XOR) == 0);
		CHECK(expect(surface, xor_expected, 4, 1) == 0);
	}
	st_rip3_surface_destroy(surface);
	return 0;
}

int
main(void)
{
	CHECK(copy_test() == 0);
	CHECK(transfer_test() == 0);
	CHECK(alias_test() == 0);
	return 0;
}
