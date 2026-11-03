/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Test the real document implementation with a failing clone boundary.
 * No fault controls are linked into the application. */
#include "theme_internal.h"

bool st_theme_test_fail_clone;

static struct st_theme *
test_document_theme_clone(const struct st_theme *source)
{
	return st_theme_test_fail_clone ? NULL : theme_clone(source);
}

#define theme_clone test_document_theme_clone
#include "../st_theme_document.c"
