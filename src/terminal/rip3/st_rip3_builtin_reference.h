/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_BUILTIN_REFERENCE_H_
#define ST_RIP3_BUILTIN_REFERENCE_H_

#include <stddef.h>
#include <stdint.h>

struct st_rip3_builtin_parameter {
	size_t length;
	char text[13];
};

struct st_rip3_builtin_reference {
	char name[21];
	size_t count;
	struct st_rip3_builtin_parameter parameters[40];
};

/* Parse one complete ordinary built-in $NAME[(parameters)]$ reference, after
 * macro expansion and special prefix/user-variable dispatch. This is not the
 * user prompt grammar or a raw stream framer. Names fold to uppercase ASCII;
 * parameter bytes retain case, trimming surrounding ASCII spaces as shown in
 * the published built-in examples. Empty () means no parameters; explicit
 * empty comma-separated fields remain present. At most 40 parameters of 12
 * bytes each; oversized input rejects rather than silently truncating.
 * Parentheses/dollar delimiters cannot nest here; no recursive expansion,
 * quoting, escape interpretation, numeric conversion or default substitution
 * is performed. NULs reject. Errors preserve the complete caller result. */
int st_rip3_builtin_reference_parse(struct st_rip3_builtin_reference *result,
	const uint8_t *text, size_t length);

#endif
