/* SyncTERM desktop application entry point. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_app_paths.h"
#include "st_app_state.h"
#include "st_audio_runtime.h"
#include "st_bbs_list.h"
#include "st_builtin_fonts.h"
#include "st_command_line.h"
#include "st_console.h"
#include "st_console_startup.h"
#include "st_connection.h"
#include "filesystem.h"
#include "st_font_catalog.h"
#include "st_hyperlink.h"
#include "io_error.h"
#include "st_presentation_font.h"
#include "st_unicode_fonts.h"
#ifdef ST_HAVE_UNIFONT
#include "st_unifont.h"
#endif
#include "st_profile.h"
#include "st_screen_mode.h"
#include "st_session_history.h"
#include "st_settings.h"
#include "st_syncterm_icon.h"
#include "st_terminfo.h"
#include "st_term.h"
#include "st_time.h"
#include "st_threads.h"
#include "st_wren_host.h"
#include "value_convert_time.h"

#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char application_title[] = "SyncTERM 2.0a"
#ifdef ST_DEBUG_BUILD
	" Debug (" __DATE__ ")"
#endif
	;

static void
usage(FILE *stream, const char *program)
{
	fprintf(stream,
	    "usage: %s [options] [URL]\n\n"
	    "options:\n"
	    "  -4            only resolve IPv4 addresses\n"
	    "  -6            only resolve IPv6 addresses\n"
	    "  -b path       use path as the personal BBS list\n"
	    "  -c            hide the status line\n"
	    "  -h            use SSH when URL has no scheme\n"
	    "  -iX           select A[U|C], C, F, I, G[W|F], Q[W|F], S[W|F],\n"
	    "                W, X[W|F], or Y[W|F] output\n"
	    "                A and AU are UTF-8 ANSI; AC is CP437 ANSI\n"
	    "  -l#           use 14, 21, 25, 28, 30, 43, 50, or 60 rows\n"
	    "  -n path       use path as the configuration file\n"
	    "  -q            hide connection popups\n"
	    "  -r            use rlogin when URL has no scheme\n"
	    "  -s            prevent writing or browsing local files\n"
	    "  -t            use telnet when URL has no scheme\n"
	    "  -w script     load script on every connection\n"
	    "  -T            as the only argument, write terminfo source\n"
	    "  -v            as the only argument, write version information\n"
	    "  --help        show this help\n\n"
	    "Options accepting values also accept attached values (for example,\n"
	    "-isf, -l43, and -b/path/to/list). Option letters are case-insensitive.\n"
	    "URL: [(rlogin|telnet|telnets|ssh|sshna|raw|ghost|mqtts)://][user[:password]@]host[:port]\n",
	    program);
}

static void
apply_profile_overrides(struct st_profile *profile,
	const struct st_command_line *command)
{
	if (command->address_family != ST_ADDRESS_FAMILY_UNSPECIFIED)
		profile->address_family = command->address_family;
	if (command->hide_status)
		profile->hide_status = true;
	if (command->hide_popups)
		profile->hide_popups = true;
}

static void
persist_window_scaling(st_wren_host_t *host, st_console_display_t *display,
	bool normal_exit, bool safe_mode)
{
	enum st_screen_mode current_mode;
	double scaling;
	int status;

	if (host == NULL || display == NULL
	    || st_wren_host_get_screen_mode(host, &current_mode) != 0
	    || !st_app_should_persist_scaling(normal_exit, safe_mode,
	    (enum st_screen_mode)st_settings.startup_mode, current_mode))
		return;
	status = st_console_display_get_scaling(display, &scaling);
	if (status == IO_ERROR_NOT_SUPPORTED)
		return;
	if (status != 0) {
		fprintf(stderr, "SyncTERM: cannot read window scaling (%d)\n",
		    status);
		return;
	}
	if (scaling != st_settings.scaling_factor) {
		status = st_settings_persist_scaling_factor(scaling);
		if (status != 0)
			fprintf(stderr,
			    "SyncTERM: cannot save window scaling (%d)\n", status);
	}
}

static enum st_settings_font_path
font_path_slot(uint32_t width, uint32_t height, bool *valid)
{
	*valid = true;
	if (width == 8 && height == 8)
		return ST_SETTINGS_FONT_8X8;
	if (width == 8 && height == 14)
		return ST_SETTINGS_FONT_8X14;
	if (width == 8 && height == 16)
		return ST_SETTINGS_FONT_8X16;
	if (width == 12 && height == 20)
		return ST_SETTINGS_FONT_12X20;
	*valid = false;
	return ST_SETTINGS_FONT_8X16;
}

static int
prepare_session_fonts(st_font_catalog_t **result, st_console_t *console,
	const struct st_profile *profile, uint8_t fallback, uint8_t *selected)
{
	st_font_catalog_t *catalog = NULL;
	uint32_t width;
	uint32_t height;
	bool valid;
	enum st_settings_font_path slot;
	int status = st_font_catalog_create(&catalog, console);

	if (status != 0)
		return status;
	if (st_console_cell_pixel_size(console, &width, &height) == 0) {
		slot = font_path_slot(width, height, &valid);
		if (valid) {
			for (size_t i = 0; i < st_settings_font_count()
			    && i + ST_FONT_CATALOG_FIRST_CUSTOM <= UINT8_MAX; ++i) {
				const struct st_settings_font *font
				    = st_settings_font_at(i);

				if (font->path[slot] != NULL)
					(void)st_font_catalog_load_custom_codepage(catalog,
					    (uint8_t)(i
					    + ST_FONT_CATALOG_FIRST_CUSTOM),
					    font->path[slot],
					    profile->encoding == ST_PROFILE_ENCODING_UTF8
					    ? ST_CODEPAGE_CP437
					    : (enum st_codepage)profile->encoding);
			}
		}
	}
	*selected = fallback;
	if (profile->font[0] != '\0') {
		for (size_t i = 0; i < st_builtin_font_count(); ++i) {
			if (strcmp(profile->font, st_builtin_font_name(i)) == 0) {
				*selected = (uint8_t)i;
				break;
			}
		}
		for (size_t i = 0; i < st_settings_font_count()
		    && i + ST_FONT_CATALOG_FIRST_CUSTOM <= UINT8_MAX; ++i) {
			const struct st_settings_font *font = st_settings_font_at(i);

			if (font->name != NULL
			    && strcmp(profile->font, font->name) == 0) {
				*selected = (uint8_t)(i
				    + ST_FONT_CATALOG_FIRST_CUSTOM);
				break;
			}
		}
	}
	{
		uint16_t identifier;

		if (!st_font_catalog_identifier(catalog, *selected, &identifier))
			*selected = fallback;
	}
	*result = catalog;
	return 0;
}

static int
run_rip_prompt(void *context, const char *question, const char *initial,
	size_t field_width, bool reject_blank, char **result)
{
	(void)reject_blank;
	return st_wren_host_prompt(context, "RIP Input", question, initial,
	    field_width, result);
}

struct connection_ui {
	st_wren_host_t *host;
	st_console_t *console;
	struct st_profile *profile;
	bool closed;
	enum st_connection_setup_phase phase;
	st_mutex_t log_mutex;
	char *log;
	size_t log_size;
	size_t log_capacity;
	FILE *log_file; /* Protected by log_mutex, like the viewer buffer. */
};

static bool
connection_log_stamp(FILE *file, const char *message)
{
	static const char *const months[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	struct tm parts;
	if (!value_convert_localtime(time(NULL), &parts)
	    || parts.tm_mon < 0 || parts.tm_mon > 11)
		return false;
	return fprintf(file, "%s %2d %02d:%02d:%02d %s\n", months[parts.tm_mon],
	    parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec, message) >= 0;
}

static void
connection_log_open(struct connection_ui *ui)
{
	if (ui->log_file != NULL || st_wren_host_safe_mode(ui->host) || ui->profile->log_path == NULL
	    || ui->profile->log_path[0] == '\0')
		return;
	if (fs_open(&ui->log_file, ui->profile->log_path,
	    ui->profile->append_log ? "a" : "w") != 0)
		fprintf(stderr, "SyncTERM: unable to open connection log\n");
	else if (!connection_log_stamp(ui->log_file, "Log opened"))
		fprintf(stderr, "SyncTERM: unable to write connection log\n");
}

static void
connection_log_close(struct connection_ui *ui)
{
	bool okay = true;
	(void)st_mutex_lock(&ui->log_mutex);
	if (ui->log_file != NULL) {
		okay = connection_log_stamp(ui->log_file, "Log closed");
		if (fputs("---------------\n", ui->log_file) == EOF)
			okay = false;
		if (fclose(ui->log_file) != 0)
			okay = false;
		ui->log_file = NULL;
	}
	(void)st_mutex_unlock(&ui->log_mutex);
	if (!okay)
		fprintf(stderr, "SyncTERM: unable to finish connection log\n");
}

static void
connection_transfer_log(void *context, int level, const char *message)
{
	struct connection_ui *ui = context;
	(void)level; /* The connected VM applies the live transfer threshold. */
	(void)st_mutex_lock(&ui->log_mutex);
	if (ui->log_file != NULL && !connection_log_stamp(ui->log_file, message))
		fprintf(stderr, "SyncTERM: unable to write transfer log\n");
	(void)st_mutex_unlock(&ui->log_mutex);
}

static void
connection_status(void *context, const char *message)
{
	struct connection_ui *ui = context;
	if (!ui->profile->hide_popups)
		(void)st_wren_host_status(ui->host, "Connecting", message);
}

static void
connection_log(void *context, enum st_connection_log_level level,
	const char *source, const char *message)
{
	struct connection_ui *ui = context;
	const int priorities[] = {3, 4, 6, 7};
	char line[2048];
	if ((unsigned)level >= sizeof(priorities) / sizeof(priorities[0])
	    || priorities[level] > ui->profile->protocol_log_level) return;
	int count = snprintf(line, sizeof(line), "%s %s: %s\n", source == NULL ? "Protocol" : source,
	    level == ST_CONNECTION_LOG_ERROR ? "Error" : level == ST_CONNECTION_LOG_WARNING
	    ? "Warning" : level == ST_CONNECTION_LOG_INFO ? "Info" : "Debug", message == NULL ? "" : message);
	if (count < 0) return;
	size_t length = (size_t)count < sizeof(line) ? (size_t)count : sizeof(line) - 1;
	/* Never render terminal escapes or embedded controls from a remote log. */
	for (size_t i = 0; i < length; ++i)
		if ((unsigned char)line[i] < 32 && line[i] != '\n' && line[i] != '\t') line[i] = '?';
	(void)st_mutex_lock(&ui->log_mutex);
	if (ui->log_file != NULL && fwrite(line, 1, length, ui->log_file) != length)
		fprintf(stderr, "SyncTERM: unable to write protocol log\n");
	if (ui->log_size < 1024U * 1024U) {
		size_t remaining = 1024U * 1024U - ui->log_size;
		if (length > remaining) length = remaining;
		size_t needed = ui->log_size + length + 1;
		if (needed > ui->log_capacity) {
			size_t capacity = ui->log_capacity == 0 ? 4096 : ui->log_capacity;
			while (capacity < needed) capacity *= 2;
			if (capacity > 1024U * 1024U + 1) capacity = 1024U * 1024U + 1;
			char *next = realloc(ui->log, capacity);
			if (next != NULL) { ui->log = next; ui->log_capacity = capacity; }
		}
		if (needed <= ui->log_capacity) {
			memcpy(ui->log + ui->log_size, line, length);
			ui->log_size += length;
			ui->log[ui->log_size] = '\0';
		}
	}
	(void)st_mutex_unlock(&ui->log_mutex);
}

static void
connection_alert(void *context, const char *title, const char *message)
{
	struct connection_ui *ui = context;
	connection_status(ui, NULL);
	if (!ui->profile->hide_popups) (void)st_wren_host_alert(ui->host, title, message);
}

static char *
connection_help_escape(char *output, const char *text, size_t length,
	bool hard_breaks)
{
	for (size_t i = 0; i < length; ++i) {
		char c = text[i];
		if (c == '\r')
			continue;
		if (c == '\n' && hard_breaks) {
			*output++ = ' ';
			*output++ = ' ';
		}
		if (c == '\\' || c == '*' || c == '`')
			*output++ = '\\';
		*output++ = c;
	}
	return output;
}

static char *
connection_help_body(struct connection_ui *ui, const char *details)
{
	static const char details_heading[] = "# Connection Details\n\n";
	static const char log_heading[] = "# Protocol Session Log\n\n";
	const size_t overhead = sizeof(details_heading) + sizeof(log_heading) + 4;
	size_t details_size = details == NULL ? 0 : strlen(details);
	char *body = NULL;

	(void)st_mutex_lock(&ui->log_mutex);
	/* Escaping expands a byte to at most three bytes (a hard break,
	 * or a log newline plus the next bullet).  Reserve the headings,
	 * section separator, final bullet prefix and terminator as well. */
	if (details_size > SIZE_MAX - ui->log_size
	    || details_size + ui->log_size > (SIZE_MAX - overhead) / 3)
		goto done;
	if (details_size == 0 && ui->log_size == 0)
		goto done;
	body = malloc(3 * (details_size + ui->log_size) + overhead);
	if (body == NULL)
		goto done;
	char *output = body;
	if (details_size != 0) {
		memcpy(output, details_heading, sizeof(details_heading) - 1);
		output += sizeof(details_heading) - 1;
		output = connection_help_escape(output, details, details_size, true);
	}
	if (ui->log_size != 0) {
		if (output != body) {
			*output++ = '\n';
			*output++ = '\n';
		}
		memcpy(output, log_heading, sizeof(log_heading) - 1);
		output += sizeof(log_heading) - 1;
		const char *line = ui->log;
		while (*line != '\0') {
			const char *end = strchr(line, '\n');
			size_t length = end == NULL ? strlen(line) : (size_t)(end - line);
			*output++ = '-';
			*output++ = ' ';
			output = connection_help_escape(output, line, length, false);
			*output++ = '\n';
			if (end == NULL)
				break;
			line = end + 1;
		}
	}
	*output = '\0';
done:
	(void)st_mutex_unlock(&ui->log_mutex);
	return body;
}

static void
connection_help(void *context, const char *title, const char *details)
{
	struct connection_ui *ui = context;
	if (ui->profile->hide_popups) return;
	connection_status(ui, NULL);
	char *body = connection_help_body(ui, details);
	if (body != NULL) {
		(void)st_wren_host_help(ui->host, title, body);
		free(body);
	}
}

static int
remember_tls_version(void *context, int version)
{
	struct connection_ui *ui = context;
	int status = st_wren_host_remember_tls_version(ui->host, ui->profile, version);
	ui->profile->tls_version_floor = version;
	return status;
}

static int
trust_tls_certificate(void *context, const char *pem, char **filename)
{
	struct connection_ui *ui = context;
	return st_wren_host_trust_tls(ui->host, ui->profile, pem, filename);
}

static enum st_connection_setup_action
connection_setup(void *context, enum st_connection_setup_phase phase)
{
	struct connection_ui *ui = context;
	struct st_console_event event, resize = {0};
	bool resized = false, cancel = ui->closed;
	if (phase != ui->phase) {
		const char *messages[] = {NULL, "Looking up host", "Connecting...",
		    "Opening device", "Initializing...", "Dialing...", "Starting shell",
		    "Activating Session", "Authenticating", "Discovering nodes"};
		ui->phase = phase;
		if ((unsigned)phase < sizeof(messages) / sizeof(messages[0]))
			connection_status(ui, messages[phase]);
	}
	while (!ui->closed && st_console_event_try(ui->console, &event) == 0) {
		if (event.type == ST_CONSOLE_EVENT_CLOSE) {
			ui->closed = true;
			cancel = true;
			(void)st_console_post_event(ui->console, &event);
		} else if (event.type == ST_CONSOLE_EVENT_RESIZE) {
			resize = event;
			resized = true;
		} else if ((event.type == ST_CONSOLE_EVENT_KEY_CODE
		    && event.data.key.code == 27)
		    || (event.type == ST_CONSOLE_EVENT_TEXT
		    && event.data.text.codepoint == 27))
			cancel = true;
	}
	if (resized) (void)st_console_post_event(ui->console, &resize);
	return cancel ? ST_CONNECTION_SETUP_CANCEL : ST_CONNECTION_SETUP_CONTINUE;
}

static int
connection_prompt(void *context, const char *title, const char *message,
	const char *initial, size_t max_length, bool masked, char **result)
{
	struct connection_ui *ui = context;
	connection_status(ui, NULL);
	if (ui->profile->hide_popups) {
		*result = NULL;
		return IO_ERROR_PASSWORD_REQUIRED;
	}
	return st_wren_host_prompt_masked(ui->host, title, message, initial,
	    max_length, masked, result);
}

static int
connection_choice(void *context, const char *title, const char *message,
	const char *const *choices, size_t count)
{
	struct connection_ui *ui = context;
	connection_status(ui, NULL);
	if (ui->profile->hide_popups) return IO_ERROR_PERMISSION_DENIED;
	return st_wren_host_choice(ui->host, title, message, choices, count);
}

static int
remember_hostkey(void *context, const uint8_t fingerprint[32], bool explicit_update)
{
	struct connection_ui *ui = context;
	return st_wren_host_remember_ssh_key(ui->host, ui->profile, fingerprint,
	    explicit_update);
}

static int
run_wren_event_hook(void *context, const struct st_console_event *event,
	bool *consumed)
{
	st_wren_connected_t *connected = *(st_wren_connected_t **)context;

	if (connected == NULL) {
		*consumed = false;
		return 0;
	}
	return st_wren_connected_handle_event(connected, event, consumed);
}

static bool
wren_wire_filter_active(void *context)
{
	return st_wren_connected_has_wire_hooks(
	    *(st_wren_connected_t **)context);
}

static int
run_wren_wire_filter(void *context, uint8_t byte, bool *consumed,
	uint8_t *replacement, size_t capacity, size_t *replacement_size)
{
	return st_wren_connected_filter_wire_byte(
	    *(st_wren_connected_t **)context, byte, consumed, replacement,
	    capacity, replacement_size);
}

static bool
wren_input_filter_active(void *context)
{
	return st_wren_connected_has_input_hooks(
	    *(st_wren_connected_t **)context);
}

static int
run_wren_input_filter(void *context, uint32_t scalar, bool *consumed,
	char *replacement, size_t capacity, size_t *replacement_size)
{
	return st_wren_connected_filter_input_scalar(
	    *(st_wren_connected_t **)context, scalar, consumed, replacement,
	    capacity, replacement_size);
}

static bool
wren_capture_active(void *context)
{
	return st_wren_connected_capture_active(
	    *(st_wren_connected_t **)context);
}

static int
run_wren_capture_input(void *context, const char *bytes, size_t size,
	bool protocol)
{
	return st_wren_connected_capture_input(
	    *(st_wren_connected_t **)context, bytes, size, protocol);
}

static int
run_session(st_wren_host_t *host, st_console_display_t *display,
	st_console_t *console, struct st_profile *profile,
	const struct st_app_paths *paths,
	st_audio_runtime_t *audio, st_hyperlink_registry_t *hyperlinks,
	st_session_history_t *history, enum st_term_disposition *disposition)
{
	const struct st_screen_mode_descriptor *mode;
	enum st_screen_mode session_mode
	    = (enum st_screen_mode)profile->screen_mode;
	st_font_catalog_t *fonts = NULL;
	st_connection_t *connection = NULL;
	st_term_session_t *session = NULL;
	st_wren_connected_t *connected = NULL;
	char *cache_directory = NULL;
	char *disconnect_details = NULL;
	bool notify_disconnect;
	struct st_connection_error error = {0};
	struct connection_ui connection_ui = {
		.host = host, .console = console, .profile = profile,
	};
	struct st_connection_hooks connection_hooks = {
		.setup = connection_setup, .setup_context = &connection_ui,
		.prompt = connection_prompt, .choice = connection_choice,
		.ui_context = &connection_ui, .ssh_key_file = paths->keys_file,
		.remember_hostkey = profile->name[0] != '\0' ? remember_hostkey : NULL,
		.alert = connection_alert, .help = connection_help, .status = connection_status,
		.log = connection_log, .log_context = &connection_ui,
		.remember_tls_version = remember_tls_version,
		.trust_tls_certificate = st_wren_host_can_trust_tls(host, profile)
		    ? trust_tls_certificate : NULL,
	};
	uint8_t selector = 0;
	int status;

	if (profile->screen_mode == ST_SCREEN_MODE_CURRENT) {
		status = st_wren_host_get_screen_mode(host, &session_mode);
		if (status != 0)
			return status;
	}
	status = st_console_display_activate(display, console);
	if (status != 0)
		return status;
	mode = st_screen_mode_get(session_mode);
	st_wren_host_apply_tls_version(host, profile);
	if (st_mutex_init(&connection_ui.log_mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS)
		return IO_ERROR_OUT_OF_MEMORY;
	connection_log_open(&connection_ui);
	if (mode != NULL)
		selector = mode->font_selector;
	status = prepare_session_fonts(&fonts, console, profile, selector,
	    &selector);
	if (status != 0)
		goto finished;
	status = st_term_connect_profile(&connection, profile, console, &connection_hooks,
	    &error);
	connection_status(&connection_ui, NULL);
	if (status != 0) {
		fprintf(stderr, "SyncTERM: connection failed (%d)%s%s\n", status,
		    error.detail[0] == '\0' ? "" : ": ",
		    error.detail[0] == '\0' ? "" : error.detail);
		if (status != IO_ERROR_CANCELLED && !profile->hide_popups && !error.presented) {
			const char *choices[] = {"OK", "View Protocol Log"};
			const char *detail = error.detail[0] == '\0' ? "Unable to establish the connection." : error.detail;
			while (connection_choice(&connection_ui, "Connection failed", detail, choices, 2) == 1)
				connection_help(&connection_ui, "Connection failed", detail);
		}
		goto finished;
	}
	{
		int stats_status = st_wren_host_record_connection(host, profile);
		if (stats_status != 0)
			fprintf(stderr, "SyncTERM: unable to save connection statistics (%d)\n",
			    stats_status);
	}
	if (st_bbs_name_valid(profile->name)) {
		status = st_bbs_cache_path_for_name(&cache_directory,
		    paths->cache_directory, profile->name);
		if (status != 0)
			goto finished;
	}
	const struct st_term_config config = {
		.console = console,
		.connection = connection,
		.fonts = fonts,
		.audio = audio,
		.hyperlinks = hyperlinks,
		.history = history,
		.screen_mode = session_mode,
		.encoding = st_term_profile_encoding(profile),
		.rip_version = st_term_profile_rip_version(profile),
		.rip_directory = cache_directory,
		.apc_directory = cache_directory,
		.application_version = application_title,
		.codepage = profile->encoding == ST_PROFILE_ENCODING_UTF8
		    ? st_font_catalog_codepage(fonts, selector)
		    : (enum st_codepage)profile->encoding,
		.font_selector = selector,
		.hide_status = profile->hide_status,
		.expand_lf = profile->expand_lf,
		.force_lcf = profile->force_lcf,
		.yellow_is_yellow = profile->yellow_is_yellow,
		.palette = profile->palette,
		.palette_size = profile->palette_size,
		.suppress_responses = profile->connection_type == ST_CONNECTION_MQTT,
		.throttle_supported = profile->connection_type != ST_CONNECTION_SERIAL
		    && profile->connection_type != ST_CONNECTION_SERIAL_3WIRE,
		.initial_throttle_bps = profile->bps_rate,
		.rip_prompt = run_rip_prompt,
		.rip_prompt_context = host,
		.event_hook = run_wren_event_hook,
		.event_hook_context = &connected,
		.wire_filter = run_wren_wire_filter,
		.wire_filter_active = wren_wire_filter_active,
		.wire_filter_context = &connected,
		.input_filter = run_wren_input_filter,
		.input_filter_active = wren_input_filter_active,
		.input_filter_context = &connected,
		.capture = run_wren_capture_input,
		.capture_active = wren_capture_active,
		.capture_context = &connected,
	};
	status = st_term_session_create(&session, &config);
	if (status == 0) {
		st_cterm_set_music_mode(st_term_session_cterm(session), profile->music);
		const struct st_wren_connected_config wren_config = {
			.host = host,
			.console = console,
			.profile = profile,
			.sftp = st_connection_sftp(connection),
			.cterm = st_term_session_cterm(session),
			.connection = connection,
			.session = session,
			.status_enabled = !profile->hide_status,
			.transfer_log = connection_transfer_log,
			.transfer_log_context = &connection_ui,
		};

		status = st_wren_connected_create(&connected, &wren_config);
		if (status != 0) {
			fprintf(stderr,
			    "SyncTERM: connected Wren initialization failed (%d); "
			    "continuing without scripts\n", status);
			status = st_term_session_run(session, disposition);
			goto finished;
		}
	}
	if (status == 0) {
		bool finished = false;
		bool shell_close_notified = false;
		uint64_t last_status_ms = 0;

		while (!finished && status == 0) {
			uint64_t now;
			bool show_status;

			st_term_session_set_auxiliary_active(session,
			    st_connection_auxiliary_alive(connection)
			    && st_wren_connected_auxiliary_active(connected));
			status = st_term_session_pump(session, disposition, &finished);
			if (!shell_close_notified
			    && (profile->connection_type == ST_CONNECTION_SHELL
			    || ((profile->connection_type == ST_CONNECTION_SSH
			    || profile->connection_type == ST_CONNECTION_SSH_NO_AUTH)
			    && st_connection_auxiliary_alive(connection)))
			    && !st_connection_is_connected(connection)) {
				int hook_status = st_wren_connected_shell_closed(connected);

				shell_close_notified = true;
				if (status == 0)
					status = hook_status;
			}
			if (status == 0)
				status = st_wren_connected_pump(connected, 0);
			if (status != 0 || finished)
				break;
			if (st_time_monotonic_ms(&now) != 0)
				continue;
			if (last_status_ms != 0 && now - last_status_ms < 100)
				continue;
			last_status_ms = now;
			show_status = st_cterm_status_display(
			    st_term_session_cterm(session)) == 1;
			status = st_wren_connected_render_status(connected,
			    show_status);
		}
	}
finished:
	if (session != NULL) {
		int finish_status = st_cterm_finish(st_term_session_cterm(session));
		if (status == 0) status = finish_status;
	}
	notify_disconnect = st_term_session_remote_closed(session)
	    && !profile->hide_popups && *disposition != ST_TERM_EXIT_APPLICATION
	    && !st_wren_host_exit_requested(host);
	if (notify_disconnect)
		disconnect_details = st_wren_connected_status_details(connected);
	st_wren_connected_destroy(connected);
	connected = NULL;
	if (connection != NULL) {
		int close_status = st_connection_close(connection);

		connection = NULL;
		if (status == 0)
			status = close_status;
	}
	/* Close first: the log viewer must include final transport diagnostics.
	 * Retain CTerm until the notice is dismissed and offline history saved. */
	if (notify_disconnect && st_term_session_prepare_disconnect(session) == 0) {
		const char *reason = "Remote host dropped connection";
		size_t size = strlen(reason) + (disconnect_details == NULL ? 0 : strlen(disconnect_details)) + 4;
		char *message = malloc(size);
		char *body = NULL;
		if (message != NULL) {
			(void)snprintf(message, size, "%s%s%s", reason,
			    disconnect_details == NULL ? "" : "\n\n",
			    disconnect_details == NULL ? "" : disconnect_details);
			body = connection_help_body(&connection_ui, message);
		}
		int notice_status = st_wren_host_disconnected(host, body != NULL ? body
		    : message != NULL ? message : reason);
		if (status == 0) status = notice_status;
		if (st_wren_host_exit_requested(host)) *disposition = ST_TERM_EXIT_APPLICATION;
		free(body); free(message);
	}
	connection_log_close(&connection_ui);
	free(disconnect_details);
	st_term_session_destroy(&session);
	connection_status(&connection_ui, NULL);
	free(connection_ui.log);
	st_mutex_destroy(&connection_ui.log_mutex);
	st_font_catalog_destroy(fonts);
	free(cache_directory);
	return status;
}

static int
run_selected(st_wren_host_t *host, st_console_display_t *display,
	st_console_t *console, struct st_profile *profile,
	const struct st_app_paths *paths,
	st_audio_runtime_t *audio, st_hyperlink_registry_t *hyperlinks,
	st_session_history_t *history, enum st_term_disposition *disposition)
{
	int status = st_wren_host_begin_session_mode(host,
	    (enum st_screen_mode)profile->screen_mode);
	int restore_status;

	if (status == 0)
		status = run_session(host, display, console, profile, paths, audio,
		    hyperlinks, history, disposition);
	/* Restore after disconnect notices and CTerm destruction, even when
	 * connection setup failed or the session requested application exit. */
	restore_status = st_wren_host_end_session_mode(host);
	if (restore_status != 0)
		*disposition = ST_TERM_EXIT_APPLICATION;
	return restore_status != 0 ? restore_status : status;
}

int
main(int argc, char **argv)
{
	struct st_command_line command;
	struct st_app_path_overrides overrides = {0};
	struct st_app_paths paths = {0};
	struct st_console_startup startup;
	struct st_console_display_config display_config;
	struct st_console_config picker_config;
	struct st_wren_host_config host_config;
	struct st_profile selected;
	st_console_t *console = NULL;
	st_console_t *picker_console = NULL;
	st_console_display_t *display = NULL;
	st_session_history_t *session_history = NULL;
	st_hyperlink_registry_t *hyperlinks = NULL;
	st_audio_runtime_t *audio_runtime = NULL;
	uint16_t default_font;
	struct st_console_font native_font;
	const struct st_console_font *application_font;
	struct st_presentation_font presentation_font = {0};
	st_wren_host_t *host = NULL;
	const char *invalid = NULL;
	bool has_selection = false;
	bool profile_initialized = false;
	bool settings_loaded = false;
	int status;
	int result = 1;

	(void)setlocale(LC_CTYPE, "");
	status = st_command_line_parse(&command, argc, argv, &invalid);
	if (status != 0) {
		fprintf(stderr, "%s: invalid option or argument: %s\n", argv[0],
		    invalid == NULL ? "" : invalid);
		usage(stderr, argv[0]);
		return 2;
	}
	if (command.action == ST_COMMAND_HELP) {
		usage(stdout, argv[0]);
		return 0;
	}
	if (command.action == ST_COMMAND_VERSION) {
		fprintf(stdout, "%s\n", application_title);
		return 0;
	}
	if (command.action == ST_COMMAND_TERMINFO)
		return st_terminfo_write(stdout) == 0 ? 0 : 1;

	overrides.settings_file = command.settings_path;
	overrides.personal_list = command.list_path;
	status = st_app_paths_init(&paths, &overrides);
	if (status != 0) {
		fprintf(stderr, "SyncTERM: cannot select application paths (%d)\n",
		    status);
		goto finished;
	}
	status = st_settings_load(&paths);
	if (status != 0) {
		fprintf(stderr, "SyncTERM: cannot load settings (%d)\n", status);
		goto finished;
	}
	settings_loaded = true;
	status = st_hyperlink_registry_create(&hyperlinks);
	if (status == 0)
		status = st_session_history_create(&session_history,
	    (size_t)st_settings.scrollback_lines);
	if (status == 0)
		status = st_audio_runtime_create(&audio_runtime,
		    st_settings.audio_output_modes);
	if (status != 0) {
		fprintf(stderr, "SyncTERM: cannot initialize runtime state (%d)\n",
		    status);
		goto finished;
	}
	status = st_console_startup_configure(&startup, &st_settings, &command);
	if (status != 0) {
		fprintf(stderr, "SyncTERM: invalid console settings (%d)\n", status);
		goto finished;
	}
	display_config = startup.display_config;
	display_config.icon = &st_syncterm_icon;
	status = st_console_display_create(&display, &display_config);
	if (status == 0)
		status = st_console_create_on_display(&console, display,
		    &startup.config);
	if (status != 0) {
		fprintf(stderr, "SyncTERM: cannot initialize the console (%d)\n",
		    status);
		goto finished;
	}
	const struct st_screen_mode_descriptor *startup_mode = st_screen_mode_get(
	    (enum st_screen_mode)st_settings.startup_mode);
	uint8_t selector = startup_mode == NULL ? 0 : startup_mode->font_selector;
	if ((startup.config.cell_height == 14
	    || startup.config.cell_height == 20)
	    && st_builtin_font_get(selector, startup.config.cell_width,
	    startup.config.cell_height, &native_font))
		application_font = &native_font;
	else
		application_font = startup.config.cell_height == 8 ? &st_unscii_8_font
#ifdef ST_HAVE_UNIFONT
		    : &st_unifont_font;
#else
		    : &st_unscii_16_font;
#endif
	status = st_presentation_font_create(&presentation_font,
	    application_font);
	if (status != 0) {
		fprintf(stderr,
		    "SyncTERM: cannot create the presentation font (%d)\n",
		    status);
		goto finished;
	}
	application_font = &presentation_font.font;
	status = st_console_register_static_font(console, application_font,
	    &default_font);
	if (status != 0) {
		fprintf(stderr, "SyncTERM: cannot install the default font (%d)\n",
		    status);
		goto finished;
	}
	if (default_font != 0) {
		status = IO_ERROR_CONFLICT;
		fprintf(stderr, "SyncTERM: default font identifier conflict\n");
		goto finished;
	}
	picker_config = startup.config;
	status = st_console_size(console, &picker_config.columns,
	    &picker_config.rows);
	if (status == 0)
		status = st_console_create_on_display(&picker_console, display,
		    &picker_config);
	if (status == 0)
		status = st_console_register_static_font(picker_console,
		    application_font, &default_font);
	if (status != 0 || default_font != 0) {
		fprintf(stderr, "SyncTERM: cannot initialize the picker console (%d)\n",
		    status == 0 ? IO_ERROR_CONFLICT : status);
		goto finished;
	}
	(void)st_console_set_title(console, application_title);
	(void)st_console_set_title(picker_console, application_title);
	if (startup.fullscreen) {
		status = st_console_set_fullscreen(console, true);
		if (status != 0) {
			fprintf(stderr, "SyncTERM: cannot enter fullscreen mode (%d)\n",
			    status);
			goto finished;
		}
	}
	host_config = (struct st_wren_host_config){
		.display = display,
		.console = console,
		.picker_console = picker_console,
		.paths = &paths,
		.application_title = application_title,
		.launch_script = command.wren_script,
		.session_history = session_history,
		.audio_runtime = audio_runtime,
		.hyperlinks = hyperlinks,
		.safe_mode = command.safe_mode,
	};
	status = st_wren_host_create(&host, &host_config);
	if (status != 0) {
		fprintf(stderr, "SyncTERM: cannot initialize the menu (%d)\n",
		    status);
		goto finished;
	}
	(void)st_wren_host_refresh_web_lists(host);
	st_profile_init(&selected);
	profile_initialized = true;
	for (;;) {
		enum st_term_disposition disposition = ST_TERM_RETURN_TO_PICKER;

		has_selection = false;
		if (command.url != NULL) {
			status = st_wren_host_select_url(host, command.url,
			    command.connection_type_set ? command.connection_type
			    : ST_CONNECTION_UNKNOWN, &selected);
			has_selection = status == 0;
		}
		else
			status = st_wren_host_run_menu(host, &selected, &has_selection);
		if (status != 0) {
			const char *detail = st_wren_host_last_error(host);

			fprintf(stderr, "SyncTERM: menu failed (%d)%s%s\n", status,
			    detail == NULL ? "" : ": ",
			    detail == NULL ? "" : detail);
			break;
		}
		if (!has_selection) {
			if (st_wren_host_exit_requested(host))
				result = 0;
			break;
		}
		apply_profile_overrides(&selected, &command);
		status = run_selected(host, display, console, &selected, &paths,
		    audio_runtime, hyperlinks, session_history, &disposition);
		if (status != 0)
			fprintf(stderr, "SyncTERM: session failed (%d)\n", status);
		if (command.url != NULL || disposition == ST_TERM_EXIT_APPLICATION) {
			bool saved;
			int save_status = st_wren_host_offer_save_profile(host,
			    &selected, &saved);

			if (save_status != 0)
				fprintf(stderr, "SyncTERM: cannot offer to save the URL entry (%d)\n",
				    save_status);
			result = status == 0 ? 0 : 1;
			break;
		}
		st_profile_dispose(&selected);
		st_profile_init(&selected);
		status = st_console_display_activate(display, console);
		if (status != 0) {
			fprintf(stderr, "SyncTERM: cannot return to the menu (%d)\n",
			    status);
			break;
		}
	}

finished:
	persist_window_scaling(host, display, result == 0, command.safe_mode);
	if (profile_initialized)
		st_profile_dispose(&selected);
	st_wren_host_destroy(host);
	st_audio_runtime_destroy(audio_runtime);
	st_session_history_destroy(session_history);
	st_hyperlink_registry_destroy(hyperlinks);
	st_console_destroy(picker_console);
	st_console_destroy(console);
	st_console_display_destroy(display);
	st_presentation_font_destroy(&presentation_font);
	if (settings_loaded)
		st_settings_shutdown();
	st_app_paths_dispose(&paths);
	return result;
}
