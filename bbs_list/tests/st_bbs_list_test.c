/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "st_bbs_list_internal.h"

#include "filesystem.h"
#include "io_error.h"
#include "st_app_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ST_BBS_TEST_DIRECTORY
#error "ST_BBS_TEST_DIRECTORY is required"
#endif

#define CHECK(e) do { if (!(e)) { \
	fprintf(stderr, "failed: %s:%d: %s\n", __FILE__, __LINE__, #e); \
	return 1; \
} } while (0)

static int
write_text(const char *path, const char *text)
{
	FILE *stream;
	int status = fs_open(&stream, path, "wb");
	if (status < 0)
		return status;
	if (fwrite(text, 1, strlen(text), stream) != strlen(text))
		status = IO_ERROR_IO;
	if (fclose(stream) != 0 && status == 0)
		status = IO_ERROR_IO;
	return status;
}

static bool
file_contains(const char *path, const char *text)
{
	char buffer[8192];
	FILE *stream;
	size_t size;
	bool okay;

	if (fs_open(&stream, path, "rb") != 0)
		return false;
	size = fread(buffer, 1, sizeof(buffer) - 1, stream);
	okay = !ferror(stream) && fclose(stream) == 0;
	buffer[size] = '\0';
	return okay && strstr(buffer, text) != NULL;
}

static int
copy_named(struct st_profile *result, const st_bbs_list_t *list,
    const char *name)
{
	const struct st_bbs_entry *entry = st_bbs_list_find(list, name);
	return entry != NULL && st_profile_copy(result, entry->profile) ? 0 : -1;
}

static int
check_profile_default_ports(void)
{
	static const struct {
		const char *name;
		int type;
	} cases[] = {
		{"Unknown", ST_CONNECTION_UNKNOWN},
		{"RLogin", ST_CONNECTION_RLOGIN},
		{"RLoginReversed", ST_CONNECTION_RLOGIN_REVERSED},
		{"Telnet", ST_CONNECTION_TELNET},
		{"Raw", ST_CONNECTION_RAW},
		{"SSH", ST_CONNECTION_SSH},
		{"SSHNA", ST_CONNECTION_SSH_NO_AUTH},
		{"Modem", ST_CONNECTION_MODEM},
		{"Serial", ST_CONNECTION_SERIAL},
		{"NoRTS", ST_CONNECTION_SERIAL_3WIRE},
		{"Shell", ST_CONNECTION_SHELL},
		{"MBBSGhost", ST_CONNECTION_MBBS_GHOST},
		{"TelnetS", ST_CONNECTION_TELNET_TLS},
		{"MQTT", ST_CONNECTION_MQTT},
	};
	char text[128];

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		ini_document_t *document = NULL;
		struct st_profile profile;
		ini_section_t *section;
		int length = snprintf(text, sizeof(text),
		    "[Profile]\nConnectionType=%s\n", cases[i].name);

		CHECK(length > 0 && (size_t)length < sizeof(text));
		CHECK(ini_document_parse(&document, text, (size_t)length) == 0);
		section = ini_document_find_section(document, "Profile");
		CHECK(section != NULL);
		CHECK(st_bbs_decode_profile(&profile, section, "Profile", false,
		    NULL) == 0);
		CHECK(profile.connection_type == cases[i].type);
		CHECK(profile.port == st_connection_default_port(
		    (enum st_connection_type)cases[i].type));
		st_profile_dispose(&profile);
		ini_document_free(&document);
	}
	{
		static const char explicit_port[] =
		    "[Profile]\nConnectionType=Raw\nPort=4567\n";
		ini_document_t *document = NULL;
		struct st_profile profile;
		ini_section_t *section;

		CHECK(ini_document_parse(&document, explicit_port,
		    sizeof(explicit_port) - 1) == 0);
		section = ini_document_find_section(document, "Profile");
		CHECK(section != NULL);
		CHECK(st_bbs_decode_profile(&profile, section, "Profile", false,
		    NULL) == 0);
		CHECK(profile.port == 4567);
		st_profile_dispose(&profile);
		ini_document_free(&document);
	}
	return 0;
}

int
main(void)
{
	static const char personal_text[] =
	    "; personal comment\n[Alpha]\nAddress=old.example\nUnknown=keep\n"
	    "[Alpha]\nAddress=personal.example\n";
	static const char shared_text[] =
	    "[Alpha]\nAddress=shared.example\n[Shared]\n"
	    "Address=shared-only.example\nUserName=ignored\nPassword=ignored\n"
	    "TotalCalls=99\n";
	static const char password[] = "list password";
	struct st_app_paths paths = {0};
	struct st_profile profile;
	struct st_profile other;
	struct st_bbs_mutation_result mutation;
	struct st_list_crypto_options crypto = ST_LIST_CRYPTO_OPTIONS_AES256;
	st_bbs_list_t *first = NULL;
	st_bbs_list_t *second = NULL;
	st_bbs_list_t *verify = NULL;
	char personal[sizeof(ST_BBS_TEST_DIRECTORY) + 32];
	char shared[sizeof(ST_BBS_TEST_DIRECTORY) + 32];
	char cache[sizeof(ST_BBS_TEST_DIRECTORY) + 32];
	char system_cache[sizeof(ST_BBS_TEST_DIRECTORY) + 64];
	char system_list[sizeof(ST_BBS_TEST_DIRECTORY) + 96];
	char lock_path[sizeof(personal) + 8];
	char cache_item[sizeof(cache) + 64];
	char *public_cache_path = NULL;
	uint8_t picker_key[32];
	uint8_t reopened_picker_key[32];
	bool picker_key_available;
	struct fs_info info;
	const struct st_bbs_entry *entry;
	int status;

	CHECK(check_profile_default_ports() == 0);

	(void)snprintf(personal, sizeof(personal), "%s/personal.lst",
	    ST_BBS_TEST_DIRECTORY);
	(void)snprintf(shared, sizeof(shared), "%s/shared.lst",
	    ST_BBS_TEST_DIRECTORY);
	(void)snprintf(cache, sizeof(cache), "%s/cache", ST_BBS_TEST_DIRECTORY);
	(void)snprintf(system_cache, sizeof(system_cache), "%s/cache/system",
	    ST_BBS_TEST_DIRECTORY);
	(void)snprintf(system_list, sizeof(system_list), "%s/System List.lst",
	    system_cache);
	CHECK(fs_create_directories(system_cache) == 0);
	CHECK(write_text(personal, personal_text) == 0);
	CHECK(write_text(shared, shared_text) == 0);
	paths.settings_file = personal;
	paths.personal_list = personal;
	paths.shared_list = shared;
	paths.cache_directory = cache;
	paths.system_cache_directory = system_cache;
	paths.cached_system_list = system_list;
	paths.download_directory = ST_BBS_TEST_DIRECTORY;

	status = st_bbs_list_open(&first, &paths, NULL, 0);
	CHECK(status == 0);
	CHECK(st_bbs_list_picker_key(first, picker_key,
	    &picker_key_available) == 0);
	CHECK(!picker_key_available);
	CHECK(st_bbs_list_cache_path(&public_cache_path, first, "Beta") == 0);
	(void)snprintf(cache_item, sizeof(cache_item), "%s/Beta", cache);
	CHECK(strcmp(public_cache_path, cache_item) == 0);
	free(public_cache_path);
	public_cache_path = NULL;
	CHECK(st_bbs_list_count(first) == 2);
	entry = st_bbs_list_find(first, "alpha");
	CHECK(entry != NULL && entry->source == ST_BBS_SOURCE_PERSONAL);
	CHECK(strcmp(entry->profile->address, "personal.example") == 0);
	CHECK(st_bbs_entry_shadowed(entry) != NULL);
	CHECK(st_bbs_entry_shadowed(entry)->source == ST_BBS_SOURCE_SHARED);
	CHECK(strcmp(st_bbs_entry_shadowed(entry)->profile->address,
	    "shared.example") == 0);
	CHECK(st_bbs_list_find(first, "Shared") != NULL);
	CHECK(st_bbs_list_find(first, "Shared")->profile->username[0] == '\0');
	CHECK(st_bbs_list_find(first, "Shared")->profile->password[0] == '\0');
	CHECK(st_bbs_list_find(first, "Shared")->profile->calls == 0);
	CHECK(st_bbs_sort_field_normalize(30) == ST_BBS_SORT_EXPLICIT);
	CHECK(st_bbs_sort_field_normalize(ST_BBS_SORT_LF_EXPAND)
	    == ST_BBS_SORT_LF_EXPAND);

	st_profile_init(&profile);
	(void)strcpy(profile.name, "Beta");
	(void)strcpy(profile.address, "beta.example");
	(void)strcpy(profile.username, "beta-user");
	(void)strcpy(profile.password, "beta-password");
	(void)strcpy(profile.system_password, "system-password");
	(void)strcpy(profile.comment, "round trip");
	(void)strcpy(profile.wren_scripts, "one.wren,two.wren");
	(void)strcpy(profile.terminal_name, "xterm-test");
	(void)strcpy(profile.tls_psk_identity, "client");
	(void)strcpy(profile.tls_psk, "00112233");
	profile.connection_type = ST_CONNECTION_TELNET_TLS;
	profile.port = 992;
	profile.screen_mode = ST_SCREEN_MODE_PRESTEL;
	profile.flow_control = ST_PROFILE_FLOW_NONE;
	profile.data_bits = 7;
	profile.stop_bits = 2;
	profile.parity = ST_PROFILE_PARITY_ODD;
	profile.music = ST_PROFILE_MUSIC_ALL;
	profile.address_family = ST_ADDRESS_FAMILY_IPV6;
	profile.rip_version = ST_RIP_VERSION_3;
	profile.tls_psk_version = ST_TLS_VERSION_1_2;
	profile.tls_version_floor = ST_TLS_VERSION_1_2;
	profile.hide_status = true;
	profile.hide_popups = true;
	profile.force_lcf = true;
	profile.yellow_is_yellow = true;
	profile.expand_lf = true;
	profile.sftp_public_key = true;
	profile.ssh_allow_aes128_cbc = true;
	profile.ssh_accept_early_data = true;
	profile.telnet_no_binary = true;
	profile.defer_telnet_negotiation = true;
	profile.bps_rate = 38400;
	profile.sort_order = 7;
	profile.palette[0] = 0x112233;
	profile.palette[1] = 0xabcdef;
	profile.palette_size = 2;
	for (size_t i = 0; i < 20; ++i)
		profile.ssh_fingerprint[i] = (uint8_t)i;
	profile.ssh_fingerprint_size = 20;
	CHECK(st_bbs_list_add(first, &profile, &mutation) == 0);
	CHECK(st_bbs_list_count(first) == 3);
	CHECK(st_bbs_list_find(first, "Alpha")->source == ST_BBS_SOURCE_PERSONAL);
	{
		const int fields[] = { ST_BBS_SORT_ADDRESS };

		CHECK(st_bbs_list_sort(first, fields, 1) == 0);
		CHECK(strcmp(st_bbs_list_at(first, 0)->profile->name, "Beta") == 0);
	}
	st_profile_dispose(&profile);

	CHECK(st_bbs_list_open(&second, &paths, NULL, 0) == 0);
	st_profile_init(&profile);
	st_profile_init(&other);
	CHECK(copy_named(&profile, first, "Beta") == 0);
	CHECK(copy_named(&other, second, "Beta") == 0);
	(void)strcpy(profile.address, "new-address.example");
	CHECK(st_bbs_list_update(first, "Beta", &profile, NULL) == 0);
	CHECK(st_bbs_list_find(first, "Alpha")->source == ST_BBS_SOURCE_PERSONAL);
	(void)strcpy(other.username, "new-user");
	CHECK(st_bbs_list_update(second, "Beta", &other, NULL) == 0);
	st_profile_dispose(&profile);
	st_profile_dispose(&other);
	CHECK(st_bbs_list_reload(first) == 0);
	CHECK(st_bbs_list_find(first, "Alpha")->source == ST_BBS_SOURCE_PERSONAL);
	CHECK(strcmp(st_bbs_list_at(first, 0)->profile->name, "Beta") == 0);
	entry = st_bbs_list_find(first, "Beta");
	CHECK(strcmp(entry->profile->address, "new-address.example") == 0);
	CHECK(strcmp(entry->profile->username, "new-user") == 0);

	CHECK(st_bbs_list_reload(second) == 0);
	st_profile_init(&profile);
	st_profile_init(&other);
	CHECK(copy_named(&profile, first, "Beta") == 0);
	CHECK(copy_named(&other, second, "Beta") == 0);
	(void)strcpy(profile.address, "winner.example");
	(void)strcpy(other.address, "loser.example");
	CHECK(st_bbs_list_update(first, "Beta", &profile, NULL) == 0);
	CHECK(st_bbs_list_find(first, "Alpha")->source == ST_BBS_SOURCE_PERSONAL);
	CHECK(st_bbs_list_update(second, "Beta", &other, NULL)
	    == IO_ERROR_CONFLICT);
	CHECK(st_bbs_list_remove(second, "Beta", NULL) == IO_ERROR_CONFLICT);
	st_profile_dispose(&profile);
	st_profile_dispose(&other);

	(void)snprintf(cache_item, sizeof(cache_item), "%s/Beta/nested", cache);
	CHECK(fs_create_directories(cache_item) == 0);
	(void)snprintf(cache_item, sizeof(cache_item), "%s/Beta/nested/item", cache);
	CHECK(write_text(cache_item, "cached") == 0);
	st_profile_init(&profile);
	CHECK(copy_named(&profile, first, "Beta") == 0);
	(void)strcpy(profile.name, "Gamma");
	CHECK(st_bbs_list_update(first, "Beta", &profile, NULL) == 0);
	(void)snprintf(cache_item, sizeof(cache_item), "%s/Beta", cache);
	CHECK(fs_get_link_info(&info, cache_item) == IO_ERROR_NOT_FOUND);
	(void)snprintf(cache_item, sizeof(cache_item), "%s/Gamma/nested/item",
	    cache);
	CHECK(fs_get_info(&info, cache_item) == 0 && info.kind == FS_ENTRY_FILE);
	st_profile_dispose(&profile);
	st_profile_init(&profile);
	CHECK(copy_named(&profile, first, "Gamma") == 0);
	(void)strcpy(profile.name, "Beta");
	CHECK(st_bbs_list_update(first, "Gamma", &profile, NULL) == 0);
	st_profile_dispose(&profile);

	(void)snprintf(cache_item, sizeof(cache_item), "%s/Alpha/nested", cache);
	CHECK(fs_create_directories(cache_item) == 0);
	status = st_bbs_list_remove(first, "Alpha", NULL);
	CHECK(status == 0);
	(void)snprintf(cache_item, sizeof(cache_item), "%s/Alpha", cache);
	CHECK(fs_get_link_info(&info, cache_item) == IO_ERROR_NOT_FOUND);
	entry = st_bbs_list_find(first, "Alpha");
	CHECK(entry != NULL && entry->source == ST_BBS_SOURCE_SHARED);
	(void)snprintf(cache_item, sizeof(cache_item), "%s/Orphan/nested", cache);
	CHECK(fs_create_directories(cache_item) == 0);
	CHECK(st_bbs_list_sweep_orphan_caches(first) == 0);
	(void)snprintf(cache_item, sizeof(cache_item), "%s/Orphan", cache);
	CHECK(fs_get_link_info(&info, cache_item) == IO_ERROR_NOT_FOUND);
	(void)snprintf(cache_item, sizeof(cache_item), "%s/Beta", cache);
	CHECK(fs_get_info(&info, cache_item) == 0
	    && info.kind == FS_ENTRY_DIRECTORY);
	CHECK(fs_get_info(&info, system_cache) == 0
	    && info.kind == FS_ENTRY_DIRECTORY);
	crypto.cost_log2 = 8;
	status = st_bbs_list_rewrite_encryption(first, &crypto, password,
	    sizeof(password) - 1);
	CHECK(status == 0 || status == IO_ERROR_NOT_SUPPORTED);
	if (status == 0) {
		CHECK(st_bbs_list_picker_key(first, picker_key,
		    &picker_key_available) == 0);
		CHECK(picker_key_available);
	}
	st_bbs_list_close(second);
	second = NULL;
	st_bbs_list_close(first);
	first = NULL;
	CHECK(st_bbs_list_open(&verify, &paths,
	    status == 0 ? password : NULL,
	    status == 0 ? sizeof(password) - 1 : 0) == 0);
	CHECK(st_bbs_list_crypto_info(verify)->format == (status == 0
	    ? ST_LIST_CRYPTO_V2 : ST_LIST_CRYPTO_PLAIN));
	CHECK(st_bbs_list_picker_key(verify, reopened_picker_key,
	    &picker_key_available) == 0);
	CHECK(picker_key_available == (status == 0));
	if (status == 0)
		CHECK(memcmp(picker_key, reopened_picker_key,
		    sizeof(picker_key)) == 0);
	CHECK(st_bbs_list_find(verify, "Beta") != NULL);
	entry = st_bbs_list_find(verify, "Beta");
	CHECK(entry->profile->connection_type == ST_CONNECTION_TELNET_TLS);
	CHECK(entry->profile->port == 992);
	CHECK(entry->profile->screen_mode == ST_SCREEN_MODE_PRESTEL);
	CHECK(entry->profile->flow_control == ST_PROFILE_FLOW_NONE);
	CHECK(entry->profile->data_bits == 7 && entry->profile->stop_bits == 2);
	CHECK(entry->profile->parity == ST_PROFILE_PARITY_ODD);
	CHECK(entry->profile->tls_psk_version == ST_TLS_VERSION_1_2);
	CHECK(entry->profile->palette_size == 2
	    && entry->profile->palette[1] == 0xabcdef);
	CHECK(entry->profile->ssh_fingerprint_size == 20
	    && entry->profile->ssh_fingerprint[19] == 19);
	CHECK(entry->profile->hide_status && entry->profile->expand_lf
	    && entry->profile->defer_telnet_negotiation);
	if (status == 0) {
		const struct st_list_crypto_options plain = {
			.cipher = ST_LIST_CIPHER_NONE,
		};

		CHECK(st_bbs_list_rewrite_encryption(verify, &plain, password,
		    sizeof(password) - 1) == 0);
		CHECK(st_bbs_list_picker_key(verify, reopened_picker_key,
		    &picker_key_available) == 0);
		CHECK(!picker_key_available);
		CHECK(!file_contains(personal, "WrenPickerHmacKey"));
	}
	st_bbs_list_close(verify);

	(void)snprintf(cache_item, sizeof(cache_item), "%s/Beta/nested/item", cache);
	(void)fs_remove_file(cache_item);
	(void)snprintf(cache_item, sizeof(cache_item), "%s/Beta/nested", cache);
	(void)fs_remove_directory(cache_item);
	(void)snprintf(cache_item, sizeof(cache_item), "%s/Beta", cache);
	(void)fs_remove_directory(cache_item);
	(void)fs_remove_file(personal);
	(void)snprintf(lock_path, sizeof(lock_path), "%s.lock", personal);
	(void)fs_remove_file(lock_path);
	(void)fs_remove_file(shared);
	(void)fs_remove_directory(system_cache);
	(void)fs_remove_directory(cache);
	(void)fs_remove_directory(ST_BBS_TEST_DIRECTORY);
	return 0;
}
