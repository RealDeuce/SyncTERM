/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_session_history.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, \
		    #expression); \
		return EXIT_FAILURE; \
	} \
} while (false)

int
main(void)
{
	st_session_history_t *history = NULL;
	struct st_console_cell source[8] = {0};
	struct st_console_cell *copy = NULL;
	uint32_t width;
	size_t lines;
	enum st_screen_mode mode;
	bool status;

	for (size_t i = 0; i < 8; ++i)
		source[i].codepoint = (uint32_t)('A' + i);
	CHECK(st_session_history_create(&history, 3) == 0);
	CHECK(st_session_history_publish(history, source, 2, 4,
	    ST_SCREEN_MODE_C64, true) == 0);
	CHECK(st_session_history_copy(history, &copy, &width, &lines, &mode,
	    &status) == 0);
	CHECK(width == 2 && lines == 3 && mode == ST_SCREEN_MODE_C64 && status);
	CHECK(copy[0].codepoint == 'C' && copy[5].codepoint == 'H');
	free(copy);
	CHECK(st_session_history_set_capacity(history, 2) == 0);
	CHECK(st_session_history_capacity(history) == 2);
	CHECK(st_session_history_copy(history, &copy, &width, &lines, &mode,
	    &status) == 0);
	CHECK(lines == 2 && copy[0].codepoint == 'E');
	free(copy);
	st_session_history_clear(history);
	CHECK(st_session_history_copy(history, &copy, &width, &lines, &mode,
	    &status) != 0);
	st_session_history_destroy(history);
	return EXIT_SUCCESS;
}
