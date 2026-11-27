/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"

int
main(void)
{
	struct st_ddofx ddo = {.graphics = calloc(1, sizeof(*ddo.graphics))};
	if (!ddo.graphics)
		return EXIT_FAILURE;
	struct st_ddofx_graphics *g = ddo.graphics;
	g->cell_width = 8;
	g->cell_height = 16;
	g->horizontal_thickness = g->vertical_thickness = 1;
	g->clip = (struct bounds){12, 31, 5, 46};
	memset(g->page[WORK], 23, sizeof(g->page[WORK]));
	struct bounds box = {10, 28, 8, 42};
	const uint8_t text[] = "ABA";
	st_ddofx_print_text_box(&ddo, WORK, box, text);
	const uint8_t *font = st_ddofx_vga_bitmap(&ddo);
	bool ok = font != NULL;
	for (int y = 0; ok && y < HEIGHT; ++y) {
		for (int x = 0; x < WIDTH; ++x) {
			int expected = 23;
			if (x >= 12 && x <= 28 && y >= 8 && y <= 42) {
				if (x == 28 || y == 8 || y == 42)
					expected = 10;
				if (x < 27 && y >= 9 && y < 41) {
					int index = (y - 9) / 16 * 2 + (x - 11) / 8;
					if (index < 3 && (font[16 * text[index] + (y - 9) % 16] & (128 >> ((x - 11) % 8))))
						expected = 15;
				}
			}
			if (g->page[WORK][y][x] != expected) {
				ok = false;
				break;
			}
		}
	}
	free(g);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
