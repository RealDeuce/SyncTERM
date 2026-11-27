/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "filesystem.h"
#include "st_ddofx_resource.h"
#include "st_ddofx_script.h"
#include "st_file_lock.h"

enum {
	RECORD_SIZE = 255,
	SLOT_COUNT  = 32000
};

struct info_update {
	const char *path;
	size_t      at;
	uint8_t     record[RECORD_SIZE];
};

static int
replace_record(FILE *out, void *context)
{
	const struct info_update *update = context;
	FILE                     *in     = NULL;
	int                       status = fs_open(&in, update->path, "rb");
	if (status && status != IO_ERROR_NOT_FOUND)
		return status;
	int64_t size = in ? fs_stream_size(in) : 0;
	if (size < 0 || size % RECORD_SIZE || size > (SLOT_COUNT + 1) * RECORD_SIZE) {
		if (in)
			fclose(in);
		return IO_ERROR_IO;
	}
	size_t before                   = (size_t)size < update->at ? (size_t)size : update->at;
	status                          = before ? st_ddofx_copy_bytes(out, in, before) : 0;
	static const uint8_t zero[8192] = {0};
	for (size_t gap = update->at - before; !status && gap;) {
		size_t count = gap < sizeof(zero) ? gap : sizeof(zero);
		if (fwrite(zero, 1, count, out) != count)
			status = IO_ERROR_IO;
		gap -= count;
	}
	if (!status && fwrite(update->record, 1, RECORD_SIZE, out) != RECORD_SIZE)
		status = IO_ERROR_IO;
	size_t after = update->at + RECORD_SIZE;
	if (!status && (size_t)size > after) {
		status = fs_stream_seek(in, after);
		if (!status)
			status = st_ddofx_copy_bytes(out, in, (size_t)size - after);
	}
	if (in)
		fclose(in);
	return status;
}

static const char *
read_record(const char *path, size_t at, uint8_t record[RECORD_SIZE], uint32_t password)
{
	FILE *file;
	if (!path || fs_open(&file, path, "rb"))
		return "NO FILE";
	bool read = !fs_stream_seek(file, at) && fread(record, 1, RECORD_SIZE, file) == RECORD_SIZE;
	fclose(file);
	if (!read || record[4] > RECORD_SIZE - 5)
		return "NO INFO";
	for (int i = 0; i < 4; ++i)
		if (record[i] != (uint8_t)(password >> (8 * i)))
			return "WRONG PW";
	return NULL;
}

void
st_ddofx_info(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, bool save, char *args)
{
	int32_t first, second;
	if (!st_ddofx_field_number(ddo, &args, &first) || !st_ddofx_field_number(ddo, &args, &second)
	    || (save ? args == NULL : args != NULL))
		return;
	int32_t slot = save ? first : second;
	if (slot < 1 || slot > SLOT_COUNT)
		return;
	uint32_t password = (uint32_t)(save ? second : first);
	char     name[sizeof(ddo->resource_prefix) + 1];
	snprintf(name, sizeof(name), "%sT", ddo->resource_prefix);
	char *directory =
	    sink->theme_path && st_ddofx_dos_filename(name) ? sink->theme_path(sink->context, NULL) : NULL;
	st_file_lock_t *lock = NULL;
	char           *path = NULL;
	if (directory && !st_file_lock_acquire(&lock, directory, save))
		path = sink->theme_path(sink->context, name);
	free(directory);
	struct info_update update = {.at = (size_t)slot * RECORD_SIZE, .path = path};
	const char        *error  = NULL;
	if (save && path) {
		for (int i = 0; i < 4; ++i)
			update.record[i] = (uint8_t)(password >> (8 * i));
		update.record[4] = (uint8_t)st_ddofx_info_value(ddo, args, update.record + 5);
		(void)fs_replace_file_with(path, replace_record, &update);
	}
	else if (!save)
		error = read_record(path, update.at, update.record, password);
	free(path);
	if (lock)
		st_file_lock_release(lock);
	if (!save)
		st_ddofx_info_reply(ddo, sink, update.record + 5, update.record[4], error);
}
