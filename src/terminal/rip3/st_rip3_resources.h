/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_RESOURCES_H_
#define ST_RIP3_RESOURCES_H_

#include <stddef.h>
#include <stdio.h>

enum st_rip3_resource_kind {
	ST_RIP3_RESOURCE_MEDIA,
	ST_RIP3_RESOURCE_FONT,
};

/* Trusted client paths, borrowed for the duration of open(). NULL/empty
 * directories are absent. Search entries are already split and ordered by
 * client configuration; no host-supplied path or directory switch is accepted. */
struct st_rip3_resource_paths {
	const char *host;
	const char *shared;
	const char *fonts;
	const char *const *search;
	size_t search_count;
};

struct st_rip3_resource {
	FILE *stream;
	char *path;
};

/*
 * Read-only storage service, not a terminal/parser processing entry point.
 * Media: host, shared, then ordered search entries. Fonts: fonts only.
 * name is a bare UTF-8 filename, with no wildcard or path syntax. Matching
 * uses the client's locale-independent Unicode caseless comparison (ASCII
 * agrees with the historical names). Multiple matching files reject that tier
 * with IO_ERROR_CONFLICT rather than relying on enumeration order.
 * Unreadable/missing tiers are tried before reporting the first storage error;
 * allocation failures stop immediately. Result is unchanged on failure.
 * A successful result owns an open binary stream positioned at zero and its
 * allocated native path. Dispose it before replacing it with another result.
 * No directories or files are created, modified or deleted.
 */
int st_rip3_resource_open(struct st_rip3_resource *result,
    const struct st_rip3_resource_paths *paths,
    enum st_rip3_resource_kind kind, const char *name);
void st_rip3_resource_dispose(struct st_rip3_resource *resource);

#endif
