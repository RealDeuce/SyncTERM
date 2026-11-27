/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_DDOFX_PACKAGE_H_
#define ST_DDOFX_PACKAGE_H_

#include <stdbool.h>

/* resolve supplies an allocated destination beneath directory for a DOS basename. */
typedef int (*st_ddofx_member_path)(char **result, const char *directory, const char *member);

struct st_ddofx_autoplay {
	char     name[13];
	unsigned rate;
};

/* The transfer worker owns the batch until join. The session then handles
 * the first RealPlay request on its audio owner thread. */
struct st_ddofx_package_batch {
	const char              *root;
	int                      (*directory)(char **result, const char *root, const char *category);
	st_ddofx_member_path     resolve;
	struct st_ddofx_autoplay autoplay;

	struct {
		const char *category;
		char       *path;
	} files[50];

	unsigned count;
};

int st_ddofx_remember_package(void *, const char *filename, const char *path);
int st_ddofx_install_packages(void *, bool complete);

#endif
