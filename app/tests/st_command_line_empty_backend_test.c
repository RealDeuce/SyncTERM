/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "st_command_line.h"
#include "io_error.h"

#include <stdlib.h>
#include <string.h>

int
main(void)
{
	char *option = malloc(3);
	char program[] = "syncterm";
	char *arguments[] = {program, option, NULL};
	struct st_command_line parsed;
	int status;

	if (option == NULL)
		return EXIT_FAILURE;
	memcpy(option, "-i", 3);
	status = st_command_line_parse(&parsed, 2, arguments, NULL);
	free(option);
	return status == IO_ERROR_INVALID_ARGUMENT ? EXIT_SUCCESS : EXIT_FAILURE;
}
