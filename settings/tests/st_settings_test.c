/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_settings.h"

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
	return 0;
}
