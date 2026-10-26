/* Internal declarations for legacy encrypted-list decoders. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_LIST_CRYPTO_INTERNAL_H_
#define ST_LIST_CRYPTO_INTERNAL_H_

#include <stddef.h>

int st_legacy_idea_decrypt_cbc(const void *key, size_t key_size,
	const void *iv, void *data, size_t size);
int st_legacy_rc2_decrypt_cbc(const void *key, size_t key_size,
	const void *iv, void *data, size_t size);

#endif
