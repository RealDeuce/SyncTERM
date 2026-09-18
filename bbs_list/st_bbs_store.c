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
#include "st_file_lock.h"
#include "xp_crypto.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *profile_keys[] = {
	"Address", "Port", "Added", "LastConnected", "TotalCalls", "UserName",
	"Password", "SystemPassword", "ConnectionType", "FlowControl",
	"ScreenMode", "NoStatus", "DownloadPath", "UploadPath", "LogFile",
	"TransferLogLevel", "ProtocolLogLevel", "AppendLogFile", "BPSRate",
	"ANSIMusic", "AddressFamily", "Font", "HidePopups", "RIP", "Comment",
	"WrenScripts", "ForceLCF", "YellowIsYellow", "TerminalType", "LFExpand",
	"TelnetBrokenTextmode", "TelnetDeferNegotiate", "TLSTrustWebPKI",
	"TLSTrustedCertificate", "TLSClientCertificate", "TLSClientKey",
	"TLSPSKIdentity", "TLSPSK", "TLSPSKVersion", "TLSVersionFloor",
	"SSHFingerprint", "SFTPPublicKey", "SSHAllowAES128CBC",
	"SSHAcceptEarlyData", "StopBits", "DataBits", "Parity", "Palette",
	"SortOrder"
};

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
	if (fclose(stream) != 0 && status == 0)
		status = IO_ERROR_IO;
	return status;
}

static int
write_personal(st_bbs_list_t *list, const ini_document_t *document,
    const struct st_list_crypto_options *options, const void *password,
    size_t password_size)
{
	FILE *prepared = tmpfile();
	FILE *output;
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
merge_profile_sections(ini_section_t *remote,
    const ini_section_t *remote_compare, const ini_section_t *base,
    const ini_section_t *local)
{
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
	return 0;
}

static void
remove_older_sections(ini_document_t *document, const char *name,
    ini_section_t *keep)
{
	size_t count = ini_document_section_count(document);

	while (count != 0) {
		ini_section_t *section = ini_document_section_at(document, --count);

		if (section != keep && st_bbs_name_equal(ini_section_name(section), name))
			(void)ini_document_remove_section(document, section);
	}
}

static int
install_document(st_bbs_list_t *list, ini_document_t **document,
    const struct st_list_crypto_info *info)
{
	ini_document_t *fresh = NULL;
	struct st_list_crypto_info fresh_info;
	int status;
	(void)info;

	status = read_personal(list, &fresh, &fresh_info);
	if (status < 0)
		return status;
	ini_document_free(&list->personal_document);
	ini_document_free(document);
	list->personal_document = fresh;
	list->crypto = fresh_info;
	status = st_bbs_list_rebuild(list);
	if (status == 0 && list->sort_field_count != 0)
		status = st_bbs_list_sort(list, list->sort_fields,
		    list->sort_field_count);
	if (status == 0)
		list->generation++;
	return status;
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
st_bbs_list_add(st_bbs_list_t *list, const struct st_profile *profile,
    struct st_bbs_mutation_result *result)
{
	ini_document_t *remote = NULL;
	ini_section_t *section;
	struct st_list_crypto_info info;
	struct st_profile value;
	st_file_lock_t *lock = NULL;
	int status;

	initialize_result(result);
	if (list == NULL || profile == NULL || !st_bbs_name_valid(profile->name))
		return IO_ERROR_INVALID_ARGUMENT;
	status = begin_write(list, &lock, &remote, &info);
	if (status == 0 && ini_document_find_section(remote, profile->name) != NULL)
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
		status = write_personal(list, remote, &list->write_options,
		    list->password, list->password_size);
	if (status == 0)
		status = install_document(list, &remote, &info);
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
	const struct st_profile *base_profile;
	st_file_lock_t *lock = NULL;
	int status;

	initialize_result(result);
	if (list == NULL || profile == NULL || !st_bbs_name_valid(old_name)
	    || !st_bbs_name_valid(profile->name)
	    || baseline_personal(list, old_name) == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = begin_write(list, &lock, &remote, &info);
	base_profile = baseline_personal(list, old_name);
	remote_section = status == 0
	    ? ini_document_find_section(remote, old_name) : NULL;
	if (status == 0 && remote_section == NULL)
		status = IO_ERROR_CONFLICT;
	if (status == 0 && !st_bbs_name_equal(old_name, profile->name)
	    && ini_document_find_section(remote, profile->name) != NULL)
		status = IO_ERROR_ALREADY_EXISTS;
	if (status == 0)
		status = ini_document_clone(&canonical_base, list->personal_document);
	base_section = status == 0
	    ? ini_document_find_section(canonical_base, old_name) : NULL;
	if (status == 0 && base_section != NULL)
		status = st_bbs_encode_profile(base_section, base_profile, false);
	if (status == 0)
		status = ini_document_clone(&local, canonical_base);
	local_section = status == 0
	    ? ini_document_find_section(local, old_name) : NULL;
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
	    ? ini_document_find_section(canonical_remote, old_name) : NULL;
	if (status == 0 && compare_remote_section != NULL)
		status = st_bbs_encode_profile(compare_remote_section,
		    &remote_profile, false);
	if (status == 0)
		status = merge_profile_sections(remote_section, compare_remote_section,
		    base_section, local_section);
	if (status == 0 && !st_bbs_name_equal(old_name, profile->name)
	    && !ini_document_rename_section(remote, remote_section,
	    profile->name))
		status = IO_ERROR_INVALID_ARGUMENT;
	if (status == 0)
		remove_older_sections(remote, old_name, remote_section);
	if (status == 0)
		status = write_personal(list, remote, &list->write_options,
		    list->password, list->password_size);
	if (status == 0)
		status = install_document(list, &remote, &info);
	if (status == 0 && !st_bbs_name_equal(old_name, profile->name)) {
		int cache_status = st_bbs_cache_rename(list, old_name, profile->name);
		if (result != NULL)
			result->cache_status = cache_status;
	}
	ini_document_free(&local);
	ini_document_free(&canonical_base);
	ini_document_free(&canonical_remote);
	ini_document_free(&remote);
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
	const struct st_profile *base_profile;
	st_file_lock_t *lock = NULL;
	bool found = false;
	size_t count;
	int status;

	initialize_result(result);
	if (list == NULL || !st_bbs_name_valid(name)
	    || baseline_personal(list, name) == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = begin_write(list, &lock, &remote, &info);
	base_profile = baseline_personal(list, name);
	st_profile_init(&remote_profile);
	if (status == 0) {
		ini_section_t *effective = ini_document_find_section(remote, name);

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

		if (st_bbs_name_equal(ini_section_name(section), name)) {
			found = true;
			(void)ini_document_remove_section(remote, section);
		}
	}
	if (status == 0 && !found)
		status = IO_ERROR_CONFLICT;
	if (status == 0)
		status = write_personal(list, remote, &list->write_options,
		    list->password, list->password_size);
	if (status == 0)
		status = install_document(list, &remote, &info);
	if (status == 0) {
		int cache_status = st_bbs_cache_remove(list, name);
		if (result != NULL)
			result->cache_status = cache_status;
	}
	ini_document_free(&remote);
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
		status = st_bbs_encode_profile(base_root, &list->defaults, true);
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
		status = write_personal(list, remote, &list->write_options,
		    list->password, list->password_size);
	if (status == 0)
		status = install_document(list, &remote, &info);
	ini_document_free(&local);
	ini_document_free(&canonical_base);
	ini_document_free(&canonical_remote);
	ini_document_free(&remote);
	st_profile_dispose(&remote_defaults);
	st_file_lock_release(lock);
	return status;
}

static void
replace_password(st_bbs_list_t *list, unsigned char *password, size_t size)
{
	xp_crypto_clear(list->password, list->password_size);
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
	if (status == 0)
		status = write_personal(list, remote, &selected, password,
		    password_size);
	if (status == 0) {
		replace_password(list, new_password, password_size);
		new_password = NULL;
		memset(&list->crypto, 0, sizeof(list->crypto));
		list->crypto.format = selected.cipher == ST_LIST_CIPHER_NONE
		    ? ST_LIST_CRYPTO_PLAIN : ST_LIST_CRYPTO_V2;
		list->crypto.cipher = selected.cipher;
		list->crypto.key_bits = selected.key_bits;
		list->crypto.kdf = selected.cipher == ST_LIST_CIPHER_NONE
		    ? ST_LIST_KDF_NONE : ST_LIST_KDF_SCRYPT;
		list->crypto.cost_log2 = selected.cost_log2;
		list->crypto.block_size = selected.block_size;
		list->crypto.parallelism = selected.parallelism;
		list->write_options = selected;
		ini_document_free(&list->personal_document);
		list->personal_document = remote;
		remote = NULL;
		status = st_bbs_list_rebuild(list);
		if (status == 0 && list->sort_field_count != 0)
			status = st_bbs_list_sort(list, list->sort_fields,
			    list->sort_field_count);
		if (status == 0)
			list->generation++;
	}
	if (new_password != NULL) {
		xp_crypto_clear(new_password, password_size);
		free(new_password);
	}
	ini_document_free(&remote);
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
