/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "filesystem.h"
#include "st_ddofx_resource.h"
#include "st_ddofx_script.h"
#include "st_file_lock.h"

void
st_ddofx_write_text_file(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *args)
{
	const char *name = st_ddofx_value(ddo, st_ddofx_field(&args));
	int32_t     append;
	if (!st_ddofx_field_number(ddo, &args, &append) || (unsigned)append > 1 || !args
	    || !st_ddofx_dos_filename(name) || !sink->theme_path)
		return;
	const char *text = st_ddofx_value(ddo, args);
	uint8_t     line[ST_DDOFX_BODY_MAX + 1];
	size_t      length = strlen(text);
	memcpy(line, text, length);
	line[length++] = '\r';

	char           *directory = sink->theme_path(sink->context, NULL);
	st_file_lock_t *lock      = NULL;
	if (directory && !st_file_lock_acquire(&lock, directory, true)) {
		char *path = sink->theme_path(sink->context, name);
		if (path) {
			FILE *file;
			if (!append)
				(void)fs_replace_file(path, line, length);
			else if (!fs_open(&file, path, "ab")) {
				int64_t original = fs_stream_size(file);
				if (original >= 0 && (fwrite(line, 1, length, file) != length || fflush(file)))
					(void)fs_stream_truncate(file, (uint64_t)original);
				fclose(file);
			}
			free(path);
		}
		st_file_lock_release(lock);
	}
	free(directory);
}

static void
finish(struct st_ddofx *ddo, const struct st_ddofx_sink *sink)
{
	if (ddo->retrieval.file) {
		fclose(ddo->retrieval.file);
		ddo->retrieval.file = NULL;
	}
	if (ddo->retrieval.reply_length)
		sink->reply(sink->context, ddo->retrieval.reply, ddo->retrieval.reply_length);
}

void
st_ddofx_retrieve_text_file(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *args)
{
	const char *name = st_ddofx_value(ddo, st_ddofx_field(&args));
	if (!args)
		return;
	ddo->retrieval.reply_length = st_ddofx_control_value(ddo, args, ddo->retrieval.reply, ST_DDOFX_BODY_MAX);
	if (st_ddofx_dos_filename(name) && sink->open_asset)
		ddo->retrieval.file = sink->open_asset(sink->context, "BBSTHEME", NULL, name);
	int64_t size = ddo->retrieval.file ? fs_stream_size(ddo->retrieval.file) : 0;
	if (size > 0)
		ddo->retrieval.remaining = (uint64_t)size;
	else
		finish(ddo, sink);
}

void
st_ddofx_retrieve_step(struct st_ddofx *ddo, const struct st_ddofx_sink *sink)
{
	uint8_t bytes[4096];
	size_t  want = ddo->retrieval.remaining < sizeof(bytes) ? (size_t)ddo->retrieval.remaining : sizeof(bytes);
	size_t  got  = fread(bytes, 1, want, ddo->retrieval.file);
	if (got)
		sink->reply(sink->context, bytes, got);
	ddo->retrieval.remaining -= got;
	if (got < want || !ddo->retrieval.remaining)
		finish(ddo, sink);
}
