/* BIG image format/storage/capture boundary, references25/40/56. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_IMAGE_H_
#define ST_BIG_IMAGE_H_
#include "st_big_memory.h"
#include "st_big_state.h"
#include <stddef.h>
#include <stdint.h>
enum {
	ST_BIG_IMAGE_CAPACITY=65548,
	ST_BIG_IMAGE_BASE=4,
	/* Reachable payload/4 rounded up to a complete maximum-width row. */
	ST_BIG_IMAGE_PENS=196608,
};
struct st_big_image {
	uint8_t *bytes, *pens;
	size_t capacity, pen_capacity, pen_count, plane_bytes;
};
/* Borrowed, disjoint storage; no initialization, allocation or valid-image
 * flag. bytes contains the complete owned allocation, beginning at B:0004.
 * WORD cursor wrap to0..3 is outside that allocation. Its last16 bytes are
 * owned but unreachable by the original fixed-segment image cursor.
 * The indexed cache includes retained tails and is decoded at binding,
 * explicit import or a capture header change, never at a blit. Direct
 * byte edits require import before using the cache. No concurrent edits. */
int st_big_image_bind(struct st_big_image *, uint8_t *bytes, size_t capacity,
	uint8_t *pens, size_t pen_capacity);
int st_big_image_import(struct st_big_image *);
int st_big_image_header(const struct st_big_image *, uint16_t *width_delta, uint16_t *height_delta);
/* Original fixed-four-plane WORD size query, including zero on overflow.
 * This is not a capacity check and does not validate a later capture. */
uint16_t st_big_image_size(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
/* Unsigned corner sort, header publication before driver dispatch, origin
 * translation without viewport clipping, rows and planes3..0, lookahead
 * even at shift0. Callback/extent errors retain reached bytes/cache/state.
 * No public/DDO/device point, palette or drawing attribute is changed. */
int st_big_image_capture(struct st_big_image *, struct st_big_state *,
	const struct st_big_memory_sink *, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
/* Checked source-byte access for a later paste: returns its eight cached
 * indexed pens and plane. No bitfield conversion. Outputs unchanged on
 * error, including allocator-prefix cursor wrap and truncated payload. */
int st_big_image_source(const struct st_big_image *, uint16_t cursor,
	const uint8_t **pens, uint8_t *plane);
#endif
