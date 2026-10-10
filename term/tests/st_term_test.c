/* Focused policy tests for the minimal connected-session loop. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_term.h"

#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, \
		    #expression); \
		return EXIT_FAILURE; \
	} \
} while (0)

int
main(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config console_config = {
		.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 16,
	};
	struct st_profile profile;
	st_console_t *console = NULL;
	st_connection_t *connection = NULL;
	st_term_session_t *session = NULL;

	CHECK(st_console_create(&console, &display, &console_config) == 0);
	st_profile_init(&profile);
	profile.connection_type = ST_CONNECTION_TELNET;
	CHECK(st_term_connect_profile(&connection, &profile, console, NULL, NULL)
	    == IO_ERROR_NOT_SUPPORTED);
	CHECK(connection == NULL);
	CHECK(st_term_session_create(&session, NULL)
	    == IO_ERROR_INVALID_ARGUMENT && session == NULL);
	st_profile_dispose(&profile);
	st_console_destroy(console);
	return EXIT_SUCCESS;
}
