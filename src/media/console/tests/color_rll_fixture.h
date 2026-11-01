/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#ifndef ST_COLOR_RLL_FIXTURE_H
#define ST_COLOR_RLL_FIXTURE_H

#include <stddef.h>
#include <stdint.h>

/* Test/benchmark encoder. The caller supplies width * height * 4 + 2 bytes.
 * Return zero if the right-half offset cannot be represented. */
static inline size_t
encode_color_rll(const uint8_t *rgba, size_t stride, size_t width, size_t height,
	uint8_t *encoded)
{
	size_t size = 2;

	for (size_t half = 0; half < 2; ++half) {
		size_t begin = half == 0 ? 0 : width / 2 * height;
		size_t end = half == 0 ? width / 2 * height : width * height;
		if (half == 1) {
			if (size > UINT16_MAX)
				return 0;
			encoded[0] = (uint8_t)size;
			encoded[1] = (uint8_t)(size >> 8);
		}
		for (size_t pixel = begin; pixel < end;) {
			const uint8_t *p = rgba + pixel % height * stride + pixel / height * 4;
			if (p[3] != 0) {
				encoded[size++] = p[3];
				encoded[size++] = p[0];
				encoded[size++] = p[1];
				encoded[size++] = p[2];
				++pixel;
			}
			else {
				size_t run = 0;
				do {
					++pixel;
					++run;
					if (pixel == end || run == 256)
						break;
					p = rgba + pixel % height * stride + pixel / height * 4;
				} while (p[3] == 0);
				encoded[size++] = 0;
				encoded[size++] = (uint8_t)(run - 1);
			}
		}
	}
	return size;
}

#endif
