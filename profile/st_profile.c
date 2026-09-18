/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_profile.h"

#include "string_compat.h"

#include <stdlib.h>
#include <string.h>

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

void
st_profile_init(struct st_profile *profile)
{
	if (profile != NULL) {
		memset(profile, 0, sizeof(*profile));
		profile->connection_type = ST_CONNECTION_SSH;
		profile->port = 22;
		profile->screen_mode = ST_SCREEN_MODE_CURRENT;
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
		(void)strcpy(profile->font, "Codepage 437 English");
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
