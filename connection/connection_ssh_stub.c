/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "connection_internal.h"

char *st_connection_ssh_public_key(void)
{
	return NULL;
}

int
st_connection_connect_ssh(st_connection_t **result,
	const struct st_profile *profile, const struct st_screen_geometry *current,
	const struct st_connection_hooks *hooks, struct st_connection_error *error)
{
	if (result == NULL || profile == NULL || profile->address[0] == '\0'
	    || (profile->connection_type != ST_CONNECTION_SSH
	    && profile->connection_type != ST_CONNECTION_SSH_NO_AUTH))
		return IO_ERROR_INVALID_ARGUMENT;
	(void)current;
	(void)hooks;
	return st_connection_fail(error, IO_ERROR_NOT_SUPPORTED,
	    ST_CONNECTION_SETUP_NONE, "SSH requires a cryptographic provider");
}
