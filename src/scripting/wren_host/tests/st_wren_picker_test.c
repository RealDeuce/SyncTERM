/* Picker VM isolation and injected backend restoration failures. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"

#include "filesystem.h"
#include "st_app_paths.h"
#include "st_console.h"
#include "console_internal.h"
#include "io_error.h"

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
fail_menu_present(st_console_t *console)
{
	return console == console->display->backend_state ? IO_ERROR_IO : 0;
}

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

static int
write_startup_probe(const char *path)
{
	for (const struct st_embedded_script *script = st_wren_embedded_scripts;
	    script->name != NULL; ++script) {
		if (strcmp(script->name, "file_picker") != 0)
			continue;
		FILE *stream = fopen(path, "wb");
		int status;

		if (stream == NULL)
			return -1;
		status = fputs(script->source, stream);
		if (status >= 0)
			status = fputs("\nScreen.window.position = [1, 1]\n"
			    "Screen.window.print(\"S\")\n", stream);
		if (fclose(stream) != 0)
			return -1;
		return status < 0 ? -1 : 0;
	}
	return -1;
}

int
main(void)
{
	char personal[sizeof(ST_WREN_PICKER_TEST_DIRECTORY) + 32];
	char shared[sizeof(ST_WREN_PICKER_TEST_DIRECTORY) + 32];
	char settings[sizeof(ST_WREN_PICKER_TEST_DIRECTORY) + 32];
	char scripts[sizeof(ST_WREN_PICKER_TEST_DIRECTORY) + 32];
	char picker_scripts[sizeof(ST_WREN_PICKER_TEST_DIRECTORY) + 48];
	char startup[sizeof(ST_WREN_PICKER_TEST_DIRECTORY) + 96];
	char picked[sizeof(ST_WREN_PICKER_TEST_DIRECTORY) + 32];
	struct st_app_paths paths = {0};
	struct st_console_config console_config = {
		.columns = 80,
		.rows = 25,
		.wrap = true,
	};
	const struct st_console_display_config display_config = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
		.text_presentation = ST_CONSOLE_TEXT_UNICODE,
	};
	struct st_wren_host_config host_config;
	struct st_console_event escape = {
		.type = ST_CONSOLE_EVENT_KEY_CODE,
		.data.key.code = 27,
	};
	struct st_wren_picker_call call;
	st_console_t *console = NULL;
	st_console_t *picker_console = NULL;
	st_console_display_t *display = NULL;
	st_wren_host_t *public_host = NULL;
	st_hyperlink_registry_t *hyperlinks = NULL;
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
	(void)snprintf(picker_scripts, sizeof(picker_scripts), "%s/auto/picker",
	    scripts);
	(void)snprintf(picked, sizeof(picked), "%s/alpha.txt",
	    ST_WREN_PICKER_TEST_DIRECTORY);
	CHECK(fs_create_directories(picker_scripts) == 0);
	(void)snprintf(startup, sizeof(startup), "%s/file_picker.wren",
	    picker_scripts);
	CHECK(write_startup_probe(startup) == 0);
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
	CHECK(st_console_display_create(&display, &display_config) == 0);
	CHECK(st_console_create_on_display(&console, display,
	    &console_config) == 0);
	CHECK(st_console_create_on_display(&picker_console, display,
	    &console_config) == 0);
	CHECK(st_hyperlink_registry_create(&hyperlinks) == 0);
	host_config = (struct st_wren_host_config){
		.display = display,
		.console = console,
		.picker_console = picker_console,
		.paths = &paths,
		.application_title = "SyncTERM picker test",
		.hyperlinks = hyperlinks,
	};
	CHECK(st_wren_host_create(&public_host, &host_config) == 0);
	host = public_host;
	CHECK(wrenInterpret(host->picker_vm, "picker_unknown_foreign",
	    "class Unknown { foreign static missing() }\n") == WREN_RESULT_RUNTIME_ERROR);
	CHECK(wrenInterpret(host->picker_vm, "picker_forbidden_menu",
	    "import \"syncterm_menu\" for Menu\n") == WREN_RESULT_RUNTIME_ERROR);
	CHECK(wrenInterpret(host->picker_vm, "picker_hyperlink_contract",
	    "import \"syncterm\" for Hyperlinks\n"
	    "Hyperlinks.containsKey(1)\n") == WREN_RESULT_RUNTIME_ERROR);
	{
		const struct st_console_rect origin = {0, 0, 1, 1};
		struct st_console_cell cell;

		CHECK(st_console_read_cells(picker_console, &origin, &cell, 1) == 0);
		CHECK(cell.glyph == 'S');
		CHECK(st_console_read_cells(console, &origin, &cell, 1) == 0);
		CHECK(cell.glyph != 'S');
	}
	/* Both VMs keep their own context even when their console is inactive. */
	CHECK(st_wren_context_from_vm(host->vm) == &host->menu);
	CHECK(st_wren_context_from_vm(host->picker_vm) == &host->picker);
	CHECK(wrenInterpret(host->picker_vm, "picker_context_probe",
	    "import \"syncterm\" for Screen, ScreenFonts, Input\n"
	    "Screen.window.position = [1, 1]\n"
	    "Screen.window.print(\"P\")\n"
	    "ScreenFonts[1] = 3\n"
	    "Input.mouseEvents = 1\n") == WREN_RESULT_SUCCESS);
	CHECK(host->picker.fonts.slots[1] == 3);
	CHECK(host->menu.fonts.slots[1] != 3);
	CHECK(host->picker.mouse_events == 1);
	CHECK(host->menu.mouse_events == 0);
	{
		const struct st_console_rect origin = {0, 0, 1, 1};
		struct st_console_cell cell;

		CHECK(st_console_read_cells(picker_console, &origin, &cell, 1) == 0);
		CHECK(cell.glyph == 'P');
		CHECK(st_console_read_cells(console, &origin, &cell, 1) == 0);
		CHECK(cell.glyph != 'P');
	}
	CHECK(st_console_display_activate(display, picker_console) == 0);
	CHECK(wrenInterpret(host->vm, "menu_context_probe",
	    "import \"syncterm\" for Screen, ScreenFonts, Input\n"
	    "Screen.window.position = [1, 1]\n"
	    "Screen.window.print(\"M\")\n"
	    "ScreenFonts[1] = 4\n"
	    "Input.mouseEvents = 2\n") == WREN_RESULT_SUCCESS);
	CHECK(host->menu.fonts.slots[1] == 4);
	CHECK(host->picker.fonts.slots[1] == 3);
	CHECK(host->menu.mouse_events == 2);
	CHECK(host->picker.mouse_events == 1);
	/* The distinct unavailable values above are context markers, not fonts
	 * the subsequent picker UI can render in this text-only fixture. */
	host->menu.fonts.slots[1] = 0;
	host->picker.fonts.slots[1] = 0;
	CHECK(st_console_display_activate(display, console) == 0);
	/* Successful picker workflows run through the normal executable.
	 * Retain backend restoration fault injection at this unit boundary. */
	{
		const struct st_console_backend_ops failing = {
			.present = fail_menu_present,
		};
		const struct st_console_backend_ops *saved_ops = display->backend_ops;
		void *saved_state = display->backend_state;
		st_console_t *active = NULL;

		call = (struct st_wren_picker_call){
			.mode = ST_WREN_PICKER_FILE,
			.title = "Restore failure",
			.initial_path = ST_WREN_PICKER_TEST_DIRECTORY,
			.mask = "alpha.txt",
		};
		CHECK(st_console_post_event(picker_console, &escape) == 0);
		display->backend_ops = &failing;
		display->backend_state = console;
		CHECK(!st_wren_picker_host_run(host, &call));
		CHECK(strstr(host->last_error, "Cannot restore the menu console") != NULL);
		CHECK(st_console_display_active(display, &active) == 0);
		CHECK(active == picker_console);
		st_wren_picker_call_dispose(&call);
		CHECK(st_console_post_event(picker_console, &escape) == 0);
		CHECK(wrenInterpret(host->vm, "picker_restore_error",
		    "import \"syncterm\" for Host\n"
		    "Host.pickFile(\".\", \"*\", 0)\n") == WREN_RESULT_RUNTIME_ERROR);
		CHECK(strstr(host->last_error, "Cannot restore the menu console") != NULL);
		display->backend_ops = saved_ops;
		display->backend_state = saved_state;
		CHECK(st_console_display_activate(display, console) == 0);
	}
	st_wren_host_destroy(public_host);
	st_console_destroy(picker_console);
	st_console_destroy(console);
	st_console_display_destroy(display);
	st_hyperlink_registry_destroy(hyperlinks);
	return 0;
}
