/* Shared BGI integer trigonometry; protocol angle/metric rules stay outside. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BGI_TRIG_H_
#define ST_BGI_TRIG_H_
#include <stdint.h>
/* Integral host degrees; returns a signed16.16 coefficient. The common
 * quarter table has91 unsigned Q1.15 words, including8000=positive1.0. */
int32_t st_bgi_sin32(int angle);
/* Unsigned radius WORD and original partial products; return the result WORD
 * interpreted as signed16. This is floor scaling, not signed truncation. */
int16_t st_bgi_trig_mul(int32_t coefficient, uint16_t radius);
#endif
