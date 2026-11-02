/* Sandboxed Directory, File, and connected Host filesystem bindings. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"

#include "file_map.h"
#include "filesystem.h"
#include "io_error.h"
#include "path.h"
#include "st_app_paths.h"
#include "st_bbs_list.h"
#include "st_consent_token.h"
#include "string_compat.h"
#include "xp_crypto.h"
#include "xp_digest.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum file_rights {
	FILE_RIGHT_READ = 1U << 0,
	FILE_RIGHT_WRITE = 1U << 1,
};

#define ST_WREN_FILE_MAGIC UINT32_C(0x5354464c)
#define ST_WREN_DIRECTORY_MAGIC UINT32_C(0x53544452)
#define FILE_ERROR_SEEK_FAILED 8
#define FILE_ERROR_OUT_OF_RANGE 9

struct st_wren_file {
	uint32_t magic;
	struct st_wren_context *context;
	char *path;
	char *token;
	FILE *stream;
	unsigned int rights;
	enum st_wren_picker_disposition disposition;
	bool consumed;
};

struct st_wren_directory {
	uint32_t magic;
	struct st_wren_context *context;
	char *path;
	bool relaxed;
};

struct st_wren_file_error {
	int code;
	int native_error;
	char *message;
};

struct fs_binding {
	const char *class_name;
	bool is_static;
	const char *signature;
	WrenForeignMethodFn method;
};

static bool
slot_bytes(WrenVM *vm, int slot, const char **data, size_t *size)
{
	int length;

	if (wrenGetSlotType(vm, slot) != WREN_TYPE_STRING)
		return false;
	*data = wrenGetSlotBytes(vm, slot, &length);
	if (length < 0)
		return false;
	*size = (size_t)length;
	return true;
}

static char *
absolute_path(const char *path)
{
	ptrdiff_t required;
	char *result;

	required = path_make_absolute(NULL, 0, path);
	if (required < 0 || (uintmax_t)required >= SIZE_MAX)
		return NULL;
	result = malloc((size_t)required + 1);
	if (result != NULL
	    && path_make_absolute(result, (size_t)required + 1, path) != required) {
		free(result);
		result = NULL;
	}
	return result;
}

static bool
reserved_name(const char *name, size_t length)
{
	static const char *const reserved[] = {
		"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3",
		"COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1",
		"LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8",
		"LPT9",
	};

	for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i) {
		if (strlen(reserved[i]) != length)
			continue;
		bool equal = true;

		for (size_t j = 0; j < length; ++j) {
			unsigned char c = (unsigned char)name[j];

			if (c >= 'a' && c <= 'z')
				c = (unsigned char)(c - ('a' - 'A'));
			if (c != (unsigned char)reserved[i][j]) {
				equal = false;
				break;
			}
		}
		if (equal)
			return true;
	}
	return false;
}

static bool
clean_name(const char *name, bool relaxed)
{
	size_t length;
	size_t basename;

	if (name == NULL)
		return false;
	length = strlen(name);
	if (length == 0 || length > (relaxed ? 255U : 64U)
	    || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		return false;
	for (size_t i = 0; i < length; ++i) {
		unsigned char c = (unsigned char)name[i];

		if (c == '/' || c == '\\' || c == 0 || c < 0x20 || c == 0x7f)
			return false;
		if (!relaxed && !((c >= 'a' && c <= 'z')
		    || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
		    || c == '.' || c == '_' || c == '-'))
			return false;
	}
	if (!relaxed && (name[0] == '.' || name[0] == '-'
	    || name[length - 1] == '.' || strstr(name, "..") != NULL))
		return false;
	basename = strcspn(name, ".");
	return !reserved_name(name, basename);
}

static char *
child_path(const struct st_wren_directory *directory, const char *name)
{
	char *result = NULL;

	if (!clean_name(name, directory->relaxed)
	    || path_join_alloc(&result, directory->path, name) != 0)
		return NULL;
	return result;
}

static char *
slot_child_path(WrenVM *vm, int slot,
	const struct st_wren_directory *directory)
{
	const char *name;
	int length;

	if (!st_wren_text_slot(vm, slot, &name, &length))
		return NULL;
	if (length < 0 || memchr(name, '\0', (size_t)length) != NULL)
		return NULL;
	return child_path(directory, name);
}

static void
file_error_allocate(WrenVM *vm)
{
	struct st_wren_file_error *error = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*error));

	*error = (struct st_wren_file_error){0};
}

static void
file_error_finalize(void *data)
{
	struct st_wren_file_error *error = data;

	free(error->message);
}

static void
push_error(WrenVM *vm, int code, int native_error, const char *message)
{
	struct st_wren_file_error *error;

	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm", "FileError", 1);
	error = wrenSetSlotNewForeign(vm, 0, 1, sizeof(*error));
	*error = (struct st_wren_file_error){
		.code = code,
		.native_error = native_error,
		.message = message == NULL ? NULL : strdup(message),
	};
}

static void
file_allocate(WrenVM *vm)
{
	struct st_wren_file *file = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*file));

	*file = (struct st_wren_file){.magic = ST_WREN_FILE_MAGIC};
}

static void
file_finalize(void *data)
{
	struct st_wren_file *file = data;

	if (file->stream != NULL)
		(void)fclose(file->stream);
	free(file->path);
	free(file->token);
	file->magic = 0;
}

static void
directory_allocate(WrenVM *vm)
{
	struct st_wren_directory *directory = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*directory));

	*directory = (struct st_wren_directory){
		.magic = ST_WREN_DIRECTORY_MAGIC,
	};
}

static void
directory_finalize(void *data)
{
	struct st_wren_directory *directory = data;

	free(directory->path);
	directory->magic = 0;
}

static struct st_wren_file *
file_handle(WrenVM *vm)
{
	struct st_wren_file *file;

	if (!st_wren_foreign_is(vm, 0, "syncterm", "File")) {
		st_wren_abort(vm, "expected a File");
		return NULL;
	}
	file = wrenGetSlotForeign(vm, 0);
	if (file == NULL || file->magic != ST_WREN_FILE_MAGIC
	    || file->context == NULL || file->path == NULL) {
		st_wren_abort(vm, "File: invalid handle");
		return NULL;
	}
	return file;
}

static struct st_wren_file *
checked_file(WrenVM *vm, bool require_open, unsigned int rights)
{
	struct st_wren_file *file = file_handle(vm);
	struct fs_info info;

	if (file == NULL)
		return NULL;
	if ((file->rights & rights) != rights) {
		st_wren_abort(vm, (rights & FILE_RIGHT_WRITE) != 0
		    ? "File: handle does not grant write access"
		    : "File: handle does not grant read access");
		return NULL;
	}
	if (require_open && file->stream == NULL) {
		st_wren_abort(vm, "File: not open");
		return NULL;
	}
	if (file->stream == NULL
	    && !(file->disposition == ST_WREN_PICKER_DISPOSITION_CREATE
	    && !file->consumed)
	    && fs_get_info(&info, file->path) != 0) {
		push_error(vm, 6, errno, "File vanished");
		return NULL;
	}
	return file;
}

static struct st_wren_directory *
directory_handle(WrenVM *vm)
{
	struct st_wren_directory *directory;

	if (!st_wren_foreign_is(vm, 0, "syncterm", "Directory")) {
		st_wren_abort(vm, "expected a Directory");
		return NULL;
	}
	directory = wrenGetSlotForeign(vm, 0);
	if (directory == NULL || directory->magic != ST_WREN_DIRECTORY_MAGIC
	    || directory->context == NULL || directory->path == NULL) {
		st_wren_abort(vm, "Directory: invalid handle");
		return NULL;
	}
	return directory;
}

static struct st_wren_directory *
checked_directory(WrenVM *vm)
{
	struct st_wren_directory *directory = directory_handle(vm);
	struct fs_info info;

	if (directory == NULL)
		return NULL;
	if (fs_get_info(&info, directory->path) != 0
	    || info.kind != FS_ENTRY_DIRECTORY) {
		st_wren_abort(vm, "Directory: path vanished");
		return NULL;
	}
	return directory;
}

static bool
push_file_full(WrenVM *vm, int slot, const char *path, unsigned int rights,
	enum st_wren_picker_disposition disposition, const char *token)
{
	struct st_wren_file *file;

	wrenEnsureSlots(vm, slot + 2);
	wrenGetVariable(vm, "syncterm", "File", slot + 1);
	file = wrenSetSlotNewForeign(vm, slot, slot + 1, sizeof(*file));
	*file = (struct st_wren_file){
		.magic = ST_WREN_FILE_MAGIC,
		.context = st_wren_context_from_vm(vm),
		.path = strdup(path),
		.token = token == NULL ? NULL : strdup(token),
		.rights = rights,
		.disposition = disposition,
	};
	if (file->path == NULL || (token != NULL && file->token == NULL)) {
		free(file->path);
		free(file->token);
		file->path = NULL;
		file->token = NULL;
		wrenSetSlotNull(vm, slot);
		return false;
	}
	return true;
}

bool
st_wren_file_push(WrenVM *vm, int slot, const char *path,
	enum st_wren_picker_disposition disposition, bool picker_read)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	char *token = NULL;
	uint8_t key[ST_CONSENT_TOKEN_KEY_SIZE];
	bool available = false;
	unsigned int rights = picker_read ? FILE_RIGHT_READ
	    : FILE_RIGHT_READ | FILE_RIGHT_WRITE;

	if (picker_read && host->bbs_list != NULL
	    && st_bbs_list_picker_key(host->bbs_list, key, &available) == 0
	    && available)
		(void)st_consent_token_sign(&token, key, path);
	if (disposition != ST_WREN_PICKER_DISPOSITION_NONE
	    && disposition != ST_WREN_PICKER_DISPOSITION_READ)
		rights = FILE_RIGHT_WRITE;
	bool result = push_file_full(vm, slot, path, rights, disposition, token);

	xp_crypto_clear(key, sizeof(key));
	free(token);
	return result;
}

static bool
push_directory(WrenVM *vm, int slot, const char *path, bool relaxed)
{
	struct st_wren_directory *directory;
	char *absolute = absolute_path(path);

	if (absolute == NULL)
		return false;
	wrenEnsureSlots(vm, slot + 2);
	wrenGetVariable(vm, "syncterm", "Directory", slot + 1);
	directory = wrenSetSlotNewForeign(vm, slot, slot + 1,
	    sizeof(*directory));
	*directory = (struct st_wren_directory){
		.magic = ST_WREN_DIRECTORY_MAGIC,
		.context = st_wren_context_from_vm(vm),
		.path = absolute,
		.relaxed = relaxed,
	};
	return true;
}

static void
fn_directory_contains(WrenVM *vm)
{
	struct st_wren_directory *directory = checked_directory(vm);
	char *path;
	struct fs_info info;

	if (directory == NULL)
		return;
	path = slot_child_path(vm, 1, directory);
	wrenSetSlotBool(vm, 0, path != NULL && fs_get_info(&info, path) == 0
	    && (info.kind == FS_ENTRY_FILE || info.kind == FS_ENTRY_DIRECTORY));
	free(path);
}

static void
fn_directory_list(WrenVM *vm)
{
	struct st_wren_directory *directory = checked_directory(vm);
	struct st_wren_directory_entries entries = {0};

	if (directory == NULL)
		return;
	if (st_wren_directory_enumerate(directory->path, &entries) != 0) {
		push_error(vm, 3, errno, "Directory enumeration failed");
		return;
	}
	wrenEnsureSlots(vm, 4);
	wrenSetSlotNewMap(vm, 0);
	for (size_t i = 0; i < entries.count; ++i) {
		struct st_wren_directory_entry *entry = &entries.items[i];
		char *path;

		if (!clean_name(entry->name, directory->relaxed))
			continue;
		path = child_path(directory, entry->name);
		if (path == NULL)
			continue;
		wrenSetSlotString(vm, 1, entry->name);
		if (entry->directory)
			(void)push_directory(vm, 2, path, directory->relaxed);
		else
			(void)push_file_full(vm, 2, path,
			    FILE_RIGHT_READ | FILE_RIGHT_WRITE,
			    ST_WREN_PICKER_DISPOSITION_NONE, NULL);
		wrenSetMapValue(vm, 0, 1, 2);
		free(path);
	}
	st_wren_directory_entries_dispose(&entries);
}

static void
fn_directory_create(WrenVM *vm)
{
	struct st_wren_directory *directory = checked_directory(vm);
	char *path;
	FILE *stream = NULL;

	if (directory == NULL)
		return;
	path = slot_child_path(vm, 1, directory);
	if (path == NULL || fs_open(&stream, path, "wbx") != 0
	    || fclose(stream) != 0
	    || !push_file_full(vm, 0, path, FILE_RIGHT_READ | FILE_RIGHT_WRITE,
	    ST_WREN_PICKER_DISPOSITION_NONE, NULL))
		wrenSetSlotNull(vm, 0);
	free(path);
}

static void
fn_directory_create_dir(WrenVM *vm)
{
	struct st_wren_directory *directory = checked_directory(vm);
	char *path;

	if (directory == NULL)
		return;
	path = slot_child_path(vm, 1, directory);
	if (path == NULL || fs_create_directory(path) != 0
	    || !push_directory(vm, 0, path, directory->relaxed))
		wrenSetSlotNull(vm, 0);
	free(path);
}

static void
fn_directory_delete(WrenVM *vm)
{
	struct st_wren_directory *directory = checked_directory(vm);
	char *path;
	struct fs_info info;
	int status;

	if (directory == NULL)
		return;
	path = slot_child_path(vm, 1, directory);
	if (path == NULL || fs_get_link_info(&info, path) != 0
	    || (info.kind != FS_ENTRY_FILE && info.kind != FS_ENTRY_DIRECTORY)) {
		wrenSetSlotBool(vm, 0, false);
		free(path);
		return;
	}
	status = info.kind == FS_ENTRY_DIRECTORY ? fs_remove_directory(path)
	    : fs_remove_file(path);
	wrenSetSlotBool(vm, 0, status == 0);
	free(path);
}

static void
fn_directory_replace(WrenVM *vm)
{
	struct st_wren_directory *directory = checked_directory(vm);
	const char *data;
	size_t size;
	char *path;
	int status;

	if (directory == NULL)
		return;
	path = slot_child_path(vm, 1, directory);
	if (path == NULL || !slot_bytes(vm, 2, &data, &size)) {
		st_wren_abort(vm, "Directory.replace: expected a valid name and String");
		free(path);
		return;
	}
	status = fs_replace_file(path, data, size);
	free(path);
	if (status != 0)
		push_error(vm, 2, errno, "Atomic replacement failed");
	else
		wrenSetSlotBool(vm, 0, true);
}

static void
fn_directory_string(WrenVM *vm)
{
	struct st_wren_directory *directory = directory_handle(vm);

	if (directory != NULL)
		wrenSetSlotString(vm, 0, directory->path);
}

static void
fn_file_open(WrenVM *vm)
{
	struct st_wren_file *file = checked_file(vm, false, 0);
	const char *mode;
	int status;

	if (file == NULL)
		return;
	if (file->stream != NULL) {
		st_wren_abort(vm, "File: already open");
		return;
	}
	if (file->disposition != ST_WREN_PICKER_DISPOSITION_NONE
	    && file->disposition != ST_WREN_PICKER_DISPOSITION_READ
	    && file->consumed) {
		st_wren_abort(vm, "File: write consent already used");
		return;
	}
	switch (file->disposition) {
	case ST_WREN_PICKER_DISPOSITION_CREATE: mode = "wbx"; break;
	case ST_WREN_PICKER_DISPOSITION_OVERWRITE: mode = "wb"; break;
	case ST_WREN_PICKER_DISPOSITION_APPEND: mode = "ab"; break;
	default:
		mode = (file->rights & FILE_RIGHT_WRITE) != 0 ? "r+b" : "rb";
		break;
	}
	status = fs_open(&file->stream, file->path, mode);
	if (status != 0) {
		file->stream = NULL;
		push_error(vm, 1, errno, "File open failed");
		return;
	}
	if (file->disposition != ST_WREN_PICKER_DISPOSITION_NONE
	    && file->disposition != ST_WREN_PICKER_DISPOSITION_READ)
		file->consumed = true;
}

static void
fn_file_close(WrenVM *vm)
{
	struct st_wren_file *file = checked_file(vm, false, 0);
	int status;

	if (file == NULL)
		return;
	if (file->stream == NULL) {
		st_wren_abort(vm, "File: not open");
		return;
	}
	status = fclose(file->stream);
	file->stream = NULL;
	if (status != 0)
		push_error(vm, 2, errno, "File close failed");
}

static void
file_read_at(WrenVM *vm, bool explicit_offset)
{
	struct st_wren_file *file = checked_file(vm, true, FILE_RIGHT_READ);
	uint64_t count;
	uint64_t offset = 0;
	char *data;
	size_t got;
	int64_t saved = -1;

	if (file == NULL)
		return;
	if (!st_wren_number_u64(vm, 1, &count) || count > SIZE_MAX
	    || (explicit_offset && !st_wren_number_u64(vm, 2, &offset))) {
		st_wren_abort(vm, "File.readBytes: invalid count or offset");
		return;
	}
	if (explicit_offset)
		saved = fs_stream_position(file->stream);
	if (explicit_offset && (saved < 0
	    || fs_stream_seek(file->stream, (int64_t)offset) != 0)) {
		push_error(vm, FILE_ERROR_SEEK_FAILED, errno, "File seek failed");
		return;
	}
	data = malloc(count == 0 ? 1 : (size_t)count);
	if (data == NULL) {
		if (explicit_offset)
			(void)fs_stream_seek(file->stream, saved);
		push_error(vm, 5, ENOMEM, "Out of memory");
		return;
	}
	got = fread(data, 1, (size_t)count, file->stream);
	if (explicit_offset && fs_stream_seek(file->stream, saved) != 0) {
		free(data);
		push_error(vm, FILE_ERROR_SEEK_FAILED, errno,
		    "File position restore failed");
		return;
	}
	if (ferror(file->stream))
		push_error(vm, 1, errno, "File read failed");
	else
		wrenSetSlotBytes(vm, 0, data, got);
	free(data);
}

static void fn_file_read_bytes(WrenVM *vm) { file_read_at(vm, false); }
static void fn_file_read_bytes_at(WrenVM *vm) { file_read_at(vm, true); }

static void
fn_file_read(WrenVM *vm)
{
	struct st_wren_file *file = checked_file(vm, true, FILE_RIGHT_READ);
	int64_t size;
	char *data;
	size_t got;

	if (file == NULL)
		return;
	size = fs_stream_size(file->stream);
	if (size < 0 || (uint64_t)size > SIZE_MAX
	    || fs_stream_seek(file->stream, 0) != 0) {
		push_error(vm, 3, errno, "File size failed");
		return;
	}
	data = malloc(size == 0 ? 1 : (size_t)size);
	if (data == NULL) {
		push_error(vm, 5, ENOMEM, "Out of memory");
		return;
	}
	got = fread(data, 1, (size_t)size, file->stream);
	if (got != (size_t)size || ferror(file->stream))
		push_error(vm, 1, errno, "File read failed");
	else
		wrenSetSlotBytes(vm, 0, data, got);
	free(data);
}

static void
file_write_at(WrenVM *vm, bool explicit_offset)
{
	struct st_wren_file *file = checked_file(vm, true, FILE_RIGHT_WRITE);
	const char *data;
	size_t size;
	uint64_t offset = 0;
	int64_t saved = -1;
	bool write_failed;
	int write_error = 0;

	if (file == NULL)
		return;
	if (!slot_bytes(vm, 1, &data, &size)
	    || (explicit_offset && !st_wren_number_u64(vm, 2, &offset))) {
		st_wren_abort(vm, "File.writeBytes: invalid bytes or offset");
		return;
	}
	if (explicit_offset)
		saved = fs_stream_position(file->stream);
	if (explicit_offset && (saved < 0
	    || fs_stream_seek(file->stream, (int64_t)offset) != 0)) {
		push_error(vm, FILE_ERROR_SEEK_FAILED, errno, "File seek failed");
		return;
	}
	write_failed = size != 0
	    && fwrite(data, 1, size, file->stream) != size;
	if (write_failed)
		write_error = errno;
	if (explicit_offset && fs_stream_seek(file->stream, saved) != 0) {
		push_error(vm, FILE_ERROR_SEEK_FAILED, errno,
		    "File position restore failed");
		return;
	}
	if (write_failed)
		push_error(vm, 2, write_error, "File write failed");
}

static void fn_file_write_bytes(WrenVM *vm) { file_write_at(vm, false); }
static void fn_file_write_bytes_at(WrenVM *vm) { file_write_at(vm, true); }

static void
fn_file_read_line(WrenVM *vm)
{
	struct st_wren_file *file = checked_file(vm, true, FILE_RIGHT_READ);
	char *line = NULL;
	size_t size = 0;
	size_t capacity = 0;
	int c;

	if (file == NULL)
		return;
	while ((c = fgetc(file->stream)) != EOF && c != '\n') {
		if (size == capacity) {
			size_t next = capacity == 0 ? 128 : capacity * 2;
			char *grown = next > capacity ? realloc(line, next) : NULL;

			if (grown == NULL) {
				free(line);
				push_error(vm, 5, ENOMEM, "Out of memory");
				return;
			}
			line = grown;
			capacity = next;
		}
		line[size++] = (char)c;
	}
	if (c == EOF && size == 0 && !ferror(file->stream))
		wrenSetSlotNull(vm, 0);
	else if (ferror(file->stream))
		push_error(vm, 1, errno, "File read failed");
	else {
		if (size != 0 && line[size - 1] == '\r')
			size--;
		wrenSetSlotBytes(vm, 0, line == NULL ? "" : line, size);
	}
	free(line);
}

static void
fn_file_write_line(WrenVM *vm)
{
	struct st_wren_file *file = checked_file(vm, true, FILE_RIGHT_WRITE);
	const char *data;
	size_t size;

	if (file == NULL)
		return;
	if (!slot_bytes(vm, 1, &data, &size)) {
		st_wren_abort(vm, "File.writeLine: expected a String");
		return;
	}
	if ((size != 0 && fwrite(data, 1, size, file->stream) != size)
	    || fputc('\n', file->stream) == EOF)
		push_error(vm, 2, errno, "File write failed");
}

static void
fn_file_write(WrenVM *vm)
{
	struct st_wren_file *file = checked_file(vm, true, FILE_RIGHT_WRITE);
	const char *data;
	size_t size;
	bool append;

	if (file == NULL)
		return;
	if (!slot_bytes(vm, 1, &data, &size)) {
		st_wren_abort(vm, "File.write: expected a String");
		return;
	}
	append = file->disposition == ST_WREN_PICKER_DISPOSITION_APPEND;
	if (!append && (fs_stream_seek(file->stream, 0) != 0
	    || fs_stream_truncate(file->stream, 0) != 0)) {
		push_error(vm, FILE_ERROR_SEEK_FAILED, errno,
		    "File truncate failed");
		return;
	}
	if (size != 0 && fwrite(data, 1, size, file->stream) != size)
		push_error(vm, 2, errno, "File write failed");
}

static void
fn_file_offset(WrenVM *vm)
{
	struct st_wren_file *file = checked_file(vm, true, 0);
	int64_t position;

	if (file == NULL)
		return;
	position = fs_stream_position(file->stream);
	if (position < 0)
		push_error(vm, FILE_ERROR_SEEK_FAILED, errno,
		    "File position failed");
	else if (!st_wren_i64_exact(position))
		push_error(vm, FILE_ERROR_OUT_OF_RANGE, 0, "File position exceeds the exact Number range");
	else
		st_wren_return_i64(vm, position);
}

static void
fn_file_offset_set(WrenVM *vm)
{
	struct st_wren_file *file = checked_file(vm, true, 0);
	uint64_t offset;

	if (file == NULL)
		return;
	if (!st_wren_number_u64(vm, 1, &offset)) {
		st_wren_abort(vm, "File.offset: invalid exact nonnegative integer");
		return;
	}
	if (fs_stream_seek(file->stream, (int64_t)offset) != 0)
		push_error(vm, FILE_ERROR_SEEK_FAILED, errno, "File seek failed");
	else
		wrenSetSlotDouble(vm, 0, (double)offset);
}

static void
fn_file_size(WrenVM *vm)
{
	struct st_wren_file *file = checked_file(vm, false, 0);
	struct fs_info info;
	int64_t size;

	if (file == NULL)
		return;
	if (file->stream != NULL) {
		size = fs_stream_size(file->stream);
		if (size >= 0) {
			if (!st_wren_i64_exact(size))
				push_error(vm, FILE_ERROR_OUT_OF_RANGE, 0, "File size exceeds the exact Number range");
			else
				st_wren_return_i64(vm, size);
			return;
		}
	}
	else if (fs_get_info(&info, file->path) == 0
	    && info.kind == FS_ENTRY_FILE) {
		if (!st_wren_u64_exact(info.size))
			push_error(vm, FILE_ERROR_OUT_OF_RANGE, 0, "File size exceeds the exact Number range");
		else
			st_wren_return_u64(vm, info.size);
		return;
	}
	push_error(vm, 3, errno, "File size failed");
}

static void
fn_file_mtime(WrenVM *vm)
{
	struct st_wren_file *file = checked_file(vm, false, 0);
	struct fs_info info;

	if (file == NULL)
		return;
	if (fs_get_info(&info, file->path) != 0)
		push_error(vm, 3, errno, "File stat failed");
	else if (!st_wren_i64_exact(info.modified_time))
		push_error(vm, FILE_ERROR_OUT_OF_RANGE, 0, "File mtime exceeds the exact Number range");
	else
		st_wren_return_i64(vm, info.modified_time);
}

static void
fn_file_mtime_set(WrenVM *vm)
{
	struct st_wren_file *file = checked_file(vm, false, FILE_RIGHT_WRITE);
	uint64_t modified;

	if (file == NULL)
		return;
	if (!st_wren_number_u64(vm, 1, &modified)) {
		st_wren_abort(vm, "File.mtime: invalid exact nonnegative integer");
		return;
	}
	if (fs_set_modified_time(file->path, (int64_t)modified) != 0)
		push_error(vm, 3, errno, "File mtime update failed");
	else
		wrenSetSlotDouble(vm, 0, (double)modified);
}

static void
fn_file_is_open(WrenVM *vm)
{
	struct st_wren_file *file = st_wren_foreign_is(vm, 0, "syncterm", "File")
	    ? wrenGetSlotForeign(vm, 0) : NULL;

	wrenSetSlotBool(vm, 0, file != NULL && file->magic == ST_WREN_FILE_MAGIC
	    && file->stream != NULL);
}

static void
fn_file_path(WrenVM *vm)
{
	struct st_wren_file *file = file_handle(vm);

	if (file != NULL)
		wrenSetSlotString(vm, 0, file->path);
}

const char *
st_wren_file_read_path(WrenVM *vm, int slot)
{
	struct st_wren_file *file;

	if (!st_wren_foreign_is(vm, slot, "syncterm", "File"))
		return NULL;
	file = wrenGetSlotForeign(vm, slot);
	if (file == NULL || file->magic != ST_WREN_FILE_MAGIC
	    || file->context != st_wren_context_from_vm(vm)
	    || (file->rights & FILE_RIGHT_READ) == 0)
		return NULL;
	return file->path;
}

bool
st_wren_file_claim_write(WrenVM *vm, int slot,
	const char *operation, FILE **stream)
{
	struct st_wren_file *file;
	const char *mode;
	char message[112];

	if (operation == NULL || stream == NULL)
		return false;
	if (!st_wren_foreign_is(vm, slot, "syncterm", "File")) {
		(void)snprintf(message, sizeof(message), "%s expects a File",
		    operation);
		st_wren_abort(vm, message);
		return false;
	}
	file = wrenGetSlotForeign(vm, slot);
	if (file == NULL || file->magic != ST_WREN_FILE_MAGIC
	    || file->context != st_wren_context_from_vm(vm)
	    || file->path == NULL || (file->rights & FILE_RIGHT_WRITE) == 0
	    || file->stream != NULL || file->consumed) {
		(void)snprintf(message, sizeof(message),
		    "%s requires unused write consent", operation);
		st_wren_abort(vm, message);
		return false;
	}
	switch (file->disposition) {
	case ST_WREN_PICKER_DISPOSITION_CREATE: mode = "wbx"; break;
	case ST_WREN_PICKER_DISPOSITION_OVERWRITE: mode = "wb"; break;
	case ST_WREN_PICKER_DISPOSITION_APPEND: mode = "ab"; break;
	default:
		(void)snprintf(message, sizeof(message),
		    "%s requires save-path consent", operation);
		st_wren_abort(vm, message);
		return false;
	}
	if (fs_open(stream, file->path, mode) != 0) {
		(void)snprintf(message, sizeof(message), "%s open failed",
		    operation);
		push_error(vm, 1, errno, message);
		return false;
	}
	file->consumed = true;
	return true;
}

void
st_wren_file_write_error(WrenVM *vm, int native_error,
	const char *message)
{
	push_error(vm, 2, native_error, message);
}

const char *
st_wren_directory_path(WrenVM *vm, int slot)
{
	struct st_wren_directory *directory;

	if (!st_wren_foreign_is(vm, slot, "syncterm", "Directory"))
		return NULL;
	directory = wrenGetSlotForeign(vm, slot);
	if (directory == NULL || directory->magic != ST_WREN_DIRECTORY_MAGIC
	    || directory->context != st_wren_context_from_vm(vm))
		return NULL;
	return directory->path;
}

static void
fn_file_token(WrenVM *vm)
{
	struct st_wren_file *file = file_handle(vm);

	if (file == NULL || file->token == NULL)
		wrenSetSlotNull(vm, 0);
	else
		wrenSetSlotString(vm, 0, file->token);
}

static void
file_digest(WrenVM *vm, enum xp_digest_algorithm algorithm)
{
	struct st_wren_file *file = checked_file(vm, false, FILE_RIGHT_READ);
	file_map_t *mapping = NULL;
	uint8_t digest[64];
	size_t size = xp_digest_size(algorithm);

	if (file == NULL)
		return;
	if (file->stream != NULL && (file->rights & FILE_RIGHT_WRITE) != 0
	    && fflush(file->stream) != 0) {
		push_error(vm, 2, errno, "File flush failed");
		return;
	}
	if (size == 0 || file_map_open(&mapping, file->path) != 0
	    || xp_digest(algorithm, file_map_data(mapping), file_map_size(mapping),
	    digest, size) != XP_CRYPTO_OK)
		push_error(vm, 4, errno, "File digest failed");
	else
		wrenSetSlotBytes(vm, 0, (const char *)digest, size);
	file_map_close(mapping);
}

static void fn_file_sha1(WrenVM *vm) { file_digest(vm, XP_DIGEST_SHA1); }
static void fn_file_md5(WrenVM *vm) { file_digest(vm, XP_DIGEST_MD5); }

static void fn_file_error_code(WrenVM *vm)
{
	struct st_wren_file_error *error = wrenGetSlotForeign(vm, 0);
	wrenSetSlotDouble(vm, 0, error->code);
}

static void fn_file_error_errno(WrenVM *vm)
{
	struct st_wren_file_error *error = wrenGetSlotForeign(vm, 0);
	wrenSetSlotDouble(vm, 0, error->native_error);
}

static void fn_file_error_message(WrenVM *vm)
{
	struct st_wren_file_error *error = wrenGetSlotForeign(vm, 0);
	if (error->message == NULL)
		wrenSetSlotNull(vm, 0);
	else
		wrenSetSlotString(vm, 0, error->message);
}

static void fn_file_error_string(WrenVM *vm)
{
	struct st_wren_file_error *error = wrenGetSlotForeign(vm, 0);
	char value[256];

	(void)snprintf(value, sizeof(value), "FileError %d: %s", error->code,
	    error->message == NULL ? "I/O failure" : error->message);
	wrenSetSlotString(vm, 0, value);
}

static void
fn_host_cache(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	const struct st_profile *profile = context->profile;
	char *path = NULL;

	if (profile == NULL && host->selected_valid)
		profile = &host->selected;
	if (host->bbs_list == NULL || profile == NULL
	    || st_bbs_list_cache_path(&path, host->bbs_list,
	    profile->name) != 0 || fs_create_directories(path) != 0
	    || !push_directory(vm, 0, path, false))
		wrenSetSlotNull(vm, 0);
	free(path);
}

static bool
dangerous_download(const char *path)
{
	const char *home = getenv("HOME");
	char *absolute_path_value;
	char *absolute_home;
	bool result;

	if (path == NULL || path[0] == '\0')
		return true;
	absolute_path_value = absolute_path(path);
	absolute_home = home == NULL ? NULL : absolute_path(home);
	result = absolute_path_value == NULL
	    || (absolute_home != NULL
	    && strcmp(absolute_path_value, absolute_home) == 0);
	free(absolute_path_value);
	free(absolute_home);
	return result;
}

bool
st_wren_download_directory_allowed(const char *path)
{
	struct fs_info info;
	return !dangerous_download(path) && fs_get_info(&info, path) == 0
	    && info.kind == FS_ENTRY_DIRECTORY;
}

static void
fn_host_download(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	const struct st_profile *profile = context->profile;
	const char *path;
	struct fs_info info;

	if (profile == NULL && host->selected_valid)
		profile = &host->selected;
	path = profile == NULL ? NULL : profile->download_directory;
	if (profile == NULL || dangerous_download(path)
	    || fs_get_info(&info, path) != 0 || info.kind != FS_ENTRY_DIRECTORY
	    || !push_directory(vm, 0, path, true))
		wrenSetSlotNull(vm, 0);
}

static void
fn_host_upload(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	const struct st_profile *profile = context->profile;
	const char *path;

	if (profile == NULL && host->selected_valid)
		profile = &host->selected;
	path = profile == NULL ? NULL : profile->upload_directory;
	if (profile == NULL || path == NULL || path[0] == '\0')
		wrenSetSlotNull(vm, 0);
	else
		wrenSetSlotString(vm, 0, path);
}

static void
fn_host_open_local(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	uint8_t key[ST_CONSENT_TOKEN_KEY_SIZE] = {0};
	bool available = false;
	char *path = NULL;
	const char *token;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_STRING
	    || host->bbs_list == NULL
	    || st_bbs_list_picker_key(host->bbs_list, key, &available) != 0
	    || !available) {
		wrenSetSlotNull(vm, 0);
		goto finished;
	}
	token = wrenGetSlotString(vm, 1);
	if (st_consent_token_verify(&path, key, token) != XP_CRYPTO_OK
	    || !push_file_full(vm, 0, path, FILE_RIGHT_READ,
	    ST_WREN_PICKER_DISPOSITION_READ, token))
		wrenSetSlotNull(vm, 0);

finished:
	xp_crypto_clear(key, sizeof(key));
	free(path);
}

static const struct fs_binding bindings[] = {
	{"Directory", false, "contains(_)", fn_directory_contains},
	{"Directory", false, "list", fn_directory_list},
	{"Directory", false, "create(_)", fn_directory_create},
	{"Directory", false, "createDir(_)", fn_directory_create_dir},
	{"Directory", false, "delete(_)", fn_directory_delete},
	{"Directory", false, "replace(_,_)", fn_directory_replace},
	{"Directory", false, "toString", fn_directory_string},
	{"File", false, "path", fn_file_path},
	{"File", false, "open()", fn_file_open},
	{"File", false, "close()", fn_file_close},
	{"File", false, "readBytes(_)", fn_file_read_bytes},
	{"File", false, "readBytes(_,_)", fn_file_read_bytes_at},
	{"File", false, "read()", fn_file_read},
	{"File", false, "writeBytes(_)", fn_file_write_bytes},
	{"File", false, "writeBytes(_,_)", fn_file_write_bytes_at},
	{"File", false, "write(_)", fn_file_write},
	{"File", false, "readLine()", fn_file_read_line},
	{"File", false, "writeLine(_)", fn_file_write_line},
	{"File", false, "offset", fn_file_offset},
	{"File", false, "offset=(_)", fn_file_offset_set},
	{"File", false, "size", fn_file_size},
	{"File", false, "mtime", fn_file_mtime},
	{"File", false, "mtime=(_)", fn_file_mtime_set},
	{"File", false, "isOpen", fn_file_is_open},
	{"File", false, "token", fn_file_token},
	{"File", false, "sha1", fn_file_sha1},
	{"File", false, "md5", fn_file_md5},
	{"File", false, "toString", fn_file_path},
	{"FileError", false, "code", fn_file_error_code},
	{"FileError", false, "errno", fn_file_error_errno},
	{"FileError", false, "message", fn_file_error_message},
	{"FileError", false, "toString", fn_file_error_string},
	{"Host", true, "cacheDirectory", fn_host_cache},
	{"Host", true, "downloadDir", fn_host_download},
	{"Host", true, "uploadPath", fn_host_upload},
	{"Host", true, "openLocalFile(_)", fn_host_open_local},
};

WrenForeignMethodFn
st_wren_fs_bind_method(const char *class_name, bool is_static,
	const char *signature)
{
	for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i) {
		if (bindings[i].is_static == is_static
		    && strcmp(bindings[i].class_name, class_name) == 0
		    && strcmp(bindings[i].signature, signature) == 0)
			return bindings[i].method;
	}
	return NULL;
}

WrenForeignClassMethods
st_wren_fs_bind_class(const char *class_name)
{
	WrenForeignClassMethods methods = {NULL, NULL};

	if (strcmp(class_name, "Directory") == 0) {
		methods.allocate = directory_allocate;
		methods.finalize = directory_finalize;
	}
	else if (strcmp(class_name, "File") == 0) {
		methods.allocate = file_allocate;
		methods.finalize = file_finalize;
	}
	else if (strcmp(class_name, "FileError") == 0) {
		methods.allocate = file_error_allocate;
		methods.finalize = file_error_finalize;
	}
	return methods;
}
