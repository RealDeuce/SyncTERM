/* Digest identifiers shared by the xptls crypto primitives. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef XP_DIGEST_H_
#define XP_DIGEST_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum xp_digest_algorithm {
	XP_DIGEST_SHA256 = 1,
	XP_DIGEST_SHA384,
	XP_DIGEST_SHA512,
};

size_t xp_digest_size(enum xp_digest_algorithm algorithm);
int xp_digest(enum xp_digest_algorithm algorithm, const void *input,
	size_t input_size, void *result, size_t result_size);

#ifdef __cplusplus
}
#endif

#endif
