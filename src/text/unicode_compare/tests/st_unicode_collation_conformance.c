/* Check Unicode's ordered UCA conformance data. */

/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "st_unicode_compare.h"
#include "st_unicode_compare_internal.h"

#include "st_utf8.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
parse_line(char *line, char *output, size_t *output_size)
{
	char *cursor = line;
	size_t used = 0;

	while (*cursor != '\0' && *cursor != '\n' && *cursor != '#'
	    && *cursor != ';') {
		char *end;
		unsigned long value;
		ptrdiff_t encoded;

		while (*cursor == ' ' || *cursor == '\t')
			++cursor;
		if (*cursor == '\0' || *cursor == '\n' || *cursor == '#'
		    || *cursor == ';')
			break;
		errno = 0;
		value = strtoul(cursor, &end, 16);
		if (errno != 0 || end == cursor || value > UINT32_MAX)
			return -1;
		/* Strict UTF-8 has no representation for surrogate code points. */
		if (value >= 0xd800 && value <= 0xdfff)
			return 2;
		encoded = st_utf8_encode_one(output + used, 4, (uint32_t)value);
		if (encoded < 0)
			return -1;
		used += (size_t)encoded;
		cursor = end;
	}
	*output_size = used;
	return used == 0 ? 0 : 1;
}

int
main(int argc, char **argv)
{
	char line[4096];
	char current[1024];
	size_t current_size;
	st_unicode_sort_key_t *prior = NULL;
	size_t line_number = 0;
	FILE *input;

	if (argc != 2) {
		fprintf(stderr, "usage: %s CollationTest_NON_IGNORABLE.txt\n",
		    argv[0]);
		return EXIT_FAILURE;
	}
	input = fopen(argv[1], "rb");
	if (input == NULL) {
		perror(argv[1]);
		return EXIT_FAILURE;
	}
	while (fgets(line, sizeof(line), input) != NULL) {
		st_unicode_sort_key_t *key = NULL;
		int parsed;

		++line_number;
		if (strchr(line, '\n') == NULL && !feof(input)) {
			fprintf(stderr, "%s:%zu: line is too long\n", argv[1],
			    line_number);
			goto failed;
		}
		parsed = parse_line(line, current, &current_size);
		if (parsed < 0) {
			fprintf(stderr, "%s:%zu: malformed input\n", argv[1],
			    line_number);
			goto failed;
		}
		if (parsed == 0 || parsed == 2)
			continue;
		if (st_unicode_sort_key_create(&key, current, current_size,
		    ST_UNICODE_COLLATION_CASE_SENSITIVE) != 0) {
			fprintf(stderr, "%s:%zu: unable to create sort key\n", argv[1],
			    line_number);
			goto failed;
		}
		if (prior != NULL && st_unicode_sort_key_compare_uca(prior, key) > 0) {
			fprintf(stderr, "%s:%zu: collation order violation\n", argv[1],
			    line_number);
			st_unicode_sort_key_destroy(&key);
			goto failed;
		}
		st_unicode_sort_key_destroy(&prior);
		prior = key;
	}
	if (ferror(input)) {
		perror(argv[1]);
		goto failed;
	}
	st_unicode_sort_key_destroy(&prior);
	(void)fclose(input);
	return EXIT_SUCCESS;

failed:
	st_unicode_sort_key_destroy(&prior);
	(void)fclose(input);
	return EXIT_FAILURE;
}
