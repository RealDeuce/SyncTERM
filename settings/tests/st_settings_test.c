/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_settings.h"

#include "io_error.h"
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
	struct st_settings source;
	struct st_settings copy;

	st_settings_init(&source);
	st_settings_init(&copy);
	source.modem_device = strdup("/dev/ttyS0");
	strcpy(source.modem_init_string, "ATZ");
	CHECK(source.modem_device != NULL);
	CHECK(name_value_list_append(&source.web_downloads, "one", "two"));
	CHECK(st_settings_copy(&copy, &source));
	CHECK(strcmp(copy.modem_device, "/dev/ttyS0") == 0);
	CHECK(copy.modem_device != source.modem_device);
	CHECK(name_value_list_count(copy.web_downloads) == 1);
	CHECK(strcmp(copy.web_downloads[0]->name, "one") == 0);
	CHECK(strcmp(copy.web_downloads[0]->value, "two") == 0);
	st_settings_dispose(&copy);
	st_settings_dispose(&source);

	st_settings_init(&st_settings);
	CHECK(st_settings_web_download_append("One",
	    "https://example.invalid/one.lst") == 0);
	CHECK(st_settings_web_download_append("M\xc3\xbcnchen",
	    "https://example.invalid/unicode.lst") == 0);
	CHECK(st_settings_web_download_append("M\xc3\x9cNCHEN",
	    "https://example.invalid/duplicate.lst")
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_settings_web_download_append("[Invalid]",
	    "https://example.invalid/rejected.lst") == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_settings_web_download_count() == 2);
	CHECK(st_settings_web_download_set(0, ";Invalid",
	    "https://example.invalid/rejected.lst") == IO_ERROR_INVALID_ARGUMENT);
	CHECK(strcmp(st_settings_web_download_at(0)->name, "One") == 0);
	CHECK(st_settings_sort_profile_add(0, "Name", "29,1") == 0);
	CHECK(st_settings_sort_profile_add(1, "Stra\xc3\x9f" "e", "1") == 0);
	CHECK(st_settings_sort_profile_add(2, "STRASSE", "1") == -1);
	CHECK(st_settings_sort_profile_add(0, "[Invalid]", "29,1") == -1);
	CHECK(st_settings_sort_profile_count() == 2);
	CHECK(st_settings_sort_profile_update(0, ";Invalid", "29,1") == -1);
	CHECK(strcmp(st_settings_sort_profile_at(0)->name, "Name") == 0);
	st_settings_dispose(&st_settings);
	return 0;
}
