/* Digest identifiers shared by the SyncTERM crypto primitives. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_DIGEST_H_
#define ST_DIGEST_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum st_digest_algorithm {
	ST_DIGEST_SHA256 = 1,
	ST_DIGEST_SHA384,
	ST_DIGEST_SHA512,
	ST_DIGEST_MD5,
	ST_DIGEST_SHA1,
};

size_t st_digest_size(enum st_digest_algorithm algorithm);
int st_digest(enum st_digest_algorithm algorithm, const void *input,
	size_t input_size, void *result, size_t result_size);

#ifdef __cplusplus
}
#endif

#endif
