/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_CONSOLE_POINTER_PIXELS_H
#define ST_CONSOLE_POINTER_PIXELS_H

#include <stdint.h>
#include <string.h>

#include "st_console.h"

/* Monochrome 16x16 cursors for backends without the corresponding stock shape.
 * Black/white are opaque ARGB; zero is transparent. Existing console shapes
 * retain their artwork when shared with the native cursor adapters. */
static inline void
st_console_pointer_pixels(uint32_t *pixels, enum st_console_pointer_shape shape)
{
	memset(pixels, 0, 16 * 16 * sizeof(*pixels));
	if (shape == ST_CONSOLE_POINTER_DEFAULT) {
		for (uint32_t y = 0; y < 14; ++y)
			for (uint32_t x = 0; x <= y / 2; ++x)
				pixels[y * 16 + x] =
				    x == 0 || x == y / 2 ? UINT32_C(0xff000000) : UINT32_C(0xffffffff);
	}
	else if (shape == ST_CONSOLE_POINTER_TEXT) {
		for (uint32_t y = 1; y < 15; ++y)
			pixels[y * 16 + 7] = UINT32_C(0xff000000);
		for (uint32_t x = 4; x < 11; ++x) {
			pixels[1 * 16 + x]  = UINT32_C(0xff000000);
			pixels[14 * 16 + x] = UINT32_C(0xff000000);
		}
	}
	else if (shape == ST_CONSOLE_POINTER_LINK) {
		for (uint32_t y = 1; y < 13; ++y)
			pixels[y * 16 + 6] = UINT32_C(0xff000000);
		for (uint32_t x = 6; x < 12; ++x)
			pixels[6 * 16 + x] = UINT32_C(0xff000000);
		for (uint32_t y = 6; y < 14; ++y) {
			pixels[y * 16 + 5]  = UINT32_C(0xff000000);
			pixels[y * 16 + 12] = UINT32_C(0xff000000);
		}
		for (uint32_t x = 5; x <= 12; ++x)
			pixels[13 * 16 + x] = UINT32_C(0xff000000);
	}
	else if (shape == ST_CONSOLE_POINTER_CROSSHAIR) {
		for (unsigned i = 1; i < 15; i++) {
			pixels[7 * 16 + i] = UINT32_C(0xff000000);
			pixels[i * 16 + 7] = UINT32_C(0xff000000);
		}
	}
	else if (shape == ST_CONSOLE_POINTER_WAIT) {
		for (unsigned y = 2; y < 14; y++)
			for (unsigned x = 3; x < 13; x++) {
				unsigned edge = y < 8 ? y - 2 : 13 - y;
				if (y == 2 || y == 13 || x == 3 + edge || x == 12 - edge)
					pixels[y * 16 + x] = UINT32_C(0xff000000);
			}
	}
	else if (shape == ST_CONSOLE_POINTER_WATCH) {
		for (int y = 2; y < 14; y++)
			for (int x = 2; x < 14; x++) {
				int dx = x - 7, dy = y - 7, r = dx * dx + dy * dy;
				if ((r >= 20 && r <= 30) || (x == 7 && y >= 4 && y <= 7)
				    || (y == 7 && x >= 7 && x <= 10))
					pixels[y * 16 + x] = UINT32_C(0xff000000);
			}
	}
	else if (shape == ST_CONSOLE_POINTER_STOP) {
		/* Raised palm, with separate fingers and an enclosed wrist. */
		for (unsigned y = 2; y < 14; y++)
			for (unsigned x = 3; x < 13; x++)
				if ((y >= 6 && (x == 3 || x == 12)) || (y == 13 && x >= 4 && x <= 11)
				    || (x == 5 && y <= 8) || (x == 7 && y <= 7) || (x == 9 && y <= 7)
				    || (x == 11 && y >= 3 && y <= 8))
					pixels[y * 16 + x] = UINT32_C(0xff000000);
	}
	else if (shape == ST_CONSOLE_POINTER_PROHIBITED) {
		for (int y = 2; y < 14; y++)
			for (int x = 2; x < 14; x++) {
				int dx = x - 7, dy = y - 7, r = dx * dx + dy * dy;
				if ((r >= 20 && r <= 30) || (x == y && x >= 3 && x <= 11))
					pixels[y * 16 + x] = UINT32_C(0xff000000);
			}
	}
	if (shape >= ST_CONSOLE_POINTER_WATCH) {
		for (int y = 0; y < 16; y++)
			for (int x = 0; x < 16; x++)
				if (pixels[y * 16 + x] == UINT32_C(0xff000000)) {
					if (x > 0 && pixels[y * 16 + x - 1] == 0)
						pixels[y * 16 + x - 1] = UINT32_C(0xffffffff);
					if (x < 15 && pixels[y * 16 + x + 1] == 0)
						pixels[y * 16 + x + 1] = UINT32_C(0xffffffff);
					if (y > 0 && pixels[(y - 1) * 16 + x] == 0)
						pixels[(y - 1) * 16 + x] = UINT32_C(0xffffffff);
					if (y < 15 && pixels[(y + 1) * 16 + x] == 0)
						pixels[(y + 1) * 16 + x] = UINT32_C(0xffffffff);
				}
	}
}

#endif
