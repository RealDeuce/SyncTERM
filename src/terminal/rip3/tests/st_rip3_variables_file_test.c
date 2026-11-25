/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_variables_file.h"
#include "filesystem.h"
#include "path.h"
#include "io_error.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

/* Independent literal client-format fixtures, sorted EMPTY then NAME. */
static const uint8_t version_one[] = {
	'S','T','R','I','P','3','V',1, 2,0,0,0,
	5,0,'E','M','P','T','Y', 4,3,'N','A','M','E','A',0,'B',
};
static const uint8_t fixture[] = {
	'S','T','R','I','P','3','V',2, 2,0,0,0,
	5,0,0,'E','M','P','T','Y', 4,3,1,'N','A','M','E','A',0,'B',
};
static const char filename[] = "variables \xc3\xa9.bin";

static int
expect_file(const char *path, const uint8_t *expected, size_t size)
{
	FILE *stream = NULL;
	uint8_t bytes[sizeof(fixture) + 1];
	size_t length;

	CHECK(size <= sizeof(fixture));
	CHECK(fs_open(&stream, path, "rb") == 0);
	length = fread(bytes, 1, sizeof(bytes), stream);
	CHECK(!ferror(stream) && length == size && memcmp(bytes, expected, size) == 0);
	CHECK(fclose(stream) == 0);
	return 0;
}

static int
write_store(const char *directory)
{
	struct st_rip3_variables *variables = NULL;
	char *path = NULL;
	const uint8_t value[] = {'A',0,'B'};

	CHECK(fs_create_directories(directory) == 0);
	CHECK(path_join_alloc(&path, directory, filename) == 0);
	CHECK(st_rip3_variables_create(&variables) == 0);
	/* Insertion order deliberately differs from deterministic file order. */
	CHECK(st_rip3_variables_define(variables, NULL, "name", 4, value, sizeof(value), true) == 0);
	CHECK(st_rip3_variables_set(variables, "empty", 5, NULL, 0) == 0);
	CHECK(st_rip3_variables_save_file(variables, path) == 0);
	st_rip3_variables_destroy(variables);
	CHECK(expect_file(path, fixture, sizeof(fixture)) == 0);
	free(path);
	return 0;
}

static int
read_store(const char *directory)
{
	struct st_rip3_variables *variables = NULL;
	struct st_rip3_variable_value value;
	struct st_rip3_variable_entry entry;
	char *path = NULL;
	const uint8_t utf8[] = {'c','a','f',0xc3,0xa9};
	const char boundary[] = "ABCDEFGHIJKLMNOPQRST";
	uint8_t maximum[255 * 4];
	size_t count;

	CHECK(path_join_alloc(&path, directory, filename) == 0);
	/* This CTest runs in a new process after the writer has exited. */
	CHECK(expect_file(path, fixture, sizeof(fixture)) == 0);
	CHECK(st_rip3_variables_load_file(&variables, path) == 0);
	CHECK(st_rip3_variables_count(variables, &count) == 0 && count == 2);
	CHECK(st_rip3_variables_get(variables, "NAME", 4, &value) == 0);
	CHECK(value.length == 3 && value.bytes[0] == 'A' && value.bytes[1] == 0 && value.bytes[2] == 'B');
	CHECK(st_rip3_variables_get_entry(variables, "NAME", 4, &entry) == 0 && entry.require_confirmation);
	CHECK(st_rip3_variables_get(variables, "EMPTY", 5, &value) == 0 && value.length == 0);
	CHECK(st_rip3_variables_remove(variables, "EMPTY", 5) == 0);
	CHECK(st_rip3_variables_set(variables, "NAME", 4, utf8, sizeof(utf8)) == 0);
	for (size_t i = 0; i < sizeof(maximum); i++)
		maximum[i] = (uint8_t)(i + 1);
	CHECK(st_rip3_variables_set(variables, boundary, 20, maximum, sizeof(maximum)) == 0);
	CHECK(st_rip3_variables_save_file(variables, path) == 0); /* Replace existing file. */
	st_rip3_variables_destroy(variables);
	variables = NULL;
	CHECK(st_rip3_variables_load_file(&variables, path) == 0);
	CHECK(st_rip3_variables_count(variables, &count) == 0 && count == 2);
	CHECK(st_rip3_variables_get(variables, "EMPTY", 5, &value) == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_variables_get(variables, "NAME", 4, &value) == 0);
	CHECK(value.length == sizeof(utf8) && memcmp(value.bytes, utf8, sizeof(utf8)) == 0);
	CHECK(st_rip3_variables_get_entry(variables, "NAME", 4, &entry) == 0 && entry.require_confirmation);
	CHECK(st_rip3_variables_get(variables, boundary, 20, &value) == 0);
	CHECK(value.length == sizeof(maximum) && memcmp(value.bytes, maximum, sizeof(maximum)) == 0
	    && value.bytes[value.length] == 0);
	st_rip3_variables_destroy(variables);
	free(path);
	return 0;
}

static int
malformed_and_io(const char *directory)
{
	struct st_rip3_variables *variables = NULL, *original, *empty = NULL;
	struct st_rip3_variable_value value;
	char *path = NULL, *blocked = NULL, *missing = NULL;
	uint8_t bad[sizeof(fixture) + 1];
	struct fs_info info;
	size_t count;
	const uint8_t duplicate[] = {
		'S','T','R','I','P','3','V',1, 2,0,0,0,
		3,1,'F','O','O','A', 3,1,'f','o','o','B',
	};
	const uint8_t too_wide[] = {
		'S','T','R','I','P','3','V',3, 1,0,0,0,
		1,0xfd,0,3,'A', /* Declares 1021 bytes, one more than storage can hold. */
	};

	CHECK(path_join_alloc(&path, directory, "corrupt.bin") == 0);
	CHECK(path_join_alloc(&blocked, directory, "blocked") == 0);
	CHECK(path_join_alloc(&missing, directory, "missing-parent/variables.bin") == 0);
	CHECK(st_rip3_variables_create(&variables) == 0);
	CHECK(st_rip3_variables_set(variables, "KEEP", 4, (const uint8_t *)"K", 1) == 0);
	original = variables;
	for (size_t length = 0; length < sizeof(fixture); length++) {
		CHECK(fs_replace_file(path, fixture, length) == 0);
		CHECK(st_rip3_variables_load_file(&variables, path) == IO_ERROR_INVALID_FORMAT);
		CHECK(variables == original);
	}
	for (unsigned variant = 0; variant < 8; variant++) {
		size_t length = sizeof(fixture);

		memcpy(bad, fixture, sizeof(fixture));
		switch (variant) {
		case 0: bad[0] = 'X'; break;
		case 1: memset(bad + 8, 255, 4); break;
		case 2: bad[12] = 0; break;
		case 3: bad[12] = 21; break;
		case 4: bad[15] = '_'; break;
		case 5: bad[21] = 255; break;
		case 6: bad[length++] = 0; break;
		case 7: bad[22] = 2; break;
		}
		CHECK(fs_replace_file(path, bad, length) == 0);
		CHECK(st_rip3_variables_load_file(&variables, path) == IO_ERROR_INVALID_FORMAT);
		CHECK(variables == original);
	}
	memcpy(bad, fixture, sizeof(fixture)); bad[7] = 4;
	CHECK(fs_replace_file(path, bad, sizeof(fixture)) == 0);
	CHECK(st_rip3_variables_load_file(&variables, path) == IO_ERROR_NOT_SUPPORTED);
	CHECK(variables == original);
	CHECK(fs_replace_file(path, too_wide, sizeof(too_wide)) == 0);
	CHECK(st_rip3_variables_load_file(&variables, path) == IO_ERROR_INVALID_FORMAT);
	CHECK(variables == original);
	CHECK(fs_replace_file(path, duplicate, sizeof(duplicate)) == 0);
	CHECK(st_rip3_variables_load_file(&variables, path) == IO_ERROR_INVALID_FORMAT);
	CHECK(variables == original);
	CHECK(st_rip3_variables_load_file(&variables, directory) == IO_ERROR_INVALID_FORMAT);
	CHECK(st_rip3_variables_load_file(&variables, missing) == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_variables_load_file(&variables, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_variables_load_file(NULL, path) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(variables == original);
	CHECK(st_rip3_variables_get(variables, "KEEP", 4, &value) == 0
	    && value.length == 1 && value.bytes[0] == 'K');
	CHECK(st_rip3_variables_count(variables, &count) == 0 && count == 1);
	CHECK(fs_replace_file(path, fixture, sizeof(fixture)) == 0);
	CHECK(st_rip3_variables_save_file(NULL, path) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(expect_file(path, fixture, sizeof(fixture)) == 0);
	CHECK(st_rip3_variables_save_file(variables, missing) != 0);
	CHECK(fs_create_directories(blocked) == 0);
	CHECK(st_rip3_variables_save_file(variables, blocked) != 0);
	CHECK(fs_get_info(&info, blocked) == 0 && info.kind == FS_ENTRY_DIRECTORY);
	CHECK(expect_file(path, fixture, sizeof(fixture)) == 0);
	/* A valid empty store is distinct from a missing/truncated file. */
	memcpy(bad, fixture, 12); memset(bad + 8, 0, 4);
	CHECK(fs_replace_file(path, bad, 12) == 0);
	CHECK(st_rip3_variables_load_file(&empty, path) == 0);
	CHECK(st_rip3_variables_count(empty, &count) == 0 && count == 0);
	st_rip3_variables_destroy(empty);
	CHECK(fs_replace_file(path, version_one, sizeof(version_one)) == 0);
	struct st_rip3_variables *old = NULL;
	struct st_rip3_variable_entry entry;
	CHECK(st_rip3_variables_load_file(&old, path) == 0);
	CHECK(st_rip3_variables_get_entry(old, "NAME", 4, &entry) == 0 && !entry.require_confirmation
	    && entry.value.length == 3 && memcmp(entry.value.bytes, "A\0B", 3) == 0);
	st_rip3_variables_destroy(old);
	st_rip3_variables_destroy(variables);
	free(path); free(blocked); free(missing);
	return 0;
}

static int
cleanup(const char *directory)
{
	const char *const files[] = {filename,"corrupt.bin"};
	char *path = NULL;
	int status;

	for (size_t i = 0; i < sizeof(files) / sizeof(*files); i++) {
		CHECK(path_join_alloc(&path, directory, files[i]) == 0);
		status = fs_remove_file(path);
		CHECK(status == 0 || status == IO_ERROR_NOT_FOUND);
		free(path); path = NULL;
	}
	CHECK(path_join_alloc(&path, directory, "blocked") == 0);
	status = fs_remove_directory(path);
	CHECK(status == 0 || status == IO_ERROR_NOT_FOUND);
	free(path);
	/* Nonrecursive removal also detects stray temporary files after failures. */
	status = fs_remove_directory(directory);
	CHECK(status == 0 || status == IO_ERROR_NOT_FOUND);
	return 0;
}

int
main(int argc, char **argv)
{
	CHECK(argc == 3);
	if (strcmp(argv[1], "write") == 0)
		return write_store(argv[2]);
	if (strcmp(argv[1], "read") == 0)
		return read_store(argv[2]);
	if (strcmp(argv[1], "errors") == 0)
		return malformed_and_io(argv[2]);
	if (strcmp(argv[1], "cleanup") == 0)
		return cleanup(argv[2]);
	return 1;
}
