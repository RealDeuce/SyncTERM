/* Combined personal and system SyncTERM BBS directory. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_bbs_list_internal.h"

#include "array_list.h"
#include "filesystem.h"
#include "file_map.h"
#include "io_error.h"
#include "path.h"
#include "st_app_paths.h"
#include "st_file_lock.h"
#include "st_settings.h"
#include "st_unicode_compare.h"
#include "string_compat.h"
#include "string_util.h"
#include "xp_crypto.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef ST_BBS_LIST_TEST_ALLOCATOR
#include "st_bbs_test_allocator.h"
#define calloc st_bbs_test_calloc
#define strdup st_bbs_test_strdup
#endif

bool
st_bbs_name_equal(const char *left, const char *right)
{
	if (left == NULL || right == NULL)
		return left == right;
	bool equal;

	return st_unicode_caseless_equal(&equal, left, strlen(left), right,
	    strlen(right)) == 0 && equal;
}

static bool
bbs_name_valid(const char *name, bool known_normalized)
{
	bool valid = known_normalized
	    ? st_unicode_name_valid_normalized(name,
	    ST_PROFILE_NAME_SCALAR_MAX, ST_PROFILE_NAME_MAX, "[]")
	    : st_unicode_name_valid(name, ST_PROFILE_NAME_SCALAR_MAX,
	    ST_PROFILE_NAME_MAX, "[]");

	return valid && !st_bbs_name_equal(name, "syncterm-system-cache");
}

bool
st_bbs_name_valid(const char *name)
{
	return bbs_name_valid(name, false);
}

bool
st_bbs_name_valid_normalized(const char *name)
{
	return bbs_name_valid(name, true);
}

static void
free_entry(struct st_bbs_owned_entry *entry)
{
	if (entry == NULL)
		return;
	if (--entry->references != 0)
		return;
	free_entry(entry->shadowed);
	st_profile_dispose(&entry->profile);
	free((char *)entry->public.name_key);
	free(entry->source_name);
	free(entry);
}

static int
compare_bytes(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	size_t shared = left_size < right_size ? left_size : right_size;
	int compared = shared == 0 ? 0 : memcmp(left, right, shared);

	if (compared != 0)
		return compared < 0 ? -1 : 1;
	return left_size == right_size ? 0 : left_size < right_size ? -1 : 1;
}

static int
compare_entry_names(const void *left, const void *right)
{
	const struct st_bbs_owned_entry *left_entry = left;
	const struct st_bbs_owned_entry *right_entry = right;

	return compare_bytes(left_entry->public.name_key,
	    left_entry->public.name_key_size, right_entry->public.name_key,
	    right_entry->public.name_key_size);
}

static int
compare_key_to_entry(const void *key_pointer, const void *entry_pointer)
{
	const struct st_bbs_owned_entry *key = key_pointer;
	const struct st_bbs_owned_entry *entry =
	    *(const struct st_bbs_owned_entry *const *)entry_pointer;

	return compare_entry_names(key, entry);
}

const struct st_bbs_entry *
st_bbs_entry_shadowed(const struct st_bbs_entry *entry)
{
	const struct st_bbs_owned_entry *owned = (const void *)entry;

	return owned == NULL || owned->shadowed == NULL ? NULL
	    : &owned->shadowed->public;
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

struct st_bbs_source_snapshot {
	/* Exact bytes, not timestamps: same-size edits within one second count. */
	char *path;
	char *bytes;
	size_t size;
	ini_document_t *document;
	int read_status;
	int status;
	size_t references;
};

static void
release_snapshot(struct st_bbs_source_snapshot *snapshot)
{
	if (snapshot == NULL || --snapshot->references != 0)
		return;
	ini_document_free(&snapshot->document);
	free(snapshot->bytes);
	free(snapshot->path);
	free(snapshot);
}

void
st_bbs_view_dispose(struct st_bbs_view *view)
{
	if (view == NULL)
		return;
	array_list_free((array_list_t *)&view->names);
	for (size_t i = 0; i < array_list_count((array_list_t)view->entries); ++i)
		free_entry(view->entries[i]);
	array_list_free((array_list_t *)&view->entries);
	for (size_t i = 0;
	    i < array_list_count((array_list_t)view->diagnostics); ++i)
		free_diagnostic(view->diagnostics[i]);
	array_list_free((array_list_t *)&view->diagnostics);
	for (size_t i = 0;
	    i < array_list_count((array_list_t)view->snapshots); ++i)
		release_snapshot(view->snapshots[i]);
	array_list_free((array_list_t *)&view->snapshots);
	st_profile_dispose(&view->defaults);
}

static int
add_diagnostic(struct st_bbs_view *view, enum st_bbs_source source,
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
	if (!array_list_append((array_list_t *)&view->diagnostics, item)) {
		free_diagnostic(item);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	return 0;
}

static struct st_bbs_owned_entry *
find_owned_by_key(const struct st_bbs_view *view,
	const struct st_bbs_owned_entry *key)
{
	size_t count = array_list_count((array_list_t)view->names);

	if (count == 0)
		return NULL;
	struct st_bbs_owned_entry **found = bsearch(key, view->names,
	    count, sizeof(*view->names),
	    compare_key_to_entry);

	return found == NULL ? NULL : *found;
}

static int
make_name_key(struct st_bbs_owned_entry *key, const char *name)
{
	char *name_key;
	int status = st_unicode_caseless_key(&name_key,
	    &key->public.name_key_size, name, strlen(name));

	if (status == 0)
		key->public.name_key = name_key;
	return status;
}

static bool
reserved_name_key(const struct st_bbs_owned_entry *key)
{
	static const char reserved[] = "syncterm-system-cache";

	return key->public.name_key_size == sizeof(reserved) - 1
	    && memcmp(key->public.name_key, reserved, sizeof(reserved) - 1) == 0;
}

static struct st_bbs_owned_entry *
find_owned(const struct st_bbs_view *view, const char *name)
{
	struct st_bbs_owned_entry key = {0};
	struct st_bbs_owned_entry *found;

	if (make_name_key(&key, name) != 0)
		return NULL;
	found = find_owned_by_key(view, &key);
	free((char *)key.public.name_key);
	return found;
}

static int
decode_entry(const st_bbs_list_t *list, ini_section_t *section,
    enum st_bbs_source source, const char *source_name,
    const char *name_key, size_t name_key_size,
    struct st_bbs_owned_entry **result)
{
	struct st_bbs_owned_entry *item = calloc(1, sizeof(*item));
	char *key_copy;
	int status;

	if (item == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	item->references = 1;
	item->source_name = source_name == NULL ? NULL : strdup(source_name);
	key_copy = malloc(name_key_size + 1);
	if ((source_name != NULL && item->source_name == NULL)
	    || key_copy == NULL) {
		free(key_copy);
		free_entry(item);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	memcpy(key_copy, name_key, name_key_size);
	key_copy[name_key_size] = '\0';
	item->public.name_key = key_copy;
	item->public.name_key_size = name_key_size;
	status = st_bbs_decode_profile(&item->profile, section,
	    ini_section_name(section), source != ST_BBS_SOURCE_PERSONAL,
	    list->download_path);
	if (status < 0) {
		free_entry(item);
		return status;
	}
	item->public.profile = &item->profile;
	item->public.source = source;
	item->public.source_name = item->source_name;
	*result = item;
	return 0;
}

static int
append_source(const st_bbs_list_t *list, struct st_bbs_view *view,
    ini_document_t *document,
    enum st_bbs_source source, const char *source_name)
{
	for (size_t i = 0; i < ini_document_section_count(document); ++i) {
		ini_section_t *section = ini_document_section_at(document, i);
		const char *name = ini_section_name(section);
		size_t name_key_size;
		const char *name_key = ini_section_name_key(section,
		    &name_key_size);
		struct st_bbs_owned_entry key = {0};
		struct st_bbs_owned_entry *existing;
		struct st_bbs_owned_entry *item;
		int status;

		if (!st_unicode_name_valid_normalized(name,
		    ST_PROFILE_NAME_SCALAR_MAX,
		    ST_PROFILE_NAME_MAX, "[]")) {
			status = add_diagnostic(view, source, source_name, name,
			    IO_ERROR_INVALID_FORMAT);
			if (status < 0)
				return status;
			continue;
		}
		key.public.name_key = name_key;
		key.public.name_key_size = name_key_size;
		if (reserved_name_key(&key)) {
			status = add_diagnostic(view, source, source_name, name,
			    IO_ERROR_INVALID_FORMAT);
			if (status < 0)
				return status;
			continue;
		}
		existing = find_owned_by_key(view, &key);
		if (existing != NULL) {
			/* Later duplicate blocks replace earlier blocks in one source. */
			if (existing->public.source != source
			    || !st_bbs_name_equal(existing->source_name, source_name)) {
				if (existing->shadowed == NULL) {
					status = decode_entry(list, section, source, source_name,
					    name_key, name_key_size,
					    &existing->shadowed);
					if (status < 0)
						return status;
				}
				else if (existing->shadowed->public.source == source
				    && st_bbs_name_equal(existing->shadowed->source_name,
				    source_name)) {
					st_profile_dispose(&existing->shadowed->profile);
					status = st_bbs_decode_profile(
					    &existing->shadowed->profile, section, name,
					    source != ST_BBS_SOURCE_PERSONAL,
					    list->download_path);
					if (status < 0)
						return status;
				}
				continue;
			}
			st_profile_dispose(&existing->profile);
			status = st_bbs_decode_profile(&existing->profile, section, name,
			    source != ST_BBS_SOURCE_PERSONAL, list->download_path);
			if (status < 0)
				return status;
			continue;
		}
		if (array_list_count((array_list_t)view->entries)
		    >= ST_BBS_LIST_MAX_ENTRIES) {
			return IO_ERROR_OUT_OF_RANGE;
		}
		status = decode_entry(list, section, source, source_name,
		    name_key, name_key_size, &item);
		if (status < 0)
			return status;
		size_t count = array_list_count((array_list_t)view->entries);
		if (!array_list_reserve((array_list_t *)&view->entries, count + 1)
		    || !array_list_reserve((array_list_t *)&view->names, count + 1)) {
			free_entry(item);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		(void)array_list_append((array_list_t *)&view->entries, item);
		(void)array_list_sorted_insert((array_list_t *)&view->names, item,
		    compare_entry_names);
	}
	return 0;
}

static int
read_plain_document(const st_bbs_list_t *list, struct st_bbs_view *view,
    const char *path, ini_document_t **result)
{
	struct st_bbs_source_snapshot *snapshot = NULL;
	file_map_t *map = NULL;
	size_t size;
	const void *bytes;
	int status = file_map_open(&map, path);

	bytes = file_map_data(map);
	size = file_map_size(map);
	for (size_t i = 0;
	    i < array_list_count((array_list_t)list->view.snapshots); ++i) {
		struct st_bbs_source_snapshot *old = list->view.snapshots[i];

		if (strcmp(old->path, path) == 0 && old->read_status == status
		    && (status < 0 || (old->size == size
		    && (size == 0 || memcmp(old->bytes, bytes, size) == 0)))) {
			snapshot = old;
			snapshot->references++;
			break;
		}
	}
	if (snapshot == NULL) {
		snapshot = calloc(1, sizeof(*snapshot));
		if (snapshot == NULL) {
			file_map_close(map);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		snapshot->path = strdup(path);
		if (status == 0)
			snapshot->bytes = malloc(size == 0 ? 1 : size);
		if (snapshot->path == NULL
		    || (status == 0 && snapshot->bytes == NULL)) {
			free(snapshot->path);
			free(snapshot->bytes);
			free(snapshot);
			file_map_close(map);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		if (status == 0 && size != 0)
			memcpy(snapshot->bytes, bytes, size);
		snapshot->size = status == 0 ? size : 0;
		snapshot->references = 1;
		snapshot->read_status = status;
		snapshot->status = status == 0
		    ? ini_document_parse(&snapshot->document, bytes, size) : status;
	}
	file_map_close(map);
	if (!array_list_append((array_list_t *)&view->snapshots, snapshot)) {
		release_snapshot(snapshot);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	*result = snapshot->document;
	return snapshot->status;
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

static bool
source_unchanged(const struct st_bbs_view *view, size_t index,
    const char *path)
{
	struct st_bbs_source_snapshot *snapshot;
	file_map_t *map = NULL;
	int status;
	bool same;

	if (index >= array_list_count((array_list_t)view->snapshots))
		return false;
	snapshot = view->snapshots[index];
	if (strcmp(snapshot->path, path) != 0)
		return false;
	status = file_map_open(&map, path);
	same = status == snapshot->read_status
	    && (status < 0 || (file_map_size(map) == snapshot->size
	    && (snapshot->size == 0 || memcmp(file_map_data(map),
	    snapshot->bytes, snapshot->size) == 0)));
	file_map_close(map);
	return same;
}

static bool
external_sources_unchanged(const st_bbs_list_t *list)
{
	size_t index = 0;

	if (list->shared_path != NULL
	    && !source_unchanged(&list->view, index++, list->shared_path))
		return false;
	if (list->cached_system_list != NULL
	    && !source_unchanged(&list->view, index++, list->cached_system_list))
		return false;
	for (size_t i = 0; i < st_settings_web_download_count(); ++i) {
		const struct name_value *download = st_settings_web_download_at(i);
		char *path = cache_list_path(list, download->name);
		bool same = path != NULL
		    && source_unchanged(&list->view, index++, path);

		free(path);
		if (!same)
			return false;
	}
	return index == array_list_count((array_list_t)list->view.snapshots);
}

int
st_bbs_view_prepare(const st_bbs_list_t *list,
    ini_document_t *personal_document, struct st_bbs_view *view)
{
	ini_document_t *document = NULL;
	ini_section_t *root;
	int status;

	if (list == NULL || personal_document == NULL || view == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	memset(view, 0, sizeof(*view));
	st_profile_init(&view->defaults);
	root = ini_document_root(personal_document);
	status = st_bbs_decode_profile(&view->defaults, root, NULL, false,
	    list->download_path);
	if (status < 0)
		goto failed;
	status = append_source(list, view, personal_document,
	    ST_BBS_SOURCE_PERSONAL, NULL);
	if (status < 0)
		goto failed;
	if (list->shared_path != NULL) {
		status = read_plain_document(list, view, list->shared_path,
		    &document);
		if (status == 0) {
			status = append_source(list, view, document,
			    ST_BBS_SOURCE_SHARED,
			    list->shared_path);
		} else if (status != IO_ERROR_NOT_FOUND) {
			status = add_diagnostic(view, ST_BBS_SOURCE_SHARED,
			    list->shared_path, NULL, status);
		} else
			status = 0;
	}
	if (status == 0 && list->cached_system_list != NULL) {
		status = read_plain_document(list, view, list->cached_system_list,
		    &document);
		if (status == 0) {
			status = append_source(list, view, document,
			    ST_BBS_SOURCE_WEB_SYSTEM,
			    "System List");
		} else if (status != IO_ERROR_NOT_FOUND) {
			status = add_diagnostic(view, ST_BBS_SOURCE_WEB_SYSTEM,
			    "System List", NULL, status);
		} else
			status = 0;
	}
	for (size_t i = 0; status == 0
	    && i < st_settings_web_download_count(); ++i) {
		const struct name_value *download = st_settings_web_download_at(i);
		char *source_path = cache_list_path(list, download->name);

		if (source_path == NULL) {
			status = add_diagnostic(view, ST_BBS_SOURCE_WEB_NAMED,
			    download->name, NULL, IO_ERROR_INVALID_ARGUMENT);
			continue;
		}
		status = read_plain_document(list, view, source_path, &document);
		free(source_path);
		if (status == 0) {
			status = append_source(list, view, document,
			    ST_BBS_SOURCE_WEB_NAMED,
			    download->name);
		} else if (status != IO_ERROR_NOT_FOUND) {
			status = add_diagnostic(view, ST_BBS_SOURCE_WEB_NAMED,
			    download->name, NULL, status);
		} else {
			status = 0;
		}
	}
	if (status == 0 && list->sort_configured)
		status = st_bbs_view_sort(view, list->sort_fields,
		    list->sort_field_count);
	if (status == 0)
		return 0;
failed:
	st_bbs_view_dispose(view);
	return status;
}

static size_t
entry_position(struct st_bbs_owned_entry **entries,
    const struct st_bbs_owned_entry *entry)
{
	for (size_t i = 0; i < array_list_count((array_list_t)entries); ++i) {
		if (entries[i] == entry)
			return i;
	}
	return SIZE_MAX;
}

int
st_bbs_view_prepare_update(const st_bbs_list_t *list,
    const char *old_name, const struct st_profile *profile,
    ini_document_t *personal_document, struct st_bbs_view *view)
{
	struct st_bbs_owned_entry *replacement = NULL;
	struct st_bbs_owned_entry *old;
	struct st_bbs_owned_entry *destination;
	size_t count, old_entry_index, old_name_index, destination_entry_index;
	size_t destination_name_index;
	int status = IO_ERROR_OUT_OF_MEMORY;

	if (!list->sort_configured || !external_sources_unchanged(list))
		return st_bbs_view_prepare(list, personal_document, view);
	/* Visible entries are immutable; share them until the old view is retired. */
	memset(view, 0, sizeof(*view));
	st_profile_init(&view->defaults);
	if (!st_profile_copy(&view->defaults, &list->view.defaults))
		goto failed;
	count = array_list_count((array_list_t)list->view.entries);
	if (!array_list_reserve((array_list_t *)&view->entries, count + 1)
	    || !array_list_reserve((array_list_t *)&view->names, count + 1))
		goto failed;
	for (size_t i = 0; i < count; ++i) {
		struct st_bbs_owned_entry *entry = list->view.entries[i];

		entry->references++;
		if (!array_list_append((array_list_t *)&view->entries, entry)) {
			free_entry(entry);
			goto failed;
		}
	}
	for (size_t i = 0; i < count; ++i) {
		if (!array_list_append((array_list_t *)&view->names,
		    list->view.names[i]))
			goto failed;
	}
	for (size_t i = 0; i < array_list_count(
	    (array_list_t)list->view.diagnostics); ++i) {
		const struct st_bbs_owned_diagnostic *diagnostic =
		    list->view.diagnostics[i];

		status = add_diagnostic(view, diagnostic->public.source,
		    diagnostic->source_name, diagnostic->section_name,
		    diagnostic->public.status);
		if (status < 0)
			goto failed;
	}
	for (size_t i = 0; i < array_list_count(
	    (array_list_t)list->view.snapshots); ++i) {
		struct st_bbs_source_snapshot *snapshot = list->view.snapshots[i];

		snapshot->references++;
		if (!array_list_append((array_list_t *)&view->snapshots,
		    snapshot)) {
			release_snapshot(snapshot);
			status = IO_ERROR_OUT_OF_MEMORY;
			goto failed;
		}
	}
	old = find_owned(view, old_name);
	if (old == NULL || old->public.source != ST_BBS_SOURCE_PERSONAL) {
		status = IO_ERROR_CONFLICT;
		goto failed;
	}
	replacement = calloc(1, sizeof(*replacement));
	if (replacement == NULL) {
		status = IO_ERROR_OUT_OF_MEMORY;
		goto failed;
	}
	replacement->references = 1;
	st_profile_init(&replacement->profile);
	if (!st_profile_copy(&replacement->profile, profile)
	    || make_name_key(replacement, profile->name) < 0) {
		status = IO_ERROR_OUT_OF_MEMORY;
		free_entry(replacement);
		goto failed;
	}
	replacement->public.profile = &replacement->profile;
	replacement->public.source = ST_BBS_SOURCE_PERSONAL;
	destination = find_owned_by_key(view, replacement);
	if (destination != old && destination != NULL
	    && destination->public.source == ST_BBS_SOURCE_PERSONAL) {
		status = IO_ERROR_CONFLICT;
		free_entry(replacement);
		goto failed;
	}
	if (destination == old) {
		old_entry_index = entry_position(view->entries, old);
		old_name_index = entry_position(view->names, old);
		replacement->shadowed = old->shadowed;
		if (replacement->shadowed != NULL)
			replacement->shadowed->references++;
		view->entries[old_entry_index] = replacement;
		view->names[old_name_index] = replacement;
		free_entry(old);
	} else {
		if (destination == NULL) {
			if (!array_list_append((array_list_t *)&view->entries,
			    replacement)) {
				status = IO_ERROR_OUT_OF_MEMORY;
				free_entry(replacement);
				goto failed;
			}
			if (!array_list_sorted_insert((array_list_t *)&view->names,
			    replacement, compare_entry_names)) {
				status = IO_ERROR_OUT_OF_MEMORY;
				goto failed;
			}
		} else {
			destination_entry_index = entry_position(view->entries,
			    destination);
			destination_name_index = entry_position(view->names,
			    destination);
			replacement->shadowed = destination;
			view->entries[destination_entry_index] = replacement;
			view->names[destination_name_index] = replacement;
		}
		old_entry_index = entry_position(view->entries, old);
		old_name_index = entry_position(view->names, old);
		if (old->shadowed != NULL) {
			old->shadowed->references++;
			view->entries[old_entry_index] = old->shadowed;
			view->names[old_name_index] = old->shadowed;
		} else {
			(void)array_list_take((array_list_t)view->entries,
			    old_entry_index);
			(void)array_list_take((array_list_t)view->names,
			    old_name_index);
		}
		free_entry(old);
	}
	if (list->sort_configured) {
		status = st_bbs_view_sort(view, list->sort_fields,
		    list->sort_field_count);
		if (status < 0)
			goto failed;
	}
	return 0;
failed:
	st_bbs_view_dispose(view);
	return status;
}

void
st_bbs_view_publish(st_bbs_list_t *list, struct st_bbs_view *view)
{
	struct st_bbs_view old = list->view;

	list->view = *view;
	memset(view, 0, sizeof(*view));
	st_bbs_view_dispose(&old);
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
	(void)fclose(stream);
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

static int
open_list(st_bbs_list_t **result, const struct st_app_paths *paths,
	const struct st_settings *settings, const void *password,
	size_t password_size)
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
	st_profile_init(&list->view.defaults);
	personal_path = settings != NULL && settings->list_path != NULL
	    && !settings->download_system_list
	    ? settings->list_path : paths->personal_list;
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
	if (list->personal_path == NULL
	    || (paths->shared_list != NULL && list->shared_path == NULL)
	    || list->cache_path == NULL
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
		st_bbs_picker_key_load(list, list->personal_document,
		    &list->crypto);
		struct st_bbs_view view;

		status = st_bbs_view_prepare(list, list->personal_document, &view);
		if (status == 0)
			st_bbs_view_publish(list, &view);
	}
	if (status < 0) {
		st_bbs_list_close(list);
		return status;
	}
	list->generation = 1;
	*result = list;
	return 0;
}

int
st_bbs_list_open(st_bbs_list_t **result, const struct st_app_paths *paths,
	const void *password, size_t password_size)
{
	return open_list(result, paths, &st_settings, password, password_size);
}

int
st_bbs_list_open_replacement(st_bbs_list_t **result,
	const st_bbs_list_t *current, const struct st_app_paths *paths,
	const struct st_settings *settings)
{
	if (current == NULL)
		return open_list(result, paths, settings, NULL, 0);
	return open_list(result, paths, settings, current->password,
	    current->password_size);
}

void
st_bbs_list_close(st_bbs_list_t *list)
{
	if (list == NULL)
		return;
	st_bbs_view_dispose(&list->view);
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
	struct st_bbs_view view;
	st_file_lock_t *lock = NULL;
	int status;

	if (list == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_file_lock_acquire(&lock, list->personal_path, false);
	if (status < 0)
		return status;
	status = read_personal_document(list, &document, &info);
	if (status == 0)
		status = st_bbs_view_prepare(list, document, &view);
	if (status == 0) {
		ini_document_free(&list->personal_document);
		list->personal_document = document;
		document = NULL;
		list->crypto = info;
		st_bbs_picker_key_load(list, list->personal_document, &info);
		set_write_options(list);
		st_bbs_view_publish(list, &view);
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
	    : array_list_count((array_list_t)list->view.entries);
}

const struct st_bbs_entry *
st_bbs_list_at(const st_bbs_list_t *list, size_t index)
{
	if (list == NULL || index >= st_bbs_list_count(list))
		return NULL;
	return &list->view.entries[index]->public;
}

const struct st_bbs_entry *
st_bbs_list_find(const st_bbs_list_t *list, const char *name)
{
	struct st_bbs_owned_entry *entry;

	if (list == NULL || name == NULL)
		return NULL;
	entry = find_owned(&list->view, name);
	return entry == NULL ? NULL : &entry->public;
}

const struct st_profile *
st_bbs_list_defaults(const st_bbs_list_t *list)
{
	return list == NULL ? NULL : &list->view.defaults;
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
	    : array_list_count((array_list_t)list->view.diagnostics);
}

const struct st_bbs_diagnostic *
st_bbs_list_diagnostic_at(const st_bbs_list_t *list, size_t index)
{
	if (list == NULL || index >= st_bbs_list_diagnostic_count(list))
		return NULL;
	return &list->view.diagnostics[index]->public;
}
