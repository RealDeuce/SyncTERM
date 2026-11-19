/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_BUILTIN_REFERENCE_H_
#define ST_RIP3_BUILTIN_REFERENCE_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

struct st_rip3_builtin_parameter {
	size_t length;
	char text[21];
};

struct st_rip3_builtin_reference {
	char name[21];
	size_t count;
	struct st_rip3_builtin_parameter parameters[40];
};

/* Internal decoded representation of one ordinary $NAME[(parameters)]$
 * reference, after
 * macro expansion and special prefix/user-variable dispatch. This is not the
 * user prompt grammar or a raw stream framer. Names fold to uppercase ASCII;
 * parameter bytes retain case and surrounding spaces for handler normalization.
 * Empty () and empty comma-separated fields occupy no parameter slots. At most
 * 40 nonempty parameters of 20 bytes each; oversized input rejects rather than
 * silently truncating. Quoted-empty arguments are rejected by the parser.
 * Parentheses/dollar delimiters cannot nest here; no recursive expansion,
 * quoting, escape interpretation, numeric conversion or default substitution
 * is performed. NULs reject. Decoding/recovery belongs to the parser; use the
 * public host-variable process entry rather than exposing decoder errors. */
/* Case-insensitive exact name recognition from the pinned 3.x inventory,
 * including obsolete fixed-slot forms and attested unrecovered processors.
 * This does not assert implementation/support or resolve alias semantics.
 * Input is an explicit identifier span, without '$' or parameters. Invalid
 * identifiers and names absent from the inventory return false. No expansion,
 * user lookup or prompt occurs. Prefix forms must be handled before lookup. */
bool st_rip3_builtin_known(const char *name, size_t length);

#endif
