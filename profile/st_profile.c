/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_profile.h"

#include "string_compat.h"

#include <stdlib.h>
#include <string.h>

int
st_profile_encoding_canonical(int encoding)
{
	switch (encoding) {
	case ST_CODEPAGE_CP1251_B: return ST_CODEPAGE_CP1251;
	case ST_CODEPAGE_CP850_B: return ST_CODEPAGE_CP850;
	case ST_CODEPAGE_CP865_B: return ST_CODEPAGE_CP865;
	case ST_CODEPAGE_PRESTEL_SEPARATED: return ST_CODEPAGE_PRESTEL;
	default: return encoding;
	}
}

bool
st_profile_encoding_valid(int encoding)
{
	return encoding == ST_PROFILE_ENCODING_UTF8
	    || (encoding >= ST_CODEPAGE_CP437
	    && encoding < ST_CODEPAGE_COUNT
	    && st_profile_encoding_canonical(encoding) == encoding);
}

const char *
st_profile_encoding_name(int encoding)
{
	if (encoding == ST_PROFILE_ENCODING_UTF8)
		return "UTF-8";
	return st_profile_encoding_valid(encoding)
	    ? st_codepage_name((enum st_codepage)encoding) : NULL;
}

bool
st_profile_encoding_parse(const char *name, int *encoding)
{
	if (name == NULL || encoding == NULL)
		return false;
	if (strcmp(name, "UTF-8") == 0) {
		*encoding = ST_PROFILE_ENCODING_UTF8;
		return true;
	}
	for (int i = 0; i < ST_CODEPAGE_COUNT; ++i) {
		const char *candidate = st_codepage_name((enum st_codepage)i);

		if (candidate != NULL && strcmp(name, candidate) == 0) {
			*encoding = st_profile_encoding_canonical(i);
			return true;
		}
	}
	return false;
}

static bool
copy_path(char **result, const char *source)
{
	if (source == NULL) {
		*result = NULL;
		return true;
	}
	*result = strdup(source);
	return *result != NULL;
}

uint16_t
st_connection_default_port(enum st_connection_type type)
{
	switch (type) {
	case ST_CONNECTION_RLOGIN:
	case ST_CONNECTION_RLOGIN_REVERSED:
		return 513;
	case ST_CONNECTION_TELNET:
		return 23;
	case ST_CONNECTION_SSH:
	case ST_CONNECTION_SSH_NO_AUTH:
		return 22;
	case ST_CONNECTION_MBBS_GHOST:
		return UINT16_MAX;
	case ST_CONNECTION_TELNET_TLS:
		return 992;
	case ST_CONNECTION_MQTT:
		return 8883;
	case ST_CONNECTION_UNKNOWN:
	case ST_CONNECTION_RAW:
	case ST_CONNECTION_MODEM:
	case ST_CONNECTION_SERIAL:
	case ST_CONNECTION_SERIAL_3WIRE:
	case ST_CONNECTION_SHELL:
	default:
		return 0;
	}
}

void
st_profile_init(struct st_profile *profile)
{
	if (profile != NULL) {
		memset(profile, 0, sizeof(*profile));
		profile->connection_type = ST_CONNECTION_SSH;
		profile->port = st_connection_default_port(ST_CONNECTION_SSH);
		profile->screen_mode = ST_SCREEN_MODE_CURRENT;
		profile->encoding = ST_CODEPAGE_CP437;
		profile->flow_control = ST_PROFILE_FLOW_RTS_CTS;
		profile->data_bits = 8;
		profile->stop_bits = 1;
		profile->parity = ST_PROFILE_PARITY_NONE;
		profile->transfer_log_level = ST_PROFILE_LOG_INFO;
		profile->protocol_log_level = ST_PROFILE_LOG_ERROR;
		profile->music = ST_PROFILE_MUSIC_BANSI;
		profile->tls_trust_web_pki = true;
		profile->tls_psk_version = ST_TLS_VERSION_1_3;
		profile->append_log = true;
		profile->sort_order = INT32_MAX;
		(void)strlcpy(profile->font, "Codepage 437 English", sizeof(profile->font));
	}
}

void
st_profile_dispose(struct st_profile *profile)
{
	if (profile == NULL)
		return;
	free(profile->download_directory);
	free(profile->upload_directory);
	free(profile->log_path);
	free(profile->tls_trusted_certificate);
	free(profile->tls_client_certificate);
	free(profile->tls_client_key);
	memset(profile, 0, sizeof(*profile));
}

bool
st_profile_copy(struct st_profile *result, const struct st_profile *source)
{
	struct st_profile copy;

	if (result == NULL || source == NULL)
		return false;
	if (result == source)
		return true;
	copy = *source;
	copy.download_directory = NULL;
	copy.upload_directory = NULL;
	copy.log_path = NULL;
	copy.tls_trusted_certificate = NULL;
	copy.tls_client_certificate = NULL;
	copy.tls_client_key = NULL;
	if (!copy_path(&copy.download_directory, source->download_directory)
	    || !copy_path(&copy.upload_directory, source->upload_directory)
	    || !copy_path(&copy.log_path, source->log_path)
	    || !copy_path(&copy.tls_trusted_certificate,
	    source->tls_trusted_certificate)
	    || !copy_path(&copy.tls_client_certificate,
	    source->tls_client_certificate)
	    || !copy_path(&copy.tls_client_key, source->tls_client_key)) {
		st_profile_dispose(&copy);
		return false;
	}
	st_profile_dispose(result);
	*result = copy;
	return true;
}
