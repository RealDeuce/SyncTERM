/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "st_bbs_list_internal.h"

#include "filesystem.h"
#include "io_error.h"
#include "st_app_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ST_BBS_CACHE_TEST_DIRECTORY
#error "ST_BBS_CACHE_TEST_DIRECTORY is required"
#endif

#define CHECK(e) do { if (!(e)) { \
	fprintf(stderr, "failed: %s:%d: %s\n", __FILE__, __LINE__, #e); \
	return 1; \
} } while (0)

static void
fixture_path(char *result, size_t result_size, const char *relative)
{
	(void)snprintf(result, result_size, "%s/%s",
	    ST_BBS_CACHE_TEST_DIRECTORY, relative);
}

static int
write_text(const char *path, const char *text)
{
	FILE *stream;
	int status = fs_create_parent_directories(path);

	if (status == 0)
		status = fs_open(&stream, path, "wb");
	if (status < 0)
		return status;
	if (fwrite(text, 1, strlen(text), stream) != strlen(text))
		status = IO_ERROR_IO;
	if (fclose(stream) != 0 && status == 0)
		status = IO_ERROR_IO;
	return status;
}

static void
clean_fixture(void)
{
	static const char *const files[] = {
		"personal.lst", "personal.lst.lock", "shared.lst",
		"Outside/keep",
		"cache/[2e2e2f6f757473696465]/cache.txt",
		"cache/Safe Name/cache.txt",
		"cache/[6261642f6e616d65]/cache.txt",
		"cache/Renamed/cache.txt",
		"cache/[7368617265642f6e616d65]/cache.txt",
		"cache/Cache Failure",
	};
	static const char *const directories[] = {
		"cache/[2e2e2f6f757473696465]", "cache/Safe Name",
		"cache/[6261642f6e616d65]", "cache/Renamed",
		"cache/[7368617265642f6e616d65]", "cache/Orphan",
		"cache/[6e756c]", "cache/[666f6f]",
		"cache/syncterm-system-cache", "cache", "Outside", "",
	};
	char path[sizeof(ST_BBS_CACHE_TEST_DIRECTORY) + 96];

	for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
		fixture_path(path, sizeof(path), files[i]);
		(void)fs_remove_file(path);
	}
	for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i) {
		fixture_path(path, sizeof(path), directories[i]);
		(void)fs_remove_directory(path);
	}
}

static int
create_cache_file(const char *relative)
{
	char path[sizeof(ST_BBS_CACHE_TEST_DIRECTORY) + 96];

	fixture_path(path, sizeof(path), relative);
	return write_text(path, "cache");
}

static int
check_key_contract(void)
{
	char key[ST_BBS_CACHE_KEY_MAX + 1];

	CHECK(st_bbs_cache_key(key, sizeof(key), "Normal Name"));
	CHECK(strcmp(key, "Normal Name") == 0);
	CHECK(st_bbs_cache_key(key, sizeof(key), "../Outside"));
	CHECK(strcmp(key, "[2e2e2f6f757473696465]") == 0);
	CHECK(st_bbs_cache_key(key, sizeof(key), "CON"));
	CHECK(strcmp(key, "[636f6e]") == 0);
	CHECK(st_bbs_cache_key(key, sizeof(key), "trail."));
	CHECK(strcmp(key, "[747261696c2e]") == 0);
	CHECK(st_bbs_cache_key(key, sizeof(key), "One\\Two"));
	CHECK(strcmp(key, "[6f6e655c74776f]") == 0);
	CHECK(st_bbs_cache_key_recognized("Normal Name"));
	CHECK(st_bbs_cache_key_recognized("[636f6e]"));
	CHECK(!st_bbs_cache_key_recognized("../Outside"));
	CHECK(!st_bbs_cache_key_recognized("[666f6f]"));
	CHECK(!st_bbs_cache_key_recognized("[636F6e]"));
	CHECK(!st_bbs_cache_key_recognized("syncterm-system-cache"));
	return 0;
}

int
main(void)
{
	static const char personal_text[] =
	    "[../Outside]\nAddress=outside.example\n"
	    "[Safe Name]\nAddress=safe.example\n"
	    "[CON]\nAddress=con.example\n"
	    "[Cache Failure]\nAddress=failure.example\n";
	static const char shared_text[] =
	    "[Shared/Name]\nAddress=shared.example\n";
	struct st_app_paths paths = {0};
	struct st_bbs_mutation_result mutation;
	struct st_profile profile;
	const struct st_bbs_entry *entry;
	const struct st_bbs_entry *source;
	st_bbs_list_t *list = NULL;
	struct fs_info info;
	char personal[sizeof(ST_BBS_CACHE_TEST_DIRECTORY) + 32];
	char shared[sizeof(ST_BBS_CACHE_TEST_DIRECTORY) + 32];
	char cache[sizeof(ST_BBS_CACHE_TEST_DIRECTORY) + 32];
	char system_cache[sizeof(ST_BBS_CACHE_TEST_DIRECTORY) + 64];
	char system_list[sizeof(ST_BBS_CACHE_TEST_DIRECTORY) + 96];
	char path[sizeof(ST_BBS_CACHE_TEST_DIRECTORY) + 96];
	int status;

	clean_fixture();
	CHECK(check_key_contract() == 0);
	fixture_path(personal, sizeof(personal), "personal.lst");
	fixture_path(shared, sizeof(shared), "shared.lst");
	fixture_path(cache, sizeof(cache), "cache");
	fixture_path(system_cache, sizeof(system_cache),
	    "cache/syncterm-system-cache");
	fixture_path(system_list, sizeof(system_list),
	    "cache/syncterm-system-cache/System List.lst");
	CHECK(write_text(personal, personal_text) == 0);
	CHECK(write_text(shared, shared_text) == 0);
	CHECK(fs_create_directories(system_cache) == 0);
	CHECK(create_cache_file("Outside/keep") == 0);
	CHECK(create_cache_file(
	    "cache/[2e2e2f6f757473696465]/cache.txt") == 0);
	paths.settings_file = personal;
	paths.personal_list = personal;
	paths.shared_list = shared;
	paths.cache_directory = cache;
	paths.system_cache_directory = system_cache;
	paths.cached_system_list = system_list;
	paths.download_directory = ST_BBS_CACHE_TEST_DIRECTORY;
	CHECK(st_bbs_list_open(&list, &paths, NULL, 0) == 0);

	entry = st_bbs_list_find(list, "../outside");
	CHECK(entry != NULL && entry->source == ST_BBS_SOURCE_PERSONAL);
	CHECK(st_bbs_list_remove(list, entry->profile->name, &mutation) == 0);
	CHECK(mutation.cache_status == 0);
	fixture_path(path, sizeof(path), "Outside/keep");
	CHECK(fs_get_info(&info, path) == 0 && info.kind == FS_ENTRY_FILE);
	fixture_path(path, sizeof(path),
	    "cache/[2e2e2f6f757473696465]");
	CHECK(fs_get_link_info(&info, path) == IO_ERROR_NOT_FOUND);
	CHECK(fs_get_info(&info, cache) == 0 && info.kind == FS_ENTRY_DIRECTORY);

	CHECK(create_cache_file("cache/Safe Name/cache.txt") == 0);
	entry = st_bbs_list_find(list, "Safe Name");
	CHECK(entry != NULL);
	st_profile_init(&profile);
	CHECK(st_profile_copy(&profile, entry->profile));
	(void)strcpy(profile.name, "Bad/Name");
	CHECK(st_bbs_list_update(list, entry->profile->name, &profile,
	    &mutation) == 0);
	CHECK(mutation.cache_status == 0);
	st_profile_dispose(&profile);
	fixture_path(path, sizeof(path), "cache/Safe Name");
	CHECK(fs_get_link_info(&info, path) == IO_ERROR_NOT_FOUND);
	fixture_path(path, sizeof(path),
	    "cache/[6261642f6e616d65]/cache.txt");
	CHECK(fs_get_info(&info, path) == 0 && info.kind == FS_ENTRY_FILE);

	entry = st_bbs_list_find(list, "Bad/Name");
	CHECK(entry != NULL);
	st_profile_init(&profile);
	CHECK(st_profile_copy(&profile, entry->profile));
	(void)strcpy(profile.name, "Renamed");
	CHECK(st_bbs_list_update(list, entry->profile->name, &profile,
	    &mutation) == 0);
	CHECK(mutation.cache_status == 0);
	st_profile_dispose(&profile);
	fixture_path(path, sizeof(path), "cache/[6261642f6e616d65]");
	CHECK(fs_get_link_info(&info, path) == IO_ERROR_NOT_FOUND);
	fixture_path(path, sizeof(path), "cache/Renamed/cache.txt");
	CHECK(fs_get_info(&info, path) == 0 && info.kind == FS_ENTRY_FILE);

	entry = st_bbs_list_find(list, "Renamed");
	source = st_bbs_list_find(list, "Shared/Name");
	CHECK(entry != NULL && source != NULL
	    && source->source == ST_BBS_SOURCE_SHARED);
	CHECK(st_bbs_list_update(list, entry->profile->name, source->profile,
	    &mutation) == 0);
	CHECK(mutation.cache_status == 0);
	fixture_path(path, sizeof(path), "cache/Renamed");
	CHECK(fs_get_link_info(&info, path) == IO_ERROR_NOT_FOUND);
	fixture_path(path, sizeof(path),
	    "cache/[7368617265642f6e616d65]/cache.txt");
	CHECK(fs_get_info(&info, path) == 0 && info.kind == FS_ENTRY_FILE);

	fixture_path(path, sizeof(path), "cache/Cache Failure");
	CHECK(write_text(path, "not a directory") == 0);
	entry = st_bbs_list_find(list, "Cache Failure");
	CHECK(entry != NULL);
	st_profile_init(&profile);
	CHECK(st_profile_copy(&profile, entry->profile));
	(void)strcpy(profile.name, "Cache Failure New");
	CHECK(st_bbs_list_update(list, entry->profile->name, &profile,
	    &mutation) == 0);
	CHECK(mutation.cache_status == IO_ERROR_NOT_DIRECTORY);
	st_profile_dispose(&profile);
	CHECK(st_bbs_list_find(list, "Cache Failure New") != NULL);
	CHECK(fs_get_info(&info, path) == 0 && info.kind == FS_ENTRY_FILE);

	fixture_path(path, sizeof(path), "cache/Orphan");
	CHECK(fs_create_directory(path) == 0);
	fixture_path(path, sizeof(path), "cache/[6e756c]");
	CHECK(fs_create_directory(path) == 0);
	fixture_path(path, sizeof(path), "cache/[666f6f]");
	CHECK(fs_create_directory(path) == 0);
	CHECK(st_bbs_list_sweep_orphan_caches(list) == 0);
	fixture_path(path, sizeof(path), "cache/Orphan");
	CHECK(fs_get_link_info(&info, path) == IO_ERROR_NOT_FOUND);
	fixture_path(path, sizeof(path), "cache/[6e756c]");
	CHECK(fs_get_link_info(&info, path) == IO_ERROR_NOT_FOUND);
	fixture_path(path, sizeof(path), "cache/[666f6f]");
	CHECK(fs_get_info(&info, path) == 0 && info.kind == FS_ENTRY_DIRECTORY);
	CHECK(fs_get_info(&info, system_cache) == 0
	    && info.kind == FS_ENTRY_DIRECTORY);
	fixture_path(path, sizeof(path),
	    "cache/[7368617265642f6e616d65]/cache.txt");
	CHECK(fs_get_info(&info, path) == 0 && info.kind == FS_ENTRY_FILE);

	st_bbs_list_close(list);
	clean_fixture();
	status = fs_get_link_info(&info, ST_BBS_CACHE_TEST_DIRECTORY);
	CHECK(status == IO_ERROR_NOT_FOUND);
	return 0;
}
