/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_variables_file.h"

#include "filesystem.h"
#include "io_error.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t magic[] = {'S','T','R','I','P','3','V',1};

static uint32_t
read_count(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8
	    | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static void
write_count(uint8_t *bytes, uint32_t value)
{
	for (unsigned i = 0; i < 4; i++)
		bytes[i] = (uint8_t)(value >> (8 * i));
}

int
st_rip3_variables_save_file(const struct st_rip3_variables *variables,
    const char *path)
{
	struct st_rip3_variable_snapshot snapshot = {0};
	uint8_t *bytes = NULL;
	size_t count, size = 12, offset = 12;
	int status;

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
		size_t added = 2 + strlen(entry->name) + entry->value.length;

		if (added > SIZE_MAX - size) {
			status = IO_ERROR_OUT_OF_RANGE;
			goto done;
		}
		size += added;
	}
	bytes = malloc(size);
	if (bytes == NULL) {
		status = IO_ERROR_OUT_OF_MEMORY;
		goto done;
	}
	memcpy(bytes, magic, sizeof(magic));
	write_count(bytes + 8, (uint32_t)snapshot.count);
	for (size_t i = 0; i < snapshot.count; i++) {
		const struct st_rip3_variable_entry *entry = &snapshot.entries[i];
		size_t name_length = strlen(entry->name);

		bytes[offset++] = (uint8_t)name_length;
		bytes[offset++] = (uint8_t)entry->value.length;
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
	uint8_t header[12];
	struct st_rip3_variables *variables = NULL;
	int64_t file_size = fs_stream_size(stream);
	uint32_t count;
	int status;

	if (file_size < 0)
		return (int)file_size;
	if (file_size < 12)
		return IO_ERROR_INVALID_FORMAT;
	status = read_exact(stream, header, sizeof(header));
	if (status != 0)
		return status;
	if (memcmp(header, magic, sizeof(magic) - 1) != 0)
		return IO_ERROR_INVALID_FORMAT;
	if (header[7] != magic[7])
		return IO_ERROR_NOT_SUPPORTED;
	count = read_count(header + 8);
	/* Even an empty value needs two length bytes and a one-byte name. This
	 * bounds declared records by the actual file before allocating any table. */
	if (count > (uint64_t)(file_size - 12) / 3)
		return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_variables_create(&variables);
	if (status != 0)
		return status;
	for (uint32_t i = 0; i < count; i++) {
		uint8_t lengths[2], value[255];
		char name[21];
		size_t stored;

		status = read_exact(stream, lengths, sizeof(lengths));
		if (status != 0)
			goto failed;
		if (lengths[0] == 0 || lengths[0] > 20) {
			status = IO_ERROR_INVALID_FORMAT;
			goto failed;
		}
		status = read_exact(stream, name, lengths[0]);
		if (status == 0)
			status = read_exact(stream, value, lengths[1]);
		if (status != 0)
			goto failed;
		name[lengths[0]] = 0;
		status = st_rip3_variables_set(variables, name, lengths[0], value, lengths[1]);
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
	struct fs_info info;
	FILE *stream = NULL;
	int status;

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
