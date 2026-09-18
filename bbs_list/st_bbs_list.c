/* Combined personal and system SyncTERM BBS directory. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_bbs_list_internal.h"

#include "array_list.h"
#include "filesystem.h"
#include "io_error.h"
#include "path.h"
#include "st_app_paths.h"
#include "st_file_lock.h"
#include "st_settings.h"
#include "string_compat.h"
#include "string_util.h"
#include "xp_crypto.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

bool
st_bbs_name_equal(const char *left, const char *right)
{
	if (left == NULL || right == NULL)
		return left == right;
	return ascii_case_compare(left, right) == 0;
}

bool
st_bbs_name_valid(const char *name)
{
	size_t length;

	if (name == NULL)
		return false;
	length = strlen(name);
	if (length == 0 || length > ST_PROFILE_NAME_MAX
	    || st_bbs_name_equal(name, "syncterm-system-cache"))
		return false;
	for (size_t i = 0; i < length; ++i) {
		unsigned char value = (unsigned char)name[i];

		if (value < 0x20 || value > 0x7e || value == '[' || value == ']')
			return false;
	}
	return true;
}

static void
free_entry(struct st_bbs_owned_entry *entry)
{
	if (entry == NULL)
		return;
	st_profile_dispose(&entry->profile);
	free(entry->source_name);
	free(entry);
}

static void
free_diagnostic(struct st_bbs_owned_diagnostic *diagnostic)
{
	if (diagnostic == NULL)
		return;
	free(diagnostic->source_name);
	free(diagnostic->section_name);
	free(diagnostic);
}

static void
clear_view(st_bbs_list_t *list)
{
	for (size_t i = 0; i < array_list_count((array_list_t)list->entries); ++i)
		free_entry(list->entries[i]);
	array_list_free((array_list_t *)&list->entries);
	for (size_t i = 0;
	    i < array_list_count((array_list_t)list->diagnostics); ++i)
		free_diagnostic(list->diagnostics[i]);
	array_list_free((array_list_t *)&list->diagnostics);
}

static int
add_diagnostic(st_bbs_list_t *list, enum st_bbs_source source,
    const char *source_name, const char *section_name, int status)
{
	struct st_bbs_owned_diagnostic *item = calloc(1, sizeof(*item));

	if (item == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	item->source_name = source_name == NULL ? NULL : strdup(source_name);
	item->section_name = section_name == NULL ? NULL : strdup(section_name);
	if ((source_name != NULL && item->source_name == NULL)
	    || (section_name != NULL && item->section_name == NULL)) {
		free_diagnostic(item);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	item->public.source = source;
	item->public.source_name = item->source_name;
	item->public.section_name = item->section_name;
	item->public.status = status;
	if (!array_list_append((array_list_t *)&list->diagnostics, item)) {
		free_diagnostic(item);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	return 0;
}

static struct st_bbs_owned_entry *
find_owned(const st_bbs_list_t *list, const char *name)
{
	for (size_t i = 0; i < array_list_count((array_list_t)list->entries); ++i) {
		if (st_bbs_name_equal(list->entries[i]->profile.name, name))
			return list->entries[i];
	}
	return NULL;
}

static int
append_source(st_bbs_list_t *list, ini_document_t *document,
    enum st_bbs_source source, const char *source_name)
{
	for (size_t i = 0; i < ini_document_section_count(document); ++i) {
		ini_section_t *section = ini_document_section_at(document, i);
		const char *name = ini_section_name(section);
		struct st_bbs_owned_entry *existing;
		struct st_bbs_owned_entry *item;
		int status;

		if (!st_bbs_name_valid(name)) {
			status = add_diagnostic(list, source, source_name, name,
			    IO_ERROR_INVALID_FORMAT);
			if (status < 0)
				return status;
			continue;
		}
		existing = find_owned(list, name);
		if (existing != NULL) {
			/* Later duplicate blocks replace earlier blocks in one source. */
			if (existing->public.source != source
			    || !st_bbs_name_equal(existing->source_name, source_name))
				continue;
			st_profile_dispose(&existing->profile);
			status = st_bbs_decode_profile(&existing->profile, section, name,
			    source != ST_BBS_SOURCE_PERSONAL, list->download_path);
			if (status < 0)
				return status;
			continue;
		}
		if (array_list_count((array_list_t)list->entries)
		    >= ST_BBS_LIST_MAX_ENTRIES)
			return IO_ERROR_OUT_OF_RANGE;
		item = calloc(1, sizeof(*item));
		if (item == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		item->source_name = source_name == NULL ? NULL : strdup(source_name);
		if (source_name != NULL && item->source_name == NULL) {
			free(item);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		status = st_bbs_decode_profile(&item->profile, section, name,
		    source != ST_BBS_SOURCE_PERSONAL, list->download_path);
		if (status < 0) {
			free_entry(item);
			return status;
		}
		item->public.profile = &item->profile;
		item->public.source = source;
		item->public.source_name = item->source_name;
		if (!array_list_append((array_list_t *)&list->entries, item)) {
			free_entry(item);
			return IO_ERROR_OUT_OF_MEMORY;
		}
	}
	return 0;
}

static int
read_plain_document(const char *path, ini_document_t **result)
{
	FILE *stream;
	int status = fs_open(&stream, path, "rb");

	if (status < 0)
		return status;
	status = ini_document_read(result, stream);
	if (fclose(stream) != 0 && status == 0)
		status = IO_ERROR_IO;
	return status;
}

static char *
cache_list_path(const st_bbs_list_t *list, const char *name)
{
	char *filename;
	char *path;
	size_t length;

	if (!path_untrusted_component_valid(name))
		return NULL;
	length = strlen(name);
	filename = malloc(length + sizeof(".lst"));
	if (filename == NULL)
		return NULL;
	memcpy(filename, name, length);
	memcpy(filename + length, ".lst", sizeof(".lst"));
	if (path_join_alloc(&path, list->system_cache_path, filename) < 0)
		path = NULL;
	free(filename);
	return path;
}

int
st_bbs_list_rebuild(st_bbs_list_t *list)
{
	ini_document_t *document = NULL;
	ini_section_t *root;
	int status;

	clear_view(list);
	st_profile_dispose(&list->defaults);
	root = ini_document_root(list->personal_document);
	status = st_bbs_decode_profile(&list->defaults, root, NULL, false,
	    list->download_path);
	if (status < 0)
		return status;
	status = append_source(list, list->personal_document,
	    ST_BBS_SOURCE_PERSONAL, NULL);
	if (status < 0)
		return status;
	if (list->shared_path != NULL) {
		status = read_plain_document(list->shared_path, &document);
		if (status == 0) {
			status = append_source(list, document, ST_BBS_SOURCE_SHARED,
			    list->shared_path);
			ini_document_free(&document);
		} else if (status != IO_ERROR_NOT_FOUND) {
			(void)add_diagnostic(list, ST_BBS_SOURCE_SHARED,
			    list->shared_path, NULL, status);
			status = 0;
		} else
			status = 0;
	}
	if (status == 0 && list->cached_system_list != NULL) {
		status = read_plain_document(list->cached_system_list, &document);
		if (status == 0) {
			status = append_source(list, document, ST_BBS_SOURCE_WEB_SYSTEM,
			    "System List");
			ini_document_free(&document);
		} else if (status != IO_ERROR_NOT_FOUND) {
			(void)add_diagnostic(list, ST_BBS_SOURCE_WEB_SYSTEM,
			    "System List", NULL, status);
			status = 0;
		} else
			status = 0;
	}
	for (size_t i = 0; status == 0
	    && i < st_settings_web_download_count(); ++i) {
		const struct name_value *download = st_settings_web_download_at(i);
		char *source_path = cache_list_path(list, download->name);

		if (source_path == NULL) {
			(void)add_diagnostic(list, ST_BBS_SOURCE_WEB_NAMED,
			    download->name, NULL, IO_ERROR_INVALID_ARGUMENT);
			continue;
		}
		status = read_plain_document(source_path, &document);
		free(source_path);
		if (status == 0) {
			status = append_source(list, document, ST_BBS_SOURCE_WEB_NAMED,
			    download->name);
			ini_document_free(&document);
		} else if (status != IO_ERROR_NOT_FOUND) {
			(void)add_diagnostic(list, ST_BBS_SOURCE_WEB_NAMED,
			    download->name, NULL, status);
			status = 0;
		} else {
			status = 0;
		}
	}
	return status;
}

static int
read_personal_document(st_bbs_list_t *list, ini_document_t **result,
    struct st_list_crypto_info *info)
{
	FILE *stream;
	int status = fs_open(&stream, list->personal_path, "rb");

	if (status == IO_ERROR_NOT_FOUND) {
		memset(info, 0, sizeof(*info));
		info->format = ST_LIST_CRYPTO_PLAIN;
		return ini_document_create(result);
	}
	if (status < 0)
		return status;
	status = st_list_crypto_read(result, stream, list->password,
	    list->password_size, 0, info);
	if (fclose(stream) != 0 && status == 0)
		status = IO_ERROR_IO;
	return status;
}

static void
set_write_options(st_bbs_list_t *list)
{
	memset(&list->write_options, 0, sizeof(list->write_options));
	if (list->crypto.format == ST_LIST_CRYPTO_V2) {
		list->write_options.cipher = list->crypto.cipher;
		list->write_options.key_bits = list->crypto.key_bits;
		list->write_options.cost_log2 = list->crypto.cost_log2;
		list->write_options.block_size = list->crypto.block_size;
		list->write_options.parallelism = list->crypto.parallelism;
	}
}

static char *
copy_required(const char *value)
{
	return value == NULL ? NULL : strdup(value);
}

int
st_bbs_list_open(st_bbs_list_t **result, const struct st_app_paths *paths,
    const void *password, size_t password_size)
{
	st_bbs_list_t *list;
	st_file_lock_t *lock = NULL;
	const char *personal_path;
	int status;

	if (result == NULL || paths == NULL || paths->personal_list == NULL
	    || paths->cache_directory == NULL
	    || paths->system_cache_directory == NULL
	    || paths->cached_system_list == NULL
	    || paths->download_directory == NULL
	    || (password_size != 0 && password == NULL))
		return IO_ERROR_INVALID_ARGUMENT;
	list = calloc(1, sizeof(*list));
	if (list == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	st_profile_init(&list->defaults);
	personal_path = st_settings.list_path != NULL
	    && !st_settings.download_system_list
	    ? st_settings.list_path : paths->personal_list;
	list->personal_path = copy_required(personal_path);
	list->shared_path = copy_required(paths->shared_list);
	list->cache_path = copy_required(paths->cache_directory);
	list->system_cache_path = copy_required(paths->system_cache_directory);
	list->cached_system_list = copy_required(paths->cached_system_list);
	list->download_path = copy_required(paths->download_directory);
	if (password_size != 0) {
		list->password = malloc(password_size);
		if (list->password != NULL)
			memcpy(list->password, password, password_size);
		list->password_size = password_size;
	}
	if (list->personal_path == NULL || list->cache_path == NULL
	    || list->system_cache_path == NULL
	    || list->cached_system_list == NULL || list->download_path == NULL
	    || (password_size != 0 && list->password == NULL)) {
		st_bbs_list_close(list);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	status = st_file_lock_acquire(&lock, list->personal_path, false);
	if (status == 0) {
		status = read_personal_document(list, &list->personal_document,
		    &list->crypto);
		st_file_lock_release(lock);
	} else if (status == IO_ERROR_NOT_FOUND) {
		status = read_personal_document(list, &list->personal_document,
		    &list->crypto);
	}
	if (status == 0) {
		set_write_options(list);
		status = st_bbs_list_rebuild(list);
	}
	if (status < 0) {
		st_bbs_list_close(list);
		return status;
	}
	list->generation = 1;
	*result = list;
	return 0;
}

void
st_bbs_list_close(st_bbs_list_t *list)
{
	if (list == NULL)
		return;
	clear_view(list);
	st_profile_dispose(&list->defaults);
	ini_document_free(&list->personal_document);
	free(list->personal_path);
	free(list->shared_path);
	free(list->cache_path);
	free(list->system_cache_path);
	free(list->cached_system_list);
	free(list->download_path);
	free(list->sort_fields);
	xp_crypto_clear(list->password, list->password_size);
	free(list->password);
	free(list);
}

int
st_bbs_list_reload(st_bbs_list_t *list)
{
	ini_document_t *document = NULL;
	struct st_list_crypto_info info;
	st_file_lock_t *lock = NULL;
	int status;

	if (list == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_file_lock_acquire(&lock, list->personal_path, false);
	if (status < 0)
		return status;
	status = read_personal_document(list, &document, &info);
	if (status == 0) {
		ini_document_free(&list->personal_document);
		list->personal_document = document;
		document = NULL;
		list->crypto = info;
		set_write_options(list);
		status = st_bbs_list_rebuild(list);
		if (status == 0 && list->sort_field_count != 0)
			status = st_bbs_list_sort(list, list->sort_fields,
			    list->sort_field_count);
		if (status == 0)
			list->generation++;
	}
	ini_document_free(&document);
	st_file_lock_release(lock);
	return status;
}

size_t
st_bbs_list_count(const st_bbs_list_t *list)
{
	return list == NULL ? 0
	    : array_list_count((array_list_t)list->entries);
}

const struct st_bbs_entry *
st_bbs_list_at(const st_bbs_list_t *list, size_t index)
{
	if (list == NULL || index >= st_bbs_list_count(list))
		return NULL;
	return &list->entries[index]->public;
}

const struct st_bbs_entry *
st_bbs_list_find(const st_bbs_list_t *list, const char *name)
{
	struct st_bbs_owned_entry *entry;

	if (list == NULL || name == NULL)
		return NULL;
	entry = find_owned(list, name);
	return entry == NULL ? NULL : &entry->public;
}

const struct st_profile *
st_bbs_list_defaults(const st_bbs_list_t *list)
{
	return list == NULL ? NULL : &list->defaults;
}

uint64_t
st_bbs_list_generation(const st_bbs_list_t *list)
{
	return list == NULL ? 0 : list->generation;
}

const struct st_list_crypto_info *
st_bbs_list_crypto_info(const st_bbs_list_t *list)
{
	return list == NULL ? NULL : &list->crypto;
}

size_t
st_bbs_list_diagnostic_count(const st_bbs_list_t *list)
{
	return list == NULL ? 0
	    : array_list_count((array_list_t)list->diagnostics);
}

const struct st_bbs_diagnostic *
st_bbs_list_diagnostic_at(const st_bbs_list_t *list, size_t index)
{
	if (list == NULL || index >= st_bbs_list_diagnostic_count(list))
		return NULL;
	return &list->diagnostics[index]->public;
}
