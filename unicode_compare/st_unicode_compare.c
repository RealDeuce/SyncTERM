/* Locale-independent Unicode comparison and collation. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_unicode_compare_internal.h"

#include "io_error.h"
#include "st_unicode_compare_data.h"
#include "st_unicode_normalization.h"
#include "st_unicode_text.h"
#include "st_utf8.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct scalar_list {
	uint32_t *items;
	size_t count;
	size_t capacity;
};

struct byte_list {
	uint8_t *items;
	size_t count;
	size_t capacity;
};

struct ce_list {
	struct st_unicode_collation_element *items;
	size_t count;
	size_t capacity;
};

struct st_unicode_sort_key {
	uint32_t *weights;
	size_t count;
	size_t level_count;
	enum st_unicode_collation_strength strength;
};

static bool
scalar_reserve(struct scalar_list *list, size_t extra)
{
	size_t needed;
	size_t capacity;
	uint32_t *items;

	if (extra > SIZE_MAX - list->count)
		return false;
	needed = list->count + extra;
	if (needed <= list->capacity)
		return true;
	capacity = list->capacity == 0 ? 32 : list->capacity;
	while (capacity < needed) {
		if (capacity > SIZE_MAX / 2)
			capacity = needed;
		else
			capacity *= 2;
	}
	if (capacity > SIZE_MAX / sizeof(*items))
		return false;
	items = realloc(list->items, capacity * sizeof(*items));
	if (items == NULL)
		return false;
	list->items = items;
	list->capacity = capacity;
	return true;
}

static bool
scalar_append(struct scalar_list *list, uint32_t value)
{
	if (!scalar_reserve(list, 1))
		return false;
	list->items[list->count++] = value;
	return true;
}

static bool
byte_reserve(struct byte_list *list, size_t extra)
{
	size_t needed;
	size_t capacity;
	uint8_t *items;

	if (extra > SIZE_MAX - list->count)
		return false;
	needed = list->count + extra;
	if (needed <= list->capacity)
		return true;
	capacity = list->capacity == 0 ? 64 : list->capacity;
	while (capacity < needed) {
		if (capacity > SIZE_MAX / 2)
			capacity = needed;
		else
			capacity *= 2;
	}
	items = realloc(list->items, capacity);
	if (items == NULL)
		return false;
	list->items = items;
	list->capacity = capacity;
	return true;
}

static bool
byte_append(struct byte_list *list, const void *bytes, size_t size)
{
	if (!byte_reserve(list, size))
		return false;
	memcpy(list->items + list->count, bytes, size);
	list->count += size;
	return true;
}

static bool
ce_append(struct ce_list *list,
	const struct st_unicode_collation_element *elements, size_t count)
{
	size_t needed;
	size_t capacity;
	struct st_unicode_collation_element *items;

	if (count > SIZE_MAX - list->count)
		return false;
	needed = list->count + count;
	if (needed > list->capacity) {
		capacity = list->capacity == 0 ? 32 : list->capacity;
		while (capacity < needed) {
			if (capacity > SIZE_MAX / 2)
				capacity = needed;
			else
				capacity *= 2;
		}
		if (capacity > SIZE_MAX / sizeof(*items))
			return false;
		items = realloc(list->items, capacity * sizeof(*items));
		if (items == NULL)
			return false;
		list->items = items;
		list->capacity = capacity;
	}
	memcpy(list->items + list->count, elements, count * sizeof(*elements));
	list->count += count;
	return true;
}

static bool
nfd_append_decomposition(struct scalar_list *output, uint32_t codepoint)
{
	size_t count = st_unicode_canonical_decompose(codepoint, NULL, 0);
	uint32_t local[8];
	uint32_t *values = local;

	if (count == 0)
		return false;
	if (count > sizeof(local) / sizeof(local[0])) {
		if (count > SIZE_MAX / sizeof(*values))
			return false;
		values = malloc(count * sizeof(*values));
		if (values == NULL)
			return false;
	}
	if (st_unicode_canonical_decompose(codepoint, values, count) != count) {
		if (values != local)
			free(values);
		return false;
	}
	for (size_t i = 0; i < count; ++i) {
		uint8_t ccc = st_unicode_canonical_combining_class(values[i]);
		size_t position;

		if (!scalar_append(output, values[i])) {
			if (values != local)
				free(values);
			return false;
		}
		if (ccc == 0)
			continue;
		position = output->count - 1;
		while (position > 0) {
			uint8_t prior = st_unicode_canonical_combining_class(
			    output->items[position - 1]);

			if (prior == 0 || prior <= ccc)
				break;
			output->items[position] = output->items[position - 1];
			--position;
		}
		output->items[position] = values[i];
	}
	if (values != local)
		free(values);
	return true;
}

static int
decode_nfd(struct scalar_list *output, const void *bytes, size_t size)
{
	const char *input = bytes;
	size_t offset = 0;

	if (bytes == NULL && size != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	while (offset < size) {
		uint32_t codepoint;
		size_t consumed;
		int status = st_utf8_decode_one(&codepoint, &consumed,
		    input + offset, size - offset);

		if (status != 0)
			return IO_ERROR_INVALID_ENCODING;
		if (!nfd_append_decomposition(output, codepoint))
			return IO_ERROR_OUT_OF_MEMORY;
		offset += consumed;
	}
	return 0;
}

static int
fold_nfd(struct scalar_list *output, const void *bytes, size_t size)
{
	struct scalar_list first = {0};
	int status = decode_nfd(&first, bytes, size);

	if (status != 0)
		return status;
	for (size_t i = 0; i < first.count; ++i) {
		uint32_t mapping[3];
		size_t count = st_unicode_casefold_mapping(first.items[i], mapping);

		for (size_t j = 0; j < count; ++j) {
			if (!nfd_append_decomposition(output, mapping[j])) {
				free(first.items);
				return IO_ERROR_OUT_OF_MEMORY;
			}
		}
	}
	free(first.items);
	return 0;
}

static int
encode_scalars(char **result, size_t *result_size,
	const struct scalar_list *scalars)
{
	struct byte_list bytes = {0};
	char local[4];

	for (size_t i = 0; i < scalars->count; ++i) {
		ptrdiff_t count = st_utf8_encode_one(local, sizeof(local),
		    scalars->items[i]);

		if (count < 0 || !byte_append(&bytes, local, (size_t)count)) {
			free(bytes.items);
			return count < 0 ? IO_ERROR_INVALID_ENCODING
			    : IO_ERROR_OUT_OF_MEMORY;
		}
	}
	if (!byte_reserve(&bytes, 1)) {
		free(bytes.items);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	bytes.items[bytes.count] = '\0';
	*result = (char *)bytes.items;
	*result_size = bytes.count;
	return 0;
}

int
st_unicode_caseless_key(char **result, size_t *result_size,
	const void *bytes, size_t size)
{
	struct scalar_list folded = {0};
	char *key;
	size_t key_size;
	int status;

	if (result == NULL || result_size == NULL
	    || (bytes == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	status = fold_nfd(&folded, bytes, size);
	if (status == 0)
		status = encode_scalars(&key, &key_size, &folded);
	free(folded.items);
	if (status == 0) {
		*result = key;
		*result_size = key_size;
	}
	return status;
}

static int
binary_compare(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	size_t shared = left_size < right_size ? left_size : right_size;
	int compared = memcmp(left, right, shared);

	if (compared != 0)
		return compared < 0 ? -1 : 1;
	return left_size == right_size ? 0 : left_size < right_size ? -1 : 1;
}

int
st_unicode_caseless_compare(int *comparison,
	const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	char *left_key = NULL;
	char *right_key = NULL;
	size_t left_key_size;
	size_t right_key_size;
	int status;

	if (comparison == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_unicode_caseless_key(&left_key, &left_key_size,
	    left, left_size);
	if (status == 0)
		status = st_unicode_caseless_key(&right_key, &right_key_size,
		    right, right_size);
	if (status == 0)
		*comparison = binary_compare(left_key, left_key_size,
		    right_key, right_key_size);
	free(right_key);
	free(left_key);
	return status;
}

int
st_unicode_caseless_equal(bool *equal,
	const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	int compared;
	int status;

	if (equal == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_unicode_caseless_compare(&compared, left, left_size,
	    right, right_size);
	if (status == 0)
		*equal = compared == 0;
	return status;
}

static bool
implicit_elements(uint32_t codepoint,
	struct st_unicode_collation_element result[2])
{
	uint32_t flags = st_unicode_property_flags(codepoint);
	uint32_t base;
	uint16_t lead;

	if ((flags & ST_UNICODE_PROPERTY_UNASSIGNED) == 0
	    && st_unicode_implicit_range(codepoint, &lead, &base)) {
		result[0] = (struct st_unicode_collation_element){lead, 0x20, 2};
		result[1] = (struct st_unicode_collation_element){
		    (uint16_t)((codepoint - base) | 0x8000), 0, 0};
		return true;
	}
	if ((flags & ST_UNICODE_PROPERTY_UNIFIED_IDEOGRAPH) != 0) {
		bool core = (codepoint >= UINT32_C(0x4e00)
		    && codepoint <= UINT32_C(0x9fff))
		    || (codepoint >= UINT32_C(0xf900)
		    && codepoint <= UINT32_C(0xfaff));

		lead = (uint16_t)((core ? 0xfb40 : 0xfb80)
		    + (codepoint >> 15));
	}
	else
		lead = (uint16_t)(0xfbc0 + (codepoint >> 15));
	result[0] = (struct st_unicode_collation_element){lead, 0x20, 2};
	result[1] = (struct st_unicode_collation_element){
	    (uint16_t)((codepoint & 0x7fff) | 0x8000), 0, 0};
	return true;
}

static int
collation_elements(struct ce_list *output, const struct scalar_list *nfd)
{
	struct scalar_list pending = {0};

	if (!scalar_reserve(&pending, nfd->count))
		return IO_ERROR_OUT_OF_MEMORY;
	memcpy(pending.items, nfd->items, nfd->count * sizeof(*pending.items));
	pending.count = nfd->count;
	while (pending.count != 0) {
		const struct st_unicode_collation_element *mapping = NULL;
		struct st_unicode_collation_element implicit[2];
		size_t mapping_count = 0;
		size_t sequence_count = 0;
		size_t matched[3] = {0};
		uint32_t sequence[3];

		for (size_t count = pending.count < 3 ? pending.count : 3;
		    count != 0; --count) {
			mapping_count = st_unicode_collation_mapping(pending.items,
			    count, &mapping);
			if (mapping_count != 0) {
				sequence_count = count;
				for (size_t i = 0; i < count; ++i) {
					sequence[i] = pending.items[i];
					matched[i] = i;
				}
				break;
			}
		}
		if (mapping_count != 0 && sequence_count < 3) {
			uint8_t skipped_ccc = 0;

			for (size_t i = sequence_count;
			    i < pending.count && sequence_count < 3; ++i) {
				uint8_t ccc = st_unicode_canonical_combining_class(
				    pending.items[i]);
				const struct st_unicode_collation_element *extended;
				size_t extended_count;

				if (ccc == 0)
					break;
				if (ccc <= skipped_ccc)
					continue;
				sequence[sequence_count] = pending.items[i];
				if (!st_unicode_collation_has_prefix(sequence,
				    sequence_count + 1)) {
					if (ccc > skipped_ccc)
						skipped_ccc = ccc;
					continue;
				}
				extended_count = st_unicode_collation_mapping(sequence,
				    sequence_count + 1, &extended);
				if (extended_count != 0) {
					matched[sequence_count++] = i;
					mapping = extended;
					mapping_count = extended_count;
				}
			}
		}
		if (mapping_count == 0) {
			(void)implicit_elements(pending.items[0], implicit);
			mapping = implicit;
			mapping_count = 2;
			sequence_count = 1;
			matched[0] = 0;
		}
		if (!ce_append(output, mapping, mapping_count)) {
			free(pending.items);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		for (size_t i = sequence_count; i-- > 0;) {
			memmove(pending.items + matched[i],
			    pending.items + matched[i] + 1,
			    (pending.count - matched[i] - 1)
			    * sizeof(*pending.items));
			--pending.count;
		}
	}
	free(pending.items);
	return 0;
}

static bool
weight_append(struct scalar_list *weights, uint32_t value)
{
	return scalar_append(weights, value);
}

int
st_unicode_sort_key_create(st_unicode_sort_key_t **result,
	const void *bytes, size_t size,
	enum st_unicode_collation_strength strength)
{
	struct scalar_list nfd = {0};
	struct scalar_list weights = {0};
	struct ce_list elements = {0};
	st_unicode_sort_key_t *key = NULL;
	int status;

	if (result == NULL || (bytes == NULL && size != 0)
	    || (strength != ST_UNICODE_COLLATION_CASE_INSENSITIVE
	    && strength != ST_UNICODE_COLLATION_CASE_SENSITIVE))
		return IO_ERROR_INVALID_ARGUMENT;
	status = decode_nfd(&nfd, bytes, size);
	if (status == 0)
		status = collation_elements(&elements, &nfd);
	for (unsigned level = 1; status == 0 && level <= (unsigned)strength;
	    ++level) {
		if (level != 1 && !weight_append(&weights, 0))
			status = IO_ERROR_OUT_OF_MEMORY;
		for (size_t i = 0; status == 0 && i < elements.count; ++i) {
			uint16_t value = level == 1 ? elements.items[i].primary
			    : level == 2 ? elements.items[i].secondary
			    : elements.items[i].tertiary;

			if (value != 0 && !weight_append(&weights, value))
				status = IO_ERROR_OUT_OF_MEMORY;
		}
	}
	size_t level_count = weights.count;
	if (status == 0 && !weight_append(&weights, 0))
		status = IO_ERROR_OUT_OF_MEMORY;
	for (size_t i = 0; status == 0 && i < nfd.count; ++i) {
		/* Reserve zero as the level separator while preserving scalar order. */
		if (!weight_append(&weights, nfd.items[i] + 1U))
			status = IO_ERROR_OUT_OF_MEMORY;
	}
	if (status == 0) {
		key = malloc(sizeof(*key));
		if (key == NULL)
			status = IO_ERROR_OUT_OF_MEMORY;
	}
	if (status == 0) {
		key->weights = weights.items;
		key->count = weights.count;
		key->level_count = level_count;
		key->strength = strength;
		*result = key;
		weights.items = NULL;
	}
	free(weights.items);
	free(elements.items);
	free(nfd.items);
	return status;
}

void
st_unicode_sort_key_destroy(st_unicode_sort_key_t **key)
{
	if (key == NULL || *key == NULL)
		return;
	free((*key)->weights);
	free(*key);
	*key = NULL;
}

int
st_unicode_sort_key_compare(const st_unicode_sort_key_t *left,
	const st_unicode_sort_key_t *right)
{
	size_t shared;

	if (left == NULL || right == NULL)
		return left == right ? 0 : left == NULL ? -1 : 1;
	if (left->strength != right->strength)
		return left->strength < right->strength ? -1 : 1;
	shared = left->count < right->count ? left->count : right->count;
	for (size_t i = 0; i < shared; ++i) {
		if (left->weights[i] != right->weights[i])
			return left->weights[i] < right->weights[i] ? -1 : 1;
	}
	return left->count == right->count ? 0
	    : left->count < right->count ? -1 : 1;
}

int
st_unicode_sort_key_compare_uca(const st_unicode_sort_key_t *left,
	const st_unicode_sort_key_t *right)
{
	size_t shared;

	if (left == NULL || right == NULL)
		return left == right ? 0 : left == NULL ? -1 : 1;
	if (left->strength != right->strength)
		return left->strength < right->strength ? -1 : 1;
	shared = left->level_count < right->level_count
	    ? left->level_count : right->level_count;
	for (size_t i = 0; i < shared; ++i) {
		if (left->weights[i] != right->weights[i])
			return left->weights[i] < right->weights[i] ? -1 : 1;
	}
	return left->level_count == right->level_count ? 0
	    : left->level_count < right->level_count ? -1 : 1;
}

int
st_unicode_collate(int *comparison,
	const void *left, size_t left_size,
	const void *right, size_t right_size,
	enum st_unicode_collation_strength strength)
{
	st_unicode_sort_key_t *left_key = NULL;
	st_unicode_sort_key_t *right_key = NULL;
	int status;

	if (comparison == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_unicode_sort_key_create(&left_key, left, left_size, strength);
	if (status == 0)
		status = st_unicode_sort_key_create(&right_key, right, right_size,
		    strength);
	if (status == 0)
		*comparison = st_unicode_sort_key_compare(left_key, right_key);
	st_unicode_sort_key_destroy(&right_key);
	st_unicode_sort_key_destroy(&left_key);
	return status;
}

struct folded_clusters {
	struct byte_list bytes;
	size_t *boundaries;
	size_t count;
	size_t capacity;
};

static bool
cluster_boundary_append(struct folded_clusters *clusters, size_t offset)
{
	size_t *boundaries;
	size_t capacity;

	if (clusters->count == clusters->capacity) {
		capacity = clusters->capacity == 0 ? 16 : clusters->capacity * 2;
		if (capacity < clusters->capacity
		    || capacity > SIZE_MAX / sizeof(*boundaries))
			return false;
		boundaries = realloc(clusters->boundaries,
		    capacity * sizeof(*boundaries));
		if (boundaries == NULL)
			return false;
		clusters->boundaries = boundaries;
		clusters->capacity = capacity;
	}
	clusters->boundaries[clusters->count++] = offset;
	return true;
}

static int
folded_clusters_create(struct folded_clusters *clusters,
	const void *text, size_t size, bool fold)
{
	st_unicode_text_t *graphemes = NULL;
	struct st_unicode_text_item item;
	size_t consumed;
	int status = st_unicode_text_create(&graphemes, ST_UNICODE_TEXT_GRAPHEMES);

	if (status == 0)
		status = st_unicode_text_feed(graphemes, text, size, &consumed);
	if (status == 0 && consumed != size)
		status = IO_ERROR_INVALID_ENCODING;
	if (status == 0)
		status = st_unicode_text_finish(graphemes);
	if (status == 0 && (st_unicode_text_state(graphemes)
	    & ST_UNICODE_TEXT_SAW_DECODE_ERROR) != 0)
		status = IO_ERROR_INVALID_ENCODING;
	if (status == 0 && !cluster_boundary_append(clusters, 0))
		status = IO_ERROR_OUT_OF_MEMORY;
	while (status == 0 && st_unicode_text_peek(graphemes, &item) == 0) {
		char *key = NULL;
		size_t key_size = 0;

		if (fold)
			status = st_unicode_caseless_key(&key, &key_size,
			    item.bytes, item.size);
		else {
			struct scalar_list nfd = {0};

			status = decode_nfd(&nfd, item.bytes, item.size);
			if (status == 0)
				status = encode_scalars(&key, &key_size, &nfd);
			free(nfd.items);
		}
		if (status == 0 && !byte_append(&clusters->bytes, key, key_size))
			status = IO_ERROR_OUT_OF_MEMORY;
		free(key);
		if (status == 0 && !cluster_boundary_append(clusters,
		    clusters->bytes.count))
			status = IO_ERROR_OUT_OF_MEMORY;
		(void)st_unicode_text_consume(graphemes);
	}
	st_unicode_text_destroy(&graphemes);
	return status;
}

static void
folded_clusters_dispose(struct folded_clusters *clusters)
{
	free(clusters->boundaries);
	free(clusters->bytes.items);
	memset(clusters, 0, sizeof(*clusters));
}

static int
caseless_search(bool *matched, const void *text, size_t text_size,
	const void *needle, size_t needle_size, bool prefix)
{
	struct folded_clusters haystack = {0};
	char *key = NULL;
	size_t key_size = 0;
	int status;

	if (matched == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = folded_clusters_create(&haystack, text, text_size, true);
	if (status == 0)
		status = st_unicode_caseless_key(&key, &key_size,
		    needle, needle_size);
	if (status == 0) {
		bool found = key_size == 0;
		size_t starts = prefix ? (haystack.count == 0 ? 0 : 1)
		    : haystack.count;

		for (size_t start = 0; !found && start < starts; ++start) {
			for (size_t end = start + 1; end < haystack.count; ++end) {
				size_t begin = haystack.boundaries[start];
				size_t length = haystack.boundaries[end] - begin;

				if (length == key_size
				    && memcmp(haystack.bytes.items + begin,
				    key, key_size) == 0) {
					found = true;
					break;
				}
				if (length > key_size)
					break;
			}
		}
		*matched = found;
	}
	free(key);
	folded_clusters_dispose(&haystack);
	return status;
}

int
st_unicode_caseless_starts_with(bool *matched,
	const void *text, size_t text_size,
	const void *prefix, size_t prefix_size)
{
	return caseless_search(matched, text, text_size, prefix, prefix_size,
	    true);
}

int
st_unicode_caseless_contains(bool *matched,
	const void *text, size_t text_size,
	const void *needle, size_t needle_size)
{
	return caseless_search(matched, text, text_size, needle, needle_size,
	    false);
}

static int
wildcard_recursive(bool *matched, const uint32_t *pattern, size_t pattern_count,
	size_t pattern_at, const struct folded_clusters *text, size_t text_at,
	bool case_sensitive)
{
	while (pattern_at < pattern_count) {
		uint32_t value = pattern[pattern_at];

		if (value == '*') {
			while (pattern_at < pattern_count && pattern[pattern_at] == '*')
				++pattern_at;
			if (pattern_at == pattern_count) {
				*matched = true;
				return 0;
			}
			for (size_t next = text_at; next < text->count; ++next) {
				bool child;
				int status = wildcard_recursive(&child, pattern, pattern_count,
				    pattern_at, text, next, case_sensitive);

				if (status != 0)
					return status;
				if (child) {
					*matched = true;
					return 0;
				}
			}
			*matched = false;
			return 0;
		}
		if (value == '?') {
			if (text_at + 1 >= text->count) {
				*matched = false;
				return 0;
			}
			++text_at;
			++pattern_at;
			continue;
		}
		{
			size_t end = pattern_at;
			char *literal;
			size_t literal_size;
			struct scalar_list scalars = {0};
			struct scalar_list folded = {0};
			int status = 0;
			bool found = false;

			while (end < pattern_count && pattern[end] != '*'
			    && pattern[end] != '?')
				++end;
			if (case_sensitive) {
				for (size_t i = pattern_at; i < end; ++i) {
					if (!nfd_append_decomposition(&folded, pattern[i])) {
						status = IO_ERROR_OUT_OF_MEMORY;
						goto wildcard_failed;
					}
				}
			}
			else {
				for (size_t i = pattern_at; i < end; ++i) {
					if (!scalar_append(&scalars, pattern[i])) {
						status = IO_ERROR_OUT_OF_MEMORY;
						goto wildcard_failed;
					}
				}
				{
					char *encoded;
					size_t encoded_size;

					status = encode_scalars(&encoded, &encoded_size,
					    &scalars);
					if (status != 0)
						goto wildcard_failed;
					status = fold_nfd(&folded, encoded, encoded_size);
					free(encoded);
					if (status != 0)
						goto wildcard_failed;
				}
			}
			status = encode_scalars(&literal, &literal_size, &folded);
			if (status != 0)
				goto wildcard_failed;
			for (size_t next = text_at + 1; next < text->count; ++next) {
				size_t begin = text->boundaries[text_at];
				size_t length = text->boundaries[next] - begin;

				if (length == literal_size
				    && memcmp(text->bytes.items + begin,
				    literal, length) == 0) {
					text_at = next;
					found = true;
					break;
				}
				if (length > literal_size)
					break;
			}
			free(literal);
			free(folded.items);
			free(scalars.items);
			if (!found) {
				*matched = false;
				return 0;
			}
			pattern_at = end;
			continue;

wildcard_failed:
			free(folded.items);
			free(scalars.items);
			return status;
		}
	}
	*matched = text_at + 1 == text->count;
	return 0;
}

int
st_unicode_wildcard_match(bool *matched,
	const void *pattern, size_t pattern_size,
	const void *text, size_t text_size, bool case_sensitive)
{
	struct scalar_list pattern_nfd = {0};
	struct folded_clusters clusters = {0};
	int status;

	if (matched == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = decode_nfd(&pattern_nfd, pattern, pattern_size);
	if (status == 0)
		status = folded_clusters_create(&clusters, text, text_size,
		    !case_sensitive);
	if (status == 0)
		status = wildcard_recursive(matched, pattern_nfd.items,
		    pattern_nfd.count, 0, &clusters, 0, case_sensitive);
	free(pattern_nfd.items);
	folded_clusters_dispose(&clusters);
	return status;
}

bool
st_unicode_name_valid(const char *name, size_t maximum_scalars,
	size_t maximum_bytes, const char *forbidden_ascii)
{
	char *normalized = NULL;
	size_t normalized_size;
	size_t length;
	size_t offset = 0;
	size_t scalars = 0;
	uint32_t first_flags = 0;
	uint32_t last_flags = 0;
	bool visible = false;

	if (name == NULL)
		return false;
	length = strlen(name);
	if (length == 0 || length > maximum_bytes
	    || st_unicode_text_normalize(&normalized, &normalized_size,
	    name, length, NULL) != 0)
		return false;
	if (normalized_size != length || memcmp(normalized, name, length) != 0) {
		free(normalized);
		return false;
	}
	free(normalized);
	while (offset < length) {
		uint32_t codepoint;
		uint32_t flags;
		size_t consumed;

		if (st_utf8_decode_one(&codepoint, &consumed, name + offset,
		    length - offset) != 0)
			return false;
		if (++scalars > maximum_scalars)
			return false;
		flags = st_unicode_property_flags(codepoint);
		if (scalars == 1)
			first_flags = flags;
		last_flags = flags;
		if ((flags & (ST_UNICODE_PROPERTY_CONTROL
		    | ST_UNICODE_PROPERTY_UNASSIGNED
		    | ST_UNICODE_PROPERTY_PRIVATE_USE
		    | ST_UNICODE_PROPERTY_BIDI_CONTROL
		    | ST_UNICODE_PROPERTY_NONCHARACTER
		    | ST_UNICODE_PROPERTY_LINE_SEPARATOR)) != 0)
			return false;
		if ((flags & ST_UNICODE_PROPERTY_FORMAT) != 0
		    && codepoint != UINT32_C(0x200c)
		    && codepoint != UINT32_C(0x200d)
		    && !(codepoint >= UINT32_C(0xe0020)
		    && codepoint <= UINT32_C(0xe007f)))
			return false;
		if (codepoint < 0x80 && forbidden_ascii != NULL
		    && strchr(forbidden_ascii, (int)codepoint) != NULL)
			return false;
		if ((flags & (ST_UNICODE_PROPERTY_FORMAT
		    | ST_UNICODE_PROPERTY_VARIATION_SELECTOR
		    | ST_UNICODE_PROPERTY_WHITE_SPACE)) == 0)
			visible = true;
		offset += consumed;
	}
	return visible && (first_flags & ST_UNICODE_PROPERTY_WHITE_SPACE) == 0
	    && (last_flags & ST_UNICODE_PROPERTY_WHITE_SPACE) == 0;
}
