/* BIG's distinct IBM system/embedded/EGA graphical bitmap profile. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_BITMAP_H_
#define ST_BIG_BITMAP_H_
#include "st_big_draw.h"
struct st_big_bitmap_font {
	void *context;
	/* Serialized source row access after bounds acceptance. Horizontal
	 * output rereads for each expanded row; rotation reads eight rows
	 * into scratch first. No font/Unicode/console transformation is added. */
	int (*row)(void *, uint8_t code, unsigned row, uint8_t *bits);
};
/* Current shared RIP bitmap data; BIG's source-selection zero/O alias
 * remains protocol policy. No historical ROM variant or runtime file/IVT.
 * The context is unused so this also serves as a source-row callback. */
int st_big_bitmap_row(void *, uint8_t code, unsigned row, uint8_t *bits);
/* Device counted bitmap output at its current anchor. No DDO/public CP
 * publication; each returning glyph (including bounds rejection) advances
 * the device by installed cell width. NULL font selects the IBM profile. */
int st_big_text_bitmap(struct st_big_state *, const uint8_t *, uint8_t count,
	const struct st_big_bitmap_font *, const struct st_big_draw_sink *);
#endif
