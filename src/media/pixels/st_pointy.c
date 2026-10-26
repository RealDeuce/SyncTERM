
/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_scaler_internal.h"

#include <stddef.h>

void
st_pointy_scale_odd_region(const uint32_t* src, uint32_t* dest,
    const int width, const int height, const int mult, uint32_t region_x,
    uint32_t region_y, uint32_t region_width, uint32_t region_height)
{
	int x, y;
	const uint32_t* s;
	uint32_t* d;
	int prevline, prevcol, nextline, nextcol;
	int i, j;
	int mid = mult / 2;
	int multoff = mult - 1;
	size_t dline = (size_t)width * (size_t)mult;
	size_t dbott;
	size_t dstripe = dline * (size_t)mult;

	for (y = (int)region_y; y < (int)(region_y + region_height); y++) {
		prevline = y == 0 ? 0 : -width;
		nextline = y == height - 1 ? 0 : width;
		for (x = (int)region_x; x < (int)(region_x + region_width); x++) {
			s = &src[(size_t)y * (size_t)width + (size_t)x];
			prevcol = x == 0 ? 0 : -1;
			nextcol = x == width - 1 ? 0 : 1;

			for (i = 0; i < mid; i++) {
				d = &dest[dstripe * (size_t)y + dline * (size_t)i
				    + (size_t)x * (size_t)mult];
				dbott = dline * (size_t)(multoff - i * 2);

				for (j = 0; j < mid - i; j++) {
					if (s[prevline + prevcol] == s[0]) {
						d[j] = s[0];
					}
					else if (s[prevline] == s[prevcol]) {
						d[j] = s[prevcol];
					}
					else {
						d[j] = s[0];
					}

					if (s[prevline + nextcol] == s[0]) {
						d[multoff - j] = s[0];
					}
					else if (s[prevline] == s[nextcol]) {
						d[multoff - j] = s[nextcol];
					}
					else {
						d[multoff - j] = s[0];
					}

					if (s[prevcol + nextline] == s[0]) {
						d[dbott + j] = s[0];
					}
					else if(s[prevcol] == s[nextline]) {
						d[dbott + j] = s[prevcol];
					}
					else {
						d[dbott + j] = s[0];
					}

					if (s[nextcol + nextline] == s[0]) {
						d[dbott + multoff - j] = s[0];
					}
					else if (s[nextcol] == s[nextline]) {
						d[dbott + multoff - j] = s[nextcol];
					}
					else {
						d[dbott + multoff - j] = s[0];
					}
				}

				// And the rest is always kept the same
				for (; j < mid; j++) {
					d[j] = s[0];
					d[multoff - j] = s[0];
					d[dbott + j] = s[0];
					d[dbott + multoff - j] = s[0];
				}

				// And the middle dot.
				d[j] = s[0];
				d[dbott + j] = s[0];
			}

			d = &dest[dstripe * (size_t)y + dline * (size_t)i
			    + (size_t)x * (size_t)mult];

			for (j = 0; j < mid; j++) {
				d[j] = s[0];
				d[multoff - j] = s[0];
			}
			d[j] = s[0];

		}
	}
}

void
st_pointy_scale_odd(const uint32_t* src, uint32_t* dest, const int width,
    const int height, const int mult)
{
	st_pointy_scale_odd_region(src, dest, width, height, mult, 0, 0,
	    (uint32_t)width, (uint32_t)height);
}

void
st_pointy_scale5_region(const uint32_t* src, uint32_t* dest,
    const int width, const int height, uint32_t region_x, uint32_t region_y,
    uint32_t region_width, uint32_t region_height)
{
	int x, y;
	const uint32_t* s;
	uint32_t* d;
	size_t w5 = (size_t)width * 5U;
	size_t w10 = (size_t)width * 10U;
	size_t w15 = (size_t)width * 15U;
	size_t w20 = (size_t)width * 20U;
	int prevline, prevcol, nextline, nextcol;

	for (y = (int)region_y; y < (int)(region_y + region_height); y++) {
		prevline = y == 0 ? 0 : -width;
		nextline = y == height - 1 ? 0 : width;
		for (x = (int)region_x; x < (int)(region_x + region_width); x++) {
			s = &src[(size_t)y * (size_t)width + (size_t)x];
			d = &dest[(size_t)y * (size_t)width * 25U
			    + (size_t)x * 5U];
			prevcol = x == 0 ? 0 : -1;
			nextcol = x == width - 1 ? 0 : 1;

			if (s[prevline + prevcol] == s[0]) {
				d[0] = s[0];
				d[1] = s[0];
				d[w5] = s[0];
			}
			else if (s[prevcol] == s[prevline]) {
				d[0] = s[prevcol];
				d[1] = s[prevcol];
				d[w5] = s[prevcol];
			}
			else {
				d[0] = *s;
				d[1] = *s;
				d[w5] = *s;
			}

			// Top-middle stays OG.
			d[2] = *s;
			d[w5+1] = *s;
			d[w5+2] = *s;
			d[w5+3] = *s;

			// And so on around the outside (round the outside)
			if (s[prevline + nextcol] == s[0]) {
				d[3] = s[0];
				d[4] = s[0];
				d[w5 + 4] = s[0];
			}
			else if (s[nextcol] == s[prevline]) {
				d[3] = s[nextcol];
				d[4] = s[nextcol];
				d[w5 + 4] = s[nextcol];
			}
			else {
				d[3] = s[0];
				d[4] = s[0];
				d[w5 + 4] = s[0];
			}

			d[w10] = *s;
			d[w10+1] = *s;
			d[w10+2] = *s;
			d[w10+3] = *s;
			d[w10+4] = *s;

			if (s[prevcol + nextline] == s[0]) {
				d[w15] = s[0];
				d[w20] = s[0];
				d[w20 + 1] = s[0];
			}
			else if(s[prevcol] == s[nextline]) {
				d[w15] = s[prevcol];
				d[w20] = s[prevcol];
				d[w20 + 1] = s[prevcol];
			}
			else {
				d[w15] = s[0];
				d[w20] = s[0];
				d[w20 + 1] = s[0];
			}

			d[w15 + 1] = *s;
			d[w15 + 2] = *s;
			d[w15 + 3] = *s;
			d[w20 + 2] = *s;

			if (s[nextcol + nextline] == s[0]) {
				d[w15 + 4] = s[0];
				d[w20 + 3] = s[0];
				d[w20 + 4] = s[0];
			}
			else if (s[nextcol] == s[nextline]) {
				d[w15 + 4] = s[nextcol];
				d[w20 + 3] = s[nextcol];
				d[w20 + 4] = s[nextcol];
			}
			else {
				d[w15 + 4] = s[0];
				d[w20 + 3] = s[0];
				d[w20 + 4] = s[0];
			}

		}
	}
}

void
st_pointy_scale5(const uint32_t* src, uint32_t* dest, const int width,
    const int height)
{
	st_pointy_scale5_region(src, dest, width, height, 0, 0,
	    (uint32_t)width, (uint32_t)height);
}

void
st_pointy_scale3_region(const uint32_t* src, uint32_t* dest,
    const int width, const int height, uint32_t region_x, uint32_t region_y,
    uint32_t region_width, uint32_t region_height)
{
	int x, y;
	const uint32_t* s;
	uint32_t* d;
	size_t w3 = (size_t)width * 3U;
	size_t w6 = (size_t)width * 6U;
	int prevline, prevcol, nextline, nextcol;

	for (y = (int)region_y; y < (int)(region_y + region_height); y++) {
		prevline = y == 0 ? 0 : -width;
		nextline = y == height - 1 ? 0 : width;
		for (x = (int)region_x; x < (int)(region_x + region_width); x++) {
			s = &src[(size_t)y * (size_t)width + (size_t)x];
			d = &dest[(size_t)y * (size_t)width * 9U
			    + (size_t)x * 3U];
			prevcol = x == 0 ? 0 : -1;
			nextcol = x == width - 1 ? 0 : 1;

			// Top-left is filled if both left and top are the same.
			if (s[prevline + prevcol] == s[0])
				d[0] = s[0];
			else if (s[prevcol] == s[prevline])
				d[0] = s[prevcol];
			else
				d[0] = *s;

			// Top-middle stays OG.
			d[1] = *s;

			// And so on around the outside (round the outside)
			if (s[prevline + nextcol] == s[0])
				d[2] = s[0];
			else if (s[nextcol] == s[prevline])
				d[2] = s[nextcol];
			else
				d[2] = *s;

			d[w3] = *s;
			d[w3 + 1] = *s;
			d[w3 + 2] = *s;

			if (s[prevcol + nextline] == s[0])
				d[w6] = s[0];
			else if(s[prevcol] == s[nextline])
				d[w6] = s[prevcol];
			else
				d[w6] = *s;

			d[w6 + 1] = *s;

			if (s[nextcol + nextline] == s[0])
				d[w6 + 2] = s[0];
			else if (s[nextcol] == s[nextline])
				d[w6 + 2] = s[nextcol];
			else
				d[w6 + 2] = s[0];

		}
	}
}

void
st_pointy_scale3(const uint32_t* src, uint32_t* dest, const int width,
    const int height)
{
	st_pointy_scale3_region(src, dest, width, height, 0, 0,
	    (uint32_t)width, (uint32_t)height);
}
