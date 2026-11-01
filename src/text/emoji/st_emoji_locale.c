/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_emoji_locale.h"
#include "st_emoji_locale_internal.h"
#include "st_emoji_catalog.h"
#include "st_unicode_compare.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct st_emoji_locale {
	char **buffers;
	const char **names;
	const char **keywords;
	const char **english_keywords;
	char **search_keys;
};

static bool
unpack_length(size_t *length, const unsigned char *input, size_t packed_size,
	size_t *position, size_t available)
{
	if (*length == 15) {
		unsigned char extra;

		do {
			if (*position >= packed_size)
				return false;
			extra = input[(*position)++];
			if (*length > available || extra > available - *length)
				return false;
			*length += extra;
		} while (extra == 255);
	}
	return *length <= available;
}

int
st_emoji_unpack(unsigned char *output, size_t size,
	const unsigned char *input, size_t packed_size)
{
	size_t in = 0, out = 0;

	while (in < packed_size) {
		unsigned char token = input[in++];
		size_t length = token >> 4;
		size_t offset;

		if (!unpack_length(&length, input, packed_size, &in, size - out)
		    || length > packed_size - in)
			return IO_ERROR_INVALID_FORMAT;
		memcpy(output + out, input + in, length);
		out += length;
		in += length;
		if (in == packed_size)
			return out == size ? 0 : IO_ERROR_INVALID_FORMAT;
		if (packed_size - in < 2)
			return IO_ERROR_INVALID_FORMAT;
		offset = input[in] | ((size_t)input[in + 1] << 8);
		in += 2;
		length = token & 15;
		if (!unpack_length(&length, input, packed_size, &in, size - out)
		    || size - out < 4 || length > size - out - 4
		    || offset == 0 || offset > out)
			return IO_ERROR_INVALID_FORMAT;
		length += 4;
		/* LZ4 matches may overlap, including runs with a one-byte offset. */
		for (size_t i = 0; i < length; i++) {
			output[out] = output[out - offset];
			out++;
		}
	}
	return IO_ERROR_INVALID_FORMAT;
}

static size_t
find_locale(const char *language)
{
	char tag[256];
	size_t size = strlen(language);
	size_t minimum = 0;

	if (size == 0 || size >= sizeof(tag))
		return st_emoji_locale_count;
	for (size_t i = 0; i <= size; i++) {
		char c = language[i];

		if (c >= 'A' && c <= 'Z')
			c = (char)(c + ('a' - 'A'));
		tag[i] = c == '_' ? '-' : c;
	}
	/* Extensions don't select an annotation locale. Keep explicit scripts:
	 * an unsupported script must not fall back to another writing system. */
	for (size_t start = 0; start < size;) {
		size_t end = start;
		while (end < size && tag[end] != '-')
			end++;
		if (start > 0 && end - start == 1) {
			size = start - 1;
			tag[size] = 0;
			break;
		}
		if (start > 0 && end - start == 4
		    && tag[start] >= 'a' && tag[start] <= 'z')
			minimum = end;
		start = end + 1;
	}
	while (size > 0) {
		size_t low = 0, high = st_emoji_locale_alias_count;

		while (low < high) {
			size_t middle = low + (high - low) / 2;
			int compared = strcmp(tag, st_emoji_locale_aliases[middle].tag);
			if (compared == 0)
				return st_emoji_locale_aliases[middle].locale;
			if (compared < 0)
				high = middle;
			else
				low = middle + 1;
		}
		if (size <= minimum)
			break;
		char *hyphen = strrchr(tag, '-');
		if (hyphen == NULL)
			break;
		size = (size_t)(hyphen - tag);
		*hyphen = 0;
	}
	return st_emoji_locale_count;
}

static int
load_locale(st_emoji_locale_t *locale, size_t index)
{
	const struct st_emoji_locale_data *data = &st_emoji_locales[index];
	bool english = strcmp(data->tag, "en") == 0;
	char *buffer = malloc(data->size == 0 ? 1 : data->size);
	size_t position = 0;
	int status;

	if (buffer == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	locale->buffers[index] = buffer;
	status = st_emoji_unpack((unsigned char *)buffer, data->size,
	    data->packed, data->packed_size);
	if (status != 0)
		return status;
	while (position < data->size) {
		size_t id;
		const char *fields[2];

		if (data->size - position < 2)
			return IO_ERROR_INVALID_FORMAT;
		id = (unsigned char)buffer[position]
		    | ((size_t)(unsigned char)buffer[position + 1] << 8);
		position += 2;
		if (id >= st_emoji_entry_count)
			return IO_ERROR_INVALID_FORMAT;
		for (size_t field = 0; field < 2; field++) {
			char *end = memchr(buffer + position, 0, data->size - position);
			if (end == NULL)
				return IO_ERROR_INVALID_FORMAT;
			fields[field] = buffer + position;
			position = (size_t)(end - buffer) + 1;
		}
		if (locale->names[id] == NULL && fields[0][0] != 0)
			locale->names[id] = english ? st_emoji_entries[id].name : fields[0];
		if (locale->keywords[id] == NULL && fields[1][0] != 0)
			locale->keywords[id] = fields[1];
		if (english)
			locale->english_keywords[id] = fields[1];
	}
	return 0;
}

static int
load_preferences(st_emoji_locale_t *locale, size_t index)
{
	while (index < st_emoji_locale_count && locale->buffers[index] == NULL) {
		int status = load_locale(locale, index);
		if (status != 0)
			return status;
		int parent = st_emoji_locales[index].parent;
		index = parent < 0 ? st_emoji_locale_count : (size_t)parent;
	}
	return 0;
}

static void
search_fields(const st_emoji_locale_t *locale, size_t id, const char *fields[4])
{
	fields[0] = locale->names[id];
	fields[1] = locale->keywords[id] == NULL ? "" : locale->keywords[id];
	fields[2] = st_emoji_entries[id].name;
	fields[3] = locale->english_keywords[id] == NULL ? "" : locale->english_keywords[id];
}

int
st_emoji_locale_create(st_emoji_locale_t **result,
	const char *const *languages, size_t count)
{
	st_emoji_locale_t *locale;
	int status = IO_ERROR_OUT_OF_MEMORY;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*result = NULL;
	if (languages == NULL && count != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	locale = calloc(1, sizeof(*locale));
	if (locale == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	locale->buffers = calloc(st_emoji_locale_count, sizeof(*locale->buffers));
	locale->names = calloc(st_emoji_entry_count, sizeof(*locale->names));
	locale->keywords = calloc(st_emoji_entry_count, sizeof(*locale->keywords));
	locale->english_keywords = calloc(st_emoji_entry_count, sizeof(*locale->english_keywords));
	locale->search_keys = calloc(st_emoji_entry_count, sizeof(*locale->search_keys));
	if (locale->buffers == NULL || locale->names == NULL || locale->keywords == NULL
	    || locale->english_keywords == NULL || locale->search_keys == NULL)
		goto fail;
	for (size_t i = 0; i < count; i++) {
		if (languages[i] == NULL) {
			status = IO_ERROR_INVALID_ARGUMENT;
			goto fail;
		}
		status = load_preferences(locale, find_locale(languages[i]));
		if (status != 0)
			goto fail;
	}
	status = load_preferences(locale, find_locale("en"));
	if (status != 0)
		goto fail;
	for (size_t i = 0; i < st_emoji_entry_count; i++) {
		const char *fields[4];
		size_t size = 0, key_size;
		char *text, *position;

		if (locale->names[i] == NULL)
			locale->names[i] = st_emoji_entries[i].name;
		search_fields(locale, i, fields);
		for (size_t field = 0; field < 4; field++)
			size += strlen(fields[field]) + 1;
		text = malloc(size + 1);
		if (text == NULL) {
			status = IO_ERROR_OUT_OF_MEMORY;
			goto fail;
		}
		position = text;
		for (size_t field = 0; field < 4; field++) {
			size_t length = strlen(fields[field]);
			memcpy(position, fields[field], length);
			position += length;
			*position++ = '\x1f';
		}
		*position = 0;
		status = st_unicode_caseless_key(&locale->search_keys[i], &key_size, text, size);
		free(text);
		if (status != 0)
			goto fail;
	}
	*result = locale;
	return 0;
fail:
	st_emoji_locale_destroy(locale);
	return status;
}

void
st_emoji_locale_destroy(st_emoji_locale_t *locale)
{
	if (locale == NULL)
		return;
	if (locale->buffers != NULL)
		for (size_t i = 0; i < st_emoji_locale_count; i++)
			free(locale->buffers[i]);
	if (locale->search_keys != NULL)
		for (size_t i = 0; i < st_emoji_entry_count; i++)
			free(locale->search_keys[i]);
	free(locale->buffers);
	free(locale->names);
	free(locale->keywords);
	free(locale->english_keywords);
	free(locale->search_keys);
	free(locale);
}

const char *
st_emoji_localized_name(const st_emoji_locale_t *locale, size_t id)
{
	if (id >= st_emoji_entry_count)
		return NULL;
	return locale == NULL ? st_emoji_entries[id].name : locale->names[id];
}

int
st_emoji_search(const st_emoji_locale_t *locale, const void *query,
	size_t size, size_t **indexes, size_t *count)
{
	char *key;
	size_t key_size;
	int status;

	if (indexes != NULL)
		*indexes = NULL;
	if (count != NULL)
		*count = 0;
	if (indexes == NULL || count == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (locale == NULL || (query == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_unicode_caseless_key(&key, &key_size, query, size);
	if (status != 0)
		return status;
	if (memchr(key, 0, key_size) != NULL) {
		free(key);
		return 0;
	}
	size_t *ids = malloc(st_emoji_entry_count * sizeof(*ids));
	if (ids == NULL) {
		free(key);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	for (size_t i = 0; i < st_emoji_entry_count; i++) {
		bool matched = key_size == 0 && st_emoji_entries[i].base == i;

		if (key_size != 0 && strstr(locale->search_keys[i], key) != NULL) {
			const char *fields[4];

			search_fields(locale, i, fields);
			/* Cached keys narrow the candidates; the shared matcher enforces
			 * grapheme boundaries and prevents cross-field matches. */
			for (size_t field = 0; field < 4 && !matched; field++) {
				status = st_unicode_caseless_contains(&matched, fields[field],
				    strlen(fields[field]), query, size);
				if (status != 0) {
					free(key);
					free(ids);
					*count = 0;
					return status;
				}
			}
		}
		if (matched)
			ids[(*count)++] = i;
	}
	free(key);
	*indexes = ids;
	return 0;
}
