/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Windows roots must exist and identify directories. */
#include "filesystem.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdio.h>

int
main(void)
{
	DWORD drives = GetLogicalDrives();
	bool existing_checked = false;

	if (drives == 0)
		return 1;
	for (unsigned int index = 0; index < 26; ++index) {
		if ((drives & ((DWORD)1 << index)) == 0)
			continue;
		char root[] = "A:\\";
		struct fs_info info;
		root[0] = (char)('A' + index);
		if (fs_get_info(&info, root) == 0
		    && info.kind == FS_ENTRY_DIRECTORY) {
			if (fs_create_directories(root) != 0) {
				fprintf(stderr, "rejected existing root %s\n", root);
				return 1;
			}
			existing_checked = true;
			break;
		}
	}
	if (!existing_checked) {
		fprintf(stderr, "no accessible existing drive root available\n");
		return 1;
	}
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
		char child[] = "A:\\file";
		child[0] = root[0];
		if (fs_create_parent_directories(child) == 0) {
			fprintf(stderr, "reported parent success for missing root %s\n",
			    root);
			return 1;
		}
		return 0;
	}
	fprintf(stderr, "no absent drive available for root test\n");
	return 77;
}
