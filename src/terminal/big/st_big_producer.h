/* Original DDO point producer, shared by bar3d and curve emission. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_PRODUCER_H_
#define ST_BIG_PRODUCER_H_
#include "st_big_draw.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct st_big_producer_point { uint16_t x, y; };
struct st_big_producer {
	uint8_t mode;
	uint16_t count, capacity, run_length, first_x, first_y;
	/* Borrowed native storage; configured WORD capacity and actual extent
	 * are independent. Binding does not reset current stage/run state. */
	struct st_big_producer_point *points;
	size_t storage_count;
	/* Only the point-return quirk observes these device inputs. Plane
	 * bytes are reconstructed at the indexed boundary, never retained. */
	bool batch;
	uint8_t read_plane;
	uint16_t video_segment;
};
struct st_big_producer_registers { uint16_t ax, bx, cx, dx; };
struct st_big_producer_sink {
	struct st_big_draw_sink drawing;
	void *read_context;
	/* Pre-write active-page byte at byte-aligned physical X, MSB first.
	 * Required only for accepted batched points; synchronous like pixel. */
	int (*read_byte)(void *, uint16_t x, uint16_t y, uint8_t plane, uint8_t *);
};
int st_big_producer_bind(struct st_big_producer *, struct st_big_producer_point *, size_t storage_count, uint16_t configured_bytes);
/* Original stage0: mode1/count0/run0; retains first-point scratch/storage. */
int st_big_producer_start(struct st_big_producer *);
/* Physical coordinates, no origin translation or CP assignment. suppressed
 * is the original returned carry: true only for collector paths. Mode2
 * returns actual point AX/BX/CX/DX, including on a rejected point. */
int st_big_producer_emit(struct st_big_producer *, struct st_big_state *,
	struct st_big_producer_registers *, const struct st_big_producer_sink *, bool *suppressed);
/* Shared DDO0378: unsuppressed actual return AX/BX becomes DDO/device CP,
 * including clipped/rejected points. Public CP is retained. */
int st_big_producer_move(struct st_big_state *, struct st_big_producer *,
	struct st_big_producer_registers *, const struct st_big_producer_sink *);
/* DDO0450: submit physical AX/BX through the current producer first, then
 * draw actual returned AX/BX -> CX/DX with ordinary clipped width strokes.
 * Suppression skips drawing; no public/DDO/device point is assigned. */
int st_big_producer_stroke(struct st_big_state *, struct st_big_producer *,
	struct st_big_producer_registers *, const struct st_big_producer_sink *);
/* DDO059e flush: snapshot reader/count, force mode0, and restore only DDO CP
 * on normal return. last_y retains its input on skipped reads; otherwise it
 * reports the last reader Y (including an empty fetch's8000), not raw device
 * line-register clobbers. Borrowed storage must stay alive during callbacks. */
int st_big_producer_flush(struct st_big_state *, struct st_big_producer *,
	const struct st_big_producer_sink *, uint16_t *last_y);
#endif
