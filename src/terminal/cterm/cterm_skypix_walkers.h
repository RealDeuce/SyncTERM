/* Ordered, clipped-by-the-caller Amiga graphics.library primitive writes. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_SKYPIX_WALKERS_H_
#define CTERM_SKYPIX_WALKERS_H_
#include <stdbool.h>
typedef void (*cterm_skypix_plot_t)(void *, int, int);
int cterm_skypix_word(unsigned value);
void cterm_skypix_line(int x, int y, int end_x, int end_y, cterm_skypix_plot_t plot, void *context);
void cterm_skypix_ellipse(int x, int y, int radius_x, int radius_y, bool kick13,
						  cterm_skypix_plot_t plot, void *context);
#endif
