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
	int encoding = -1;

	CHECK(st_profile_encoding_valid(ST_CODEPAGE_CP437));
	CHECK(st_profile_encoding_valid(ST_PROFILE_ENCODING_UTF8));
	CHECK(!st_profile_encoding_valid(ST_CODEPAGE_CP850_B));
	CHECK(!st_profile_encoding_valid(ST_CODEPAGE_PRESTEL_SEPARATED));
	CHECK(st_profile_encoding_parse("CP850B", &encoding)
	    && encoding == ST_CODEPAGE_CP850);
	CHECK(st_profile_encoding_parse("PRESTEL-SEP", &encoding)
	    && encoding == ST_CODEPAGE_PRESTEL);
	CHECK(st_profile_encoding_parse("UTF-8", &encoding)
	    && encoding == ST_PROFILE_ENCODING_UTF8);
	CHECK(!st_profile_encoding_parse("invalid", &encoding));
	CHECK(strcmp(st_profile_encoding_name(ST_CODEPAGE_CP1251), "CP1251")
	    == 0);
	CHECK(strcmp(st_profile_encoding_name(ST_PROFILE_ENCODING_UTF8),
	    "UTF-8") == 0);

	for (size_t i = 0; i < sizeof(ports) / sizeof(ports[0]); ++i)
		CHECK(st_connection_default_port((enum st_connection_type)i)
		    == ports[i]);
	CHECK(st_connection_default_port((enum st_connection_type)-1) == 0);
	CHECK(st_connection_default_port((enum st_connection_type)
	    (ST_CONNECTION_MQTT + 1)) == 0);
	st_profile_init(&source);
	st_profile_init(&copy);
	CHECK(source.identifier == -1);
	CHECK(source.cterm_log_level == ST_PROFILE_LOG_ERROR);
	{
		char scripts[ST_PROFILE_WREN_SCRIPTS_MAX + 1] = "unchanged";
		CHECK(st_profile_scripts_parse(scripts, " first,\tsecond ,first"));
		CHECK(strcmp(scripts, "first,second") == 0);
		CHECK(!st_profile_scripts_parse(scripts, "valid,syncterm_menu"));
		CHECK(strcmp(scripts, "first,second") == 0);
		CHECK(!st_profile_scripts_parse(scripts, "valid,bad-name"));
		CHECK(!st_profile_scripts_append(scripts, "x\0y", 3));
		CHECK(st_profile_scripts_parse(scripts, ""));
		CHECK(scripts[0] == '\0');
	}
	CHECK(source.encoding == ST_CODEPAGE_CP437);
	source.encoding = ST_PROFILE_ENCODING_UTF8;
	CHECK(source.port == st_connection_default_port(source.connection_type));
	strcpy(source.name, "Example");
	source.download_directory = strdup("downloads");
	source.tls_client_key = strdup("key.pem");
	CHECK(source.download_directory != NULL);
	CHECK(source.tls_client_key != NULL);
	CHECK(st_profile_copy(&copy, &source));
	CHECK(strcmp(copy.name, "Example") == 0);
	CHECK(copy.encoding == ST_PROFILE_ENCODING_UTF8);
	CHECK(strcmp(copy.download_directory, "downloads") == 0);
	CHECK(copy.download_directory != source.download_directory);
	CHECK(copy.upload_directory == NULL);
	CHECK(strcmp(copy.tls_client_key, "key.pem") == 0);
	st_profile_dispose(&copy);
	st_profile_dispose(&source);
	return 0;
}
