/* Shared eight-bit glyph row expansion; protocol layout stays with owners. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BGI_BITMAP_H_
#define ST_BGI_BITMAP_H_
#include <stdint.h>
/* Expand set bits MSB-first, then repeated rows/columns for each bit.
 * Relative coordinates allow each caller's placement/rotation policy.
 * BIG calls once per expanded output row after rereading its source;
 * RIP calls once per source row with both magnifications. */
int st_bgi_bitmap_expand_row(uint8_t bits, unsigned scale_x, unsigned scale_y,
	int (*pixel)(void *, unsigned x, unsigned y), void *context);
#endif
