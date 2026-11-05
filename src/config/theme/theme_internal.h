/* Internal theme directory enumeration. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_THEME_INTERNAL_H_
#define ST_THEME_INTERNAL_H_

#include "st_settings.h"
#include "st_theme.h"

#include <stddef.h>
#include <stdio.h>

struct st_theme_directory_entry {
	char *name;
};

struct st_theme_directory {
	struct st_theme_directory_entry *entries;
	size_t count;
};

int st_theme_directory_read(struct st_theme_directory *result,
	const char *path);
void st_theme_directory_dispose(struct st_theme_directory *directory);

/* Takes ownership of stream and closes it on every return path. */
int theme_read_stream_exact(FILE *stream, size_t size, char **result);
int theme_read_file(const char *path, char **result, size_t *result_size);

struct st_theme_manager {
	char *directory;
	char *system_cache_directory;
	struct st_theme *default_theme;
	struct st_theme *committed_theme;
	struct st_theme *preview_theme;
	struct st_theme_catalog_entry *catalog;
	size_t catalog_count;
	struct st_cloud_theme_entry *cloud_catalog;
	size_t cloud_catalog_count;
	uint64_t cloud_catalog_generation;
	uint64_t generation;
	struct st_settings classic_settings;
};

void theme_free(struct st_theme *theme);
struct st_theme *theme_clone(const struct st_theme *source);
struct st_theme_style *find_style(struct st_theme *theme, const char *role);
struct st_theme_style *add_style(struct st_theme *theme, const char *role);
struct st_theme_glyph *find_glyph(struct st_theme *theme, const char *name);
struct st_theme_glyph *add_glyph(struct st_theme *theme, const char *name);
bool resolve_glyph(struct st_theme_glyph *glyph);
struct st_theme *build_classic_theme(const struct st_theme *default_theme,
	const struct st_settings *settings);
int load_file(const struct st_theme_manager *manager, const char *filename,
	struct st_theme **result, char *error, size_t error_size);
int load_path(const struct st_theme_manager *manager, const char *path,
	const char *filename, struct st_theme **result, char *error,
	size_t error_size);
void install_theme(struct st_theme_manager *manager, struct st_theme *theme);
void st_theme_cloud_dispose(struct st_theme_manager *manager);
int st_theme_cloud_load_package(struct st_theme_manager *manager,
	const char *package, struct st_theme **result, char *error,
	size_t error_size);

#endif
