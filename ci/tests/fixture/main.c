/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <string.h>

int
main(int argc, char **argv)
{
	puts("CI fixture");
	if (argc > 1 && strcmp(argv[1], "fail") == 0)
		return 1;
	if (argc > 1 && strcmp(argv[1], "skip") == 0)
		return 77;
	return 0;
}
