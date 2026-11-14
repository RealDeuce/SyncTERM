/* BIG active graphics clear and VGA console blank, references23/46/59. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_CLEAR_H_
#define ST_BIG_CLEAR_H_
#include "st_big_memory.h"
#include "st_big_profile.h"
#include <stdint.h>
struct st_big_state;
/* Clear the allocated active page, then public moveto0,0 on success.
 * Sink addresses are relative to the current BGI active segment. */
int st_big_clear_device(struct st_big_state *, const struct st_big_memory_sink *);
/* Snapshot byte window/geometry and emit original per-scanline zero then
 * attribute plane passes. Sink segment is A000; page is BIOS-selected.
 * WORD addresses, packed width/height borrow and zero-count LOOP apply.
 * Cursor home/current window reload and native page binding are callers. */
int st_big_clear_console(const uint8_t bounds[4], uint8_t attribute,
	const struct st_big_mode *, uint8_t page, const struct st_big_memory_sink *);
#endif
