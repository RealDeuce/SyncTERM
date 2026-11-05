/* Probe native primitive/font results; expected results live outside this code. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_skypix_walkers.h"
#include "st_amiga_font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void point(void *context, int x, int y)
{
	(void)context;
	printf("%d %d\n", x, y);
}
int main(int argc, char **argv)
{
	if (argc == 7 && strcmp(argv[1], "ellipse") == 0) {
		cterm_skypix_ellipse(atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]),
							 atoi(argv[2]) == 13, point, NULL);
		return 0;
	}
	if (argc == 6 && strcmp(argv[1], "line") == 0) {
		cterm_skypix_line(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), point, NULL);
		return 0;
	}
	if (argc == 3 && strcmp(argv[1], "text") == 0) {
		const st_amiga_font_t *font = st_amiga_font_builtin("Diamond.font", 20);
		const struct st_amiga_glyph *g = st_amiga_font_glyph(font, '<');
		int end = 16 + g->advance;
		printf("%d\n", end);
		for (unsigned y = 0; y < g->height; ++y)
			for (unsigned x = 0; x < g->width; ++x) {
				unsigned bit = x + g->bit_offset;
				int px = 16 + g->bearing + (int)x,
					py = 40 - (int)st_amiga_font_height(font) + 1 + g->top + (int)y;
				if (atoi(argv[2]) == 13 && px >= end)
					continue;
				if (g->bits[(size_t)y * g->stride + bit / 8] & (0x80u >> (bit % 8)))
					point(NULL, px, py);
			}
		return 0;
	}
	return 2;
}
