/* Integration test for the isolated Wren file-picker VM. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"

#include "filesystem.h"
#include "path.h"
#include "st_app_paths.h"
#include "st_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ST_WREN_PICKER_TEST_DIRECTORY
#error ST_WREN_PICKER_TEST_DIRECTORY is required
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, \
		    __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static int
write_file(const char *path, const char *contents)
{
	FILE *stream = fopen(path, "wb");
	size_t length = strlen(contents);

	if (stream == NULL)
		return -1;
	if (fwrite(contents, 1, length, stream) != length) {
		fclose(stream);
		return -1;
	}
	return fclose(stream);
}

int
main(void)
{
	char personal[sizeof(ST_WREN_PICKER_TEST_DIRECTORY) + 32];
	char shared[sizeof(ST_WREN_PICKER_TEST_DIRECTORY) + 32];
	char settings[sizeof(ST_WREN_PICKER_TEST_DIRECTORY) + 32];
	char scripts[sizeof(ST_WREN_PICKER_TEST_DIRECTORY) + 32];
	char picked[sizeof(ST_WREN_PICKER_TEST_DIRECTORY) + 32];
	char *expected = NULL;
	struct st_app_paths paths = {0};
	struct st_console_config console_config = {
		.columns = 80,
		.rows = 25,
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
		.wrap = true,
	};
	struct st_wren_host_config host_config;
	struct st_console_event enter = {
		.type = ST_CONSOLE_EVENT_KEY_CODE,
		.data.key.code = 13,
	};
	struct st_console_event escape = {
		.type = ST_CONSOLE_EVENT_KEY_CODE,
		.data.key.code = 27,
	};
	const struct st_console_rect marker_rect = {7, 4, 1, 1};
	const struct st_console_cell marker = {
		.codepoint = 'Z', .foreground = 3, .background = 4,
	};
	struct st_console_cell restored = {0};
	struct st_wren_picker_call call;
	st_console_t *console = NULL;
	st_wren_host_t *public_host = NULL;
	struct st_wren_host *host;

	CHECK(fs_create_directories(ST_WREN_PICKER_TEST_DIRECTORY) == 0);
	(void)snprintf(personal, sizeof(personal), "%s/personal.lst",
	    ST_WREN_PICKER_TEST_DIRECTORY);
	(void)snprintf(shared, sizeof(shared), "%s/shared.lst",
	    ST_WREN_PICKER_TEST_DIRECTORY);
	(void)snprintf(settings, sizeof(settings), "%s/syncterm.ini",
	    ST_WREN_PICKER_TEST_DIRECTORY);
	(void)snprintf(scripts, sizeof(scripts), "%s/scripts",
	    ST_WREN_PICKER_TEST_DIRECTORY);
	(void)snprintf(picked, sizeof(picked), "%s/alpha.txt",
	    ST_WREN_PICKER_TEST_DIRECTORY);
	CHECK(write_file(personal, "") == 0);
	CHECK(write_file(shared, "") == 0);
	CHECK(write_file(picked, "picked\n") == 0);
	paths.settings_file = settings;
	paths.personal_list = personal;
	paths.shared_list = shared;
	paths.cache_directory = ST_WREN_PICKER_TEST_DIRECTORY;
	paths.system_cache_directory = ST_WREN_PICKER_TEST_DIRECTORY;
	paths.cached_system_list = shared;
	paths.download_directory = ST_WREN_PICKER_TEST_DIRECTORY;
	paths.scripts_directory = scripts;
	paths.themes_directory = ST_WREN_PICKER_TEST_DIRECTORY;
	paths.keys_file = settings;
	CHECK(st_console_create(&console, &console_config) == 0);
	host_config = (struct st_wren_host_config){
		.console = console,
		.paths = &paths,
		.application_title = "SyncTERM picker test",
	};
	CHECK(st_wren_host_create(&public_host, &host_config) == 0);
	CHECK(st_console_write_cells(console, &marker_rect, &marker, 1) == 0);
	host = public_host;
	call = (struct st_wren_picker_call){
		.mode = ST_WREN_PICKER_FILE,
		.title = "Pick",
		.initial_path = ST_WREN_PICKER_TEST_DIRECTORY,
		.mask = "alpha.txt",
		.options = ST_WREN_PICKER_MASK_LOCKED,
	};
	CHECK(st_console_post_event(console, &enter) == 0);
	CHECK(st_wren_picker_host_run(host, &call));
	CHECK(call.path_count == 1);
	CHECK(path_make_absolute(NULL, 0, picked) > 0);
	ptrdiff_t length = path_make_absolute(NULL, 0, picked);
	expected = malloc((size_t)length + 1);
	CHECK(expected != NULL);
	CHECK(path_make_absolute(expected, (size_t)length + 1, picked) == length);
	CHECK(strcmp(call.paths[0], expected) == 0);
	free(expected);
	expected = NULL;
	st_wren_picker_call_dispose(&call);
	CHECK(st_console_read_cells(console, &marker_rect, &restored, 1) == 0);
	CHECK(restored.codepoint == marker.codepoint
	    && restored.foreground == marker.foreground
	    && restored.background == marker.background);
	call = (struct st_wren_picker_call){
		.mode = ST_WREN_PICKER_FILE,
		.title = "Cancel",
		.initial_path = ST_WREN_PICKER_TEST_DIRECTORY,
		.mask = "alpha.txt",
	};
	CHECK(st_console_post_event(console, &escape) == 0);
	CHECK(st_wren_picker_host_run(host, &call));
	CHECK(call.path_count == 0 && call.completed);
	st_wren_picker_call_dispose(&call);
	st_wren_host_destroy(public_host);
	st_console_destroy(console);
	return 0;
}
