/* Common immutable BGI data; protocol state and wire grammars stay separate. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BGI_DATA_H_
#define ST_BGI_DATA_H_
#include <stddef.h>
#include <stdint.h>
extern const uint16_t st_bgi_ega_colors[64][3];
extern const uint8_t st_bgi_ega_defaults[16];
extern const uint16_t st_bgi_line_patterns[4];
extern const uint8_t st_bgi_fill_patterns[12][8];
/* Positive sizes1..10 match RIP and original BIG. Index0 is RIP's1/1;
 * BIG size0 instead reapplies its separately saved custom ratios. */
extern const int st_bgi_stroke_mults[11], st_bgi_stroke_divs[11];
extern const uint8_t st_bgi_bitmap_font[2048];
/* The single current font set: bitmap0, CHRs1..10. Views are borrowed.
 * Unknown IDs return NULL/size0. No runtime font-file dependency. */
const uint8_t *st_bgi_font_data(unsigned number, size_t *size);
#endif
