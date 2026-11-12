/* Shared already-clipped thin BGI stepping; no protocol/state ownership. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BGI_LINE_H_
#define ST_BGI_LINE_H_
#include <stdint.h>
/* Endpoints/doubled deltas must fit int. Both endpoints are inclusive;
 * upper Y starts, equal Y preserves caller order, pattern bit0 is first.
 * Non-null borrowed pattern/phase/callback; phase is0..15 and is updated.
 * Clipping, width, colour, writes and callback failure belong to callers. */
void st_bgi_thin_line(int x1, int y1, int x2, int y2,
	const uint16_t *pattern, int *phase, void (*pixel)(void *, int, int), void *context);
#endif
