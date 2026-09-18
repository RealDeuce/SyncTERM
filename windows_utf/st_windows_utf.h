/* Strict conversion between UTF-8 and Windows UTF-16 strings. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_WINDOWS_UTF_H_
#define ST_WINDOWS_UTF_H_

#include <wchar.h>

/* Results are allocated with malloc() and unchanged on failure. */
int st_windows_utf8_to_utf16(wchar_t **result, const char *source);
int st_windows_utf16_to_utf8(char **result, const wchar_t *source);

#endif
