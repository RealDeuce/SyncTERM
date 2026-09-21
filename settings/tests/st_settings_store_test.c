/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "st_settings.h"

#include "filesystem.h"
#include "ini.h"
#include "io_error.h"
#include "st_app_paths.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ST_SETTINGS_TEST_DIRECTORY
#error "ST_SETTINGS_TEST_DIRECTORY is required"
#endif

#define CHECK(e) do { if (!(e)) { \
	fprintf(stderr, "failed: %s:%d: %s\n", __FILE__, __LINE__, #e); \
	return 1; \
} } while (0)

static int
rewrite_value(const char *path, const char *key, int value)
{
	ini_document_t *document = NULL;
	ini_section_t *section;
	FILE *stream;
	int status = fs_open(&stream, path, "rb");

	if (status == 0) {
		status = ini_document_read(&document, stream);
		(void)fclose(stream);
	}
	if (status == 0) {
		section = ini_document_find_section(document, "SyncTERM");
		status = ini_section_set_int64(section, key, value,
		    &ini_default_style);
	}
	if (status == 0)
		status = fs_open(&stream, path, "wb");
	if (status == 0) {
		status = ini_document_write(document, stream);
		if (fclose(stream) != 0 && status == 0)
			status = IO_ERROR_IO;
	}
	ini_document_free(&document);
	return status;
}

static int
write_text(const char *path, const char *text)
{
	FILE *stream = NULL;
	size_t size = strlen(text);
	size_t written;
	int close_status;

	if (fs_open(&stream, path, "wb") != 0)
		return -1;
	written = fwrite(text, 1, size, stream);
	close_status = fclose(stream);
	return written == size && close_status == 0 ? 0 : -1;
}

static int
read_document(const char *path, ini_document_t **document)
{
	FILE *stream = NULL;
	int status = fs_open(&stream, path, "rb");

	if (status == 0) {
		status = ini_document_read(document, stream);
		if (fclose(stream) != 0 && status == 0)
			status = IO_ERROR_IO;
	}
	return status;
}

static int
test_empty_font_name(const char *path)
{
	ini_document_t *document = NULL;
	ini_section_t *section;
	size_t before = st_settings_font_count();

	CHECK(st_settings_font_insert(before, "Temporary font") == 0);
	CHECK(st_settings_font_set_path(before, ST_SETTINGS_FONT_8X16,
	    "/fonts/empty-name.f16") == 0);
	CHECK(st_settings_font_rename(before, "") == 0);
	CHECK(st_settings_save() == 0);
	CHECK(st_settings_reload() == 0);
	CHECK(st_settings_font_count() == before + 1);
	CHECK(strcmp(st_settings_font_at(before)->name, "") == 0);
	CHECK(strcmp(st_settings_font_at(before)->path[ST_SETTINGS_FONT_8X16],
	    "/fonts/empty-name.f16") == 0);
	CHECK(read_document(path, &document) == 0);
	section = ini_document_find_section(document, "Font:");
	CHECK(section != NULL);
	CHECK(strcmp(ini_section_get_string(section, "Path8x16", ""),
	    "/fonts/empty-name.f16") == 0);
	ini_document_free(&document);
	return 0;
}

static int
test_classic_color_bounds(const char *path)
{
	ini_document_t *document = NULL;
	ini_section_t *classic;

	CHECK(write_text(path,
	    "[ClassicTheme]\n"
	    "FrameColour=1000000\nTextColour=16\n"
	    "BackgroundColour=1000000\nInverseColour=8\n"
	    "LightbarColour=17\nLightbarBackgroundColour=9\n"
	    "[UIFC]\nFrameColour=Red\nBackgroundColour=Green\n") == 0);
	CHECK(st_settings_reload() == 0);
	CHECK(st_settings.theme_frame_color == 16);
	CHECK(st_settings.theme_text_color == 16);
	CHECK(st_settings.theme_background_color == 8);
	CHECK(st_settings.theme_inverse_color == 8);
	CHECK(st_settings.theme_lightbar_color == 16);
	CHECK(st_settings.theme_lightbar_background_color == 8);
	st_settings.theme_frame_color = UINT_MAX;
	st_settings.theme_text_color = UINT_MAX;
	st_settings.theme_background_color = UINT_MAX;
	st_settings.theme_inverse_color = UINT_MAX;
	st_settings.theme_lightbar_color = UINT_MAX;
	st_settings.theme_lightbar_background_color = UINT_MAX;
	CHECK(st_settings_save() == 0);
	CHECK(read_document(path, &document) == 0);
	classic = ini_document_find_section(document, "ClassicTheme");
	CHECK(classic != NULL);
	CHECK(strcmp(ini_section_get_string(classic, "FrameColour", ""),
	    "Default") == 0);
	CHECK(strcmp(ini_section_get_string(classic, "TextColour", ""),
	    "Default") == 0);
	CHECK(strcmp(ini_section_get_string(classic, "BackgroundColour", ""),
	    "Default") == 0);
	CHECK(strcmp(ini_section_get_string(classic, "InverseColour", ""),
	    "Default") == 0);
	CHECK(strcmp(ini_section_get_string(classic, "LightbarColour", ""),
	    "Default") == 0);
	CHECK(strcmp(ini_section_get_string(classic,
	    "LightbarBackgroundColour", ""), "Default") == 0);
	ini_document_free(&document);
	CHECK(st_settings_reload() == 0);
	return 0;
}

static int
test_classic_per_key_fallback(const char *path)
{
	ini_document_t *document = NULL;
	ini_section_t *classic;

	CHECK(write_text(path,
	    "[ClassicTheme]\nFrameColour=Blue\nTextColour=invalid\n"
	    "BackgroundColour=LightGray\n"
	    "[UIFC]\nTextColour=Red\nInverseColour=Cyan\n"
	    "LightbarColour=Yellow\nLightbarBackgroundColour=Brown\n") == 0);
	CHECK(st_settings_reload() == 0);
	CHECK(st_settings.theme_frame_color == 1);
	CHECK(st_settings.theme_text_color == 16);
	CHECK(st_settings.theme_background_color == 7);
	CHECK(st_settings.theme_inverse_color == 3);
	CHECK(st_settings.theme_lightbar_color == 14);
	CHECK(st_settings.theme_lightbar_background_color == 6);
	CHECK(st_settings_save() == 0);
	CHECK(read_document(path, &document) == 0);
	CHECK(ini_document_find_section(document, "UIFC") == NULL);
	classic = ini_document_find_section(document, "ClassicTheme");
	CHECK(classic != NULL);
	CHECK(strcmp(ini_section_get_string(classic, "FrameColour", ""),
	    "Blue") == 0);
	CHECK(strcmp(ini_section_get_string(classic, "TextColour", ""),
	    "Default") == 0);
	CHECK(strcmp(ini_section_get_string(classic, "BackgroundColour", ""),
	    "LightGray") == 0);
	CHECK(strcmp(ini_section_get_string(classic, "InverseColour", ""),
	    "Cyan") == 0);
	CHECK(strcmp(ini_section_get_string(classic, "LightbarColour", ""),
	    "Yellow") == 0);
	CHECK(strcmp(ini_section_get_string(classic,
	    "LightbarBackgroundColour", ""), "Brown") == 0);
	ini_document_free(&document);
	return 0;
}

static int
test_classic_external_migration(const char *path)
{
	ini_document_t *document = NULL;
	ini_section_t *classic;
	ini_section_t *legacy;

	CHECK(write_text(path, "[UIFC]\nTextColour=Red\n") == 0);
	CHECK(st_settings_reload() == 0);
	CHECK(write_text(path, "[UIFC]\nTextColour=Green\n") == 0);
	CHECK(st_settings_save() == 0);
	CHECK(read_document(path, &document) == 0);
	CHECK(ini_document_find_section(document, "UIFC") == NULL);
	classic = ini_document_find_section(document, "ClassicTheme");
	CHECK(classic != NULL);
	CHECK(strcmp(ini_section_get_string(classic, "TextColour", ""),
	    "Green") == 0);
	ini_document_free(&document);

	CHECK(write_text(path, "[UIFC]\nTextColour=Red\n") == 0);
	CHECK(st_settings_reload() == 0);
	st_settings.theme_text_color = 1;
	CHECK(write_text(path, "[UIFC]\nTextColour=Green\n") == 0);
	CHECK(st_settings_save() == IO_ERROR_CONFLICT);
	CHECK(read_document(path, &document) == 0);
	CHECK(ini_document_find_section(document, "ClassicTheme") == NULL);
	legacy = ini_document_find_section(document, "UIFC");
	CHECK(legacy != NULL);
	CHECK(strcmp(ini_section_get_string(legacy, "TextColour", ""),
	    "Green") == 0);
	ini_document_free(&document);
	return 0;
}

int
main(void)
{
	static const char initial[] =
	    "; keep this\n[SyncTERM]\nScrollBackLines=100\nUnknown=preserve\n"
	    "ListPath=https://example.invalid/system.lst\n"
	    "SortOrder=29,3,1\nActiveSortProfile=Recent\n"
	    "[WebLists]\nOne=https://example.invalid/one.lst\n"
	    "[SortProfiles]\nName=29,1\nRecent=29,3,1\n"
	    "[Font:Custom VGA]\nPath8x16=/fonts/custom.f16\n"
	    "[Font:Tall]\nPath12x20=/fonts/tall.f20\n";
	struct st_app_paths paths = {0};
	char settings_path[sizeof(ST_SETTINGS_TEST_DIRECTORY) + 32];
	char list_path[sizeof(ST_SETTINGS_TEST_DIRECTORY) + 32];
	char cache_path[sizeof(ST_SETTINGS_TEST_DIRECTORY) + 32];
	char system_path[sizeof(ST_SETTINGS_TEST_DIRECTORY) + 64];
	FILE *stream;
	char *serialized = NULL;
	size_t size = 0;
	ini_document_t *document = NULL;

	(void)snprintf(settings_path, sizeof(settings_path), "%s/settings.ini",
	    ST_SETTINGS_TEST_DIRECTORY);
	(void)snprintf(list_path, sizeof(list_path), "%s/personal.lst",
	    ST_SETTINGS_TEST_DIRECTORY);
	(void)snprintf(cache_path, sizeof(cache_path), "%s/cache",
	    ST_SETTINGS_TEST_DIRECTORY);
	(void)snprintf(system_path, sizeof(system_path), "%s/cache/System List.lst",
	    ST_SETTINGS_TEST_DIRECTORY);
	CHECK(fs_create_directories(ST_SETTINGS_TEST_DIRECTORY) == 0);
	CHECK(fs_open(&stream, settings_path, "wb") == 0);
	CHECK(fwrite(initial, 1, sizeof(initial) - 1, stream) == sizeof(initial) - 1);
	CHECK(fclose(stream) == 0);
	paths.settings_file = settings_path;
	paths.personal_list = list_path;
	paths.cache_directory = cache_path;
	paths.system_cache_directory = cache_path;
	paths.cached_system_list = system_path;
	paths.download_directory = ST_SETTINGS_TEST_DIRECTORY;
	CHECK(st_settings_load(&paths) == 0);
	CHECK(st_settings.scrollback_lines == 100);
	CHECK(st_settings.prompt_save);
	CHECK(st_settings.download_system_list);
	CHECK(strcmp(st_settings.stored_list_path,
	    "https://example.invalid/system.lst") == 0);
	CHECK(strcmp(st_settings.list_path, system_path) == 0);
	CHECK(st_settings_web_download_count() == 1);
	CHECK(st_settings_sort_profile_count() == 2);
	CHECK(st_settings_active_sort_profile() == 1);
	CHECK(strcmp(st_settings.sort_order, "29,3,1") == 0);
	CHECK(strcmp(st_settings_sort_profile_at(1)->name, "Recent") == 0);
	CHECK(st_settings_font_count() == 2);
	CHECK(strcmp(st_settings_font_at(0)->name, "Custom VGA") == 0);
	CHECK(strcmp(st_settings_font_at(0)->path[ST_SETTINGS_FONT_8X16],
	    "/fonts/custom.f16") == 0);
	CHECK(st_settings_font_at(0)->path[ST_SETTINGS_FONT_8X8] == NULL);
	CHECK(strcmp(st_settings_font_at(1)->path[ST_SETTINGS_FONT_12X20],
	    "/fonts/tall.f20") == 0);
	CHECK(st_settings_web_download_append("Two",
	    "https://example.invalid/two.lst") == 0);
	CHECK(st_settings_web_download_set(0, "First",
	    "https://example.invalid/first.lst") == 0);
	CHECK(st_settings_web_download_move(1, 0) == 0);
	CHECK(strcmp(st_settings_web_download_at(0)->name, "Two") == 0);
	CHECK(st_settings_sort_profile_select(0) == 0);
	CHECK(st_settings_sort_profile_update(0, "Alphabetical", "29,1") == 0);
	CHECK(st_settings_font_rename(0, "Renamed VGA") == 0);
	CHECK(st_settings_font_set_path(0, ST_SETTINGS_FONT_8X8,
	    "/fonts/custom.f8") == 0);
	CHECK(st_settings_font_remove(1) == 0);
	st_settings.scrollback_lines = 200;
	CHECK(rewrite_value(settings_path, "OutputMode", 4) == 0);
	CHECK(st_settings_save() == 0);
	CHECK(fs_open(&stream, settings_path, "rb") == 0);
	CHECK(ini_document_read(&document, stream) == 0);
	CHECK(fclose(stream) == 0);
	CHECK(ini_section_get_int64(ini_document_find_section(document,
	    "SyncTERM"), "ScrollBackLines", 0) == 200);
	CHECK(ini_section_get_int64(ini_document_find_section(document,
	    "SyncTERM"), "OutputMode", 0) == 4);
	CHECK(strcmp(ini_section_get_string(ini_document_find_section(document,
	    "SyncTERM"), "SortOrder", ""), "29,1") == 0);
	CHECK(strcmp(ini_section_get_string(ini_document_find_section(document,
	    "SyncTERM"), "ActiveSortProfile", ""), "Alphabetical") == 0);
	CHECK(strcmp(ini_section_get_string(ini_document_find_section(document,
	    "SortProfiles"), "Alphabetical", ""), "29,1") == 0);
	CHECK(ini_document_find_section(document, "Font:Custom VGA") == NULL);
	CHECK(ini_document_find_section(document, "Font:Tall") == NULL);
	CHECK(strcmp(ini_section_get_string(ini_document_find_section(document,
	    "Font:Renamed VGA"), "Path8x8", ""), "/fonts/custom.f8") == 0);
	CHECK(strcmp(ini_section_get_string(ini_document_find_section(document,
	    "Font:Renamed VGA"), "Path8x16", ""), "/fonts/custom.f16") == 0);
	CHECK(ini_document_serialize(document, &serialized, &size) == 0);
	CHECK(strstr(serialized, "; keep this") != NULL);
	CHECK(strstr(serialized, "Unknown=preserve") != NULL);
	CHECK(strstr(serialized, "Two=https://example.invalid/two.lst") != NULL);
	CHECK(strstr(serialized,
	    "First=https://example.invalid/first.lst") != NULL);
	free(serialized);
	ini_document_free(&document);
	{
		struct st_settings candidate;
		bool old_confirm = st_settings.confirm_close;

		st_settings_init(&candidate);
		CHECK(st_settings_copy(&candidate, &st_settings));
		candidate.confirm_close = !old_confirm;
		CHECK(st_settings.confirm_close == old_confirm);
		CHECK(st_settings_save_value(&candidate) == 0);
		CHECK(st_settings.confirm_close == !old_confirm);
		CHECK(read_document(settings_path, &document) == 0);
		CHECK(ini_section_get_bool(ini_document_find_section(document,
		    "SyncTERM"), "ConfirmClose", old_confirm) == !old_confirm);
		ini_document_free(&document);
		candidate.confirm_close = old_confirm;
		st_settings_install(&candidate);
		CHECK(st_settings.confirm_close == old_confirm);
		st_settings_dispose(&candidate);
	}

	st_settings.scrollback_lines = 300;
	CHECK(rewrite_value(settings_path, "ScrollBackLines", 400) == 0);
	CHECK(st_settings_save() == IO_ERROR_CONFLICT);
	st_settings_shutdown();
	CHECK(fs_open(&stream, settings_path, "wb") == 0);
	CHECK(fputs("[SyncTERM]\nSortOrder=29,3,1\n", stream) >= 0);
	CHECK(fclose(stream) == 0);
	CHECK(st_settings_load(&paths) == 0);
	CHECK(st_settings_sort_profile_count() == 4);
	CHECK(st_settings_active_sort_profile() == 1);
	CHECK(strcmp(st_settings_sort_profile_at(1)->name,
	    "Last Connected") == 0);
	st_settings_shutdown();
	CHECK(fs_open(&stream, settings_path, "wb") == 0);
	CHECK(fputs("[SyncTERM]\nSortOrder=\nActiveSortProfile=\n"
	    "[SortProfiles]\nOnly=1\n", stream) >= 0);
	CHECK(fclose(stream) == 0);
	CHECK(st_settings_load(&paths) == 0);
	CHECK(strcmp(st_settings.sort_order, "") == 0);
	CHECK(st_settings_sort_profile_count() == 1);
	CHECK(st_settings_active_sort_profile() == 0);
	int failure = test_empty_font_name(settings_path);
	failure += test_classic_color_bounds(settings_path);
	failure += test_classic_per_key_fallback(settings_path);
	failure += test_classic_external_migration(settings_path);
	st_settings_shutdown();
	(void)fs_remove_file(settings_path);
	(void)fs_remove_file(list_path);
	{
		char lock_path[sizeof(settings_path) + 8];
		(void)snprintf(lock_path, sizeof(lock_path), "%s.lock", settings_path);
		(void)fs_remove_file(lock_path);
	}
	(void)fs_remove_directory(ST_SETTINGS_TEST_DIRECTORY);
	return failure;
}
