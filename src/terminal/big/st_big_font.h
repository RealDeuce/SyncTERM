/* Original embedded V100 CHRs and borrowed startup registry, BIG28/30. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_FONT_H_
#define ST_BIG_FONT_H_
#include <stddef.h>
#include <stdint.h>
enum { ST_BIG_FONT_SLOTS=20 };
struct st_big_font_binding { uint8_t name[4]; const uint8_t *body; size_t size; };
struct st_big_fonts {
	struct st_big_font_binding slots[ST_BIG_FONT_SLOTS];
	uint16_t count, loaded;
	const uint8_t *active;
	size_t active_size;
};
/* Full original files, including headers/notices;1..4 only, bitmap0 is a
 * separate source. Unknown IDs returnNULL/size0; no runtime file access. */
const uint8_t *st_big_font_data(unsigned number, size_t *size);
/* Native borrowed binding. Header magic, marker and independent version
 * BYTE tests precede the20-name scan. Success keeps an older graph error;
 * malformed/name rejection sets-13/-11. Checked extents replace foreign
 * accesses. Registration does not invalidate selected/loaded/active owners.
 * No source mutation/alignment copy or owned DOS resource is introduced. */
int st_big_font_register(struct st_big_fonts *, const uint8_t *, size_t, int *graph_error, uint16_t *id);
/* Loaded ten names/twenty records, then actual4,2,3,1 startup registrations.
 * Borrowed assets are never freed by I/R/reset/destruction. */
int st_big_fonts_init(struct st_big_fonts *, int *graph_error);
#endif
