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
#include "st_crypto.h"
#include "st_digest.h"

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

enum file_direction { FILE_IDLE, FILE_READING, FILE_WRITING };

struct entry_cache_owner {
	uint64_t generation, identity;
	char name[ST_PROFILE_NAME_MAX + 1];
};

static bool
cache_owner_valid(WrenVM *vm, const struct entry_cache_owner *owner)
{
	if (owner->identity == 0) return true;
	struct st_wren_bbs_record *record = st_wren_bbs_model_find(
	    &st_wren_host_from_vm(vm)->bbs_model, owner->generation, owner->identity);
	return record != NULL && !record->new_entry
	    && record->source == ST_BBS_SOURCE_PERSONAL
	    && strcmp(record->persisted_name, owner->name) == 0;
}
static bool
check_cache_owner(WrenVM *vm, const struct entry_cache_owner *owner)
{
	if (cache_owner_valid(vm, owner)) return true;
	st_wren_abort(vm, "entry cache grant is stale");
	return false;
}

struct st_wren_file {
	uint32_t magic;
	struct st_wren_context *context;
	struct entry_cache_owner owner;
	char *path;
	char *token;
	FILE *stream;
	enum file_direction direction;
	unsigned int rights;
	enum st_wren_picker_disposition disposition;
	bool consumed;
};

struct st_wren_directory {
	uint32_t magic;
	struct st_wren_context *context;
	struct entry_cache_owner owner;
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

static void push_error(WrenVM *vm, int code, int native_error, const char *message);

static void
push_errno_error(WrenVM *vm, int native_error, const char *message)
{
	push_error(vm, native_error == 0 ? IO_ERROR_IO : io_error_from_errno(native_error),
	    native_error, message);
}

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
	char *path = NULL;
	int length;
	int status;

	if (!st_wren_text_slot(vm, slot, &name, &length) || length < 0
	    || memchr(name, '\0', (size_t)length) != NULL
	    || !clean_name(name, directory->relaxed)) {
		st_wren_abort(vm, "Directory: expected a valid child name");
		return NULL;
	}
	status = path_join_alloc(&path, directory->path, name);
	if (status != 0)
		push_error(vm, status, 0, "Directory child path failed");
	return path;
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
		.code = code < 0 ? code : IO_ERROR_IO,
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
	    || file->context != st_wren_context_from_vm(vm) || file->path == NULL) {
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
	if (file->stream == NULL && !check_cache_owner(vm, &file->owner)) return NULL;
	if (file->stream == NULL
	    && !(file->disposition == ST_WREN_PICKER_DISPOSITION_CREATE
	    && !file->consumed)) {
		errno = 0;
		int status = fs_get_info(&info, file->path);
		if (status != 0) {
			push_error(vm, status, errno, "File path unavailable");
			return NULL;
		}
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
	    || directory->context != st_wren_context_from_vm(vm) || directory->path == NULL) {
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

	if (directory == NULL || !check_cache_owner(vm, &directory->owner))
		return NULL;
	errno = 0;
	int status = fs_get_info(&info, directory->path);
	if (status != 0 || info.kind != FS_ENTRY_DIRECTORY) {
		push_error(vm, status != 0 ? status : IO_ERROR_NOT_DIRECTORY,
		    status != 0 ? errno : 0, "Directory root unavailable");
		return NULL;
	}
	return directory;
}

static struct st_wren_directory *
directory_child(WrenVM *vm, char **path)
{
	struct st_wren_directory *directory = directory_handle(vm);

	if (directory == NULL)
		return NULL;
	*path = slot_child_path(vm, 1, directory);
	if (*path == NULL)
		return NULL;
	if (checked_directory(vm) == NULL) {
		free(*path);
		*path = NULL;
		return NULL;
	}
	return directory;
}

static bool
push_file_full(WrenVM *vm, int slot, const char *path, unsigned int rights,
	enum st_wren_picker_disposition disposition, const char *token,
	const struct entry_cache_owner *owner)
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
	if (owner != NULL) file->owner = *owner;
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
	bool result = push_file_full(vm, slot, path, rights, disposition, token, NULL);

	st_crypto_clear(key, sizeof(key));
	free(token);
	return result;
}

static bool
push_directory(WrenVM *vm, int slot, const char *path, bool relaxed,
	const struct entry_cache_owner *owner)
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
	if (owner != NULL) directory->owner = *owner;
	return true;
}

static void
fn_directory_contains(WrenVM *vm)
{
	char *path = NULL;
	struct st_wren_directory *directory = directory_child(vm, &path);
	struct fs_info info;
	int status;

	if (directory == NULL)
		return;
	errno = 0;
	status = fs_get_link_info(&info, path);
	if (status != 0 && status != IO_ERROR_NOT_FOUND)
		push_error(vm, status, errno, "Directory query failed");
	else
		wrenSetSlotBool(vm, 0, status == 0
		    && (info.kind == FS_ENTRY_FILE || info.kind == FS_ENTRY_DIRECTORY));
	free(path);
}

static void
fn_directory_list(WrenVM *vm)
{
	struct st_wren_directory *directory = checked_directory(vm);
	struct st_wren_directory_entries entries = {0};
	WrenHandle *owner;
	int status;

	if (directory == NULL)
		return;
	errno = 0;
	status = st_wren_directory_enumerate(directory->path, &entries);
	if (status != 0) {
		push_error(vm, status, errno, "Directory enumeration failed");
		return;
	}
	owner = wrenGetSlotHandle(vm, 0);
	wrenEnsureSlots(vm, 4);
	wrenSetSlotNewMap(vm, 0);
	for (size_t i = 0; i < entries.count; ++i) {
		struct st_wren_directory_entry *entry = &entries.items[i];
		char *path;
		bool pushed;

		if (!clean_name(entry->name, directory->relaxed))
			continue;
		path = child_path(directory, entry->name);
		if (path == NULL) {
			push_error(vm, IO_ERROR_OUT_OF_MEMORY, 0, "Directory child path failed");
			break;
		}
		struct fs_info info;
		errno = 0;
		status = fs_get_link_info(&info, path);
		if (status == IO_ERROR_NOT_FOUND) {
			free(path);
			continue;
		}
		if (status != 0) {
			push_error(vm, status, errno, "Directory entry metadata failed");
			free(path);
			break;
		}
		/* Picker navigation may follow links after explicit user consent.
		 * Directory grants must not mint access through those links. */
		if (info.kind != FS_ENTRY_FILE && info.kind != FS_ENTRY_DIRECTORY) {
			free(path);
			continue;
		}
		wrenSetSlotString(vm, 1, entry->name);
		if (info.kind == FS_ENTRY_DIRECTORY)
			pushed = push_directory(vm, 2, path, directory->relaxed, &directory->owner);
		else
			pushed = push_file_full(vm, 2, path,
			    FILE_RIGHT_READ | FILE_RIGHT_WRITE,
			    ST_WREN_PICKER_DISPOSITION_NONE, NULL, &directory->owner);
		free(path);
		if (!pushed) {
			push_error(vm, IO_ERROR_OUT_OF_MEMORY, 0, "Directory entry allocation failed");
			break;
		}
		wrenSetMapValue(vm, 0, 1, 2);
	}
	wrenReleaseHandle(vm, owner);
	st_wren_directory_entries_dispose(&entries);
}

static void
fn_directory_create(WrenVM *vm)
{
	char *path = NULL;
	struct st_wren_directory *directory = directory_child(vm, &path);
	FILE *stream = NULL;
	int status;

	if (directory == NULL)
		return;
	errno = 0;
	status = fs_open(&stream, path, "wbx");
	if (status != 0)
		push_error(vm, status, errno, "File creation failed");
	else {
		errno = 0;
		if (fclose(stream) != 0)
			push_errno_error(vm, errno, "New file close failed");
		else if (!push_file_full(vm, 0, path, FILE_RIGHT_READ | FILE_RIGHT_WRITE,
		    ST_WREN_PICKER_DISPOSITION_NONE, NULL, &directory->owner))
			push_error(vm, IO_ERROR_OUT_OF_MEMORY, 0, "File grant allocation failed");
	}
	free(path);
}

static void
fn_directory_create_dir(WrenVM *vm)
{
	char *path = NULL;
	struct st_wren_directory *directory = directory_child(vm, &path);
	int status;

	if (directory == NULL)
		return;
	errno = 0;
	status = fs_create_directory(path);
	if (status != 0)
		push_error(vm, status, errno, "Directory creation failed");
	else if (!push_directory(vm, 0, path, directory->relaxed, &directory->owner))
		push_error(vm, IO_ERROR_OUT_OF_MEMORY, 0, "Directory grant allocation failed");
	free(path);
}

static void
fn_directory_delete(WrenVM *vm)
{
	char *path = NULL;
	struct st_wren_directory *directory = directory_child(vm, &path);
	struct fs_info info;
	int status;

	if (directory == NULL)
		return;
	errno = 0;
	status = fs_get_link_info(&info, path);
	if (status == 0) {
		if (info.kind == FS_ENTRY_DIRECTORY)
			status = fs_remove_directory(path);
		else if (info.kind == FS_ENTRY_FILE)
			status = fs_remove_file(path);
		else
			status = IO_ERROR_NOT_SUPPORTED;
	}
	if (status != 0)
		push_error(vm, status, errno, "Directory entry removal failed");
	else
		wrenSetSlotNull(vm, 0);
	free(path);
}

static void
fn_directory_replace(WrenVM *vm)
{
	struct st_wren_directory *directory;
	const char *data;
	size_t size;
	char *path = NULL;
	int status;

	if (!slot_bytes(vm, 2, &data, &size)) {
		st_wren_abort(vm, "Directory.replace expects a byte String");
		return;
	}
	directory = directory_child(vm, &path);
	if (directory == NULL)
		return;
	errno = 0;
	status = fs_replace_file(path, data, size);
	if (status != 0)
		push_error(vm, status, errno, "Atomic replacement failed");
	else
		wrenSetSlotNull(vm, 0);
	free(path);
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
	errno = 0;
	status = fs_open(&file->stream, file->path, mode);
	if (status != 0) {
		file->stream = NULL;
		push_error(vm, status, errno, "File open failed");
		return;
	}
	if (file->disposition != ST_WREN_PICKER_DISPOSITION_NONE
	    && file->disposition != ST_WREN_PICKER_DISPOSITION_READ)
		file->consumed = true;
	wrenSetSlotNull(vm, 0);
}

static void
fn_file_close(WrenVM *vm)
{
	struct st_wren_file *file = file_handle(vm);
	int status;

	if (file == NULL)
		return;
	if (file->stream == NULL) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	errno = 0;
	status = fclose(file->stream);
	file->stream = NULL;
	file->direction = FILE_IDLE;
	if (status != 0)
		push_errno_error(vm, errno, "File close failed");
	else
		wrenSetSlotNull(vm, 0);
}

static bool
file_seek(WrenVM *vm, struct st_wren_file *file, int64_t offset, const char *message)
{
	errno = 0;
	int status = fs_stream_seek(file->stream, offset);
	if (status == 0) {
		file->direction = FILE_IDLE;
		return true;
	}
	push_error(vm, status, errno, message);
	return false;
}

static int64_t
file_position(WrenVM *vm, FILE *stream)
{
	errno = 0;
	int64_t position = fs_stream_position(stream);
	if (position < 0)
		push_error(vm, (int)position, errno, "File position failed");
	return position;
}

static bool
file_flush(WrenVM *vm, struct st_wren_file *file)
{
	if (file->direction != FILE_WRITING)
		return true;
	errno = 0;
	if (fflush(file->stream) != 0) {
		push_errno_error(vm, errno, "File flush failed");
		return false;
	}
	file->direction = FILE_IDLE;
	return true;
}

static bool
file_prepare_write(WrenVM *vm, struct st_wren_file *file)
{
	/* C update streams require positioning between input and output. Keep
	 * their buffering/direction rules inside the binding, not in scripts. */
	if (file->direction == FILE_READING) {
		int64_t position = file_position(vm, file->stream);
		if (position < 0 || !file_seek(vm, file, position, "File direction change failed"))
			return false;
	}
	file->direction = FILE_WRITING;
	return true;
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
	bool failed;
	int read_error;

	if (file == NULL)
		return;
	if (!st_wren_number_u64(vm, 1, &count) || count > SIZE_MAX
	    || (explicit_offset && !st_wren_number_u64(vm, 2, &offset))) {
		st_wren_abort(vm, "File.readBytes: invalid count or offset");
		return;
	}
	if (count == 0) {
		wrenSetSlotBytes(vm, 0, "", 0);
		return;
	}
	if (explicit_offset) {
		saved = file_position(vm, file->stream);
		if (saved < 0 || !file_seek(vm, file, (int64_t)offset, "File seek failed"))
			return;
	}
	data = malloc((size_t)count);
	if (data == NULL) {
		if (explicit_offset && !file_seek(vm, file, saved, "File position restore failed"))
			return;
		push_error(vm, IO_ERROR_OUT_OF_MEMORY, 0, "Out of memory");
		return;
	}
	if (!file_flush(vm, file)) {
		free(data);
		return;
	}
	file->direction = FILE_READING;
	clearerr(file->stream);
	errno = 0;
	got = fread(data, 1, (size_t)count, file->stream);
	failed = ferror(file->stream) != 0;
	read_error = errno;
	if (explicit_offset && !file_seek(vm, file, saved, "File position restore failed")) {
		free(data);
		return;
	}
	if (failed)
		push_errno_error(vm, read_error, "File read failed");
	else if (got == 0)
		wrenSetSlotNull(vm, 0);
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
	if (!file_seek(vm, file, 0, "File seek failed"))
		return;
	errno = 0;
	size = fs_stream_size(file->stream);
	if (size < 0) {
		push_error(vm, (int)size, errno, "File size failed");
		return;
	}
	if ((uint64_t)size > SIZE_MAX) {
		push_error(vm, IO_ERROR_OUT_OF_RANGE, 0, "File is too large for one read");
		return;
	}
	data = malloc(size == 0 ? 1 : (size_t)size);
	if (data == NULL) {
		push_error(vm, IO_ERROR_OUT_OF_MEMORY, ENOMEM, "Out of memory");
		return;
	}
	if (!file_flush(vm, file)) {
		free(data);
		return;
	}
	file->direction = FILE_READING;
	clearerr(file->stream);
	errno = 0;
	got = fread(data, 1, (size_t)size, file->stream);
	if (ferror(file->stream))
		push_errno_error(vm, errno, "File read failed");
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
	if (size == 0) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	if (explicit_offset) {
		saved = file_position(vm, file->stream);
		if (saved < 0 || !file_seek(vm, file, (int64_t)offset, "File seek failed"))
			return;
	}
	if (!file_prepare_write(vm, file))
		return;
	clearerr(file->stream);
	errno = 0;
	write_failed = size != 0
	    && fwrite(data, 1, size, file->stream) != size;
	if (write_failed)
		write_error = errno;
	if (explicit_offset && !file_seek(vm, file, saved, "File position restore failed"))
		return;
	if (write_failed)
		push_errno_error(vm, write_error, "File write failed");
	else
		wrenSetSlotNull(vm, 0);
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
	if (!file_flush(vm, file))
		return;
	file->direction = FILE_READING;
	clearerr(file->stream);
	errno = 0;
	while ((c = fgetc(file->stream)) != EOF && c != '\n') {
		if (size == capacity) {
			size_t next = capacity == 0 ? 128 : capacity * 2;
			char *grown = next > capacity ? realloc(line, next) : NULL;

			if (grown == NULL) {
				free(line);
				push_error(vm, IO_ERROR_OUT_OF_MEMORY, ENOMEM, "Out of memory");
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
		push_errno_error(vm, errno, "File read failed");
	else {
		if (c == '\n' && size != 0 && line[size - 1] == '\r')
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
	if (!file_prepare_write(vm, file))
		return;
	clearerr(file->stream);
	errno = 0;
	if ((size != 0 && fwrite(data, 1, size, file->stream) != size)
	    || fputc('\n', file->stream) == EOF)
		push_errno_error(vm, errno, "File write failed");
	else
		wrenSetSlotNull(vm, 0);
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
	if (!append) {
		if (!file_seek(vm, file, 0, "File seek failed"))
			return;
		errno = 0;
		int status = fs_stream_truncate(file->stream, 0);
		if (status != 0) {
			push_error(vm, status, errno, "File truncate failed");
			return;
		}
	}
	if (!file_prepare_write(vm, file))
		return;
	clearerr(file->stream);
	errno = 0;
	if (size != 0 && fwrite(data, 1, size, file->stream) != size)
		push_errno_error(vm, errno, "File write failed");
	else
		wrenSetSlotNull(vm, 0);
}

static void
fn_file_offset(WrenVM *vm)
{
	struct st_wren_file *file = checked_file(vm, true, 0);
	int64_t position;

	if (file == NULL)
		return;
	position = file_position(vm, file->stream);
	if (position < 0)
		return;
	else if (!st_wren_i64_exact(position))
		push_error(vm, IO_ERROR_OUT_OF_RANGE, 0, "File position exceeds the exact Number range");
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
	if (file_seek(vm, file, (int64_t)offset, "File seek failed"))
		wrenSetSlotDouble(vm, 0, (double)offset);
}

static void
fn_file_size(WrenVM *vm)
{
	struct st_wren_file *file = checked_file(vm, false, 0);
	struct fs_info info;
	uint64_t size;

	if (file == NULL || !file_flush(vm, file))
		return;
	errno = 0;
	if (file->stream != NULL) {
		int64_t result = fs_stream_size(file->stream);
		if (result < 0) {
			push_error(vm, (int)result, errno, "File size failed");
			return;
		}
		size = (uint64_t)result;
	}
	else {
		int status = fs_get_info(&info, file->path);
		if (status != 0 || info.kind != FS_ENTRY_FILE) {
			push_error(vm, status != 0 ? status : IO_ERROR_NOT_SUPPORTED,
			    status != 0 ? errno : 0, "File size failed");
			return;
		}
		size = info.size;
	}
	if (!st_wren_u64_exact(size))
		push_error(vm, IO_ERROR_OUT_OF_RANGE, 0, "File size exceeds the exact Number range");
	else
		st_wren_return_u64(vm, size);
}

static void
fn_file_mtime(WrenVM *vm)
{
	struct st_wren_file *file = checked_file(vm, false, 0);
	struct fs_info info;

	if (file == NULL || !check_cache_owner(vm, &file->owner) || !file_flush(vm, file))
		return;
	errno = 0;
	int status = fs_get_info(&info, file->path);
	if (status != 0)
		push_error(vm, status, errno, "File stat failed");
	else if (!st_wren_i64_exact(info.modified_time))
		push_error(vm, IO_ERROR_OUT_OF_RANGE, 0, "File mtime exceeds the exact Number range");
	else
		st_wren_return_i64(vm, info.modified_time);
}

static void
fn_file_mtime_set(WrenVM *vm)
{
	struct st_wren_file *file;
	int64_t modified;
	struct fs_info info;

	if (!st_wren_number_i64(vm, 1, &modified)) {
		st_wren_abort(vm, "File.mtime: invalid exact integer");
		return;
	}
	file = checked_file(vm, false, FILE_RIGHT_WRITE);
	if (file == NULL || !check_cache_owner(vm, &file->owner) || !file_flush(vm, file))
		return;
	errno = 0;
	int status = fs_set_modified_time(file->path, modified);
	if (status != 0)
		push_error(vm, status, errno, "File mtime update failed");
	else {
		/* Native filesystems may ignore sentinel values or round timestamps.
		 * Do not claim that the requested integer was stored in that case. */
		errno = 0;
		status = fs_get_info(&info, file->path);
		if (status != 0)
			push_error(vm, status, errno, "File timestamp verification failed");
		else if (info.modified_time != modified)
			push_error(vm, IO_ERROR_OUT_OF_RANGE, 0, "Native filesystem cannot represent that timestamp");
		else
			wrenSetSlotDouble(vm, 0, (double)modified);
	}
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
	    || (file->rights & FILE_RIGHT_READ) == 0
	    || !cache_owner_valid(vm, &file->owner))
		return NULL;
	return file->path;
}

/* The configured root is trusted; links below it must not widen the grant.
 * Work on the normalized absolute path that will actually reach the worker. */
static bool
upload_below_root(char *path, const char *root)
{
	char *absolute = root == NULL || root[0] == '\0' ? NULL : absolute_path(root);
	struct fs_info info;
	bool allowed = false;

	if (absolute == NULL || fs_get_info(&info, absolute) != 0
	    || info.kind != FS_ENTRY_DIRECTORY)
		goto finished;
	size_t length = strlen(path), prefix = strlen(absolute);
	if (length <= prefix || strncmp(path, absolute, prefix) != 0)
		goto finished;
	if (!path_is_separator(absolute[prefix - 1])) {
		if (!path_is_separator(path[prefix]))
			goto finished;
		++prefix;
	}
	for (size_t end = prefix; end <= length; ++end) {
		if (end != length && !path_is_separator(path[end]))
			continue;
		char saved = path[end];
		path[end] = '\0';
		int status = fs_get_link_info(&info, path);
		path[end] = saved;
		if (status != 0 || info.kind != (end == length
		    ? FS_ENTRY_FILE : FS_ENTRY_DIRECTORY))
			goto finished;
	}
	allowed = true;
finished:
	free(absolute);
	return allowed;
}

char *
st_wren_file_upload_path(WrenVM *vm, int slot)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	const char *source;
	char *path, *cache = NULL;
	struct fs_info info;
	bool granted = wrenGetSlotType(vm, slot) != WREN_TYPE_STRING;

	if (granted) {
		source = st_wren_file_read_path(vm, slot);
		if (source == NULL) {
			st_wren_abort(vm, "Upload source requires a readable File");
			return NULL;
		}
	} else {
		int length;
		if (!st_wren_text_slot(vm, slot, &source, &length))
			return NULL;
		if (length <= 0 || memchr(source, '\0', (size_t)length) != NULL) {
			st_wren_abort(vm, "Upload path must be nonempty text without NUL");
			return NULL;
		}
	}
	path = absolute_path(source);
	if (path == NULL) {
		st_wren_abort(vm, "Upload source path could not be resolved");
		return NULL;
	}
	if (granted) {
		if (fs_get_info(&info, path) == 0 && info.kind == FS_ENTRY_FILE)
			return path;
	} else if (context->connected != NULL && context->profile != NULL) {
		if (upload_below_root(path, context->profile->upload_directory))
			return path;
		if (context->host->bbs_list != NULL
		    && st_bbs_list_cache_path(&cache, context->host->bbs_list,
		    context->profile->name) == 0) {
			bool allowed = upload_below_root(path, cache);
			free(cache);
			if (allowed)
				return path;
		}
	}
	free(path);
	st_wren_abort(vm, granted ? "Upload source is not a regular file"
	    : "Upload path is outside the session cache or configured upload directory, or traverses a link");
	return NULL;
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
	errno = 0;
	int status = fs_open(stream, file->path, mode);
	if (status != 0) {
		(void)snprintf(message, sizeof(message), "%s open failed",
		    operation);
		push_error(vm, status, errno, message);
		return false;
	}
	file->consumed = true;
	return true;
}

void
st_wren_file_write_error(WrenVM *vm, int native_error,
	const char *message)
{
	push_errno_error(vm, native_error, message);
}

const char *
st_wren_directory_path(WrenVM *vm, int slot)
{
	struct st_wren_directory *directory;

	if (!st_wren_foreign_is(vm, slot, "syncterm", "Directory"))
		return NULL;
	directory = wrenGetSlotForeign(vm, slot);
	if (directory == NULL || directory->magic != ST_WREN_DIRECTORY_MAGIC
	    || directory->context != st_wren_context_from_vm(vm)
	    || !cache_owner_valid(vm, &directory->owner))
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
file_digest(WrenVM *vm, enum st_digest_algorithm algorithm)
{
	struct st_wren_file *file = checked_file(vm, false, FILE_RIGHT_READ);
	file_map_t *mapping = NULL;
	uint8_t digest[64];
	size_t size = st_digest_size(algorithm);

	if (file == NULL || !check_cache_owner(vm, &file->owner))
		return;
	if (!file_flush(vm, file))
		return;
	errno = 0;
	int status = file_map_open(&mapping, file->path);
	if (status != 0)
		push_error(vm, status, errno, "File mapping failed");
	else if (size == 0 || st_digest(algorithm, file_map_data(mapping),
	    file_map_size(mapping), digest, size) != ST_CRYPTO_OK)
		push_error(vm, IO_ERROR_IO, 0, "File digest failed");
	else
		wrenSetSlotBytes(vm, 0, (const char *)digest, size);
	file_map_close(mapping);
}

static void fn_file_sha1(WrenVM *vm) { file_digest(vm, ST_DIGEST_SHA1); }
static void fn_file_md5(WrenVM *vm) { file_digest(vm, ST_DIGEST_MD5); }

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

	if (error->native_error != 0)
		(void)snprintf(value, sizeof(value), "FileError %d: %s (errno=%d)", error->code,
		    error->message == NULL ? "I/O failure" : error->message, error->native_error);
	else
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
	    || !push_directory(vm, 0, path, false, NULL))
		wrenSetSlotNull(vm, 0);
	free(path);
}

void
st_wren_entry_cache_push(WrenVM *vm, const struct st_wren_bbs_record *record,
	uint64_t generation, uint64_t identity)
{
	if (record->source != ST_BBS_SOURCE_PERSONAL || record->new_entry
	    || record->persisted_name[0] == '\0') { wrenSetSlotNull(vm, 0); return; }
	struct entry_cache_owner owner = {.generation = generation, .identity = identity};
	(void)snprintf(owner.name, sizeof(owner.name), "%s", record->persisted_name);
	char *path = NULL;
	int status = st_bbs_list_cache_path(&path, st_wren_host_from_vm(vm)->bbs_list, owner.name);
	if (status == 0) status = fs_create_directories(path);
	if (status != 0) push_error(vm, status, 0, "Entry cache unavailable");
	else if (!push_directory(vm, 0, path, false, &owner))
		push_error(vm, IO_ERROR_OUT_OF_MEMORY, 0, "Entry cache allocation failed");
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
	    || !push_directory(vm, 0, path, true, NULL))
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
	if (st_consent_token_verify(&path, key, token) != ST_CRYPTO_OK
	    || !push_file_full(vm, 0, path, FILE_RIGHT_READ,
	    ST_WREN_PICKER_DISPOSITION_READ, token, NULL))
		wrenSetSlotNull(vm, 0);

finished:
	st_crypto_clear(key, sizeof(key));
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
