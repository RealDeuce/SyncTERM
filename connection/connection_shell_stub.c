/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "connection_internal.h"

#include "io_error.h"

int
st_connection_connect_shell(st_connection_t **result,
	const struct st_profile *profile,
	const struct st_screen_geometry *current_screen,
	const struct st_connection_hooks *hooks,
	struct st_connection_error *error)
{
	(void)result;
	(void)profile;
	(void)current_screen;
	(void)hooks;
	return st_connection_fail(error, IO_ERROR_NOT_SUPPORTED,
	    ST_CONNECTION_SETUP_STARTING_SHELL,
	    "this target does not provide a supported pseudo-terminal API");
}
