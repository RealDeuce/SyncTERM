/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io_error.h"
#include "st_rip3_variables.h"

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

static int
names_and_values(void)
{
	struct st_rip3_variables     *variables  = NULL;
	struct st_rip3_variable_value value      = {0};
	const char                    boundary[] = "ABCDEFGHIJKLMNOPQRST";
	const uint8_t                 cp437[]    = {'c', 'a', 'f', 0x82};
	const uint8_t                 utf8[]     = {'c', 'a', 'f', 0xc3, 0xa9};
	const uint8_t                 binary[]   = {'A', 0, 'B'};
	uint8_t                       long_value[sizeof(value.bytes)];
	size_t                        count = 77;

	for (size_t i = 0; i < sizeof(long_value); i++)
		long_value[i] = (uint8_t)(i + 1);
	CHECK(st_rip3_variables_create(&variables) == 0);
	CHECK(st_rip3_variables_count(variables, &count) == 0 && count == 0);
	CHECK(st_rip3_variables_set(variables, "Full_Name", 9, (const uint8_t *)"John Doe", 8) == 0);
	CHECK(st_rip3_variables_get(variables, "full_name", 9, &value) == 0);
	CHECK(value.length == 8 && memcmp(value.bytes, "John Doe", 9) == 0);
	CHECK(st_rip3_variables_set(variables, "FULL_NAME", 9, cp437, sizeof(cp437)) == 0);
	CHECK(st_rip3_variables_count(variables, &count) == 0 && count == 1);
	CHECK(st_rip3_variables_get(variables, "fUlL_nAmE", 9, &value) == 0);
	CHECK(value.length == sizeof(cp437) && memcmp(value.bytes, cp437, sizeof(cp437)) == 0);
	CHECK(st_rip3_variables_set(variables, "Utf8", 4, utf8, sizeof(utf8)) == 0);
	CHECK(st_rip3_variables_get(variables, "UTF8", 4, &value) == 0);
	CHECK(value.length == sizeof(utf8) && memcmp(value.bytes, utf8, sizeof(utf8)) == 0);
	CHECK(st_rip3_variables_set(variables, boundary, 20, binary, sizeof(binary)) == 0);
	CHECK(st_rip3_variables_get(variables, "abcdefghijklmnopqrst", 20, &value) == 0);
	CHECK(value.length == 3 && memcmp(value.bytes, binary, 3) == 0 && value.bytes[3] == 0);
	CHECK(st_rip3_variables_set(variables, "Long", 4, long_value, sizeof(long_value) - 1) == 0);
	CHECK(
	    st_rip3_variables_set(variables, "Long", 4, long_value, sizeof(long_value)) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_variables_get(variables, "LONG", 4, &value) == 0);
	CHECK(value.length == sizeof(long_value) - 1 && memcmp(value.bytes, long_value, value.length) == 0
	      && value.bytes[value.length] == 0);
	CHECK(st_rip3_variables_set(variables, "Long", 4, (const uint8_t *)"Z", 1) == 0);
	CHECK(st_rip3_variables_get(variables, "LONG", 4, &value) == 0 && value.length == 1
	      && value.bytes[0] == 'Z');
	for (size_t i = 1; i < sizeof(value.bytes); i++)
		CHECK(value.bytes[i] == 0);
	CHECK(st_rip3_variables_set(variables, "Empty", 5, NULL, 0) == 0);
	CHECK(st_rip3_variables_get(variables, "EMPTY", 5, &value) == 0 && value.length == 0);
	CHECK(st_rip3_variables_remove(variables, "empty", 5) == 0);
	CHECK(st_rip3_variables_remove(variables, "EMPTY", 5) == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_variables_get(variables, "EMPTY", 5, &value) == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_variables_count(variables, &count) == 0 && count == 4);
	st_rip3_variables_destroy(variables);
	st_rip3_variables_destroy(NULL);
	return 0;
}

static int
invalid_names(void)
{
	struct st_rip3_variables     *variables = NULL;
	struct st_rip3_variable_value value     = {
	    3, {'o', 'l', 'd', 0}
        };
	const char *const invalid[]  = {"_NAME", "0NAME", "A-B", "A B", "A.B", "A/B", "A(B)", "A$B"};
	const char        embedded[] = {'A', 0, 'B'};
	const char        nonascii[] = {'A', (char)0xe9};
	size_t            count;

	CHECK(st_rip3_variables_create(&variables) == 0);
	CHECK(st_rip3_variables_set(variables, "", 0, NULL, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_variables_set(variables, "ABCDEFGHIJKLMNOPQRSTU", 21, NULL, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_variables_set(variables, "A", SIZE_MAX, NULL, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_variables_set(variables, NULL, 1, NULL, 0) == IO_ERROR_INVALID_ARGUMENT);
	for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++)
		CHECK(st_rip3_variables_set(variables, invalid[i], strlen(invalid[i]), NULL, 0)
		      == IO_ERROR_INVALID_FORMAT);
	CHECK(st_rip3_variables_set(variables, embedded, sizeof(embedded), NULL, 0) == IO_ERROR_INVALID_FORMAT);
	CHECK(st_rip3_variables_set(variables, nonascii, sizeof(nonascii), NULL, 0) == IO_ERROR_INVALID_FORMAT);
	CHECK(st_rip3_variables_set(variables, "GOOD", 4, NULL, 1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_variables_count(variables, &count) == 0 && count == 0);
	CHECK(st_rip3_variables_get(variables, "GOOD", 4, &value) == IO_ERROR_NOT_FOUND);
	CHECK(value.length == 3 && memcmp(value.bytes, "old", 4) == 0);
	CHECK(st_rip3_variables_get(variables, "A-B", 3, &value) == IO_ERROR_INVALID_FORMAT);
	CHECK(value.length == 3 && memcmp(value.bytes, "old", 4) == 0);
	CHECK(st_rip3_variables_remove(variables, "GOOD", 4) == IO_ERROR_NOT_FOUND);
	st_rip3_variables_destroy(variables);
	return 0;
}

static int
growth_and_snapshots(void)
{
	struct st_rip3_variables        *variables = NULL, *restored = NULL;
	struct st_rip3_variable_snapshot snapshot = {0};
	struct st_rip3_variable_value    value;
	size_t                           count;

	CHECK(st_rip3_variables_create(&variables) == 0);
	CHECK(st_rip3_variables_snapshot(variables, &snapshot) == 0);
	CHECK(snapshot.entries == NULL && snapshot.count == 0);
	st_rip3_variables_snapshot_free(&snapshot);
	for (unsigned i = 0; i < 4096; i++) {
		char    name[21];
		uint8_t bytes[] = {(uint8_t)(i >> 8), (uint8_t)i};

		(void)snprintf(name, sizeof(name), "N%08u", i);
		CHECK(st_rip3_variables_set(variables, name, strlen(name), bytes, sizeof(bytes)) == 0);
	}
	CHECK(st_rip3_variables_count(variables, &count) == 0 && count == 4096);
	for (unsigned i = 0; i < 4096; i++) {
		char name[21];

		(void)snprintf(name, sizeof(name), "n%08u", i);
		CHECK(st_rip3_variables_get(variables, name, strlen(name), &value) == 0);
		CHECK(value.length == 2 && value.bytes[0] == (uint8_t)(i >> 8) && value.bytes[1] == (uint8_t)i);
		if ((i & 1) == 0)
			CHECK(st_rip3_variables_remove(variables, name, strlen(name)) == 0);
	}
	CHECK(st_rip3_variables_snapshot(variables, &snapshot) == 0 && snapshot.count == 2048);
	for (size_t i = 0; i < snapshot.count; i++) {
		char     name[21];
		unsigned number = (unsigned)i * 2 + 1;

		(void)snprintf(name, sizeof(name), "N%08u", number);
		CHECK(strcmp(snapshot.entries[i].name, name) == 0);
		CHECK(snapshot.entries[i].value.length == 2
		      && snapshot.entries[i].value.bytes[0] == (uint8_t)(number >> 8)
		      && snapshot.entries[i].value.bytes[1] == (uint8_t)number);
	}
	CHECK(st_rip3_variables_set(variables, "N00000001", 9, (const uint8_t *)"new", 3) == 0);
	CHECK(snapshot.entries[0].value.length == 2 && snapshot.entries[0].value.bytes[1] == 1);
	CHECK(st_rip3_variables_from_snapshot(&restored, &snapshot) == 0);
	st_rip3_variables_snapshot_free(&snapshot);
	st_rip3_variables_snapshot_free(&snapshot);
	CHECK(st_rip3_variables_get(restored, "N00000001", 9, &value) == 0);
	CHECK(value.length == 2 && value.bytes[0] == 0 && value.bytes[1] == 1);
	CHECK(st_rip3_variables_count(restored, &count) == 0 && count == 2048);
	st_rip3_variables_destroy(variables);
	st_rip3_variables_destroy(restored);
	return 0;
}

static int
invalid_import(void)
{
	struct st_rip3_variables        *variables  = NULL;
	struct st_rip3_variable_entry    entries[2] = {0};
	struct st_rip3_variable_snapshot snapshot   = {entries, 2};

	memcpy(entries[0].name, "First", 6);
	entries[0].value.length   = 1;
	entries[0].value.bytes[0] = 'A';
	memcpy(entries[1].name, "first", 6);
	entries[1].value.length   = 1;
	entries[1].value.bytes[0] = 'B';
	CHECK(st_rip3_variables_from_snapshot(&variables, &snapshot) == IO_ERROR_INVALID_FORMAT);
	CHECK(variables == NULL);
	memset(entries[1].name, 'A', sizeof(entries[1].name));
	CHECK(st_rip3_variables_from_snapshot(&variables, &snapshot) == IO_ERROR_INVALID_FORMAT);
	CHECK(variables == NULL);
	memcpy(entries[1].name, "Second", 7);
	entries[1].value.length = sizeof(entries[1].value.bytes);
	CHECK(st_rip3_variables_from_snapshot(&variables, &snapshot) == IO_ERROR_INVALID_FORMAT);
	CHECK(variables == NULL);
	snapshot.count = SIZE_MAX;
	CHECK(st_rip3_variables_from_snapshot(&variables, &snapshot) == IO_ERROR_OUT_OF_RANGE);
	CHECK(variables == NULL);
	snapshot.count   = 1;
	snapshot.entries = NULL;
	CHECK(st_rip3_variables_from_snapshot(&variables, &snapshot) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(variables == NULL);
	return 0;
}

static int
independent_tables(void)
{
	struct st_rip3_variables        *session = NULL, *persistent = NULL, *reloaded = NULL;
	struct st_rip3_variable_snapshot snapshot = {0};
	struct st_rip3_variable_value    value;

	CHECK(st_rip3_variables_create(&session) == 0);
	CHECK(st_rip3_variables_create(&persistent) == 0);
	CHECK(st_rip3_variables_set(session, "NAME", 4, (const uint8_t *)"session", 7) == 0);
	CHECK(st_rip3_variables_set(persistent, "NAME", 4, (const uint8_t *)"persistent", 10) == 0);
	CHECK(st_rip3_variables_get(session, "NAME", 4, &value) == 0 && value.length == 7);
	CHECK(st_rip3_variables_get(persistent, "NAME", 4, &value) == 0 && value.length == 10);
	CHECK(st_rip3_variables_snapshot(persistent, &snapshot) == 0);
	st_rip3_variables_destroy(session);
	st_rip3_variables_destroy(persistent);
	CHECK(st_rip3_variables_from_snapshot(&reloaded, &snapshot) == 0);
	st_rip3_variables_snapshot_free(&snapshot);
	CHECK(st_rip3_variables_get(reloaded, "name", 4, &value) == 0);
	CHECK(value.length == 10 && memcmp(value.bytes, "persistent", 11) == 0);
	st_rip3_variables_destroy(reloaded);
	return 0;
}

int
main(void)
{
	CHECK(names_and_values() == 0);
	CHECK(invalid_names() == 0);
	CHECK(growth_and_snapshots() == 0);
	CHECK(invalid_import() == 0);
	CHECK(independent_tables() == 0);
	return 0;
}
