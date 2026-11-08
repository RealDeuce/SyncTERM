/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int calls;
int arguments_main(int argc, char **argv)
{
	FILE *file = fopen("marker", "rb");
	if (file == NULL)
		return 1;
	(void)fclose(file);
	const char *value = getenv("CASE_ENV");
	return CASE_VALUE == 2 && ++calls == 1 && value != NULL
	    && strcmp(value, "present") == 0 && argc == 3
	    && strcmp(argv[0], "arguments") == 0
	    && strcmp(argv[1], "space argument") == 0
	    && strcmp(argv[2], "--marker") == 0 ? 0 : 1;
}
