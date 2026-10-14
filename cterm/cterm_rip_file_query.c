/* RIP !|1F response generation. */

/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "cterm_rip_file_query.h"

#include "cterm_rip_asset.h"
#include "cterm_rip_time.h"
#include "io_error.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
mega_digit(char byte)
{
	if (byte >= '0' && byte <= '9') return byte - '0';
	if (byte >= 'A' && byte <= 'Z') return byte - 'A' + 10;
	if (byte >= 'a' && byte <= 'z') return byte - 'a' + 10;
	if (byte == '-') return 0;
	return -1;
}

int
cterm_rip_file_query(char **response, size_t *response_size,
    const char *directory, const char *arguments)
{
	const char *name;
	char *output;
	char date[32];
	struct tm local;
	uint64_t size;
	int64_t modified_time;
	int mode = 0, status, written;
	bool readable;
	size_t capacity, name_size;
	time_t timestamp;

	if (response == NULL || response_size == NULL || arguments == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*response = NULL;
	*response_size = 0;
	/* The legacy command descriptor is 2#4#++, despite the abbreviated
	 * field widths in the command's prose comment. */
	if (directory == NULL || strlen(arguments) < 7)
		return 0;
	for (size_t i = 0; i < 6; i++) {
		int digit = mega_digit(arguments[i]);

		if (digit < 0)
			return 0;
		if (i < 2)
			mode = mode * 36 + digit;
	}
	if (mode > 4)
		return 0;
	name = arguments + 6;
	/* Keep malformed or unsafe names silent, as in the legacy parser. */
	status = cterm_rip_asset_get_info(&size, &modified_time, &readable,
	    directory, name);
	if (status == IO_ERROR_INVALID_ARGUMENT)
		return 0;
	if (status != 0 && status != IO_ERROR_NOT_FOUND)
		return status;
	if (status == 0 && mode <= 1 && !readable)
		status = IO_ERROR_NOT_FOUND;
	name_size = strlen(name);
	if (name_size > SIZE_MAX - 128)
		return IO_ERROR_OUT_OF_RANGE;
	capacity = name_size + 128;
	output = malloc(capacity);
	if (output == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (status == IO_ERROR_NOT_FOUND)
		written = snprintf(output, capacity, mode == 0 ? "0" : "0\r\n");
	else if (mode == 0)
		written = snprintf(output, capacity, "1");
	else if (mode == 1)
		written = snprintf(output, capacity, "1\r\n");
	else if (mode == 2)
		written = snprintf(output, capacity, "1.%" PRIu64 "\r\n", size);
	else {
		timestamp = (time_t)modified_time;
		if ((int64_t)timestamp != modified_time
		    || !cterm_rip_localtime(timestamp, &local)
		    || strftime(date, sizeof(date),
		        "%m/%d/%y.%H:%M:%S", &local) == 0) {
			free(output);
			return IO_ERROR_OUT_OF_RANGE;
		}
		if (mode == 3)
			written = snprintf(output, capacity,
			    "1.%" PRIu64 ".%s\r\n", size, date);
		else
			written = snprintf(output, capacity,
			    "1.%s.%" PRIu64 ".%s\r\n", name, size, date);
	}
	if (written < 0 || (size_t)written >= capacity) {
		free(output);
		return IO_ERROR_OUT_OF_RANGE;
	}
	*response = output;
	*response_size = (size_t)written;
	return 0;
}
