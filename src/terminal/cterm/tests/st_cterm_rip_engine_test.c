/* RIP parser-to-host handoff, including deferred transfer recognition. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "cterm_rip_engine.h"
#include "io_error.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		return 1; \
	} \
} while (0)

struct capture {
	uint8_t text[32];
	size_t text_size;
	uint8_t control[32];
	size_t control_size;
	int transfer_commands;
	int ignored_commands;
};

static int
passthrough(void *context, const uint8_t *bytes, size_t size)
{
	struct capture *capture = context;

	if (size > sizeof(capture->text) - capture->text_size)
		return 1;
	memcpy(capture->text + capture->text_size, bytes, size);
	capture->text_size += size;
	return 0;
}

static int
unknown_command(void *context, int level, int sublevel, uint8_t code,
    const char *arguments)
{
	struct capture *capture = context;

	if (level == 0 && sublevel == 0 && code == 'n'
	    && strcmp(arguments, "2000") == 0) {
		capture->ignored_commands++;
		return IO_ERROR_NOT_SUPPORTED;
	}
	if (level != 9 || sublevel != 0 || code != 0x1b
	    || strcmp(arguments, "0100FILE<>") != 0)
		return 1;
	capture->transfer_commands++;
	return 0;
}

static int
control(void *context, const uint8_t *bytes, size_t size)
{
	struct capture *capture = context;

	if (size > sizeof(capture->control))
		return 1;
	memcpy(capture->control, bytes, size);
	capture->control_size = size;
	return 0;
}

static bool
at_column(void *context)
{
	(void)context;
	return true;
}

int
st_cterm_rip_engine_test_main(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 80, .rows = 43,
		.cell_width = 8, .cell_height = 8,
		.pixel_width = 640, .pixel_height = 350,
	};
	const uint8_t stream[] =
	    "!|n2000|c0F\r\n!|9\x1b" "0100FILE<>\r\n\x1b[0!Z";
	struct capture capture = {0};
	const struct cterm_rip_parser_sink host = {
		.context = &capture,
		.passthrough = passthrough,
		.command = unknown_command,
		.control = control,
		.at_carriage_return_column = at_column,
	};
	struct cterm_rip_engine engine;
	st_console_t *console = NULL;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(cterm_rip_engine_init(&engine, console, &host) == 0);
	CHECK(!engine.hotkeys_enabled && !engine.tab_enabled);
	for (size_t i = 0; i < sizeof(stream) - 1; i++) {
		size_t consumed = 0;

		CHECK(cterm_rip_engine_feed(&engine, stream + i, 1,
		    &consumed) == 0);
		CHECK(consumed == 1);
	}
	CHECK(engine.commands_seen == 3);
	CHECK(engine.raster.colour == 15);
	CHECK(capture.ignored_commands == 1);
	CHECK(capture.transfer_commands == 1);
	CHECK(capture.control_size == sizeof("\x1b[0!") - 1);
	CHECK(memcmp(capture.control, "\x1b[0!", capture.control_size) == 0);
	CHECK(capture.text_size == 1 && capture.text[0] == 'Z');
	{
		const uint8_t unexpected[] = "!|qX\r";
		size_t consumed = 0;

		CHECK(cterm_rip_engine_feed(&engine, unexpected,
		    sizeof(unexpected) - 1, &consumed) == 1);
	}
	cterm_rip_engine_destroy(&engine);
	st_console_destroy(console);
	return 0;
}
