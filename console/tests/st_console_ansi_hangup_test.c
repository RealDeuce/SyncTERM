/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_ansi_system.h"

#if ST_CONSOLE_TEST_HAVE_PTY_H
#include <pty.h>
#elif ST_CONSOLE_TEST_HAVE_UTIL_H
#include <util.h>
#elif ST_CONSOLE_TEST_HAVE_LIBUTIL_H
#include <libutil.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "failed at %d: %s\n", __LINE__, #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (false)

int
main(void)
{
	struct st_console_ansi_system *system = NULL;
	int master, slave;
	int saved_input = dup(STDIN_FILENO);
	bool available = false;
	unsigned char byte;
	size_t received;
	int result;

	CHECK(saved_input >= 0);
	CHECK(openpty(&master, &slave, NULL, NULL, NULL) == 0);
	CHECK(dup2(slave, STDIN_FILENO) == STDIN_FILENO);
	CHECK(close(slave) == 0);
	CHECK(st_console_ansi_system_open(&system, &available, true) == 0);
	CHECK(available);
	CHECK(close(master) == 0);
	result = st_console_ansi_system_read(system, &byte, 1, &received, 25);
	st_console_ansi_system_close(system);
	CHECK(dup2(saved_input, STDIN_FILENO) == STDIN_FILENO);
	CHECK(close(saved_input) == 0);
	fprintf(stderr, "hangup: result=%d received=%zu\n", result, received);
	/* Disconnection must be distinguishable from an ordinary idle timeout. */
	CHECK(result != 0);
	return EXIT_SUCCESS;
}
