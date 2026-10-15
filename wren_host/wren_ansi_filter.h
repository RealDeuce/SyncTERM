/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#ifndef ST_WREN_ANSI_FILTER_H
#define ST_WREN_ANSI_FILTER_H

/* Stateful ANSI / DEC escape stripping for capture and onMatchClean.
 * The caller owns one state per input stream.  The transitions follow
 * the legacy RIP ANSI-envelope recognizer, but only the text output
 * needed by these two consumers is exposed here.
 *
 * The state machine recognises:
 *   - 7-bit C1 escapes:  ESC <intermediate>* <final>
 *   - CSI sequences:     ESC [ <param>* <intermediate>* <final>
 *   - DCS/OSC/PM/APC:    ESC P|]|^|_ <body until ESC \>
 *   - SOS-style strings: ESC X <body until ESC \>
 *
 * It does NOT model 8-bit C1 (single-byte CSI/OSC at 0x9B etc.) —
 * the streams it sees are the standard 7-bit form BBSes use.
 */

#include <stddef.h>
#include <stdint.h>

enum ansi_state {
	ANSI_STATE_NONE = 0,
	ANSI_STATE_GOT_ESC,
	ANSI_STATE_GOT_CSI,
	ANSI_STATE_GOT_STRING,
	ANSI_STATE_GOT_LIMITED_STRING,
	ANSI_STATE_GOT_ESC_IN_STRING,
	ANSI_STATE_GOT_ESC_IN_LIMITED_STRING,
	ANSI_STATE_GOT_IB
};

/* `out` may alias `in`.  The return value is bytes written to `out`. */
size_t st_wren_ansi_keep_text(const uint8_t *in, size_t in_len,
	uint8_t *out, enum ansi_state *state);

#endif /* ST_WREN_ANSI_FILTER_H */
