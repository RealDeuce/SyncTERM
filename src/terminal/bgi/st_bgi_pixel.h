/* Shared four-bit BGI pixel operations; geometry/ownership stay at caller. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BGI_PIXEL_H_
#define ST_BGI_PIXEL_H_
#include <stdint.h>
/* Inputs0..15; modes0..4: COPY, XOR, OR, AND, NOT-source. Caller validates.
 * Bit permutations of distinct indexed pens commute with these operations.
 * Palette lookup and native pixel-token tags belong to the adapter. */
static inline uint8_t st_bgi_combine_pixel(uint8_t source, uint8_t destination, uint8_t mode)
{
	switch (mode) {
	case 1: return (uint8_t)((source^destination)&15u);
	case 2: return (uint8_t)((source|destination)&15u);
	case 3: return (uint8_t)((source&destination)&15u);
	case 4: return (uint8_t)(source^15u);
	default: return (uint8_t)(source&15u);
	}
}
#endif
