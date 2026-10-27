/* SyncTERM command-line parsing. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_command_line.h"

#include "io_error.h"

#include <stddef.h>
#include <string.h>

static unsigned char
ascii_upper(unsigned char value)
{
	return value >= 'a' && value <= 'z' ? (unsigned char)(value - 'a' + 'A')
	    : value;
}

static bool
option_prefix(char value)
{
	if (value == '-')
		return true;
#ifdef _WIN32
	return value == '/';
#else
	return false;
#endif
}

static bool
parse_backend(struct st_command_line *result, const char *value)
{
	unsigned char backend;
	unsigned char presentation;

	if (value[0] == '\0')
		return false;
	backend = ascii_upper((unsigned char)value[0]);
	presentation = ascii_upper((unsigned char)value[1]);
	if (value[presentation == '\0' ? 1 : 2] != '\0')
		return false;
	result->fullscreen = false;
	switch (backend) {
	case 'A':
		if (presentation != '\0' && presentation != 'U'
		    && presentation != 'C')
			return false;
		result->backend = ST_CONSOLE_BACKEND_ANSI;
		result->text_presentation = presentation == 'C'
		    ? ST_CONSOLE_TEXT_CP437 : ST_CONSOLE_TEXT_UNICODE;
		break;
	case 'C':
		if (presentation != '\0')
			return false;
		result->backend = ST_CONSOLE_BACKEND_NCURSES;
		result->text_presentation = ST_CONSOLE_TEXT_UNICODE;
		break;
	case 'F':
		if (presentation != '\0')
			return false;
		result->backend = ST_CONSOLE_BACKEND_NCURSES;
		result->text_presentation = ST_CONSOLE_TEXT_CP437;
		break;
	case 'G':
		if (presentation != '\0' && presentation != 'W'
		    && presentation != 'F')
			return false;
		result->backend = ST_CONSOLE_BACKEND_GDI;
		result->fullscreen = presentation == 'F';
		break;
	case 'I':
		if (presentation != '\0')
			return false;
		result->backend = ST_CONSOLE_BACKEND_NCURSES;
		result->text_presentation = ST_CONSOLE_TEXT_ASCII;
		break;
	case 'Q': case 'S': case 'X': case 'Y':
		if (presentation != '\0' && presentation != 'W'
		    && presentation != 'F')
			return false;
		result->backend = backend == 'Q' ? ST_CONSOLE_BACKEND_QUARTZ
		    : backend == 'S' ? ST_CONSOLE_BACKEND_SDL
		    : backend == 'X' ? ST_CONSOLE_BACKEND_X11
		    : ST_CONSOLE_BACKEND_WAYLAND;
		result->fullscreen = presentation == 'F';
		break;
	case 'W':
		if (presentation != '\0')
			return false;
		result->backend = ST_CONSOLE_BACKEND_WIN32;
		break;
	default: return false;
	}
	result->backend_set = true;
	return true;
}

static bool
parse_rows(uint32_t *result, const char *value)
{
	static const uint32_t supported[] = {14, 21, 25, 28, 30, 43, 50, 60};
	uint32_t parsed = 0;

	if (*value == '\0')
		return false;
	for (; *value != '\0'; ++value) {
		if (*value < '0' || *value > '9' || parsed > 1000)
			return false;
		parsed = parsed * 10 + (uint32_t)(*value - '0');
	}
	for (size_t i = 0; i < sizeof(supported) / sizeof(supported[0]); ++i) {
		if (parsed == supported[i]) {
			*result = parsed;
			return true;
		}
	}
	return false;
}

static const char *
option_value(const char *argument, int *index, int argc, char *const argv[])
{
	if (argument[2] != '\0')
		return argument + 2;
	if (*index + 1 >= argc)
		return NULL;
	return argv[++*index];
}

int
st_command_line_parse(struct st_command_line *result, int argc,
	char *const argv[], const char **invalid_argument)
{
	struct st_command_line parsed = {
		.backend = ST_CONSOLE_BACKEND_AUTO,
		.address_family = ST_ADDRESS_FAMILY_UNSPECIFIED,
	};
	const char *bad_argument = NULL;

	if (result == NULL || argc < 0 || (argc != 0 && argv == NULL))
		return IO_ERROR_INVALID_ARGUMENT;
	if (invalid_argument != NULL)
		*invalid_argument = NULL;
	if (argc == 2 && strcmp(argv[1], "-T") == 0) {
		parsed.action = ST_COMMAND_TERMINFO;
		*result = parsed;
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "-v") == 0) {
		parsed.action = ST_COMMAND_VERSION;
		*result = parsed;
		return 0;
	}
	for (int i = 1; i < argc; ++i) {
		const char *argument = argv[i];
		const char *value;
		unsigned char option;

		bad_argument = argument;

		if (strcmp(argument, "--test-directory") == 0) {
			if (++i >= argc || argv[i][0] == '\0') goto invalid;
			parsed.test_directory = argv[i];
			continue;
		}
		if (strcmp(argument, "--test-control") == 0) {
			if ((st_console_build_features() & ST_CONSOLE_BUILD_TEST) == 0
			    || parsed.backend_set) goto invalid;
			parsed.test_control = true;
			parsed.backend = ST_CONSOLE_BACKEND_TEST;
			parsed.backend_set = true;
			continue;
		}
		if (strcmp(argument, "--help") == 0 || strcmp(argument, "-?") == 0) {
			parsed.action = ST_COMMAND_HELP;
			continue;
		}
		if (!option_prefix(argument[0]) || argument[1] == '\0') {
			parsed.url = argument;
			continue;
		}
		option = ascii_upper((unsigned char)argument[1]);
		switch (option) {
		case '4':
			if (argument[2] != '\0')
				goto invalid;
			parsed.address_family = ST_ADDRESS_FAMILY_IPV4;
			break;
		case '6':
			if (argument[2] != '\0')
				goto invalid;
			parsed.address_family = ST_ADDRESS_FAMILY_IPV6;
			break;
		case 'B':
			value = option_value(argument, &i, argc, argv);
			if (value == NULL || value[0] == '\0')
				goto invalid;
			parsed.list_path = value;
			break;
		case 'C':
			if (argument[2] != '\0')
				goto invalid;
			parsed.hide_status = true;
			break;
		case 'H':
			if (argument[2] != '\0')
				goto invalid;
			parsed.connection_type = ST_CONNECTION_SSH;
			parsed.connection_type_set = true;
			break;
		case 'I':
			if (parsed.test_control) goto invalid;
			if (!parse_backend(&parsed, argument + 2))
				goto invalid;
			break;
		case 'L':
			if (!parse_rows(&parsed.rows, argument + 2))
				goto invalid;
			parsed.rows_set = true;
			break;
		case 'N':
			value = option_value(argument, &i, argc, argv);
			if (value == NULL || value[0] == '\0')
				goto invalid;
			parsed.settings_path = value;
			break;
		case 'Q':
			if (argument[2] != '\0')
				goto invalid;
			parsed.hide_popups = true;
			break;
		case 'R':
			if (argument[2] != '\0')
				goto invalid;
			parsed.connection_type = ST_CONNECTION_RLOGIN;
			parsed.connection_type_set = true;
			break;
		case 'S':
			if (argument[2] != '\0')
				goto invalid;
			parsed.safe_mode = true;
			break;
		case 'T':
			if (argument[2] != '\0')
				goto invalid;
			parsed.connection_type = ST_CONNECTION_TELNET;
			parsed.connection_type_set = true;
			break;
		case 'W':
			value = option_value(argument, &i, argc, argv);
			if (value == NULL || value[0] == '\0')
				goto invalid;
			parsed.wren_script = value;
			break;
		default:
			goto invalid;
		}
	}
	if (parsed.test_directory != NULL && !parsed.test_control) goto invalid;
	*result = parsed;
	return 0;

invalid:
	if (invalid_argument != NULL)
		*invalid_argument = bad_argument;
	return IO_ERROR_INVALID_ARGUMENT;
}
