/* Extracted from RIP draw_line_to; BIG reference12/22/57/83 proves reuse. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_bgi_line.h"
#include <limits.h>
#include <stdlib.h>
void st_bgi_thin_line(int x1, int y1, int x2, int y2,
    const uint16_t *pattern, int *ppos, void (*pixel)(void *, int, int), void *context)
{
	int *minc, *mins, *mino, *mind;
	int *maxc, *maxs, *maxe, *maxo, *maxd;
	int  x, y;
	int  swap = 0;

	int  dx = abs(x2 - x1);
	int  dy = abs(y2 - y1);

	if (y2 < y1)
		swap = 1;

	if (swap) {
		x = x2; y = y2;
		x2 = x1; y2 = y1;
		x1 = x; y1 = y;
	}

	int xoff = (x2 < x1) ? -1 : 1;
	int yoff = (y2 < y1) ? -1 : 1;

	if (dx >= dy) {
		minc = &y; mins = &y1; mino = &yoff; mind = &dy;
		maxc = &x; maxs = &x1; maxe = &x2; maxo = &xoff; maxd = &dx;
	}
	else {
		minc = &x; mins = &x1; mino = &xoff; mind = &dx;
		maxc = &y; maxs = &y1; maxe = &y2; maxo = &yoff; maxd = &dy;
	}

	int de = 2 * *mind - *maxd;
	int pp = *ppos;

	*minc = *mins;

	for ((*maxc) = *maxs; (((*maxs) < (*maxe)) ? ((*maxc) <= (*maxe)) : ((*maxc) >= (*maxe))); (*maxc) += *maxo) {
		if (*pattern & (1 << pp))
			pixel(context, x, y);
		if (de >= 0) {
			*minc = (*minc) + *mino;
			de = de - ((2 * *maxd) & INT_MAX);
		}
		de = de + ((2 * *mind) & INT_MAX);
		pp = (pp + 1) & 0x0F;
		if (*maxs == *maxe)
			break;
	}
	*ppos = pp;
}
