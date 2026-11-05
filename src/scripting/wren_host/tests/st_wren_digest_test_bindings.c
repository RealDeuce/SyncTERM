/* Exercise the production digest binding with controlled native return values. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "wren_host_internal.h"
#include "st_digest.h"
#include "file_map.h"

extern file_map_t *st_wren_test_digest_mapping;
int st_wren_test_digest_status;
unsigned st_wren_test_digest_calls;
bool st_wren_test_digest_direct_mapping = true;

static int
test_digest(enum st_digest_algorithm algorithm, const void *input,
	size_t input_size, void *result, size_t result_size)
{
	++st_wren_test_digest_calls;
	if (st_wren_test_digest_mapping != NULL
	    && (input != file_map_data(st_wren_test_digest_mapping)
	    || input_size != file_map_size(st_wren_test_digest_mapping)))
		st_wren_test_digest_direct_mapping = false;
	return st_wren_test_digest_status != 0 ? st_wren_test_digest_status
	    : st_digest(algorithm, input, input_size, result, result_size);
}

#define st_digest test_digest
#include "../wren_bind_digest.c"
