/* Locked read/merge/write transactions for the personal BBS directory. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_bbs_list_internal.h"

#include "array_list.h"
#include "filesystem.h"
#include "io_error.h"
#include "path.h"
#include "st_consent_token.h"
#include "st_file_lock.h"
#include "st_hex.h"
#include "st_crypto.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *profile_keys[] = {
	"Address", "Port", "Added", "LastConnected", "TotalCalls", "UserName",
	"Password", "SystemPassword", "ConnectionType", "FlowControl",
	"ScreenMode", "NoStatus", "DownloadPath", "UploadPath", "LogFile",
	"TransferLogLevel", "ProtocolLogLevel", "CTermLogLevel", "AppendLogFile", "BPSRate",
	"ANSIMusic", "AddressFamily", "Font", "Encoding", "HidePopups", "Graphics", "Comment",
	"WrenScripts", "ForceLCF", "YellowIsYellow", "TerminalType", "LFExpand",
	"TelnetBrokenTextmode", "TelnetDeferNegotiate", "TLSTrustWebPKI",
	"TLSTrustedCertificate", "TLSClientCertificate", "TLSClientKey",
	"TLSPSKIdentity", "TLSPSK", "TLSPSKVersion", "TLSVersionFloor",
	"SSHFingerprint", "SFTPPublicKey", "SSHAllowAES128CBC",
	"SSHAcceptEarlyData", "StopBits", "DataBits", "Parity", "Palette",
	"SortOrder"
};

static bool
section_name_equal(const ini_section_t *section, const char *name)
{
	char loaded[ST_PROFILE_NAME_MAX + 1];
	return st_bbs_loaded_name(loaded, ini_section_name(section))
	    && st_bbs_name_equal(loaded, name);
}

static const char picker_key_name[] = "WrenPickerHmacKey";

void
st_bbs_picker_key_load(st_bbs_list_t *list, ini_document_t *document,
	const struct st_list_crypto_info *info)
{
	const char *encoded;

	list->picker_key_available = false;
	st_crypto_clear(list->picker_key, sizeof(list->picker_key));
	if (document == NULL || info == NULL
	    || info->format != ST_LIST_CRYPTO_V2)
		return;
	encoded = ini_section_find(ini_document_root(document), picker_key_name);
	if (encoded != NULL && strlen(encoded) == sizeof(list->picker_key) * 2
	    && st_hex_decode(list->picker_key, sizeof(list->picker_key), encoded,
	    strlen(encoded)))
		list->picker_key_available = true;
}

static int
read_personal(st_bbs_list_t *list, ini_document_t **result,
    struct st_list_crypto_info *info)
{
	FILE *stream;
	int status = fs_open(&stream, list->personal_path, "rb");

	if (status == IO_ERROR_NOT_FOUND) {
		memset(info, 0, sizeof(*info));
		info->format = ST_LIST_CRYPTO_PLAIN;
		return ini_document_create(result);
	}
	if (status < 0)
		return status;
	status = st_list_crypto_read(result, stream, list->password,
	    list->password_size, 0, info);
	(void)fclose(stream);
	return status;
}

static int
write_personal(st_bbs_list_t *list, const ini_document_t *document,
    const struct st_list_crypto_options *options, const void *password,
    size_t password_size)
{
	FILE *prepared = tmpfile();
	FILE *output = NULL;
	unsigned char buffer[16384];
	size_t count;
	int status;

	if (prepared == NULL)
		return IO_ERROR_IO;
	status = st_list_crypto_write(prepared, document, options, password,
	    password_size);
	if (status == 0 && fflush(prepared) != 0)
		status = IO_ERROR_IO;
	if (status == 0 && fseek(prepared, 0, SEEK_SET) != 0)
		status = IO_ERROR_IO;
	if (status == 0)
		status = fs_open(&output, list->personal_path, "wb");
	if (status == 0) {
		while ((count = fread(buffer, 1, sizeof(buffer), prepared)) != 0) {
			if (fwrite(buffer, 1, count, output) != count) {
				status = IO_ERROR_IO;
				break;
			}
		}
		if (ferror(prepared) && status == 0)
			status = IO_ERROR_IO;
		if (fclose(output) != 0 && status == 0)
			status = IO_ERROR_IO;
	}
	(void)fclose(prepared);
	return status;
}

static bool
same_value(const char *left, const char *right)
{
	return left == NULL ? right == NULL
	    : right != NULL && strcmp(left, right) == 0;
}

static int
documents_equal(bool *equal, const ini_document_t *left,
    const ini_document_t *right)
{
	char *left_bytes = NULL;
	char *right_bytes = NULL;
	size_t left_size = 0, right_size = 0;
	int status = ini_document_serialize(left, &left_bytes, &left_size);

	if (status == 0)
		status = ini_document_serialize(right, &right_bytes, &right_size);
	if (status == 0)
		*equal = left_size == right_size
		    && memcmp(left_bytes, right_bytes, left_size) == 0;
	free(left_bytes);
	free(right_bytes);
	return status;
}

static int
merge_profile_sections(ini_section_t *remote,
    const ini_section_t *remote_compare, const ini_section_t *base,
    const ini_section_t *local)
{
	/* Materialize aliases before removing them, including unchanged values. */
	static const char *const canonical[] = { "ConnectionType", "ProtocolLogLevel", "Graphics" };
	for (size_t i = 0; i < sizeof(canonical) / sizeof(canonical[0]); ++i) {
		const char *value = ini_section_find(remote_compare, canonical[i]);
		if (value != NULL) {
			int status = ini_section_set_string(remote, canonical[i], value,
			    &ini_default_style);
			if (status < 0)
				return status;
		}
	}
	/* Preserve the remote's decoded legacy encoding before changing the
	 * font or transport used to infer it. Explicit local encoding edits
	 * still go through the normal three-way merge below. */
	if (ini_section_find(remote, "Encoding") == NULL) {
		const char *encoding = ini_section_find(remote_compare, "Encoding");
		int status;

		if (encoding != NULL) {
			status = ini_section_set_string(remote, "Encoding", encoding,
			    &ini_default_style);
			if (status < 0)
				return status;
		}
	}
	for (size_t i = 0; i < sizeof(profile_keys) / sizeof(profile_keys[0]); ++i) {
		const char *key = profile_keys[i];
		const char *base_value = base == NULL ? NULL
		    : ini_section_find(base, key);
		const char *local_value = local == NULL ? NULL
		    : ini_section_find(local, key);
		const char *remote_value = remote_compare == NULL ? NULL
		    : ini_section_find(remote_compare, key);
		int status;

		if (same_value(base_value, local_value))
			continue;
		if (!same_value(base_value, remote_value)
		    && !same_value(local_value, remote_value))
			return IO_ERROR_CONFLICT;
		if (local_value == NULL) {
			(void)ini_section_remove_key(remote, key);
			continue;
		}
		status = ini_section_set_string(remote, key, local_value,
		    &ini_default_style);
		if (status < 0)
			return status;
	}
	(void)ini_section_remove_key(remote, "BeDumb");
	(void)ini_section_remove_key(remote, "TelnetLogLevel");
	(void)ini_section_remove_key(remote, "RIP");
	return 0;
}

static void
remove_older_sections(ini_document_t *document, const char *name,
    ini_section_t *keep)
{
	size_t count = ini_document_section_count(document);

	while (count != 0) {
		ini_section_t *section = ini_document_section_at(document, --count);

		if (section != keep && section_name_equal(section, name))
			(void)ini_document_remove_section(document, section);
	}
}

static void
install_document(st_bbs_list_t *list, ini_document_t **document,
    const struct st_list_crypto_info *info, struct st_bbs_view *view)
{
	ini_document_free(&list->personal_document);
	list->personal_document = *document;
	*document = NULL;
	list->crypto = *info;
	st_bbs_picker_key_load(list, list->personal_document, info);
	st_bbs_view_publish(list, view);
	list->generation++;
}

static void
crypto_info_for_options(struct st_list_crypto_info *info,
    const struct st_list_crypto_options *options)
{
	memset(info, 0, sizeof(*info));
	info->format = options->cipher == ST_LIST_CIPHER_NONE
	    ? ST_LIST_CRYPTO_PLAIN : ST_LIST_CRYPTO_V2;
	info->cipher = options->cipher;
	info->key_bits = options->key_bits;
	info->kdf = options->cipher == ST_LIST_CIPHER_NONE
	    ? ST_LIST_KDF_NONE : ST_LIST_KDF_SCRYPT;
	info->cost_log2 = options->cost_log2;
	info->block_size = options->block_size;
	info->parallelism = options->parallelism;
}

static void
initialize_result(struct st_bbs_mutation_result *result)
{
	if (result != NULL)
		result->cache_status = 0;
}

static int
begin_write(st_bbs_list_t *list, st_file_lock_t **lock,
    ini_document_t **remote, struct st_list_crypto_info *info)
{
	int status = fs_create_parent_directories(list->personal_path);

	if (status == 0)
		status = st_file_lock_acquire(lock, list->personal_path, true);
	if (status == 0)
		status = read_personal(list, remote, info);
	if (status == 0 && info->format == ST_LIST_CRYPTO_V1)
		status = IO_ERROR_NOT_SUPPORTED;
	return status;
}

static const struct st_profile *
baseline_personal(const st_bbs_list_t *list, const char *name)
{
	const struct st_bbs_entry *entry = st_bbs_list_find(list, name);

	return entry != NULL && entry->source == ST_BBS_SOURCE_PERSONAL
	    ? entry->profile : NULL;
}

int
st_bbs_list_record_connection(st_bbs_list_t *list,
	const struct st_profile *profile, time_t connected, uint32_t *calls)
{
	ini_document_t *remote = NULL;
	ini_section_t *section = NULL;
	struct st_profile value;
	struct st_list_crypto_info info;
	struct st_bbs_view view = {0};
	st_file_lock_t *lock = NULL;
	bool unchanged = false;
	int status;

	if (list == NULL || profile == NULL || calls == NULL
	    || !st_bbs_name_valid(profile->name))
		return IO_ERROR_INVALID_ARGUMENT;
	st_profile_init(&value);
	status = begin_write(list, &lock, &remote, &info);
	if (status == 0) {
		section = st_bbs_find_profile_section(remote, profile->name);
		status = documents_equal(&unchanged, remote, list->personal_document);
	}
	if (status == 0 && section != NULL)
		status = st_bbs_decode_profile(&value, section, profile->name,
		    false, list->download_path);
	else if (status == 0 && profile->source != ST_PROFILE_SOURCE_SYSTEM)
		status = IO_ERROR_NOT_FOUND;
	else if (status == 0) {
		if (!st_profile_copy(&value, profile))
			status = IO_ERROR_OUT_OF_MEMORY;
		if (status == 0) {
			value.source = ST_PROFILE_SOURCE_USER;
			value.added = connected;
			value.calls = 0;
			status = ini_document_add_section(remote, profile->name,
			    &ini_default_style, &section);
		}
		if (status == 0)
			status = st_bbs_encode_profile(section, &value, false);
	}
	if (status == 0) {
		++value.calls;
		value.connected = connected;
		status = ini_section_set_uint64(section, "TotalCalls", value.calls,
		    &ini_default_style);
	}
	if (status == 0)
		status = ini_section_set_datetime(section, "LastConnected", connected,
		    true, &ini_default_style);
	if (status == 0)
		status = unchanged && baseline_personal(list, profile->name) != NULL
		    ? st_bbs_view_prepare_update(list, profile->name,
		    &value, remote, &view) : st_bbs_view_prepare(list, remote, &view);
	if (status == 0)
		status = write_personal(list, remote, &list->write_options,
		    list->password, list->password_size);
	if (status == 0) {
		crypto_info_for_options(&info, &list->write_options);
		install_document(list, &remote, &info, &view);
		*calls = value.calls;
	}
	st_bbs_view_dispose(&view);
	st_profile_dispose(&value);
	ini_document_free(&remote);
	st_file_lock_release(lock);
	return status;
}

int
st_bbs_list_add(st_bbs_list_t *list, const struct st_profile *profile,
    struct st_bbs_mutation_result *result)
{
	ini_document_t *remote = NULL;
	ini_section_t *section = NULL;
	struct st_list_crypto_info info;
	struct st_profile value;
	struct st_bbs_view view = {0};
	st_file_lock_t *lock = NULL;
	int status;

	initialize_result(result);
	if (list == NULL || profile == NULL || !st_bbs_name_valid(profile->name))
		return IO_ERROR_INVALID_ARGUMENT;
	status = begin_write(list, &lock, &remote, &info);
	if (status == 0 && st_bbs_find_profile_section(remote, profile->name) != NULL)
		status = IO_ERROR_ALREADY_EXISTS;
	st_profile_init(&value);
	if (status == 0 && !st_profile_copy(&value, profile))
		status = IO_ERROR_OUT_OF_MEMORY;
	if (status == 0) {
		value.added = time(NULL);
		value.connected = 0;
		value.calls = 0;
		status = ini_document_add_section(remote, value.name,
		    &ini_default_style, &section);
	}
	if (status == 0)
		status = st_bbs_encode_profile(section, &value, false);
	if (status == 0)
		status = st_bbs_view_prepare(list, remote, &view);
	if (status == 0)
		status = write_personal(list, remote, &list->write_options,
		    list->password, list->password_size);
	if (status == 0) {
		crypto_info_for_options(&info, &list->write_options);
		install_document(list, &remote, &info, &view);
	}
	st_bbs_view_dispose(&view);
	st_profile_dispose(&value);
	ini_document_free(&remote);
	st_file_lock_release(lock);
	return status;
}

int
st_bbs_list_update(st_bbs_list_t *list, const char *old_name,
    const struct st_profile *profile, struct st_bbs_mutation_result *result)
{
	ini_document_t *remote = NULL;
	ini_document_t *local = NULL;
	ini_document_t *canonical_base = NULL;
	ini_document_t *canonical_remote = NULL;
	ini_section_t *remote_section;
	ini_section_t *base_section;
	ini_section_t *local_section;
	ini_section_t *compare_remote_section;
	struct st_list_crypto_info info;
	struct st_profile remote_profile;
	struct st_profile merged_profile;
	struct st_bbs_view view = {0};
	const struct st_profile *base_profile;
	st_file_lock_t *lock = NULL;
	char old_cache_name[ST_PROFILE_NAME_MAX + 1];
	char new_cache_name[ST_PROFILE_NAME_MAX + 1];
	bool rename_cache;
	bool rename_section;
	bool remote_unchanged = false;
	int status;

	initialize_result(result);
	if (list == NULL || profile == NULL || !st_bbs_name_valid(old_name)
	    || !st_bbs_name_valid(profile->name))
		return IO_ERROR_INVALID_ARGUMENT;
	base_profile = baseline_personal(list, old_name);
	if (base_profile == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	(void)snprintf(old_cache_name, sizeof(old_cache_name), "%s", base_profile->name);
	(void)snprintf(new_cache_name, sizeof(new_cache_name), "%s", profile->name);
	rename_cache = !st_bbs_name_equal(old_name, profile->name);
	rename_section = strcmp(old_name, profile->name) != 0;
	status = begin_write(list, &lock, &remote, &info);
	if (status == 0)
		status = documents_equal(&remote_unchanged, remote,
		    list->personal_document);
	remote_section = status == 0
	    ? st_bbs_find_profile_section(remote, old_name) : NULL;
	if (status == 0 && remote_section == NULL)
		status = IO_ERROR_CONFLICT;
	if (status == 0 && rename_cache
	    && st_bbs_find_profile_section(remote, profile->name) != NULL)
		status = IO_ERROR_ALREADY_EXISTS;
	if (status == 0)
		status = ini_document_clone(&canonical_base, list->personal_document);
	base_section = status == 0
	    ? st_bbs_find_profile_section(canonical_base, old_name) : NULL;
	if (status == 0 && base_section != NULL)
		status = st_bbs_encode_profile(base_section, base_profile, false);
	if (status == 0)
		status = ini_document_clone(&local, canonical_base);
	local_section = status == 0
	    ? st_bbs_find_profile_section(local, old_name) : NULL;
	if (status == 0 && (base_section == NULL || local_section == NULL))
		status = IO_ERROR_CONFLICT;
	if (status == 0)
		status = st_bbs_encode_profile(local_section, profile, false);
	st_profile_init(&remote_profile);
	if (status == 0)
		status = st_bbs_decode_profile(&remote_profile, remote_section,
		    old_name, false, list->download_path);
	if (status == 0)
		status = ini_document_clone(&canonical_remote, remote);
	compare_remote_section = status == 0
	    ? st_bbs_find_profile_section(canonical_remote, old_name) : NULL;
	if (status == 0 && compare_remote_section != NULL)
		status = st_bbs_encode_profile(compare_remote_section,
		    &remote_profile, false);
	if (status == 0)
		status = merge_profile_sections(remote_section, compare_remote_section,
		    base_section, local_section);
	if (status == 0 && rename_section
	    && !ini_document_rename_section(remote, remote_section,
	    profile->name))
		status = IO_ERROR_INVALID_ARGUMENT;
	if (status == 0)
		remove_older_sections(remote, old_name, remote_section);
	st_profile_init(&merged_profile);
	if (status == 0 && remote_unchanged)
		status = st_bbs_decode_profile(&merged_profile, remote_section,
		    profile->name, false, list->download_path);
	if (status == 0)
		status = remote_unchanged
		    ? st_bbs_view_prepare_update(list, old_name, &merged_profile,
		    remote, &view)
		    : st_bbs_view_prepare(list, remote, &view);
	if (status == 0)
		status = write_personal(list, remote, &list->write_options,
		    list->password, list->password_size);
	if (status == 0) {
		crypto_info_for_options(&info, &list->write_options);
		install_document(list, &remote, &info, &view);
	}
	if (status == 0 && rename_cache) {
		int cache_status = st_bbs_cache_rename(list, old_cache_name,
		    new_cache_name);
		if (result != NULL)
			result->cache_status = cache_status;
	}
	ini_document_free(&local);
	ini_document_free(&canonical_base);
	ini_document_free(&canonical_remote);
	ini_document_free(&remote);
	st_bbs_view_dispose(&view);
	st_profile_dispose(&merged_profile);
	st_profile_dispose(&remote_profile);
	st_file_lock_release(lock);
	return status;
}

int
st_bbs_list_remove(st_bbs_list_t *list, const char *name,
    struct st_bbs_mutation_result *result)
{
	ini_document_t *remote = NULL;
	struct st_list_crypto_info info;
	struct st_profile remote_profile;
	struct st_bbs_view view = {0};
	const struct st_profile *base_profile;
	st_file_lock_t *lock = NULL;
	char cache_name[ST_PROFILE_NAME_MAX + 1];
	bool found = false;
	size_t count;
	int status;

	initialize_result(result);
	if (list == NULL || !st_bbs_name_valid(name))
		return IO_ERROR_INVALID_ARGUMENT;
	base_profile = baseline_personal(list, name);
	if (base_profile == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	(void)snprintf(cache_name, sizeof(cache_name), "%s", base_profile->name);
	status = begin_write(list, &lock, &remote, &info);
	st_profile_init(&remote_profile);
	if (status == 0) {
		ini_section_t *effective = st_bbs_find_profile_section(remote, name);

		if (effective == NULL)
			status = IO_ERROR_CONFLICT;
		else
			status = st_bbs_decode_profile(&remote_profile, effective, name,
			    false, list->download_path);
	}
	if (status == 0 && !st_bbs_profiles_equal(base_profile, &remote_profile))
		status = IO_ERROR_CONFLICT;
	count = status == 0 ? ini_document_section_count(remote) : 0;
	while (count != 0) {
		ini_section_t *section = ini_document_section_at(remote, --count);

		if (section_name_equal(section, name)) {
			found = true;
			(void)ini_document_remove_section(remote, section);
		}
	}
	if (status == 0 && !found)
		status = IO_ERROR_CONFLICT;
	if (status == 0)
		status = st_bbs_view_prepare(list, remote, &view);
	if (status == 0)
		status = write_personal(list, remote, &list->write_options,
		    list->password, list->password_size);
	if (status == 0) {
		crypto_info_for_options(&info, &list->write_options);
		install_document(list, &remote, &info, &view);
	}
	if (status == 0) {
		int cache_status = st_bbs_cache_remove(list, cache_name);
		if (result != NULL)
			result->cache_status = cache_status;
	}
	ini_document_free(&remote);
	st_bbs_view_dispose(&view);
	st_profile_dispose(&remote_profile);
	st_file_lock_release(lock);
	return status;
}

int
st_bbs_list_set_defaults(st_bbs_list_t *list,
    const struct st_profile *defaults)
{
	ini_document_t *remote = NULL;
	ini_document_t *local = NULL;
	ini_document_t *canonical_base = NULL;
	ini_document_t *canonical_remote = NULL;
	ini_section_t *remote_root;
	ini_section_t *base_root;
	ini_section_t *local_root;
	ini_section_t *compare_remote_root;
	struct st_list_crypto_info info;
	struct st_profile remote_defaults;
	struct st_bbs_view view = {0};
	st_file_lock_t *lock = NULL;
	int status;

	if (list == NULL || defaults == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = begin_write(list, &lock, &remote, &info);
	if (status == 0)
		status = ini_document_clone(&canonical_base, list->personal_document);
	remote_root = status == 0 ? ini_document_root(remote) : NULL;
	base_root = status == 0 ? ini_document_root(canonical_base) : NULL;
	if (status == 0)
		status = st_bbs_encode_profile(base_root, &list->view.defaults, true);
	if (status == 0)
		status = ini_document_clone(&local, canonical_base);
	local_root = status == 0 ? ini_document_root(local) : NULL;
	if (status == 0)
		status = st_bbs_encode_profile(local_root, defaults, true);
	st_profile_init(&remote_defaults);
	if (status == 0)
		status = st_bbs_decode_profile(&remote_defaults, remote_root, NULL,
		    false, list->download_path);
	if (status == 0)
		status = ini_document_clone(&canonical_remote, remote);
	compare_remote_root = status == 0
	    ? ini_document_root(canonical_remote) : NULL;
	if (status == 0)
		status = st_bbs_encode_profile(compare_remote_root, &remote_defaults,
		    true);
	if (status == 0)
		status = merge_profile_sections(remote_root, compare_remote_root,
		    base_root, local_root);
	if (status == 0)
		status = st_bbs_view_prepare(list, remote, &view);
	if (status == 0)
		status = write_personal(list, remote, &list->write_options,
		    list->password, list->password_size);
	if (status == 0) {
		crypto_info_for_options(&info, &list->write_options);
		install_document(list, &remote, &info, &view);
	}
	ini_document_free(&local);
	ini_document_free(&canonical_base);
	ini_document_free(&canonical_remote);
	ini_document_free(&remote);
	st_bbs_view_dispose(&view);
	st_profile_dispose(&remote_defaults);
	st_file_lock_release(lock);
	return status;
}

static void
replace_password(st_bbs_list_t *list, unsigned char *password, size_t size)
{
	st_crypto_clear(list->password, list->password_size);
	free(list->password);
	list->password = password;
	list->password_size = size;
}

int
st_bbs_list_rewrite_encryption(st_bbs_list_t *list,
    const struct st_list_crypto_options *options, const void *password,
    size_t password_size)
{
	ini_document_t *remote = NULL;
	struct st_list_crypto_info info;
	struct st_list_crypto_options selected = {0};
	struct st_bbs_view view = {0};
	unsigned char *new_password = NULL;
	st_file_lock_t *lock = NULL;
	int status;

	if (list == NULL || (password_size != 0 && password == NULL))
		return IO_ERROR_INVALID_ARGUMENT;
	if (options != NULL)
		selected = *options;
	if (selected.cipher != ST_LIST_CIPHER_NONE && password_size == 0)
		return IO_ERROR_PASSWORD_REQUIRED;
	if (password_size != 0) {
		new_password = malloc(password_size);
		if (new_password == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		memcpy(new_password, password, password_size);
	}
	status = fs_create_parent_directories(list->personal_path);
	if (status == 0)
		status = st_file_lock_acquire(&lock, list->personal_path, true);
	if (status == 0)
		status = read_personal(list, &remote, &info);
	if (status == 0 && selected.cipher == ST_LIST_CIPHER_NONE)
		(void)ini_section_remove_key(ini_document_root(remote),
		    picker_key_name);
	if (status == 0)
		status = st_bbs_view_prepare(list, remote, &view);
	if (status == 0)
		status = write_personal(list, remote, &selected, password,
		    password_size);
	if (status == 0) {
		crypto_info_for_options(&info, &selected);
		replace_password(list, new_password, password_size);
		new_password = NULL;
		list->write_options = selected;
		install_document(list, &remote, &info, &view);
	}
	if (new_password != NULL) {
		st_crypto_clear(new_password, password_size);
		free(new_password);
	}
	ini_document_free(&remote);
	st_bbs_view_dispose(&view);
	st_file_lock_release(lock);
	return status;
}

int
st_bbs_list_rewrite_encryption_current_password(st_bbs_list_t *list,
    const struct st_list_crypto_options *options)
{
	if (list == NULL || list->password_size == 0)
		return IO_ERROR_PASSWORD_REQUIRED;
	return st_bbs_list_rewrite_encryption(list, options, list->password,
	    list->password_size);
}

int
st_bbs_list_picker_key(st_bbs_list_t *list, uint8_t key[32], bool *available)
{
	ini_document_t *remote = NULL;
	struct st_list_crypto_info info;
	struct st_bbs_view view = {0};
	st_file_lock_t *lock = NULL;
	uint8_t generated[ST_CONSENT_TOKEN_KEY_SIZE];
	char encoded[ST_CONSENT_TOKEN_KEY_SIZE * 2 + 1];
	const char *stored;
	int status;

	if (list == NULL || key == NULL || available == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*available = false;
	if (list->crypto.format != ST_LIST_CRYPTO_V2)
		return 0;
	if (list->picker_key_available) {
		memcpy(key, list->picker_key, sizeof(list->picker_key));
		*available = true;
		return 0;
	}
	status = begin_write(list, &lock, &remote, &info);
	if (status == 0 && info.format != ST_LIST_CRYPTO_V2)
		status = IO_ERROR_CONFLICT;
	stored = status == 0 ? ini_section_find(ini_document_root(remote),
	    picker_key_name) : NULL;
	if (status == 0 && stored != NULL
	    && strlen(stored) == sizeof(generated) * 2
	    && st_hex_decode(generated, sizeof(generated), stored,
	    strlen(stored))) {
		memcpy(list->picker_key, generated, sizeof(generated));
		list->picker_key_available = true;
	}
	else if (status == 0) {
		if (st_consent_token_generate_key(generated) != ST_CRYPTO_OK)
			status = IO_ERROR_NOT_SUPPORTED;
		if (status == 0 && !st_hex_encode(encoded, sizeof(encoded), generated,
		    sizeof(generated)))
			status = IO_ERROR_OUT_OF_RANGE;
		if (status == 0)
			status = ini_section_set_string(ini_document_root(remote),
			    picker_key_name, encoded, &ini_default_style);
		if (status == 0)
			status = st_bbs_view_prepare(list, remote, &view);
		if (status == 0)
			status = write_personal(list, remote, &list->write_options,
			    list->password, list->password_size);
		if (status == 0) {
			crypto_info_for_options(&info, &list->write_options);
			install_document(list, &remote, &info, &view);
		}
	}
	if (status == 0 && list->picker_key_available) {
		memcpy(key, list->picker_key, sizeof(list->picker_key));
		*available = true;
	}
	st_crypto_clear(generated, sizeof(generated));
	ini_document_free(&remote);
	st_bbs_view_dispose(&view);
	st_file_lock_release(lock);
	return status;
}
