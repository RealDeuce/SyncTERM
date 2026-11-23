/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_RESOURCES_H_
#define ST_RIP3_RESOURCES_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

/* Literal bare-name policy shared by file lookup and button filename slots.
 * Expansion is a separate ordinary-text consumer; this never opens a file. */
bool st_rip3_resource_name_valid(const char *name);

enum st_rip3_resource_kind {
	ST_RIP3_RESOURCE_MEDIA,
	ST_RIP3_RESOURCE_FONT,
};

/* Trusted client paths, borrowed for each resource operation. NULL/empty
 * directories are absent. A protocol-selected host directory is a direct
 * child of its per-BBS RIP cache, supplied as named_host_root. */
struct st_rip3_resource_paths {
	const char *host;
	const char *shared;
	const char *fonts;
	const char *named_host_root;
};

/* Create the per-BBS RIP cache and optionally its direct child.
 * Neither path can be a symlink or reparse point. The caller owns the paths
 * and supplies an existing trusted parent for the root. */
int st_rip3_resource_prepare_cache(const char *root, const char *child);

struct st_rip3_resource {
	FILE *stream;
	char *path;
};

/* Exclusively create a client-owned scratch file directly in the BBS RIP
 * cache. Removal requires a scratch resource returned by this function. */
int st_rip3_resource_scratch(struct st_rip3_resource *, const struct st_rip3_resource_paths *);
int st_rip3_resource_read_scratch(struct st_rip3_resource *, const struct st_rip3_resource *);
void st_rip3_resource_remove_scratch(struct st_rip3_resource *);

/*
 * Read-only storage service, not a terminal/parser processing entry point.
 * Media: active BBS cache, then the global RIP directory. Fonts: fonts only.
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
/* File queries stop at the first matching media file, even if its open fails.
 * matched distinguishes that failure from a miss, including on a name race. */
int st_rip3_resource_open_first(struct st_rip3_resource *result, bool *matched,
	const struct st_rip3_resource_paths *paths, const char *name);
/* Return success for a unique regular media file without opening it or
 * following a final symlink/reparse point. Uses the media lookup order. */
int st_rip3_resource_exists(const struct st_rip3_resource_paths *paths, const char *name);
/* Write complete resource bytes to the explicitly bound trusted host directory.
 * The BBS's cache bucket must exist. The RIP directory and its selected child
 * can be created on demand. Shared and font directories are never
 * written, including after any error or when host is absent (NOT_SUPPORTED).
 * The owner must bind a client-owned session directory; host text cannot select
 * that path. Name validation/caseless matching use the same UTF-8 namespace as
 * reads. Existing unique regular files retain their native spelling. Ambiguous
 * matches, directories, links/reparse points and other non-files reject with
 * CONFLICT. Bytes may be NULL only for an empty file. Replacement is atomic:
 * pre-rename failures retain prior contents. This is a storage status for the
 * owning parser to consume, never a terminal/session processing return.
 * No clipboard/port capture, encoding, reset or connection setup is implied. */
int st_rip3_resource_write(const struct st_rip3_resource_paths *paths,
	const char *name, const void *bytes, size_t size);
/* Remove a unique regular file from the active BBS RIP directory. Uses the
 * same bare-name and caseless matching as writes. No fallback or creation. */
int st_rip3_resource_remove(const struct st_rip3_resource_paths *paths, const char *name);
void st_rip3_resource_dispose(struct st_rip3_resource *resource);

#endif
