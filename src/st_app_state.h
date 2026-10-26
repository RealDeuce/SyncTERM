/* SyncTERM application state policy. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_APP_STATE_H_
#define ST_APP_STATE_H_

#include "st_profile.h"

#include <stdbool.h>

bool st_app_should_persist_scaling(bool normal_exit, bool safe_mode,
	enum st_screen_mode startup_mode, enum st_screen_mode current_mode);

#endif
