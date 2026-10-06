/* Portable and confined per-BBS cache keys. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_bbs_list_internal.h"

#include "io_error.h"
#include "path.h"
#include "st_hex.h"
#include "st_unicode_compare.h"
#include "xp_digest.h"

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
	status = xp_digest(XP_DIGEST_SHA256, folded, folded_size, digest,
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
	if (key == NULL || strlen(key) != ST_BBS_CACHE_KEY_MAX
	    || memcmp(key, "[u18-", 5) != 0 || key[69] != ']')
		return false;
	for (size_t i = 5; i < 69; ++i) {
		if (!((key[i] >= '0' && key[i] <= '9')
		    || (key[i] >= 'a' && key[i] <= 'f')))
			return false;
	}
	return true;
}

int
st_bbs_cache_path(char **result, const st_bbs_list_t *list, const char *key)
{
	if (result == NULL || list == NULL || !st_bbs_cache_key_recognized(key))
		return IO_ERROR_INVALID_ARGUMENT;
	return path_join_alloc(result, list->cache_path, key);
}

int
st_bbs_list_cache_path(char **result, const st_bbs_list_t *list,
	const char *name)
{
	char key[ST_BBS_CACHE_KEY_MAX + 1];

	if (result == NULL || list == NULL || name == NULL
	    || !st_bbs_cache_key(key, sizeof(key), name))
		return IO_ERROR_INVALID_ARGUMENT;
	return st_bbs_cache_path(result, list, key);
}
