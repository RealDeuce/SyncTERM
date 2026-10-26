/* Internal application path selection interface. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_APP_PATHS_INTERNAL_H_
#define ST_APP_PATHS_INTERNAL_H_

#include "st_app_paths.h"

int st_app_paths_platform_init(struct st_app_paths *paths);
char *st_app_path_join(const char *directory, const char *name);
char *st_app_path_copy(const char *path);

#endif
