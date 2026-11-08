/* Session-mode API invariants and private logging/error contracts.
 * Application connection/cleanup workflows use the testing bitmap process harness. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* The private helper copy must not replace the runner's production entry. */
#define st_application_run st_test_session_application_run
#include "../syncterm.c"
#undef st_application_run

#include "filesystem.h"

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, \
		    __LINE__, #expression); \
		return 1; \
	} \
} while (false)

static int
check_session_log(void)
{
	struct st_profile profile;
	st_profile_init(&profile);
	struct connection_ui ui = {.profile = &profile};
	char *snapshot = NULL, *next = NULL;
	CHECK(st_mutex_init(&ui.log_mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(connection_log_snapshot(&ui, &snapshot) == 0 && snapshot[0] == '\0');
	free(snapshot);
	connection_log(&ui, ST_CONNECTION_LOG_INFO, "SSH", "hidden");
	connection_cterm_log(&ui, ST_CTERM_LOG_DEBUG, "hidden");
	CHECK(ui.log_size == 0 && ui.log_file == NULL);
	connection_log(&ui, ST_CONNECTION_LOG_ERROR, "SSH", "protocol fixture");
	connection_cterm_log(&ui, ST_CTERM_LOG_ERROR, "CTerm fixture");
	connection_transfer_log(&ui, ST_PROFILE_LOG_INFO, "**literal** `code` \\path");
	CHECK(connection_log_snapshot(&ui, &snapshot) == 0);
	CHECK(strstr(snapshot, "SSH Error: protocol fixture\n") != NULL);
	CHECK(strstr(snapshot, "CTerm Error: CTerm fixture\n") != NULL);
	CHECK(strstr(snapshot, "Transfer Info: **literal** `code` \\path\n") != NULL);
	connection_cterm_log(&ui, ST_CTERM_LOG_ERROR, "next\033record");
	CHECK(connection_log_snapshot(&ui, &next) == 0);
	CHECK(strstr(next, "next?record") != NULL && strstr(snapshot, "next?record") == NULL);
	free(snapshot);
	free(next);
	/* A full buffer must keep complete recent records and accept new ones. */
	char record[1024];
	memset(record, 'a', sizeof(record) - 1);
	record[sizeof(record) - 1] = '\0';
	for (unsigned i = 0; i < 1100; ++i)
		connection_cterm_log(&ui, ST_CTERM_LOG_ERROR, record);
	connection_cterm_log(&ui, ST_CTERM_LOG_ERROR, "latest fixture");
	CHECK(connection_log_snapshot(&ui, &snapshot) == 0);
	CHECK(strlen(snapshot) <= 1024U * 1024U);
	CHECK(strncmp(snapshot, "CTerm Error: ", 13) == 0);
	CHECK(strstr(snapshot, "protocol fixture") == NULL);
	CHECK(strstr(snapshot, "CTerm Error: latest fixture\n") != NULL);
	free(snapshot);
	free(ui.log);
	st_mutex_destroy(&ui.log_mutex);
	st_profile_dispose(&profile);
	return 0;
}

static int
check_connection_help(void)
{
	struct connection_ui ui = {0};
	char *body;

	CHECK(st_mutex_init(&ui.log_mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	body = connection_help_body(&ui, "Remote *host*\r\nCipher: `aes`\\path");
	CHECK(body != NULL);
	CHECK(strcmp(body, "# Connection Details\n\nRemote \\*host\\*  \n"
	    "Cipher: \\`aes\\`\\\\path") == 0);
	free(body);
	ui.log = "SSH Info: **literal** `key`\\path\r\n"
	    "\n# not a heading\n: not a definition\nlast";
	ui.log_size = strlen(ui.log);
	body = connection_help_body(&ui, "Closed\nConnected: 00:00:12");
	CHECK(body != NULL);
	CHECK(strcmp(body, "# Connection Details\n\nClosed  \nConnected: 00:00:12"
	    "\n\n# Session Log\n\n"
	    "- SSH Info: \\*\\*literal\\*\\* \\`key\\`\\\\path\n"
	    "- \n- # not a heading\n- : not a definition\n- last\n") == 0);
	free(body);
	body = connection_help_body(&ui, NULL);
	CHECK(body != NULL && strncmp(body, "# Session Log\n\n", 15) == 0);
	free(body);
	ui.log = "line\n\n";
	ui.log_size = strlen(ui.log);
	body = connection_help_body(&ui, "");
	CHECK(body != NULL && strcmp(body,
	    "# Session Log\n\n- line\n- \n") == 0);
	free(body);
	ui.log_size = SIZE_MAX;
	CHECK(connection_help_body(&ui, "overflow") == NULL);
	ui.log = NULL;
	ui.log_size = 0;
	CHECK(connection_help_body(&ui, "") == NULL);
	CHECK(connection_help_body(&ui, NULL) == NULL);
	st_mutex_destroy(&ui.log_mutex);
	return 0;
}

static int
check_log_policy(const struct st_wren_host_config *base,
	st_wren_host_t *safe_host, st_console_display_t *display, st_console_t *console,
	const struct st_app_paths *paths, st_hyperlink_registry_t *hyperlinks)
{
	struct st_wren_host_config config = *base;
	st_wren_host_t *host = NULL;
	struct st_profile profile;
	char buffer[4096];
	config.safe_mode = false;
	CHECK(st_wren_host_create(&host, &config) == 0);
	st_profile_init(&profile);
	strcpy(profile.name, "Test");
	profile.log_path = strdup(ST_APP_SESSION_TEST_DIRECTORY "/helper.log");
	CHECK(profile.log_path != NULL);
	struct connection_ui ui = {.host = host, .profile = &profile};
	CHECK(st_mutex_init(&ui.log_mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	for (unsigned pass = 0; pass < 3; ++pass) {
		profile.append_log = pass == 1;
		connection_log_open(&ui);
		CHECK(ui.log_file != NULL);
		if (pass != 2)
			connection_transfer_log(&ui, ST_PROFILE_LOG_INFO, "transfer record");
		connection_log_close(&ui);
		FILE *file = fopen(profile.log_path, "rb");
		CHECK(file != NULL);
		size_t size = fread(buffer, 1, sizeof(buffer) - 1, file);
		CHECK(!ferror(file) && fclose(file) == 0);
		buffer[size] = '\0';
		CHECK(strstr(buffer, "Log opened\n") != NULL);
		CHECK(strstr(buffer, "Log closed\n---------------\n") != NULL);
		CHECK((strstr(buffer, "transfer record\n") != NULL) == (pass != 2));
	}
	ui.host = safe_host;
	connection_log_open(&ui);
	CHECK(ui.log_file == NULL);
	CHECK(st_wren_host_record_connection(safe_host, &profile) == 0 && profile.calls == 1);
	st_bbs_list_t *list = NULL;
	CHECK(st_bbs_list_open_replacement(&list, NULL, paths, NULL) == 0);
	CHECK(st_bbs_list_find(list, "Test")->profile->calls == 0);
	st_bbs_list_close(list);
	/* Invalid API input still closes its log without recording a call. */
	profile.connection_type = ST_CONNECTION_UNKNOWN;
	profile.hide_popups = true;
	enum st_term_disposition disposition = ST_TERM_RETURN_TO_PICKER;
	CHECK(run_selected(host, display, console, &profile, paths, NULL,
	    hyperlinks, NULL, &disposition) != 0);
	CHECK(profile.calls == 1);
	FILE *file = fopen(profile.log_path, "rb");
	CHECK(file != NULL);
	size_t size = fread(buffer, 1, sizeof(buffer) - 1, file);
	CHECK(!ferror(file) && fclose(file) == 0);
	buffer[size] = '\0';
	CHECK(strstr(buffer, "Log closed\n---------------\n") != NULL);
	free(ui.log);
	st_mutex_destroy(&ui.log_mutex);
	st_profile_dispose(&profile);
	st_wren_host_destroy(host);
	return 0;
}

static int
check_mode(st_wren_host_t *host, st_console_t *console,
	st_console_t *picker, enum st_screen_mode expected,
	uint32_t columns, uint32_t rows, uint32_t cell_height)
{
	enum st_screen_mode actual;

	CHECK(st_wren_host_get_screen_mode(host, &actual) == 0);
	CHECK(actual == expected);
	for (unsigned i = 0; i < 2; ++i) {
		st_console_t *surface = i == 0 ? console : picker;
		struct st_console_custom_cursor cursor;
		struct st_console_rect window;
		uint32_t width, height;
		uint32_t flags;

		CHECK(st_console_size(surface, &width, &height) == 0);
		CHECK(width == columns && height == rows);
		CHECK(st_console_cell_pixel_size(surface, &width, &height) == 0);
		CHECK(height == cell_height);
		CHECK(st_console_get_default_cursor(surface, &cursor) == 0);
		CHECK(cursor.range == cell_height && cursor.visible);
		CHECK(st_console_get_video_flags(surface, &flags) == 0);
		CHECK(flags == 0);
		CHECK(st_console_get_window(surface, &window) == 0);
		CHECK(window.x == 0 && window.y == 0
		    && window.width == columns && window.height == rows);
	}
	return 0;
}

int
st_app_session_mode_test_main(void)
{
	struct st_app_paths paths = {
		.settings_file = ST_APP_SESSION_TEST_DIRECTORY "/settings.ini",
		.personal_list = ST_APP_SESSION_TEST_DIRECTORY "/personal.ini",
		.shared_list = ST_APP_SESSION_TEST_DIRECTORY "/shared.ini",
		.cache_directory = ST_APP_SESSION_TEST_DIRECTORY "/cache",
		.system_cache_directory = ST_APP_SESSION_TEST_DIRECTORY "/cache/system",
		.cached_system_list = ST_APP_SESSION_TEST_DIRECTORY "/cache/system/list.ini",
		.scripts_directory = ST_APP_SESSION_TEST_DIRECTORY "/scripts",
		.themes_directory = ST_APP_SESSION_TEST_DIRECTORY "/themes",
		.download_directory = ST_APP_SESSION_TEST_DIRECTORY,
		.keys_file = ST_APP_SESSION_TEST_DIRECTORY "/keys.ini",
	};
	const struct st_console_display_config display_config = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_screen_mode_descriptor *mode
	    = st_screen_mode_get(ST_SCREEN_MODE_AMIGA_640X400);
	st_console_display_t *display = NULL;
	st_console_t *console = NULL, *picker = NULL;
	st_wren_host_t *host = NULL;
	st_hyperlink_registry_t *hyperlinks = NULL;
	struct st_console_font font;
	struct st_profile profile;
	uint16_t identifier;
	enum st_term_disposition disposition = ST_TERM_RETURN_TO_PICKER;
	FILE *file;

	CHECK(check_connection_help() == 0);
	CHECK(check_session_log() == 0);
	CHECK(fs_create_directories(ST_APP_SESSION_TEST_DIRECTORY) == 0);
	file = fopen(paths.settings_file, "wb");
	CHECK(file != NULL);
	CHECK(fputs("[SyncTERM]\nScreenMode=Amiga640x400\n", file) >= 0);
	CHECK(fclose(file) == 0);
	file = fopen(paths.personal_list, "wb");
	CHECK(file != NULL);
	CHECK(fputs("[Test]\nAddress=example.invalid\n", file) >= 0);
	CHECK(fclose(file) == 0);
	CHECK(st_settings_load(&paths) == 0);
	CHECK(st_console_display_create(&display, &display_config) == 0);
	CHECK(st_console_create_on_display(&console, display, &mode->console) == 0);
	CHECK(st_console_create_on_display(&picker, display, &mode->console) == 0);
	CHECK(st_builtin_font_get(0, 8, 8, &font));
	CHECK(st_console_register_static_font(console, &font, &identifier) == 0);
	CHECK(identifier == 0);
	CHECK(st_console_register_static_font(picker, &font, &identifier) == 0);
	CHECK(identifier == 0);
	CHECK(st_hyperlink_registry_create(&hyperlinks) == 0);
	struct st_wren_host_config host_config = {
		.display = display, .console = console, .picker_console = picker,
		.paths = &paths, .application_title = "Session mode test",
		.safe_mode = true,
		.hyperlinks = hyperlinks,
	};
	CHECK(st_wren_host_create(&host, &host_config) == 0);
	CHECK(check_mode(host, console, picker, ST_SCREEN_MODE_AMIGA_640X400, 80, 50, 8) == 0);
	CHECK(st_wren_host_begin_session_mode(NULL, ST_SCREEN_MODE_80X25)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_wren_host_end_session_mode(NULL) == IO_ERROR_INVALID_ARGUMENT);
	st_profile_init(&profile);
	profile.hide_popups = true;
	profile.hide_status = true;
	profile.screen_mode = ST_SCREEN_MODE_PRESTEL;
	profile.connection_type = ST_CONNECTION_UNKNOWN;
	/* A menu-selected mode is deliberately different from startup mode. */
	CHECK(st_wren_host_set_screen_mode(host, ST_SCREEN_MODE_80X28) == 0);
	CHECK(st_wren_host_begin_session_mode(host, (enum st_screen_mode)-1)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(check_mode(host, console, picker, ST_SCREEN_MODE_80X28, 80, 28, 14) == 0);
	CHECK(run_selected(host, display, console, &profile, &paths, NULL,
	    hyperlinks, NULL, &disposition) != 0);
	CHECK(check_mode(host, console, picker, ST_SCREEN_MODE_80X28, 80, 28, 14) == 0);
	/* Component contract: restoring a non-ANSI mode restores its palette. */
	CHECK(st_wren_host_set_screen_mode(host, ST_SCREEN_MODE_PRESTEL) == 0);
	CHECK(st_wren_host_begin_session_mode(host, ST_SCREEN_MODE_80X25) == 0);
	CHECK(st_wren_host_end_session_mode(host) == 0);
	CHECK(check_mode(host, console, picker, ST_SCREEN_MODE_PRESTEL, 40, 25, 20) == 0);
	uint32_t palette[16];
	enum st_console_blink_mode blink;
	CHECK(st_console_get_palette(console, 0, palette, 16) == 0);
	CHECK(palette[4] == UINT32_C(0x0000ff));
	CHECK(st_console_get_blink_mode(console, &blink) == 0);
	CHECK(blink == ST_CONSOLE_BLINK_PRESTEL);

	/* CURRENT after an external resize has no named-mode equivalent. */
	CHECK(st_wren_host_set_screen_mode(host, ST_SCREEN_MODE_80X28) == 0);
	CHECK(st_wren_host_begin_session_mode(host, ST_SCREEN_MODE_CURRENT) == 0);
	/* A remote mode change need not update the menu host's mode enum. */
	mode = st_screen_mode_get(ST_SCREEN_MODE_80X50);
	CHECK(st_console_reconfigure(console, &mode->console) == 0);
	CHECK(st_console_set_video_flags(console, ST_CONSOLE_VIDEO_NO_BLINK) == 0);
	CHECK(st_console_set_cursor(console, false, ST_CONSOLE_CURSOR_BLOCK) == 0);
	CHECK(st_wren_host_end_session_mode(host) == 0);
	CHECK(check_mode(host, console, picker, ST_SCREEN_MODE_80X28, 80, 28, 14) == 0);
	/* Drive the same resize path used by Input.poll(). */
	const struct st_console_event resize = {
		.type = ST_CONSOLE_EVENT_RESIZE,
		.data.resize = {.columns = 93, .rows = 31},
	};
	CHECK(st_console_post_event(console, &resize) == 0);
	CHECK(st_wren_host_begin_session_mode(host, ST_SCREEN_MODE_CURRENT) == 0);
	CHECK(st_wren_host_begin_session_mode(host, ST_SCREEN_MODE_80X25)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_wren_host_end_session_mode(host) == 0);
	/* The persistent menu consumes the resize, then selects its only row. */
	const struct st_console_event enter = {
		.type = ST_CONSOLE_EVENT_KEY_CODE, .data.key.code = 13,
	};
	CHECK(st_console_post_event(console, &enter) == 0);
	struct st_profile selected;
	bool has_selection = false;
	st_profile_init(&selected);
	CHECK(st_wren_host_run_menu(host, &selected, &has_selection) == 0);
	CHECK(has_selection && strcmp(selected.name, "Test") == 0);
	st_profile_dispose(&selected);
	CHECK(st_wren_host_begin_session_mode(host, profile.screen_mode) == 0);
	CHECK(st_wren_host_end_session_mode(host) == 0);
	CHECK(check_mode(host, console, picker, ST_SCREEN_MODE_CURRENT, 93, 31, 14) == 0);
	st_settings.custom_columns = 91;
	st_settings.custom_rows = 27;
	st_settings.custom_font_height = 8;
	st_settings.custom_aspect_width = 5;
	st_settings.custom_aspect_height = 4;
	CHECK(st_wren_host_set_screen_mode(host, ST_SCREEN_MODE_CUSTOM) == 0);
	CHECK(st_wren_host_begin_session_mode(host, profile.screen_mode) == 0);
	CHECK(st_wren_host_end_session_mode(host) == 0);
	CHECK(check_mode(host, console, picker, ST_SCREEN_MODE_CUSTOM, 91, 27, 8) == 0);
	uint32_t aspect_width, aspect_height;
	CHECK(st_console_get_aspect_ratio(console, &aspect_width, &aspect_height) == 0);
	CHECK(aspect_width == 5 && aspect_height == 4);
	CHECK(st_wren_host_end_session_mode(host) == 0);

	st_profile_dispose(&profile);
	CHECK(check_log_policy(&host_config, host, display, console, &paths, hyperlinks) == 0);
	st_wren_host_destroy(host);
	st_console_destroy(picker);
	st_console_destroy(console);
	/* The single-console text-only path has no pixel planes to query. */
	const struct st_console_config text_config = {
		.columns = 80, .rows = 25, .wrap = true,
	};
	CHECK(st_console_create_on_display(&console, display, &text_config) == 0);
	CHECK(st_console_register_static_font(console, &font, &identifier) == 0);
	host_config.display = NULL;
	host_config.console = console;
	host_config.picker_console = NULL;
	CHECK(st_wren_host_create(&host, &host_config) == 0);
	CHECK(st_wren_host_begin_session_mode(host, ST_SCREEN_MODE_80X50) == 0);
	CHECK(st_wren_host_end_session_mode(host) == 0);
	uint32_t width, height;
	CHECK(st_console_size(console, &width, &height) == 0);
	CHECK(width == 80 && height == 25);
	CHECK(st_console_cell_pixel_size(console, &width, &height) == 0);
	CHECK(width == 0 && height == 0);
	st_wren_host_destroy(host);
	st_console_destroy(console);
	st_console_display_destroy(display);
	st_hyperlink_registry_destroy(hyperlinks);
	st_settings_shutdown();
	return 0;
}
