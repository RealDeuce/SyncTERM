/* Portable and confined per-BBS cache keys. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_bbs_list_internal.h"

#include "io_error.h"
#include "st_hex.h"
#include "st_unicode_compare.h"
#include "st_digest.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool
st_bbs_cache_key(char *result, size_t result_size, const char *name)
{
	unsigned char digest[32];
	char *folded = NULL;
	size_t folded_size;
	int status;

	if (result == NULL || result_size <= ST_BBS_CACHE_KEY_MAX
	    || !st_bbs_name_valid(name))
		return false;
	status = st_unicode_caseless_key(&folded, &folded_size, name,
	    strlen(name));
	if (status != 0)
		return false;
	status = st_digest(ST_DIGEST_SHA256, folded, folded_size, digest,
	    sizeof(digest));
	free(folded);
	if (status != 0)
		return false;
	memcpy(result, "[u18-", 5);
	if (!st_hex_encode(result + 5, result_size - 5, digest, sizeof(digest)))
		return false;
	result[69] = ']';
	result[70] = '\0';
	return true;
}

bool
st_bbs_cache_key_recognized(const char *key)
{
	if (key == NULL || strlen(key) < ST_BBS_CACHE_KEY_MAX
	    || memcmp(key, "[u18-", 5) != 0 || key[69] != ']')
		return false;
	for (size_t i = 5; i < 69; ++i) {
		if (!((key[i] >= '0' && key[i] <= '9')
		    || (key[i] >= 'a' && key[i] <= 'f')))
			return false;
	}
	if (key[70] != '\0') {
		size_t suffix = 0;

		if (key[70] != '-' || key[71] < '1' || key[71] > '9')
			return false;
		for (size_t i = 71; key[i] != '\0'; ++i) {
			if (key[i] < '0' || key[i] > '9'
			    || suffix > (SIZE_MAX - (size_t)(key[i] - '0')) / 10)
				return false;
			suffix = suffix * 10 + (size_t)(key[i] - '0');
		}
	}
	return true;
}
