/* BIG curve scalars, decimal/angular outlines and producer binding. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_CURVE_H_
#define ST_BIG_CURVE_H_
#include <stdint.h>
int32_t st_big_curve_sin(uint16_t angle);
int32_t st_big_curve_cos(uint16_t angle);
uint16_t st_big_curve_metric(uint16_t x, uint16_t y);
/* Radius is unsigned; aspect words are signed. Zero denominator is a checked
 * error and leaves the output unchanged. No graphics positioning occurs. */
int st_big_curve_aspect(uint16_t radius, uint16_t aspect_x, uint16_t aspect_y, uint16_t *radius_y);
struct st_big_curve_decimal_frame {
	uint16_t center[2], radius[2], start[2], end[2];
};
struct st_big_curve_point_sink {
	void *context;
	/* Synchronous physical WORD requests, retaining order and duplicates.
	 * The adapter owns current pen/producer/point/batch effects. */
	int (*point)(void *, uint16_t x, uint16_t y);
};
/* Outline arm of DDO0d70 after batch entry. Limits use original endpoint
 * offsets before replacing zero radii. Captured geometry survives callbacks.
 * Checked32 overflow is a normal empty return; errors keep prior requests.
 * Batch exit, public/DDO/device point gates and arc publication are callers. */
int st_big_curve_decimal_outline(struct st_big_curve_decimal_frame,
	const struct st_big_curve_point_sink *);
struct st_big_state;
struct st_big_producer;
struct st_big_producer_sink;
struct st_big_curve_batch_sink {
	void *context;
	/* Optional replacement for the built-in sixteen-pen batch methods.
	 * Converted begin colour is published after return. Current method/
	 * context fields are reread at each reached site, without reentry. */
	int (*begin)(void *, uint16_t *color);
	int (*end)(void *);
};
/* Complete decimal batch/producer-move binding, without curve front/stage
 * selection, center placement, outer DDO restoration or arc publication.
 * NULL batch selects the built-in native methods. Current nonzero gate
 * skips each method independently. Normal empty overflow still reaches end. */
int st_big_curve_decimal_draw(struct st_big_state *, struct st_big_producer *,
	struct st_big_curve_decimal_frame, const struct st_big_producer_sink *,
	const struct st_big_curve_batch_sink *);
/* Selected regular outline route of07a3: capture existing physical DDO
 * center/radii,0906 shortcuts/core,A55 publication,stage5 producer clear.
 * No public center move, ffff query, outer DDO restore or angular selection.
 * Publication reads current origin/offsets; failures keep reached prefixes. */
int st_big_curve_regular_outline(struct st_big_state *, struct st_big_producer *,
	uint16_t start, uint16_t end, uint16_t radius_x, uint16_t radius_y,
	const struct st_big_producer_sink *, const struct st_big_curve_batch_sink *);
/* Complete lower07a3 outline selection, regular or angular/buffered.
 * Physical DDO center is already placed; public aspect/query/017a save
 * belongs to the wrapper. A nonterminating signed angle loop fails after
 * one complete WORD turn, preserving its accepted collector/style prefix. */
int st_big_curve_outline(struct st_big_state *, struct st_big_producer *,
	uint16_t start, uint16_t end, uint16_t radius_x, uint16_t radius_y,
	const struct st_big_producer_sink *, const struct st_big_curve_batch_sink *);
#endif
