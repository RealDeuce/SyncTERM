/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <string.h>

int
main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "features") == 0) {
		printf("test_control=%d testing=%d\n", ST_CONSOLE_TEST, BUILD_TESTING);
		return 0;
	}
	puts("CI fixture");
	if (!ST_CONSOLE_TEST || !BUILD_TESTING) return 2;
	if (argc > 1 && strcmp(argv[1], "fail") == 0)
		return 1;
	if (argc > 1 && strcmp(argv[1], "skip") == 0)
		return 77;
	return 0;
}
