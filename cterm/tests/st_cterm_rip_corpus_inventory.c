/* Inventory RIP commands through the production incremental wire parser. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "cterm_rip_parser.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t commands[10][10][256];

struct find_context {
	bool enabled;
	int level;
	int sublevel;
	uint8_t code;
	uint64_t matches;
};

static int
ignore_bytes(void *context, const uint8_t *bytes, size_t size)
{
	(void)context;
	(void)bytes;
	(void)size;
	return 0;
}

static int
count_command(void *context, int level, int sublevel, uint8_t code,
    const char *arguments)
{
	struct find_context *find = context;

	(void)arguments;
	if (level >= 0 && level < 10 && sublevel >= 0 && sublevel < 10)
		commands[level][sublevel][code]++;
	if (find->enabled && level == find->level
	    && sublevel == find->sublevel && code == find->code)
		find->matches++;
	return 0;
}

static bool
at_column(void *context)
{
	(void)context;
	/* The inventory has no terminal cursor; count unambiguous BOL RIP. */
	return false;
}

int
main(int argc, char **argv)
{
	struct find_context find = {0};
	struct cterm_rip_parser_sink sink = {
		.context = &find,
		.passthrough = ignore_bytes,
		.command = count_command,
		.control = ignore_bytes,
		.at_carriage_return_column = at_column,
	};
	uint64_t bytes_total = 0;
	uint64_t commands_total = 0;
	int first_file = 1;

	if (argc >= 6 && strcmp(argv[1], "--find") == 0
	    && strlen(argv[2]) == 1 && argv[2][0] >= '0'
	    && argv[2][0] <= '9' && strlen(argv[3]) == 1
	    && argv[3][0] >= '0' && argv[3][0] <= '9'
	    && strlen(argv[4]) == 1) {
		find.enabled = true;
		find.level = argv[2][0] - '0';
		find.sublevel = argv[3][0] - '0';
		find.code = (uint8_t)argv[4][0];
		first_file = 5;
	}

	if (argc <= first_file) {
		fprintf(stderr, "usage: %s [--find level sublevel code] file.rip [file.rip ...]\n", argv[0]);
		return 2;
	}
	for (int file_index = first_file; file_index < argc; file_index++) {
		struct cterm_rip_parser parser;
		uint8_t data[4096];
		FILE *file = fopen(argv[file_index], "rb");

		if (file == NULL) {
			perror(argv[file_index]);
			return 1;
		}
		find.matches = 0;
		cterm_rip_parser_init(&parser, true);
		for (;;) {
			size_t size = fread(data, 1, sizeof(data), file);
			size_t consumed;
			int status;

			if (size == 0)
				break;
			bytes_total += size;
			status = cterm_rip_parser_feed(&parser, &sink,
			    data, size, &consumed);
			if (status != 0 || consumed != size) {
				fprintf(stderr, "%s: RIP parser status %d at byte %" PRIu64 "\n",
				    argv[file_index], status, bytes_total);
				cterm_rip_parser_destroy(&parser);
				fclose(file);
				return 1;
			}
		}
		if (ferror(file)) {
			perror(argv[file_index]);
			cterm_rip_parser_destroy(&parser);
			fclose(file);
			return 1;
		}
		int status = cterm_rip_parser_flush(&parser, &sink);

		if (status != 0) {
			fprintf(stderr, "%s: RIP parser flush status %d\n",
			    argv[file_index], status);
			cterm_rip_parser_destroy(&parser);
			fclose(file);
			return 1;
		}
		cterm_rip_parser_destroy(&parser);
		fclose(file);
		if (find.enabled && find.matches != 0)
			printf("%s: %" PRIu64 "\n", argv[file_index],
			    find.matches);
	}
	if (find.enabled)
		return 0;
	for (int level = 0; level < 10; level++) {
		for (int sublevel = 0; sublevel < 10; sublevel++) {
			for (int code = 0; code < 256; code++) {
				uint64_t count = commands[level][sublevel][code];

				if (count == 0)
					continue;
				commands_total += count;
				printf("%" PRIu64 " level=%d sublevel=%d code=0x%02x %c\n",
				    count, level, sublevel, code,
				    code >= 32 && code <= 126 ? code : '.');
			}
		}
	}
	fprintf(stderr, "%d files, %" PRIu64 " bytes, %" PRIu64
	    " commands\n", argc - first_file, bytes_total, commands_total);
	return 0;
}
