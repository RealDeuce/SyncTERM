/* SyncTERM desktop application entry point. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_app_paths.h"
#include "st_command_line.h"
#include "st_console.h"
#include "st_console_startup.h"
#include "io_error.h"
#include "st_unicode_fonts.h"
#ifdef ST_HAVE_UNIFONT
#include "st_unifont.h"
#endif
#include "st_profile.h"
#include "st_settings.h"
#include "st_terminfo.h"
#include "st_wren_host.h"

#include <locale.h>
#include <stdbool.h>
#include <stdio.h>

static const char application_title[] = "SyncTERM 1.10a"
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
	    "URL: [(rlogin|telnet|ssh|raw|mqtts)://][user[:password]@]host[:port]\n",
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

int
main(int argc, char **argv)
{
	struct st_command_line command;
	struct st_app_path_overrides overrides = {0};
	struct st_app_paths paths = {0};
	struct st_console_startup startup;
	struct st_wren_host_config host_config;
	struct st_profile selected;
	st_console_t *console = NULL;
	uint16_t default_font;
	const struct st_console_font *application_font;
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
	status = st_console_startup_configure(&startup, &st_settings, &command);
	if (status != 0) {
		fprintf(stderr, "SyncTERM: invalid console settings (%d)\n", status);
		goto finished;
	}
	status = st_console_create(&console, &startup.config);
	if (status != 0) {
		fprintf(stderr, "SyncTERM: cannot initialize the console (%d)\n",
		    status);
		goto finished;
	}
	application_font = startup.config.cell_height == 8 ? &st_unscii_8_font
#ifdef ST_HAVE_UNIFONT
	    : &st_unifont_font;
#else
	    : &st_unscii_16_font;
#endif
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
	(void)st_console_set_title(console, application_title);
	if (startup.fullscreen) {
		status = st_console_set_fullscreen(console, true);
		if (status != 0) {
			fprintf(stderr, "SyncTERM: cannot enter fullscreen mode (%d)\n",
			    status);
			goto finished;
		}
	}
	host_config = (struct st_wren_host_config){
		.console = console,
		.paths = &paths,
		.application_title = application_title,
		.launch_script = command.wren_script,
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
		    detail == NULL ? "" : ": ", detail == NULL ? "" : detail);
	}
	else if (has_selection) {
		apply_profile_overrides(&selected, &command);
		result = 0;
	}
	else if (st_wren_host_exit_requested(host))
		result = 0;

finished:
	if (profile_initialized)
		st_profile_dispose(&selected);
	st_wren_host_destroy(host);
	st_console_destroy(console);
	if (settings_loaded)
		st_settings_shutdown();
	st_app_paths_dispose(&paths);
	return result;
}
