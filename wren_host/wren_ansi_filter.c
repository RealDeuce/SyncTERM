/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

/* The escape-state transitions are adapted from the legacy RIP filter. */

#include "wren_ansi_filter.h"

size_t
st_wren_ansi_keep_text(const uint8_t *in, size_t in_len, uint8_t *out,
	enum ansi_state *state)
{
	uint8_t *out_start = out;

	for (size_t i = 0; i < in_len; i++) {
		uint8_t b = in[i];
		/* Only a byte outside a sequence can be visible text. */
		enum ansi_state entry = *state;

		if (entry == ANSI_STATE_NONE && b != 0x1B)
			*out++ = b;

		switch (entry) {
		case ANSI_STATE_NONE:
			if (b == 0x1B)
				*state = ANSI_STATE_GOT_ESC;
			break;

		case ANSI_STATE_GOT_ESC:
			if (b < '0' || b > '~') {
				*state = ANSI_STATE_NONE;
				break;
			}
			switch (b) {
			case 'X':
				*state = ANSI_STATE_GOT_STRING;
				break;
			case 'P':
			case ']':
			case '^':
			case '_':
				*state = ANSI_STATE_GOT_LIMITED_STRING;
				break;
			case '[':
				*state = ANSI_STATE_GOT_CSI;
				break;
			default:
				*state = ANSI_STATE_NONE;
				break;
			}
			break;

		case ANSI_STATE_GOT_LIMITED_STRING:
			if (b == 0x1B) {
				*state = ANSI_STATE_GOT_ESC_IN_STRING;
				break;
			}
			if (b < 0x08 || (b > 0x0d && b < 0x20) || b > 0x7e)
				*state = ANSI_STATE_NONE;
			break;

		case ANSI_STATE_GOT_ESC_IN_LIMITED_STRING:
			*state = ANSI_STATE_NONE;
			break;

		case ANSI_STATE_GOT_ESC_IN_STRING:
			if (b == 'X' || b == '\\')
				*state = ANSI_STATE_NONE;
			break;

		case ANSI_STATE_GOT_STRING:
			if (b == 0x1B)
				*state = ANSI_STATE_GOT_ESC_IN_STRING;
			break;

		case ANSI_STATE_GOT_CSI:
			if (b >= '@' && b <= '~')
				*state = ANSI_STATE_NONE;
			else if (b >= ' ' && b <= '/')
				*state = ANSI_STATE_GOT_IB;
			else if (b < '0' || b > '?')
				*state = ANSI_STATE_NONE;
			break;

		case ANSI_STATE_GOT_IB:
			if (b < '0' || b > '?')
				*state = ANSI_STATE_NONE;
			else if (b < ' ' || b > '/')
				*state = ANSI_STATE_NONE;
			break;
		}
	}

	return (size_t)(out - out_start);
}
