/* Isolated Wren file-picker host and request bindings. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"

#include "filesystem.h"
#include "io_error.h"
#include "path.h"
#include "st_app_paths.h"
#include "st_console.h"
#include "string_compat.h"
#include "value_convert.h"

#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct foreign_request {
	uint64_t generation;
};

struct binding {
	const char *module;
	const char *class_name;
	bool is_static;
	const char *signature;
	WrenForeignMethodFn method;
};

static struct st_wren_picker_call *
request_call(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct foreign_request *request;

	if (wrenGetSlotType(vm, 0) != WREN_TYPE_FOREIGN) {
		st_wren_abort(vm, "PickerRequest: invalid receiver");
		return NULL;
	}
	request = wrenGetSlotForeign(vm, 0);
	if (request == NULL || host->picker_call == NULL
	    || !host->picker_call->active
	    || request->generation != host->picker_call->generation) {
		st_wren_abort(vm, "PickerRequest: picker call has ended");
		return NULL;
	}
	return host->picker_call;
}

static bool
slot_string(WrenVM *vm, int slot, const char **result, size_t *length,
	const char *operation)
{
	int count;
	const char *value;
	char message[128];

	if (wrenGetSlotType(vm, slot) != WREN_TYPE_STRING) {
		(void)snprintf(message, sizeof(message),
		    "%s: expected a path String", operation);
		st_wren_abort(vm, message);
		return false;
	}
	value = wrenGetSlotBytes(vm, slot, &count);
	if (count < 0 || memchr(value, '\0', (size_t)count) != NULL) {
		(void)snprintf(message, sizeof(message),
		    "%s: path contains NUL", operation);
		st_wren_abort(vm, message);
		return false;
	}
	*result = value;
	*length = (size_t)count;
	return true;
}

static char *
copy_slot_string(WrenVM *vm, int slot, const char *operation)
{
	const char *value;
	size_t length;
	char *copy;

	if (!slot_string(vm, slot, &value, &length, operation))
		return NULL;
	copy = malloc(length + 1);
	if (copy == NULL) {
		st_wren_abort(vm, "out of memory");
		return NULL;
	}
	memcpy(copy, value, length);
	copy[length] = '\0';
	return copy;
}

static char *
path_output_alloc(ptrdiff_t (*operation)(char *, size_t, const char *),
	const char *source)
{
	ptrdiff_t required = operation(NULL, 0, source);
	char *result;

	if (required < 0 || (uintmax_t)required > SIZE_MAX - 1)
		return NULL;
	result = malloc((size_t)required + 1);
	if (result == NULL)
		return NULL;
	if (operation(result, (size_t)required + 1, source) != required) {
		free(result);
		return NULL;
	}
	return result;
}

static char *
absolute_path(const char *path)
{
	return path_output_alloc(path_make_absolute, path);
}

static char *
directory_path(const char *path)
{
	char *absolute = absolute_path(path == NULL || path[0] == '\0' ? "." :
	    path);
	char *directory;

	if (absolute == NULL)
		return NULL;
	directory = path_output_alloc(path_append_separator, absolute);
	free(absolute);
	return directory;
}

static char *
parent_path(const char *path)
{
	ptrdiff_t required = path_parent(NULL, 0, path);
	char *parent;

	if (required < 0 || (uintmax_t)required > SIZE_MAX - 1)
		return NULL;
	parent = malloc((size_t)required + 1);
	if (parent == NULL)
		return NULL;
	if (path_parent(parent, (size_t)required + 1, path) != required) {
		free(parent);
		return NULL;
	}
	return parent;
}

static bool
directory_root(const char *directory)
{
	char *parent = parent_path(directory);
	char *normalized;
	bool root;

	if (parent == NULL)
		return false;
	normalized = directory_path(parent);
	free(parent);
	if (normalized == NULL)
		return false;
	root = strcmp(normalized, directory) == 0;
	free(normalized);
	return root;
}

static unsigned char
ascii_fold(unsigned char value)
{
	return value >= 'A' && value <= 'Z' ?
	    (unsigned char)(value + ('a' - 'A')) : value;
}

static int
ascii_compare(const char *left, const char *right, bool case_sensitive)
{
	const unsigned char *a = (const unsigned char *)left;
	const unsigned char *b = (const unsigned char *)right;

	while (*a != '\0' && *b != '\0') {
		unsigned char ca = case_sensitive ? *a : ascii_fold(*a);
		unsigned char cb = case_sensitive ? *b : ascii_fold(*b);

		if (ca != cb)
			return ca < cb ? -1 : 1;
		++a;
		++b;
	}
	if (*a != *b)
		return *a == '\0' ? -1 : 1;
	return case_sensitive ? 0 : strcmp(left, right);
}

static int
compare_entries_case(const void *left, const void *right)
{
	const struct st_wren_directory_entry *a = left;
	const struct st_wren_directory_entry *b = right;

	return ascii_compare(a->name, b->name, true);
}

static int
compare_entries_folded(const void *left, const void *right)
{
	const struct st_wren_directory_entry *a = left;
	const struct st_wren_directory_entry *b = right;

	return ascii_compare(a->name, b->name, false);
}

static bool
wildcard_match(const char *pattern, const char *text, bool case_sensitive)
{
	const unsigned char *p = (const unsigned char *)pattern;
	const unsigned char *s = (const unsigned char *)text;
	const unsigned char *star = NULL;
	const unsigned char *retry = NULL;

	while (*s != '\0') {
		if (*p == '*') {
			star = ++p;
			retry = s;
			continue;
		}
		if (*p == '?' || (case_sensitive ? *p == *s :
		    ascii_fold(*p) == ascii_fold(*s))) {
			++p;
			++s;
			continue;
		}
		if (star == NULL)
			return false;
		p = star;
		s = ++retry;
	}
	while (*p == '*')
		++p;
	return *p == '\0';
}

static int
move_entry(struct st_wren_directory_entries *destination,
	struct st_wren_directory_entry *entry)
{
	struct st_wren_directory_entry *items;
	size_t capacity;

	if (destination->count == destination->capacity) {
		capacity = destination->capacity == 0 ? 16 :
		    destination->capacity * 2;
		if (capacity < destination->capacity
		    || capacity > SIZE_MAX / sizeof(destination->items[0]))
			return IO_ERROR_OUT_OF_MEMORY;
		items = realloc(destination->items,
		    capacity * sizeof(destination->items[0]));
		if (items == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		destination->items = items;
		destination->capacity = capacity;
	}
	destination->items[destination->count++] = *entry;
	entry->name = NULL;
	return 0;
}

static void
format_timestamp(char result[24], int64_t modified)
{
	time_t converted = (time_t)modified;

	result[0] = '\0';
	if ((int64_t)converted != modified)
		return;
	(void)value_format_datetime(result, 24, converted, true);
}

static void
push_entry_list(WrenVM *vm, int slot,
	const struct st_wren_directory_entries *entries)
{
	wrenEnsureSlots(vm, slot + 3);
	wrenSetSlotNewList(vm, slot);
	for (size_t i = 0; i < entries->count; ++i) {
		const struct st_wren_directory_entry *entry = &entries->items[i];
		char timestamp[24];

		format_timestamp(timestamp, entry->modified);
		wrenSetSlotNewList(vm, slot + 1);
		wrenSetSlotString(vm, slot + 2, entry->name);
		wrenInsertInList(vm, slot + 1, -1, slot + 2);
		wrenSetSlotBool(vm, slot + 2, entry->directory);
		wrenInsertInList(vm, slot + 1, -1, slot + 2);
		wrenSetSlotDouble(vm, slot + 2, (double)entry->size);
		wrenInsertInList(vm, slot + 1, -1, slot + 2);
		wrenSetSlotDouble(vm, slot + 2, (double)entry->modified);
		wrenInsertInList(vm, slot + 1, -1, slot + 2);
		wrenSetSlotString(vm, slot + 2, timestamp);
		wrenInsertInList(vm, slot + 1, -1, slot + 2);
		wrenInsertInList(vm, slot, -1, slot + 1);
	}
}

static void
request_allocate(WrenVM *vm)
{
	struct foreign_request *request = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*request));

	request->generation = 0;
}

static void
request_finalize(void *data)
{
	struct foreign_request *request = data;

	request->generation = 0;
}

static bool
push_request(struct st_wren_host *host, int slot,
	struct st_wren_picker_call *call)
{
	struct foreign_request *request;

	host->picker_call = call;
	call->generation = ++host->picker_generation;
	call->active = true;
	wrenEnsureSlots(host->picker_vm, slot + 2);
	wrenGetVariable(host->picker_vm, "syncterm_picker", "PickerRequest",
	    slot + 1);
	request = wrenSetSlotNewForeign(host->picker_vm, slot, slot + 1,
	    sizeof(*request));
	if (request == NULL)
		return false;
	request->generation = call->generation;
	return true;
}

static void
fn_request_mode(WrenVM *vm)
{
	static const char *const names[] = {
		"file", "directory", "multiple", "save",
	};
	struct st_wren_picker_call *call = request_call(vm);

	if (call != NULL)
		wrenSetSlotString(vm, 0, names[call->mode]);
}

#define REQUEST_STRING_GETTER(name, field) \
	static void fn_request_##name(WrenVM *vm) \
	{ \
		struct st_wren_picker_call *call = request_call(vm); \
		if (call != NULL) \
			wrenSetSlotString(vm, 0, call->field == NULL ? "" : \
			    call->field); \
	}

REQUEST_STRING_GETTER(title, title)
REQUEST_STRING_GETTER(initial_path, initial_path)
REQUEST_STRING_GETTER(mask, mask)

static void
fn_request_options(WrenVM *vm)
{
	struct st_wren_picker_call *call = request_call(vm);

	if (call != NULL)
		wrenSetSlotDouble(vm, 0, call->options);
}

static void
fn_request_initial(WrenVM *vm)
{
	struct st_wren_picker_call *call = request_call(vm);
	const char *initial;
	char *absolute;
	char *directory;
	char *selected = NULL;
	struct fs_info info;

	if (call == NULL)
		return;
	initial = call->initial_path == NULL || call->initial_path[0] == '\0' ?
	    "." : call->initial_path;
	absolute = absolute_path(initial);
	if (absolute == NULL) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	if (fs_get_info(&info, absolute) == 0 && info.kind == FS_ENTRY_FILE) {
		selected = strdup(path_basename(absolute));
		directory = parent_path(absolute);
	}
	else {
		directory = strdup(absolute);
		selected = strdup("");
	}
	free(absolute);
	if (directory == NULL || selected == NULL) {
		free(directory);
		free(selected);
		wrenSetSlotNull(vm, 0);
		return;
	}
	char *normalized = directory_path(directory);
	free(directory);
	if (normalized == NULL) {
		free(selected);
		wrenSetSlotNull(vm, 0);
		return;
	}
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	wrenSetSlotString(vm, 1, normalized);
	wrenInsertInList(vm, 0, -1, 1);
	wrenSetSlotString(vm, 1, selected);
	wrenInsertInList(vm, 0, -1, 1);
	free(normalized);
	free(selected);
}

static void
fn_request_list(WrenVM *vm)
{
	struct st_wren_picker_call *call = request_call(vm);
	char *input;
	char *mask;
	char *directory;
	struct st_wren_directory_entries raw = {0};
	struct st_wren_directory_entries directories = {0};
	struct st_wren_directory_entries files = {0};
	bool root;
	int status;

	if (call == NULL)
		return;
	input = copy_slot_string(vm, 1, "PickerRequest.list");
	if (input == NULL)
		return;
	mask = copy_slot_string(vm, 2, "PickerRequest.list");
	if (mask == NULL) {
		free(input);
		return;
	}
	if (mask[0] == '\0') {
		free(mask);
		mask = strdup("*");
		if (mask == NULL) {
			free(input);
			st_wren_abort(vm, "out of memory");
			return;
		}
	}
	directory = directory_path(input);
	free(input);
	if (directory == NULL) {
		free(mask);
		wrenSetSlotNull(vm, 0);
		return;
	}
	root = directory_root(directory);
	status = st_wren_directory_enumerate(directory, &raw);
	if (status == 0 && !root) {
		struct st_wren_directory_entry parent = {
			.name = strdup(".."), .directory = true,
		};

		if (parent.name == NULL)
			status = IO_ERROR_OUT_OF_MEMORY;
		else
			status = move_entry(&directories, &parent);
		free(parent.name);
	}
	for (size_t i = 0; status == 0 && i < raw.count; ++i) {
		struct st_wren_directory_entry *entry = &raw.items[i];
		bool hidden = entry->name[0] == '.';

		if (hidden && !(call->options & ST_WREN_PICKER_SHOW_HIDDEN))
			continue;
		if (entry->directory)
			status = move_entry(&directories, entry);
		else if (wildcard_match(mask, entry->name,
		    (call->options & ST_WREN_PICKER_MASK_CASE) != 0))
			status = move_entry(&files, entry);
	}
	free(mask);
	st_wren_directory_entries_dispose(&raw);
	if (status != 0) {
		free(directory);
		st_wren_directory_entries_dispose(&directories);
		st_wren_directory_entries_dispose(&files);
		wrenSetSlotNull(vm, 0);
		return;
	}
	if (directories.count > (root ? 1U : 2U))
		qsort(directories.items + (root ? 0 : 1),
		    directories.count - (root ? 0 : 1),
		    sizeof(directories.items[0]),
		    (call->options & ST_WREN_PICKER_CASE_SORT) != 0 ?
		    compare_entries_case : compare_entries_folded);
	if (files.count > 1)
		qsort(files.items, files.count, sizeof(files.items[0]),
		    (call->options & ST_WREN_PICKER_CASE_SORT) != 0 ?
		    compare_entries_case : compare_entries_folded);
	wrenEnsureSlots(vm, 4);
	wrenSetSlotNewList(vm, 0);
	wrenSetSlotString(vm, 1, directory);
	wrenInsertInList(vm, 0, -1, 1);
	wrenSetSlotBool(vm, 1, root);
	wrenInsertInList(vm, 0, -1, 1);
	push_entry_list(vm, 1, &directories);
	wrenInsertInList(vm, 0, -1, 1);
	push_entry_list(vm, 1, &files);
	wrenInsertInList(vm, 0, -1, 1);
	free(directory);
	st_wren_directory_entries_dispose(&directories);
	st_wren_directory_entries_dispose(&files);
}

static void
fn_request_join(WrenVM *vm)
{
	char *base;
	char *name;
	char *joined = NULL;
	char *absolute;

	if (request_call(vm) == NULL)
		return;
	base = copy_slot_string(vm, 1, "PickerRequest.join");
	if (base == NULL)
		return;
	name = copy_slot_string(vm, 2, "PickerRequest.join");
	if (name == NULL) {
		free(base);
		return;
	}
	if (path_join_alloc(&joined, base, name) != 0)
		joined = NULL;
	free(base);
	free(name);
	absolute = joined == NULL ? NULL : absolute_path(joined);
	free(joined);
	if (absolute == NULL)
		wrenSetSlotNull(vm, 0);
	else {
		wrenSetSlotString(vm, 0, absolute);
		free(absolute);
	}
}

static void
fn_request_resolve(WrenVM *vm)
{
	char *base;
	char *text;
	char *joined = NULL;
	char *absolute;
	char *parent;
	char *directory;
	const char *name;
	struct fs_info info;
	bool exists;
	bool is_directory;
	bool wildcards;

	if (request_call(vm) == NULL)
		return;
	base = copy_slot_string(vm, 1, "PickerRequest.resolve");
	if (base == NULL)
		return;
	text = copy_slot_string(vm, 2, "PickerRequest.resolve");
	if (text == NULL) {
		free(base);
		return;
	}
	if (path_is_rooted(text))
		absolute = absolute_path(text);
	else {
		if (path_join_alloc(&joined, base, text) != 0)
			joined = NULL;
		absolute = joined == NULL ? NULL : absolute_path(joined);
	}
	free(joined);
	free(base);
	free(text);
	if (absolute == NULL) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	exists = fs_get_info(&info, absolute) == 0;
	is_directory = exists && info.kind == FS_ENTRY_DIRECTORY;
	name = path_basename(absolute);
	wildcards = strchr(name, '*') != NULL || strchr(name, '?') != NULL;
	parent = parent_path(absolute);
	directory = parent == NULL ? NULL : directory_path(parent);
	free(parent);
	if (directory == NULL) {
		free(absolute);
		wrenSetSlotNull(vm, 0);
		return;
	}
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
#define INSERT_VALUE(statement) do { \
	statement; \
	wrenInsertInList(vm, 0, -1, 1); \
} while (0)
	INSERT_VALUE(wrenSetSlotString(vm, 1, absolute));
	INSERT_VALUE(wrenSetSlotString(vm, 1, directory));
	INSERT_VALUE(wrenSetSlotString(vm, 1, name));
	INSERT_VALUE(wrenSetSlotBool(vm, 1, exists));
	INSERT_VALUE(wrenSetSlotBool(vm, 1, is_directory));
	INSERT_VALUE(wrenSetSlotBool(vm, 1, wildcards));
#undef INSERT_VALUE
	free(directory);
	free(absolute);
}

static bool
store_paths(WrenVM *vm, struct st_wren_picker_call *call, int slot,
	bool multiple)
{
	size_t count = 1;
	char **paths;

	if (call->completed) {
		st_wren_abort(vm, "PickerRequest: result is already complete");
		return false;
	}
	if (multiple) {
		if (wrenGetSlotType(vm, slot) != WREN_TYPE_LIST) {
			st_wren_abort(vm,
			    "PickerRequest.acceptAll: expected a List");
			return false;
		}
		count = (size_t)wrenGetListCount(vm, slot);
		if (count == 0) {
			st_wren_abort(vm,
			    "PickerRequest.acceptAll: list is empty");
			return false;
		}
	}
	paths = calloc(count, sizeof(paths[0]));
	if (paths == NULL) {
		st_wren_abort(vm, "PickerRequest: out of memory");
		return false;
	}
	wrenEnsureSlots(vm, slot + 2);
	for (size_t i = 0; i < count; ++i) {
		int source = slot;
		char *input;

		if (multiple) {
			source = slot + 1;
			wrenGetListElement(vm, slot, (int)i, source);
		}
		input = copy_slot_string(vm, source, "PickerRequest.accept");
		if (input != NULL)
			paths[i] = absolute_path(input);
		free(input);
		if (paths[i] == NULL) {
			for (size_t j = 0; j < i; ++j)
				free(paths[j]);
			free(paths);
			if (input != NULL)
				st_wren_abort(vm,
				    "PickerRequest: invalid absolute path");
			return false;
		}
	}
	call->paths = paths;
	call->path_count = count;
	call->completed = true;
	return true;
}

static void
fn_request_accept(WrenVM *vm)
{
	struct st_wren_picker_call *call = request_call(vm);

	if (call == NULL)
		return;
	if (call->mode != ST_WREN_PICKER_FILE
	    && call->mode != ST_WREN_PICKER_DIRECTORY) {
		st_wren_abort(vm,
		    "PickerRequest.accept: picker is not single-select");
		return;
	}
	if (store_paths(vm, call, 1, false))
		call->disposition = ST_WREN_PICKER_DISPOSITION_READ;
	wrenSetSlotNull(vm, 0);
}

static void
fn_request_accept_all(WrenVM *vm)
{
	struct st_wren_picker_call *call = request_call(vm);

	if (call == NULL)
		return;
	if (call->mode != ST_WREN_PICKER_MULTIPLE) {
		st_wren_abort(vm,
		    "PickerRequest.acceptAll: picker is not multi-select");
		return;
	}
	if (store_paths(vm, call, 1, true))
		call->disposition = ST_WREN_PICKER_DISPOSITION_READ;
	wrenSetSlotNull(vm, 0);
}

static void
accept_save(WrenVM *vm, enum st_wren_picker_disposition disposition)
{
	struct st_wren_picker_call *call = request_call(vm);

	if (call == NULL)
		return;
	if (call->mode != ST_WREN_PICKER_SAVE) {
		st_wren_abort(vm,
		    "PickerRequest: completion does not match picker mode");
		return;
	}
	if (store_paths(vm, call, 1, false))
		call->disposition = disposition;
	wrenSetSlotNull(vm, 0);
}

static void fn_request_accept_create(WrenVM *vm)
{
	accept_save(vm, ST_WREN_PICKER_DISPOSITION_CREATE);
}

static void fn_request_accept_overwrite(WrenVM *vm)
{
	accept_save(vm, ST_WREN_PICKER_DISPOSITION_OVERWRITE);
}

static void fn_request_accept_append(WrenVM *vm)
{
	accept_save(vm, ST_WREN_PICKER_DISPOSITION_APPEND);
}

static void
fn_request_cancel(WrenVM *vm)
{
	struct st_wren_picker_call *call = request_call(vm);

	if (call != NULL && !call->completed)
		call->completed = true;
	wrenSetSlotNull(vm, 0);
}

static void
fn_request_quit(WrenVM *vm)
{
	struct st_wren_picker_call *call = request_call(vm);

	if (call != NULL) {
		call->quit_application = true;
		call->completed = true;
	}
	wrenSetSlotNull(vm, 0);
}

static bool
push_file(WrenVM *vm, int slot, const char *path,
	enum st_wren_picker_disposition disposition)
{
	return st_wren_file_push(vm, slot, path, disposition,
	    disposition == ST_WREN_PICKER_DISPOSITION_READ);
}

static bool
slot_integer(WrenVM *vm, int slot, int *result)
{
	double value;

	if (wrenGetSlotType(vm, slot) != WREN_TYPE_NUM)
		return false;
	value = wrenGetSlotDouble(vm, slot);
	if (!isfinite(value) || trunc(value) != value || value < 0
	    || value > INT_MAX)
		return false;
	*result = (int)value;
	return true;
}

static const char *
picker_initial(WrenVM *vm, int slot)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);

	if (wrenGetSlotType(vm, slot) == WREN_TYPE_STRING)
		return wrenGetSlotString(vm, slot);
	const char *directory = st_wren_directory_path(vm, slot);
	if (directory != NULL)
		return directory;
	if (wrenGetSlotType(vm, slot) == WREN_TYPE_NULL)
		return host->config.paths->download_directory;
	st_wren_abort(vm, "Host picker initial path must be a String or null");
	return NULL;
}

static void
host_pick_file(WrenVM *vm, const char *title)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	const char *initial = picker_initial(vm, 1);
	const char *mask;
	int options;
	struct st_wren_picker_call call;

	if (initial == NULL)
		return;
	if (wrenGetSlotType(vm, 2) == WREN_TYPE_NULL)
		mask = "*";
	else if (wrenGetSlotType(vm, 2) == WREN_TYPE_STRING)
		mask = wrenGetSlotString(vm, 2);
	else {
		st_wren_abort(vm, "Host.pickFile: mask must be a String or null");
		return;
	}
	if (!slot_integer(vm, 3, &options)) {
		st_wren_abort(vm, "Host.pickFile: options must be an integer");
		return;
	}
	call = (struct st_wren_picker_call){
		.mode = (options & ST_WREN_PICKER_SELECT_DIRECTORY) != 0 ?
		    ST_WREN_PICKER_DIRECTORY : ST_WREN_PICKER_FILE,
		.title = title,
		.initial_path = initial,
		.mask = mask,
		.options = options,
	};
	if (!st_wren_picker_host_run(host, &call)) {
		st_wren_abort(vm, host->last_error[0] == '\0'
		    ? "The file picker is unavailable." : host->last_error);
		st_wren_picker_call_dispose(&call);
		return;
	}
	if (call.path_count == 0
	    || !push_file(vm, 0, call.paths[0], call.disposition))
		wrenSetSlotNull(vm, 0);
	st_wren_picker_call_dispose(&call);
}

static void fn_host_pick_file(WrenVM *vm)
{
	host_pick_file(vm, "Pick a file");
}

static void
fn_host_pick_file_title(WrenVM *vm)
{
	if (wrenGetSlotType(vm, 4) != WREN_TYPE_STRING) {
		st_wren_abort(vm, "Host.pickFile: title must be a String");
		return;
	}
	host_pick_file(vm, wrenGetSlotString(vm, 4));
}

static void
fn_host_pick_files(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	const char *initial = picker_initial(vm, 1);
	const char *mask;
	int options;
	struct st_wren_picker_call call;

	if (initial == NULL)
		return;
	mask = wrenGetSlotType(vm, 2) == WREN_TYPE_STRING ?
	    wrenGetSlotString(vm, 2) : "*";
	if ((wrenGetSlotType(vm, 2) != WREN_TYPE_STRING
	    && wrenGetSlotType(vm, 2) != WREN_TYPE_NULL)
	    || !slot_integer(vm, 3, &options)) {
		st_wren_abort(vm, "Host.pickFiles: invalid arguments");
		return;
	}
	if (options & (ST_WREN_PICKER_ALLOW_ENTRY
	    | ST_WREN_PICKER_CONFIRM_OVERWRITE
	    | ST_WREN_PICKER_CONFIRM_CREATE | ST_WREN_PICKER_ALLOW_APPEND)) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	call = (struct st_wren_picker_call){
		.mode = ST_WREN_PICKER_MULTIPLE,
		.title = "Tag files to upload",
		.initial_path = initial,
		.mask = mask,
		.options = options,
	};
	if (!st_wren_picker_host_run(host, &call)) {
		st_wren_abort(vm, host->last_error[0] == '\0'
		    ? "The file picker is unavailable." : host->last_error);
		st_wren_picker_call_dispose(&call);
		return;
	}
	if (call.path_count == 0) {
		wrenSetSlotNull(vm, 0);
		st_wren_picker_call_dispose(&call);
		return;
	}
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < call.path_count; ++i) {
		if (push_file(vm, 1, call.paths[i], call.disposition))
			wrenInsertInList(vm, 0, -1, 1);
	}
	st_wren_picker_call_dispose(&call);
}

static void
host_pick_save(WrenVM *vm, int options)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	const char *initial = picker_initial(vm, 1);
	const char *mask;
	struct st_wren_picker_call call;

	if (initial == NULL)
		return;
	mask = wrenGetSlotType(vm, 2) == WREN_TYPE_STRING ?
	    wrenGetSlotString(vm, 2) : "*";
	if (wrenGetSlotType(vm, 2) != WREN_TYPE_STRING
	    && wrenGetSlotType(vm, 2) != WREN_TYPE_NULL) {
		st_wren_abort(vm, "Host.pickSavePath: invalid mask");
		return;
	}
	call = (struct st_wren_picker_call){
		.mode = ST_WREN_PICKER_SAVE,
		.title = "Save as",
		.initial_path = initial,
		.mask = mask,
		.options = ST_WREN_PICKER_ALLOW_ENTRY
		    | ST_WREN_PICKER_CONFIRM_OVERWRITE | options,
	};
	if (!st_wren_picker_host_run(host, &call)) {
		st_wren_abort(vm, host->last_error[0] == '\0'
		    ? "The file picker is unavailable." : host->last_error);
		st_wren_picker_call_dispose(&call);
		return;
	}
	if (call.path_count == 0
	    || !push_file(vm, 0, call.paths[0], call.disposition))
		wrenSetSlotNull(vm, 0);
	st_wren_picker_call_dispose(&call);
}

static void fn_host_pick_save(WrenVM *vm)
{
	host_pick_save(vm, 0);
}

static void
fn_host_pick_save_options(WrenVM *vm)
{
	int options;

	if (!slot_integer(vm, 3, &options)) {
		st_wren_abort(vm,
		    "Host.pickSavePath: options must be an integer");
		return;
	}
	host_pick_save(vm, options & ST_WREN_PICKER_ALLOW_APPEND);
}

static const struct binding bindings[] = {
	{"syncterm_picker", "PickerRequest", false, "mode", fn_request_mode},
	{"syncterm_picker", "PickerRequest", false, "title", fn_request_title},
	{"syncterm_picker", "PickerRequest", false, "initialPath", fn_request_initial_path},
	{"syncterm_picker", "PickerRequest", false, "mask", fn_request_mask},
	{"syncterm_picker", "PickerRequest", false, "options", fn_request_options},
	{"syncterm_picker", "PickerRequest", false, "initial_()", fn_request_initial},
	{"syncterm_picker", "PickerRequest", false, "list_(_,_)", fn_request_list},
	{"syncterm_picker", "PickerRequest", false, "join_(_,_)", fn_request_join},
	{"syncterm_picker", "PickerRequest", false, "resolve_(_,_)", fn_request_resolve},
	{"syncterm_picker", "PickerRequest", false, "accept(_)", fn_request_accept},
	{"syncterm_picker", "PickerRequest", false, "acceptAll(_)", fn_request_accept_all},
	{"syncterm_picker", "PickerRequest", false, "acceptCreate(_)", fn_request_accept_create},
	{"syncterm_picker", "PickerRequest", false, "acceptOverwrite(_)", fn_request_accept_overwrite},
	{"syncterm_picker", "PickerRequest", false, "acceptAppend(_)", fn_request_accept_append},
	{"syncterm_picker", "PickerRequest", false, "cancel()", fn_request_cancel},
	{"syncterm_picker", "PickerRequest", false, "quitApplication()", fn_request_quit},
	{"syncterm", "Host", true, "pickFile(_,_,_)", fn_host_pick_file},
	{"syncterm", "Host", true, "pickFile(_,_,_,_)", fn_host_pick_file_title},
	{"syncterm", "Host", true, "pickFiles(_,_,_)", fn_host_pick_files},
	{"syncterm", "Host", true, "pickSavePath(_,_)", fn_host_pick_save},
	{"syncterm", "Host", true, "pickSavePath(_,_,_)", fn_host_pick_save_options},
};

WrenForeignMethodFn
st_wren_picker_bind_method(const char *module, const char *class_name,
	bool is_static, const char *signature)
{
	for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i) {
		if (bindings[i].is_static == is_static
		    && strcmp(bindings[i].module, module) == 0
		    && strcmp(bindings[i].class_name, class_name) == 0
		    && strcmp(bindings[i].signature, signature) == 0)
			return bindings[i].method;
	}
	return NULL;
}

WrenForeignClassMethods
st_wren_picker_bind_class(const char *module, const char *class_name)
{
	WrenForeignClassMethods methods = {NULL, NULL};

	if (strcmp(module, "syncterm_picker") == 0
	    && strcmp(class_name, "PickerRequest") == 0) {
		methods.allocate = request_allocate;
		methods.finalize = request_finalize;
	}
	return methods;
}

void
st_wren_picker_call_dispose(struct st_wren_picker_call *call)
{
	if (call == NULL)
		return;
	call->active = false;
	for (size_t i = 0; i < call->path_count; ++i)
		free(call->paths[i]);
	free(call->paths);
	call->paths = NULL;
	call->path_count = 0;
}

bool
st_wren_picker_host_run(struct st_wren_host *host,
	struct st_wren_picker_call *call)
{
	st_console_snapshot_t *snapshot = NULL;
	st_console_t *previous_console;
	bool switched = false;
	int restore_status;
	WrenInterpretResult result;

	if (host == NULL || call == NULL || host->picker_vm == NULL
	    || host->picker_bootstrap == NULL || host->picker_run == NULL
	    || host->picker_recover == NULL)
		return false;
	host->last_error[0] = '\0';
	previous_console = host->menu.console;
	if (host->config.display != NULL && host->config.picker_console != NULL
	    && host->config.picker_console != previous_console) {
		if (st_console_display_activate(host->config.display,
		    host->config.picker_console) != 0)
			return false;
		switched = true;
	}
	else if (st_console_snapshot_create(&snapshot,
	    host->config.console) != 0)
		return false;
	if (host->picker_class == NULL)
		result = WREN_RESULT_RUNTIME_ERROR;
	else {
		wrenEnsureSlots(host->picker_vm, 3);
		wrenSetSlotHandle(host->picker_vm, 0, host->picker_bootstrap);
		wrenSetSlotHandle(host->picker_vm, 1, host->picker_class);
		if (!push_request(host, 2, call))
			result = WREN_RESULT_RUNTIME_ERROR;
		else
			result = wrenCall(host->picker_vm, host->picker_run);
	}
	if (result != WREN_RESULT_SUCCESS) {
		wrenEnsureSlots(host->picker_vm, 3);
		wrenSetSlotHandle(host->picker_vm, 0, host->picker_bootstrap);
		if (!push_request(host, 1, call))
			result = WREN_RESULT_RUNTIME_ERROR;
		else {
			wrenSetSlotString(host->picker_vm, 2,
			    host->last_error[0] == '\0' ?
			    "The file picker is unavailable." : host->last_error);
			result = wrenCall(host->picker_vm, host->picker_recover);
		}
	}
	call->active = false;
	host->picker_call = NULL;
	if (!call->completed)
		call->completed = true;
	if (call->quit_application)
		host->exit_requested = true;
	if (switched) {
		restore_status = st_console_display_activate(host->config.display,
		    previous_console);
	}
	else
		restore_status = st_console_snapshot_restore(host->config.console, snapshot);
	st_console_snapshot_destroy(snapshot);
	if (restore_status != 0) {
		(void)snprintf(host->last_error, sizeof(host->last_error),
		    "Cannot restore the menu console after the file picker (%d)",
		    restore_status);
		return false;
	}
	return result == WREN_RESULT_SUCCESS;
}
