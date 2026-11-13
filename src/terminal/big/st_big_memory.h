/* Indexed active-page byte access shared by BIG raster consumers. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_MEMORY_H_
#define ST_BIG_MEMORY_H_
#include <stdint.h>
struct st_big_memory_sink {
	void *context;
	/* Eight logical pens0..15, MSB first. WORD address wrap belongs to
	 * the caller; the backend checks actual storage/aperture bounds.
	 * write replaces selected pen bits (planes) at selected pixels
	 * (mask), retaining all other bits/pixels. Serialized callbacks must
	 * complete synchronously; IN_PROGRESS is a protocol error. */
	int (*read)(void *, uint16_t address, uint8_t pens[8]);
	int (*write)(void *, uint16_t address, uint8_t mask, uint8_t planes, const uint8_t pens[8]);
};
#endif
