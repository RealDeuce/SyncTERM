/* Test-only boundaries around the real filesystem binding. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "wren_host_internal.h"
#include "filesystem.h"
#include "io_error.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

unsigned st_wren_test_truncate_positions, st_wren_test_truncate_seeks;
unsigned st_wren_test_truncate_calls, st_wren_test_truncate_closes;
uint64_t st_wren_test_truncate_length;
int st_wren_test_truncate_position_error, st_wren_test_truncate_seek_error;
int st_wren_test_truncate_error, st_wren_test_truncate_errno;
WrenVM *st_wren_test_truncate_vm;

static struct st_wren_context *
test_context(WrenVM *vm)
{
	st_wren_test_truncate_vm = vm;
	return st_wren_context_from_vm(vm);
}

static int64_t
test_position(FILE *stream)
{
	++st_wren_test_truncate_positions;
	if (st_wren_test_truncate_position_error != 0) {
		errno = EIO;
		return st_wren_test_truncate_position_error;
	}
	return fs_stream_position(stream);
}

static int
test_seek(FILE *stream, int64_t offset)
{
	++st_wren_test_truncate_seeks;
	if (st_wren_test_truncate_seek_error != 0) {
		errno = ENOSPC;
		return st_wren_test_truncate_seek_error;
	}
	return fs_stream_seek(stream, offset);
}

static int
test_truncate(FILE *stream, uint64_t size)
{
	++st_wren_test_truncate_calls;
	st_wren_test_truncate_length = size;
	if (st_wren_test_truncate_error != 0) {
		errno = st_wren_test_truncate_errno;
		return st_wren_test_truncate_error;
	}
	return fs_stream_truncate(stream, size);
}

static int
test_close(FILE *stream)
{
	++st_wren_test_truncate_closes;
	return fclose(stream);
}

#define fs_stream_position test_position
#define fs_stream_seek test_seek
#define fs_stream_truncate test_truncate
#define fclose test_close
#define st_wren_context_from_vm test_context
#include "../wren_bind_fs.c"

void
st_wren_test_truncate_grant(WrenVM *vm, const char *path,
	enum st_wren_picker_disposition disposition)
{
	(void)st_wren_file_push(vm, 1, path, disposition,
	    disposition == ST_WREN_PICKER_DISPOSITION_READ);
}

void
st_wren_test_truncate_invalidate(WrenVM *vm)
{
	struct st_wren_file *file = wrenGetSlotForeign(vm, 0);
	file->owner = (struct entry_cache_owner){.generation = UINT64_MAX,
	    .identity = UINT64_MAX};
}
