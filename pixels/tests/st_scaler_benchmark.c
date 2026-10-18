/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_pixels.h"
#include "st_scaler_internal.h"
#include "st_time.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct benchmark_ycocg {
	int32_t y;
	int32_t co;
	int32_t cg;
};

static struct benchmark_ycocg
benchmark_rgb_to_ycocg(uint32_t pixel)
{
	int32_t red = (int32_t)((pixel >> 16) & UINT32_C(0xff));
	int32_t green = (int32_t)((pixel >> 8) & UINT32_C(0xff));
	int32_t blue = (int32_t)(pixel & UINT32_C(0xff));
	int32_t co = red - blue;
	int32_t temporary = blue + co / 2;
	int32_t cg = green - temporary;

	return (struct benchmark_ycocg){temporary + cg / 2, co, cg};
}

static uint32_t
benchmark_clamp(int32_t value)
{
	return value < 0 ? 0 : value > 255 ? 255 : (uint32_t)value;
}

static uint32_t
benchmark_blend(uint32_t left, uint32_t right, uint32_t weight)
{
	struct benchmark_ycocg first;
	struct benchmark_ycocg second;
	struct benchmark_ycocg result;
	int32_t factor;
	int32_t inverse;
	int32_t temporary;
	int32_t green;
	int32_t blue;
	int32_t red;

	if (left == right || weight == 0)
		return left;
	first = benchmark_rgb_to_ycocg(left);
	second = benchmark_rgb_to_ycocg(right);
	factor = (int32_t)weight;
	inverse = INT32_C(65535) - factor;
	result.y = (first.y * inverse + second.y * factor) / INT32_C(65535);
	result.co = (first.co * inverse + second.co * factor)
	    / INT32_C(65535);
	result.cg = (first.cg * inverse + second.cg * factor)
	    / INT32_C(65535);
	temporary = result.y - result.cg / 2;
	green = result.cg + temporary;
	blue = temporary - result.co / 2;
	red = blue + result.co;
	return (benchmark_clamp(red) << 16) | (benchmark_clamp(green) << 8)
	    | benchmark_clamp(blue);
}

static void
benchmark_multiply(const uint32_t *source, uint32_t *destination,
	uint32_t width, uint32_t height, uint32_t x_repeat, uint32_t y_repeat)
{
	uint32_t output_width = width * x_repeat;

	for (uint32_t y = 0; y < height; ++y) {
		uint32_t *row = &destination[(size_t)y * y_repeat * output_width];

		for (uint32_t x = 0; x < width; ++x)
			for (uint32_t repeat = 0; repeat < x_repeat; ++repeat)
				row[(size_t)x * x_repeat + repeat]
				    = source[(size_t)y * width + x];
		for (uint32_t repeat = 1; repeat < y_repeat; ++repeat)
			memcpy(&row[(size_t)repeat * output_width], row,
			    (size_t)output_width * sizeof(row[0]));
	}
}

static void
benchmark_interpolate_height(const uint32_t *source, uint32_t *destination,
	uint32_t width, uint32_t height, uint32_t output_height,
	uint32_t repeated_rows)
{
	uint64_t position = 0;
	uint64_t increment = ((uint64_t)height << 16) / output_height;

	for (uint32_t y = 0; y < output_height; ++y) {
		uint32_t first = (uint32_t)(position >> 16);
		uint32_t second = first + 1 < height ? first + 1 : first;
		uint32_t weight = (uint32_t)position & UINT32_C(0xffff);
		bool repeated = repeated_rows > 1
		    && first / repeated_rows == second / repeated_rows;
		const uint32_t *first_row = &source[(size_t)first * width];
		const uint32_t *second_row = &source[(size_t)second * width];
		uint32_t *output = &destination[(size_t)y * width];

		if (first == second || weight == 0 || repeated)
			memcpy(output, first_row, (size_t)width * sizeof(output[0]));
		else
			for (uint32_t x = 0; x < width; ++x)
				output[x] = benchmark_blend(first_row[x],
				    second_row[x], weight);
		position += increment;
	}
}

static uint64_t
cpu_ns(void)
{
	uint64_t result;

	if (st_time_process_cpu_ns(&result) != 0) {
		fprintf(stderr, "process CPU clock failed\n");
		exit(EXIT_FAILURE);
	}
	return result;
}

static double
seconds(uint64_t start, uint64_t finish)
{
	return (double)(finish - start) / 1000000000.0;
}

static void
run_case(st_pixel_scaler_t *scaler, const struct st_pixel_buffer *source,
	uint32_t width, uint32_t height, enum st_pixel_scaling_policy policy,
	unsigned long iterations)
{
	const struct st_rgb_buffer input = {source->primary, source->width,
	    source->height};
	const struct st_rgb_buffer *result = NULL;
	uint64_t start;
	uint64_t finish;
	uint64_t checksum = 0;
	double elapsed;

	if (st_pixel_scaler_scale(scaler, &result, &input, width, height,
	    policy) != 0) {
		fprintf(stderr, "scaler warmup failed\n");
		exit(EXIT_FAILURE);
	}
	start = cpu_ns();
	for (unsigned long i = 0; i < iterations; ++i) {
		if (st_pixel_scaler_scale(scaler, &result, &input, width, height,
		    policy) != 0) {
			fprintf(stderr, "scaler failed\n");
			exit(EXIT_FAILURE);
		}
		checksum += result->pixels[(size_t)(i % height) * width
		    + i % width];
	}
	finish = cpu_ns();
	elapsed = seconds(start, finish);
	printf("%-8s %ux%u -> %ux%u: %8.3f ms/frame, %8.1f Mpixel/s"
	    "  checksum=%llu\n",
	    policy == ST_PIXEL_SCALING_BLOCKY ? "blocky" : "pointy",
	    source->width, source->height, width, height,
	    elapsed * 1000.0 / (double)iterations,
	    (double)width * height * iterations / elapsed / 1000000.0,
	    (unsigned long long)checksum);
}

static void
legacy_full_frame(const struct st_pixel_buffer *source, uint32_t *first,
	uint32_t *second, uint32_t output_height, bool pointy)
{
	memcpy(first, source->primary,
	    (size_t)source->width * source->height * sizeof(first[0]));
	if (pointy)
		st_xbr_filter(first, second, (int)source->width,
		    (int)source->height, 2);
	else
		benchmark_multiply(first, second, source->width, source->height,
		    2, 2);
	benchmark_interpolate_height(second, first, source->width * 2U,
	    source->height * 2U, output_height, pointy ? 1 : 2);
}

static void
run_legacy_case(const struct st_pixel_buffer *source, uint32_t width,
	uint32_t height, bool pointy, unsigned long iterations)
{
	uint32_t *first = malloc((size_t)width * height * sizeof(first[0]));
	uint32_t *second = malloc((size_t)width * height * sizeof(second[0]));
	uint64_t start;
	uint64_t finish;
	uint64_t checksum = 0;
	double elapsed;

	if (first == NULL || second == NULL)
		exit(EXIT_FAILURE);
	legacy_full_frame(source, first, second, height, pointy);
	start = cpu_ns();
	for (unsigned long i = 0; i < iterations; ++i) {
		legacy_full_frame(source, first, second, height, pointy);
		checksum += first[(size_t)(i % height) * width + i % width];
	}
	finish = cpu_ns();
	elapsed = seconds(start, finish);
	printf("%-8s %ux%u -> %ux%u: %8.3f ms/frame, %8.1f Mpixel/s"
	    "  checksum=%llu\n", pointy ? "old-point" : "old-block",
	    source->width, source->height, width, height,
	    elapsed * 1000.0 / (double)iterations,
	    (double)width * height * iterations / elapsed / 1000000.0,
	    (unsigned long long)checksum);
	free(second);
	free(first);
}

static void
run_damage_case(st_pixel_scaler_t *scaler, struct st_pixel_buffer *source,
	uint32_t width, uint32_t height, enum st_pixel_scaling_policy policy,
	unsigned long iterations, const char *name, struct st_pixel_rect damage)
{
	const struct st_rgb_buffer input = {source->primary, source->width,
	    source->height};
	const struct st_rgb_buffer *result = NULL;
	struct st_pixel_rect output_damage = {0};
	uint64_t start;
	uint64_t finish;
	uint64_t checksum = 0;
	double elapsed;

	if (st_pixel_scaler_scale(scaler, &result, &input, width, height,
	    policy) != 0)
		exit(EXIT_FAILURE);
	start = cpu_ns();
	for (unsigned long i = 0; i < iterations; ++i) {
		size_t changed = (size_t)damage.y * source->width + damage.x;

		source->primary[changed] ^= UINT32_C(0x00010101);
		if (st_pixel_scaler_scale_region(scaler, &result, &input, &damage,
		    &output_damage) != 0)
			exit(EXIT_FAILURE);
		checksum += result->pixels[(size_t)output_damage.y * width
		    + output_damage.x];
	}
	finish = cpu_ns();
	elapsed = seconds(start, finish);
	printf("%-8s %-9s: %8.3f us/update, output damage %ux%u"
	    "  checksum=%llu\n",
	    policy == ST_PIXEL_SCALING_BLOCKY ? "blocky" : "pointy", name,
	    elapsed * 1000000.0 / (double)iterations, output_damage.width,
	    output_damage.height, (unsigned long long)checksum);
}

int
main(int argc, char **argv)
{
	static const uint32_t output_width = 1440;
	static const uint32_t output_height = 1080;
	struct st_pixel_buffer *source = NULL;
	st_pixel_scaler_t *scaler = NULL;
	unsigned long iterations = 20;
	char *end = NULL;

	if (argc == 2) {
		errno = 0;
		iterations = strtoul(argv[1], &end, 10);
		if (errno != 0 || end == argv[1] || *end != 0 || iterations == 0
		    || iterations > ULONG_MAX / 100) {
			fprintf(stderr, "usage: %s [iterations]\n", argv[0]);
			return EXIT_FAILURE;
		}
	}
	if (st_pixel_buffer_create(&source, 720, 400, false) != 0
	    || st_pixel_scaler_create(&scaler) != 0)
		return EXIT_FAILURE;
	for (uint32_t y = 0; y < source->height; ++y)
		for (uint32_t x = 0; x < source->width; ++x)
			source->primary[(size_t)y * source->width + x]
			    = ((x * 19U) & UINT32_C(0xff)) << 16
			    | ((y * 23U) & UINT32_C(0xff)) << 8
			    | ((x ^ y) & UINT32_C(0xff));
	run_case(scaler, source, output_width, output_height,
	    ST_PIXEL_SCALING_BLOCKY,
	    iterations);
	run_legacy_case(source, output_width, output_height, false, iterations);
	run_case(scaler, source, output_width, output_height,
	    ST_PIXEL_SCALING_POINTY,
	    iterations);
	run_legacy_case(source, output_width, output_height, true, iterations);
	run_damage_case(scaler, source, output_width, output_height,
	    ST_PIXEL_SCALING_BLOCKY, iterations * 100, "one-cell",
	    (struct st_pixel_rect){80, 80, 8, 16});
	run_damage_case(scaler, source, output_width, output_height,
	    ST_PIXEL_SCALING_BLOCKY, iterations * 100, "cursor",
	    (struct st_pixel_rect){80, 94, 8, 2});
	run_damage_case(scaler, source, output_width, output_height,
	    ST_PIXEL_SCALING_POINTY, iterations, "one-cell",
	    (struct st_pixel_rect){80, 80, 8, 16});
	st_pixel_scaler_destroy(scaler);
	st_pixel_buffer_destroy(source);
	return 0;
}
