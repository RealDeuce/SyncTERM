/* Portable and confined per-BBS cache keys. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_bbs_list_internal.h"

#include "io_error.h"
#include "path.h"
#include "st_hex.h"
#include "string_util.h"

#include <string.h>

static bool
reserved_device(const char *name, size_t length)
{
	static const char *const reserved[] = {
		"con", "prn", "aux", "nul",
		"com1", "com2", "com3", "com4", "com5",
		"com6", "com7", "com8", "com9",
		"lpt1", "lpt2", "lpt3", "lpt4", "lpt5",
		"lpt6", "lpt7", "lpt8", "lpt9",
	};

	while (length != 0 && (name[length - 1] == ' '
	    || name[length - 1] == '.'))
		length--;
	for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i) {
		if (strlen(reserved[i]) == length
		    && ascii_case_ncompare(name, reserved[i], length) == 0)
			return true;
	}
	return false;
}

static bool
portable_component(const char *name)
{
	size_t length;
	size_t basename;

	if (!st_bbs_name_valid(name) || !path_untrusted_component_valid(name))
		return false;
	length = strlen(name);
	if (name[length - 1] == '.' || name[length - 1] == ' ')
		return false;
	for (size_t i = 0; i < length; ++i) {
		if (strchr("<>:\"|?*", name[i]) != NULL)
			return false;
	}
	basename = strcspn(name, ".");
	return !reserved_device(name, basename);
}

/*
 * Brackets are forbidden in display names, so they reserve a collision-free
 * namespace for folded hexadecimal keys without changing ordinary cache names.
 */
bool
st_bbs_cache_key(char *result, size_t result_size, const char *name)
{
	unsigned char folded[ST_PROFILE_NAME_MAX];
	size_t length;

	if (result == NULL || !st_bbs_name_valid(name))
		return false;
	length = strlen(name);
	if (portable_component(name)) {
		if (result_size <= length)
			return false;
		memcpy(result, name, length + 1);
		return true;
	}
	if (result_size < length * 2U + 3U)
		return false;
	for (size_t i = 0; i < length; ++i)
		folded[i] = ascii_to_lower((unsigned char)name[i]);
	result[0] = '[';
	if (!st_hex_encode(result + 1, result_size - 1, folded, length))
		return false;
	result[length * 2U + 1U] = ']';
	result[length * 2U + 2U] = '\0';
	return true;
}

bool
st_bbs_cache_key_recognized(const char *key)
{
	unsigned char decoded[ST_PROFILE_NAME_MAX + 1];
	char canonical[ST_BBS_CACHE_KEY_MAX + 1];
	size_t length;
	size_t encoded_length;

	if (portable_component(key))
		return true;
	if (key == NULL)
		return false;
	length = strlen(key);
	if (length < 4 || length > ST_BBS_CACHE_KEY_MAX || key[0] != '['
	    || key[length - 1] != ']')
		return false;
	encoded_length = length - 2;
	if ((encoded_length & 1U) != 0
	    || !st_hex_decode(decoded, sizeof(decoded) - 1, key + 1,
	    encoded_length))
		return false;
	decoded[encoded_length / 2] = '\0';
	return !portable_component((const char *)decoded)
	    && st_bbs_cache_key(canonical, sizeof(canonical),
	    (const char *)decoded)
	    && strcmp(canonical, key) == 0;
}

int
st_bbs_cache_path(char **result, const st_bbs_list_t *list, const char *key)
{
	if (result == NULL || list == NULL || !st_bbs_cache_key_recognized(key))
		return IO_ERROR_INVALID_ARGUMENT;
	return path_join_alloc(result, list->cache_path, key);
}
