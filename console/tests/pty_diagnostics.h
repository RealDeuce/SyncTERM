/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_PTY_TEST_DIAGNOSTICS_H
#define ST_PTY_TEST_DIAGNOSTICS_H

/* Test-only bounded captures: do not dump an inherited environment or turn
 * terminal output into control sequences in the CI job's own log. */
static void
dump_terminal_output(const unsigned char *output, size_t size)
{
	const char *term = getenv("TERM");
	const char *terminfo = getenv("TERMINFO");
	const char *colorterm = getenv("COLORTERM");
	size_t start = size > 512 ? size - 512 : 0;

	fprintf(stderr, "PTY bytes=%zu TERM=%.120s TERMINFO=%.120s COLORTERM=%.120s\n",
	    size, term == NULL ? "(unset)" : term,
	    terminfo == NULL ? "(unset)" : terminfo,
	    colorterm == NULL ? "(unset)" : colorterm);
	for (size_t i = start; i < size; ++i)
		fprintf(stderr, "%02x%s", output[i],
		    (i - start + 1) % 32 == 0 ? "\n" : " ");
	fputc('\n', stderr);
}
#endif
