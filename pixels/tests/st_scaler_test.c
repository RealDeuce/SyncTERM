/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_pixels.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
		    __FILE__, __LINE__, #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (false)

struct scale_case {
	uint32_t width;
	uint32_t height;
	uint64_t pointy_hash;
};

static uint64_t
hash_pixels(const struct st_rgb_buffer *pixels)
{
	uint64_t hash = UINT64_C(1469598103934665603);
	size_t count = (size_t)pixels->width * pixels->height;

	for (size_t i = 0; i < count; ++i) {
		uint32_t value = pixels->pixels[i];

		for (unsigned int byte = 0; byte < 4; ++byte) {
			hash ^= value & UINT32_C(0xff);
			hash *= UINT64_C(1099511628211);
			value >>= 8;
		}
	}
	return hash;
}

static void
fill_pattern(struct st_pixel_buffer *source)
{
	for (uint32_t y = 0; y < source->height; ++y)
		for (uint32_t x = 0; x < source->width; ++x) {
			uint32_t red = (x * 47U + y * 13U) & UINT32_C(0xff);
			uint32_t green = (x * 11U + y * 61U) & UINT32_C(0xff);
			uint32_t blue = ((x ^ y) * 37U) & UINT32_C(0xff);

			if ((x + y) % 3 == 0)
				red = green = blue = 0;
			source->primary[(size_t)y * source->width + x]
			    = (red << 16) | (green << 8) | blue;
		}
}

static void
check_pointy_damage(st_pixel_scaler_t *scaler,
	st_pixel_scaler_t *reference_scaler, struct st_pixel_buffer *source,
	uint32_t width, uint32_t height)
{
	struct st_rgb_buffer input = {
		.pixels = source->primary,
		.width = source->width,
		.height = source->height,
	};
	const struct st_rgb_buffer *scaled;
	const struct st_rgb_buffer *fresh;
	struct st_pixel_rect damage = {5, 4, 4, 3};
	struct st_pixel_rect output_damage;

	fill_pattern(source);
	CHECK(st_pixel_scaler_scale(scaler, &scaled, &input, width, height,
	    ST_PIXEL_SCALING_POINTY) == 0);
	for (uint32_t y = damage.y; y < damage.y + damage.height; ++y)
		for (uint32_t x = damage.x; x < damage.x + damage.width; ++x)
			source->primary[(size_t)y * source->width + x]
			    ^= UINT32_C(0xa55a3c);
	CHECK(st_pixel_scaler_scale_region(scaler, &scaled, &input, &damage,
	    &output_damage) == 0);
	CHECK(output_damage.width < width);
	CHECK(output_damage.height < height);
	CHECK(st_pixel_scaler_scale(reference_scaler, &fresh, &input, width, height,
	    ST_PIXEL_SCALING_POINTY) == 0);
	CHECK(memcmp(scaled->pixels, fresh->pixels,
	    (size_t)width * height * sizeof(scaled->pixels[0])) == 0);
}

static void
check_small_edge_damage(void)
{
	st_pixel_scaler_t *incremental = NULL;
	st_pixel_scaler_t *reference = NULL;
	uint32_t source[16];
	struct st_rgb_buffer input = {.pixels = source};
	const struct st_rgb_buffer *updated;
	const struct st_rgb_buffer *fresh;

	CHECK(st_pixel_scaler_create(&incremental) == 0);
	CHECK(st_pixel_scaler_create(&reference) == 0);
	for (uint32_t sw = 1; sw <= 4; ++sw)
		for (uint32_t sh = 1; sh <= 4; ++sh)
			for (uint32_t multiplier = 2; multiplier <= 7; ++multiplier)
				for (unsigned policy = 0; policy < 2; ++policy) {
					uint32_t width = sw * multiplier;
					uint32_t height = sh * multiplier + 1;
					input.width = sw;
					input.height = sh;
					for (uint32_t i = 0; i < sw * sh; ++i)
						source[i] = (i * UINT32_C(0x314159)) & UINT32_C(0xffffff);
					CHECK(st_pixel_scaler_scale(incremental, &updated, &input,
					    width, height, (enum st_pixel_scaling_policy)policy) == 0);
					for (uint32_t y = 0; y < sh; ++y)
						for (uint32_t x = 0; x < sw; ++x) {
							struct st_pixel_rect damage = {x, y, 1, 1};
							struct st_pixel_rect changed;
							source[y * sw + x] ^= UINT32_C(0xa55a3c);
							CHECK(st_pixel_scaler_scale_region(incremental,
							    &updated, &input, &damage, &changed) == 0);
							CHECK(changed.x <= width && changed.width <= width - changed.x);
							CHECK(changed.y <= height && changed.height <= height - changed.y);
							CHECK(st_pixel_scaler_scale(reference, &fresh, &input,
							    width, height,
							    (enum st_pixel_scaling_policy)policy) == 0);
							CHECK(memcmp(updated->pixels, fresh->pixels,
							    (size_t)width * height * sizeof(uint32_t)) == 0);
						}
				}
	st_pixel_scaler_destroy(reference);
	st_pixel_scaler_destroy(incremental);
}

int
main(int argc, char **argv)
{
	static const struct scale_case cases[] = {
		{4, 3, UINT64_C(0x66217c64977a5424)},
		{7, 5, UINT64_C(0x422f2f3a6a36f610)},
		{13, 11, UINT64_C(0x67372fe83dd0aac3)},
		{14, 11, UINT64_C(0x4b398639f168c615)},
		{14, 10, UINT64_C(0x40aa4e53431864b4)},
		{21, 15, UINT64_C(0xae822866e7180488)},
		{28, 20, UINT64_C(0xf92510f963615375)},
		{35, 25, UINT64_C(0x10fee0d730f64090)},
		{49, 35, UINT64_C(0x02406ab0f11ffdb0)},
		{35, 15, UINT64_C(0xaa9c42e5cdb76f42)},
	};
	struct st_pixel_buffer *source = NULL;
	struct st_rgb_buffer input;
	const struct st_rgb_buffer *scaled;
	const struct st_rgb_buffer *fresh;
	st_pixel_scaler_t *scaler = NULL;
	st_pixel_scaler_t *reference_scaler = NULL;
	bool generate = argc == 2 && strcmp(argv[1], "--generate") == 0;

	check_small_edge_damage();
	CHECK(st_pixel_buffer_create(&source, 7, 5, false) == 0);
	input = (struct st_rgb_buffer){source->primary, source->width,
	    source->height};
	fill_pattern(source);
	CHECK(st_pixel_scaler_create(&scaler) == 0);
	CHECK(st_pixel_scaler_create(&reference_scaler) == 0);
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		struct st_pixel_rect damage = {2, 1, 3, 2};
		struct st_pixel_rect output_damage;
		uint64_t hash;

		CHECK(st_pixel_scaler_scale(scaler, &scaled, &input, cases[i].width,
		    cases[i].height, ST_PIXEL_SCALING_BLOCKY) == 0);
		for (uint32_t y = damage.y; y < damage.y + damage.height; ++y)
			for (uint32_t x = damage.x; x < damage.x + damage.width; ++x)
				source->primary[(size_t)y * source->width + x]
				    ^= UINT32_C(0x0055aa);
		CHECK(st_pixel_scaler_scale_region(scaler, &scaled, &input, &damage,
		    &output_damage) == 0);
		CHECK(output_damage.width != 0 && output_damage.height != 0);
		/* Restore the reference input before testing the pointy path. */
		fill_pattern(source);

		CHECK(st_pixel_scaler_scale(scaler, &scaled, &input, cases[i].width,
		    cases[i].height, ST_PIXEL_SCALING_POINTY) == 0);
		hash = hash_pixels(scaled);
		if (generate)
			printf("{%u, %u, UINT64_C(0x%016llx)},\n",
			    cases[i].width, cases[i].height,
			    (unsigned long long)hash);
		else
			CHECK(hash == cases[i].pointy_hash);
		for (uint32_t y = damage.y; y < damage.y + damage.height; ++y)
			for (uint32_t x = damage.x; x < damage.x + damage.width; ++x)
				source->primary[(size_t)y * source->width + x]
				    ^= UINT32_C(0x0055aa);
		CHECK(st_pixel_scaler_scale_region(scaler, &scaled, &input, &damage,
		    &output_damage) == 0);
		CHECK(st_pixel_scaler_scale(reference_scaler, &fresh, &input,
		    cases[i].width,
		    cases[i].height, ST_PIXEL_SCALING_POINTY) == 0);
		CHECK(memcmp(scaled->pixels, fresh->pixels,
		    (size_t)scaled->width * scaled->height
		    * sizeof(scaled->pixels[0])) == 0);
		fill_pattern(source);
	}
	st_pixel_buffer_destroy(source);
	source = NULL;
	CHECK(st_pixel_buffer_create(&source, 64, 40, false) == 0);
	input = (struct st_rgb_buffer){source->primary, source->width,
	    source->height};
	fill_pattern(source);
	CHECK(st_pixel_scaler_scale(scaler, &scaled, &input, 128, 108,
	    ST_PIXEL_SCALING_POINTY) == 0);
	{
		struct st_pixel_rect damage = {24, 12, 8, 16};
		struct st_pixel_rect output_damage;

		for (uint32_t y = damage.y; y < damage.y + damage.height; ++y)
			for (uint32_t x = damage.x; x < damage.x + damage.width; ++x)
				source->primary[(size_t)y * source->width + x]
				    ^= UINT32_C(0x55aa33);
		CHECK(st_pixel_scaler_scale_region(scaler, &scaled, &input, &damage,
		    &output_damage) == 0);
		CHECK(output_damage.width < scaled->width);
		CHECK(output_damage.height < scaled->height);
		CHECK(st_pixel_scaler_scale(reference_scaler, &fresh, &input, 128, 108,
		    ST_PIXEL_SCALING_POINTY) == 0);
		CHECK(memcmp(scaled->pixels, fresh->pixels,
		    (size_t)scaled->width * scaled->height
		    * sizeof(scaled->pixels[0])) == 0);
	}
	st_pixel_buffer_destroy(source);
	source = NULL;
	CHECK(st_pixel_buffer_create(&source, 16, 12, false) == 0);
	check_pointy_damage(scaler, reference_scaler, source, 48, 40);
	check_pointy_damage(scaler, reference_scaler, source, 64, 50);
	check_pointy_damage(scaler, reference_scaler, source, 80, 65);
	check_pointy_damage(scaler, reference_scaler, source, 96, 80);
	check_pointy_damage(scaler, reference_scaler, source, 112, 90);
	check_pointy_damage(scaler, reference_scaler, source, 128, 105);
	check_pointy_damage(scaler, reference_scaler, source, 144, 120);
	check_pointy_damage(scaler, reference_scaler, source, 192, 155);
	check_pointy_damage(scaler, reference_scaler, source, 224, 180);
	st_pixel_scaler_destroy(reference_scaler);
	st_pixel_scaler_destroy(scaler);
	st_pixel_buffer_destroy(source);
	return 0;
}
