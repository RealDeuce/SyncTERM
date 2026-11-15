/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_table_host.h"
#include "st_rip3_palettes.h"
#include "io_error.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static int
execute(const struct st_rip3_table_host *host, const char *command,
    char *result, size_t capacity)
{
	struct st_rip3_builtin_reference reference;
	int status = st_rip3_builtin_reference_parse(&reference, (const uint8_t *)command, strlen(command));

	return status == 0 ? st_rip3_table_host_execute(host, &reference, result, capacity) : status;
}

int
main(void)
{
	struct st_rip3_table_host host = {0};
	struct st_rip3_palettes *palettes = NULL;
	struct st_rip3_palette_info info;
	char output[3100], before[3100], expected[3100];
	uint32_t colors[256];
	unsigned current;
	size_t length = 2;
	const char *const invalid[] = {
		"$PALENTRY$", "$PALENTRY(CUR)$", "$PALENTRY(CUR,1,2,3)$",
		"$PALENTRY(36,0)$", "$PALENTRY(-1,0)$", "$PALENTRY(CUR,256)$",
		"$PALENTRY(CUR,2,1)$", "$PALENTRY(CUR,1,256)$", "$PALENTRY(CUR,ALL,255)$",
		"$PALENTRY(CUR,)$", "$PALENTRY(CUR,1,)$", "$PALENTRY(CUR,1x)$",
		"$PALENTRY(CUR, 5, 7);",
	};

	CHECK(st_rip3_palettes_create(&palettes) == 0);
	host.tables[ST_RIP3_TABLE_PAL].live = palettes;
	/* Literal eight-bit example and standard desktop colors, independently
	 * specified rather than read back to manufacture expected replies. */
	CHECK(execute(&host, "$PALENTRY(CUR,15)$", output, sizeof(output)) == 0);
	CHECK(strcmp(output, "8;255:255:255") == 0);
	CHECK(execute(&host, "$PALENTRY(0,5,7)$", output, sizeof(output)) == 0);
	CHECK(strcmp(output, "8;170:0:170,170:85:0,170:170:170") == 0);
	CHECK(execute(&host, "$PALENTRY(12,0)$", output, sizeof(output)) == 0);
	CHECK(strcmp(output, "-1") == 0);
	CHECK(st_rip3_palettes_info(palettes, 12, &info) == 0 && !info.defined);
	CHECK(st_rip3_palettes_current(palettes, &current) == 0 && current == 0);
	CHECK(st_rip3_palettes_switch(palettes, 35) == 0);
	for (size_t i = 0; i < 256; i++) colors[i] = 0xffffff;
	CHECK(st_rip3_palettes_rgb(palettes, 0, 8, colors, 256) == 0);
	CHECK(st_rip3_palettes_protect(palettes, 35, true) == 0);
	memcpy(expected, "8;", 2);
	for (unsigned i = 0; i < 256; i++) {
		if (i != 0) expected[length++] = ',';
		memcpy(expected + length, "255:255:255", 11); length += 11;
	}
	expected[length] = 0;
	CHECK(length == 3073);
	memset(output, 'Q', sizeof(output)); memcpy(before, output, sizeof(output));
	CHECK(execute(&host, "$PALENTRY(CUR,ALL)$", output, length) == IO_ERROR_OUT_OF_RANGE);
	CHECK(memcmp(output, before, sizeof(output)) == 0);
	CHECK(execute(&host, "$PALENTRY(CUR,ALL)$", output, length + 1) == 0);
	CHECK(strcmp(output, expected) == 0 && output[length + 1] == 'Q');
	CHECK(execute(&host, "$pAlEnTrY(035,000,255)$", output, sizeof(output)) == 0);
	CHECK(strcmp(output, expected) == 0);
	CHECK(execute(&host, "$PALENTRY(CUR,255)$", output, sizeof(output)) == 0);
	CHECK(strcmp(output, "8;255:255:255") == 0);
	CHECK(st_rip3_palettes_current(palettes, &current) == 0 && current == 35);
	CHECK(st_rip3_palettes_info(palettes, 35, &info) == 0 && info.protected);
	memset(output, 'Q', sizeof(output)); memcpy(before, output, sizeof(output));
	for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
		CHECK(execute(&host, invalid[i], output, sizeof(output)) == IO_ERROR_INVALID_FORMAT);
		CHECK(memcmp(output, before, sizeof(output)) == 0);
	}
	CHECK(execute(&host, "$PALENTRY(12,0)$", output, 2) == IO_ERROR_OUT_OF_RANGE);
	CHECK(execute(&host, "$PALENTRY(0,0)$", output, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(memcmp(output, before, sizeof(output)) == 0);
	host.tables[ST_RIP3_TABLE_PAL].live = NULL;
	CHECK(execute(&host, "$PALENTRY(CUR,0)$", output, sizeof(output)) == IO_ERROR_NOT_SUPPORTED);
	CHECK(execute(&host, "$PALENTRY(0,0)$", output, sizeof(output)) == IO_ERROR_NOT_SUPPORTED);
	CHECK(memcmp(output, before, sizeof(output)) == 0);
	st_rip3_palettes_destroy(palettes);
	return 0;
}
