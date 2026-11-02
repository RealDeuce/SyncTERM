/* Downloaded SyncTERM theme catalog and cache. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_theme.h"

#include "filesystem.h"
#include "ini.h"
#include "io_error.h"
#include "path.h"
#include "st_hex.h"
#include "st_webget.h"
#include "st_unicode_compare.h"
#include "string_compat.h"
#include "string_util.h"
#include "theme_internal.h"
#include "st_crypto.h"
#include "st_digest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ST_THEME_MANIFEST_URL
#define ST_THEME_MANIFEST_URL \
	"http://syncterm.bbsdev.net/themes/manifest.lst"
#endif

static bool
text_equal_span(const char *left, size_t left_size, const char *right)
{
	bool equal;

	return st_unicode_caseless_equal(&equal, left, left_size, right,
	    strlen(right)) == 0 && equal;
}

static bool
text_equal(const char *left, const char *right)
{
	return text_equal_span(left, strlen(left), right);
}

static const char *
section_suffix(const char *name, const char *prefix)
{
	const char *colon = strchr(name, ':');

	return colon != NULL && text_equal_span(name, (size_t)(colon - name),
	    prefix) ? colon + 1 : NULL;
}

#define ST_THEME_REPOSITORY "official"

static void
cloud_error(char *error, size_t error_size, const char *message)
{
	if (error != NULL && error_size != 0)
		(void)snprintf(error, error_size, "%s", message);
}

static void
free_cloud_entry(struct st_cloud_theme_entry *entry)
{
	free(entry->package);
	free(entry->name);
	free(entry->author);
	free(entry->description);
	free(entry->version);
	free(entry->uri);
	free(entry->sha256);
	free(entry->error);
	theme_free(entry->theme);
	*entry = (struct st_cloud_theme_entry){0};
}

void
st_theme_cloud_dispose(struct st_theme_manager *manager)
{
	if (manager == NULL)
		return;
	for (size_t i = 0; i < manager->cloud_catalog_count; ++i)
		free_cloud_entry(&manager->cloud_catalog[i]);
	free(manager->cloud_catalog);
	manager->cloud_catalog = NULL;
	manager->cloud_catalog_count = 0;
}

bool
st_theme_valid_package(const char *package)
{
	static const char prefix[] = ST_THEME_REPOSITORY "/";
	const unsigned char *id;

	if (package == NULL || strncmp(package, prefix, sizeof(prefix) - 1) != 0)
		return false;
	id = (const unsigned char *)package + sizeof(prefix) - 1;
	if (*id == '\0')
		return false;
	for (; *id != '\0'; ++id) {
		if (!(*id >= 'a' && *id <= 'z') && !(*id >= '0' && *id <= '9')
		    && *id != '.' && *id != '_' && *id != '-')
			return false;
	}
	return true;
}

static const char *
package_id(const char *package)
{
	return st_theme_valid_package(package) ? strchr(package, '/') + 1 : NULL;
}

static int
cloud_directory(struct st_theme_manager *manager, bool create, char **result)
{
	char *themes = NULL;
	char *directory = NULL;
	int status;

	if (manager->system_cache_directory == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = path_join_alloc(&themes, manager->system_cache_directory,
	    "themes");
	if (status == 0)
		status = path_join_alloc(&directory, themes, ST_THEME_REPOSITORY);
	free(themes);
	if (status == 0 && create)
		status = fs_create_directories(directory);
	if (status != 0) {
		free(directory);
		return status;
	}
	*result = directory;
	return 0;
}

static int
cloud_file_path(struct st_theme_manager *manager, const char *name,
	bool create, char **result)
{
	char *directory = NULL;
	int status = cloud_directory(manager, create, &directory);

	if (status == 0)
		status = path_join_alloc(result, directory, name);
	free(directory);
	return status;
}

static int
package_path(struct st_theme_manager *manager, const char *package,
	bool create, char **result)
{
	const char *id = package_id(package);
	char *name;
	int status;

	if (id == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	name = malloc(strlen(id) + 5);
	if (name == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	(void)sprintf(name, "%s.ini", id);
	status = cloud_file_path(manager, name, create, result);
	free(name);
	return status;
}

int
st_theme_cloud_load_package(struct st_theme_manager *manager,
	const char *package, struct st_theme **result, char *error,
	size_t error_size)
{
	char *path = NULL;
	struct st_theme *theme = NULL;
	int status = package_path(manager, package, false, &path);

	if (status == 0)
		status = load_path(manager, path, "", &theme, error, error_size);
	free(path);
	if (status == 0) {
		theme->package = strdup(package);
		if (theme->package == NULL)
			status = IO_ERROR_OUT_OF_MEMORY;
	}
	if (status != 0) {
		theme_free(theme);
		return status;
	}
	*result = theme;
	return 0;
}

static bool
hash_matches(const char *path, const char *expected)
{
	unsigned char expected_bytes[32];
	unsigned char actual[32];
	char *data = NULL;
	size_t size = 0;
	int status;

	if (expected == NULL || strlen(expected) != 64
	    || !st_hex_decode(expected_bytes, sizeof(expected_bytes), expected, 64))
		return false;
	status = theme_read_file(path, &data, &size);
	if (status == 0)
		status = st_digest(ST_DIGEST_SHA256, data, size, actual,
		    sizeof(actual));
	free(data);
	return status == ST_CRYPTO_OK
	    && memcmp(expected_bytes, actual, sizeof(actual)) == 0;
}

static bool
copy_optional(char **destination, const char *value)
{
	*destination = value == NULL ? NULL : strdup(value);
	return value == NULL || *destination != NULL;
}

static int
append_manifest_entry(struct st_cloud_theme_entry **entries, size_t *count,
	const char *id, ini_section_t *section, char *error, size_t error_size)
{
	const char *values[6] = {0};
	static const char *const keys[] = {
		"Name", "Author", "Description", "Version", "URI", "SHA256",
	};
	unsigned seen = 0;

	for (size_t i = 0; i < ini_section_count(section); ++i) {
		const struct name_value *pair = ini_section_at(section, i);
		for (unsigned field = 0; field < sizeof(keys) / sizeof(keys[0]);
		    ++field) {
			if (!text_equal(pair->name, keys[field]))
				continue;
			if ((seen & (1u << field)) != 0) {
				cloud_error(error, error_size,
				    "duplicate field in theme manifest");
				return IO_ERROR_INVALID_FORMAT;
			}
			seen |= 1u << field;
			values[field] = pair->value;
			break;
		}
	}
	size_t package_size = strlen(ST_THEME_REPOSITORY) + strlen(id) + 2;
	char *package = malloc(package_size);
	if (package == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	(void)snprintf(package, package_size, "%s/%s", ST_THEME_REPOSITORY, id);
	unsigned char hash[32];
	if (!st_theme_valid_package(package) || values[0] == NULL
	    || values[0][0] == '\0' || values[4] == NULL
	    || (ascii_case_ncompare(values[4], "http://", 7) != 0
	    && ascii_case_ncompare(values[4], "https://", 8) != 0)
	    || values[5] == NULL || strlen(values[5]) != 64
	    || !st_hex_decode(hash, sizeof(hash), values[5], 64)) {
		free(package);
		cloud_error(error, error_size, "invalid theme manifest entry");
		return IO_ERROR_INVALID_FORMAT;
	}
	for (size_t i = 0; i < *count; ++i) {
		if (strcmp((*entries)[i].package, package) == 0) {
			free(package);
			cloud_error(error, error_size,
			    "duplicate theme manifest section");
			return IO_ERROR_INVALID_FORMAT;
		}
	}
	struct st_cloud_theme_entry *resized = realloc(*entries,
	    (*count + 1) * sizeof((*entries)[0]));
	if (resized == NULL) {
		free(package);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	*entries = resized;
	struct st_cloud_theme_entry *entry = &resized[*count];
	*entry = (struct st_cloud_theme_entry){.package = package};
	if (!copy_optional(&entry->name, values[0])
	    || !copy_optional(&entry->author, values[1])
	    || !copy_optional(&entry->description, values[2])
	    || !copy_optional(&entry->version, values[3])
	    || !copy_optional(&entry->uri, values[4])
	    || !copy_optional(&entry->sha256, values[5])) {
		free_cloud_entry(entry);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	++*count;
	return 0;
}

static int
cloud_compare(const void *left_ptr, const void *right_ptr)
{
	const struct st_cloud_theme_entry *left = left_ptr;
	const struct st_cloud_theme_entry *right = right_ptr;
	int value;

	if (st_unicode_collate(&value, left->name, strlen(left->name),
	    right->name, strlen(right->name),
	    ST_UNICODE_COLLATION_CASE_INSENSITIVE) != 0)
		value = strcmp(left->name, right->name);

	return value != 0 ? value : strcmp(left->package, right->package);
}

static void
free_entries(struct st_cloud_theme_entry *entries, size_t count)
{
	for (size_t i = 0; i < count; ++i)
		free_cloud_entry(&entries[i]);
	free(entries);
}

static int
parse_manifest(const char *path, struct st_cloud_theme_entry **result,
	size_t *result_count, char *error, size_t error_size)
{
	ini_document_t *document = NULL;
	struct st_cloud_theme_entry *entries = NULL;
	size_t count = 0;
	FILE *stream = NULL;
	unsigned repository_count = 0;
	int status = fs_open(&stream, path, "rb");

	if (status == 0)
		status = ini_document_read(&document, stream);
	if (stream != NULL)
		(void)fclose(stream);
	if (status != 0)
		goto failed;
	ini_section_t *root = ini_document_root(document);
	for (size_t i = 0; i < ini_section_count(root); ++i) {
		const struct name_value *pair = ini_section_at(root, i);
		if (text_equal(pair->name, "Repository")) {
			++repository_count;
			if (strcmp(pair->value, ST_THEME_REPOSITORY) != 0)
				repository_count += 2;
		}
	}
	if (repository_count != 1) {
		status = IO_ERROR_INVALID_FORMAT;
		cloud_error(error, error_size,
		    "theme manifest has an invalid Repository");
		goto failed;
	}
	for (size_t i = 0; i < ini_document_section_count(document); ++i) {
		ini_section_t *section = ini_document_section_at(document, i);
		const char *name = ini_section_name(section);
		const char *id = section_suffix(name, "Theme");

		if (id == NULL)
			continue;
		status = append_manifest_entry(&entries, &count, id, section,
		    error, error_size);
		if (status != 0)
			goto failed;
	}
	if (count > 1)
		qsort(entries, count, sizeof(entries[0]), cloud_compare);
	ini_document_free(&document);
	*result = entries;
	*result_count = count;
	return 0;

failed:
	if (status != IO_ERROR_INVALID_FORMAT)
		cloud_error(error, error_size, "unable to read theme manifest");
	ini_document_free(&document);
	free_entries(entries, count);
	return status;
}

static struct st_cloud_theme_entry *
find_entry(struct st_theme_manager *manager, const char *package)
{
	for (size_t i = 0; i < manager->cloud_catalog_count; ++i)
		if (strcmp(manager->cloud_catalog[i].package, package) == 0)
			return &manager->cloud_catalog[i];
	return NULL;
}

static void
set_entry_error(struct st_cloud_theme_entry *entry, const char *message)
{
	char *copy = message == NULL ? NULL : strdup(message);
	if (message != NULL && copy == NULL)
		return;
	free(entry->error);
	entry->error = copy;
}

static int
load_cached(struct st_theme_manager *manager,
	struct st_cloud_theme_entry *entry)
{
	char *path = NULL;
	char error[256] = "";
	struct fs_info info;
	int status = package_path(manager, entry->package, false, &path);

	theme_free(entry->theme);
	entry->theme = NULL;
	entry->cached = status == 0 && fs_get_info(&info, path) == 0
	    && info.kind == FS_ENTRY_FILE;
	entry->update_available = entry->cached
	    && !hash_matches(path, entry->sha256);
	if (!entry->cached || entry->update_available) {
		free(path);
		return IO_ERROR_NOT_FOUND;
	}
	status = load_path(manager, path, "", &entry->theme, error,
	    sizeof(error));
	free(path);
	if (status == 0) {
		entry->theme->package = strdup(entry->package);
		if (entry->theme->package == NULL)
			status = IO_ERROR_OUT_OF_MEMORY;
	}
	if (status != 0) {
		theme_free(entry->theme);
		entry->theme = NULL;
		set_entry_error(entry, error[0] == '\0' ? "invalid cached theme"
		    : error);
	}
	else
		set_entry_error(entry, NULL);
	return status;
}

struct download_validation {
	struct st_theme_manager *manager;
	const char *sha256;
};

static int
validate_download(const char *path, void *context_ptr)
{
	struct download_validation *context = context_ptr;
	struct st_theme *theme = NULL;
	int status;

	if (!hash_matches(path, context->sha256))
		return IO_ERROR_INVALID_FORMAT;
	status = load_path(context->manager, path, "", &theme, NULL, 0);
	theme_free(theme);
	return status;
}

static int
download_entry(struct st_theme_manager *manager,
	struct st_cloud_theme_entry *entry, char *error, size_t error_size)
{
	const char *id = package_id(entry->package);
	char *directory = NULL;
	char *output = NULL;
	char *cache_key = NULL;
	struct download_validation validation = {manager, entry->sha256};
	struct st_webget_request request = {0};
	struct st_webget_result result;
	int status;

	if (id == NULL || entry->uri == NULL || entry->sha256 == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = cloud_directory(manager, true, &directory);
	output = malloc(strlen(id) + 5);
	cache_key = malloc(strlen(id) + 7);
	if (status != 0 || output == NULL || cache_key == NULL) {
		status = status == 0 ? IO_ERROR_OUT_OF_MEMORY : status;
		goto finished;
	}
	(void)sprintf(output, "%s.ini", id);
	(void)sprintf(cache_key, "theme-%s", id);
	request = (struct st_webget_request){
		.url = entry->uri,
		.cache_directory = directory,
		.cache_key = cache_key,
		.output_name = output,
		.connect_timeout_ms = 10000,
		.io_timeout_ms = 30000,
		.maximum_size = ST_WEBGET_DEFAULT_MAX_SIZE,
		.validate = validate_download,
		.validate_context = &validation,
	};
	status = st_webget(&result, &request);
	if (status == 0)
		status = load_cached(manager, entry);
	if (status != 0)
		cloud_error(error, error_size,
		    "unable to download or validate the theme package");

finished:
	free(directory);
	free(output);
	free(cache_key);
	return status;
}

static int
validate_manifest_download(const char *path, void *context)
{
	struct st_cloud_theme_entry *entries = NULL;
	size_t count = 0;
	int status = parse_manifest(path, &entries, &count, NULL, 0);

	(void)context;
	free_entries(entries, count);
	return status;
}

int
st_theme_cloud_refresh(struct st_theme_manager *manager, bool fetch,
	const char *selected_package, char *warning, size_t warning_size)
{
	char *directory = NULL;
	char *manifest = NULL;
	struct st_cloud_theme_entry *entries = NULL;
	size_t count = 0;
	int status;

	if (manager == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (warning != NULL && warning_size != 0)
		warning[0] = '\0';
	status = cloud_directory(manager, fetch, &directory);
	if (status == 0)
		status = path_join_alloc(&manifest, directory, "manifest.lst");
	if (status != 0)
		goto finished;
	if (fetch) {
		struct st_webget_request request = {
			.url = ST_THEME_MANIFEST_URL,
			.cache_directory = directory,
			.cache_key = "theme-manifest",
			.output_name = "manifest.lst",
			.connect_timeout_ms = 10000,
			.io_timeout_ms = 30000,
			.maximum_size = ST_WEBGET_DEFAULT_MAX_SIZE,
			.validate = validate_manifest_download,
		};
		struct st_webget_result result;
		status = st_webget(&result, &request);
		if (status != 0)
			cloud_error(warning, warning_size,
			    "unable to update the online theme catalog");
	}
	status = parse_manifest(manifest, &entries, &count, warning, warning_size);
	if (status != 0)
		goto finished;
	st_theme_cloud_dispose(manager);
	manager->cloud_catalog = entries;
	manager->cloud_catalog_count = count;
	entries = NULL;
	count = 0;
	for (size_t i = 0; i < manager->cloud_catalog_count; ++i)
		(void)load_cached(manager, &manager->cloud_catalog[i]);
	if (selected_package != NULL && selected_package[0] != '\0'
	    && find_entry(manager, selected_package) == NULL
	    && st_theme_valid_package(selected_package)) {
		struct st_cloud_theme_entry *resized = realloc(manager->cloud_catalog,
		    (manager->cloud_catalog_count + 1)
		    * sizeof(manager->cloud_catalog[0]));
		if (resized != NULL) {
			manager->cloud_catalog = resized;
			struct st_cloud_theme_entry *entry =
			    &resized[manager->cloud_catalog_count];
			*entry = (struct st_cloud_theme_entry){
				.package = strdup(selected_package),
				.name = strdup(selected_package),
				.error = strdup("the selected package is not in the manifest"),
			};
			if (entry->package != NULL && entry->name != NULL
			    && entry->error != NULL)
				++manager->cloud_catalog_count;
			else
				free_cloud_entry(entry);
		}
	}
	status = 0;

finished:
	free_entries(entries, count);
	free(directory);
	free(manifest);
	return status;
}

size_t
st_theme_cloud_count(const struct st_theme_manager *manager)
{
	return manager == NULL ? 0 : manager->cloud_catalog_count;
}

const struct st_cloud_theme_entry *
st_theme_cloud_at(const struct st_theme_manager *manager, size_t index)
{
	return manager == NULL || index >= manager->cloud_catalog_count ? NULL
	    : &manager->cloud_catalog[index];
}

int
st_theme_cloud_cache(struct st_theme_manager *manager, const char *package,
	char *error, size_t error_size)
{
	struct st_cloud_theme_entry *entry;

	if (manager == NULL || !st_theme_valid_package(package))
		return IO_ERROR_INVALID_ARGUMENT;
	entry = find_entry(manager, package);
	if (entry == NULL) {
		cloud_error(error, error_size,
		    "theme package is not in the manifest");
		return IO_ERROR_NOT_FOUND;
	}
	if (load_cached(manager, entry) == 0)
		return 0;
	int status = download_entry(manager, entry, error, error_size);
	if (status != 0)
		set_entry_error(entry, error);
	return status;
}

int
st_theme_cloud_preview(struct st_theme_manager *manager, const char *package,
	char *error, size_t error_size)
{
	struct st_cloud_theme_entry *entry = manager == NULL ? NULL
	    : find_entry(manager, package);
	struct st_theme *theme;

	if (entry == NULL || entry->theme == NULL) {
		cloud_error(error, error_size, entry != NULL && entry->error != NULL
		    ? entry->error : "theme package is not cached");
		return IO_ERROR_NOT_FOUND;
	}
	theme = theme_clone(entry->theme);
	if (theme == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	theme_free(manager->preview_theme);
	manager->preview_theme = theme;
	++manager->generation;
	return 0;
}

int
st_theme_cloud_select(struct st_theme_manager *manager, const char *package,
	char *error, size_t error_size)
{
	int status = st_theme_cloud_cache(manager, package, error, error_size);
	struct st_cloud_theme_entry *entry;
	struct st_theme *theme;

	if (status != 0)
		return status;
	entry = find_entry(manager, package);
	theme = theme_clone(entry->theme);
	if (theme == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	install_theme(manager, theme);
	return 0;
}

int
st_theme_cloud_delete(struct st_theme_manager *manager, const char *package,
	char *error, size_t error_size)
{
	struct st_cloud_theme_entry *entry = manager == NULL ? NULL
	    : find_entry(manager, package);
	char *path = NULL;
	int status;

	if (entry == NULL)
		return IO_ERROR_NOT_FOUND;
	status = package_path(manager, package, false, &path);
	if (status == 0)
		status = fs_remove_file(path);
	free(path);
	if (status != 0) {
		cloud_error(error, error_size, "unable to delete cached theme");
		return status;
	}
	entry->cached = false;
	entry->update_available = false;
	theme_free(entry->theme);
	entry->theme = NULL;
	set_entry_error(entry, NULL);
	return 0;
}

int
st_theme_cloud_copy_document(struct st_theme_document **result,
	struct st_theme_manager *manager, const char *package, char *error,
	size_t error_size)
{
	char *path = NULL;
	int status = st_theme_cloud_cache(manager, package, error, error_size);

	if (status == 0)
		status = package_path(manager, package, false, &path);
	if (status == 0)
		status = st_theme_document_copy_path(result, manager, path, error,
		    error_size);
	free(path);
	return status;
}
