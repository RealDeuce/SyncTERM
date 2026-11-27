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

enum { EDIT_LIMIT = 65532 };

struct message {
	char  *bytes;
	size_t length;
};

static int
read_message(const char *path, struct message *message)
{
	FILE *file;
	int   status = fs_open(&file, path, "rb");
	if (status)
		return status;
	int64_t size = fs_stream_size(file);
	if (size < 0)
		status = IO_ERROR_IO;
	else if (size > EDIT_LIMIT)
		status = IO_ERROR_OUT_OF_RANGE;
	else if ((message->bytes = malloc((size_t)size + 1)) == NULL)
		status = IO_ERROR_OUT_OF_MEMORY;
	else if (fread(message->bytes, 1, (size_t)size, file) != (size_t)size)
		status = IO_ERROR_IO;
	else {
		message->length      = (size_t)size;
		message->bytes[size] = 0;
	}
	fclose(file);
	return status;
}

static bool
same_message(const struct message *a, const struct message *b)
{
	return a->length == b->length && (!a->length || !memcmp(a->bytes, b->bytes, a->length));
}

static int
editor_file(const struct st_ddofx_sink *sink, const char *directory, const char *name, struct message *original,
    const struct message *edited)
{
	st_file_lock_t *lock   = NULL;
	int             status = st_file_lock_acquire(&lock, directory, true);
	if (status)
		return status;
	char *path = sink->theme_path(sink->context, name);
	if (!path)
		status = IO_ERROR_NOT_FOUND;
	else if (!edited) {
		status = read_message(path, original);
		if (status == IO_ERROR_NOT_FOUND)
			status = fs_replace_file(path, "", 0);
	}
	else {
		/* A second session can edit the same file while the dialog is open. */
		struct message current = {0};
		status                 = read_message(path, &current);
		if (!status && !same_message(&current, original))
			status = IO_ERROR_CONFLICT;
		if (!status)
			status = fs_replace_file(path, edited->bytes, edited->length);
		free(current.bytes);
	}
	free(path);
	st_file_lock_release(lock);
	return status;
}

void
st_ddofx_edit_text_file(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *args)
{
	const char *name = st_ddofx_value(ddo, st_ddofx_field(&args));
	if (!args)
		return;
	uint8_t reply[ST_DDOFX_BODY_MAX];
	size_t  reply_length = st_ddofx_control_value(ddo, args, reply, sizeof(reply));
	if (sink->theme_path && sink->edit_text && st_ddofx_dos_filename(name)) {
		char          *directory = sink->theme_path(sink->context, NULL);
		struct message original = {0}, edited = {0};
		int status = directory ? editor_file(sink, directory, name, &original, NULL) : IO_ERROR_NOT_FOUND;
		if (!status) {
			edited.length = original.length;
			status = sink->edit_text(sink->ui_context, name, original.bytes ? original.bytes : "",
			    EDIT_LIMIT, &edited.bytes, &edited.length);
			if (!status && edited.length > EDIT_LIMIT)
				status = IO_ERROR_OUT_OF_RANGE;
			if (!status && edited.bytes && !same_message(&original, &edited))
				status = editor_file(sink, directory, name, &original, &edited);
		}
		free(directory);
		free(original.bytes);
		free(edited.bytes);
		if (status && sink->alert)
			sink->alert(sink->ui_context, "DDO F/X text editor",
			    status == IO_ERROR_CONFLICT
				? "Another session changed the message. Your edits could not be saved."
				: "Could not open or save the message.");
	}
	if (reply_length)
		sink->reply(sink->context, reply, reply_length);
}
