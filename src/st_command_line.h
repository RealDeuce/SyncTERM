/* SyncTERM command-line parsing. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_COMMAND_LINE_H_
#define ST_COMMAND_LINE_H_

#include "st_console.h"
#include "st_profile.h"

#include <stdbool.h>
#include <stdint.h>

enum st_command_action {
	ST_COMMAND_RUN,
	ST_COMMAND_HELP,
	ST_COMMAND_VERSION,
	ST_COMMAND_TERMINFO,
};

struct st_command_line {
	enum st_command_action action;
	const char *test_directory;
	const char *settings_path;
	const char *list_path;
	const char *wren_script;
	const char *url;
	enum st_console_backend backend;
	enum st_console_text_presentation text_presentation;
	enum st_address_family address_family;
	int connection_type;
	uint32_t rows;
	bool backend_set;
	bool test_control;
	bool fullscreen;
	bool connection_type_set;
	bool rows_set;
	bool hide_status;
	bool hide_popups;
	bool safe_mode;
};

/* argv strings remain borrowed by the result. */
int st_command_line_parse(struct st_command_line *result, int argc,
	char *const argv[], const char **invalid_argument);

#endif
