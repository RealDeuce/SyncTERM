/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Isolate the private parser without starting a terminal input thread. */
#include "../console_ansi.c"

int
main(void)
{
	struct ansi_state state = {0};
	static const unsigned char sequence[] = "\033[1;2;3;4A";

	memcpy(state.escape, sequence, sizeof(sequence) - 1);
	state.escape_length = sizeof(sequence) - 1;
	/* A NULL display safely rejects event delivery after parsing. */
	return parse_escape(NULL, &state) ? EXIT_SUCCESS : EXIT_FAILURE;
}
