/* BIG curve scalar adapters; complete fronts/recurrence remain separate. */
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
#endif
