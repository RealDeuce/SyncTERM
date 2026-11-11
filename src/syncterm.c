/* SyncTERM desktop application entry point. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_app_paths.h"
#include "st_app_state.h"
#include "st_application.h"
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
#include "path.h"
#include "st_presentation_font.h"
#include "st_unicode_fonts.h"
#ifdef ST_HAVE_UNIFONT
#include "st_unifont.h"
#endif
#include "st_profile.h"
#include "st_screen_mode.h"
#include "st_amiga_fonts.h"
#include "st_session_history.h"
#include "st_settings.h"
#include "st_syncterm_icon.h"
#include "st_terminfo.h"
#include "st_term.h"
#include "st_time.h"
#include "st_threads.h"
#include "st_wren_host.h"
#include "value_convert_time.h"

#include "st_console_test.h"
#if ST_CONSOLE_TEST
#include "cterm.h"
#include "cterm_igs.h"
#include "cterm_skypix.h"
#endif

#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

static atomic_bool application_running;

#if ST_CONSOLE_TEST
struct application_inspection {
	st_term_session_t *session;
	st_connection_t *connection;
};

static void inspect_application(void *context, struct st_console_test_application *app)
{
	struct application_inspection *owner = context;
	struct st_cterm_state state;
	st_console_test_number(app, "session_active", owner->session != NULL);
	st_console_test_number(app, "connected", owner->connection != NULL && !st_term_session_remote_closed(owner->session));
	if (owner->session == NULL || st_cterm_state(st_term_session_cterm(owner->session), &state) != 0) return;
#define FIELD(name) st_console_test_number(app, "cterm." #name, state.name)
	FIELD(x); FIELD(y); FIELD(top_margin); FIELD(bottom_margin); FIELD(left_margin); FIELD(right_margin);
	FIELD(attr); FIELD(fg_color); FIELD(bg_color); FIELD(font_slot);
	FIELD(scrollback_lines); FIELD(scrollback_width); FIELD(scrollback_pos); FIELD(scrollback_start);
	FIELD(emulation); FIELD(started); FIELD(skypix); FIELD(graphics_mode);
	/* Read-only test-control observations, captured with the scene/state. */
	const struct cterm_skypix *sky = st_term_session_cterm(owner->session)->skypix_engine;
	if (sky != NULL) {
		st_console_test_number(app, "cterm.skypix_sample_size", (int64_t)sky->audio.count);
		st_console_test_number(app, "cterm.skypix_sample_period", sky->audio.period);
		st_console_test_number(app, "cterm.skypix_playing", sky->audio.playing);
	}
	const struct cterm_igs *igs = st_term_session_cterm(owner->session)->igs_engine;
	if (igs != NULL) {
		st_console_test_number(app, "cterm.igs_resolution", igs->resolution);
		st_console_test_number(app, "cterm.igs_console_foreground", igs->terminal->vt52_foreground);
		st_console_test_number(app, "cterm.igs_console_background", igs->terminal->vt52_background);
		st_console_test_number(app, "cterm.igs_console_reverse", igs->terminal->negative);
		st_console_test_number(app, "cterm.igs_console_escape", igs->terminal->sequence != 0);
		st_console_test_number(app, "cterm.igs_console_parameters",
		    igs->terminal->accumulator != NULL && !igs->terminal->sequence ? igs->terminal->seq_trailing_remain : 0);
		st_console_test_number(app, "cterm.igs_scaling_mode", igs->scaling_mode);
		st_console_test_number(app, "cterm.igs_scaling_resolution", igs->scaling_resolution);
		st_console_test_number(app, "cterm.igs_writing_mode", igs->attributes.writing_mode);
		st_console_test_number(app, "cterm.igs_marker_pen", igs->attributes.marker_pen);
		st_console_test_number(app, "cterm.igs_marker_type", igs->attributes.marker_type);
		st_console_test_number(app, "cterm.igs_marker_height", igs->attributes.marker_height);
		st_console_test_number(app, "cterm.igs_draw_x", igs->draw_x);
		st_console_test_number(app, "cterm.igs_draw_y", igs->draw_y);
		st_console_test_number(app, "cterm.igs_draw_start_x", igs->draw_start_x);
		st_console_test_number(app, "cterm.igs_draw_start_y", igs->draw_start_y);
		st_console_test_number(app, "cterm.igs_last_line", igs->last_line);
		st_console_test_number(app, "cterm.igs_line_pen", igs->attributes.line_pen);
		st_console_test_number(app, "cterm.igs_line_type", igs->attributes.line_type);
		st_console_test_number(app, "cterm.igs_line_width", igs->attributes.line_width);
		st_console_test_number(app, "cterm.igs_line_begin", igs->attributes.line_begin);
		st_console_test_number(app, "cterm.igs_line_end", igs->attributes.line_end);
		st_console_test_number(app, "cterm.igs_user_line", igs->attributes.user_line);
		st_console_test_number(app, "cterm.igs_fill_pen", igs->attributes.fill_pen);
		st_console_test_number(app, "cterm.igs_fill_interior", igs->attributes.fill_interior);
		st_console_test_number(app, "cterm.igs_fill_index", igs->attributes.fill_index);
		st_console_test_number(app, "cterm.igs_fill_perimeter", igs->attributes.fill_perimeter);
		st_console_test_number(app, "cterm.igs_random_seed", igs->random_seed);
		st_console_test_number(app, "cterm.igs_r_lower", igs->random_range[0][0]);
		st_console_test_number(app, "cterm.igs_r_upper", igs->random_range[0][1]);
		st_console_test_number(app, "cterm.igs_R_lower", igs->random_range[1][0]);
		st_console_test_number(app, "cterm.igs_R_upper", igs->random_range[1][1]);
		st_console_test_number(app, "cterm.igs_pattern_seed", igs->pattern_seed);
		for (unsigned i = 0; i < 16; ++i) {
			char name[48];
			(void)snprintf(name, sizeof(name), "cterm.igs_user_fill_%u", i);
			st_console_test_number(app, name, igs->attributes.user_fill[i]);
		}
		st_console_test_number(app, "cterm.igs_shared_text_size", (int64_t)igs->shared_text_size);
		st_console_test_number(app, "cterm.igs_text_counter", (int64_t)igs->text_counter);
		st_console_test_number(app, "cterm.igs_text_deadline", (int64_t)igs->text_deadline);
		st_console_test_number(app, "cterm.igs_timed_pause", igs->timed_pause);
		st_console_test_number(app, "cterm.igs_input_paused", cterm_igs_paused(igs));
		st_console_test_number(app, "cterm.igs_double_step", igs->double_step);
		st_console_test_number(app, "cterm.igs_text_pen", igs->attributes.text_pen);
		st_console_test_number(app, "cterm.igs_text_effects", igs->attributes.text_effects);
		st_console_test_number(app, "cterm.igs_text_rotation", igs->attributes.text_rotation);
		st_console_test_number(app, "cterm.igs_text_height", igs->attributes.text_height);
		st_console_test_number(app, "cterm.igs_text_points", igs->attributes.text_points);
		st_console_test_number(app, "cterm.igs_text_point_mode", igs->attributes.text_point_mode);
		st_console_test_number(app, "cterm.igs_text_face", igs->attributes.text_face);
		st_console_test_number(app, "cterm.igs_text_align_h", igs->attributes.text_align_h);
		st_console_test_number(app, "cterm.igs_text_align_v", igs->attributes.text_align_v);
		st_console_test_number(app, "cterm.igs_marker_pen_cache", igs->marker_pen_cache);
		st_console_test_number(app, "cterm.igs_marker_cycle", igs->marker_cycle);
		st_console_test_number(app, "cterm.igs_marker_cycle_restart", igs->marker_cycle_restart);
	}
	FIELD(ext_attr.auto_wrap); FIELD(ext_attr.origin_mode); FIELD(ext_attr.sx_scroll); FIELD(ext_attr.dec_lrmm);
	FIELD(ext_attr.bracket_paste); FIELD(ext_attr.dec_bkm); FIELD(ext_attr.alternate_keypad);
	FIELD(ext_attr.prestel_mosaic); FIELD(ext_attr.prestel_double_height); FIELD(ext_attr.prestel_conceal);
	FIELD(ext_attr.prestel_separated); FIELD(ext_attr.prestel_hold);
	FIELD(last_column_flag.set); FIELD(last_column_flag.enabled); FIELD(last_column_flag.forced);
#undef FIELD
	st_console_test_number(app, "cterm.encoding", st_cterm_encoding(st_term_session_cterm(owner->session)));
	st_console_test_number(app, "cterm.codepage", st_cterm_codepage(st_term_session_cterm(owner->session)));
	st_console_test_number(app, "cterm.status_display", st_cterm_status_display(st_term_session_cterm(owner->session)));
	struct st_transfer_inspection transfer;
	bool reserved;
	bool active = st_term_session_inspect_transfer(owner->session, &transfer, &reserved);
	st_console_test_number(app, "transfer.active", active);
	st_console_test_number(app, "transfer.reserved", reserved);
	if (active) {
#define TRANSFER(name, value) st_console_test_number(app, "transfer." name, value)
		TRANSFER("protocol", transfer.protocol); TRANSFER("upload", transfer.upload);
		TRANSFER("done", transfer.done); TRANSFER("aborted", transfer.aborted);
		TRANSFER("result", transfer.result); TRANSFER("dialog", transfer.dialog);
		TRANSFER("bytes_current", transfer.progress.bytes_current); TRANSFER("bytes_total", transfer.progress.bytes_total);
#undef TRANSFER
		st_console_test_text(app, "transfer.line1", transfer.progress.line1);
		st_console_test_text(app, "transfer.line2", transfer.progress.line2);
		st_console_test_text(app, "transfer.line3", transfer.progress.line3);
		st_console_test_text(app, "transfer.line4", transfer.progress.line4);
	}
}
#endif

static int
application_paths(struct st_app_paths *paths, const struct st_command_line *command,
	const struct st_application_host *frontend)
{
	struct st_app_path_overrides overrides = {0};
	char *files[8] = {0};
	int status = 0;
	const char *directory = frontend == NULL ? NULL : frontend->directory;
#if ST_CONSOLE_TEST
	if (command->test_directory != NULL) directory = command->test_directory;
#endif
	if (directory != NULL) {
		const char *names[] = {"syncterm.ini", "syncterm.lst", "shared.lst",
		    "cache", "downloads", "keys.ini", "scripts", "themes"};
		for (size_t i = 0; i < 8 && status == 0; ++i)
			status = path_join_alloc(&files[i], directory, names[i]);
		overrides = (struct st_app_path_overrides){
			.settings_file = files[0], .personal_list = files[1],
			.shared_list = files[2], .cache_directory = files[3],
			.download_directory = files[4], .keys_file = files[5],
			.scripts_directory = files[6], .themes_directory = files[7],
		};
	}
	if (command->settings_path != NULL) overrides.settings_file = command->settings_path;
	if (command->list_path != NULL) overrides.personal_list = command->list_path;
	if (status == 0) status = st_app_paths_init(paths, &overrides);
	for (size_t i = 0; i < 8; ++i) free(files[i]);
	return status;
}

static const char application_title[] = u8"SyncTERM 2.0\u03b1"
#ifdef ST_DEBUG_BUILD
	" Debug (" __DATE__ ")"
#endif
	;

static void
usage(FILE *stream, const char *program)
{
	static const struct {
		uint32_t feature;
		const char *mode;
		const char *description;
	} modes[] = {
		{ST_CONSOLE_BUILD_ANSI, "A or AU", "ANSI terminal output using UTF-8"},
		{ST_CONSOLE_BUILD_ANSI, "AC", "ANSI terminal output using CP437 bytes"},
		{ST_CONSOLE_BUILD_NCURSES, "C", "ncurses terminal output using Unicode"},
		{ST_CONSOLE_BUILD_NCURSES, "F", "ncurses terminal output using CP437 bytes"},
		{ST_CONSOLE_BUILD_NCURSES, "I", "ncurses terminal output using ASCII"},
		{ST_CONSOLE_BUILD_GDI, "G[W|F]", "Windows GDI graphics"},
		{ST_CONSOLE_BUILD_QUARTZ, "Q[W|F]", "macOS Quartz graphics"},
		{ST_CONSOLE_BUILD_SDL, "S[W|F]", "SDL graphics"},
		{ST_CONSOLE_BUILD_WIN32, "W", "Windows text console"},
		{ST_CONSOLE_BUILD_X11, "X[W|F]", "X11 graphics"},
		{ST_CONSOLE_BUILD_WAYLAND, "Y[W|F]", "Wayland graphics"},
	};
	uint32_t features = st_console_build_features();

	fprintf(stream,
	    "usage: %s [options] [URL]\n\n"
	    "options:\n"
	    "  -4            only resolve IPv4 addresses\n"
	    "  -6            only resolve IPv6 addresses\n"
	    "  -b path       use path as the personal BBS list\n"
	    "  -c            hide the status line\n"
	    "  -h            use SSH when URL has no scheme\n"
	    "  -iX           select output mode X (default: configured or automatic)\n"
	    "                Modes compiled into this build:\n", program);
	for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
		if ((features & modes[i].feature) != 0)
			fprintf(stream, "                  %-8s %s\n",
			    modes[i].mode, modes[i].description);
	}
	if ((features & (ST_CONSOLE_BUILD_GDI | ST_CONSOLE_BUILD_QUARTZ
	    | ST_CONSOLE_BUILD_SDL | ST_CONSOLE_BUILD_X11
	    | ST_CONSOLE_BUILD_WAYLAND)) != 0)
		fputs("                Graphics modes accept W for windowed or F for fullscreen;\n"
		    "                the suffix is optional.\n", stream);
#if ST_CONSOLE_TEST
	if ((features & ST_CONSOLE_BUILD_TEST) != 0)
		fputs("  --test-control  use the testing bitmap driver with pipe input/output\n"
		    "  --test-directory path  use private application paths with --test-control\n", stream);
#endif
	fputs(
	    "  -l#           use 14, 21, 25, 28, 30, 43, 50, or 60 rows\n"
	    "                (default: configured startup screen mode)\n"
	    "  -n path       use path as the configuration file\n"
	    "  -q            hide connection popups\n"
	    "  -r            use rlogin when URL has no scheme\n"
	    "  -s            prevent writing or browsing local files\n"
	    "  -t            use telnet when URL has no scheme\n"
	    "  -w script     additionally load a Wren script on every connection,\n"
	    "                after built-in and user auto-load scripts; load errors\n"
	    "                are reported on standard error\n"
	    "  -T            as the only argument, write terminfo source and exit\n"
	    "  -v            as the only argument, write version information and exit\n"
	    "  -?, --help    show this help and exit\n\n"
	    "Attach values to -i and -l (for example, -iac and -l43). Paths for\n"
	    "-b, -n, and -w may be attached or separate (for example, -b/path/to/list\n"
	    "or -b /path/to/list). Option letters are case-insensitive, except for\n"
	    "the standalone -T and -v actions above.\n\n"
	    "URL format: [scheme://][user[:password]@]host[:port]\n"
	    "Schemes: rlogin, telnet, telnets (TLS), ssh, sshna (no authentication),\n"
	    "         raw, ghost (MajorBBS Ghost), mqtts (Synchronet MQTT sysop spy).\n"
	    "raw:// URLs must include a port. shell:command runs a local command\n"
	    "on POSIX systems. MQTT node selection follows authentication.\n"
	    "A matching BBS list entry supplies connection settings; its name may\n"
	    "also be used instead of a URL. With no URL, open the BBS list.\n\n"
	    "Examples:\n"
	    "  syncterm bbs.example.org\n"
	    "  syncterm -l43 telnet://bbs.example.org\n"
	    "  syncterm rlogin://user:password@bbs.example.org:513\n"
	    "  syncterm raw://bbs.example.org:23\n"
	    "  syncterm mqtts://user:password@bbs.example.org\n"
	    "  syncterm shell:/bin/sh\n",
	    stream);
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

static int
run_rip_choice(void *context, const char *question,
	const struct st_cterm_rip_choice *choices, size_t count,
	bool required, int *selected)
{
	const char *labels[20];
	uint32_t hotkeys[20];
	int result;

	if (count == 0 || count > 20)
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < count; i++) {
		labels[i] = choices[i].label;
		hotkeys[i] = choices[i].hotkey;
	}
	*selected = -1;
	result = st_wren_host_rip_choice(context, "RIP Selection", question,
	    labels, hotkeys, count, required);
	if (result == IO_ERROR_CANCELLED && !required)
		return 0;
	if (result < 0)
		return result;
	*selected = result;
	return 0;
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
connection_log_append(struct connection_ui *ui, char *line, size_t length)
{
	const size_t limit = 1024U * 1024U;
	size_t drop = 0;
	/* The caller holds log_mutex. Keep whole recent lines so a busy
	 * debug session does not freeze the viewer at its first megabyte. */
	for (size_t i = 0; i < length; ++i)
		if ((unsigned char)line[i] < 32 && line[i] != '\n' && line[i] != '\t') line[i] = '?';
	if (length > limit) return;
	if (ui->log_size > limit - length) {
		drop = ui->log_size - (limit - length);
		const char *end = memchr(ui->log + drop, '\n', ui->log_size - drop);
		drop = end == NULL ? ui->log_size : (size_t)(end - ui->log) + 1;
	}
	size_t needed = ui->log_size - drop + length + 1;
	if (needed > ui->log_capacity) {
		size_t capacity = ui->log_capacity == 0 ? 4096 : ui->log_capacity;
		while (capacity < needed) capacity *= 2;
		if (capacity > limit + 1) capacity = limit + 1;
		char *next = realloc(ui->log, capacity);
		if (next == NULL) return;
		ui->log = next;
		ui->log_capacity = capacity;
	}
	if (drop != 0) {
		memmove(ui->log, ui->log + drop, ui->log_size - drop);
		ui->log_size -= drop;
	}
	memcpy(ui->log + ui->log_size, line, length);
	ui->log_size += length;
	ui->log[ui->log_size] = '\0';
}

static int
connection_log_snapshot(void *context, char **text)
{
	struct connection_ui *ui = context;
	(void)st_mutex_lock(&ui->log_mutex);
	char *copy = strdup(ui->log == NULL ? "" : ui->log);
	(void)st_mutex_unlock(&ui->log_mutex);
	if (copy == NULL) return IO_ERROR_OUT_OF_MEMORY;
	*text = copy;
	return 0;
}

static void
connection_transfer_log(void *context, int level, const char *message)
{
	struct connection_ui *ui = context;
	static const char *const names[] = {
		"Emergency", "Alert", "Critical", "Error", "Warning", "Notice", "Info", "Debug",
	};
	char line[2048];
	/* The connected VM applies the live transfer threshold. */
	int count = snprintf(line, sizeof(line), "Transfer %s: %s\n",
	    (unsigned)level < sizeof(names) / sizeof(names[0]) ? names[level] : "Info",
	    message == NULL ? "" : message);
	(void)st_mutex_lock(&ui->log_mutex);
	if (ui->log_file != NULL && !connection_log_stamp(ui->log_file, message))
		fprintf(stderr, "SyncTERM: unable to write transfer log\n");
	if (count > 0)
		connection_log_append(ui, line, (size_t)count < sizeof(line) ? (size_t)count : sizeof(line) - 1);
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
connection_log_message(struct connection_ui *ui, const char *source,
	const char *severity, const char *message)
{
	char line[2048];
	int count = snprintf(line, sizeof(line), "%s %s: %s\n", source == NULL ? "Protocol" : source,
	    severity, message == NULL ? "" : message);
	if (count < 0) return;
	size_t length = (size_t)count < sizeof(line) ? (size_t)count : sizeof(line) - 1;
	/* Never render terminal escapes or embedded controls from a remote log. */
	for (size_t i = 0; i < length; ++i)
		if ((unsigned char)line[i] < 32 && line[i] != '\n' && line[i] != '\t') line[i] = '?';
	(void)st_mutex_lock(&ui->log_mutex);
	if (ui->log_file != NULL && fwrite(line, 1, length, ui->log_file) != length)
		fprintf(stderr, "SyncTERM: unable to write session log\n");
	connection_log_append(ui, line, length);
	(void)st_mutex_unlock(&ui->log_mutex);
}

static void
connection_log(void *context, enum st_connection_log_level level,
	const char *source, const char *message)
{
	struct connection_ui *ui = context;
	const int priorities[] = {3, 4, 6, 7};
	if ((unsigned)level >= sizeof(priorities) / sizeof(priorities[0])
	    || priorities[level] > ui->profile->protocol_log_level) return;
	connection_log_message(ui, source,
	    level == ST_CONNECTION_LOG_ERROR ? "Error" : level == ST_CONNECTION_LOG_WARNING
	    ? "Warning" : level == ST_CONNECTION_LOG_INFO ? "Info" : "Debug", message);
}

static void
connection_cterm_log(void *context, enum st_cterm_log_level level,
	const char *message)
{
	struct connection_ui *ui = context;
	if ((int)level > ui->profile->cterm_log_level) return;
	connection_log_message(ui, "CTerm", level == ST_CTERM_LOG_ERROR ? "Error"
	    : level == ST_CTERM_LOG_WARNING ? "Warning"
	    : level == ST_CTERM_LOG_INFO ? "Info" : "Debug", message);
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
	static const char log_heading[] = "# Session Log\n\n";
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

static int
run_wren_skypix_transfer(void *context, unsigned mode, unsigned width, unsigned height,
	const uint8_t *name, size_t size)
{
	return st_wren_connected_skypix_transfer(*(st_wren_connected_t **)context,mode,width,height,name,size);
}

static int
run_wren_rip_transfer(void *context, const struct st_cterm_rip_transfer_request *request)
{
	return st_wren_connected_rip_transfer(*(st_wren_connected_t **)context, request);
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
run_wren_capture_wire(void *context, const uint8_t *bytes, size_t size)
{
	return st_wren_connected_capture_wire(
	    *(st_wren_connected_t **)context, bytes, size);
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
	st_session_history_t *history, enum st_term_disposition *disposition,
	const st_amiga_font_t *amiga_font)
{
	const struct st_screen_mode_descriptor *mode;
	enum st_screen_mode session_mode
	    = (enum st_screen_mode)profile->screen_mode;
	st_font_catalog_t *fonts = NULL;
	st_connection_t *connection = NULL;
	st_term_session_t *session = NULL;
	st_wren_connected_t *connected = NULL;
#if ST_CONSOLE_TEST
	struct application_inspection inspection = {0};
#endif
	char *cache_directory = NULL;
	char *amiga_font_directory = NULL;
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
	if (profile->graphics_mode == ST_GRAPHICS_SKYPIX_13 || profile->graphics_mode == ST_GRAPHICS_SKYPIX_31) {
		selector = 0;
		status = st_font_catalog_create(&fonts, console);
	}
	else status = prepare_session_fonts(&fonts, console, profile, selector, &selector);
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
	if (profile->graphics_mode == ST_GRAPHICS_SKYPIX_13 || profile->graphics_mode == ST_GRAPHICS_SKYPIX_31) {
		status = st_amiga_fonts_directory(&amiga_font_directory, paths->settings_file);
		if (status != 0) goto finished;
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
		.graphics_mode = st_term_profile_graphics_mode(profile),
		.rip_directory = cache_directory,
		.amiga_font_directory = amiga_font_directory,
		.amiga_font = amiga_font,
		.skypix_transfer = run_wren_skypix_transfer,
		.skypix_transfer_context = &connected,
		.rip_transfer = run_wren_rip_transfer,
		.rip_transfer_context = &connected,
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
		.rip_choice = run_rip_choice,
		.rip_choice_context = host,
		.event_hook = run_wren_event_hook,
		.event_hook_context = &connected,
		.wire_filter = run_wren_wire_filter,
		.wire_filter_active = wren_wire_filter_active,
		.wire_filter_context = &connected,
		.input_filter = run_wren_input_filter,
		.input_filter_active = wren_input_filter_active,
		.input_filter_context = &connected,
		.wire_capture = run_wren_capture_wire,
		.capture = run_wren_capture_input,
		.capture_active = wren_capture_active,
		.capture_context = &connected,
		.cterm_log_level = profile->cterm_log_level,
		.cterm_diagnostic = connection_cterm_log,
		.cterm_diagnostic_context = &connection_ui,
	};
	status = st_term_session_create(&session, &config);
	if (status == 0) {
#if ST_CONSOLE_TEST
		inspection.session = session; inspection.connection = connection;
		st_console_test_set_application_reader(display, inspect_application, &inspection);
#endif
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
			.session_log = connection_log_snapshot,
			.session_log_context = &connection_ui,
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
#if ST_CONSOLE_TEST
		inspection.connection = NULL;
#endif
		int close_status = st_connection_close(connection);

		connection = NULL;
		if (status == 0)
			status = close_status;
	}
	if (session != NULL) (void)st_wren_host_capture_history_mode(host);
	/* Close first: the log viewer must include final transport diagnostics.
	 * Save offline history before displaying the notice, retaining CTerm
	 * until the notice is dismissed. */
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
#if ST_CONSOLE_TEST
	st_console_test_set_application_reader(display, NULL, NULL);
#endif
	st_term_session_destroy(&session);
	connection_status(&connection_ui, NULL);
	free(connection_ui.log);
	st_mutex_destroy(&connection_ui.log_mutex);
	st_font_catalog_destroy(fonts);
	free(cache_directory);
	free(amiga_font_directory);
	return status;
}

static int run_selected(st_wren_host_t *host, st_console_display_t *display, st_console_t *console,
						struct st_profile *profile, const struct st_app_paths *paths,
						st_audio_runtime_t *audio, st_hyperlink_registry_t *hyperlinks,
						st_session_history_t *history, enum st_term_disposition *disposition)
{
	const st_amiga_font_t *font = NULL;
	int status;
	if (profile->graphics_mode == ST_GRAPHICS_SKYPIX_13 || profile->graphics_mode == ST_GRAPHICS_SKYPIX_31) {
		char *directory = NULL;
		status = st_amiga_fonts_directory(&directory, paths->settings_file);
		if (status == 0)
			status = st_amiga_fonts_lookup(&font, profile->graphics_mode == ST_GRAPHICS_SKYPIX_13 ?
					ST_AMIGA_FONT_SET_13 : ST_AMIGA_FONT_SET_2PLUS, directory, profile->amiga_font,
										   profile->amiga_font_size);
		free(directory);
		if (font == NULL)
			font = st_amiga_font_builtin(ST_AMIGA_FONT_SET_2PLUS, "pearl.font", 8);
		if (status != 0)
			fprintf(stderr, "SyncTERM: Amiga default font unavailable (%d); using fallback\n",
					status);
	}
	status = st_wren_host_begin_session_mode(host, (enum st_screen_mode)profile->screen_mode);
	int restore_status;

	if (status == 0)
		status = run_session(host, display, console, profile, paths, audio, hyperlinks, history,
							 disposition, font);
	/* Restore after disconnect notices and CTerm destruction, even when
	 * connection setup failed or the session requested application exit. */
	restore_status = st_wren_host_end_session_mode(host);
	st_amiga_font_release(font);
	if (restore_status != 0)
		*disposition = ST_TERM_EXIT_APPLICATION;
	return restore_status != 0 ? restore_status : status;
}

static int
run_application(int argc, char **argv, const struct st_application_host *frontend)
{
	struct st_command_line command;
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
	bool published = false;
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

	status = application_paths(&paths, &command, frontend);
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
		status = frontend != NULL && frontend->libretro
		    ? st_audio_runtime_create_pull(&audio_runtime)
		    : st_audio_runtime_create(&audio_runtime, st_settings.audio_output_modes);
	if (status != 0) {
		fprintf(stderr, "SyncTERM: cannot initialize runtime state (%d)\n",
		    status);
		goto finished;
	}
	st_audio_runtime_set_midi_allowed(audio_runtime, st_settings.midi_output_modes);
	status = st_console_startup_configure(&startup, &st_settings, &command);
	if (status != 0) {
		fprintf(stderr, "SyncTERM: invalid console settings (%d)\n", status);
		goto finished;
	}
	display_config = startup.display_config;
	if (frontend != NULL && frontend->libretro) {
		display_config.backend = ST_CONSOLE_BACKEND_LIBRETRO;
		startup.fullscreen = false;
	} else if (frontend != NULL && frontend->console_mode
	    && display_config.backend == ST_CONSOLE_BACKEND_AUTO)
		display_config.backend = ST_CONSOLE_BACKEND_WIN32;
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
	status = st_presentation_font_create_fallback(&presentation_font,
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
	if (status == 0)
		status = st_console_set_fallback_font(console, default_font);
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
	/* Text backends can adopt the host terminal's grid and discard the
	 * pixel surface. Copy the initialized geometry rather than mixing that
	 * grid with the configured startup pixel dimensions. */
	status = st_console_size(console, &picker_config.columns,
	    &picker_config.rows);
	if (status == 0)
		status = st_console_cell_pixel_size(console,
		    &picker_config.cell_width, &picker_config.cell_height);
	picker_config.pixel_width = 0;
	picker_config.pixel_height = 0;
	if (status == 0 && picker_config.cell_width != 0)
		status = st_console_pixel_size(console, &picker_config.pixel_width,
		    &picker_config.pixel_height);
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
#if ST_CONSOLE_TEST
	if (command.test_control) {
		status = st_console_test_start_control(display);
		if (status != 0) {
			fprintf(stderr, "SyncTERM: test control requires redirected pipe input/output (%d)\n", status);
			goto finished;
		}
	}
#endif
	/* Auto-loaded Wren scripts may open a modal during host creation. The
	 * frontend must already be able to present it and deliver input. */
	if (frontend != NULL && frontend->ready != NULL) {
		frontend->ready(frontend->context, display, audio_runtime);
		published = true;
	}
	status = st_wren_host_create(&host, &host_config);
	if (status != 0) {
		fprintf(stderr, "SyncTERM: cannot initialize the menu (%d)\n",
		    status);
		goto finished;
	}
	if (frontend != NULL && frontend->stopping != NULL
	    && frontend->stopping(frontend->context)) {
		result = 0;
		goto finished;
	}
	(void)st_wren_host_refresh_web_lists(host);
	st_profile_init(&selected);
	profile_initialized = true;
	for (;;) {
		enum st_term_disposition disposition = ST_TERM_RETURN_TO_PICKER;
		if (frontend != NULL && frontend->stopping != NULL
		    && frontend->stopping(frontend->context)) {
			result = 0;
			break;
		}

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
#if ST_CONSOLE_TEST
	st_console_test_stop_control(display);
#endif
	if (published && frontend->release != NULL)
		frontend->release(frontend->context);
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

int
st_application_run(int argc, char **argv, const struct st_application_host *frontend)
{
	bool expected = false;
	int result;
	if (!atomic_compare_exchange_strong_explicit(&application_running, &expected,
	    true, memory_order_acq_rel, memory_order_acquire))
		return 1;
	result = run_application(argc, argv, frontend);
	atomic_store_explicit(&application_running, false, memory_order_release);
	return result;
}
