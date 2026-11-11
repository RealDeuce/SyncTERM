/* Wren bindings for the persistent BBS-directory menu model. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"
#include "path.h"
#include "wren_licenses.h"

#include "audio.h"
#include "filesystem.h"
#include "io_error.h"
#include "st_app_paths.h"
#include "st_amiga_fonts.h"
#include "st_audio_runtime.h"
#include "st_music.h"
#include "st_audio_file.h"
#include "st_bbs_list.h"
#include "st_builtin_fonts.h"
#include "st_console.h"
#include "st_pixel_image.h"
#include "serial.h"
#include "st_settings.h"
#include "st_session_history.h"
#include "st_theme.h"
#include "st_unicode_compare.h"
#include "st_unicode_text.h"
#include "string_util.h"
#include "value_convert.h"
#include "value_convert_time.h"
#include "st_crypto.h"
#include "st_kdf.h"

#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum menu_foreign_kind {
	MENU_FOREIGN_GENERIC = 0x100,
	MENU_FOREIGN_BBS,
	MENU_FOREIGN_SETTINGS,
	MENU_FOREIGN_THEME_DOCUMENT,
	MENU_FOREIGN_FONT,
	MENU_FOREIGN_AMIGA_FONT,
	MENU_FOREIGN_ENCRYPTION_INFO,
	MENU_FOREIGN_ERROR,
	MENU_FOREIGN_LOAD_DIAGNOSTIC,
	MENU_FOREIGN_CLOUD_THEME_ENTRY,
};

struct menu_foreign_header {
	enum menu_foreign_kind kind;
};

struct foreign_bbs {
	struct menu_foreign_header header;
	struct st_wren_bbs_record *record;
	uint64_t generation, identity;
	struct st_wren_context *context;
	bool owned, read_only;
	enum st_wren_bbs_kind kind;
};

struct foreign_settings {
	struct menu_foreign_header header;
	struct st_settings settings;
	uint64_t generation;
	bool dirty;
};

struct foreign_theme_document {
	struct menu_foreign_header header;
	struct st_theme_document *document;
};

struct foreign_font {
	struct menu_foreign_header header;
	size_t index;
	uint64_t generation;
};

struct foreign_amiga_font {
	struct menu_foreign_header header;
	struct st_amiga_font_family_info info;
	uint64_t generation;
};

struct foreign_encryption_info {
	struct menu_foreign_header header;
	struct st_list_crypto_info info;
};

struct foreign_menu_error {
	struct menu_foreign_header header;
	int code;
	int native_code;
	char *message;
};

struct foreign_load_diagnostic {
	struct menu_foreign_header header;
	enum st_bbs_source source;
	int status;
	char *source_name;
	char *section_name;
};

struct foreign_cloud_theme_entry {
	struct menu_foreign_header header;
	struct st_wren_context *context;
	uint64_t generation;
	size_t index;
};

struct binding {
	const char *class_name;
	bool is_static;
	const char *signature;
	WrenForeignMethodFn method;
};

static const char *
menu_io_reason(int code)
{
	static const char *const reasons[] = {
		"Invalid argument", "Invalid encoding", "Not found",
		"Permission denied", "Already exists", "Not a directory",
		"Is a directory", "Directory not empty", "Out of memory",
		"No space available", "Name too long", "Out of range",
		"Busy", "Read-only", "Cross-device operation",
		"Too many links", "Interrupted", "Not supported",
		"I/O failure", "Timed out", "Cancelled",
		"Would block", "In progress", "Name resolution failed",
		"Connection refused", "Connection reset", "Connection aborted",
		"Not connected", "Network unreachable", "Host unreachable",
		"Address in use", "Address not available", "Message too large",
		"Protocol failure", "Password required", "Decryption failed",
		"Invalid format", "Conflict with changed data",
	};
	return code <= -1 && code >= -38 ? reasons[-code - 1] : "Native I/O failure";
}

/* A zero code denotes an unavailable reason, not successful construction. */
static void
menu_error(WrenVM *vm, int code, int native_code, const char *message)
{
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm_menu", "MenuError", 1);
	struct foreign_menu_error *error = wrenSetSlotNewForeign(vm, 0, 1,
	    sizeof(*error));
	*error = (struct foreign_menu_error){
		.header.kind = MENU_FOREIGN_ERROR, .code = code,
		.native_code = native_code,
	};
	if (code == 0)
		error->message = strdup(message);
	else {
		const char *reason = menu_io_reason(code);
		size_t size = strlen(message) + strlen(reason) + sizeof(" ()");
		error->message = malloc(size);
		if (error->message != NULL)
			(void)snprintf(error->message, size, "%s (%s)", message, reason);
	}
	if (error->message == NULL)
		st_wren_abort(vm, "out of memory copying menu error");
}

/* Classify by the called API's contract, never by the integer's value. */
static void
menu_result(WrenVM *vm, int status, bool portable, const char *message)
{
	if (status == 0)
		wrenSetSlotNull(vm, 0);
	else
		menu_error(vm, portable ? status : 0, status, message);
}

static bool
menu_writable(WrenVM *vm, const char *message)
{
	if (!st_wren_host_from_vm(vm)->config.safe_mode)
		return true;
	menu_error(vm, IO_ERROR_READ_ONLY, 0, message);
	return false;
}

static void fn_menu_error_code(WrenVM *vm)
{
	int code = ((struct foreign_menu_error *)wrenGetSlotForeign(vm, 0))->code;
	if (code == 0) wrenSetSlotNull(vm, 0);
	else wrenSetSlotDouble(vm, 0, code);
}
static void fn_menu_error_native_code(WrenVM *vm)
{
	int code = ((struct foreign_menu_error *)wrenGetSlotForeign(vm, 0))->native_code;
	if (code == 0) wrenSetSlotNull(vm, 0);
	else wrenSetSlotDouble(vm, 0, code);
}
static void fn_menu_error_message(WrenVM *vm)
{
	struct foreign_menu_error *error = wrenGetSlotForeign(vm, 0);
	wrenSetSlotString(vm, 0, error->message);
}
static void fn_menu_error_string(WrenVM *vm)
{
	struct foreign_menu_error *error = wrenGetSlotForeign(vm, 0);
	size_t size = strlen(error->message) + sizeof("MenuError: ");
	char *text = malloc(size);
	if (text == NULL) {
		st_wren_abort(vm, "out of memory formatting menu error");
		return;
	}
	(void)snprintf(text, size, "MenuError: %s", error->message);
	wrenSetSlotString(vm, 0, text);
	free(text);
}

static bool slot_c_string(WrenVM *vm, int slot, const char **value);
static void fn_menu_sort(WrenVM *vm);
static const char *personal_list_path(const struct st_settings *settings,
	const struct st_app_paths *paths);

static bool
slot_integer(WrenVM *vm, int slot, int64_t minimum, int64_t maximum,
	int64_t *result)
{
	int64_t value;

	if (!st_wren_number_i64(vm, slot, &value)
	    || value < minimum || value > maximum) {
		st_wren_abort(vm, "expected an exact integer in range");
		return false;
	}
	*result = value;
	return true;
}

static void
allocate_generic(WrenVM *vm)
{
	struct menu_foreign_header *header = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*header));

	header->kind = MENU_FOREIGN_GENERIC;
}

static void
finalize_menu_foreign(void *data)
{
	struct menu_foreign_header *header = data;

	if (header == NULL)
		return;
	if (header->kind == MENU_FOREIGN_AMIGA_FONT) {
		struct foreign_amiga_font *font = data;
		st_amiga_font_family_info_dispose(&font->info);
	}
	else if (header->kind == MENU_FOREIGN_ERROR)
		free(((struct foreign_menu_error *)data)->message);
	else if (header->kind == MENU_FOREIGN_LOAD_DIAGNOSTIC) {
		struct foreign_load_diagnostic *item = data;
		free(item->source_name);
		free(item->section_name);
	}
	else if (header->kind == MENU_FOREIGN_SETTINGS)
		st_settings_dispose(&((struct foreign_settings *)data)->settings);
	else if (header->kind == MENU_FOREIGN_BBS && ((struct foreign_bbs *)data)->owned) {
		struct foreign_bbs *bbs = data;
		st_profile_dispose(&bbs->record->profile);
		free(bbs->record);
	}
	else if (header->kind == MENU_FOREIGN_THEME_DOCUMENT)
		st_theme_document_free(
		    &((struct foreign_theme_document *)data)->document);
}

static struct foreign_bbs *
bbs_value(WrenVM *vm, int slot)
{
	struct menu_foreign_header *header;

	if (!st_wren_foreign_is(vm, slot, "syncterm_menu", "DirectoryEntry")
	    && !st_wren_foreign_is(vm, slot, "syncterm", "Profile")) {
		st_wren_abort(vm, "expected a DirectoryEntry or Profile");
		return NULL;
	}
	header = wrenGetSlotForeign(vm, slot);
	if (header == NULL || header->kind != MENU_FOREIGN_BBS) {
		st_wren_abort(vm, "expected a DirectoryEntry or Profile");
		return NULL;
	}
	struct foreign_bbs *bbs = (struct foreign_bbs *)header;
	struct st_wren_host *host = st_wren_host_from_vm(vm);

	if (bbs->context != st_wren_context_from_vm(vm) || (!bbs->owned
	    && !st_wren_bbs_model_valid(&host->bbs_model, bbs->record, bbs->kind,
	    bbs->generation, bbs->identity))) {
		st_wren_abort(vm,
		    "directory entry or profile view is stale");
		return NULL;
	}
	return bbs;
}

static struct st_profile *
bbs_mutable_profile(WrenVM *vm, struct foreign_bbs *bbs)
{
	if (bbs == NULL)
		return NULL;
	if (bbs->read_only || (!bbs->owned && bbs->kind == ST_WREN_BBS_MODEL_ENTRY
	    && bbs->record->source != ST_BBS_SOURCE_PERSONAL)) {
		st_wren_abort(vm, "Profile is read-only");
		return NULL;
	}
	return &bbs->record->profile;
}

static void
bbs_changed(struct foreign_bbs *bbs)
{
	if (!bbs->owned)
		bbs->record->dirty = true;
}

static struct foreign_settings *
settings_value(WrenVM *vm)
{
	struct menu_foreign_header *header;

	if (!st_wren_foreign_is(vm, 0, "syncterm_menu", "Settings")) {
		st_wren_abort(vm, "expected Settings");
		return NULL;
	}
	header = wrenGetSlotForeign(vm, 0);
	if (header == NULL || header->kind != MENU_FOREIGN_SETTINGS) {
		st_wren_abort(vm, "expected Settings");
		return NULL;
	}
	struct foreign_settings *settings = (struct foreign_settings *)header;

	if (settings->generation
	    != st_wren_host_from_vm(vm)->settings_generation) {
		st_wren_abort(vm,
		    "Settings: stale snapshot; reacquire it from Menu.settings");
		return NULL;
	}
	return settings;
}

static struct foreign_theme_document *
theme_document_value(WrenVM *vm)
{
	struct menu_foreign_header *header;

	if (!st_wren_foreign_is(vm, 0, "syncterm_menu", "ThemeDocument")) {
		st_wren_abort(vm, "expected ThemeDocument");
		return NULL;
	}
	header = wrenGetSlotForeign(vm, 0);
	if (header == NULL || header->kind != MENU_FOREIGN_THEME_DOCUMENT) {
		st_wren_abort(vm, "expected ThemeDocument");
		return NULL;
	}
	return (struct foreign_theme_document *)header;
}

static struct st_settings_font *
font_value(WrenVM *vm, struct foreign_font **handle_result)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct menu_foreign_header *header;
	struct foreign_font *handle;

	if (!st_wren_foreign_is(vm, 0, "syncterm_menu", "MenuFont")) {
		st_wren_abort(vm, "MenuFont: invalid receiver");
		return NULL;
	}
	header = wrenGetSlotForeign(vm, 0);
	if (header == NULL || header->kind != MENU_FOREIGN_FONT) {
		st_wren_abort(vm, "MenuFont: invalid receiver");
		return NULL;
	}
	handle = (struct foreign_font *)header;
	if (handle->generation != host->font_generation
	    || handle->index >= st_settings_font_draft_count(&host->font_draft)) {
		st_wren_abort(vm,
		    "MenuFont: stale handle; reacquire it from Menu.fonts");
		return NULL;
	}
	if (handle_result != NULL)
		*handle_result = handle;
	return host->font_draft.fonts[handle->index];
}

static bool
slot_bool(WrenVM *vm, int slot, bool *result)
{
	if (wrenGetSlotType(vm, slot) != WREN_TYPE_BOOL) {
		st_wren_abort(vm, "expected a Bool");
		return false;
	}
	*result = wrenGetSlotBool(vm, slot);
	return true;
}

static bool
slot_fixed_string(WrenVM *vm, int slot, char *destination, size_t size)
{
	const char *value;
	int length;

	if (!st_wren_text_slot(vm, slot, &value, &length))
		return false;
	if (length < 0 || (size_t)length >= size
	    || memchr(value, '\0', (size_t)length) != NULL) {
		st_wren_abort(vm, "String is too long or contains NUL");
		return false;
	}
	memcpy(destination, value, (size_t)length);
	destination[length] = '\0';
	return true;
}

static bool
slot_owned_string(WrenVM *vm, int slot, char **destination)
{
	const char *value;
	int length;
	char *copy = NULL;

	if (!st_wren_text_slot(vm, slot, &value, &length))
		return false;
	if (length < 0 || memchr(value, '\0', (size_t)length) != NULL) {
		st_wren_abort(vm, "String contains NUL");
		return false;
	}
	if (length != 0) {
		copy = malloc((size_t)length + 1);
		if (copy == NULL) {
			st_wren_abort(vm, "out of memory");
			return false;
		}
		memcpy(copy, value, (size_t)length);
		copy[length] = '\0';
	}
	free(*destination);
	*destination = copy;
	return true;
}

static struct foreign_bbs *
push_bbs(WrenVM *vm, int destination, struct st_wren_bbs_record *record,
	enum st_wren_bbs_kind kind)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);

	wrenEnsureSlots(vm, destination + 2);
	wrenGetVariable(vm, kind != ST_WREN_BBS_DEFAULTS ? "syncterm_menu" : "syncterm",
	    kind != ST_WREN_BBS_DEFAULTS ? "DirectoryEntry" : "Profile", destination + 1);
	struct foreign_bbs *bbs = wrenSetSlotNewForeign(vm, destination,
	    destination + 1, sizeof(*bbs));
	memset(bbs, 0, sizeof(*bbs));
	bbs->header.kind = MENU_FOREIGN_BBS;
	bbs->record = record;
	bbs->kind = kind;
	bbs->read_only = kind == ST_WREN_BBS_SHADOWED_ENTRY;
	bbs->generation = host->bbs_model.generation;
	bbs->identity = record->identity;
	bbs->context = st_wren_context_from_vm(vm);
	return bbs;
}

static void
detach_profile(struct st_profile *profile)
{
	profile->identifier = -1;
	profile->source = ST_PROFILE_SOURCE_USER;
	profile->added = profile->connected = profile->calls = 0;
	(void)snprintf(profile->name, sizeof(profile->name), "%.*s",
	    ST_PROFILE_NAME_MAX, profile->address);
}

bool
st_wren_profile_push(WrenVM *vm, int slot, const struct st_profile *profile, bool read_only)
{
	struct st_wren_bbs_record *record = calloc(1, sizeof(*record));
	if (record == NULL) return false;
	st_profile_init(&record->profile);
	if (!st_profile_copy(&record->profile, profile)) {
		st_profile_dispose(&record->profile); free(record); return false;
	}
	if (!read_only) detach_profile(&record->profile);
	wrenEnsureSlots(vm, slot + 2);
	wrenGetVariable(vm, "syncterm", "Profile", slot + 1);
	struct foreign_bbs *value = wrenSetSlotNewForeign(vm, slot, slot + 1, sizeof(*value));
	*value = (struct foreign_bbs){.header = {MENU_FOREIGN_BBS}, .record = record,
	    .context = st_wren_context_from_vm(vm), .owned = true, .read_only = read_only};
	return true;
}

static void
fn_entry_profile(WrenVM *vm)
{
	struct foreign_bbs *entry = bbs_value(vm, 0);
	if (entry == NULL) return;
	struct foreign_bbs copy = *entry;
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm", "Profile", 1);
	struct foreign_bbs *value = wrenSetSlotNewForeign(vm, 0, 1, sizeof(*value));
	*value = copy;
}
static void fn_profile_copy(WrenVM *vm)
{
	struct foreign_bbs *profile = bbs_value(vm, 0);
	if (profile != NULL && !st_wren_profile_push(vm, 0, &profile->record->profile, false))
		st_wren_abort(vm, "out of memory copying profile");
}
static void fn_profile_read_only(WrenVM *vm)
{
	struct foreign_bbs *profile = bbs_value(vm, 0);
	if (profile != NULL) wrenSetSlotBool(vm, 0, profile->read_only
	    || (!profile->owned && profile->kind == ST_WREN_BBS_MODEL_ENTRY
	    && profile->record->source != ST_BBS_SOURCE_PERSONAL));
}

static size_t
sort_fields(const char *order, int fields[32])
{
	size_t count = 0;
	const char *cursor = order;

	while (cursor != NULL && *cursor != '\0' && count < 32) {
		char token[24];
		const char *end = strchr(cursor, ',');
		size_t length = end == NULL ? strlen(cursor) : (size_t)(end - cursor);
		int64_t parsed;
		int normalized;

		if (length == 0 || length >= sizeof(token))
			break;
		memcpy(token, cursor, length);
		token[length] = '\0';
		if (!value_parse_int64(token, &parsed) || parsed < INT_MIN
		    || parsed > INT_MAX
		    || (normalized = st_bbs_sort_field_normalize((int)parsed)) == 0)
			break;
		fields[count++] = normalized;
		if (end == NULL) {
			cursor = NULL;
			break;
		}
		cursor = end + 1;
	}
	if (count == 0 || (cursor != NULL && *cursor != '\0')) {
		fields[0] = ST_BBS_SORT_EXPLICIT;
		fields[1] = ST_BBS_SORT_SOURCE;
		fields[2] = ST_BBS_SORT_NAME;
		count = 3;
	}
	return count;
}

static size_t
configured_sort_fields(int fields[32])
{
	return sort_fields(st_settings.sort_order, fields);
}

bool
st_wren_menu_copy_profile(WrenVM *vm, int slot, struct st_profile *result)
{
	struct menu_foreign_header *header;
	struct foreign_bbs *bbs;
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	bool valid;

	if (!st_wren_foreign_is(vm, slot, "syncterm_menu", "DirectoryEntry")
	    && !st_wren_foreign_is(vm, slot, "syncterm", "Profile"))
		return false;
	header = wrenGetSlotForeign(vm, slot);
	if (header == NULL || header->kind != MENU_FOREIGN_BBS)
		return false;
	bbs = (struct foreign_bbs *)header;
	valid = bbs->context == st_wren_context_from_vm(vm)
	    && (bbs->owned || st_wren_bbs_model_valid(&host->bbs_model,
	    bbs->record, bbs->kind, bbs->generation, bbs->identity))
	    && st_profile_copy(result, &bbs->record->profile);
	if (valid && st_wren_foreign_is(vm, slot, "syncterm", "Profile"))
		detach_profile(result);
	return valid;
}

static void
fn_menu_load(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct st_wren_bbs_model replacement_model;
	st_bbs_list_t *replacement_list = NULL;
	const void *password = NULL;
	size_t password_size = 0;
	int fields[32];
	size_t field_count;
	int length;
	int status;
	const char *message = "The directory could not be loaded.";

	if (wrenGetSlotType(vm, 1) == WREN_TYPE_STRING) {
		password = wrenGetSlotBytes(vm, 1, &length);
		password_size = (size_t)length;
	}
	else if (wrenGetSlotType(vm, 1) != WREN_TYPE_NULL) {
		st_wren_abort(vm, "Menu.load expects a String or null");
		return;
	}
	st_wren_bbs_model_init(&replacement_model);
	status = st_bbs_list_open(&replacement_list, host->config.paths, password,
	    password_size);
	if (status == 0) {
		message = "The loaded directory could not be sorted.";
		field_count = configured_sort_fields(fields);
		status = st_bbs_list_sort(replacement_list, fields, field_count);
	}
	if (status == 0) {
		message = "The loaded directory model could not be constructed.";
		status = st_wren_bbs_model_build(&replacement_model,
		    replacement_list);
	}
	if (status == 0) {
		st_bbs_list_t *old_list = host->bbs_list;

		host->bbs_list = replacement_list;
		replacement_list = NULL;
		st_wren_bbs_model_replace(&host->bbs_model, &replacement_model);
		st_bbs_list_close(old_list);
	}
	st_wren_bbs_model_dispose(&replacement_model);
	st_bbs_list_close(replacement_list);
	menu_result(vm, status, true, message);
}

static int
copy_diagnostic_name(char **result, const char *name)
{
	size_t size;
	int status;

	if (name == NULL)
		return 0;
	status = st_unicode_text_normalize(result, &size, name, strlen(name), NULL);
	if (status == 0 && size > INT_MAX) {
		free(*result);
		*result = NULL;
		return IO_ERROR_OUT_OF_RANGE;
	}
	return status;
}

static void
fn_menu_load_diagnostics(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);

	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	if (!host->bbs_model.loaded)
		return;
	wrenGetVariable(vm, "syncterm_menu", "BBSLoadDiagnostic", 1);
	for (size_t i = 0; i < st_bbs_list_diagnostic_count(host->bbs_list); ++i) {
		const struct st_bbs_diagnostic *native =
		    st_bbs_list_diagnostic_at(host->bbs_list, i);
		struct foreign_load_diagnostic *item = wrenSetSlotNewForeign(vm,
		    2, 1, sizeof(*item));
		*item = (struct foreign_load_diagnostic){
			.header.kind = MENU_FOREIGN_LOAD_DIAGNOSTIC,
			.source = native->source, .status = native->status,
		};
		int status = copy_diagnostic_name(&item->source_name,
		    native->source_name);
		if (status == 0)
			status = copy_diagnostic_name(&item->section_name,
			    native->section_name);
		if (status != 0) {
			menu_error(vm, status, status,
			    "The directory warnings could not be copied.");
			wrenAbortFiber(vm, 0);
			return;
		}
		wrenInsertInList(vm, 0, -1, 2);
	}
}

static void
fn_load_diagnostic_source(WrenVM *vm)
{
	struct foreign_load_diagnostic *item = wrenGetSlotForeign(vm, 0);
	wrenSetSlotDouble(vm, 0, item->source);
}

static void
fn_load_diagnostic_source_name(WrenVM *vm)
{
	struct foreign_load_diagnostic *item = wrenGetSlotForeign(vm, 0);
	if (item->source_name == NULL) wrenSetSlotNull(vm, 0);
	else wrenSetSlotString(vm, 0, item->source_name);
}

static void
fn_load_diagnostic_section_name(WrenVM *vm)
{
	struct foreign_load_diagnostic *item = wrenGetSlotForeign(vm, 0);
	if (item->section_name == NULL) wrenSetSlotNull(vm, 0);
	else wrenSetSlotString(vm, 0, item->section_name);
}

static void
fn_load_diagnostic_error(WrenVM *vm)
{
	struct foreign_load_diagnostic *item = wrenGetSlotForeign(vm, 0);
	menu_error(vm, item->status, item->status,
	    item->status == IO_ERROR_NAME_TOO_LONG && item->section_name != NULL
	    ? "The entry name was truncated to fit the supported name limits."
	    : "A directory source or section was skipped.");
}

static unsigned char
ascii_lower(unsigned char value)
{
	return value >= 'A' && value <= 'Z' ? (unsigned char)(value + 'a' - 'A') :
	    value;
}

static bool
ascii_prefix(const char *value, size_t length, const char *prefix)
{
	size_t prefix_length = strlen(prefix);

	if (length < prefix_length)
		return false;
	for (size_t i = 0; i < prefix_length; ++i) {
		if (ascii_lower((unsigned char)value[i])
		    != ascii_lower((unsigned char)prefix[i]))
			return false;
	}
	return true;
}

static bool
copy_span(char *destination, size_t size, const char *source, size_t length)
{
	if (length >= size || memchr(source, '\0', length) != NULL)
		return false;
	memcpy(destination, source, length);
	destination[length] = '\0';
	return true;
}

static bool
parse_quick_connect(struct st_profile *profile, const char *value,
	size_t length)
{
	const char *cursor = value;
	const char *end = value + length;
	const char *authority_end = end;
	const char *at;
	const char *host;
	const char *host_end;
	const char *port = NULL;
	size_t prefix = 0;
	uint64_t port_value;

	struct {
		const char *prefix;
		int type;
	} schemes[] = {
		{"rlogin://", ST_CONNECTION_RLOGIN},
		{"ssh://", ST_CONNECTION_SSH},
		{"sshna://", ST_CONNECTION_SSH_NO_AUTH},
		{"telnet://", ST_CONNECTION_TELNET},
		{"telnets://", ST_CONNECTION_TELNET_TLS},
		{"raw://", ST_CONNECTION_RAW},
		{"ghost://", ST_CONNECTION_MBBS_GHOST},
		{"mqtts://", ST_CONNECTION_MQTT},
		{"shell:", ST_CONNECTION_SHELL},
	};

	if (length == 0 || memchr(value, '\0', length) != NULL)
		return false;
	for (size_t i = 0; i < sizeof(schemes) / sizeof(schemes[0]); ++i) {
		if (ascii_prefix(value, length, schemes[i].prefix)) {
			prefix = strlen(schemes[i].prefix);
			profile->connection_type = schemes[i].type;
			profile->port = st_connection_default_port(
			    (enum st_connection_type)schemes[i].type);
			break;
		}
	}
	cursor += prefix;
	if (profile->connection_type == ST_CONNECTION_SHELL) {
		if (cursor >= end || !copy_span(profile->address,
		    sizeof(profile->address), cursor, (size_t)(end - cursor)))
			return false;
		(void)copy_span(profile->name, sizeof(profile->name), "Shell", 5);
		return true;
	}
	if (profile->connection_type == ST_CONNECTION_MBBS_GHOST) {
		const char *slash = memchr(cursor, '/', (size_t)(end - cursor));

		if (slash != NULL) {
			if (!copy_span(profile->ghost_program,
			    sizeof(profile->ghost_program), slash + 1,
			    (size_t)(end - slash - 1)))
				return false;
			authority_end = slash;
		}
	}
	at = memchr(cursor, '@', (size_t)(authority_end - cursor));
	if (at != NULL) {
		const char *colon = memchr(cursor, ':', (size_t)(at - cursor));
		const char *user_end = colon == NULL ? at : colon;

		if (!copy_span(profile->username, sizeof(profile->username), cursor,
		    (size_t)(user_end - cursor))
		    || (colon != NULL && !copy_span(profile->password,
		    sizeof(profile->password), colon + 1,
		    (size_t)(at - colon - 1))))
			return false;
		cursor = at + 1;
	}
	if (cursor >= authority_end)
		return false;
	host = cursor;
	if (*host == '[') {
		const char *close = memchr(host + 1, ']',
		    (size_t)(authority_end - host - 1));

		if (close == NULL || close == host + 1)
			return false;
		host++;
		host_end = close;
		if (close + 1 < authority_end) {
			if (close[1] != ':')
				return false;
			port = close + 2;
		}
	}
	else {
		const char *colon = memchr(host, ':',
		    (size_t)(authority_end - host));

		host_end = colon == NULL ? authority_end : colon;
		if (colon != NULL) {
			if (memchr(colon + 1, ':',
			    (size_t)(authority_end - colon - 1)) != NULL)
				return false;
			port = colon + 1;
		}
	}
	if (host == host_end || !copy_span(profile->address,
	    sizeof(profile->address), host, (size_t)(host_end - host)))
		return false;
	{
		size_t name_length = (size_t)(host_end - host);

		if (name_length >= sizeof(profile->name))
			name_length = sizeof(profile->name) - 1;
		(void)copy_span(profile->name, sizeof(profile->name), host,
		    name_length);
	}
	if (port != NULL) {
		char text[6];
		size_t port_length = (size_t)(authority_end - port);

		if (!copy_span(text, sizeof(text), port, port_length)
		    || !value_parse_uint64(text, &port_value) || port_value == 0
		    || port_value > UINT16_MAX)
			return false;
		profile->port = (uint16_t)port_value;
	}
	return true;
}

int
st_wren_host_select_url(st_wren_host_t *host, const char *url,
	int default_connection_type, struct st_profile *selected)
{
	const struct st_profile *defaults = NULL;
	struct st_profile profile;

	if (host == NULL || url == NULL || selected == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	st_profile_init(&profile);
	if (host->bbs_list != NULL)
		defaults = st_bbs_list_defaults(host->bbs_list);
	if (defaults != NULL && !st_profile_copy(&profile, defaults)) {
		st_profile_dispose(&profile);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	if (defaults == NULL) {
		profile.connection_type = ST_CONNECTION_TELNET;
		profile.port = st_connection_default_port(ST_CONNECTION_TELNET);
	}
	if (default_connection_type != ST_CONNECTION_UNKNOWN) {
		profile.connection_type = default_connection_type;
		profile.port = st_connection_default_port(
		    (enum st_connection_type)default_connection_type);
	}
	if (!parse_quick_connect(&profile, url, strlen(url))) {
		st_profile_dispose(&profile);
		return IO_ERROR_INVALID_ARGUMENT;
	}
	profile.identifier = -1;
	profile.added = time(NULL);
	profile.calls = 0;
	st_profile_dispose(selected);
	*selected = profile;
	return 0;
}

int
st_wren_host_offer_save_profile(st_wren_host_t *host,
	const struct st_profile *profile, bool *saved)
{
	WrenHandle *prepare;
	WrenHandle *offer;
	int status = 0;

	if (host == NULL || profile == NULL || saved == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*saved = false;
	if (host->config.safe_mode || !st_settings.prompt_save
	    || profile->hide_popups || profile->identifier >= 0)
		return 0;
	prepare = wrenMakeCallHandle(host->vm, "prepare()");
	offer = wrenMakeCallHandle(host->vm, "offerSave(_,_)");
	if (prepare == NULL || offer == NULL) {
		status = IO_ERROR_OUT_OF_MEMORY;
		goto finished;
	}
	wrenEnsureSlots(host->vm, 3);
	wrenSetSlotHandle(host->vm, 0, host->main_menu);
	if (wrenCall(host->vm, prepare) != WREN_RESULT_SUCCESS
	    || wrenGetSlotType(host->vm, 0) != WREN_TYPE_BOOL) {
		status = IO_ERROR_INVALID_FORMAT;
		goto finished;
	}
	if (!wrenGetSlotBool(host->vm, 0))
		goto finished;
	wrenSetSlotHandle(host->vm, 0, host->main_menu);
	if (!st_wren_profile_push(host->vm, 1, profile, false)) {
		status = IO_ERROR_OUT_OF_MEMORY;
		goto finished;
	}
	wrenSetSlotString(host->vm, 2, profile->name);
	if (wrenCall(host->vm, offer) != WREN_RESULT_SUCCESS
	    || wrenGetSlotType(host->vm, 0) != WREN_TYPE_BOOL)
		status = IO_ERROR_INVALID_FORMAT;
	else
		*saved = wrenGetSlotBool(host->vm, 0);
finished:
	if (prepare != NULL)
		wrenReleaseHandle(host->vm, prepare);
	if (offer != NULL)
		wrenReleaseHandle(host->vm, offer);
	return status;
}

static void
fn_menu_quick_connect(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct st_profile profile;
	const char *value;
	int length;

	if (!host->bbs_model.loaded
	    || wrenGetSlotType(vm, 1) != WREN_TYPE_STRING) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	if (!st_wren_text_slot(vm, 1, &value, &length)) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	st_profile_init(&profile);
	if (!st_profile_copy(&profile, &host->bbs_model.defaults.profile)
	    || !parse_quick_connect(&profile, value, (size_t)length)) {
		st_profile_dispose(&profile);
		wrenSetSlotNull(vm, 0);
		return;
	}
	profile.identifier = -1;
	profile.added = time(NULL);
	profile.calls = 0;
	if (!st_wren_profile_push(vm, 0, &profile, false))
		wrenSetSlotNull(vm, 0);
	st_profile_dispose(&profile);
}

static void
fn_menu_application_title(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	const char *title = host->config.application_title;
	enum st_console_backend backend;
	char composed[96];

	if (title == NULL)
		title = "SyncTERM";
	if (st_console_get_backend(host->config.console, &backend) != 0) {
		wrenSetSlotString(vm, 0, title);
		return;
	}
	(void)snprintf(composed, sizeof(composed), "%.60s - %.30s", title,
	    st_console_backend_name(backend));
	wrenSetSlotString(vm, 0, composed);
}

static void
fn_menu_time_text(WrenVM *vm)
{
	static const char *const weekdays[] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
	};
	static const char *const months[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
	};
	time_t current = time(NULL);
	struct tm local_value;
	struct tm *local = value_convert_localtime(current, &local_value)
	    ? &local_value : NULL;
	char text[32] = "";

	if (local != NULL) {
		int hour = local->tm_hour % 12;

		if (hour == 0)
			hour = 12;
		(void)snprintf(text, sizeof(text), "%s %s %02d %04d %02d:%02d %s",
		    weekdays[local->tm_wday], months[local->tm_mon], local->tm_mday,
		    local->tm_year + 1900, hour, local->tm_min,
		    local->tm_hour >= 12 ? "pm" : "am");
	}
	wrenSetSlotString(vm, 0, text);
}

static void
fn_menu_timestamp(WrenVM *vm)
{
	st_wren_return_i64(vm, time(NULL));
}

static void
fn_menu_show_entry(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	const char *title = host->config.application_title == NULL ? "SyncTERM" :
	    host->config.application_title;
	char composed[128];

	if (wrenGetSlotType(vm, 1) == WREN_TYPE_NULL) {
		st_wren_screen_result(vm, st_console_set_title(host->config.console, title), "Menu.showEntry");
		return;
	}
	struct foreign_bbs *bbs = bbs_value(vm, 1);
	if (bbs == NULL) return;
	(void)snprintf(composed, sizeof(composed), "%s - %s", title,
	    bbs->record->profile.name);
	st_wren_screen_result(vm, st_console_set_title(host->config.console, composed), "Menu.showEntry");
}

static void
fn_menu_quit(WrenVM *vm)
{
	st_wren_host_from_vm(vm)->exit_requested = true;
	wrenSetSlotNull(vm, 0);
}

static void
fn_menu_entries(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);

	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	if (!host->bbs_model.loaded)
		return;
	for (size_t i = 0; i < st_wren_bbs_model_count(&host->bbs_model); ++i) {
		struct st_wren_bbs_record *record =
		    st_wren_bbs_model_at(&host->bbs_model, i);

		if (record != NULL && push_bbs(vm, 1, record,
		    ST_WREN_BBS_MODEL_ENTRY) != NULL)
			wrenInsertInList(vm, 0, -1, 1);
	}
}

static void
fn_menu_can_append(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);

	wrenSetSlotBool(vm, 0, host->bbs_model.loaded
	    && st_wren_bbs_model_count(&host->bbs_model)
	    < ST_BBS_LIST_MAX_ENTRIES);
}

static void
fn_menu_defaults(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	if (!host->bbs_model.loaded)
		wrenSetSlotNull(vm, 0);
	else
		(void)push_bbs(vm, 0, &host->bbs_model.defaults,
		    ST_WREN_BBS_DEFAULTS);
}

static void
fn_menu_name_available(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_STRING) {
		wrenSetSlotBool(vm, 0, false);
		return;
	}
	{
		const char *name;

		if (!st_wren_text_slot(vm, 1, &name, NULL))
			return;
		wrenSetSlotBool(vm, 0,
		    st_wren_bbs_model_name_available(&host->bbs_model, name));
	}
}

static void
fn_menu_create(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct st_wren_bbs_record *record;
	const char *name;
	int fields[32];
	size_t field_count;

	if (!host->bbs_model.loaded
	    || wrenGetSlotType(vm, 1) != WREN_TYPE_STRING) {
		menu_error(vm, 0, 0, "The directory entry could not be created.");
		return;
	}
	if (!st_wren_text_slot(vm, 1, &name, NULL))
		return;
	field_count = configured_sort_fields(fields);
	record = st_wren_bbs_model_create(&host->bbs_model, name,
	    &host->bbs_model.defaults.profile, fields, field_count);
	if (record == NULL)
		menu_error(vm, 0, 0, "The directory entry could not be created.");
	else
		(void)push_bbs(vm, 0, record, ST_WREN_BBS_MODEL_ENTRY);
}

static void
fn_menu_copy(WrenVM *vm)
{
	struct foreign_bbs *source = bbs_value(vm, 1);
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct st_wren_bbs_record *record;
	const char *name;
	int fields[32];
	size_t field_count;

	if (source == NULL || wrenGetSlotType(vm, 2) != WREN_TYPE_STRING) {
		menu_error(vm, 0, 0, "The directory entry could not be created.");
		return;
	}
	if (!st_wren_text_slot(vm, 2, &name, NULL))
		return;
	field_count = configured_sort_fields(fields);
	record = st_wren_bbs_model_create(&host->bbs_model, name,
	    &source->record->profile, fields, field_count);
	if (record == NULL)
		menu_error(vm, 0, 0, "The directory entry could not be created.");
	else
		(void)push_bbs(vm, 0, record, ST_WREN_BBS_MODEL_ENTRY);
}

static void
fn_menu_sort(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	int fields[32];
	size_t count = configured_sort_fields(fields);
	int status = host->bbs_list == NULL ? IO_ERROR_INVALID_ARGUMENT
	    : st_bbs_list_sort(host->bbs_list, fields, count);
	if (status == 0)
		st_wren_bbs_model_sort(&host->bbs_model, fields, count);
	menu_result(vm, status, true, "The directory could not be sorted.");
}

static void
push_row(WrenVM *vm, int list_slot, int row_slot, int value_slot,
	double value, const char *name)
{
	wrenSetSlotNewList(vm, row_slot);
	wrenSetSlotDouble(vm, value_slot, value);
	wrenInsertInList(vm, row_slot, -1, value_slot);
	wrenSetSlotString(vm, value_slot, name);
	wrenInsertInList(vm, row_slot, -1, value_slot);
	wrenInsertInList(vm, list_slot, -1, row_slot);
}

static void
push_rows(WrenVM *vm, const char *const *names, size_t count)
{
	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < count; ++i)
		push_row(vm, 0, 1, 2, (double)i, names[i]);
}

static void
fn_menu_sort_fields(WrenVM *vm)
{
	wrenEnsureSlots(vm, 4);
	wrenSetSlotNewList(vm, 0);
	for (int field = ST_BBS_SORT_NAME; field <= ST_BBS_SORT_ENCODING;
	    ++field) {
		const char *name = st_bbs_sort_field_name(field);

		if (name == NULL)
			continue;
		wrenSetSlotNewList(vm, 1);
		wrenSetSlotDouble(vm, 2, field);
		wrenInsertInList(vm, 1, -1, 2);
		wrenSetSlotString(vm, 2, name);
		wrenInsertInList(vm, 1, -1, 2);
		wrenSetSlotBool(vm, 2, st_bbs_sort_field_reversed(field));
		wrenInsertInList(vm, 1, -1, 2);
		wrenInsertInList(vm, 0, -1, 1);
	}
}

static void
fn_menu_sort_profiles(WrenVM *vm)
{
	wrenEnsureSlots(vm, 4);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < st_settings_sort_profile_count(); ++i) {
		const struct name_value *profile = st_settings_sort_profile_at(i);
		const char *cursor = profile->value;

		wrenSetSlotNewList(vm, 1);
		wrenSetSlotString(vm, 2, profile->name);
		wrenInsertInList(vm, 1, -1, 2);
		wrenSetSlotNewList(vm, 2);
		while (*cursor != '\0') {
			char token[24];
			const char *end = strchr(cursor, ',');
			size_t length = end == NULL ? strlen(cursor)
			    : (size_t)(end - cursor);
			int64_t field;

			if (length == 0 || length >= sizeof(token))
				break;
			memcpy(token, cursor, length);
			token[length] = '\0';
			if (!value_parse_int64(token, &field) || field < INT_MIN
			    || field > INT_MAX
			    || st_bbs_sort_field_normalize((int)field) == 0)
				break;
			wrenSetSlotDouble(vm, 3,
			    st_bbs_sort_field_normalize((int)field));
			wrenInsertInList(vm, 2, -1, 3);
			if (end == NULL)
				break;
			cursor = end + 1;
		}
		wrenInsertInList(vm, 1, -1, 2);
		wrenInsertInList(vm, 0, -1, 1);
	}
}

static bool
slot_sort_order(WrenVM *vm, int slot, int *fields, size_t *count,
	char *text, size_t text_size)
{
	int list_count;
	size_t used = 0;

	if (wrenGetSlotType(vm, slot) != WREN_TYPE_LIST) {
		st_wren_abort(vm, "sort order must be a List");
		return false;
	}
	list_count = wrenGetListCount(vm, slot);
	if (list_count < 0 || list_count > 32) {
		st_wren_abort(vm, "sort order has too many fields");
		return false;
	}
	for (int i = 0; i < list_count; ++i) {
		int64_t value;
		int normalized;
		char number[24];
		int length;

		wrenGetListElement(vm, slot, i, slot + 1);
		if (!slot_integer(vm, slot + 1, -ST_BBS_SORT_ENCODING,
		    ST_BBS_SORT_ENCODING, &value))
			return false;
		normalized = st_bbs_sort_field_normalize((int)value);
		if (normalized == 0) {
			st_wren_abort(vm, "sort order contains an unknown field");
			return false;
		}
		for (int prior = 0; prior < i; ++prior) {
			if (abs(fields[prior]) == abs(normalized)) {
				st_wren_abort(vm, "sort order contains a duplicate field");
				return false;
			}
		}
		fields[i] = normalized;
		length = snprintf(number, sizeof(number), "%d", normalized);
		if (length < 0 || (size_t)length >= sizeof(number)
		    || used + (i == 0 ? 0U : 1U) + (size_t)length >= text_size) {
			st_wren_abort(vm, "sort order is too long");
			return false;
		}
		if (i != 0)
			text[used++] = ',';
		memcpy(text + used, number, (size_t)length);
		used += (size_t)length;
	}
	text[used] = '\0';
	*count = (size_t)list_count;
	return true;
}

static bool
slot_profile_name(WrenVM *vm, int slot, size_t except, const char **name)
{
	int length;
	const char *value;
	bool equal;

	if (!st_wren_text_slot(vm, slot, &value, &length)) {
		return false;
	}
	if (length < 1
	    || memchr(value, '\0', (size_t)length) != NULL
	    || !st_unicode_name_valid_normalized(value, 19, 38, "=:")) {
		st_wren_abort(vm, "sort profile name must contain 1 through 19 "
		    "Unicode scalars (38 UTF-8 bytes maximum)");
		return false;
	}
	for (size_t i = 0; i < st_settings_sort_profile_count(); ++i) {
		const char *existing = st_settings_sort_profile_at(i)->name;

		if (i != except && st_unicode_caseless_equal(&equal, existing,
		    strlen(existing), value, (size_t)length) == 0 && equal) {
			st_wren_abort(vm, "sort profile name is already in use");
			return false;
		}
	}
	*name = value;
	return true;
}

static void
fn_menu_active_sort_profile(WrenVM *vm)
{
	wrenSetSlotDouble(vm, 0, (double)st_settings_active_sort_profile());
}

static void
fn_menu_set_active_sort_profile(WrenVM *vm)
{
	int64_t index;
	if (!slot_integer(vm, 1, 0,
	    (int64_t)st_settings_sort_profile_count() - 1, &index))
		return;
	int status = st_settings_sort_profile_select((size_t)index);
	if (status != 0) {
		menu_result(vm, status, false, "The sort profile could not be selected.");
		return;
	}
	fn_menu_sort(vm);
}

static void
fn_menu_add_sort_profile(WrenVM *vm)
{
	int64_t index;
	int fields[32];
	size_t count;
	char order[256];
	const char *name;

	wrenEnsureSlots(vm, 5);
	if (!slot_integer(vm, 1, 0,
	    (int64_t)st_settings_sort_profile_count(), &index)
	    || !slot_profile_name(vm, 2, SIZE_MAX, &name)
	    || !slot_sort_order(vm, 3, fields, &count, order, sizeof(order)))
		return;
	(void)fields;
	(void)count;
	menu_result(vm, st_settings_sort_profile_add((size_t)index,
	    name, order), false, "The sort profile could not be added.");
}

static void
fn_menu_update_sort_profile(WrenVM *vm)
{
	int64_t index;
	int fields[32];
	size_t count;
	char order[256];
	const char *name;
	bool was_active;
	int status;

	wrenEnsureSlots(vm, 5);
	if (!slot_integer(vm, 1, 0,
	    (int64_t)st_settings_sort_profile_count() - 1, &index)
	    || !slot_profile_name(vm, 2, (size_t)index, &name)
	    || !slot_sort_order(vm, 3, fields, &count, order, sizeof(order)))
		return;
	(void)fields;
	(void)count;
	was_active = (size_t)index == st_settings_active_sort_profile();
	status = st_settings_sort_profile_update((size_t)index, name, order);
	if (status == 0 && was_active) {
		fn_menu_sort(vm);
		return;
	}
	menu_result(vm, status, false, "The sort profile could not be updated.");
}

static void
fn_menu_delete_sort_profile(WrenVM *vm)
{
	int64_t index;

	if (!slot_integer(vm, 1, 0,
	    (int64_t)st_settings_sort_profile_count() - 1, &index))
		return;
	int status = st_settings_sort_profile_remove((size_t)index);
	if (status != 0) {
		menu_result(vm, status, false, "The sort profile could not be removed.");
		return;
	}
	fn_menu_sort(vm);
}

static void
fn_menu_save_sort_profiles(WrenVM *vm)
{
	if (menu_writable(vm, "Sort profiles cannot be saved in safe mode."))
		menu_result(vm, st_settings_save(), true,
		    "The sort profiles could not be saved.");
}

static void
fn_menu_offline_scrollback(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct st_console_cell *cells = NULL;
	uint32_t width;
	size_t lines;
	enum st_screen_mode mode;
	bool has_status;

	if (host->config.session_history == NULL
	    || st_session_history_copy(host->config.session_history, &cells,
	    &width, &lines, &mode, &has_status) != 0
	    || lines > UINT32_MAX) {
		free(cells);
		wrenSetSlotNull(vm, 0);
		return;
	}
	(void)mode;
	(void)has_status;
	st_wren_push_surface_take(vm, cells, width, (uint32_t)lines);
	if (wrenGetSlotType(vm, 0) != WREN_TYPE_FOREIGN)
		return;
	for (size_t row = 0; row < lines; ++row) {
		struct st_pixel_buffer *pixels = NULL;
		int status = st_session_history_copy_pixels(host->config.session_history, row, &pixels);
		if (status == 0)
			status = st_wren_surface_take_row_pixels(vm, 0, (uint32_t)row, width, pixels);
		if (status != 0) {
			st_wren_screen_result(vm, status, "Menu.offlineScrollback pixels");
			return;
		}
	}
}

static bool
offline_scrollback_info(struct st_wren_host *host, enum st_screen_mode *mode,
	bool *has_status)
{
	uint32_t width;
	size_t lines;

	return host->config.session_history != NULL
	    && st_session_history_info(host->config.session_history, &width,
	    &lines, mode, has_status) == 0;
}

static void
fn_menu_prepare_offline_scrollback(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	enum st_screen_mode mode;
	bool has_status;
	bool success = offline_scrollback_info(host, &mode, &has_status);

	(void)has_status;
	if (success) {
		int status = 0;
		if (!host->session_mode_active) {
			status = st_wren_host_begin_session_mode(host, ST_SCREEN_MODE_CURRENT);
			if (status == 0)
				host->offline_history_mode_active = true;
		}
		if (status == 0)
			status = host->history_mode_valid ? st_wren_host_prepare_history_mode(host)
				: mode == ST_SCREEN_MODE_CURRENT ? 0 : st_wren_host_set_screen_mode(host, mode);
		if (!st_wren_screen_status(vm, status, "Menu.prepareOfflineScrollback")) return;
	}
	wrenSetSlotBool(vm, 0, success);
}

static void
fn_menu_finish_offline_scrollback(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	int status = 0;
	if (host->offline_history_mode_active) {
		status = st_wren_host_end_session_mode(host);
		if (status == 0)
			host->offline_history_mode_active = false;
	}
	st_wren_screen_result(vm, status, "Menu.finishOfflineScrollback");
}

static void
fn_menu_offline_scrollback_has_status(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	enum st_screen_mode mode;
	bool has_status = false;

	(void)offline_scrollback_info(host, &mode, &has_status);
	wrenSetSlotBool(vm, 0, has_status);
}

static void
fn_menu_script_modules(WrenVM *vm)
{
	const struct st_app_paths *paths = st_wren_host_from_vm(vm)->config.paths;
	struct st_wren_directory_entries entries = {0};

	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	if (st_wren_script_inventory(paths->scripts_directory, NULL, &entries)
	    != 0)
		return;
	for (size_t i = 0; i < entries.count; ++i) {
		if (!st_wren_bbs_script_name_valid(entries.items[i].name))
			continue;
		wrenSetSlotString(vm, 1, entries.items[i].name);
		wrenInsertInList(vm, 0, -1, 1);
	}
	st_wren_directory_entries_dispose(&entries);
}

static void
fn_menu_encryption_available(WrenVM *vm)
{
	wrenSetSlotBool(vm, 0, st_list_crypto_encryption_available());
}

static void
fn_menu_connection_types(WrenVM *vm)
{
	static const char *const names[] = {
		"Unknown", "RLogin", "RLogin (reversed)", "Telnet", "Raw TCP",
		"SSH", "SSH (no authentication)", "Modem", "Serial",
		"Serial (no RTS/CTS)", "Shell", "MBBS GHOST", "Telnet TLS",
		"MQTT",
	};

	push_rows(vm, names, sizeof(names) / sizeof(names[0]));
}

static void
fn_menu_default_port(WrenVM *vm)
{
	int64_t type;

	if (!slot_integer(vm, 1, 0, ST_CONNECTION_MQTT, &type))
		return;
	wrenSetSlotDouble(vm, 0, st_connection_default_port(
	    (enum st_connection_type)type));
}

static void
fn_menu_address_families(WrenVM *vm)
{
	static const char *const names[] = {
		"As per DNS", "IPv4 only", "IPv6 only", "Happy Eyeballs"
	};

	push_rows(vm, names, sizeof(names) / sizeof(names[0]));
}

static void
fn_menu_rates(WrenVM *vm)
{
	static const struct {
		uint32_t value;
		const char *name;
	} rates[] = {
		{300, "300"}, {600, "600"}, {1200, "1200"},
		{2400, "2400"}, {4800, "4800"}, {9600, "9600"},
		{19200, "19200"}, {38400, "38400"}, {57600, "57600"},
		{76800, "76800"}, {115200, "115200"}, {0, "Current"},
	};
	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i)
		push_row(vm, 0, 1, 2, rates[i].value, rates[i].name);
}

static int
compare_rates(const void *left, const void *right)
{
	uint32_t a = *(const uint32_t *)left;
	uint32_t b = *(const uint32_t *)right;

	return (a > b) - (a < b);
}

static void
fn_menu_serial_rates(WrenVM *vm)
{
	serial_port_t *port = NULL;
	const char *device;
	uint32_t *rates = NULL;
	size_t count = 0;
	int status;

	if (!slot_c_string(vm, 1, &device))
		return;
	status = serial_port_open(&port, device);
	if (status == 0)
		status = serial_port_get_baud_rates(port, NULL, 0, &count);
	if (status == 0 && count != 0 && count <= SIZE_MAX / sizeof(*rates)) {
		rates = malloc(count * sizeof(*rates));
		if (rates == NULL)
			status = IO_ERROR_OUT_OF_MEMORY;
	}
	else if (status == 0)
		status = IO_ERROR_NOT_SUPPORTED;
	if (status == 0) {
		size_t available;

		status = serial_port_get_baud_rates(port, rates, count,
		    &available);
		if (status == 0 && available > count)
			status = IO_ERROR_OUT_OF_RANGE;
		else if (status == 0)
			count = available;
	}
	if (port != NULL && serial_port_close(port) != 0)
		status = IO_ERROR_IO;
	if (status != 0 || count == 0) {
		free(rates);
		fn_menu_rates(vm);
		return;
	}

	qsort(rates, count, sizeof(*rates), compare_rates);
	size_t first = 0;
	while (first < count && rates[first] == 0)
		first++;
	if (first == count) {
		free(rates);
		fn_menu_rates(vm);
		return;
	}
	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	uint32_t previous = 0;
	for (size_t i = first; i < count; ++i) {
		char name[16];

		if (rates[i] == 0 || rates[i] == previous)
			continue;
		(void)snprintf(name, sizeof(name), "%" PRIu32, rates[i]);
		push_row(vm, 0, 1, 2, rates[i], name);
		previous = rates[i];
	}
	push_row(vm, 0, 1, 2, 0, "Current");
	free(rates);
}

static void
fn_menu_music_modes(WrenVM *vm)
{
	static const char *const names[] = {"SyncTERM", "BANSI", "Enabled"};
	push_rows(vm, names, sizeof(names) / sizeof(names[0]));
}

static void
fn_menu_graphics_modes(WrenVM *vm)
{
	static const char *const names[] = {"None", "RIP v1", "RIP v3", "SkyPix (Kick 1.3)", "SkyPix (Kick 3.1)", "IGS (2.21)"};
	push_rows(vm, names, sizeof(names) / sizeof(names[0]));
}

static void
fn_menu_flow_controls(WrenVM *vm)
{
	static const char *const names[] = {
		"RTS/CTS", "XON/XOFF", "RTS/CTS + XON/XOFF", "None",
	};
	push_rows(vm, names, sizeof(names) / sizeof(names[0]));
}

static void
fn_menu_flow_controls_no_rts(WrenVM *vm)
{
	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	push_row(vm, 0, 1, 2, ST_PROFILE_FLOW_XON_XOFF, "XON/XOFF");
	push_row(vm, 0, 1, 2, ST_PROFILE_FLOW_NONE, "None");
}

static void
fn_menu_parities(WrenVM *vm)
{
	static const char *const names[] = {"None", "Even", "Odd"};
	push_rows(vm, names, sizeof(names) / sizeof(names[0]));
}

static void
fn_menu_log_levels(WrenVM *vm)
{
	static const char *const names[] = {
		"Emergency", "Alert", "Critical", "Error", "Warning",
		"Notice", "Info", "Debug",
	};
	push_rows(vm, names, sizeof(names) / sizeof(names[0]));
}

static void
fn_menu_screen_modes(WrenVM *vm)
{
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < st_screen_mode_count(); ++i) {
		wrenSetSlotString(vm, 1, st_screen_mode_at(i)->name);
		wrenInsertInList(vm, 0, -1, 1);
	}
}

static void fn_menu_canvas_modes(WrenVM *vm)
{
	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < st_screen_mode_count(); ++i) {
		const struct st_screen_mode_descriptor *mode = st_screen_mode_at(i);
		char label[96];
		if (mode->mode == ST_SCREEN_MODE_CURRENT || mode->mode == ST_SCREEN_MODE_CUSTOM)
			(void)snprintf(label, sizeof(label), "%s canvas", mode->name);
		else
			(void)snprintf(label, sizeof(label), "%" PRIu32 "x%" PRIu32 " (%s)",
						   mode->console.pixel_width, mode->console.pixel_height, mode->name);
		push_row(vm, 0, 1, 2, (double)i, label);
	}
}

static void
fn_menu_output_modes(WrenVM *vm)
{
	static const struct {
		uint32_t value;
		uint32_t feature;
		const char *name;
	} modes[] = {
		{0, 0, "Automatic"},
		{1, ST_CONSOLE_BUILD_NCURSES, "Curses"},
		{2, ST_CONSOLE_BUILD_NCURSES, "Curses on cp437 Device"},
		{3, ST_CONSOLE_BUILD_NCURSES, "Curses using US-ASCII"},
		{4, ST_CONSOLE_BUILD_ANSI, "ANSI"},
		{5, ST_CONSOLE_BUILD_X11, "X11"},
		{6, ST_CONSOLE_BUILD_X11, "X11 Fullscreen"},
		{7, ST_CONSOLE_BUILD_WIN32, "Win32 Console"},
		{8, ST_CONSOLE_BUILD_WIN32, "Win32 Console Fullscreen"},
		{9, ST_CONSOLE_BUILD_SDL, "SDL"},
		{10, ST_CONSOLE_BUILD_SDL, "SDL Fullscreen"},
		{11, ST_CONSOLE_BUILD_GDI, "GDI"},
		{12, ST_CONSOLE_BUILD_GDI, "GDI Fullscreen"},
		{14, ST_CONSOLE_BUILD_WAYLAND, "Wayland"},
		{15, ST_CONSOLE_BUILD_WAYLAND, "Wayland Fullscreen"},
		{16, ST_CONSOLE_BUILD_QUARTZ, "Quartz"},
		{17, ST_CONSOLE_BUILD_QUARTZ, "Quartz Fullscreen"},
	};
	uint32_t features = st_console_build_features();

	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
		if (modes[i].feature != 0
		    && (features & modes[i].feature) == 0)
			continue;
		push_row(vm, 0, 1, 2, modes[i].value, modes[i].name);
	}
}

static void
fn_menu_audio_modes(WrenVM *vm)
{
	static const struct {
		uint32_t bit;
		const char *name;
	} modes[] = {
		/* Keep the displayed order consistent with audio_backends.c. */
		{AUDIO_BACKEND_CORE_AUDIO, "Core Audio"},
		{AUDIO_BACKEND_WASAPI, "WASAPI"},
		{AUDIO_BACKEND_PORTAUDIO, "PortAudio"},
		{AUDIO_BACKEND_PIPEWIRE, "PipeWire"},
		{AUDIO_BACKEND_SDL, "SDL"},
		{AUDIO_BACKEND_PULSE_AUDIO, "PulseAudio"},
		{AUDIO_BACKEND_ALSA, "ALSA"},
		{AUDIO_BACKEND_OSS, "OSS"},
	};
	uint32_t available = audio_available_backends();

	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
		if ((available & modes[i].bit) == 0)
			continue;
		push_row(vm, 0, 1, 2, modes[i].bit, modes[i].name);
	}
}

static const struct { enum st_midi_backend bit; const char *name; } midi_modes[] = {
	{ST_MIDI_WINMM, "WinMM"}, {ST_MIDI_APPLE, "Apple DLS"},
	{ST_MIDI_ALSA, "ALSA Sequencer"}, {ST_MIDI_FLUIDSYNTH, "FluidSynth"},
	{ST_MIDI_ADLMIDI, "libADLMIDI"},
};
static void
fn_menu_midi_modes(WrenVM *vm)
{
	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < sizeof(midi_modes) / sizeof(midi_modes[0]); ++i)
		if (st_midi_compiled_backends() & midi_modes[i].bit)
			push_row(vm, 0, 1, 2, midi_modes[i].bit, midi_modes[i].name);
}

static void
push_build_option(WrenVM *vm, const char *category, const char *name,
	bool enabled)
{
	wrenSetSlotNewList(vm, 1);
	wrenSetSlotString(vm, 2, category);
	wrenInsertInList(vm, 1, -1, 2);
	wrenSetSlotString(vm, 2, name);
	wrenInsertInList(vm, 1, -1, 2);
	wrenSetSlotBool(vm, 2, enabled);
	wrenInsertInList(vm, 1, -1, 2);
	wrenInsertInList(vm, 0, -1, 1);
}

static void
fn_menu_licenses(WrenVM *vm)
{
	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	for (const struct st_wren_license *notice = st_wren_licenses;
	    notice->name != NULL; ++notice) {
		wrenSetSlotNewList(vm, 1);
		wrenSetSlotString(vm, 2, notice->name);
		wrenInsertInList(vm, 1, -1, 2);
		wrenSetSlotBytes(vm, 2, (const char *)notice->text, notice->length);
		wrenInsertInList(vm, 1, -1, 2);
		wrenInsertInList(vm, 0, -1, 1);
	}
}

static void
fn_menu_build_options(WrenVM *vm)
{
	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
#define ST_BUILD_OPTION(category, name, enabled) \
	push_build_option(vm, category, name, enabled);
#include "st_build_options.inc"
#undef ST_BUILD_OPTION
}

static void
fn_menu_cursor_styles(WrenVM *vm)
{
	static const char *const names[] = {
		"Default", "Blinking underline", "Solid underline",
		"Blinking block", "Solid block",
	};
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		wrenSetSlotString(vm, 1, names[i]);
		wrenInsertInList(vm, 0, -1, 1);
	}
}

static void
fn_menu_scaling_modes(WrenVM *vm)
{
	static const char *const names[] = {"Blocky", "Pointy", "External"};
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		wrenSetSlotString(vm, 1, names[i]);
		wrenInsertInList(vm, 0, -1, 1);
	}
}

static void
fn_menu_colours(WrenVM *vm)
{
	static const char *const names[] = {
		"Black", "Blue", "Green", "Cyan", "Red", "Magenta", "Brown",
		"Light Gray", "Dark Gray", "Light Blue", "Light Green",
		"Light Cyan", "Light Red", "Light Magenta", "Yellow", "White",
		"Default",
	};
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		wrenSetSlotString(vm, 1, names[i]);
		wrenInsertInList(vm, 0, -1, 1);
	}
}

static void
fn_menu_background_colours(WrenVM *vm)
{
	static const char *const names[] = {
		"Black", "Blue", "Green", "Cyan", "Red", "Magenta", "Brown",
		"Light Gray", "Default",
	};
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		wrenSetSlotString(vm, 1, names[i]);
		wrenInsertInList(vm, 0, -1, 1);
	}
}

static void
fn_menu_themes(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	char error[256] = "";

	int status = st_theme_catalog_refresh(host->themes, error, sizeof(error));
	if (status != 0) {
		menu_error(vm, status, status, error[0] != '\0' ? error
		    : "unable to enumerate themes");
		return;
	}
	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < st_theme_catalog_count(host->themes); ++i) {
		const struct st_theme_catalog_entry *entry =
		    st_theme_catalog_at(host->themes, i);
		const char *const values[] = {
			entry->filename, entry->name, entry->author,
			entry->description, entry->version, entry->error,
		};

		wrenSetSlotNewList(vm, 1);
		for (size_t field = 0; field < sizeof(values) / sizeof(values[0]);
		    ++field) {
			if (values[field] == NULL)
				wrenSetSlotNull(vm, 2);
			else
				wrenSetSlotString(vm, 2, values[field]);
			wrenInsertInList(vm, 1, -1, 2);
		}
		wrenInsertInList(vm, 0, -1, 1);
	}
}

static void
fn_menu_selected_theme_file(WrenVM *vm)
{
	wrenSetSlotString(vm, 0,
	    st_settings.theme_file == NULL ? "" : st_settings.theme_file);
}

static void
fn_menu_selected_theme_package(WrenVM *vm)
{
	wrenSetSlotString(vm, 0,
	    st_settings.theme_package == NULL ? "" : st_settings.theme_package);
}

static bool
theme_filename(WrenVM *vm, int slot, const char **result)
{
	int length;
	const char *value;

	if (!st_wren_text_slot(vm, slot, &value, &length))
		return false;
	if (length < 0 || memchr(value, '\0', (size_t)length) != NULL) {
		st_wren_abort(vm, "theme filename contains NUL");
		return false;
	}
	if (length != 0 && !st_theme_valid_filename(value)) {
		st_wren_abort(vm, "invalid theme filename");
		return false;
	}
	*result = value;
	return true;
}

static void
theme_operation_result(WrenVM *vm, int status, const char *error)
{
	menu_result(vm, status, true, error != NULL && error[0] != '\0'
	    ? error : "theme operation failed");
}

static void
fn_menu_preview_theme(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	const char *filename;
	char error[256] = "";

	if (!theme_filename(vm, 1, &filename))
		return;
	theme_operation_result(vm, st_theme_preview(host->themes, filename,
	    error, sizeof(error)), error);
}

static void
fn_menu_cancel_theme_preview(WrenVM *vm)
{
	st_theme_cancel_preview(st_wren_host_from_vm(vm)->themes);
	wrenSetSlotNull(vm, 0);
}

static bool
replace_owned_string(char **destination, const char *value)
{
	char *copy = value == NULL || value[0] == '\0' ? NULL : strdup(value);

	if (value != NULL && value[0] != '\0' && copy == NULL)
		return false;
	free(*destination);
	*destination = copy;
	return true;
}

static void
restore_theme_selection(struct st_wren_host *host,
	const struct st_settings *previous, char *error, size_t size)
{
	bool settings_restored = st_settings_copy(&st_settings, previous);
	int status = st_theme_apply_settings(host->themes, previous, NULL, 0);

	if (!settings_restored || status != 0) {
		char original[256];
		(void)snprintf(original, sizeof(original), "%s", error);
		/* Keep restoration visible even when the primary diagnostic was full. */
		int written = snprintf(error, size,
		    "Restoration failed:%s%s%s%s Original failure: ",
		    settings_restored ? "" : " running settings could not be restored.",
		    status == 0 ? "" : " the active theme could not be restored (",
		    status == 0 ? "" : menu_io_reason(status),
		    status == 0 ? "" : ").");
		if (written >= 0 && (size_t)written < size) {
			size_t count = strlen(original);
			size_t available = size - (size_t)written - 1;
			if (count > available)
				count = available;
			memcpy(error + written, original, count);
			error[(size_t)written + count] = '\0';
		}
	}
}

static void
theme_delete_result(WrenVM *vm, int status, const char *error,
	bool classic_saved)
{
	char message[384];

	if (status != 0 && classic_saved) {
		(void)snprintf(message, sizeof(message),
		    "Classic Theme was selected and saved, but deletion failed. %s", error);
		theme_operation_result(vm, status, message);
	}
	else
		theme_operation_result(vm, status, error);
}

static void
fn_menu_select_theme(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct st_settings previous;
	const char *filename;
	char error[256] = "";
	int status;
	bool native_status = true;

	if (!theme_filename(vm, 1, &filename))
		return;
	st_settings_init(&previous);
	if (!st_settings_copy(&previous, &st_settings)) {
		menu_error(vm, 0, 0, "The running settings could not be copied.");
		return;
	}
	status = st_theme_select(host->themes, filename, error, sizeof(error));
	if (status == 0 && (!replace_owned_string(&st_settings.theme_file,
	    filename) || !replace_owned_string(&st_settings.theme_package, NULL))) {
		native_status = false;
		status = IO_ERROR_OUT_OF_MEMORY;
		(void)snprintf(error, sizeof(error), "out of memory");
	}
	if (status == 0 && !host->config.safe_mode)
		status = st_settings_save();
	if (status != 0 && error[0] == '\0')
		(void)snprintf(error, sizeof(error),
		    "unable to save the selected theme");
	if (status != 0) {
		restore_theme_selection(host, &previous, error, sizeof(error));
	}
	else
		++host->settings_generation;
	st_settings_dispose(&previous);
	if (status != 0 && !native_status)
		menu_error(vm, 0, 0, error);
	else
		theme_operation_result(vm, status, error);
}

static void
fn_menu_delete_theme(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct st_settings previous;
	const char *filename;
	char error[256] = "";
	bool selection_changed = false;
	bool selection_saved = false;
	int status;

	if (!theme_filename(vm, 1, &filename))
		return;
	if (host->config.safe_mode) {
		menu_error(vm, IO_ERROR_READ_ONLY, 0, "Themes cannot be deleted in safe mode.");
		return;
	}
	if (filename[0] == '\0') {
		menu_error(vm, IO_ERROR_INVALID_ARGUMENT, 0,
		    "The built-in Classic Theme cannot be deleted.");
		return;
	}
	st_settings_init(&previous);
	if (!st_settings_copy(&previous, &st_settings)) {
		menu_error(vm, 0, 0, "The running settings could not be copied.");
		return;
	}
	if (st_settings.theme_file != NULL
	    && strcmp(st_settings.theme_file, filename) == 0) {
		selection_changed = true;
		status = st_theme_select(host->themes, "", error, sizeof(error));
		if (status == 0) {
			free(st_settings.theme_file);
			st_settings.theme_file = NULL;
			free(st_settings.theme_package);
			st_settings.theme_package = NULL;
			status = st_settings_save();
			selection_saved = status == 0;
		}
	}
	else
		status = 0;
	if (status == 0)
		status = st_theme_delete(host->themes, filename, error,
		    sizeof(error));
	if (status != 0 && selection_changed && !selection_saved) {
		restore_theme_selection(host, &previous, error, sizeof(error));
	}
	if (selection_changed && (status == 0 || selection_saved))
		++host->settings_generation;
	st_settings_dispose(&previous);
	theme_delete_result(vm, status, error, selection_saved);
}

static void
push_theme_document(WrenVM *vm, struct st_theme_document *source)
{
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm_menu", "ThemeDocument", 1);
	struct foreign_theme_document *document = wrenSetSlotNewForeign(vm, 0, 1,
	    sizeof(*document));
	document->header.kind = MENU_FOREIGN_THEME_DOCUMENT;
	document->document = source;
}

static void
fn_menu_new_theme_document(WrenVM *vm)
{
	struct st_theme_document *document = NULL;
	char error[256] = "";
	int status = st_theme_document_create(&document,
	    st_wren_host_from_vm(vm)->themes, error, sizeof(error));

	if (status != 0) {
		menu_error(vm, status, status, error[0] != '\0' ? error
		    : "unable to create theme document");
		return;
	}
	push_theme_document(vm, document);
}

static void
fn_menu_open_theme_document(WrenVM *vm)
{
	struct st_theme_document *document = NULL;
	const char *filename;
	char error[256] = "";
	int status;

	if (!theme_filename(vm, 1, &filename))
		return;
	if (filename[0] == '\0') {
		st_wren_abort(vm, "ThemeDocument.open requires a nonempty filename");
		return;
	}
	status = st_theme_document_open(&document,
	    st_wren_host_from_vm(vm)->themes, filename, error, sizeof(error));
	if (status != 0) {
		menu_error(vm, status, status, error[0] != '\0' ? error
		    : "unable to open theme document");
		return;
	}
	push_theme_document(vm, document);
}

static void
fn_theme_document_filename(WrenVM *vm)
{
	struct foreign_theme_document *document = theme_document_value(vm);

	if (document != NULL)
		wrenSetSlotString(vm, 0,
		    st_theme_document_filename(document->document));
}

static void
fn_theme_document_metadata(WrenVM *vm)
{
	struct foreign_theme_document *document = theme_document_value(vm);
	int64_t field;

	if (document == NULL || !slot_integer(vm, 1, 0,
	    ST_THEME_METADATA_FIELD_COUNT - 1, &field))
		return;
	const char *value = st_theme_document_metadata(document->document,
	    (unsigned)field);
	if (value == NULL)
		wrenSetSlotNull(vm, 0);
	else
		wrenSetSlotString(vm, 0, value);
}

static void
fn_theme_document_set_metadata(WrenVM *vm)
{
	struct foreign_theme_document *document = theme_document_value(vm);
	const char *value;
	int64_t field;
	char error[256] = "";

	if (document == NULL || !slot_integer(vm, 1, 0,
	    ST_THEME_METADATA_FIELD_COUNT - 1, &field)
	    || !slot_c_string(vm, 2, &value))
		return;
	theme_operation_result(vm, st_theme_document_set_metadata(
	    document->document, (unsigned)field, value, error, sizeof(error)),
	    error);
}

static void
fn_theme_document_styles(WrenVM *vm)
{
	struct foreign_theme_document *document = theme_document_value(vm);

	if (document == NULL)
		return;
	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < st_theme_document_style_count(document->document);
	    ++i) {
		struct st_theme_document_style style;

		if (!st_theme_document_style_at(document->document, i, &style))
			continue;
		wrenSetSlotNewList(vm, 1);
		wrenSetSlotString(vm, 2, style.role);
		wrenInsertInList(vm, 1, -1, 2);
		wrenSetSlotBool(vm, 2, style.builtin);
		wrenInsertInList(vm, 1, -1, 2);
		wrenSetSlotDouble(vm, 2, style.present);
		wrenInsertInList(vm, 1, -1, 2);
		for (unsigned field = 0; field < ST_THEME_STYLE_FIELD_COUNT; ++field) {
			wrenSetSlotDouble(vm, 2, style.values[field]);
			wrenInsertInList(vm, 1, -1, 2);
		}
		wrenInsertInList(vm, 0, -1, 1);
	}
}

static void
fn_theme_document_set_style(WrenVM *vm)
{
	struct foreign_theme_document *document = theme_document_value(vm);
	const char *role;
	int64_t field, mode, value = 0;
	char error[256] = "";

	if (document == NULL || !slot_c_string(vm, 1, &role)
	    || !slot_integer(vm, 2, 0, ST_THEME_STYLE_FIELD_COUNT - 1, &field)
	    || !slot_integer(vm, 3, 0, ST_THEME_VALUE_EXPLICIT, &mode))
		return;
	if (mode == ST_THEME_VALUE_EXPLICIT
	    && !slot_integer(vm, 4, 0, 0xffffff, &value))
		return;
	theme_operation_result(vm, st_theme_document_set_style(
	    document->document, role, (unsigned)field,
	    (enum st_theme_value_mode)mode, (int)value, error, sizeof(error)),
	    error);
}

static void
fn_theme_document_add_style(WrenVM *vm)
{
	struct foreign_theme_document *document = theme_document_value(vm);
	const char *role;
	char error[256] = "";

	if (document == NULL || !slot_c_string(vm, 1, &role))
		return;
	theme_operation_result(vm, st_theme_document_add_style(document->document,
	    role, error, sizeof(error)), error);
}

static void
fn_theme_document_remove_style(WrenVM *vm)
{
	struct foreign_theme_document *document = theme_document_value(vm);
	const char *role;
	char error[256] = "";

	if (document == NULL || !slot_c_string(vm, 1, &role))
		return;
	theme_operation_result(vm, st_theme_document_remove_style(
	    document->document, role, error, sizeof(error)), error);
}

static void
fn_theme_document_glyphs(WrenVM *vm)
{
	struct foreign_theme_document *document = theme_document_value(vm);

	if (document == NULL)
		return;
	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < st_theme_document_glyph_count(document->document);
	    ++i) {
		struct st_theme_document_glyph glyph;

		if (!st_theme_document_glyph_at(document->document, i, &glyph))
			continue;
		wrenSetSlotNewList(vm, 1);
		wrenSetSlotString(vm, 2, glyph.name);
		wrenInsertInList(vm, 1, -1, 2);
		wrenSetSlotBool(vm, 2, glyph.builtin);
		wrenInsertInList(vm, 1, -1, 2);
		wrenSetSlotDouble(vm, 2, glyph.present);
		wrenInsertInList(vm, 1, -1, 2);
		for (unsigned field = 0; field < ST_THEME_GLYPH_FIELD_COUNT; ++field) {
			wrenSetSlotDouble(vm, 2, glyph.values[field]);
			wrenInsertInList(vm, 1, -1, 2);
		}
		wrenInsertInList(vm, 0, -1, 1);
	}
}

static void
fn_theme_document_set_glyph(WrenVM *vm)
{
	struct foreign_theme_document *document = theme_document_value(vm);
	const char *name;
	int64_t field, mode, value = 0;
	char error[256] = "";

	if (document == NULL || !slot_c_string(vm, 1, &name)
	    || !slot_integer(vm, 2, 0, ST_THEME_GLYPH_FIELD_COUNT - 1, &field)
	    || !slot_integer(vm, 3, 0, ST_THEME_VALUE_EXPLICIT, &mode))
		return;
	if (mode == ST_THEME_VALUE_EXPLICIT) {
		int64_t maximum = field == ST_THEME_GLYPH_CODEPOINT
		    ? 0x10ffff : 0xff;

		if (!slot_integer(vm, 4, 0, maximum, &value))
			return;
	}
	theme_operation_result(vm, st_theme_document_set_glyph(
	    document->document, name, (unsigned)field,
	    (enum st_theme_value_mode)mode, (uint32_t)value, error, sizeof(error)),
	    error);
}

static void
fn_theme_document_add_glyph(WrenVM *vm)
{
	struct foreign_theme_document *document = theme_document_value(vm);
	const char *name;
	int64_t ascii;
	char error[256] = "";

	if (document == NULL || !slot_c_string(vm, 1, &name)
	    || !slot_integer(vm, 2, 0x20, 0x7e, &ascii))
		return;
	theme_operation_result(vm, st_theme_document_add_glyph(
	    document->document, name, (int)ascii, error, sizeof(error)), error);
}

static void
fn_theme_document_remove_glyph(WrenVM *vm)
{
	struct foreign_theme_document *document = theme_document_value(vm);
	const char *name;
	char error[256] = "";

	if (document == NULL || !slot_c_string(vm, 1, &name))
		return;
	theme_operation_result(vm, st_theme_document_remove_glyph(
	    document->document, name, error, sizeof(error)), error);
}

static void
fn_theme_document_theme_data(WrenVM *vm)
{
	struct foreign_theme_document *document = theme_document_value(vm);

	if (document != NULL)
		st_wren_push_theme_data(vm,
		    st_theme_document_preview(document->document));
}

static void
fn_theme_document_dirty(WrenVM *vm)
{
	struct foreign_theme_document *document = theme_document_value(vm);
	if (document != NULL)
		wrenSetSlotBool(vm, 0,
		    st_theme_document_dirty(document->document));
}

static void
fn_theme_document_can_undo(WrenVM *vm)
{
	struct foreign_theme_document *document = theme_document_value(vm);
	if (document != NULL)
		wrenSetSlotBool(vm, 0,
		    st_theme_document_can_undo(document->document));
}

static void
fn_theme_document_can_redo(WrenVM *vm)
{
	struct foreign_theme_document *document = theme_document_value(vm);
	if (document != NULL)
		wrenSetSlotBool(vm, 0,
		    st_theme_document_can_redo(document->document));
}

static void
fn_theme_document_undo(WrenVM *vm)
{
	struct foreign_theme_document *document = theme_document_value(vm);
	wrenSetSlotBool(vm, 0, document != NULL
	    && st_theme_document_undo(document->document));
}

static void
fn_theme_document_redo(WrenVM *vm)
{
	struct foreign_theme_document *document = theme_document_value(vm);
	wrenSetSlotBool(vm, 0, document != NULL
	    && st_theme_document_redo(document->document));
}

static void
fn_theme_document_import(WrenVM *vm)
{
	struct foreign_theme_document *document = theme_document_value(vm);
	const char *filename;
	char error[256] = "";

	if (document == NULL || !theme_filename(vm, 1, &filename))
		return;
	theme_operation_result(vm, st_theme_document_import(document->document,
	    filename, error, sizeof(error)), error);
}

static void
fn_theme_document_save(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct foreign_theme_document *document = theme_document_value(vm);
	const char *filename = NULL;
	bool force;
	char error[256] = "";
	enum st_theme_document_error result;

	if (document == NULL)
		return;
	if (wrenGetSlotType(vm, 1) == WREN_TYPE_STRING) {
		if (!theme_filename(vm, 1, &filename))
			return;
	}
	else if (wrenGetSlotType(vm, 1) != WREN_TYPE_NULL) {
		st_wren_abort(vm,
		    "ThemeDocument.save filename must be a String or null");
		return;
	}
	if (!slot_bool(vm, 2, &force))
		return;
	if (host->config.safe_mode) {
		menu_error(vm, IO_ERROR_READ_ONLY, 0,
		    "Themes cannot be saved in safe mode.");
		return;
	}
	else
		result = st_theme_document_save(document->document, filename, force,
		    error, sizeof(error));
	if (result == ST_THEME_DOCUMENT_OK)
		wrenSetSlotNull(vm, 0);
	else
		menu_error(vm, 0, result, error[0] == '\0'
		    ? "theme save failed" : error);
}

static void
fn_theme_document_activate(WrenVM *vm)
{
	struct foreign_theme_document *document = theme_document_value(vm);
	char error[256] = "";

	if (document != NULL)
		theme_operation_result(vm, st_theme_document_activate(document->document,
		    error, sizeof(error)), error);
}

static bool
theme_package(WrenVM *vm, int slot, const char **result)
{
	if (!slot_c_string(vm, slot, result))
		return false;
	if (!st_theme_valid_package(*result)) {
		st_wren_abort(vm, "invalid theme package");
		return false;
	}
	return true;
}

static void
fn_menu_refresh_cloud_themes(WrenVM *vm)
{
	char warning[256] = "";
	const char *selected = st_settings.theme_package == NULL ? ""
	    : st_settings.theme_package;
	int status = st_theme_cloud_refresh(st_wren_host_from_vm(vm)->themes,
	    true, selected, warning, sizeof(warning));

	if (status != 0 && warning[0] == '\0')
		(void)snprintf(warning, sizeof(warning),
		    "unable to load the online theme catalog");
	if (status != 0)
		theme_operation_result(vm, status, warning);
	else if (warning[0] == '\0')
		wrenSetSlotNull(vm, 0);
	else
		wrenSetSlotString(vm, 0, warning);
}

static void
fn_menu_cloud_themes(WrenVM *vm)
{
	struct st_theme_manager *themes = st_wren_host_from_vm(vm)->themes;
	uint64_t generation = st_theme_cloud_generation(themes);

	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	wrenGetVariable(vm, "syncterm_menu", "CloudThemeEntry", 2);
	for (size_t i = 0; i < st_theme_cloud_count(themes); ++i) {
		struct foreign_cloud_theme_entry *view = wrenSetSlotNewForeign(vm,
		    1, 2, sizeof(*view));
		*view = (struct foreign_cloud_theme_entry){
		    .header.kind = MENU_FOREIGN_CLOUD_THEME_ENTRY,
		    .context = st_wren_context_from_vm(vm),
		    .generation = generation, .index = i,
		};
		wrenInsertInList(vm, 0, -1, 1);
	}
}

static const struct st_cloud_theme_entry *
cloud_theme_entry_value(WrenVM *vm)
{
	struct foreign_cloud_theme_entry *view = st_wren_foreign_require(vm, 0,
	    "syncterm_menu", "CloudThemeEntry");
	if (view == NULL) return NULL;
	if (view->header.kind != MENU_FOREIGN_CLOUD_THEME_ENTRY) {
		st_wren_abort(vm, "expected CloudThemeEntry");
		return NULL;
	}
	struct st_theme_manager *themes = st_wren_host_from_vm(vm)->themes;
	if (view->context != st_wren_context_from_vm(vm)
	    || view->generation != st_theme_cloud_generation(themes)
	    || view->index >= st_theme_cloud_count(themes)) {
		st_wren_abort(vm, "CloudThemeEntry: stale view; reacquire it from Menu.cloudThemes");
		return NULL;
	}
	return st_theme_cloud_at(themes, view->index);
}

#define CLOUD_TEXT_GETTER(name, field) \
	static void fn_cloud_theme_##name(WrenVM *vm) \
	{ \
		const struct st_cloud_theme_entry *entry = cloud_theme_entry_value(vm); \
		char *text = NULL; \
		if (entry == NULL) return; \
		int status = copy_diagnostic_name(&text, entry->field); \
		if (status != 0) menu_error(vm, status, status, "The cloud theme metadata could not be converted."); \
		else if (text == NULL) wrenSetSlotNull(vm, 0); \
		else wrenSetSlotString(vm, 0, text); \
		free(text); \
	}
CLOUD_TEXT_GETTER(package, package)
CLOUD_TEXT_GETTER(name, name)
CLOUD_TEXT_GETTER(author, author)
CLOUD_TEXT_GETTER(description, description)
CLOUD_TEXT_GETTER(version, version)
CLOUD_TEXT_GETTER(source_uri, uri)
CLOUD_TEXT_GETTER(expected_sha256, sha256)
CLOUD_TEXT_GETTER(error, error)
#undef CLOUD_TEXT_GETTER

static void fn_cloud_theme_cached(WrenVM *vm)
{
	const struct st_cloud_theme_entry *entry = cloud_theme_entry_value(vm);
	if (entry != NULL) wrenSetSlotBool(vm, 0, entry->cached);
}
static void fn_cloud_theme_update_available(WrenVM *vm)
{
	const struct st_cloud_theme_entry *entry = cloud_theme_entry_value(vm);
	if (entry != NULL) wrenSetSlotBool(vm, 0, entry->update_available);
}

static void
fn_menu_preview_cloud_theme(WrenVM *vm)
{
	const char *package;
	char error[256] = "";

	if (!theme_package(vm, 1, &package))
		return;
	theme_operation_result(vm, st_theme_cloud_preview(
	    st_wren_host_from_vm(vm)->themes, package, error, sizeof(error)),
	    error);
}

static void
fn_menu_cache_cloud_theme(WrenVM *vm)
{
	const char *package;
	char error[256] = "";

	if (!theme_package(vm, 1, &package))
		return;
	theme_operation_result(vm, st_theme_cloud_cache(
	    st_wren_host_from_vm(vm)->themes, package, error, sizeof(error)),
	    error);
}

static void
fn_menu_select_cloud_theme(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct st_settings previous;
	const char *package;
	char error[256] = "";
	int status;
	bool native_status = true;

	if (!theme_package(vm, 1, &package))
		return;
	st_settings_init(&previous);
	if (!st_settings_copy(&previous, &st_settings)) {
		menu_error(vm, 0, 0, "The running settings could not be copied.");
		return;
	}
	status = st_theme_cloud_select(host->themes, package, error,
	    sizeof(error));
	if (status == 0 && (!replace_owned_string(&st_settings.theme_file, NULL)
	    || !replace_owned_string(&st_settings.theme_package, package))) {
		native_status = false;
		status = IO_ERROR_OUT_OF_MEMORY;
		(void)snprintf(error, sizeof(error), "out of memory");
	}
	if (status == 0 && !host->config.safe_mode)
		status = st_settings_save();
	if (status != 0 && error[0] == '\0')
		(void)snprintf(error, sizeof(error),
		    "unable to save the selected theme package");
	if (status != 0) {
		restore_theme_selection(host, &previous, error, sizeof(error));
	}
	else
		++host->settings_generation;
	st_settings_dispose(&previous);
	if (status != 0 && !native_status)
		menu_error(vm, 0, 0, error);
	else
		theme_operation_result(vm, status, error);
}

static void
fn_menu_delete_cloud_theme(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct st_settings previous;
	const char *package;
	char error[256] = "";
	bool selection_changed = false;
	bool selection_saved = false;
	int status;

	if (!theme_package(vm, 1, &package))
		return;
	if (host->config.safe_mode) {
		menu_error(vm, IO_ERROR_READ_ONLY, 0, "Themes cannot be deleted in safe mode.");
		return;
	}
	st_settings_init(&previous);
	if (!st_settings_copy(&previous, &st_settings)) {
		menu_error(vm, 0, 0, "The running settings could not be copied.");
		return;
	}
	if (st_settings.theme_package != NULL
	    && strcmp(st_settings.theme_package, package) == 0) {
		selection_changed = true;
		status = st_theme_select(host->themes, "", error, sizeof(error));
		if (status == 0) {
			free(st_settings.theme_file);
			st_settings.theme_file = NULL;
			free(st_settings.theme_package);
			st_settings.theme_package = NULL;
			status = st_settings_save();
			selection_saved = status == 0;
		}
	}
	else
		status = 0;
	if (status == 0)
		status = st_theme_cloud_delete(host->themes, package, error,
		    sizeof(error));
	if (status != 0 && selection_changed && !selection_saved) {
		restore_theme_selection(host, &previous, error, sizeof(error));
	}
	if (selection_changed && (status == 0 || selection_saved))
		++host->settings_generation;
	st_settings_dispose(&previous);
	theme_delete_result(vm, status, error, selection_saved);
}

static void
fn_menu_copy_cloud_theme(WrenVM *vm)
{
	struct st_theme_document *document = NULL;
	const char *package;
	char error[256] = "";
	int status;

	if (!theme_package(vm, 1, &package))
		return;
	status = st_theme_cloud_copy_document(&document,
	    st_wren_host_from_vm(vm)->themes, package, error, sizeof(error));
	if (status != 0) {
		menu_error(vm, status, status, error[0] != '\0' ? error
		    : "unable to copy the theme package");
		return;
	}
	push_theme_document(vm, document);
}

static void
fn_menu_open_url(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	const char *url;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_STRING) {
		st_wren_abort(vm, "Menu.openUrl expects a String");
		return;
	}
	if (!st_wren_text_slot(vm, 1, &url, NULL))
		return;
	if (host->config.safe_mode || url[0] == '\0') {
		wrenSetSlotBool(vm, 0, false);
		return;
	}
	if (st_wren_screen_status(vm, st_console_open_url(host->config.console, url), "Menu.openUrl"))
		wrenSetSlotBool(vm, 0, true);
}

static void
fn_menu_open_hyperlink(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct st_hyperlink hyperlink = {0};
	int64_t value;
	bool opened = false;
	int status = 0;

	if (!slot_integer(vm, 1, 1, UINT16_MAX, &value))
		return;
	if (!host->config.safe_mode) {
		status = st_hyperlink_registry_copy(host->config.hyperlinks, (uint16_t)value, &hyperlink);
		if (status == IO_ERROR_NOT_FOUND)
			status = 0;
		else if (status == 0) {
			if (st_wren_uri_openable(hyperlink.uri)) {
				status = st_console_open_url(host->config.console, hyperlink.uri);
				opened = status == 0;
			}
			st_hyperlink_dispose(&hyperlink);
		}
	}
	if (st_wren_screen_status(vm, status, "Menu.openHyperlink"))
		wrenSetSlotBool(vm, 0, opened);
}

static void
fn_menu_set_hyperlink_hover(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	int64_t value;
	bool present = false;
	enum st_console_pointer_shape shape = ST_CONSOLE_POINTER_TEXT;

	if (!slot_integer(vm, 1, 0, UINT16_MAX, &value))
		return;
	if (value != 0) {
		if (!st_wren_screen_status(vm, st_hyperlink_registry_contains(host->config.hyperlinks,
		    (uint16_t)value, &present), "Menu.setHyperlinkHover lookup")) return;
		if (present)
			shape = ST_CONSOLE_POINTER_LINK;
	}
	st_wren_screen_result(vm, st_console_set_pointer_shape(host->config.console, shape), "Menu.setHyperlinkHover");
}

static void
map_string(WrenVM *vm, const char *key, const char *value)
{
	wrenSetSlotString(vm, 1, key);
	wrenSetSlotString(vm, 2, value == NULL ? "" : value);
	wrenSetMapValue(vm, 0, 1, 2);
}

static void
fn_menu_file_locations(WrenVM *vm)
{
	const struct st_app_paths *paths = st_wren_host_from_vm(vm)->config.paths;

	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewMap(vm, 0);
	map_string(vm, "globalList", paths->shared_list);
	map_string(vm, "personalList", personal_list_path(&st_settings, paths));
	map_string(vm, "configuration", paths->settings_file);
	map_string(vm, "download", paths->download_directory);
	map_string(vm, "cache", paths->cache_directory);
	map_string(vm, "keys", paths->keys_file);
	map_string(vm, "scripts", paths->scripts_directory);
	map_string(vm, "themes", paths->themes_directory);
}

static void
fn_menu_max_path_length(WrenVM *vm)
{
	/* The path libraries allocate dynamically; this is an editor input cap. */
	wrenSetSlotDouble(vm, 0, 32767);
}

static void
fn_menu_wren_scripts_max_length(WrenVM *vm)
{
	wrenSetSlotDouble(vm, 0, ST_PROFILE_WREN_SCRIPTS_MAX);
}

static void
fn_menu_current_screen_mode(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	wrenSetSlotDouble(vm, 0, host->current_screen_mode);
}

static void
fn_menu_set_screen_mode(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	int64_t mode;

	if (!slot_integer(vm, 1, ST_SCREEN_MODE_80X25,
	    ST_SCREEN_MODE_AMIGA_640X400, &mode))
		return;
	st_wren_screen_result(vm, st_wren_host_set_screen_mode(host,
	    (enum st_screen_mode)mode), "Menu.setScreenMode");
}

static void amiga_push(WrenVM *vm, int slot, int class_slot, struct st_amiga_font_family_info *info)
{
	wrenGetVariable(vm, "syncterm_menu", "AmigaFontFamily", class_slot);
	struct foreign_amiga_font *font = wrenSetSlotNewForeign(vm, slot, class_slot, sizeof(*font));
	*font =
		(struct foreign_amiga_font){.header.kind = MENU_FOREIGN_AMIGA_FONT,
									.info = *info,
									.generation = st_wren_host_from_vm(vm)->amiga_font_generation};
	*info = (struct st_amiga_font_family_info){0};
}
static struct foreign_amiga_font *amiga_value(WrenVM *vm, int slot)
{
	if (!st_wren_foreign_is(vm, slot, "syncterm_menu", "AmigaFontFamily"))
		return NULL;
	struct foreign_amiga_font *font = wrenGetSlotForeign(vm, slot);
	return font->header.kind == MENU_FOREIGN_AMIGA_FONT ? font : NULL;
}
static int amiga_root(WrenVM *vm, char **root)
{
	const struct st_app_paths *paths = st_wren_host_from_vm(vm)->config.paths;
	return paths == NULL ? IO_ERROR_NOT_FOUND
						 : st_amiga_fonts_directory(root, paths->settings_file);
}
static void fn_menu_amiga_fonts(WrenVM *vm)
{
	char *root = NULL;
	struct st_amiga_fonts_catalog catalog = {0};
	int status = amiga_root(vm, &root);
	if (status == 0)
		status = st_amiga_fonts_catalog_read(&catalog, root);
	free(root);
	if (status != 0) {
		menu_error(vm, status, status, "The Amiga font catalog could not be read.");
		return;
	}
	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < catalog.count; ++i) {
		amiga_push(vm, 1, 2, &catalog.families[i]);
		wrenInsertInList(vm, 0, -1, 1);
	}
	st_amiga_fonts_catalog_dispose(&catalog);
}
static void fn_amiga_name(WrenVM *vm)
{
	struct foreign_amiga_font *font = amiga_value(vm, 0);
	if (font == NULL) {
		st_wren_abort(vm, "Invalid AmigaFontFamily");
		return;
	}
	wrenSetSlotString(vm, 0, font->info.name);
}
static void amiga_sizes(WrenVM *vm, bool installed)
{
	struct foreign_amiga_font *font = amiga_value(vm, 0);
	if (font == NULL) {
		st_wren_abort(vm, "Invalid AmigaFontFamily");
		return;
	}
	const unsigned *sizes = installed ? font->info.installed_sizes : font->info.sizes;
	size_t count = installed ? font->info.installed_count : font->info.count;
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < count; ++i) {
		wrenSetSlotDouble(vm, 1, sizes[i]);
		wrenInsertInList(vm, 0, -1, 1);
	}
}
static void fn_amiga_sizes(WrenVM *vm) { amiga_sizes(vm, false); }
static void fn_amiga_installed_sizes(WrenVM *vm) { amiga_sizes(vm, true); }
static void fn_amiga_source(WrenVM *vm)
{
	struct foreign_amiga_font *font = amiga_value(vm, 0);
	if (font == NULL) {
		st_wren_abort(vm, "Invalid AmigaFontFamily");
		return;
	}
	wrenSetSlotString(
		vm, 0, font->info.installed ? (font->info.bundled ? "mixed" : "installed") : "bundled");
}
static void fn_amiga_error(WrenVM *vm)
{
	struct foreign_amiga_font *font = amiga_value(vm, 0);
	if (font == NULL) {
		st_wren_abort(vm, "Invalid AmigaFontFamily");
		return;
	}
	menu_result(vm, font->info.error, true,
				"The installed Amiga font descriptor could not be read.");
}
static void fn_menu_install_amiga_font(WrenVM *vm)
{
	if (!menu_writable(vm, "Amiga fonts cannot be installed in safe mode."))
		return;
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	const char *initial = ".";
	struct fs_info directory;
	if (host->config.paths != NULL && host->config.paths->download_directory != NULL &&
		fs_get_info(&directory, host->config.paths->download_directory) == 0 &&
		directory.kind == FS_ENTRY_DIRECTORY)
		initial = host->config.paths->download_directory;
	struct st_wren_picker_call call = {
		.mode = ST_WREN_PICKER_FILE,
		.title = "Install Amiga font family (.font and its bitmap strikes)",
		.initial_path = initial,
		.mask = "*.font",
		.options = ST_WREN_PICKER_FILE_MUST_EXIST | ST_WREN_PICKER_PATH_MUST_EXIST |
				   ST_WREN_PICKER_MASK_LOCKED | ST_WREN_PICKER_ALLOW_ENTRY};
	if (!st_wren_picker_host_run(host, &call)) {
		st_wren_picker_call_dispose(&call);
		menu_error(vm, IO_ERROR_IO, IO_ERROR_IO, "The Amiga font picker failed.");
		return;
	}
	if (call.path_count == 0) {
		st_wren_picker_call_dispose(&call);
		wrenSetSlotNull(vm, 0);
		return;
	}
	char *root = NULL;
	int status = amiga_root(vm, &root);
	struct st_amiga_font_family_info installed = {0};
	if (status == 0)
		status = st_amiga_fonts_install(root, call.paths[0], false, &installed);
	if (status == IO_ERROR_ALREADY_EXISTS) {
		/* The menu VM is inside a foreign call. Confirmation belongs to
		 * the isolated picker VM, through its synchronous UI driver. */
		struct st_wren_picker_call confirm = {.mode = ST_WREN_PICKER_FILE,
											  .amiga_replacement = true,
											  .title = "Replace Amiga font family",
											  .initial_path = path_basename(call.paths[0])};
		if (!st_wren_picker_host_run(host, &confirm))
			status = IO_ERROR_IO;
		else if (confirm.confirmed)
			status = st_amiga_fonts_install(root, call.paths[0], true, &installed);
		else {
			free(root);
			st_wren_picker_call_dispose(&call);
			st_wren_picker_call_dispose(&confirm);
			wrenSetSlotNull(vm, 0);
			return;
		}
		st_wren_picker_call_dispose(&confirm);
	}
	if (status == 0)
		++host->amiga_font_generation;
	if (status == 0) {
		wrenEnsureSlots(vm, 2);
		amiga_push(vm, 0, 1, &installed);
	}
	free(root);
	st_wren_picker_call_dispose(&call);
	st_amiga_font_family_info_dispose(&installed);
	if (status != 0)
		menu_error(vm, status, status, "The Amiga font family could not be installed.");
}
static void fn_menu_remove_amiga_font(WrenVM *vm)
{
	if (!menu_writable(vm, "Amiga fonts cannot be removed in safe mode."))
		return;
	struct foreign_amiga_font *font = amiga_value(vm, 1);
	if (font == NULL) {
		st_wren_abort(vm, "Menu.removeAmigaFont expects an AmigaFontFamily");
		return;
	}
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	if (font->generation != host->amiga_font_generation) {
		menu_error(vm, IO_ERROR_BUSY, IO_ERROR_BUSY,
				   "Refresh the Amiga font catalog before removing a family.");
		return;
	}
	if (!font->info.installed) {
		menu_error(vm, IO_ERROR_READ_ONLY, IO_ERROR_READ_ONLY,
				   "Bundled Amiga fonts cannot be removed.");
		return;
	}
	char *root = NULL;
	int status = amiga_root(vm, &root);
	if (status == 0)
		status = st_amiga_fonts_remove(root, font->info.name);
	free(root);
	if (status == 0)
		++host->amiga_font_generation;
	menu_result(vm, status, true, "The Amiga font family could not be removed.");
}

static void
fn_menu_fonts_catalog(WrenVM *vm)
{
	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < st_builtin_font_count(); ++i)
		push_row(vm, 0, 1, 2, (double)i, st_builtin_font_name(i));
	for (size_t i = 0; i < st_settings_font_count(); ++i) {
		push_row(vm, 0, 1, 2, (double)(ST_BUILTIN_FONT_COUNT + i),
		    st_settings_font_at(i)->name);
	}
}

static void
fn_menu_encodings(WrenVM *vm)
{
	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	for (int i = 0; i < ST_CODEPAGE_COUNT; ++i) {
		if (st_profile_encoding_valid(i))
			push_row(vm, 0, 1, 2, (double)i,
			    st_profile_encoding_name(i));
	}
	push_row(vm, 0, 1, 2, ST_PROFILE_ENCODING_UTF8, "UTF-8");
}

static void
fn_menu_fonts_for_encoding(WrenVM *vm)
{
	int64_t encoding;

	if (!slot_integer(vm, 1, 0, ST_PROFILE_ENCODING_UTF8, &encoding))
		return;
	if (!st_profile_encoding_valid((int)encoding)) {
		st_wren_abort(vm, "invalid BBS encoding");
		return;
	}
	wrenEnsureSlots(vm, 4);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < st_builtin_font_count(); ++i) {
		const char *name = st_builtin_font_name(i);

		if (encoding == ST_PROFILE_ENCODING_UTF8
		    || st_profile_encoding_canonical(st_builtin_font_codepage(i))
		    == encoding)
			push_row(vm, 0, 2, 3, (double)i, name);
	}
	for (size_t i = 0; i < st_settings_font_count(); ++i) {
		const char *name = st_settings_font_at(i)->name;

		push_row(vm, 0, 2, 3,
		    (double)(ST_BUILTIN_FONT_COUNT + i), name);
	}
}

static void
fn_menu_font_encoding(WrenVM *vm)
{
	const char *name;

	if (!st_wren_text_slot(vm, 1, &name, NULL))
		return;
	for (size_t i = 0; i < st_builtin_font_count(); ++i) {
		if (strcmp(name, st_builtin_font_name(i)) == 0) {
			wrenSetSlotDouble(vm, 0, st_profile_encoding_canonical(
			    st_builtin_font_codepage(i)));
			return;
		}
	}
	wrenSetSlotNull(vm, 0);
}

static void
push_font(WrenVM *vm, int slot, int class_slot, size_t index)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct foreign_font *font = wrenSetSlotNewForeign(vm, slot, class_slot,
	    sizeof(*font));

	font->header.kind = MENU_FOREIGN_FONT;
	font->index = index;
	font->generation = host->font_generation;
}

static void
fn_menu_fonts(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);

	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	wrenGetVariable(vm, "syncterm_menu", "MenuFont", 1);
	for (size_t i = 0; i < st_settings_font_draft_count(&host->font_draft);
	    ++i) {
		push_font(vm, 2, 1, i);
		wrenInsertInList(vm, 0, -1, 2);
	}
}

static void
fn_menu_fonts_dirty(WrenVM *vm)
{
	wrenSetSlotBool(vm, 0, st_wren_host_from_vm(vm)->fonts_dirty);
}

static void
fn_menu_can_create_font(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);

	wrenSetSlotBool(vm, 0, !host->config.safe_mode
	    && st_settings_font_draft_count(&host->font_draft)
	    < ST_SETTINGS_CUSTOM_FONT_MAX);
}

static bool
slot_font_name(WrenVM *vm, int slot, const char **name)
{
	int length;
	const char *value;

	if (wrenGetSlotType(vm, slot) != WREN_TYPE_STRING) {
		st_wren_abort(vm, "font name must be a String");
		return false;
	}
	if (!st_wren_text_slot(vm, slot, &value, &length))
		return false;
	if (length == 0
	    || memchr(value, '\0', (size_t)length) != NULL
	    || !st_settings_font_name_valid(value)) {
		st_wren_abort(vm, "font name must contain 1 through 50 Unicode "
		    "scalars (100 UTF-8 bytes maximum)");
		return false;
	}
	*name = value;
	return true;
}

static bool
font_name_available(struct st_wren_host *host, const char *name,
    size_t except)
{
	bool equal;

	for (size_t i = 0;
	    i < st_settings_font_draft_count(&host->font_draft); ++i) {
		const struct st_settings_font *font
		    = st_settings_font_draft_at(&host->font_draft, i);

		if (i != except && st_unicode_caseless_equal(&equal, font->name,
		    strlen(font->name), name, strlen(name)) == 0 && equal)
			return false;
	}
	return true;
}

static void
fn_menu_create_font(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	const char *name;
	int64_t index;

	if (!menu_writable(vm, "Fonts cannot be created in safe mode."))
		return;
	if (st_settings_font_draft_count(&host->font_draft)
	    >= ST_SETTINGS_CUSTOM_FONT_MAX) {
		menu_error(vm, 0, 0, "No custom font slots are available.");
		return;
	}
	if (!slot_font_name(vm, 1, &name)
	    || !slot_integer(vm, 2, 0,
	    (int64_t)st_settings_font_draft_count(&host->font_draft),
	    &index))
		return;
	if (!font_name_available(host, name, SIZE_MAX)) {
		st_wren_abort(vm, "font name is already configured");
		return;
	}
	int status = st_settings_font_draft_insert(&host->font_draft, (size_t)index, name);
	if (status != 0) {
		menu_result(vm, status, false, "The font could not be created.");
		return;
	}
	host->font_generation++;
	host->fonts_dirty = true;
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm_menu", "MenuFont", 1);
	push_font(vm, 0, 1, (size_t)index);
}

static void
fn_menu_reload_fonts(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	int status = st_settings_font_draft_reload(&host->font_draft);

	if (status == 0) {
		host->font_generation++;
		host->fonts_dirty = false;
	}
	menu_result(vm, status, true, "The font draft could not be reloaded.");
}

static void
fn_menu_save_fonts(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	if (!menu_writable(vm, "Fonts cannot be saved in safe mode."))
		return;
	int status = st_settings_font_draft_save(&host->font_draft);
	if (status == 0)
		host->fonts_dirty = false;
	menu_result(vm, status, true, "The font configuration could not be saved.");
}

static void
fn_menu_activate_fonts(WrenVM *vm)
{
	if (menu_writable(vm, "Fonts cannot be activated in safe mode."))
		menu_result(vm, st_wren_reload_custom_fonts(st_wren_host_from_vm(vm)),
		    true, "The configured fonts could not be activated.");
}

static void
fn_font_name(WrenVM *vm)
{
	struct st_settings_font *font = font_value(vm, NULL);

	if (font != NULL)
		wrenSetSlotString(vm, 0, font->name);
}

static void
fn_font_name_set(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct foreign_font *handle;
	struct st_settings_font *font = font_value(vm, &handle);
	const char *name;

	(void)font;
	if (font == NULL || !slot_font_name(vm, 1, &name))
		return;
	if (host->config.safe_mode) {
		menu_error(vm, IO_ERROR_READ_ONLY, 0, "Fonts are read-only in safe mode.");
		wrenAbortFiber(vm, 0);
		return;
	}
	if (!font_name_available(host, name, handle->index)) {
		st_wren_abort(vm, "MenuFont.name: name is already configured");
		return;
	}
	int status = st_settings_font_draft_rename(&host->font_draft, handle->index, name);
	if (status != 0) {
		menu_result(vm, status, false, "The font could not be renamed.");
		wrenAbortFiber(vm, 0);
		return;
	}
	host->fonts_dirty = true;
}

static bool
slot_font_path(WrenVM *vm, int slot, enum st_settings_font_path *result)
{
	int64_t value;

	if (!slot_integer(vm, slot, 0, ST_SETTINGS_FONT_PATH_COUNT - 1, &value))
		return false;
	*result = (enum st_settings_font_path)value;
	return true;
}

static void
fn_font_path(WrenVM *vm)
{
	struct st_settings_font *font = font_value(vm, NULL);
	enum st_settings_font_path slot;

	if (font == NULL || !slot_font_path(vm, 1, &slot))
		return;
	if (font->path[slot] == NULL)
		wrenSetSlotNull(vm, 0);
	else
		wrenSetSlotString(vm, 0, font->path[slot]);
}

static void
fn_font_set_file(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct foreign_font *handle;
	struct st_settings_font *font = font_value(vm, &handle);
	enum st_settings_font_path slot;
	if (font == NULL || !slot_font_path(vm, 1, &slot))
		return;
	const char *path = st_wren_file_read_path(vm, 2);
	if (!menu_writable(vm, "Font files cannot be changed in safe mode."))
		return;
	if (path == NULL) {
		menu_error(vm, IO_ERROR_PERMISSION_DENIED, 0, "The font File has no live read grant.");
		return;
	}
	int status = st_settings_font_draft_set_path(&host->font_draft,
	    handle->index, slot, path);
	if (status == 0)
		host->fonts_dirty = true;
	menu_result(vm, status, false, "The font file could not be assigned.");
}

static void
fn_font_clear_file(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct foreign_font *handle;
	struct st_settings_font *font = font_value(vm, &handle);
	enum st_settings_font_path slot;
	int status;

	(void)font;
	if (font == NULL || !slot_font_path(vm, 1, &slot))
		return;
	if (!menu_writable(vm, "Fonts are read-only in safe mode."))
		return;
	status = st_settings_font_draft_set_path(&host->font_draft,
	    handle->index, slot, NULL);
	if (status == 0)
		host->fonts_dirty = true;
	menu_result(vm, status, false, "The font file could not be cleared.");
}

static void
fn_font_delete(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct foreign_font *handle;
	struct st_settings_font *font = font_value(vm, &handle);
	int status;

	(void)font;
	if (font == NULL)
		return;
	if (!menu_writable(vm, "Fonts are read-only in safe mode."))
		return;
	status = st_settings_font_draft_remove(&host->font_draft,
	    handle->index);
	if (status == 0) {
		host->font_generation++;
		host->fonts_dirty = true;
	}
	menu_result(vm, status, false, "The font could not be deleted.");
}

static void
fn_menu_palette_defaults(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	const struct st_screen_mode_descriptor *descriptor;
	int64_t mode;

	if (!slot_integer(vm, 1, ST_SCREEN_MODE_CURRENT,
	    ST_SCREEN_MODE_AMIGA_640X400, &mode))
		return;
	if (mode == ST_SCREEN_MODE_CURRENT)
		mode = host->current_screen_mode;
	descriptor = st_screen_mode_get((enum st_screen_mode)mode);
	if (descriptor == NULL || descriptor->mode == ST_SCREEN_MODE_CUSTOM)
		descriptor = st_screen_mode_get(ST_SCREEN_MODE_CURRENT);
	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	wrenSetSlotDouble(vm, 1, descriptor->palette_count);
	wrenInsertInList(vm, 0, -1, 1);
	wrenSetSlotNewList(vm, 1);
	for (size_t i = 0; i < 16; ++i) {
		wrenSetSlotDouble(vm, 2, descriptor->palette_rgb[i]);
		wrenInsertInList(vm, 1, -1, 2);
	}
	wrenInsertInList(vm, 0, -1, 1);
}

static const struct st_list_crypto_info *
menu_crypto_info(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);

	return host->bbs_list == NULL ? NULL :
	    st_bbs_list_crypto_info(host->bbs_list);
}

/* This is copied metadata: it has no password, resource, or model lifetime. */
static bool
encryption_info_valid(const struct st_list_crypto_info *info)
{
	if (info->format == ST_LIST_CRYPTO_PLAIN)
		return info->cipher == ST_LIST_CIPHER_NONE
		    && info->kdf == ST_LIST_KDF_NONE && info->key_bits == 0;
	if (info->format != ST_LIST_CRYPTO_V1 && info->format != ST_LIST_CRYPTO_V2)
		return false;
	if (info->key_bits == 0 || info->key_bits % 8 != 0)
		return false;
	switch (info->cipher) {
	case ST_LIST_CIPHER_3DES:
		if (info->key_bits != 192) return false;
		break;
	case ST_LIST_CIPHER_IDEA:
		if (info->key_bits != 128) return false;
		break;
	case ST_LIST_CIPHER_CAST:
		if (info->key_bits < 40 || info->key_bits > 128) return false;
		break;
	case ST_LIST_CIPHER_RC2:
	case ST_LIST_CIPHER_RC4:
		if (info->key_bits > 256) return false;
		break;
	case ST_LIST_CIPHER_AES:
		if (info->key_bits != 128 && info->key_bits != 192
		    && info->key_bits != 256) return false;
		break;
	case ST_LIST_CIPHER_CHACHA20:
		if (info->key_bits != 256) return false;
		break;
	default: return false;
	}
	if (info->kdf == ST_LIST_KDF_PBKDF2_SHA256)
		return info->iterations > 0 && st_wren_u64_exact(info->iterations);
	if (info->kdf == ST_LIST_KDF_SCRYPT)
		return info->format == ST_LIST_CRYPTO_V2
		    && info->cost_log2 >= 8 && info->cost_log2 < 64
		    && st_kdf_scrypt_parameters_valid(UINT64_C(1) << info->cost_log2,
		    info->block_size, info->parallelism);
	return false;
}

static void
fn_menu_encryption_info(WrenVM *vm)
{
	const struct st_list_crypto_info *info = menu_crypto_info(vm);

	if (info == NULL) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	if (!encryption_info_valid(info)) {
		st_wren_abort(vm, "Invalid native list encryption metadata");
		return;
	}
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm_menu", "ListEncryptionInfo", 1);
	struct foreign_encryption_info *snapshot = wrenSetSlotNewForeign(vm, 0, 1,
	    sizeof(*snapshot));
	snapshot->header.kind = MENU_FOREIGN_ENCRYPTION_INFO;
	snapshot->info = *info;
}

static const struct st_list_crypto_info *
encryption_info_value(WrenVM *vm)
{
	if (!st_wren_foreign_is(vm, 0, "syncterm_menu", "ListEncryptionInfo")) {
		st_wren_abort(vm, "expected ListEncryptionInfo");
		return NULL;
	}
	struct foreign_encryption_info *snapshot = wrenGetSlotForeign(vm, 0);

	if (snapshot == NULL || snapshot->header.kind != MENU_FOREIGN_ENCRYPTION_INFO) {
		st_wren_abort(vm, "expected ListEncryptionInfo");
		return NULL;
	}
	return &snapshot->info;
}

#define ENCRYPTION_INFO_NUMBER(name, field) \
	static void fn_encryption_info_##name(WrenVM *vm) \
	{ \
		const struct st_list_crypto_info *info = encryption_info_value(vm); \
		if (info != NULL) st_wren_return_u64(vm, info->field); \
	}
ENCRYPTION_INFO_NUMBER(format, format)
ENCRYPTION_INFO_NUMBER(algorithm, cipher)
ENCRYPTION_INFO_NUMBER(key_bits, key_bits)
ENCRYPTION_INFO_NUMBER(kdf, kdf)
#undef ENCRYPTION_INFO_NUMBER

#define ENCRYPTION_INFO_KDF_NUMBER(name, field, applicable) \
	static void fn_encryption_info_##name(WrenVM *vm) \
	{ \
		const struct st_list_crypto_info *info = encryption_info_value(vm); \
		if (info == NULL) return; \
		if (info->kdf == applicable) st_wren_return_u64(vm, info->field); \
		else wrenSetSlotNull(vm, 0); \
	}
ENCRYPTION_INFO_KDF_NUMBER(iterations, iterations, ST_LIST_KDF_PBKDF2_SHA256)
ENCRYPTION_INFO_KDF_NUMBER(cost_log2, cost_log2, ST_LIST_KDF_SCRYPT)
ENCRYPTION_INFO_KDF_NUMBER(block_size, block_size, ST_LIST_KDF_SCRYPT)
ENCRYPTION_INFO_KDF_NUMBER(parallelism, parallelism, ST_LIST_KDF_SCRYPT)
#undef ENCRYPTION_INFO_KDF_NUMBER

static void
fn_encryption_info_rewrite_recommended(WrenVM *vm)
{
	const struct st_list_crypto_info *info = encryption_info_value(vm);

	if (info != NULL)
		wrenSetSlotBool(vm, 0, info->rewrite_recommended);
}

static void
fn_encryption_info_name(WrenVM *vm)
{
	const struct st_list_crypto_info *info = encryption_info_value(vm);
	const char *name;
	bool variable = false;
	char display[32];

	if (info == NULL)
		return;
	switch (info->cipher) {
	case ST_LIST_CIPHER_NONE: name = "Not Encrypted"; break;
	case ST_LIST_CIPHER_3DES: name = "3DES"; break;
	case ST_LIST_CIPHER_IDEA: name = "IDEA"; break;
	case ST_LIST_CIPHER_CAST: name = "CAST"; variable = true; break;
	case ST_LIST_CIPHER_RC2: name = "RC2"; variable = true; break;
	case ST_LIST_CIPHER_RC4: name = "RC4"; variable = true; break;
	case ST_LIST_CIPHER_AES: name = "AES"; variable = true; break;
	case ST_LIST_CIPHER_CHACHA20: name = "ChaCha20"; break;
	default:
		st_wren_abort(vm, "Invalid native list encryption cipher");
		return;
	}
	if (variable) {
		(void)snprintf(display, sizeof(display), "%s-%" PRIu32, name, info->key_bits);
		wrenSetSlotString(vm, 0, display);
	}
	else
		wrenSetSlotString(vm, 0, name);
}

static uint8_t
settings_kdf_shift(const struct st_settings *settings)
{
	const char *text = settings->key_derivation_iterations;

	if (strncmp(text, "scrypt-N", 8) == 0 && text[8] >= '0'
	    && text[8] <= '9') {
		unsigned parsed = 0;

		text += 8;
		while (*text >= '0' && *text <= '9' && parsed <= 100) {
			parsed = parsed * 10 + (unsigned)(*text - '0');
			++text;
		}
		if (*text == '\0' && parsed >= 8 && parsed <= 20)
			return (uint8_t)parsed;
	}
	return 15;
}

static void
fn_menu_set_encryption(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct st_list_crypto_options options = ST_LIST_CRYPTO_OPTIONS_AES256;
	const char *password = NULL;
	int password_length = 0;
	int64_t algorithm;
	int64_t key_size;
	int status;

	if (host->bbs_list == NULL
	    || !slot_integer(vm, 1, ST_LIST_CIPHER_NONE,
	    ST_LIST_CIPHER_CHACHA20, &algorithm)
	    || !slot_integer(vm, 2, 0, 256, &key_size)) {
		menu_error(vm, IO_ERROR_INVALID_ARGUMENT, 0, "The encryption request is invalid.");
		return;
	}
	if (algorithm != ST_LIST_CIPHER_NONE && algorithm != ST_LIST_CIPHER_AES
	    && algorithm != ST_LIST_CIPHER_CHACHA20) {
		menu_error(vm, IO_ERROR_INVALID_ARGUMENT, 0, "The encryption request is invalid.");
		return;
	}
	if ((algorithm == ST_LIST_CIPHER_AES
	    && key_size != 128 && key_size != 256)
	    || (algorithm == ST_LIST_CIPHER_CHACHA20
	    && key_size != 0 && key_size != 256)) {
		menu_error(vm, IO_ERROR_INVALID_ARGUMENT, 0, "The encryption request is invalid.");
		return;
	}
	if (wrenGetSlotType(vm, 3) == WREN_TYPE_STRING)
		password = wrenGetSlotBytes(vm, 3, &password_length);
	else if (wrenGetSlotType(vm, 3) != WREN_TYPE_NULL) {
		st_wren_abort(vm, "Menu.setEncryption expects a String or null");
		return;
	}
	if (!menu_writable(vm, "List encryption cannot be changed in safe mode."))
		return;
	options.cipher = (enum st_list_cipher)algorithm;
	options.key_bits = algorithm == ST_LIST_CIPHER_CHACHA20
	    ? 256 : (uint32_t)key_size;
	options.cost_log2 = settings_kdf_shift(&st_settings);
	if (password == NULL && algorithm != ST_LIST_CIPHER_NONE)
		status = st_bbs_list_rewrite_encryption_current_password(
		    host->bbs_list, &options);
	else
		status = st_bbs_list_rewrite_encryption(host->bbs_list, &options,
		    password, (size_t)password_length);
	menu_result(vm, status, true, "The personal directory could not be rewritten.");
}

static void
fn_menu_web_lists(WrenVM *vm)
{
	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < st_settings_web_download_count(); ++i) {
		const struct name_value *item = st_settings_web_download_at(i);

		if (item == NULL)
			continue;
		wrenSetSlotNewList(vm, 1);
		wrenSetSlotString(vm, 2, item->name);
		wrenInsertInList(vm, 1, -1, 2);
		wrenSetSlotString(vm, 2, item->value);
		wrenInsertInList(vm, 1, -1, 2);
		wrenInsertInList(vm, 0, -1, 1);
	}
}

static void
fn_menu_web_lists_dirty(WrenVM *vm)
{
	wrenSetSlotBool(vm, 0, st_wren_host_from_vm(vm)->web_lists_dirty);
}

static bool
slot_c_string(WrenVM *vm, int slot, const char **value)
{
	int length;

	if (!st_wren_text_slot(vm, slot, value, &length))
		return false;
	if (length < 0 || memchr(*value, '\0', (size_t)length) != NULL) {
		st_wren_abort(vm, "String contains NUL");
		return false;
	}
	return true;
}

static void
fn_menu_add_web_list(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	const char *name;
	const char *url;
	int64_t index;
	size_t count = st_settings_web_download_count();
	size_t added_index = count;
	bool inserted;
	int status;

	if (!slot_c_string(vm, 1, &name) || !slot_c_string(vm, 2, &url)
	    || !slot_integer(vm, 3, 0, (int64_t)count, &index))
		return;
	status = st_settings_web_download_append(name, url);
	inserted = status == 0;
	if (status == 0 && (size_t)index != count) {
		status = st_settings_web_download_move(count, (size_t)index);
		if (status == 0)
			added_index = (size_t)index;
	}
	if (status == 0)
		status = host->bbs_list == NULL ? IO_ERROR_INVALID_ARGUMENT
		    : st_bbs_list_refresh_web_at(host->bbs_list, added_index,
		    NULL, NULL);
	if (status != 0 && inserted)
		(void)st_settings_web_download_remove(added_index);
	if (status == 0) {
		host->web_lists_dirty = true;
		wrenSetSlotNull(vm, 0);
	}
	else
		menu_result(vm, status, true, "The web list could not be added.");
}

static void
fn_menu_update_web_list(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	const struct name_value *item;
	const char *url;
	int64_t index;
	int status = IO_ERROR_INVALID_ARGUMENT;

	if (!slot_integer(vm, 1, 0,
	    (int64_t)st_settings_web_download_count() - 1, &index)
	    || !slot_c_string(vm, 2, &url))
		return;
	item = st_settings_web_download_at((size_t)index);
	if (item != NULL)
		status = st_settings_web_download_set((size_t)index, item->name, url);
	if (status == 0)
		host->web_lists_dirty = true;
	menu_result(vm, status, true, "The web-list URI could not be updated.");
}

static void
fn_menu_delete_web_list(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	int64_t index;
	int status;

	if (!slot_integer(vm, 1, 0,
	    (int64_t)st_settings_web_download_count() - 1, &index))
		return;
	status = st_settings_web_download_remove((size_t)index);
	if (status == 0)
		host->web_lists_dirty = true;
	menu_result(vm, status, true, "The web list could not be deleted.");
}

static void
fn_menu_save_web_lists(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	if (!menu_writable(vm, "Web lists cannot be saved in safe mode."))
		return;
	int status = st_settings_save();

	if (status == 0)
		host->web_lists_dirty = false;
	menu_result(vm, status, true, "The web-list configuration could not be saved.");
}

static void
fn_menu_refresh_web_list(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	int64_t index;

	if (!slot_integer(vm, 1, 0,
	    (int64_t)st_settings_web_download_count() - 1, &index))
		return;
	int status = host->bbs_list == NULL ? IO_ERROR_INVALID_ARGUMENT
	    : st_bbs_list_refresh_web_at(host->bbs_list, (size_t)index, NULL, NULL);
	menu_result(vm, status, true, "The web list could not be refreshed.");
}

static void
fn_menu_settings(WrenVM *vm)
{
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm_menu", "Settings", 1);
	struct foreign_settings *settings = wrenSetSlotNewForeign(vm, 0, 1,
	    sizeof(*settings));

	memset(settings, 0, sizeof(*settings));
	settings->header.kind = MENU_FOREIGN_SETTINGS;
	settings->generation = st_wren_host_from_vm(vm)->settings_generation;
	st_settings_init(&settings->settings);
	if (!st_settings_copy(&settings->settings, &st_settings))
		st_wren_abort(vm, "out of memory copying settings");
}

#define SETTINGS_FIXED_PROPERTY(name, field) \
	static void fn_settings_##name(WrenVM *vm) \
	{ \
		struct foreign_settings *settings = settings_value(vm); \
		if (settings != NULL) \
			wrenSetSlotString(vm, 0, settings->settings.field); \
	} \
	static void fn_settings_##name##_set(WrenVM *vm) \
	{ \
		struct foreign_settings *settings = settings_value(vm); \
		if (settings != NULL && slot_fixed_string(vm, 1, \
		    settings->settings.field, sizeof(settings->settings.field))) \
			settings->dirty = true; \
	}

#define SETTINGS_OWNED_PROPERTY(name, field) \
	static void fn_settings_##name(WrenVM *vm) \
	{ \
		struct foreign_settings *settings = settings_value(vm); \
		if (settings != NULL) \
			wrenSetSlotString(vm, 0, settings->settings.field == NULL ? \
			    "" : settings->settings.field); \
	} \
	static void fn_settings_##name##_set(WrenVM *vm) \
	{ \
		struct foreign_settings *settings = settings_value(vm); \
		if (settings != NULL && slot_owned_string(vm, 1, \
		    &settings->settings.field)) \
			settings->dirty = true; \
	}

#define SETTINGS_BOOL_PROPERTY(name, field) \
	static void fn_settings_##name(WrenVM *vm) \
	{ \
		struct foreign_settings *settings = settings_value(vm); \
		if (settings != NULL) \
			wrenSetSlotBool(vm, 0, settings->settings.field != 0); \
	} \
	static void fn_settings_##name##_set(WrenVM *vm) \
	{ \
		struct foreign_settings *settings = settings_value(vm); \
		bool value; \
		if (settings != NULL && slot_bool(vm, 1, &value)) { \
			settings->settings.field = value; \
			settings->dirty = true; \
		} \
	}

#define SETTINGS_NUM_PROPERTY(name, field, type, minimum, maximum) \
	static void fn_settings_##name(WrenVM *vm) \
	{ \
		struct foreign_settings *settings = settings_value(vm); \
		if (settings != NULL) \
			wrenSetSlotDouble(vm, 0, (double)settings->settings.field); \
	} \
	static void fn_settings_##name##_set(WrenVM *vm) \
	{ \
		struct foreign_settings *settings = settings_value(vm); \
		int64_t value; \
		if (settings != NULL && slot_integer(vm, 1, minimum, maximum, \
		    &value)) { \
			settings->settings.field = (type)value; \
			settings->dirty = true; \
		} \
	}

SETTINGS_BOOL_PROPERTY(confirm_close, confirm_close)
SETTINGS_BOOL_PROPERTY(prompt_save, prompt_save)
SETTINGS_BOOL_PROPERTY(invert_wheel, invert_wheel)
SETTINGS_BOOL_PROPERTY(color_emoji, color_emoji)
SETTINGS_BOOL_PROPERTY(atari_st_key_clicks, atari_st_key_clicks)
SETTINGS_OWNED_PROPERTY(modem_device, modem_device)
SETTINGS_FIXED_PROPERTY(modem_init, modem_init_string)
SETTINGS_FIXED_PROPERTY(modem_dial, modem_dial_string)
SETTINGS_OWNED_PROPERTY(list_path, stored_list_path)
SETTINGS_FIXED_PROPERTY(shell_term, terminal_type)
SETTINGS_OWNED_PROPERTY(tls_client_cert, tls_client_certificate)
SETTINGS_OWNED_PROPERTY(tls_client_key, tls_client_key)
SETTINGS_NUM_PROPERTY(startup_mode, startup_mode, int, ST_SCREEN_MODE_CURRENT,
    ST_SCREEN_MODE_AMIGA_640X400)
SETTINGS_NUM_PROPERTY(output_mode, output_mode, int, 0, INT_MAX)
SETTINGS_NUM_PROPERTY(cursor_style, default_cursor, int, ST_CURSOR_DEFAULT,
    ST_CURSOR_SOLID_BLOCK)
SETTINGS_NUM_PROPERTY(audio_modes, audio_output_modes, uint32_t, 0, UINT32_MAX)
SETTINGS_NUM_PROPERTY(midi_modes, midi_output_modes, uint32_t, 0, UINT32_MAX)
SETTINGS_NUM_PROPERTY(scrollback_lines, scrollback_lines, int, 1, INT_MAX)
SETTINGS_NUM_PROPERTY(transfer_success_keypress_timeout,
	transfer_success_keypress_timeout, int, 0, INT_MAX)
SETTINGS_NUM_PROPERTY(transfer_failure_keypress_timeout,
	transfer_failure_keypress_timeout, int, 0, INT_MAX)
SETTINGS_NUM_PROPERTY(modem_rate, modem_rate, uint32_t, 0, UINT32_MAX)
SETTINGS_NUM_PROPERTY(custom_rows, custom_rows, int, 14, 1000)
SETTINGS_NUM_PROPERTY(custom_columns, custom_columns, int, 40, 1000)
SETTINGS_NUM_PROPERTY(custom_font_height, custom_font_height, int, 8, 64)
SETTINGS_NUM_PROPERTY(custom_aspect_width, custom_aspect_width, int, 1, INT_MAX)
SETTINGS_NUM_PROPERTY(custom_aspect_height, custom_aspect_height, int, 1, INT_MAX)
SETTINGS_NUM_PROPERTY(frame_color, theme_frame_color, unsigned, 0, 16)
SETTINGS_NUM_PROPERTY(text_color, theme_text_color, unsigned, 0, 16)
SETTINGS_NUM_PROPERTY(background_color, theme_background_color, unsigned, 0, 8)
SETTINGS_NUM_PROPERTY(inverse_color, theme_inverse_color, unsigned, 0, 8)
SETTINGS_NUM_PROPERTY(lightbar_color, theme_lightbar_color, unsigned, 0, 16)
SETTINGS_NUM_PROPERTY(lightbar_background_color,
    theme_lightbar_background_color, unsigned, 0, 8)

static void
fn_settings_scaling_mode(WrenVM *vm)
{
	struct foreign_settings *settings = settings_value(vm);

	if (settings != NULL)
		wrenSetSlotDouble(vm, 0, settings->settings.external_scaling ? 2 :
		    settings->settings.blocky_scaling ? 0 : 1);
}

static void
fn_settings_scaling_mode_set(WrenVM *vm)
{
	struct foreign_settings *settings = settings_value(vm);
	int64_t value;

	if (settings != NULL && slot_integer(vm, 1, 0, 2, &value)) {
		settings->settings.blocky_scaling = value == 0;
		settings->settings.external_scaling = value == 2;
		settings->dirty = true;
	}
}

static void
fn_settings_kdf_shift(WrenVM *vm)
{
	struct foreign_settings *settings = settings_value(vm);

	if (settings == NULL) return;
	wrenSetSlotDouble(vm, 0, settings_kdf_shift(&settings->settings));
}

static void
fn_settings_kdf_shift_set(WrenVM *vm)
{
	struct foreign_settings *settings = settings_value(vm);
	int64_t value;

	if (settings != NULL && slot_integer(vm, 1, 8, 20, &value)) {
		(void)snprintf(settings->settings.key_derivation_iterations,
		    sizeof(settings->settings.key_derivation_iterations),
		    "scrypt-N%d", (int)value);
		settings->dirty = true;
	}
}

static void
fn_settings_dirty(WrenVM *vm)
{
	struct foreign_settings *settings = settings_value(vm);

	if (settings != NULL)
		wrenSetSlotBool(vm, 0, settings->dirty);
}

static int
apply_console_settings(st_console_t *console, const struct st_settings *settings,
	const struct st_screen_mode_descriptor *mode)
{
	struct st_console_custom_cursor cursor;
	uint32_t width;
	uint32_t height;

	if (console == NULL) return 0;
	int status = st_console_cell_pixel_size(console, &width, &height);
	if (status != 0) return status;
	int result;
	(void)width;
	cursor = mode->console.default_cursor;
	if (settings->default_cursor == ST_CURSOR_BLINK_UNDERLINE
	    || settings->default_cursor == ST_CURSOR_SOLID_UNDERLINE) {
		cursor.start = height - (height > 8 ? 2U : 1U);
		cursor.end = height - 1;
		cursor.range = height;
	}
	if (settings->default_cursor == ST_CURSOR_BLINK_BLOCK
	    || settings->default_cursor == ST_CURSOR_SOLID_BLOCK) {
		cursor.start = 0;
		cursor.end = height - 1;
		cursor.range = height;
	}
	cursor.visible = true;
	cursor.blink = true;
	if (settings->default_cursor == ST_CURSOR_SOLID_UNDERLINE
	    || settings->default_cursor == ST_CURSOR_SOLID_BLOCK)
		cursor.blink = false;
	result = st_console_set_default_cursor(console, &cursor);
	if (status == 0) status = result;
	result = st_console_set_color_emoji(console, settings->color_emoji);
	if (status == 0) status = result;
	result = st_console_set_blocky_scaling(console,
	    settings->blocky_scaling != 0);
	if (status == 0) status = result;
	result = st_console_set_scaling_mode(console,
	    settings->external_scaling ? ST_CONSOLE_SCALING_EXTERNAL :
	    ST_CONSOLE_SCALING_INTERNAL);
	if (status == 0) status = result;
	return status;
}

struct settings_failure {
	int status;
	bool portable;
	const char *message;
};

struct prepared_settings {
	struct st_settings installed;
	struct st_theme_prepared *theme;
	st_bbs_list_t *bbs_list;
	struct st_wren_bbs_model bbs_model;
	bool replace_bbs;
	bool rewrite_kdf;
	bool reconfigure_custom;
	struct st_list_crypto_options crypto;
};

static bool
same_optional_string(const char *left, const char *right)
{
	if (left == NULL || left[0] == '\0')
		return right == NULL || right[0] == '\0';
	return right != NULL && strcmp(left, right) == 0;
}

static bool
theme_settings_changed(const struct st_settings *candidate)
{
	if (!same_optional_string(candidate->theme_file, st_settings.theme_file)
	    || !same_optional_string(candidate->theme_package,
	    st_settings.theme_package))
		return true;
	if ((candidate->theme_file != NULL && candidate->theme_file[0] != '\0')
	    || (candidate->theme_package != NULL
	    && candidate->theme_package[0] != '\0'))
		return false;
	return candidate->theme_frame_color != st_settings.theme_frame_color
	    || candidate->theme_text_color != st_settings.theme_text_color
	    || candidate->theme_background_color
	    != st_settings.theme_background_color
	    || candidate->theme_inverse_color != st_settings.theme_inverse_color
	    || candidate->theme_lightbar_color
	    != st_settings.theme_lightbar_color
	    || candidate->theme_lightbar_background_color
	    != st_settings.theme_lightbar_background_color;
}

static void
prepared_settings_dispose(struct prepared_settings *prepared)
{
	st_settings_dispose(&prepared->installed);
	st_theme_prepared_free(&prepared->theme);
	st_bbs_list_close(prepared->bbs_list);
	st_wren_bbs_model_dispose(&prepared->bbs_model);
	memset(prepared, 0, sizeof(*prepared));
}

static bool
resolve_settings_list_path(struct st_settings *settings,
	const struct st_app_paths *paths)
{
	const char *stored = settings->stored_list_path;
	const char *resolved;
	bool download;

	if (stored == NULL || stored[0] == '\0')
		stored = paths->personal_list;
	download = ascii_case_starts_with(stored, "http://")
	    || ascii_case_starts_with(stored, "https://");
	resolved = download ? paths->cached_system_list : stored;
	if (!replace_owned_string(&settings->list_path, resolved))
		return false;
	settings->download_system_list = download;
	return true;
}

static const char *
personal_list_path(const struct st_settings *settings,
	const struct st_app_paths *paths)
{
	return settings->list_path != NULL && !settings->download_system_list
	    ? settings->list_path : paths->personal_list;
}

static bool
prepare_kdf(struct prepared_settings *prepared,
	const struct st_wren_host *host, const struct st_settings *candidate)
{
	const struct st_list_crypto_info *info;
	unsigned shift;

	if (host->bbs_list == NULL || strcmp(candidate->key_derivation_iterations,
	    st_settings.key_derivation_iterations) == 0)
		return true;
	if (sscanf(candidate->key_derivation_iterations, "scrypt-N%u", &shift)
	    != 1 || shift < 8 || shift > 20)
		return false;
	info = st_bbs_list_crypto_info(host->bbs_list);
	if (info == NULL || info->format == ST_LIST_CRYPTO_PLAIN)
		return true;
	prepared->crypto = (struct st_list_crypto_options)
	    ST_LIST_CRYPTO_OPTIONS_AES256;
	if (info->format == ST_LIST_CRYPTO_V2) {
		prepared->crypto.cipher = info->cipher;
		prepared->crypto.key_bits = info->key_bits;
		prepared->crypto.block_size = info->block_size;
		prepared->crypto.parallelism = info->parallelism;
	}
	prepared->crypto.cost_log2 = (uint8_t)shift;
	prepared->rewrite_kdf = true;
	return true;
}

static bool
prepare_settings_change(WrenVM *vm, struct foreign_settings *settings,
	struct prepared_settings *prepared, struct settings_failure *failure)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	int fields[32];
	size_t field_count;
	char error[256] = "";
	int status;

	*failure = (struct settings_failure){.message = "The settings could not be prepared."};
	memset(prepared, 0, sizeof(*prepared));
	st_settings_init(&prepared->installed);
	st_wren_bbs_model_init(&prepared->bbs_model);
	if (!st_settings_copy(&prepared->installed, &settings->settings)
	    || !resolve_settings_list_path(&prepared->installed,
	    host->config.paths))
		goto failed;
	prepared->reconfigure_custom
	    = host->current_screen_mode == ST_SCREEN_MODE_CUSTOM
	    && (prepared->installed.custom_columns != st_settings.custom_columns
	    || prepared->installed.custom_rows != st_settings.custom_rows
	    || prepared->installed.custom_font_height
	    != st_settings.custom_font_height
	    || prepared->installed.custom_aspect_width
	    != st_settings.custom_aspect_width
	    || prepared->installed.custom_aspect_height
	    != st_settings.custom_aspect_height);
	status = 0;
	if (theme_settings_changed(&prepared->installed))
		status = st_theme_prepare_settings(&prepared->theme, host->themes,
		    &prepared->installed, error, sizeof(error));
	if (status != 0) {
		*failure = (struct settings_failure){status, true, "The settings theme could not be prepared."};
		goto failed;
	}
	if (!prepare_kdf(prepared, host, &prepared->installed))
		goto failed;
	if (host->bbs_list != NULL && strcmp(personal_list_path(&st_settings,
	    host->config.paths), personal_list_path(&prepared->installed,
	    host->config.paths)) != 0) {
		status = st_bbs_list_open_replacement(&prepared->bbs_list,
		    host->bbs_list, host->config.paths, &prepared->installed);
		field_count = sort_fields(prepared->installed.sort_order, fields);
		if (status == 0)
			status = st_bbs_list_sort(prepared->bbs_list, fields,
			    field_count);
		if (status == 0)
			status = st_wren_bbs_model_build(&prepared->bbs_model,
			    prepared->bbs_list);
		if (status != 0) {
			*failure = (struct settings_failure){status, true, "The replacement directory could not be prepared."};
			goto failed;
		}
		prepared->replace_bbs = true;
	}
	return true;

failed:
	prepared_settings_dispose(prepared);
	return false;
}

static int
rewrite_settings_kdf(struct st_wren_host *host,
	const struct prepared_settings *prepared)
{
	if (!prepared->rewrite_kdf) return 0;
	if (host->config.safe_mode) return IO_ERROR_READ_ONLY;
	return st_bbs_list_rewrite_encryption_current_password(host->bbs_list,
	    &prepared->crypto);
}

static int
install_prepared_settings(struct st_wren_host *host,
	struct foreign_settings *settings, struct prepared_settings *prepared,
	bool settings_already_installed)
{
	int status = 0;
	int result;
	if (!settings_already_installed)
		st_settings_install(&prepared->installed);
	st_theme_install_prepared(host->themes, &prepared->theme);
	if (prepared->replace_bbs) {
		st_bbs_list_t *old_list = host->bbs_list;

		host->bbs_list = prepared->bbs_list;
		prepared->bbs_list = NULL;
		st_wren_bbs_model_replace(&host->bbs_model, &prepared->bbs_model);
		st_bbs_list_close(old_list);
	}
	if (prepared->reconfigure_custom)
		status = st_wren_host_set_screen_mode(host, ST_SCREEN_MODE_CUSTOM);
	result = apply_console_settings(host->config.console, &st_settings,
	    &host->current_mode);
	if (status == 0) status = result;
	if (host->config.picker_console != NULL) {
		result = apply_console_settings(host->config.picker_console, &st_settings,
		    &host->current_mode);
		if (status == 0) status = result;
	}
	if (host->config.session_history != NULL) {
		result = st_session_history_set_capacity(host->config.session_history,
		    (size_t)st_settings.scrollback_lines);
		if (status == 0) status = result;
	}
	st_audio_runtime_set_allowed(host->config.audio_runtime,
	    st_settings.audio_output_modes);
	st_audio_runtime_set_midi_allowed(host->config.audio_runtime,
	    st_settings.midi_output_modes);
	for (struct st_wren_context *context = host->poll_contexts;
	    context != NULL; context = context->poll_next)
		st_cterm_set_atari_st_key_clicks(context->cterm,
		    st_settings.atari_st_key_clicks);
	++host->settings_generation;
	settings->generation = host->settings_generation;
	settings->dirty = false;
	return status;
}

static void
settings_change(WrenVM *vm, struct foreign_settings *settings, bool save)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct prepared_settings prepared;
	struct settings_failure failure;
	if (settings == NULL) return;
	if (save && !menu_writable(vm, "Settings cannot be saved in safe mode."))
		return;
	if (!prepare_settings_change(vm, settings, &prepared, &failure)) {
		menu_error(vm, failure.portable ? failure.status : 0,
		    failure.status, failure.message);
		return;
	}
	int status = rewrite_settings_kdf(host, &prepared);
	if (status != 0) {
		if (host->config.safe_mode)
			menu_error(vm, IO_ERROR_READ_ONLY, 0, "The list KDF cannot be rewritten in safe mode.");
		else
			menu_result(vm, status, true, "The list KDF could not be rewritten.");
	}
	else {
		if (save) status = st_settings_save_value(&prepared.installed);
		if (status != 0)
			menu_result(vm, status, true, prepared.rewrite_kdf
			    ? "The list KDF was rewritten, but the settings could not be saved."
			    : "The settings could not be saved.");
		else {
			status = install_prepared_settings(host, settings, &prepared, save);
			menu_result(vm, status, true, save
			    ? "The settings were saved and installed, but runtime application failed."
			    : "The settings were installed, but runtime application failed.");
		}
	}
	prepared_settings_dispose(&prepared);
}

static void
fn_settings_apply(WrenVM *vm)
{
	settings_change(vm, settings_value(vm), false);
}

static void
fn_settings_save(WrenVM *vm)
{
	settings_change(vm, settings_value(vm), true);
}

static void
fn_settings_reload(WrenVM *vm)
{
	struct foreign_settings *settings = settings_value(vm);
	struct st_settings replacement;

	if (settings == NULL) return;
	st_settings_init(&replacement);
	if (!st_settings_copy(&replacement, &st_settings)) {
		menu_error(vm, 0, 0, "The running settings could not be copied.");
		return;
	}
	st_settings_dispose(&settings->settings);
	settings->settings = replacement;
	settings->dirty = false;
	wrenSetSlotNull(vm, 0);
}

#define BBS_FIXED_PROPERTY(name, field) \
	static void fn_bbs_##name(WrenVM *vm) \
	{ \
		struct foreign_bbs *bbs = bbs_value(vm, 0); \
		if (bbs != NULL) \
			wrenSetSlotString(vm, 0, bbs->record->profile.field); \
	} \
	static void fn_bbs_##name##_set(WrenVM *vm) \
	{ \
		struct foreign_bbs *bbs = bbs_value(vm, 0); \
		struct st_profile *profile = bbs_mutable_profile(vm, bbs); \
		if (profile != NULL && slot_fixed_string(vm, 1, profile->field, \
		    sizeof(profile->field))) \
			bbs_changed(bbs); \
	}

#define BBS_OWNED_PROPERTY(name, field) \
	static void fn_bbs_##name(WrenVM *vm) \
	{ \
		struct foreign_bbs *bbs = bbs_value(vm, 0); \
		if (bbs != NULL) \
			wrenSetSlotString(vm, 0, bbs->record->profile.field == NULL ? "" : \
			    bbs->record->profile.field); \
	} \
	static void fn_bbs_##name##_set(WrenVM *vm) \
	{ \
		struct foreign_bbs *bbs = bbs_value(vm, 0); \
		struct st_profile *profile = bbs_mutable_profile(vm, bbs); \
		if (profile != NULL && slot_owned_string(vm, 1, \
		    &profile->field)) \
			bbs_changed(bbs); \
	}

#define BBS_NUM_PROPERTY(name, field, type, minimum, maximum) \
	static void fn_bbs_##name(WrenVM *vm) \
	{ \
		struct foreign_bbs *bbs = bbs_value(vm, 0); \
		if (bbs != NULL) \
			st_wren_return_i64(vm, bbs->record->profile.field); \
	} \
	static void fn_bbs_##name##_set(WrenVM *vm) \
	{ \
		struct foreign_bbs *bbs = bbs_value(vm, 0); \
		struct st_profile *profile = bbs_mutable_profile(vm, bbs); \
		int64_t value; \
		if (profile != NULL && slot_integer(vm, 1, minimum, maximum, &value)) { \
			profile->field = (type)value; \
			bbs_changed(bbs); \
		} \
	}

#define BBS_BOOL_PROPERTY(name, field) \
	static void fn_bbs_##name(WrenVM *vm) \
	{ \
		struct foreign_bbs *bbs = bbs_value(vm, 0); \
		if (bbs != NULL) \
			wrenSetSlotBool(vm, 0, bbs->record->profile.field); \
	} \
	static void fn_bbs_##name##_set(WrenVM *vm) \
	{ \
		struct foreign_bbs *bbs = bbs_value(vm, 0); \
		struct st_profile *profile = bbs_mutable_profile(vm, bbs); \
		bool value; \
		if (profile != NULL && slot_bool(vm, 1, &value)) { \
			profile->field = value; \
			bbs_changed(bbs); \
		} \
	}

BBS_FIXED_PROPERTY(addr, address)
BBS_FIXED_PROPERTY(user, username)
static void
fn_bbs_password(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);
	if (bbs != NULL)
		wrenSetSlotString(vm, 0, bbs->record->profile.password);
}

static void
fn_bbs_password_set(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);
	struct st_profile *profile = bbs_mutable_profile(vm, bbs);
	if (profile != NULL && slot_fixed_string(vm, 1, profile->password,
	    sizeof(profile->password))) {
		profile->ghost_program[0] = '\0';
		bbs_changed(bbs);
	}
}
BBS_FIXED_PROPERTY(syspass, system_password)
BBS_FIXED_PROPERTY(term_name, terminal_name)
BBS_FIXED_PROPERTY(font, font)
BBS_FIXED_PROPERTY(amigaFont, amiga_font)

static void
fn_bbs_encoding(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);

	if (bbs != NULL)
		wrenSetSlotDouble(vm, 0, bbs->record->profile.encoding);
}

static void
fn_bbs_encoding_set(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);
	struct st_profile *profile = bbs_mutable_profile(vm, bbs);
	int64_t value;

	if (profile == NULL || !slot_integer(vm, 1, 0,
	    ST_PROFILE_ENCODING_UTF8, &value))
		return;
	if (!st_profile_encoding_valid((int)value)) {
		st_wren_abort(vm, "invalid BBS encoding");
		return;
	}
	profile->encoding = (int)value;
	bbs_changed(bbs);
}
static void
fn_bbs_ghost_program(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);
	if (bbs != NULL)
		wrenSetSlotString(vm, 0,
		    st_profile_ghost_program(&bbs->record->profile));
}

static void
fn_bbs_ghost_program_set(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);
	struct st_profile *profile = bbs_mutable_profile(vm, bbs);
	if (profile == NULL)
		return;
	if (profile->connection_type == ST_CONNECTION_MBBS_GHOST)
		fn_bbs_password_set(vm);
	else if (slot_fixed_string(vm, 1, profile->ghost_program,
	    sizeof(profile->ghost_program)))
		bbs_changed(bbs);
}
BBS_FIXED_PROPERTY(tls_psk_identity, tls_psk_identity)
BBS_FIXED_PROPERTY(tls_psk, tls_psk)
BBS_OWNED_PROPERTY(dl_dir, download_directory)
BBS_OWNED_PROPERTY(ul_dir, upload_directory)
BBS_OWNED_PROPERTY(log_file, log_path)
BBS_OWNED_PROPERTY(tls_trusted_cert, tls_trusted_certificate)
BBS_OWNED_PROPERTY(tls_client_cert, tls_client_certificate)
BBS_OWNED_PROPERTY(tls_client_key, tls_client_key)

BBS_NUM_PROPERTY(port, port, uint16_t, 0, UINT16_MAX)
BBS_NUM_PROPERTY(conn_type, connection_type, int, ST_CONNECTION_UNKNOWN,
    ST_CONNECTION_MQTT)
BBS_NUM_PROPERTY(music, music, int, ST_PROFILE_MUSIC_SYNCTERM,
    ST_PROFILE_MUSIC_ALL)
BBS_NUM_PROPERTY(graphics, graphics_mode, int, ST_GRAPHICS_NONE, ST_GRAPHICS_IGS_221)
BBS_NUM_PROPERTY(amigaFontSize, amiga_font_size, uint16_t, 1, UINT16_MAX)
BBS_NUM_PROPERTY(address_family, address_family, int,
    ST_ADDRESS_FAMILY_UNSPECIFIED, ST_ADDRESS_FAMILY_HAPPY_EYEBALLS)
BBS_NUM_PROPERTY(screen_mode, screen_mode, int, ST_SCREEN_MODE_CURRENT,
    ST_SCREEN_MODE_AMIGA_640X400)
BBS_NUM_PROPERTY(bps_rate, bps_rate, uint32_t, 0, UINT32_MAX)
BBS_NUM_PROPERTY(xfer_log_level, transfer_log_level, int,
    ST_PROFILE_LOG_EMERGENCY, ST_PROFILE_LOG_DEBUG)
BBS_NUM_PROPERTY(protocol_log_level, protocol_log_level, int,
    ST_PROFILE_LOG_EMERGENCY, ST_PROFILE_LOG_DEBUG)
BBS_NUM_PROPERTY(cterm_log_level, cterm_log_level, int,
    ST_PROFILE_LOG_EMERGENCY, ST_PROFILE_LOG_DEBUG)
BBS_NUM_PROPERTY(stop_bits, stop_bits, uint16_t, 1, 2)
BBS_NUM_PROPERTY(data_bits, data_bits, uint16_t, 7, 8)
BBS_NUM_PROPERTY(parity, parity, int, ST_PROFILE_PARITY_NONE,
    ST_PROFILE_PARITY_ODD)
BBS_NUM_PROPERTY(flow_control, flow_control, int, ST_PROFILE_FLOW_RTS_CTS,
    ST_PROFILE_FLOW_NONE)
BBS_NUM_PROPERTY(tls_psk_version, tls_psk_version, int, ST_TLS_VERSION_UNKNOWN,
    ST_TLS_VERSION_1_3)
BBS_NUM_PROPERTY(tls_version_floor, tls_version_floor, int,
    ST_TLS_VERSION_UNKNOWN, ST_TLS_VERSION_1_3)
BBS_NUM_PROPERTY(sort_order, sort_order, int32_t, INT32_MIN, INT32_MAX)

BBS_BOOL_PROPERTY(no_status, hide_status)
BBS_BOOL_PROPERTY(hide_popups, hide_popups)
BBS_BOOL_PROPERTY(yellow_is_yellow, yellow_is_yellow)
BBS_BOOL_PROPERTY(force_lcf, force_lcf)
BBS_BOOL_PROPERTY(append_log_file, append_log)
BBS_BOOL_PROPERTY(telnet_no_binary, telnet_no_binary)
BBS_BOOL_PROPERTY(defer_telnet_negotiation, defer_telnet_negotiation)
BBS_BOOL_PROPERTY(tls_trust_web_pki, tls_trust_web_pki)
BBS_BOOL_PROPERTY(sftp_public_key, sftp_public_key)
BBS_BOOL_PROPERTY(ssh_allow_aes128_cbc, ssh_allow_aes128_cbc)
BBS_BOOL_PROPERTY(ssh_accept_early_data, ssh_accept_early_data)
BBS_BOOL_PROPERTY(lf_expand, expand_lf)

static void
fn_bbs_id(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);

	if (bbs != NULL)
		wrenSetSlotDouble(vm, 0, bbs->record->profile.identifier);
}

static void
fn_bbs_added(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);

	if (bbs != NULL)
		st_wren_return_i64(vm, bbs->record->profile.added);
}

static void
fn_bbs_connected(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);

	if (bbs != NULL)
		st_wren_return_i64(vm, bbs->record->profile.connected);
}

static void
fn_bbs_calls(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);

	if (bbs != NULL)
		wrenSetSlotDouble(vm, 0, bbs->record->profile.calls);
}

static void
fn_bbs_cache(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);
	if (bbs == NULL) return;
	if (bbs->kind == ST_WREN_BBS_SHADOWED_ENTRY) wrenSetSlotNull(vm, 0);
	else st_wren_entry_cache_push(vm, bbs->record,
	    bbs->generation, bbs->identity);
}

static void
fn_bbs_fingerprint(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);

	if (bbs != NULL)
		wrenSetSlotBytes(vm, 0,
		    (const char *)bbs->record->profile.ssh_fingerprint,
		    bbs->record->profile.ssh_fingerprint_size);
}

static void
fn_bbs_fingerprint_set(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);
	struct st_profile *profile = bbs_mutable_profile(vm, bbs);
	const char *value;
	int length;

	if (profile == NULL)
		return;
	if (wrenGetSlotType(vm, 1) != WREN_TYPE_STRING) {
		st_wren_abort(vm, "Profile.sshFingerprint expects a String");
		return;
	}
	value = wrenGetSlotBytes(vm, 1, &length);
	if (length != 0 && length != 20 && length != 32) {
		st_wren_abort(vm,
		    "Profile.sshFingerprint expects 0, 20, or 32 bytes");
		return;
	}
	if (length != 0)
		memcpy(profile->ssh_fingerprint, value, (size_t)length);
	profile->ssh_fingerprint_size = (uint8_t)length;
	bbs_changed(bbs);
}

static void
fn_bbs_conn_type_name(WrenVM *vm)
{
	static const char *const names[] = {
		"Unknown", "RLogin", "RLogin (reversed)", "Telnet", "Raw TCP",
		"SSH", "SSH (no authentication)", "Modem", "Serial",
		"Serial (no RTS/CTS)", "Shell", "MBBS GHOST", "Telnet TLS",
		"MQTT",
	};
	struct foreign_bbs *bbs = bbs_value(vm, 0);
	int type;

	if (bbs == NULL) return;
	type = bbs->record->profile.connection_type;
	wrenSetSlotString(vm, 0, type >= 0
	    && (size_t)type < sizeof(names) / sizeof(names[0]) ? names[type] :
	    names[0]);
}

static void
fn_bbs_palette(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);

	if (bbs == NULL) return;
	uint32_t palette[16];
	unsigned count = bbs->record->profile.palette_size;
	memcpy(palette, bbs->record->profile.palette, sizeof(palette));
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	for (unsigned i = 0; i < count; ++i) {
		wrenSetSlotDouble(vm, 1,
		    palette[i] & UINT32_C(0xffffff));
		wrenInsertInList(vm, 0, -1, 1);
	}
}

static void
fn_bbs_palette_set(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);
	struct st_profile *profile = bbs_mutable_profile(vm, bbs);
	uint32_t palette[16];
	int count;

	if (profile == NULL)
		return;
	if (wrenGetSlotType(vm, 1) != WREN_TYPE_LIST
	    || (count = wrenGetListCount(vm, 1)) < 0 || count > 16) {
		st_wren_abort(vm, "Profile.palette expects at most 16 colors");
		return;
	}
	wrenEnsureSlots(vm, 3);
	for (int i = 0; i < count; ++i) {
		int64_t value;

		wrenGetListElement(vm, 1, i, 2);
		if (!slot_integer(vm, 2, 0, UINT32_C(0xffffff), &value))
			return;
		palette[i] = (uint32_t)value;
	}
	if (count != 0)
		memcpy(profile->palette, palette, (size_t)count * sizeof(palette[0]));
	profile->palette_size = (unsigned)count;
	bbs_changed(bbs);
}

static void
fn_bbs_wren_scripts(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);
	const char *next;

	if (bbs == NULL) return;
	char scripts[ST_PROFILE_WREN_SCRIPTS_MAX + 1];
	memcpy(scripts, bbs->record->profile.wren_scripts, sizeof(scripts));
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	next = scripts;
	while (*next != '\0') {
		const char *comma = strchr(next, ',');
		size_t length = comma == NULL ? strlen(next) : (size_t)(comma - next);

		if (length != 0) {
			wrenSetSlotBytes(vm, 1, next, length);
			wrenInsertInList(vm, 0, -1, 1);
		}
		if (comma == NULL)
			break;
		next = comma + 1;
	}
}

static void
fn_bbs_wren_scripts_set(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);
	struct st_profile *profile = bbs_mutable_profile(vm, bbs);
	char scripts[ST_PROFILE_WREN_SCRIPTS_MAX + 1] = "";
	int count;

	if (profile == NULL)
		return;
	if (bbs->kind == ST_WREN_BBS_DEFAULTS) {
		st_wren_abort(vm, "Wren scripts are configured per entry, not in defaults");
		return;
	}
	if (wrenGetSlotType(vm, 1) != WREN_TYPE_LIST) {
		st_wren_abort(vm, "Profile.wrenScripts expects a List");
		return;
	}
	count = wrenGetListCount(vm, 1);
	wrenEnsureSlots(vm, 3);
	for (int i = 0; i < count; ++i) {
		const char *name;
		int length;

		wrenGetListElement(vm, 1, i, 2);
		if (wrenGetSlotType(vm, 2) != WREN_TYPE_STRING) {
			st_wren_abort(vm, "Profile.wrenScripts expects Strings");
			return;
		}
		name = wrenGetSlotBytes(vm, 2, &length);
		if (length < 0 || !st_profile_scripts_append(scripts, name,
		    (size_t)length)) {
			st_wren_abort(vm, "Profile.wrenScripts contains an invalid name");
			return;
		}
	}
	strcpy(profile->wren_scripts, scripts);
	bbs_changed(bbs);
}

static void
fn_bbs_name(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);

	if (bbs != NULL)
		wrenSetSlotString(vm, 0, bbs->record->profile.name);
}

static void
fn_bbs_search_key(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct foreign_bbs *bbs = bbs_value(vm, 0);
	size_t size;
	const char *key;

	if (bbs == NULL) return;
	key = st_wren_bbs_model_search_key(&host->bbs_model, bbs->record,
	    &size);
	if (key == NULL)
		st_wren_abort(vm, "DirectoryEntry.searchKey: out of memory");
	else
		wrenSetSlotBytes(vm, 0, key, size);
}

static void
fn_bbs_rename(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct foreign_bbs *bbs = bbs_value(vm, 0);
	char name[ST_PROFILE_NAME_MAX + 1];
	int fields[32];
	size_t field_count;
	bool success = false;

	if (bbs != NULL && bbs->kind == ST_WREN_BBS_MODEL_ENTRY
	    && bbs->record->source == ST_BBS_SOURCE_PERSONAL
	    && slot_fixed_string(vm, 1, name, sizeof(name))) {
		field_count = configured_sort_fields(fields);
		success = st_wren_bbs_model_rename(&host->bbs_model, bbs->record,
		    name, fields, field_count);
	}
	if (success) wrenSetSlotNull(vm, 0);
	else if (bbs != NULL) menu_error(vm, bbs->read_only || bbs->record->source != ST_BBS_SOURCE_PERSONAL
	    ? IO_ERROR_READ_ONLY : 0, 0, "The directory entry could not be renamed.");
}

static void
fn_bbs_source(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);

	if (bbs != NULL)
		wrenSetSlotDouble(vm, 0, bbs->record->source);
}

static void
fn_bbs_source_name(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);
	char *name = NULL;
	if (bbs == NULL) return;
	int status = copy_diagnostic_name(&name, bbs->record->source_name);
	if (status != 0)
		menu_error(vm, status, status, "The directory source name could not be converted.");
	else if (name == NULL)
		wrenSetSlotNull(vm, 0);
	else
		wrenSetSlotString(vm, 0, name);
	free(name);
}

static void
fn_bbs_shadowed(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);
	if (bbs == NULL) return;
	if (bbs->record->shadowed == NULL) wrenSetSlotNull(vm, 0);
	else (void)push_bbs(vm, 0, bbs->record->shadowed, ST_WREN_BBS_SHADOWED_ENTRY);
}

static void
fn_bbs_comment(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);

	if (bbs != NULL)
		wrenSetSlotString(vm, 0, bbs->record->profile.comment);
}

static void
fn_bbs_comment_set(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);
	struct st_profile *profile = bbs_mutable_profile(vm, bbs);

	if (profile != NULL && slot_fixed_string(vm, 1, profile->comment,
	    sizeof(profile->comment)))
		bbs_changed(bbs);
}

static void
fn_bbs_dirty(WrenVM *vm)
{
	struct foreign_bbs *bbs = bbs_value(vm, 0);

	if (bbs != NULL)
		wrenSetSlotBool(vm, 0, bbs->record->dirty);
}

static void
save_record(WrenVM *vm, struct foreign_bbs *bbs)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct st_bbs_mutation_result mutation;
	int fields[32];
	size_t field_count;
	int status;

	if (bbs == NULL) return;
	if (host->bbs_list == NULL) {
		menu_error(vm, 0, 0, "No directory is loaded.");
		return;
	}
	if (bbs->owned || bbs->read_only
	    || (bbs->kind == ST_WREN_BBS_MODEL_ENTRY
	    && bbs->record->source != ST_BBS_SOURCE_PERSONAL)
	    || host->config.safe_mode) {
		menu_error(vm, IO_ERROR_READ_ONLY, 0, "The directory is read-only.");
		return;
	}
	if (!bbs->record->dirty) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	field_count = configured_sort_fields(fields);
	if (bbs->kind == ST_WREN_BBS_DEFAULTS)
		status = st_bbs_list_set_defaults(host->bbs_list,
		    &bbs->record->profile);
	else if (!st_wren_bbs_model_prepare_save(&host->bbs_model, bbs->record)) {
		menu_error(vm, 0, 0, "The directory entry could not be prepared for saving.");
		return;
	}
	else if (bbs->record->new_entry)
		status = st_bbs_list_add(host->bbs_list, &bbs->record->profile,
		    &mutation);
	else
		status = st_bbs_list_update(host->bbs_list,
		    bbs->record->persisted_name, &bbs->record->profile, &mutation);
	if (status == 0 && bbs->kind == ST_WREN_BBS_DEFAULTS)
		bbs->record->dirty = false;
	else if (status == 0)
		st_wren_bbs_model_saved(&host->bbs_model, bbs->record, fields,
		    field_count);
	menu_result(vm, status, true, "The directory entry or defaults could not be saved.");
}

static void fn_bbs_save(WrenVM *vm) { save_record(vm, bbs_value(vm, 0)); }
static void
fn_menu_save_defaults(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct foreign_bbs bbs = {
		.record = &host->bbs_model.defaults,
		.kind = ST_WREN_BBS_DEFAULTS,
	};

	if (!host->bbs_model.loaded) {
		menu_error(vm, 0, 0, "No directory is loaded.");
		return;
	}
	save_record(vm, &bbs);
}

static void
fn_menu_defaults_dirty(WrenVM *vm)
{
	struct st_wren_bbs_model *model = &st_wren_host_from_vm(vm)->bbs_model;

	wrenSetSlotBool(vm, 0, model->loaded && model->defaults.dirty);
}

static void
fn_bbs_delete(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct foreign_bbs *bbs = bbs_value(vm, 0);
	struct st_bbs_mutation_result mutation;
	int fields[32];
	if (bbs == NULL) return;
	if (host->bbs_list == NULL) {
		menu_error(vm, 0, 0, "No directory is loaded.");
		return;
	}
	if (bbs->read_only || bbs->kind != ST_WREN_BBS_MODEL_ENTRY
	    || bbs->record->source != ST_BBS_SOURCE_PERSONAL) {
		menu_error(vm, IO_ERROR_READ_ONLY, 0, "The directory entry is read-only.");
		return;
	}
	size_t field_count = configured_sort_fields(fields);
	if (!bbs->record->new_entry) {
		if (!menu_writable(vm, "Directory entries cannot be deleted in safe mode."))
			return;
		int status = st_bbs_list_remove(host->bbs_list,
		    bbs->record->persisted_name, &mutation);
		if (status != 0) {
			menu_result(vm, status, true, "The directory entry could not be deleted.");
			return;
		}
	}
	if (st_wren_bbs_model_delete(&host->bbs_model, bbs->record, fields, field_count))
		wrenSetSlotNull(vm, 0);
	else
		menu_error(vm, 0, 0, "The directory entry could not be removed from the working model.");
}

static void
fn_bbs_to_string(WrenVM *vm)
{
	fn_bbs_name(vm);
}

#define BIND_GETSET(class_name, wren_name, c_name) \
	{class_name, false, wren_name, fn_##c_name}, \
	{class_name, false, wren_name "=(_)", fn_##c_name##_set}

static const struct binding bindings[] = {
	{"MenuError", false, "code", fn_menu_error_code},
	{"MenuError", false, "nativeCode", fn_menu_error_native_code},
	{"MenuError", false, "message", fn_menu_error_message},
	{"MenuError", false, "toString", fn_menu_error_string},
	{"Menu", true, "activateFonts()", fn_menu_activate_fonts},
	{"Menu", true, "load(_)", fn_menu_load},
	{"Menu", true, "loadDiagnostics", fn_menu_load_diagnostics},
	{"BBSLoadDiagnostic", false, "source", fn_load_diagnostic_source},
	{"BBSLoadDiagnostic", false, "sourceName", fn_load_diagnostic_source_name},
	{"BBSLoadDiagnostic", false, "sectionName", fn_load_diagnostic_section_name},
	{"BBSLoadDiagnostic", false, "error", fn_load_diagnostic_error},
	{"Menu", true, "quickConnect(_)", fn_menu_quick_connect},
	{"Menu", true, "offlineScrollback", fn_menu_offline_scrollback},
	{"Menu", true, "finishOfflineScrollback()", fn_menu_finish_offline_scrollback},
	{"Menu", true, "prepareOfflineScrollback()",
	    fn_menu_prepare_offline_scrollback},
	{"Menu", true, "offlineScrollbackHasStatus",
	    fn_menu_offline_scrollback_has_status},
	{"Menu", true, "openUrl(_)", fn_menu_open_url},
	{"Menu", true, "openHyperlink(_)", fn_menu_open_hyperlink},
	{"Menu", true, "setHyperlinkHover(_)", fn_menu_set_hyperlink_hover},
	{"Menu", true, "applicationTitle", fn_menu_application_title},
	{"Menu", true, "timeText", fn_menu_time_text},
	{"Menu", true, "timestamp", fn_menu_timestamp},
	{"Menu", true, "showEntry(_)", fn_menu_show_entry},
	{"Menu", true, "quitApplication()", fn_menu_quit},
	{"Menu", true, "entries", fn_menu_entries},
	{"Menu", true, "canAppendEntry", fn_menu_can_append},
	{"Menu", true, "scriptModules", fn_menu_script_modules},
	{"Menu", true, "wrenScriptsMaxLength",
	    fn_menu_wren_scripts_max_length},
	{"Menu", true, "defaults", fn_menu_defaults},
	{"Menu", true, "defaultsDirty", fn_menu_defaults_dirty},
	{"Menu", true, "saveDefaults()", fn_menu_save_defaults},
	{"Menu", true, "settings", fn_menu_settings},
	{"Menu", true, "nameAvailable(_)", fn_menu_name_available},
	{"Menu", true, "create(_)", fn_menu_create},
	{"Menu", true, "copy(_,_)", fn_menu_copy},
	{"Menu", true, "sort()", fn_menu_sort},
	{"Menu", true, "sortFields", fn_menu_sort_fields},
	{"Menu", true, "sortProfiles", fn_menu_sort_profiles},
	{"Menu", true, "activeSortProfile", fn_menu_active_sort_profile},
	{"Menu", true, "saveSortProfiles()", fn_menu_save_sort_profiles},
	{"Menu", true, "setActiveSortProfile(_)",
	    fn_menu_set_active_sort_profile},
	{"Menu", true, "addSortProfile(_,_,_)", fn_menu_add_sort_profile},
	{"Menu", true, "updateSortProfile(_,_,_)",
	    fn_menu_update_sort_profile},
	{"Menu", true, "deleteSortProfile(_)", fn_menu_delete_sort_profile},
	{"Menu", true, "connectionTypes", fn_menu_connection_types},
	{"Menu", true, "defaultPort(_)", fn_menu_default_port},
	{"Menu", true, "addressFamilies", fn_menu_address_families},
	{"Menu", true, "rates", fn_menu_rates},
	{"Menu", true, "serialRates(_)", fn_menu_serial_rates},
	{"Menu", true, "musicModes", fn_menu_music_modes},
	{"Menu", true, "graphicsModes", fn_menu_graphics_modes},
	{"Menu", true, "flowControls", fn_menu_flow_controls},
	{"Menu", true, "flowControlsNoRts", fn_menu_flow_controls_no_rts},
	{"Menu", true, "parities", fn_menu_parities},
	{"Menu", true, "paletteDefaults(_)", fn_menu_palette_defaults},
	{"Menu", true, "fontsCatalog", fn_menu_fonts_catalog},
	{"Menu", true, "amigaFonts", fn_menu_amiga_fonts},
	{"Menu", true, "installAmigaFont()", fn_menu_install_amiga_font},
	{"Menu", true, "removeAmigaFont(_)", fn_menu_remove_amiga_font},
	{"AmigaFontFamily", false, "name", fn_amiga_name},
	{"AmigaFontFamily", false, "sizes", fn_amiga_sizes},
	{"AmigaFontFamily", false, "installedSizes", fn_amiga_installed_sizes},
	{"AmigaFontFamily", false, "source", fn_amiga_source},
	{"AmigaFontFamily", false, "error", fn_amiga_error},
	{"Menu", true, "encodings", fn_menu_encodings},
	{"Menu", true, "fontsForEncoding(_)", fn_menu_fonts_for_encoding},
	{"Menu", true, "fontEncoding(_)", fn_menu_font_encoding},
	{"Menu", true, "logLevels", fn_menu_log_levels},
	{"Menu", true, "screenModes", fn_menu_screen_modes},
	{"Menu", true, "canvasModes", fn_menu_canvas_modes},
	{"Menu", true, "outputModes", fn_menu_output_modes},
	{"Menu", true, "cursorStyles", fn_menu_cursor_styles},
	{"Menu", true, "audioModes", fn_menu_audio_modes},
	{"Menu", true, "midiModes", fn_menu_midi_modes},
	{"Menu", true, "buildOptions", fn_menu_build_options},
	{"Menu", true, "licenses", fn_menu_licenses},
	{"Menu", true, "maxPathLength", fn_menu_max_path_length},
	{"Menu", true, "scalingModes", fn_menu_scaling_modes},
	{"Menu", true, "colors", fn_menu_colours},
	{"Menu", true, "backgroundColors", fn_menu_background_colours},
	{"Menu", true, "currentScreenMode", fn_menu_current_screen_mode},
	{"Menu", true, "setScreenMode(_)", fn_menu_set_screen_mode},
	{"Menu", true, "fileLocations", fn_menu_file_locations},
	{"Menu", true, "encryptionAvailable", fn_menu_encryption_available},
	{"Menu", true, "themes", fn_menu_themes},
	{"Menu", true, "selectedThemeFile", fn_menu_selected_theme_file},
	{"Menu", true, "selectedThemePackage", fn_menu_selected_theme_package},
	{"Menu", true, "previewTheme(_)", fn_menu_preview_theme},
	{"Menu", true, "cancelThemePreview()", fn_menu_cancel_theme_preview},
	{"Menu", true, "selectTheme(_)", fn_menu_select_theme},
	{"Menu", true, "deleteTheme(_)", fn_menu_delete_theme},
	{"Menu", true, "newThemeDocument()", fn_menu_new_theme_document},
	{"Menu", true, "openThemeDocument(_)", fn_menu_open_theme_document},
	{"Menu", true, "refreshCloudThemes()", fn_menu_refresh_cloud_themes},
	{"Menu", true, "cloudThemes", fn_menu_cloud_themes},
	{"CloudThemeEntry", false, "package", fn_cloud_theme_package},
	{"CloudThemeEntry", false, "name", fn_cloud_theme_name},
	{"CloudThemeEntry", false, "author", fn_cloud_theme_author},
	{"CloudThemeEntry", false, "description", fn_cloud_theme_description},
	{"CloudThemeEntry", false, "version", fn_cloud_theme_version},
	{"CloudThemeEntry", false, "sourceUri", fn_cloud_theme_source_uri},
	{"CloudThemeEntry", false, "expectedSha256", fn_cloud_theme_expected_sha256},
	{"CloudThemeEntry", false, "cached", fn_cloud_theme_cached},
	{"CloudThemeEntry", false, "updateAvailable", fn_cloud_theme_update_available},
	{"CloudThemeEntry", false, "error", fn_cloud_theme_error},
	{"Menu", true, "previewCloudTheme(_)", fn_menu_preview_cloud_theme},
	{"Menu", true, "selectCloudTheme(_)", fn_menu_select_cloud_theme},
	{"Menu", true, "deleteCloudTheme(_)", fn_menu_delete_cloud_theme},
	{"Menu", true, "cacheCloudTheme(_)", fn_menu_cache_cloud_theme},
	{"Menu", true, "copyCloudTheme(_)", fn_menu_copy_cloud_theme},
	{"Menu", true, "encryptionInfo", fn_menu_encryption_info},
	{"ListEncryptionInfo", false, "format", fn_encryption_info_format},
	{"ListEncryptionInfo", false, "algorithm", fn_encryption_info_algorithm},
	{"ListEncryptionInfo", false, "keyBits", fn_encryption_info_key_bits},
	{"ListEncryptionInfo", false, "name", fn_encryption_info_name},
	{"ListEncryptionInfo", false, "kdf", fn_encryption_info_kdf},
	{"ListEncryptionInfo", false, "iterations", fn_encryption_info_iterations},
	{"ListEncryptionInfo", false, "costLog2", fn_encryption_info_cost_log2},
	{"ListEncryptionInfo", false, "blockSize", fn_encryption_info_block_size},
	{"ListEncryptionInfo", false, "parallelism", fn_encryption_info_parallelism},
	{"ListEncryptionInfo", false, "rewriteRecommended", fn_encryption_info_rewrite_recommended},
	{"Menu", true, "setEncryption(_,_,_)", fn_menu_set_encryption},
	{"Menu", true, "webLists", fn_menu_web_lists},
	{"Menu", true, "webListsDirty", fn_menu_web_lists_dirty},
	{"Menu", true, "addWebList(_,_,_)", fn_menu_add_web_list},
	{"Menu", true, "updateWebList(_,_)", fn_menu_update_web_list},
	{"Menu", true, "deleteWebList(_)", fn_menu_delete_web_list},
	{"Menu", true, "saveWebLists()", fn_menu_save_web_lists},
	{"Menu", true, "refreshWebList(_)", fn_menu_refresh_web_list},
	{"Menu", true, "fonts", fn_menu_fonts},
	{"Menu", true, "fontsDirty", fn_menu_fonts_dirty},
	{"Menu", true, "canCreateFont", fn_menu_can_create_font},
	{"Menu", true, "reloadFonts()", fn_menu_reload_fonts},
	{"Menu", true, "createFont(_,_)", fn_menu_create_font},
	{"Menu", true, "saveFonts()", fn_menu_save_fonts},

	{"MenuFont", false, "name", fn_font_name},
	{"MenuFont", false, "name=(_)", fn_font_name_set},
	{"MenuFont", false, "path(_)", fn_font_path},
	{"MenuFont", false, "setFile(_,_)", fn_font_set_file},
	{"MenuFont", false, "clearFile(_)", fn_font_clear_file},
	{"MenuFont", false, "delete()", fn_font_delete},
	{"MenuFont", false, "toString", fn_font_name},

	{"ThemeDocument", false, "filename", fn_theme_document_filename},
	{"ThemeDocument", false, "metadata(_)", fn_theme_document_metadata},
	{"ThemeDocument", false, "setMetadata(_,_)",
	    fn_theme_document_set_metadata},
	{"ThemeDocument", false, "styles", fn_theme_document_styles},
	{"ThemeDocument", false, "setStyle(_,_,_,_)",
	    fn_theme_document_set_style},
	{"ThemeDocument", false, "addStyle(_)", fn_theme_document_add_style},
	{"ThemeDocument", false, "removeStyle(_)",
	    fn_theme_document_remove_style},
	{"ThemeDocument", false, "glyphs", fn_theme_document_glyphs},
	{"ThemeDocument", false, "setGlyph(_,_,_,_)",
	    fn_theme_document_set_glyph},
	{"ThemeDocument", false, "addGlyph(_,_)",
	    fn_theme_document_add_glyph},
	{"ThemeDocument", false, "removeGlyph(_)",
	    fn_theme_document_remove_glyph},
	{"ThemeDocument", false, "themeData", fn_theme_document_theme_data},
	{"ThemeDocument", false, "dirty", fn_theme_document_dirty},
	{"ThemeDocument", false, "canUndo", fn_theme_document_can_undo},
	{"ThemeDocument", false, "canRedo", fn_theme_document_can_redo},
	{"ThemeDocument", false, "undo()", fn_theme_document_undo},
	{"ThemeDocument", false, "redo()", fn_theme_document_redo},
	{"ThemeDocument", false, "importTheme(_)", fn_theme_document_import},
	{"ThemeDocument", false, "save(_,_)", fn_theme_document_save},
	{"ThemeDocument", false, "activate()", fn_theme_document_activate},

	{"Settings", false, "dirty", fn_settings_dirty},
	{"Settings", false, "apply()", fn_settings_apply},
	{"Settings", false, "save()", fn_settings_save},
	{"Settings", false, "reload()", fn_settings_reload},
	BIND_GETSET("Settings", "confirmClose", settings_confirm_close),
	BIND_GETSET("Settings", "promptSave", settings_prompt_save),
	BIND_GETSET("Settings", "invertWheel", settings_invert_wheel),
	BIND_GETSET("Settings", "colorEmoji", settings_color_emoji),
	BIND_GETSET("Settings", "atariStKeyClicks", settings_atari_st_key_clicks),
	BIND_GETSET("Settings", "modemDevice", settings_modem_device),
	BIND_GETSET("Settings", "modemInit", settings_modem_init),
	BIND_GETSET("Settings", "modemDial", settings_modem_dial),
	BIND_GETSET("Settings", "listPath", settings_list_path),
	BIND_GETSET("Settings", "shellTerm", settings_shell_term),
	BIND_GETSET("Settings", "tlsClientCert", settings_tls_client_cert),
	BIND_GETSET("Settings", "tlsClientKey", settings_tls_client_key),
	BIND_GETSET("Settings", "startupMode", settings_startup_mode),
	BIND_GETSET("Settings", "outputMode", settings_output_mode),
	BIND_GETSET("Settings", "cursorStyle", settings_cursor_style),
	BIND_GETSET("Settings", "audioModes", settings_audio_modes),
	BIND_GETSET("Settings", "midiModes", settings_midi_modes),
	BIND_GETSET("Settings", "scrollbackLines", settings_scrollback_lines),
	BIND_GETSET("Settings", "transferSuccessKeypressTimeout",
	    settings_transfer_success_keypress_timeout),
	BIND_GETSET("Settings", "transferFailureKeypressTimeout",
	    settings_transfer_failure_keypress_timeout),
	BIND_GETSET("Settings", "modemRate", settings_modem_rate),
	BIND_GETSET("Settings", "scalingMode", settings_scaling_mode),
	BIND_GETSET("Settings", "kdfShift", settings_kdf_shift),
	BIND_GETSET("Settings", "customRows", settings_custom_rows),
	BIND_GETSET("Settings", "customColumns", settings_custom_columns),
	BIND_GETSET("Settings", "customFontHeight",
	    settings_custom_font_height),
	BIND_GETSET("Settings", "customAspectWidth",
	    settings_custom_aspect_width),
	BIND_GETSET("Settings", "customAspectHeight",
	    settings_custom_aspect_height),
	BIND_GETSET("Settings", "frameColor", settings_frame_color),
	BIND_GETSET("Settings", "textColor", settings_text_color),
	BIND_GETSET("Settings", "backgroundColor", settings_background_color),
	BIND_GETSET("Settings", "inverseColor", settings_inverse_color),
	BIND_GETSET("Settings", "lightbarColor", settings_lightbar_color),
	BIND_GETSET("Settings", "lightbarBackgroundColor",
	    settings_lightbar_background_color),

	{"DirectoryEntry", false, "profile", fn_entry_profile},
	{"DirectoryEntry", false, "readOnly", fn_profile_read_only},
	{"Profile", false, "readOnly", fn_profile_read_only},
	{"Profile", false, "copy()", fn_profile_copy},
	{"DirectoryEntry", false, "name", fn_bbs_name},
	{"DirectoryEntry", false, "searchKey", fn_bbs_search_key},
	{"DirectoryEntry", false, "rename(_)", fn_bbs_rename},
	{"DirectoryEntry", false, "source", fn_bbs_source},
	{"DirectoryEntry", false, "sourceName", fn_bbs_source_name},
	{"DirectoryEntry", false, "shadowed", fn_bbs_shadowed},
	{"DirectoryEntry", false, "cache", fn_bbs_cache},
	{"DirectoryEntry", false, "id", fn_bbs_id},
	{"DirectoryEntry", false, "added", fn_bbs_added},
	{"DirectoryEntry", false, "connected", fn_bbs_connected},
	{"DirectoryEntry", false, "calls", fn_bbs_calls},
	{"Profile", false, "sshFingerprint", fn_bbs_fingerprint},
	{"Profile", false, "sshFingerprint=(_)", fn_bbs_fingerprint_set},
	{"Profile", false, "connTypeName", fn_bbs_conn_type_name},
	{"Profile", false, "comment", fn_bbs_comment},
	{"Profile", false, "comment=(_)", fn_bbs_comment_set},
	{"DirectoryEntry", false, "dirty", fn_bbs_dirty},
	{"DirectoryEntry", false, "save()", fn_bbs_save},
	{"DirectoryEntry", false, "delete()", fn_bbs_delete},
	{"Profile", false, "palette", fn_bbs_palette},
	{"Profile", false, "palette=(_)", fn_bbs_palette_set},
	{"Profile", false, "wrenScripts", fn_bbs_wren_scripts},
	{"Profile", false, "wrenScripts=(_)", fn_bbs_wren_scripts_set},
	{"DirectoryEntry", false, "toString", fn_bbs_to_string},
	BIND_GETSET("Profile", "addr", bbs_addr),
	BIND_GETSET("Profile", "port", bbs_port),
	BIND_GETSET("Profile", "connType", bbs_conn_type),
	BIND_GETSET("Profile", "user", bbs_user),
	BIND_GETSET("Profile", "password", bbs_password),
	BIND_GETSET("Profile", "syspass", bbs_syspass),
	BIND_GETSET("Profile", "music", bbs_music),
	BIND_GETSET("Profile", "graphics", bbs_graphics),
	BIND_GETSET("Profile", "addressFamily", bbs_address_family),
	BIND_GETSET("Profile", "termName", bbs_term_name),
	BIND_GETSET("Profile", "screenMode", bbs_screen_mode),
	BIND_GETSET("Profile", "bpsRate", bbs_bps_rate),
	BIND_GETSET("Profile", "font", bbs_font),
	BIND_GETSET("Profile", "amigaFont", bbs_amigaFont),
	BIND_GETSET("Profile", "amigaFontSize", bbs_amigaFontSize),
	BIND_GETSET("Profile", "encoding", bbs_encoding),
	BIND_GETSET("Profile", "noStatus", bbs_no_status),
	BIND_GETSET("Profile", "hidePopups", bbs_hide_popups),
	BIND_GETSET("Profile", "yellowIsYellow", bbs_yellow_is_yellow),
	BIND_GETSET("Profile", "forceLcf", bbs_force_lcf),
	BIND_GETSET("Profile", "dlDir", bbs_dl_dir),
	BIND_GETSET("Profile", "ulDir", bbs_ul_dir),
	BIND_GETSET("Profile", "logFile", bbs_log_file),
	BIND_GETSET("Profile", "appendLogFile", bbs_append_log_file),
	BIND_GETSET("Profile", "xferLogLevel", bbs_xfer_log_level),
	BIND_GETSET("Profile", "protocolLogLevel", bbs_protocol_log_level),
	BIND_GETSET("Profile", "ctermLogLevel", bbs_cterm_log_level),
	BIND_GETSET("Profile", "stopBits", bbs_stop_bits),
	BIND_GETSET("Profile", "dataBits", bbs_data_bits),
	BIND_GETSET("Profile", "parity", bbs_parity),
	BIND_GETSET("Profile", "flowControl", bbs_flow_control),
	BIND_GETSET("Profile", "telnetNoBinary", bbs_telnet_no_binary),
	BIND_GETSET("Profile", "deferTelnetNegotiation",
	    bbs_defer_telnet_negotiation),
	BIND_GETSET("Profile", "tlsTrustWebPki", bbs_tls_trust_web_pki),
	BIND_GETSET("Profile", "tlsTrustedCert", bbs_tls_trusted_cert),
	BIND_GETSET("Profile", "tlsClientCert", bbs_tls_client_cert),
	BIND_GETSET("Profile", "tlsClientKey", bbs_tls_client_key),
	BIND_GETSET("Profile", "tlsPskIdentity", bbs_tls_psk_identity),
	BIND_GETSET("Profile", "tlsPsk", bbs_tls_psk),
	BIND_GETSET("Profile", "tlsPskVersion", bbs_tls_psk_version),
	BIND_GETSET("Profile", "tlsVersionFloor", bbs_tls_version_floor),
	BIND_GETSET("Profile", "ghostProgram", bbs_ghost_program),
	BIND_GETSET("Profile", "sftpPublicKey", bbs_sftp_public_key),
	BIND_GETSET("Profile", "sshAllowAes128Cbc", bbs_ssh_allow_aes128_cbc),
	BIND_GETSET("Profile", "sshAcceptEarlyData", bbs_ssh_accept_early_data),
	BIND_GETSET("DirectoryEntry", "sortOrder", bbs_sort_order),
	BIND_GETSET("Profile", "lfExpand", bbs_lf_expand),
};

#undef BIND_GETSET

WrenForeignMethodFn
st_wren_menu_bind_method(const char *class_name, bool is_static,
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
st_wren_menu_bind_class(const char *class_name)
{
	WrenForeignClassMethods methods = {allocate_generic,
	    finalize_menu_foreign};

	if (strcmp(class_name, "BBSLoadDiagnostic") != 0
	    && strcmp(class_name, "MenuError") != 0
	    && strcmp(class_name, "ListEncryptionInfo") != 0
	    && strcmp(class_name, "MenuFont") != 0
	    && strcmp(class_name, "AmigaFontFamily") != 0
	    && strcmp(class_name, "ThemeDocument") != 0
	    && strcmp(class_name, "CloudThemeEntry") != 0
	    && strcmp(class_name, "Settings") != 0
	    && strcmp(class_name, "DirectoryEntry") != 0)
		methods = (WrenForeignClassMethods){NULL, NULL};
	return methods;
}

WrenForeignMethodFn
st_wren_profile_bind_method(const char *class_name, bool is_static, const char *signature)
{
	return strcmp(class_name, "Profile") == 0
	    ? st_wren_menu_bind_method(class_name, is_static, signature) : NULL;
}
WrenForeignClassMethods
st_wren_profile_bind_class(const char *class_name)
{
	return strcmp(class_name, "Profile") == 0
	    ? (WrenForeignClassMethods){allocate_generic, finalize_menu_foreign}
	    : (WrenForeignClassMethods){NULL, NULL};
}
