/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_profile.h"

#include "string_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { \
	fprintf(stderr, "failed: %s:%d: %s\n", __FILE__, __LINE__, #e); \
	return 1; \
} } while (0)

int
main(void)
{
	static const uint16_t ports[] = {
		[ST_CONNECTION_UNKNOWN] = 0,
		[ST_CONNECTION_RLOGIN] = 513,
		[ST_CONNECTION_RLOGIN_REVERSED] = 513,
		[ST_CONNECTION_TELNET] = 23,
		[ST_CONNECTION_RAW] = 0,
		[ST_CONNECTION_SSH] = 22,
		[ST_CONNECTION_SSH_NO_AUTH] = 22,
		[ST_CONNECTION_MODEM] = 0,
		[ST_CONNECTION_SERIAL] = 0,
		[ST_CONNECTION_SERIAL_3WIRE] = 0,
		[ST_CONNECTION_SHELL] = 0,
		[ST_CONNECTION_MBBS_GHOST] = UINT16_MAX,
		[ST_CONNECTION_TELNET_TLS] = 992,
		[ST_CONNECTION_MQTT] = 8883,
	};
	struct st_profile source;
	struct st_profile copy;

	for (size_t i = 0; i < sizeof(ports) / sizeof(ports[0]); ++i)
		CHECK(st_connection_default_port((enum st_connection_type)i)
		    == ports[i]);
	CHECK(st_connection_default_port((enum st_connection_type)-1) == 0);
	CHECK(st_connection_default_port((enum st_connection_type)
	    (ST_CONNECTION_MQTT + 1)) == 0);
	st_profile_init(&source);
	st_profile_init(&copy);
	CHECK(source.port == st_connection_default_port(source.connection_type));
	strcpy(source.name, "Example");
	source.download_directory = strdup("downloads");
	source.tls_client_key = strdup("key.pem");
	CHECK(source.download_directory != NULL);
	CHECK(source.tls_client_key != NULL);
	CHECK(st_profile_copy(&copy, &source));
	CHECK(strcmp(copy.name, "Example") == 0);
	CHECK(strcmp(copy.download_directory, "downloads") == 0);
	CHECK(copy.download_directory != source.download_directory);
	CHECK(copy.upload_directory == NULL);
	CHECK(strcmp(copy.tls_client_key, "key.pem") == 0);
	st_profile_dispose(&copy);
	st_profile_dispose(&source);
	return 0;
}
