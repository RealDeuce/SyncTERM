/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#ifndef ST_WREN_LICENSES_H
#define ST_WREN_LICENSES_H

#include <stddef.h>

struct st_wren_license {
	const char *name;
	const unsigned char *text;
	size_t length;
};

/* Generated from the checked-in notices, terminated by a NULL name. */
extern const struct st_wren_license st_wren_licenses[];

#endif
