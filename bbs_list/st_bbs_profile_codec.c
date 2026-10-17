/* SyncTERM BBS-list profile INI codec. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_bbs_list_internal.h"

#include "io_error.h"
#include "st_builtin_fonts.h"
#include "st_hex.h"
#include "string_compat.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static const char *connection_types[] = {
	"Unknown", "RLogin", "RLoginReversed", "Telnet", "Raw", "SSH",
	"SSHNA", "Modem", "Serial", "NoRTS", "Shell", "MBBSGhost",
	"TelnetS", "MQTT", NULL
};

static const char *screen_modes[] = {
	"Current", "80x25", "LCD80x25", "80x28", "80x30", "80x43",
	"80x50", "80x60", "132x37", "132x52", "132x25", "132x28",
	"132x30", "132x34", "132x43", "132x50", "132x60", "C64",
	"C128-40col", "C128-80col", "Atari", "Atari-XEP80", "Custom",
	"EGA80x25", "VGA80x25", "Prestel", "BBCMicro7", "AtariST40x25",
	"AtariST80x25", "AtariST80x25Mono", NULL
};

static const char *flow_controls[] = {
	"RTSCTS", "XONXOFF", "RTSCTS_XONXOFF", "None", NULL
};
static const char *log_levels[] = {
	"Emergency", "Alert", "Critical", "Error", "Warning", "Notice",
	"Info", "Debug", NULL
};
static const char *address_families[] = { "PerDNS", "IPv4", "IPv6", NULL };
static const char *rip_versions[] = { "Off", "RIPv1", "RIPv3", NULL };
static const char *parities[] = { "None", "Even", "Odd", NULL };
static const char *tls_versions[] = { "Unknown", "TLS1.2", "TLS1.3", NULL };

#define ENUM_NAMES(table) (table), (sizeof(table) / sizeof((table)[0]) - 1)

static int
replace_path(char **result, const char *value)
{
	char *copy = value == NULL || value[0] == '\0' ? NULL : strdup(value);

	if (value != NULL && value[0] != '\0' && copy == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	free(*result);
	*result = copy;
	return 0;
}

static void
copy_fixed(char *result, size_t capacity, const ini_section_t *section,
    const char *key, const char *fallback)
{
	const char *value = ini_section_get_string(section, key, fallback);

	if (strlen(value) >= capacity)
		value = fallback;
	(void)strcpy(result, value);
}

static int
bounded_int(const ini_section_t *section, const char *key, int fallback,
    int minimum, int maximum)
{
	return (int)ini_section_get_bounded_int64(section, key, fallback,
	    minimum, maximum);
}

static void
decode_fingerprint(struct st_profile *profile, const ini_section_t *section)
{
	const char *value = ini_section_find(section, "SSHFingerprint");
	size_t bytes;

	if (value == NULL)
		return;
	bytes = strlen(value) / 2;
	if ((bytes != 20 && bytes != 32) || strlen(value) != bytes * 2)
		return;
	if (!st_hex_decode(profile->ssh_fingerprint,
	    sizeof(profile->ssh_fingerprint), value, bytes * 2))
		return;
	profile->ssh_fingerprint_size = (uint8_t)bytes;
}

static int
legacy_encoding(const struct st_profile *profile)
{
	if (profile->connection_type == ST_CONNECTION_SHELL) {
		/* UTF-8 is the legacy Shell default only for emulations that
		 * accept it. Fixed-byte parsers still use the saved font's table. */
		switch (profile->screen_mode) {
		case ST_SCREEN_MODE_C64:
		case ST_SCREEN_MODE_C128_40:
		case ST_SCREEN_MODE_C128_80:
		case ST_SCREEN_MODE_ATARI:
		case ST_SCREEN_MODE_ATARI_XEP80:
		case ST_SCREEN_MODE_PRESTEL:
		case ST_SCREEN_MODE_BEEB:
			break;
		default:
			return ST_PROFILE_ENCODING_UTF8;
		}
	}
	for (size_t i = 0; i < st_builtin_font_count(); ++i) {
		if (strcmp(profile->font, st_builtin_font_name(i)) == 0)
			return st_profile_encoding_canonical(
			    st_builtin_font_codepage(i));
	}
	return ST_CODEPAGE_CP437;
}

int
st_bbs_decode_profile(struct st_profile *profile,
    const ini_section_t *section, const char *name, bool system,
    const char *download_path)
{
	int64_t *palette = NULL;
	size_t palette_count = 0;
	size_t i;
	int connection;
	int status;

	if (profile == NULL || section == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	st_profile_init(profile);
	profile->source = system ? ST_PROFILE_SOURCE_SYSTEM
	    : ST_PROFILE_SOURCE_USER;
	if (name != NULL)
		(void)strcpy(profile->name, name);
	copy_fixed(profile->address, sizeof(profile->address), section, "Address",
	    "");
	connection = (int)ini_section_get_enum_names(section, "ConnectionType",
	    ENUM_NAMES(connection_types), ST_CONNECTION_SSH);
	if (connection < ST_CONNECTION_UNKNOWN || connection > ST_CONNECTION_MQTT)
		connection = ST_CONNECTION_SSH;
	if (ini_section_get_bool(section, "BeDumb", false))
		connection = ST_CONNECTION_RAW;
	profile->connection_type = connection;
	profile->port = (uint16_t)bounded_int(section, "Port",
	    st_connection_default_port((enum st_connection_type)connection), 0,
	    UINT16_MAX);
	profile->flow_control = (int)ini_section_get_enum_names(section,
	    "FlowControl", ENUM_NAMES(flow_controls), ST_PROFILE_FLOW_RTS_CTS);
	profile->screen_mode = (int)ini_section_get_enum_names(section,
	    "ScreenMode", ENUM_NAMES(screen_modes), ST_SCREEN_MODE_CURRENT);
	profile->hide_status = ini_section_get_bool(section, "NoStatus", false);
	profile->hide_popups = ini_section_get_bool(section, "HidePopups", false);
	profile->rip_version = (int)ini_section_get_enum_names(section, "RIP",
	    ENUM_NAMES(rip_versions), ST_RIP_VERSION_NONE);
	profile->force_lcf = ini_section_get_bool(section, "ForceLCF", false);
	profile->yellow_is_yellow = ini_section_get_bool(section,
	    "YellowIsYellow", false);
	profile->expand_lf = ini_section_get_bool(section, "LFExpand", false);
	profile->sftp_public_key = ini_section_get_bool(section, "SFTPPublicKey",
	    false);
	profile->ssh_allow_aes128_cbc = ini_section_get_bool(section,
	    "SSHAllowAES128CBC", false);
	profile->ssh_accept_early_data = ini_section_get_bool(section,
	    "SSHAcceptEarlyData", false);
	profile->telnet_no_binary = ini_section_get_bool(section,
	    "TelnetBrokenTextmode", false);
	profile->defer_telnet_negotiation = ini_section_get_bool(section,
	    "TelnetDeferNegotiate", false);
	copy_fixed(profile->terminal_name, sizeof(profile->terminal_name), section,
	    "TerminalType", "");
	copy_fixed(profile->font, sizeof(profile->font), section, "Font",
	    "Codepage 437 English");
	{
		const char *name = ini_section_find(section, "Encoding");

		if (name == NULL || !st_profile_encoding_parse(name,
		    &profile->encoding))
			profile->encoding = legacy_encoding(profile);
	}
	copy_fixed(profile->comment, sizeof(profile->comment), section, "Comment",
	    "");
	profile->data_bits = (uint16_t)bounded_int(section, "DataBits", 8, 7, 8);
	profile->stop_bits = (uint16_t)bounded_int(section, "StopBits", 1, 1, 2);
	profile->parity = (int)ini_section_get_enum_names(section, "Parity",
	    ENUM_NAMES(parities), ST_PROFILE_PARITY_NONE);
	profile->bps_rate = (uint32_t)bounded_int(section, "BPSRate", 0, 0,
	    INT_MAX);
	profile->music = bounded_int(section, "ANSIMusic", ST_PROFILE_MUSIC_BANSI,
	    ST_PROFILE_MUSIC_SYNCTERM, ST_PROFILE_MUSIC_ALL);
	profile->address_family = (int)ini_section_get_enum_names(section,
	    "AddressFamily", ENUM_NAMES(address_families),
	    ST_ADDRESS_FAMILY_UNSPECIFIED);
	profile->sort_order = (int32_t)ini_section_get_int64(section, "SortOrder",
	    INT32_MAX);
	decode_fingerprint(profile, section);
	status = ini_section_get_int64_list(section, "Palette", ",", &palette,
	    &palette_count);
	if (status < 0)
		return status;
	if (palette_count > 16)
		palette_count = 16;
	for (i = 0; i < palette_count; ++i) {
		if (palette[i] < 0 || (uint64_t)palette[i] > UINT32_MAX) {
			palette_count = 0;
			break;
		}
		profile->palette[i] = (uint32_t)palette[i];
	}
	profile->palette_size = (unsigned)palette_count;
	ini_free_int64_list(palette);

	if (!system) {
		profile->added = ini_section_get_datetime(section, "Added", 0);
		profile->connected = ini_section_get_datetime(section, "LastConnected",
		    0);
		profile->calls = (uint32_t)ini_section_get_uint64(section, "TotalCalls",
		    0);
		copy_fixed(profile->username, sizeof(profile->username), section,
		    "UserName", "");
		copy_fixed(profile->password, sizeof(profile->password), section,
		    "Password", "");
		copy_fixed(profile->system_password, sizeof(profile->system_password),
		    section, "SystemPassword", "");
		copy_fixed(profile->wren_scripts, sizeof(profile->wren_scripts), section,
		    "WrenScripts", "");
		copy_fixed(profile->tls_psk_identity,
		    sizeof(profile->tls_psk_identity), section, "TLSPSKIdentity", "");
		copy_fixed(profile->tls_psk, sizeof(profile->tls_psk), section,
		    "TLSPSK", "");
		profile->tls_psk_version = (int)ini_section_get_enum_names(section,
		    "TLSPSKVersion", ENUM_NAMES(tls_versions), ST_TLS_VERSION_1_3);
		profile->tls_version_floor = (int)ini_section_get_enum_names(section,
		    "TLSVersionFloor", ENUM_NAMES(tls_versions),
		    ST_TLS_VERSION_UNKNOWN);
		profile->tls_trust_web_pki = ini_section_get_bool(section,
		    "TLSTrustWebPKI", true);
		profile->append_log = ini_section_get_bool(section, "AppendLogFile",
		    true);
		profile->transfer_log_level = (int)ini_section_get_enum_names(section,
		    "TransferLogLevel", ENUM_NAMES(log_levels), ST_PROFILE_LOG_INFO);
		profile->protocol_log_level = (int)ini_section_get_enum_names(section,
		    "ProtocolLogLevel", ENUM_NAMES(log_levels),
		    ini_section_get_enum_names(section, "TelnetLogLevel",
		    ENUM_NAMES(log_levels), ST_PROFILE_LOG_ERROR));
		status = replace_path(&profile->download_directory,
		    ini_section_get_string(section, "DownloadPath", download_path));
		if (status == 0)
			status = replace_path(&profile->upload_directory,
			    ini_section_get_string(section, "UploadPath", download_path));
		if (status == 0)
			status = replace_path(&profile->log_path,
			    ini_section_get_string(section, "LogFile", ""));
		if (status == 0)
			status = replace_path(&profile->tls_trusted_certificate,
			    ini_section_get_string(section, "TLSTrustedCertificate", ""));
		if (status == 0)
			status = replace_path(&profile->tls_client_certificate,
			    ini_section_get_string(section, "TLSClientCertificate", ""));
		if (status == 0)
			status = replace_path(&profile->tls_client_key,
			    ini_section_get_string(section, "TLSClientKey", ""));
		if (status < 0)
			return status;
	}
	return 0;
}

#define ENCODE(call) do { status = (call); if (status < 0) return status; } while (0)

int
st_bbs_encode_profile(ini_section_t *section,
    const struct st_profile *profile, bool defaults)
{
	int64_t palette[16];
	char fingerprint[65];
	size_t i;
	int status;

	if (section == NULL || profile == NULL
	    || !st_profile_encoding_valid(profile->encoding))
		return IO_ERROR_INVALID_ARGUMENT;
	if (!defaults)
		ENCODE(ini_section_set_string(section, "Address", profile->address,
		    &ini_default_style));
	ENCODE(ini_section_set_uint64(section, "Port", profile->port,
	    &ini_default_style));
	ENCODE(ini_section_set_enum_names(section, "ConnectionType",
	    ENUM_NAMES(connection_types), (size_t)profile->connection_type,
	    &ini_default_style));
	ENCODE(ini_section_set_enum_names(section, "FlowControl",
	    ENUM_NAMES(flow_controls), (size_t)profile->flow_control,
	    &ini_default_style));
	ENCODE(ini_section_set_enum_names(section, "ScreenMode", ENUM_NAMES(screen_modes),
	    (size_t)profile->screen_mode, &ini_default_style));
	ENCODE(ini_section_set_bool(section, "NoStatus", profile->hide_status,
	    &ini_default_style));
	ENCODE(ini_section_set_bool(section, "HidePopups", profile->hide_popups,
	    &ini_default_style));
	ENCODE(ini_section_set_enum_names(section, "RIP", ENUM_NAMES(rip_versions),
	    (size_t)profile->rip_version, &ini_default_style));
	ENCODE(ini_section_set_bool(section, "ForceLCF", profile->force_lcf,
	    &ini_default_style));
	ENCODE(ini_section_set_bool(section, "YellowIsYellow",
	    profile->yellow_is_yellow, &ini_default_style));
	ENCODE(ini_section_set_string(section, "TerminalType",
	    profile->terminal_name, &ini_default_style));
	ENCODE(ini_section_set_bool(section, "LFExpand", profile->expand_lf,
	    &ini_default_style));
	ENCODE(ini_section_set_bool(section, "TelnetBrokenTextmode",
	    profile->telnet_no_binary, &ini_default_style));
	ENCODE(ini_section_set_bool(section, "TelnetDeferNegotiate",
	    profile->defer_telnet_negotiation, &ini_default_style));
	ENCODE(ini_section_set_uint64(section, "DataBits", profile->data_bits,
	    &ini_default_style));
	ENCODE(ini_section_set_uint64(section, "StopBits", profile->stop_bits,
	    &ini_default_style));
	ENCODE(ini_section_set_enum_names(section, "Parity", ENUM_NAMES(parities),
	    (size_t)profile->parity, &ini_default_style));
	ENCODE(ini_section_set_uint64(section, "BPSRate", profile->bps_rate,
	    &ini_default_style));
	ENCODE(ini_section_set_int64(section, "ANSIMusic", profile->music,
	    &ini_default_style));
	ENCODE(ini_section_set_enum_names(section, "AddressFamily",
	    ENUM_NAMES(address_families), (size_t)profile->address_family,
	    &ini_default_style));
	ENCODE(ini_section_set_string(section, "Font", profile->font,
	    &ini_default_style));
	ENCODE(ini_section_set_string(section, "Encoding",
	    st_profile_encoding_name(profile->encoding), &ini_default_style));
	for (i = 0; i < profile->palette_size && i < 16; ++i)
		palette[i] = profile->palette[i];
	if (i != 0)
		ENCODE(ini_section_set_int64_list(section, "Palette", palette, i, ",",
		    &ini_default_style));
	else
		(void)ini_section_remove_key(section, "Palette");

	if (!defaults) {
		ENCODE(ini_section_set_datetime(section, "Added", profile->added, true,
		    &ini_default_style));
		ENCODE(ini_section_set_datetime(section, "LastConnected",
		    profile->connected, true, &ini_default_style));
		ENCODE(ini_section_set_uint64(section, "TotalCalls", profile->calls,
		    &ini_default_style));
#define ENCODE_STRING(key, member) ENCODE(ini_section_set_string(section, key, \
		    profile->member == NULL ? "" : profile->member, &ini_default_style))
		ENCODE(ini_section_set_string(section, "UserName", profile->username,
		    &ini_default_style));
		ENCODE(ini_section_set_string(section, "Password", profile->password,
		    &ini_default_style));
		ENCODE(ini_section_set_string(section, "SystemPassword",
		    profile->system_password, &ini_default_style));
		ENCODE_STRING("DownloadPath", download_directory);
		ENCODE_STRING("UploadPath", upload_directory);
		ENCODE_STRING("LogFile", log_path);
		ENCODE_STRING("TLSTrustedCertificate", tls_trusted_certificate);
		ENCODE_STRING("TLSClientCertificate", tls_client_certificate);
		ENCODE_STRING("TLSClientKey", tls_client_key);
#undef ENCODE_STRING
		ENCODE(ini_section_set_bool(section, "AppendLogFile",
		    profile->append_log, &ini_default_style));
		ENCODE(ini_section_set_enum_names(section, "TransferLogLevel",
		    ENUM_NAMES(log_levels), (size_t)profile->transfer_log_level,
		    &ini_default_style));
		ENCODE(ini_section_set_enum_names(section, "ProtocolLogLevel",
		    ENUM_NAMES(log_levels), (size_t)profile->protocol_log_level,
		    &ini_default_style));
		(void)ini_section_remove_key(section, "TelnetLogLevel");
		ENCODE(ini_section_set_string(section, "Comment", profile->comment,
		    &ini_default_style));
		ENCODE(ini_section_set_string(section, "WrenScripts",
		    profile->wren_scripts, &ini_default_style));
		ENCODE(ini_section_set_bool(section, "TLSTrustWebPKI",
		    profile->tls_trust_web_pki, &ini_default_style));
		ENCODE(ini_section_set_string(section, "TLSPSKIdentity",
		    profile->tls_psk_identity, &ini_default_style));
		ENCODE(ini_section_set_string(section, "TLSPSK", profile->tls_psk,
		    &ini_default_style));
		ENCODE(ini_section_set_enum_names(section, "TLSPSKVersion",
		    ENUM_NAMES(tls_versions), (size_t)profile->tls_psk_version,
		    &ini_default_style));
		ENCODE(ini_section_set_enum_names(section, "TLSVersionFloor",
		    ENUM_NAMES(tls_versions), (size_t)profile->tls_version_floor,
		    &ini_default_style));
		ENCODE(ini_section_set_bool(section, "SFTPPublicKey",
		    profile->sftp_public_key, &ini_default_style));
		ENCODE(ini_section_set_bool(section, "SSHAllowAES128CBC",
		    profile->ssh_allow_aes128_cbc, &ini_default_style));
		ENCODE(ini_section_set_bool(section, "SSHAcceptEarlyData",
		    profile->ssh_accept_early_data, &ini_default_style));
		if (profile->ssh_fingerprint_size == 20
		    || profile->ssh_fingerprint_size == 32) {
			if (!st_hex_encode(fingerprint, sizeof(fingerprint),
			    profile->ssh_fingerprint,
			    profile->ssh_fingerprint_size))
				return IO_ERROR_OUT_OF_RANGE;
			ENCODE(ini_section_set_string(section, "SSHFingerprint",
			    fingerprint, &ini_default_style));
		} else {
			(void)ini_section_remove_key(section, "SSHFingerprint");
		}
		if (profile->sort_order != INT32_MAX)
			ENCODE(ini_section_set_int64(section, "SortOrder",
			    profile->sort_order, &ini_default_style));
		else
			(void)ini_section_remove_key(section, "SortOrder");
	} else {
		ENCODE(ini_section_set_string(section, "UserName", profile->username,
		    &ini_default_style));
		ENCODE(ini_section_set_string(section, "Password", profile->password,
		    &ini_default_style));
		ENCODE(ini_section_set_string(section, "SystemPassword",
		    profile->system_password, &ini_default_style));
#define ENCODE_DEFAULT_PATH(key, member) \
		ENCODE(ini_section_set_string(section, key, \
		    profile->member == NULL ? "" : profile->member, &ini_default_style))
		ENCODE_DEFAULT_PATH("DownloadPath", download_directory);
		ENCODE_DEFAULT_PATH("UploadPath", upload_directory);
		ENCODE_DEFAULT_PATH("LogFile", log_path);
		ENCODE_DEFAULT_PATH("TLSTrustedCertificate", tls_trusted_certificate);
		ENCODE_DEFAULT_PATH("TLSClientCertificate", tls_client_certificate);
		ENCODE_DEFAULT_PATH("TLSClientKey", tls_client_key);
#undef ENCODE_DEFAULT_PATH
		ENCODE(ini_section_set_bool(section, "AppendLogFile",
		    profile->append_log, &ini_default_style));
		ENCODE(ini_section_set_enum_names(section, "TransferLogLevel",
		    ENUM_NAMES(log_levels), (size_t)profile->transfer_log_level,
		    &ini_default_style));
		ENCODE(ini_section_set_enum_names(section, "ProtocolLogLevel",
		    ENUM_NAMES(log_levels), (size_t)profile->protocol_log_level,
		    &ini_default_style));
		ENCODE(ini_section_set_bool(section, "TLSTrustWebPKI",
		    profile->tls_trust_web_pki, &ini_default_style));
		ENCODE(ini_section_set_string(section, "TLSPSKIdentity",
		    profile->tls_psk_identity, &ini_default_style));
		ENCODE(ini_section_set_string(section, "TLSPSK", profile->tls_psk,
		    &ini_default_style));
		ENCODE(ini_section_set_enum_names(section, "TLSPSKVersion",
		    ENUM_NAMES(tls_versions), (size_t)profile->tls_psk_version,
		    &ini_default_style));
		ENCODE(ini_section_set_enum_names(section, "TLSVersionFloor",
		    ENUM_NAMES(tls_versions), (size_t)profile->tls_version_floor,
		    &ini_default_style));
		ENCODE(ini_section_set_bool(section, "SFTPPublicKey",
		    profile->sftp_public_key, &ini_default_style));
		ENCODE(ini_section_set_bool(section, "SSHAllowAES128CBC",
		    profile->ssh_allow_aes128_cbc, &ini_default_style));
		ENCODE(ini_section_set_bool(section, "SSHAcceptEarlyData",
		    profile->ssh_accept_early_data, &ini_default_style));
	}
	return 0;
}

#undef ENCODE

bool
st_bbs_profiles_equal(const struct st_profile *left,
    const struct st_profile *right)
{
	ini_document_t *left_document = NULL;
	ini_document_t *right_document = NULL;
	ini_section_t *left_section;
	ini_section_t *right_section;
	char *left_text = NULL;
	char *right_text = NULL;
	size_t left_size = 0;
	size_t right_size = 0;
	bool equal = false;

	if (ini_document_create(&left_document) < 0
	    || ini_document_create(&right_document) < 0
	    || ini_document_add_section(left_document, "Profile",
	    &ini_default_style, &left_section) < 0
	    || ini_document_add_section(right_document, "Profile",
	    &ini_default_style, &right_section) < 0
	    || st_bbs_encode_profile(left_section, left, false) < 0
	    || st_bbs_encode_profile(right_section, right, false) < 0
	    || ini_document_serialize(left_document, &left_text, &left_size) < 0
	    || ini_document_serialize(right_document, &right_text,
	    &right_size) < 0)
		goto done;
	equal = left_size == right_size
	    && memcmp(left_text, right_text, left_size) == 0;
done:
	free(left_text);
	free(right_text);
	ini_document_free(&left_document);
	ini_document_free(&right_document);
	return equal;
}
