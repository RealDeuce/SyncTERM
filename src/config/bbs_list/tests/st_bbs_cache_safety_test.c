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
	FILE *stream = NULL;
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
		"cache/[u18-62ca1d92c4a3fc44a5fa30d1ddc593be1a9945ca21c0821af53d4f2b604075e7]/cache.txt",
		"cache/[u18-91dd0f8f29c38fee318385492df1eb0f2114869cb2c319f086f22c545db87c1d]/cache.txt",
		"cache/[u18-f209ddc9341cc72fb06d7876cfa266ae5db799e093d453d43c8adfe577fe1dc5]/cache.txt",
		"cache/[u18-017e9e217af8bbb732e91d3f534e0702b9f492edd2c3a9994383aa9bfa700f77]/cache.txt",
		"cache/[u18-2bcf4635382b5ba52b8343c9b6273ba9e9f0fd95a90e132767bfcff041b20c3e]/cache.txt",
		"cache/[u18-b9b1535db6d18b54bf63266443c037af0567f4177f19554b05022feaa5ec8476]",
	};
	static const char *const directories[] = {
		"cache/[u18-62ca1d92c4a3fc44a5fa30d1ddc593be1a9945ca21c0821af53d4f2b604075e7]",
		"cache/[u18-91dd0f8f29c38fee318385492df1eb0f2114869cb2c319f086f22c545db87c1d]",
		"cache/[u18-f209ddc9341cc72fb06d7876cfa266ae5db799e093d453d43c8adfe577fe1dc5]",
		"cache/[u18-017e9e217af8bbb732e91d3f534e0702b9f492edd2c3a9994383aa9bfa700f77]",
		"cache/[u18-2bcf4635382b5ba52b8343c9b6273ba9e9f0fd95a90e132767bfcff041b20c3e]",
		"cache/Orphan",
		"cache/[u18-0000000000000000000000000000000000000000000000000000000000000000]",
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
	CHECK(strcmp(key, "[u18-f0f4db0c76b5d4f68d5c58b47d15de497777f1a7812044728507ee6457fb8ec6]") == 0);
	CHECK(st_bbs_cache_key_recognized(key));
	CHECK(st_bbs_cache_key(key, sizeof(key), "NORMAL NAME"));
	CHECK(strcmp(key, "[u18-f0f4db0c76b5d4f68d5c58b47d15de497777f1a7812044728507ee6457fb8ec6]") == 0);
	CHECK(st_bbs_cache_key(key, sizeof(key), "../Outside"));
	CHECK(strcmp(key, "[u18-62ca1d92c4a3fc44a5fa30d1ddc593be1a9945ca21c0821af53d4f2b604075e7]") == 0);
	CHECK(st_bbs_cache_key(key, sizeof(key), "CON"));
	CHECK(strcmp(key, "[u18-1143da2bc54c495c4be31d3868785d39ffdfd56df5668f0645d8f14d47647952]") == 0);
	CHECK(st_bbs_cache_key(key, sizeof(key), "trail."));
	CHECK(strcmp(key, "[u18-cedd4b9ff43e695b31d896bebded15445818ddbec673abc6baa2e22661864b9d]") == 0);
	CHECK(st_bbs_cache_key(key, sizeof(key), "One\\Two"));
	CHECK(strcmp(key, "[u18-11b8e8b4c13a882a72dd9b86344147d13b63f5bbef78e343e63699db5c46e28a]") == 0);
	CHECK(!st_bbs_cache_key_recognized("Normal Name"));
	CHECK(!st_bbs_cache_key_recognized("[636f6e]"));
	CHECK(!st_bbs_cache_key_recognized("../Outside"));
	CHECK(!st_bbs_cache_key_recognized("[666f6f]"));
	CHECK(!st_bbs_cache_key_recognized("[u18-F0f4db0c76b5d4f68d5c58b47d15de497777f1a7812044728507ee6457fb8ec6]"));
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
	    "cache/[u18-62ca1d92c4a3fc44a5fa30d1ddc593be1a9945ca21c0821af53d4f2b604075e7]/cache.txt") == 0);
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
	    "cache/[u18-62ca1d92c4a3fc44a5fa30d1ddc593be1a9945ca21c0821af53d4f2b604075e7]");
	CHECK(fs_get_link_info(&info, path) == IO_ERROR_NOT_FOUND);
	CHECK(fs_get_info(&info, cache) == 0 && info.kind == FS_ENTRY_DIRECTORY);

	CHECK(create_cache_file("cache/[u18-91dd0f8f29c38fee318385492df1eb0f2114869cb2c319f086f22c545db87c1d]/cache.txt") == 0);
	entry = st_bbs_list_find(list, "Safe Name");
	CHECK(entry != NULL);
	st_profile_init(&profile);
	CHECK(st_profile_copy(&profile, entry->profile));
	(void)strcpy(profile.name, "Bad/Name");
	CHECK(st_bbs_list_update(list, entry->profile->name, &profile,
	    &mutation) == 0);
	CHECK(mutation.cache_status == 0);
	st_profile_dispose(&profile);
	fixture_path(path, sizeof(path), "cache/[u18-91dd0f8f29c38fee318385492df1eb0f2114869cb2c319f086f22c545db87c1d]");
	CHECK(fs_get_link_info(&info, path) == IO_ERROR_NOT_FOUND);
	fixture_path(path, sizeof(path),
	    "cache/[u18-f209ddc9341cc72fb06d7876cfa266ae5db799e093d453d43c8adfe577fe1dc5]/cache.txt");
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
	fixture_path(path, sizeof(path), "cache/[u18-f209ddc9341cc72fb06d7876cfa266ae5db799e093d453d43c8adfe577fe1dc5]");
	CHECK(fs_get_link_info(&info, path) == IO_ERROR_NOT_FOUND);
	fixture_path(path, sizeof(path), "cache/[u18-017e9e217af8bbb732e91d3f534e0702b9f492edd2c3a9994383aa9bfa700f77]/cache.txt");
	CHECK(fs_get_info(&info, path) == 0 && info.kind == FS_ENTRY_FILE);

	entry = st_bbs_list_find(list, "Renamed");
	source = st_bbs_list_find(list, "Shared/Name");
	CHECK(entry != NULL && source != NULL
	    && source->source == ST_BBS_SOURCE_SHARED);
	CHECK(st_bbs_list_update(list, entry->profile->name, source->profile,
	    &mutation) == 0);
	CHECK(mutation.cache_status == 0);
	fixture_path(path, sizeof(path), "cache/[u18-017e9e217af8bbb732e91d3f534e0702b9f492edd2c3a9994383aa9bfa700f77]");
	CHECK(fs_get_link_info(&info, path) == IO_ERROR_NOT_FOUND);
	fixture_path(path, sizeof(path),
	    "cache/[u18-2bcf4635382b5ba52b8343c9b6273ba9e9f0fd95a90e132767bfcff041b20c3e]/cache.txt");
	CHECK(fs_get_info(&info, path) == 0 && info.kind == FS_ENTRY_FILE);

	fixture_path(path, sizeof(path), "cache/[u18-b9b1535db6d18b54bf63266443c037af0567f4177f19554b05022feaa5ec8476]");
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
	fixture_path(path, sizeof(path),
	    "cache/[u18-0000000000000000000000000000000000000000000000000000000000000000]");
	CHECK(fs_create_directory(path) == 0);
	fixture_path(path, sizeof(path), "cache/[6e756c]");
	CHECK(fs_create_directory(path) == 0);
	fixture_path(path, sizeof(path), "cache/[666f6f]");
	CHECK(fs_create_directory(path) == 0);
	CHECK(st_bbs_list_sweep_orphan_caches(list) == 0);
	fixture_path(path, sizeof(path), "cache/Orphan");
	CHECK(fs_get_info(&info, path) == 0 && info.kind == FS_ENTRY_DIRECTORY);
	fixture_path(path, sizeof(path),
	    "cache/[u18-0000000000000000000000000000000000000000000000000000000000000000]");
	CHECK(fs_get_link_info(&info, path) == IO_ERROR_NOT_FOUND);
	fixture_path(path, sizeof(path), "cache/[6e756c]");
	CHECK(fs_get_info(&info, path) == 0 && info.kind == FS_ENTRY_DIRECTORY);
	fixture_path(path, sizeof(path), "cache/[666f6f]");
	CHECK(fs_get_info(&info, path) == 0 && info.kind == FS_ENTRY_DIRECTORY);
	CHECK(fs_get_info(&info, system_cache) == 0
	    && info.kind == FS_ENTRY_DIRECTORY);
	fixture_path(path, sizeof(path),
	    "cache/[u18-2bcf4635382b5ba52b8343c9b6273ba9e9f0fd95a90e132767bfcff041b20c3e]/cache.txt");
	CHECK(fs_get_info(&info, path) == 0 && info.kind == FS_ENTRY_FILE);

	st_bbs_list_close(list);
	clean_fixture();
	status = fs_get_link_info(&info, ST_BBS_CACHE_TEST_DIRECTORY);
	CHECK(status == IO_ERROR_NOT_FOUND);
	return 0;
}
