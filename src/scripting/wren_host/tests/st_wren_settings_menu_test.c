/* Settings host application, private failure injection, and save policy. */
/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"
#include "filesystem.h"
#include "io_error.h"
#include "st_app_paths.h"
#include "st_audio_runtime.h"
#include "st_builtin_fonts.h"
#include "st_bbs_list.h"
#include "st_presentation_font.h"
#include "st_session_history.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
		    __FILE__, __LINE__, #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (false)

static void
write_text(const char *path, const char *text)
{
	FILE *stream = fopen(path, "wb");

	CHECK(stream != NULL);
	CHECK(fputs(text, stream) >= 0);
	CHECK(fclose(stream) == 0);
}

static void
interpret(struct st_wren_host *host, const char *module, const char *source)
{
	if (wrenInterpret(host->vm, module, source) != WREN_RESULT_SUCCESS) {
		fprintf(stderr, "%s: %s\n", module, host->last_error);
		exit(EXIT_FAILURE);
	}
}

static void
test_default_cursor(struct st_wren_host *host, st_console_t *console)
{
	for (size_t index = 0; index < st_screen_mode_count(); ++index) {
		const struct st_screen_mode_descriptor *mode = st_screen_mode_at(index);
		struct st_console_custom_cursor actual;
		char module[40];

		if (mode->mode == ST_SCREEN_MODE_CURRENT
		    || mode->mode == ST_SCREEN_MODE_CUSTOM)
			continue;
		CHECK(st_wren_host_set_screen_mode(host, mode->mode) == 0);
		CHECK(snprintf(module, sizeof(module), "cursor_apply_%zu", index)
		    < (int)sizeof(module));
		interpret(host, module,
		    "import \"syncterm_menu\" for Menu\n"
		    "var s = Menu.settings\n"
		    "s.cursorStyle = 0\n"
		    "s.confirmClose = !s.confirmClose\n"
		    "if (!s.apply()) Fiber.abort(\"cursor apply\")\n");
		CHECK(st_console_get_default_cursor(console, &actual) == 0);
		CHECK(actual.start == mode->console.default_cursor.start);
		CHECK(actual.end == mode->console.default_cursor.end);
		CHECK(actual.range == mode->console.default_cursor.range);
	}
	CHECK(st_wren_host_set_screen_mode(host, ST_SCREEN_MODE_80X25) == 0);
}

static void
test_safe_kdf(struct st_wren_host *host)
{
	if (!st_list_crypto_encryption_available())
		return;
	interpret(host, "kdf_setup",
	    "import \"syncterm_menu\" for Menu\n"
	    "if (!Menu.setEncryption(6, 128, \"audit password\")) Fiber.abort(\"encrypt fixture\")\n");
	unsigned before = st_bbs_list_crypto_info(host->bbs_list)->cost_log2;
	CHECK(before == 8);
	char old_kdf[sizeof(st_settings.key_derivation_iterations)];
	strcpy(old_kdf, st_settings.key_derivation_iterations);
	host->config.safe_mode = true;
	interpret(host, "safe_kdf",
	    "import \"syncterm_menu\" for Menu\n"
	    "var s = Menu.settings\n"
	    "s.kdfShift = s.kdfShift == 8 ? 9 : 8\n"
	    "if (s.apply() || !s.dirty) Fiber.abort(\"safe mode rewrote encrypted list\")\n");
	CHECK(strcmp(old_kdf, st_settings.key_derivation_iterations) == 0);
	CHECK(st_bbs_list_reload(host->bbs_list) == 0);
	CHECK(st_bbs_list_crypto_info(host->bbs_list)->cost_log2 == before);
	host->config.safe_mode = false;
	interpret(host, "kdf_cleanup",
	    "import \"syncterm_menu\" for Menu\n"
	    "if (!Menu.setEncryption(0, 0, null)) Fiber.abort(\"decrypt fixture\")\n");
}

static void
test_custom_mode(struct st_wren_host *host, st_console_t *console)
{
	uint32_t columns, rows, width, height;

	CHECK(st_wren_host_set_screen_mode(host, ST_SCREEN_MODE_CUSTOM) == 0);
	interpret(host, "custom_apply",
	    "import \"syncterm_menu\" for Menu\n"
	    "var s = Menu.settings\n"
	    "s.customRows = 31\n"
	    "s.customColumns = 101\n"
	    "s.customFontHeight = 14\n"
	    "s.customAspectWidth = 4\n"
	    "s.customAspectHeight = 3\n"
	    "if (!s.apply()) Fiber.abort(\"custom apply\")\n");
	CHECK(st_console_size(console, &columns, &rows) == 0);
	CHECK(columns == 101 && rows == 31);
	CHECK(st_console_cell_pixel_size(console, &width, &height) == 0);
	CHECK(width == 8 && height == 14);
	CHECK(st_console_get_aspect_ratio(console, &width, &height) == 0);
	CHECK(width == 4 && height == 3);
	CHECK(st_wren_host_set_screen_mode(host, ST_SCREEN_MODE_80X25) == 0);
}

static void
test_save_retry(struct st_wren_host *host, const char *settings)
{
	char backup[sizeof(ST_WREN_SETTINGS_TEST_DIRECTORY) + 64];

	CHECK(snprintf(backup, sizeof(backup), "%s.backup", settings) < (int)sizeof(backup));
	CHECK(rename(settings, backup) == 0);
	CHECK(fs_create_directories(settings) == 0);
	interpret(host, "save_retry",
	    "import \"menu_settings_ui\" for SettingsMenu\n"
	    "import \"menu_settings_ui_test\" for SettingsTestApp, SettingsMenuTest\n"
	    "var app = SettingsTestApp.new([\n"
	    "  SettingsMenuTest.row_(\"Confirm Program Exit\"),\n"
	    "  SettingsMenuTest.cancel_,\n"
	    "  Fn.new {|pane| Fiber.abort(\"retry offered\") }\n"
	    "])\n"
	    "var result = Fiber.new { SettingsMenu.program_(app, false) }.try()\n"
	    "if (result != \"retry offered\" || app.alerts != 1) "
	    "Fiber.abort(\"failed save closed the settings editor\")\n");
	CHECK(fs_remove_directory(settings) == 0);
	CHECK(rename(backup, settings) == 0);
	CHECK(st_settings_save() == 0);
}

static void
post_key(st_console_t *console, uint16_t key)
{
	const struct st_console_event event = key >= 32 && key < 127
	    ? (struct st_console_event){.type = ST_CONSOLE_EVENT_TEXT,
		.data.text.codepoint = key}
	    : (struct st_console_event){.type = ST_CONSOLE_EVENT_KEY_CODE,
		.data.key.code = key};
	CHECK(st_console_post_event(console, &event) == 0);
}

static void
test_offer_save(struct st_wren_host *host, st_console_t *console)
{
	struct st_profile source;
	bool saved;
	bool old_prompt = st_settings.prompt_save;

	st_profile_init(&source);
	strcpy(source.name, "Offered");
	strcpy(source.address, "offered.example");
	strcpy(source.username, "guest");
	source.identifier = -1;
	source.connection_type = ST_CONNECTION_RAW;
	source.port = 23;
	source.screen_mode = ST_SCREEN_MODE_80X25;
	source.encoding = ST_PROFILE_ENCODING_UTF8;
	source.calls = 3;
	st_settings.prompt_save = false;
	CHECK(st_wren_host_offer_save_profile(host, &source, &saved) == 0 && !saved);
	st_settings.prompt_save = true;
	host->config.safe_mode = true;
	CHECK(st_wren_host_offer_save_profile(host, &source, &saved) == 0 && !saved);
	host->config.safe_mode = false;
	source.hide_popups = true;
	CHECK(st_wren_host_offer_save_profile(host, &source, &saved) == 0 && !saved);
	source.hide_popups = false;
	source.identifier = 1;
	CHECK(st_wren_host_offer_save_profile(host, &source, &saved) == 0 && !saved);
	source.identifier = -1;
	post_key(console, 27);
	CHECK(st_wren_host_offer_save_profile(host, &source, &saved) == 0 && !saved);
	CHECK(st_bbs_list_find(host->bbs_list, "Offered") == NULL);
	post_key(console, 'y');
	post_key(console, 27);
	CHECK(st_wren_host_offer_save_profile(host, &source, &saved) == 0 && saved);
	const struct st_bbs_entry *entry = st_bbs_list_find(host->bbs_list, "Offered");
	CHECK(entry != NULL && strcmp(entry->profile->username, "guest") == 0
	    && entry->profile->calls == 0 && source.calls == 3);
	st_settings.prompt_save = old_prompt;
	st_profile_dispose(&source);
}

int
main(void)
{
	static const char directory[] = ST_WREN_SETTINGS_TEST_DIRECTORY;
	char settings[sizeof(directory) + 32];
	char personal[sizeof(directory) + 32];
	char cached[sizeof(directory) + 32];
	struct st_app_paths paths = {0};
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config geometry = {
		.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 16,
	};
	st_console_t *console = NULL;
	st_console_t *picker = NULL;
	st_console_display_t *console_display = NULL;
	st_wren_host_t *host = NULL;
	st_hyperlink_registry_t *hyperlinks = NULL;
	st_session_history_t *history = NULL;
	st_audio_runtime_t *audio = NULL;
	struct st_console_font builtin;
	struct st_presentation_font presentation = {0};
	uint16_t font;
	char source[32768];
	FILE *stream;
	size_t size;

	CHECK(fs_create_directories(directory) == 0);
	CHECK(snprintf(settings, sizeof(settings), "%s/settings.ini", directory) < (int)sizeof(settings));
	CHECK(snprintf(personal, sizeof(personal), "%s/personal.lst", directory) < (int)sizeof(personal));
	CHECK(snprintf(cached, sizeof(cached), "%s/system.lst", directory) < (int)sizeof(cached));
	struct fs_info info;
	if (fs_get_info(&info, settings) == 0 && info.kind == FS_ENTRY_DIRECTORY)
		CHECK(fs_remove_directory(settings) == 0);
	write_text(settings, "[SyncTERM]\n");
	write_text(personal, "[Existing]\nAddress=existing.example\n");
	paths.settings_file = settings;
	paths.personal_list = personal;
	paths.cached_system_list = cached;
	paths.scripts_directory = (char *)directory;
	paths.themes_directory = (char *)directory;
	paths.cache_directory = (char *)directory;
	paths.system_cache_directory = (char *)directory;
	paths.download_directory = (char *)directory;
	CHECK(st_settings_load(&paths) == 0);
	CHECK(st_settings.color_emoji);
	CHECK(st_console_display_create(&console_display, &display) == 0);
	CHECK(st_console_create_on_display(&console, console_display, &geometry) == 0);
	CHECK(st_console_create_on_display(&picker, console_display, &geometry) == 0);
	CHECK(st_builtin_font_get(0, 8, 16, &builtin));
	CHECK(st_presentation_font_create(&presentation, &builtin) == 0);
	CHECK(st_console_register_static_font(console, &presentation.font, &font) == 0 && font == 0);
	CHECK(st_console_register_static_font(picker, &presentation.font, &font) == 0 && font == 0);
	CHECK(st_session_history_create(&history, 2000) == 0);
	CHECK(st_hyperlink_registry_create(&hyperlinks) == 0);
	CHECK(st_audio_runtime_create(&audio, UINT32_MAX) == 0);
	const struct st_wren_host_config config = {
		.display = console_display, .console = console,
		.picker_console = picker, .paths = &paths, .session_history = history,
		.hyperlinks = hyperlinks,
		.audio_runtime = audio,
	};
	CHECK(st_wren_host_create(&host, &config) == 0);
	interpret(host, "settings_setup",
	    "import \"syncterm_menu\" for Menu\n"
	    "if (Menu.load(null) != 0) Fiber.abort(\"load fixture\")\n");
	stream = fopen(ST_WREN_SETTINGS_SOURCE_DIRECTORY "/scripts/menu_settings_ui_test.wren", "rb");
	CHECK(stream != NULL);
	size = fread(source, 1, sizeof(source) - 1, stream);
	CHECK(!ferror(stream) && feof(stream));
	CHECK(fclose(stream) == 0);
	source[size] = '\0';
	interpret(host, "menu_settings_ui_test", source);
	interpret(host, "settings_apply",
	    "import \"syncterm_menu\" for Menu\n"
	    "var s = Menu.settings\n"
	    "s.colorEmoji = false\n"
	    "s.scrollbackLines = 123\n"
	    "s.customRows = 30\n"
	    "s.customColumns = 100\n"
	    "s.frameColor = 1\n"
	    "s.kdfShift = 8\n"
	    "if (!s.save()) Fiber.abort(\"settings runtime apply\")\n");
	bool color = true;
	CHECK(st_console_get_color_emoji(console, &color) == 0 && !color);
	CHECK(st_console_get_color_emoji(picker, &color) == 0 && !color);
	CHECK(st_session_history_capacity(history) == 123);
	CHECK(st_audio_runtime_allowed(audio) == st_settings.audio_output_modes);
	CHECK(st_settings_reload() == 0);
	CHECK(!st_settings.color_emoji);
	CHECK(st_settings.scrollback_lines == 123 && st_settings.custom_rows == 30
	    && st_settings.custom_columns == 100 && st_settings.theme_frame_color == 1);
	host->config.safe_mode = true;
	interpret(host, "settings_safe_licenses",
	    "import \"menu_settings_ui_test\" for SettingsMenuTest\n"
	    "SettingsMenuTest.testLicenses_()\n");
	interpret(host, "settings_colour_safe",
	    "import \"syncterm_menu\" for Menu\n"
	    "var s = Menu.settings\n"
	    "s.colorEmoji = true\n"
	    "if (!s.apply()) Fiber.abort(\"apply colour in safe mode\")\n");
	CHECK(st_console_get_color_emoji(console, &color) == 0 && color);
	CHECK(st_console_get_color_emoji(picker, &color) == 0 && color);
	host->config.safe_mode = false;
	test_default_cursor(host, console);
	test_custom_mode(host, console);
	CHECK(st_console_get_color_emoji(console, &color) == 0 && color);
	test_save_retry(host, settings);
	test_safe_kdf(host);
	test_offer_save(host, console);
	st_wren_host_destroy(host);
	st_audio_runtime_destroy(audio);
	st_session_history_destroy(history);
	st_console_destroy(console);
	st_console_destroy(picker);
	st_console_display_destroy(console_display);
	st_presentation_font_destroy(&presentation);
	st_hyperlink_registry_destroy(hyperlinks);
	st_settings_shutdown();
	return EXIT_SUCCESS;
}
