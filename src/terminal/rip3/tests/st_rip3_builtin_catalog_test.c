/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <string.h>

#include "st_rip3_builtin_reference.h"

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

int
main(int argc, char **argv)
{
	const char *const absent[] = {
	    "",
	    "P",
	    "FIRST_NAME",
	    "UNKNOWN",
	    "APP10",
	    "APPX",
	    "SAVE10",
	    "RESTORE10",
	    "FEATURE",
	    "SWITCH",
	    "TERMDATA",
	    "TERMNAME",
	    "TERMMFG",
	    "TERMVER",
	    "VIEWX",
	    "VIEWY",
	    "VIEWX0",
	    "VIEWY0",
	    "VIEWX1",
	    "VIEWY1",
	    "CLIP2CLIPOK",
	    "CLIPSAVEOK",
	    "STACKPOS",
	    "STACKCLEAR",
	    "TEXTQUERY",
	    "VIEWQUERY",
	    "_IFS",
	    "1IFS",
	    "$IFS$",
	    "IFS()",
	    "NULL ",
	    "ABCDEFGHIJKLMNOPQRSTU",
	};
	const char *const present[]      = {"IFS", "NULL", "COPY", "RESX", "RESY", "PROTECT", "HOSTDIR", "SAVE0",
	    "RESTORE0", "APP9", "T"};
	const char        unterminated[] = {'I', 'F', 'S'};
	const char        embedded[]     = {'I', 'F', 'S', 0, 'X'};

	CHECK(!st_rip3_builtin_known(NULL, 0));
	CHECK(!st_rip3_builtin_known(NULL, 3));
	CHECK(st_rip3_builtin_known(unterminated, sizeof(unterminated)));
	CHECK(!st_rip3_builtin_known(embedded, sizeof(embedded)));
	for (size_t i = 0; i < sizeof(absent) / sizeof(*absent); i++)
		CHECK(!st_rip3_builtin_known(absent[i], strlen(absent[i])));
	for (size_t i = 0; i < sizeof(present) / sizeof(*present); i++)
		CHECK(st_rip3_builtin_known(present[i], strlen(present[i])));
	/* The separate contract CTest passes the full pinned-reference inventory,
	 * independently extracted from source blocks rather than this C table. */
	bool denied    = argc > 1 && strcmp(argv[1], "--graphical-denied") == 0;
	bool graphical = denied || (argc > 1 && strcmp(argv[1], "--graphical") == 0);
	CHECK(!st_rip3_builtin_graphical("WORLD", 5) && !st_rip3_builtin_graphical("MKILL", 5));
	for (int i = graphical ? 2 : 1; i < argc; i++) {
		char   mixed[21];
		size_t length = strlen(argv[i]);

		CHECK(length > 0 && length <= 20);
		if (!st_rip3_builtin_known(argv[i], length))
			fprintf(stderr, "missing reference builtin: %s\n", argv[i]);
		CHECK(st_rip3_builtin_known(argv[i], length));
		for (size_t j = 0; j < length; j++)
			mixed[j] = (j % 2 == 0 && argv[i][j] >= 'A' && argv[i][j] <= 'Z')
			               ? (char)(argv[i][j] - 'A' + 'a')
			               : argv[i][j];
		CHECK(st_rip3_builtin_known(mixed, length));
		if (graphical)
			CHECK(st_rip3_builtin_graphical(argv[i], length) != denied
			      && st_rip3_builtin_graphical(mixed, length) != denied);
	}
	return 0;
}
