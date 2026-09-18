/* SyncTERM console startup policy. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_CONSOLE_STARTUP_H_
#define ST_CONSOLE_STARTUP_H_

#include "st_console.h"

#include <stdbool.h>

struct st_command_line;
struct st_settings;

struct st_console_startup {
	struct st_console_config config;
	bool fullscreen;
};

int st_console_startup_configure(struct st_console_startup *result,
	const struct st_settings *settings, const struct st_command_line *command);

#endif
