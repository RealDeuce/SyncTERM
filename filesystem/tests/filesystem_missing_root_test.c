/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Windows-only audit regression. Link with syncterm_filesystem and its
 * dependencies. No files or directories are created: only an absent drive
 * root is passed to fs_create_directories. Expected to fail on current code.
 */
#include "filesystem.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

int
main(void)
{
	DWORD drives = GetLogicalDrives();
	if (drives == 0)
		return 2;
	for (unsigned int index = 0; index < 26; ++index) {
		if ((drives & ((DWORD)1 << index)) != 0)
			continue;
		char root[] = "A:\\";
		struct fs_info info;
		root[0] = (char)('A' + index);
		if (fs_get_info(&info, root) == 0)
			continue;
		if (fs_create_directories(root) == 0) {
			fprintf(stderr, "reported success for missing root %s\n", root);
			return 1;
		}
		return 0;
	}
	fprintf(stderr, "no absent drive available for regression test\n");
	return 2;
}
