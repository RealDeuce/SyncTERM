/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#ifndef ST_COLOR_RLL_FIXTURE_H
#define ST_COLOR_RLL_FIXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>

/* Test/benchmark encoder. The caller supplies width * height * 4 + 2 bytes.
 * Return zero if the right-half offset cannot be represented. */
static inline size_t
encode_color_rll(const uint8_t *rgba, size_t stride, size_t width, size_t height,
	uint8_t *encoded)
{
	size_t size = 2;

	for (size_t half = 0; half < 2; ++half) {
		size_t first_x = half == 0 ? 0 : width / 2;
		size_t span = half == 0 ? width / 2 : width - width / 2;
		size_t end = span * height;
		if (half == 1) {
			if (size > UINT16_MAX)
				return 0;
			encoded[0] = (uint8_t)size;
			encoded[1] = (uint8_t)(size >> 8);
		}
		for (size_t pixel = 0; pixel < end;) {
			const uint8_t *p = rgba + pixel / span * stride + (first_x + pixel % span) * 4;
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
					p = rgba + pixel / span * stride + (first_x + pixel % span) * 4;
				} while (p[3] == 0);
				encoded[size++] = 0;
				encoded[size++] = (uint8_t)(run - 1);
			}
		}
	}
	return size;
}

/* Independent reference expansion for comparisons with the streaming renderer. */
static inline bool
decode_color_rll(const uint8_t *data, size_t size, size_t width, size_t height,
	uint8_t *rgba)
{
	if (size < 2)
		return false;
	size_t offset = (size_t)data[0] | ((size_t)data[1] << 8);
	if (offset < 2 || offset > size)
		return false;
	memset(rgba, 0, width * height * 4);
	for (unsigned half = 0; half < 2; ++half) {
		size_t pos = half == 0 ? 2 : offset;
		size_t end = half == 0 ? offset : size;
		size_t first_x = half == 0 ? 0 : width / 2;
		size_t span = half == 0 ? width / 2 : width - width / 2;
		size_t pixel = 0;
		while (pos < end) {
			if (data[pos] == 0) {
				if (end - pos < 2)
					return false;
				size_t run = (size_t)data[pos + 1] + 1;
				if (run > span * height - pixel)
					return false;
				pixel += run;
				pos += 2;
			}
			else {
				if (end - pos < 4 || pixel >= span * height)
					return false;
				size_t target = (pixel / span * width + first_x + pixel % span) * 4;
				rgba[target] = data[pos + 1];
				rgba[target + 1] = data[pos + 2];
				rgba[target + 2] = data[pos + 3];
				rgba[target + 3] = data[pos];
				pos += 4;
				++pixel;
			}
		}
		if (pixel != span * height)
			return false;
	}
	return true;
}

#endif
