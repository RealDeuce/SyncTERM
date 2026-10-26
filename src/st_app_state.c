/* SyncTERM application state policy. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_app_state.h"

bool
st_app_should_persist_scaling(bool normal_exit, bool safe_mode,
	enum st_screen_mode startup_mode, enum st_screen_mode current_mode)
{
	return normal_exit && !safe_mode && startup_mode == current_mode;
}
