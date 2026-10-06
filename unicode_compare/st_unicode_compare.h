/* Locale-independent Unicode comparison and collation. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_UNICODE_COMPARE_H_
#define ST_UNICODE_COMPARE_H_

#include <stdbool.h>
#include <stddef.h>

enum st_unicode_collation_strength {
	ST_UNICODE_COLLATION_CASE_INSENSITIVE = 2,
	ST_UNICODE_COLLATION_CASE_SENSITIVE = 3,
};

typedef struct st_unicode_sort_key st_unicode_sort_key_t;

/*
 * Produce NFD(toCasefold(NFD(input))) using the Unicode 18 default full
 * mapping (C + F, excluding Turkic T).  The result is allocated UTF-8 and
 * NUL-terminated; result_size excludes the terminator.  Embedded NUL scalars
 * are supported by the length-based interface.
 */
int st_unicode_caseless_key(char **result, size_t *result_size,
	const void *bytes, size_t size);

/* Strict comparisons. Outputs are unchanged on malformed UTF-8 or failure. */
int st_unicode_caseless_compare(int *comparison,
	const void *left, size_t left_size,
	const void *right, size_t right_size);
int st_unicode_caseless_equal(bool *equal,
	const void *left, size_t left_size,
	const void *right, size_t right_size);

/* Matches must begin and end at extended grapheme-cluster boundaries. */
int st_unicode_caseless_starts_with(bool *matched,
	const void *text, size_t text_size,
	const void *prefix, size_t prefix_size);
int st_unicode_caseless_contains(bool *matched,
	const void *text, size_t text_size,
	const void *needle, size_t needle_size);

/* '*' matches clusters and '?' matches exactly one cluster. */
int st_unicode_wildcard_match(bool *matched,
	const void *pattern, size_t pattern_size,
	const void *text, size_t text_size, bool case_sensitive);

/*
 * DUCET non-ignorable sort keys.  Strength 2 ignores case until the final
 * NFD identical tie-break; strength 3 includes case before that tie-break.
 */
int st_unicode_sort_key_create(st_unicode_sort_key_t **result,
	const void *bytes, size_t size,
	enum st_unicode_collation_strength strength);
void st_unicode_sort_key_destroy(st_unicode_sort_key_t **key);
int st_unicode_sort_key_compare(const st_unicode_sort_key_t *left,
	const st_unicode_sort_key_t *right);
int st_unicode_collate(int *comparison,
	const void *left, size_t left_size,
	const void *right, size_t right_size,
	enum st_unicode_collation_strength strength);

/* Shared policy for user-visible configuration/display names. */
bool st_unicode_name_valid(const char *name, size_t maximum_scalars,
	size_t maximum_bytes, const char *forbidden_ascii);

#endif
