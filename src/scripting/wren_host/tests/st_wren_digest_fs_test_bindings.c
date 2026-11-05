/* Test-only observations at the real filesystem binding's native boundaries. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "wren_host_internal.h"
#include "file_map.h"
#include <errno.h>

file_map_t *st_wren_test_digest_mapping;
unsigned st_wren_test_digest_map_opens, st_wren_test_digest_map_closes;
unsigned st_wren_test_digest_map_calls, st_wren_test_digest_flush_calls;
int st_wren_test_digest_map_status;
bool st_wren_test_digest_flush_fail;

static int
test_digest_flush(FILE *stream)
{
	++st_wren_test_digest_flush_calls;
	if (st_wren_test_digest_flush_fail) {
		errno = ENOSPC;
		return EOF;
	}
	return fflush(stream);
}

static int
test_digest_map_open(file_map_t **result, const char *path)
{
	++st_wren_test_digest_map_calls;
	int status = st_wren_test_digest_map_status;
	if (status == 0) status = file_map_open(result, path);
	if (status == 0) {
		st_wren_test_digest_mapping = *result;
		++st_wren_test_digest_map_opens;
	}
	return status;
}

static void
test_digest_map_close(file_map_t *map)
{
	if (map != NULL) ++st_wren_test_digest_map_closes;
	file_map_close(map);
	st_wren_test_digest_mapping = NULL;
}

#define file_map_open test_digest_map_open
#define file_map_close test_digest_map_close
#define fflush test_digest_flush
#include "../wren_bind_fs.c"

void
st_wren_test_digest_push_grant(WrenVM *vm, const char *path, bool stale, bool save)
{
	const struct entry_cache_owner owner = {.generation = UINT64_MAX, .identity = UINT64_MAX};
	if (stale)
		(void)push_file_full(vm, 1, path, FILE_RIGHT_READ,
		    ST_WREN_PICKER_DISPOSITION_READ, NULL, &owner);
	else
		(void)st_wren_file_push(vm, 1, path, save
		    ? ST_WREN_PICKER_DISPOSITION_OVERWRITE : ST_WREN_PICKER_DISPOSITION_READ, !save);
}
