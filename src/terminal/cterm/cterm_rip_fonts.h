/* Exact bitmap and stroke font data used by RIPterm graphics text. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef CTERM_RIP_FONTS_H_
#define CTERM_RIP_FONTS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Font zero is the 256-glyph 8x8 bitmap; one through ten are CHR strokes. */
const uint8_t *cterm_rip_font_data(int number, size_t *size);
/* RIP TTY font styles 1 (7x8), 3 (7x14), and 4 (16x14). */
const uint8_t *cterm_rip_text_font_data(int style, size_t *size);

struct cterm_rip_font_metric {
	int top;
	int bow;
	int base;
	int drop;
};

/* Exact RIPterm button-label metrics, including its byte-truncated values. */
const struct cterm_rip_font_metric *cterm_rip_font_button_metric(
    int number, int size);
bool cterm_rip_button_char_descends(uint8_t ch);

#endif
