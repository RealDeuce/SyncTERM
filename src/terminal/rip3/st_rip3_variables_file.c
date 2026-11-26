/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filesystem.h"
#include "io_error.h"
#include "st_rip3_variables_file.h"

static const uint8_t magic[] = {'S', 'T', 'R', 'I', 'P', '3', 'V'};

enum {
	VARIABLE_FILE_WITH_FLAGS  = 2,
	VARIABLE_FILE_WIDE_LENGTH = 3,
	CONFIRMATION_REQUIRED     = 1
};

static uint32_t
read_count(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static void
write_count(uint8_t *bytes, uint32_t value)
{
	for (unsigned i = 0; i < 4; i++)
		bytes[i] = (uint8_t)(value >> (8 * i));
}

int
st_rip3_variables_save_file(const struct st_rip3_variables *variables, const char *path)
{
	struct st_rip3_variable_snapshot snapshot = {0};
	uint8_t                         *bytes    = NULL;
	size_t                           count, size = 12, offset = 12;
	bool                             wide = false;
	int                              status;

	if (path == NULL || path[0] == 0)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rip3_variables_count(variables, &count);
	if (status != 0)
		return status;
	if (count > UINT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	status = st_rip3_variables_snapshot(variables, &snapshot);
	if (status != 0)
		return status;
	for (size_t i = 0; i < snapshot.count; i++) {
		const struct st_rip3_variable_entry *entry = &snapshot.entries[i];
		size_t                               added = 3 + strlen(entry->name) + entry->value.length;

		if (entry->value.length > UINT8_MAX)
			wide = true;
		if (added > SIZE_MAX - size) {
			status = IO_ERROR_OUT_OF_RANGE;
			goto done;
		}
		size += added;
	}
	if (wide) {
		if (snapshot.count > SIZE_MAX - size) {
			status = IO_ERROR_OUT_OF_RANGE;
			goto done;
		}
		size += snapshot.count;
	}
	bytes = malloc(size);
	if (bytes == NULL) {
		status = IO_ERROR_OUT_OF_MEMORY;
		goto done;
	}
	memcpy(bytes, magic, sizeof(magic));
	bytes[7] = wide ? VARIABLE_FILE_WIDE_LENGTH : VARIABLE_FILE_WITH_FLAGS;
	write_count(bytes + 8, (uint32_t)snapshot.count);
	for (size_t i = 0; i < snapshot.count; i++) {
		const struct st_rip3_variable_entry *entry       = &snapshot.entries[i];
		size_t                               name_length = strlen(entry->name);

		bytes[offset++] = (uint8_t)name_length;
		bytes[offset++] = (uint8_t)entry->value.length;
		bytes[offset++] = entry->require_confirmation ? CONFIRMATION_REQUIRED : 0;
		if (wide)
			bytes[offset++] = (uint8_t)(entry->value.length >> 8);
		memcpy(bytes + offset, entry->name, name_length);
		offset += name_length;
		memcpy(bytes + offset, entry->value.bytes, entry->value.length);
		offset += entry->value.length;
	}
	status = fs_replace_file(path, bytes, size);
done:
	free(bytes);
	st_rip3_variables_snapshot_free(&snapshot);
	return status;
}

static int
read_exact(FILE *stream, void *bytes, size_t size)
{
	if (size == 0 || fread(bytes, 1, size, stream) == size)
		return 0;
	return ferror(stream) ? IO_ERROR_IO : IO_ERROR_INVALID_FORMAT;
}

static int
load_stream(struct st_rip3_variables **result, FILE *stream)
{
	uint8_t                   header[12];
	struct st_rip3_variables *variables = NULL;
	int64_t                   file_size = fs_stream_size(stream);
	uint32_t                  count;
	int                       status;

	if (file_size < 0)
		return (int)file_size;
	if (file_size < 12)
		return IO_ERROR_INVALID_FORMAT;
	status = read_exact(stream, header, sizeof(header));
	if (status != 0)
		return status;
	if (memcmp(header, magic, sizeof(magic)) != 0)
		return IO_ERROR_INVALID_FORMAT;
	if (header[7] < 1 || header[7] > VARIABLE_FILE_WIDE_LENGTH)
		return IO_ERROR_NOT_SUPPORTED;
	size_t record_header = (size_t)header[7] + 1;
	count                = read_count(header + 8);
	/* Each record contains a header and at least one name byte. */
	if (count > (uint64_t)(file_size - 12) / (record_header + 1))
		return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_variables_create(&variables);
	if (status != 0)
		return status;
	for (uint32_t i = 0; i < count; i++) {
		uint8_t                       fields[4] = {0};
		struct st_rip3_variable_value value;
		char                          name[21];
		size_t                        stored;

		status = read_exact(stream, fields, record_header);
		if (status != 0)
			goto failed;
		value.length = (size_t)fields[1] | (size_t)fields[3] << 8;
		if (fields[0] == 0 || fields[0] > 20 || fields[2] > CONFIRMATION_REQUIRED
		    || value.length >= sizeof(value.bytes)) {
			status = IO_ERROR_INVALID_FORMAT;
			goto failed;
		}
		status = read_exact(stream, name, fields[0]);
		if (status == 0)
			status = read_exact(stream, value.bytes, value.length);
		if (status != 0)
			goto failed;
		name[fields[0]] = 0;
		status = st_rip3_variables_define(variables, NULL, name, fields[0], value.bytes, value.length,
		    fields[2] == CONFIRMATION_REQUIRED);
		if (status != 0)
			goto failed;
		(void)st_rip3_variables_count(variables, &stored);
		if (stored != (size_t)i + 1) {
			status = IO_ERROR_INVALID_FORMAT; /* Folded duplicate name. */
			goto failed;
		}
	}
	if (fgetc(stream) != EOF)
		status = IO_ERROR_INVALID_FORMAT;
	else if (ferror(stream))
		status = IO_ERROR_IO;
	if (status != 0)
		goto failed;
	*result = variables;
	return 0;
failed:
	st_rip3_variables_destroy(variables);
	return status;
}

int
st_rip3_variables_load_file(struct st_rip3_variables **result, const char *path)
{
	struct st_rip3_variables *variables = NULL;
	struct fs_info            info;
	FILE                     *stream = NULL;
	int                       status;

	if (result == NULL || path == NULL || path[0] == 0)
		return IO_ERROR_INVALID_ARGUMENT;
	status = fs_get_info(&info, path);
	if (status == 0 && info.kind != FS_ENTRY_FILE)
		status = IO_ERROR_INVALID_FORMAT;
	if (status == 0)
		status = fs_open(&stream, path, "rb");
	if (status == 0)
		status = load_stream(&variables, stream);
	if (stream != NULL && fclose(stream) != 0 && status == 0)
		status = IO_ERROR_IO;
	if (status == 0)
		*result = variables;
	else
		st_rip3_variables_destroy(variables);
	return status;
}
