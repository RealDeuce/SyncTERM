/* Windows-native path conversion for platform implementation code. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef SYNCTERM_PATH_WINDOWS_H_
#define SYNCTERM_PATH_WINDOWS_H_

#include <wchar.h>

/* Returns an allocated absolute extended-namespace path. */
int path_windows_extended(wchar_t **result, const char *path);

#endif
