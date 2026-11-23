/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_file_query.h"
#include "st_rip3_resources.h"
#include "filesystem.h"
#include "io_error.h"
#include "value_convert_time.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static bool
name_byte(uint8_t byte)
{
	return byte >= 0x20 && byte <= 0x7f && strchr("\"*+,/;<=>?[]|", byte) == NULL;
}

static bool
rewrite_alias(char *name)
{
	static const char old[][5] = {".ICN", ".HIC", ".MSK"};
	static const char replacement[][5] = {".BMP", ".BMH", ".BMM"};

	for (size_t i = 0; i < sizeof(old)/sizeof(*old); i++) {
		char *marker = strstr(name, old[i]);
		if (marker != NULL) {
			memcpy(marker, replacement[i], sizeof(replacement[i]));
			return true;
		}
	}
	return false;
}

static uint32_t
read_le32(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8
	    | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static int32_t
reported_size(FILE *stream, bool alias)
{
	int64_t length = fs_stream_size(stream);
	if (alias) {
		uint8_t header[54];
		if (fread(header, 1, sizeof(header), stream) == sizeof(header)
		    && header[0] == 'B' && header[1] == 'M') {
			uint32_t layout = read_le32(header+14);
			uint32_t width = read_le32(header+18);
			uint32_t height = read_le32(header+22);
			if ((layout == 40 || layout == 108 || layout == 124)
			    && width > 0 && width <= INT16_MAX && height > 0 && height <= INT16_MAX)
				return (int32_t)(6 + 4*((width+7)/8)*height);
		}
	}
	/* The original length request uses a signed 32-bit file position. */
	return length < 0 || length > INT32_MAX ? -1 : (int32_t)length;
}

static int
format_reply(char *reply, size_t capacity, FILE *stream, unsigned mode, const char *name, bool alias)
{
	char date[sizeof("00/00/00.00:00:00")];
	const char *timestamp = "00/00/00.00:00:00";
	int32_t size = 0;

	if (stream != NULL && mode >= 2) {
		struct fs_info info;
		size = reported_size(stream, alias);
		if (fs_get_stream_info(&info, stream) != 0)
			size = 0;
		else if (mode >= 3) {
			time_t mtime = (time_t)info.modified_time;
			struct tm local;
			if ((int64_t)mtime == info.modified_time && value_convert_localtime(mtime, &local)
			    && strftime(date, sizeof(date), "%m/%d/%y.%H:%M:%S", &local) == sizeof(date)-1)
				timestamp = date;
			else
				size = 0;
		}
	}
	if (mode == 4)
		return snprintf(reply, capacity, "%c.%s:%" PRId32 ".%s\r", stream != NULL ? '1' : '0', name, size, timestamp);
	if (stream == NULL)
		return snprintf(reply, capacity, "%s", mode == 0 ? "0" : "0\r");
	if (mode == 3)
		return snprintf(reply, capacity, "1.%" PRId32 ".%s\r", size, timestamp);
	if (mode == 2)
		return snprintf(reply, capacity, "1.%" PRId32 "\r", size);
	return snprintf(reply, capacity, "%s", mode == 0 ? "1" : "1\r");
}

int
st_rip3_file_query(const struct st_rip3_resource_paths *paths, unsigned mode,
    const uint8_t *filename, size_t length, char **reply, size_t *reply_length)
{
	struct st_rip3_resource resource = {0};
	size_t start = length;
	bool matched = false;
	*reply = NULL;
	*reply_length = 0;
	if (mode > 4 || length == 0) return IO_ERROR_INVALID_FORMAT;
	while (start != 0 && name_byte(filename[start-1])) start--;
	if (start == length) return 0;
	length -= start;
	if (length > SIZE_MAX/2-1 || length > SIZE_MAX-64) return IO_ERROR_OUT_OF_RANGE;
	char *name = malloc(2*(length+1));
	if (name == NULL) return IO_ERROR_OUT_OF_MEMORY;
	for (size_t i = 0; i < length; i++) {
		uint8_t byte = filename[start+i];
		name[i] = byte >= 'a' && byte <= 'z' ? (char)(byte-'a'+'A') : (char)byte;
	}
	name[length] = '\0';
	const char *base = strrchr(name, '\\');
	if (base != NULL) base++;
	else base = length >= 2 && name[1] == ':' ? name+2 : name;
	char *lookup = name+length+1;
	strcpy(lookup, base);
	bool alias = rewrite_alias(lookup);
	int status = st_rip3_resource_name_valid(lookup)
	    ? st_rip3_resource_open_first(&resource, &matched, paths, lookup) : IO_ERROR_NOT_FOUND;
	if (status != 0 && (status != IO_ERROR_NOT_FOUND || matched)) {
		free(name);
		return matched ? 0 : status;
	}
	size_t capacity = length+64;
	char *output = malloc(capacity);
	if (output == NULL) status = IO_ERROR_OUT_OF_MEMORY;
	else {
		int size = format_reply(output, capacity, resource.stream, mode, name, alias);
		if (size < 0 || (size_t)size >= capacity) {
			free(output);
			status = IO_ERROR_OUT_OF_RANGE;
		}
		else {
			*reply = output;
			*reply_length = (size_t)size;
			status = 0;
		}
	}
	st_rip3_resource_dispose(&resource);
	free(name);
	return status;
}
