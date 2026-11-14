/* BIG cached Borland console coordinate gate, references31/46/51. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_CONSOLE_H_
#define ST_BIG_CONSOLE_H_
#include <stdbool.h>
#include <stdint.h>
/* p clamps signed-negative WORDs to0 after both reads. gotoxy converts low
 * bytes relative to cached inclusive left/top/right/bottom, modulo256.
 * Rejection leaves position untouched; admission returns zero-based bytes.
 * Native page/visible cursor effects and host extent checks are callers. */
bool st_big_console_position(const uint8_t bounds[4], uint16_t x, uint16_t y,
	uint8_t position[2]);
#endif
