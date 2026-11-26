/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP_ENABLE_H
#define ST_RIP_ENABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const uint8_t st_rip_enable_sequence[] = {0x1b, '[', '2', '!'};

/* While disabled, hold only a possible prefix. A mismatching ESC starts a
 * fresh candidate immediately after the bytes released to the terminal. */
static inline bool
st_rip_match_enable(size_t *matched, uint8_t byte, uint8_t plain[4], size_t *length)
{
	*length = 0;
	if (byte == st_rip_enable_sequence[*matched]) {
		if (++(*matched) < sizeof(st_rip_enable_sequence))
			return false;
		*matched = 0;
		return true;
	}
	*length = *matched;
	memcpy(plain, st_rip_enable_sequence, *matched);
	*matched = byte == 0x1b ? 1 : 0;
	if (*matched == 0)
		plain[(*length)++] = byte;
	return false;
}

#endif
