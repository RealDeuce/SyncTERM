/* Round-trip-preserving, locale-independent INI documents. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ini.h"

#include "array_list.h"
#include "file_map.h"
#include "io_error.h"
#include "st_hex.h"
#include "st_unicode_compare.h"
#include "st_unicode_text.h"
#include "string_compat.h"
#include "string_util.h"
#include "value_convert.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(SIZE_MAX <= UINT64_MAX,
    "INI enum values require size_t to fit in uint64_t");

enum ini_record_kind {
	INI_RECORD_TRIVIA,
	INI_RECORD_SECTION,
	INI_RECORD_KEY,
};

struct ini_record {
	enum ini_record_kind kind;
	char *raw;
	size_t separator;
	size_t value_begin;
	size_t value_end;
	bool literal;
};

struct ini_section {
	struct ini_document *document;
	char *name;
	struct ini_record *header;
	name_value_list_t entries;
	name_value_index_t index;
	struct ini_record **entry_records;
};

static int ini_section_compare(const struct ini_section *left,
    const struct ini_section *right);
static bool valid_name(const char *name, const char *forbidden);

ARRAY_LIST_DECLARE(ini_record_list, struct ini_record)
ARRAY_LIST_DECLARE(ini_section_list, struct ini_section)
SORTED_ARRAY_LIST_DECLARE(ini_section_index, struct ini_section,
    ini_section_compare)

ARRAY_LIST_DEFINE(ini_record_list, struct ini_record)
ARRAY_LIST_DEFINE(ini_section_list, struct ini_section)
SORTED_ARRAY_LIST_DEFINE(ini_section_index, struct ini_section,
    ini_section_compare)

struct ini_document {
	struct ini_section root;
	ini_record_list_t records;
	ini_section_list_t sections;
	ini_section_index_t index;
	char newline[3];
};

const struct ini_write_style ini_default_style = {
	0, "", "", "=", ":", "|"
};

static int
ini_section_compare(const struct ini_section *left,
    const struct ini_section *right)
{
	int compared;

	return st_unicode_caseless_compare(&compared, left->name,
	    strlen(left->name), right->name, strlen(right->name)) == 0
	    ? compared : strcmp(left->name, right->name);
}

static bool
text_equal(const char *left, const char *right)
{
	bool equal;

	return st_unicode_caseless_equal(&equal, left, strlen(left), right,
	    strlen(right)) == 0 && equal;
}

static void
record_free(struct ini_record *record)
{
	if (record == NULL)
		return;
	free(record->raw);
	free(record);
}

static struct ini_record *
record_create_span(enum ini_record_kind kind, const char *raw, size_t length)
{
	struct ini_record *record = malloc(sizeof(*record));

	if (record == NULL)
		return NULL;
	record->raw = strndup(raw, length);
	if (record->raw == NULL) {
		free(record);
		return NULL;
	}
	record->kind = kind;
	record->separator = 0;
	record->value_begin = 0;
	record->value_end = 0;
	record->literal = false;
	return record;
}

static struct ini_record *
record_create(enum ini_record_kind kind, const char *raw)
{
	return record_create_span(kind, raw, strlen(raw));
}

static void
section_release(struct ini_section *section)
{
	if (section == NULL)
		return;
	name_value_index_free_storage(&section->index);
	ini_record_list_free_storage(&section->entry_records);
	name_value_list_free(&section->entries);
	free(section->name);
}

static void
section_free(struct ini_section *section)
{
	if (section == NULL)
		return;
	section_release(section);
	free(section);
}

int
ini_document_create(ini_document_t **result)
{
	ini_document_t *document;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	document = calloc(1, sizeof(*document));
	if (document == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	document->root.document = document;
	document->newline[0] = '\n';
	document->newline[1] = '\0';
	ini_document_free(result);
	*result = document;
	return 0;
}

int
ini_document_clone(ini_document_t **result, const ini_document_t *source)
{
	char *serialized = NULL;
	size_t size = 0;
	int status;

	if (result == NULL || source == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = ini_document_serialize(source, &serialized, &size);
	if (status == 0)
		status = ini_document_parse(result, serialized, size);
	free(serialized);
	return status;
}

void
ini_document_free(ini_document_t **document)
{
	if (document == NULL || *document == NULL)
		return;
	for (size_t i = 0; i < ini_record_list_count((*document)->records); i++)
		record_free((*document)->records[i]);
	ini_record_list_free_storage(&(*document)->records);
	for (size_t i = 0; i < ini_section_list_count((*document)->sections); i++)
		section_free((*document)->sections[i]);
	ini_section_list_free_storage(&(*document)->sections);
	ini_section_index_free_storage(&(*document)->index);
	section_release(&(*document)->root);
	free(*document);
	*document = NULL;
}

static size_t
content_length(const char *line, size_t length)
{
	if (length > 0 && line[length - 1] == '\n') {
		length--;
		if (length > 0 && line[length - 1] == '\r')
			length--;
	}
	return length;
}

static char *
duplicate_span(const char *string, size_t begin, size_t end)
{
	return strndup(string + begin, end - begin);
}

static int
decode_literal(const char *string, size_t length, char **result)
{
	char *decoded = malloc(length + 1);
	size_t output = 0;

	if (decoded == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (size_t i = 0; i < length; i++) {
		unsigned char value = (unsigned char)string[i];

		if (value != '\\') {
			decoded[output++] = (char)value;
			continue;
		}
		if (++i >= length)
			goto invalid;
		switch (string[i]) {
		case '\\': value = '\\'; break;
		case '"': value = '"'; break;
		case 'n': value = '\n'; break;
		case 'r': value = '\r'; break;
		case 't': value = '\t'; break;
		case 'v': value = '\v'; break;
		case 'f': value = '\f'; break;
		case 'x': {
			uint8_t high;
			uint8_t low;

			if (i + 2 >= length
			    || !st_hex_digit_value((unsigned char)string[i + 1],
			    &high)
			    || !st_hex_digit_value((unsigned char)string[i + 2],
			    &low))
				goto invalid;
			value = (unsigned char)(high * 16U + low);
			i += 2;
			if (value == 0)
				goto invalid;
			break;
		}
		default: goto invalid;
		}
		decoded[output++] = (char)value;
	}
	decoded[output] = '\0';
	*result = decoded;
	return 0;

invalid:
	free(decoded);
	return IO_ERROR_INVALID_ARGUMENT;
}

static int
append_record(struct ini_document *document, struct ini_record *record)
{
	if (!ini_record_list_append(&document->records, record))
		return IO_ERROR_OUT_OF_MEMORY;
	return 0;
}

static int
append_section(struct ini_document *document, char *name,
    struct ini_record *header, struct ini_section **result)
{
	struct ini_section *section = calloc(1, sizeof(*section));

	if (section == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	section->document = document;
	section->name = name;
	section->header = header;
	if (!ini_section_list_reserve(&document->sections,
	    ini_section_list_count(document->sections) + 1)
	    || !ini_section_index_reserve(&document->index,
	    ini_section_index_count(document->index) + 1)) {
		free(section);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	(void)ini_section_list_append(&document->sections, section);
	(void)ini_section_index_insert_sorted(&document->index, section);
	*result = section;
	return 0;
}

static int
append_key(struct ini_document *document, struct ini_section *section,
    struct ini_record *record, char *name, char *value)
{
	struct name_value *item;
	size_t count = name_value_list_count(section->entries);

	item = name_value_create(name, value);
	if (item == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (!name_value_array_reserve(&section->entries, count + 1)
	    || !name_value_index_reserve(&section->index, count + 1)
	    || !ini_record_list_reserve(&section->entry_records, count + 1)
	    || !ini_record_list_reserve(&document->records,
	    ini_record_list_count(document->records) + 1)) {
		name_value_free_item(item);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	(void)name_value_array_append(&section->entries, item);
	(void)name_value_index_insert_sorted(&section->index, item);
	(void)ini_record_list_append(&section->entry_records, record);
	(void)ini_record_list_append(&document->records, record);
	return 0;
}

static int
parse_line(struct ini_document *document, struct ini_section **current,
    const char *line, size_t raw_length)
{
	size_t length = content_length(line, raw_length);
	size_t begin;
	size_t end;
	struct ini_record *record;

	string_ascii_trim_span(line, length, &begin, &end);
	if (begin == end || line[begin] == ';') {
		record = record_create_span(INI_RECORD_TRIVIA, line, raw_length);
		if (record == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		if (append_record(document, record) != 0) {
			record_free(record);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		return 0;
	}
	if (line[begin] == '[' && end > begin + 2 && line[end - 1] == ']') {
		size_t name_begin;
		size_t name_end;
		char *name;
		int error;

		string_ascii_trim_span(line + begin + 1, end - begin - 2,
		    &name_begin, &name_end);
		name_begin += begin + 1;
		name_end += begin + 1;
		name = duplicate_span(line, name_begin, name_end);
		if (name == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		if (!valid_name(name, "[]")) {
			free(name);
			*current = NULL;
			goto trivia;
		}
		record = record_create_span(INI_RECORD_SECTION, line, raw_length);
		if (record == NULL) {
			free(name);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		if (!ini_record_list_reserve(&document->records,
		    ini_record_list_count(document->records) + 1)) {
			record_free(record);
			free(name);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		error = append_section(document, name, record, current);
		if (error != 0) {
			record_free(record);
			free(name);
			return error;
		}
		(void)ini_record_list_append(&document->records, record);
		return 0;
	}
	if (line[begin] == '[') {
		*current = NULL;
		goto trivia;
	}
	for (size_t separator = begin; separator < end; separator++) {
		if (line[separator] == '=' || line[separator] == ':') {
			size_t name_begin;
			size_t name_end;
			size_t value_begin;
			size_t value_end;
			char *name;
			char *value;

			if (*current == NULL)
				goto trivia;
			string_ascii_trim_span(line + begin, separator - begin,
			    &name_begin, &name_end);
			name_begin += begin;
			name_end += begin;
			string_ascii_trim_span(line + separator + 1,
			    end - separator - 1, &value_begin, &value_end);
			value_begin += separator + 1;
			value_end += separator + 1;
			if (line[separator] == ':') {
				if (value_end - value_begin < 2
				    || line[value_begin] != '"'
				    || line[value_end - 1] != '"')
					goto trivia;
				int decode_error = decode_literal(line + value_begin + 1,
				    value_end - value_begin - 2, &value);
				if (decode_error == IO_ERROR_OUT_OF_MEMORY)
					return decode_error;
				if (decode_error != 0)
					goto trivia;
			} else {
				value = duplicate_span(line, value_begin, value_end);
				if (value == NULL)
					return IO_ERROR_OUT_OF_MEMORY;
			}
			{
				char *normalized;
				size_t normalized_size;
				int normalize_error = st_unicode_text_normalize(
				    &normalized, &normalized_size, value, strlen(value),
				    NULL);

				if (normalize_error != 0) {
					free(value);
					return normalize_error;
				}
				free(value);
				value = normalized;
			}
			name = duplicate_span(line, name_begin, name_end);
			if (name == NULL) {
				free(value);
				return IO_ERROR_OUT_OF_MEMORY;
			}
			if (!ini_key_name_valid(name)) {
				free(name);
				free(value);
				goto trivia;
			}
			record = record_create_span(INI_RECORD_KEY, line, raw_length);
			if (record == NULL) {
				free(name);
				free(value);
				return IO_ERROR_OUT_OF_MEMORY;
			}
			record->separator = separator;
			record->value_begin = value_begin;
			record->value_end = value_end;
			record->literal = line[separator] == ':';
			if (append_key(document, *current, record, name, value) != 0) {
				record_free(record);
				free(name);
				free(value);
				return IO_ERROR_OUT_OF_MEMORY;
			}
			free(name);
			free(value);
			return 0;
		}
	}

trivia:
	record = record_create_span(INI_RECORD_TRIVIA, line, raw_length);
	if (record == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (append_record(document, record) != 0) {
		record_free(record);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	return 0;
}

int
ini_document_parse(ini_document_t **result, const void *input, size_t size)
{
	ini_document_t *document = NULL;
	struct ini_section *current;
	const char *data = input;
	size_t offset = 0;
	bool saw_newline = false;
	int error = 0;

	if (result == NULL || (input == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	error = ini_document_create(&document);
	if (error != 0)
		return error;
	current = &document->root;
	while (offset < size) {
		const char *line = data + offset;
		const char *newline = memchr(line, '\n', size - offset);
		size_t length = newline == NULL ? size - offset
		    : (size_t)(newline - line) + 1;
		char *normalized;
		size_t normalized_length;

		if (memchr(line, '\0', length) != NULL) {
			error = IO_ERROR_INVALID_ENCODING;
			break;
		}
		error = st_unicode_text_normalize(&normalized, &normalized_length,
		    line, length, NULL);
		if (error != 0)
			break;
		if (!saw_newline) {
			if (normalized_length > 0
			    && normalized[normalized_length - 1] == '\n') {
				if (normalized_length > 1
				    && normalized[normalized_length - 2] == '\r')
					memcpy(document->newline, "\r\n", 3);
				else
					memcpy(document->newline, "\n", 2);
				saw_newline = true;
			}
		}
		error = parse_line(document, &current, normalized,
		    normalized_length);
		free(normalized);
		if (error != 0)
			break;
		offset += length;
	}
	if (error != 0) {
		ini_document_free(&document);
		return error;
	}
	ini_document_free(result);
	*result = document;
	return 0;
}

int
ini_document_read(ini_document_t **result, FILE *stream)
{
	file_map_t *map = NULL;
	int error;

	if (result == NULL || stream == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	error = file_map_open_stream(&map, stream);
	if (error != 0)
		return error;
	error = ini_document_parse(result, file_map_data(map), file_map_size(map));
	file_map_close(map);
	return error;
}

int
ini_document_serialize(const ini_document_t *document, char **result,
    size_t *size)
{
	char *serialized;
	size_t total = 0;
	size_t offset = 0;

	if (document == NULL || result == NULL || size == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < ini_record_list_count(document->records); i++) {
		size_t length = strlen(document->records[i]->raw);

		if (length > SIZE_MAX - total)
			return IO_ERROR_OUT_OF_RANGE;
		total += length;
	}
	if (total == SIZE_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	serialized = malloc(total + 1);
	if (serialized == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (size_t i = 0; i < ini_record_list_count(document->records); i++) {
		size_t length = strlen(document->records[i]->raw);

		memcpy(serialized + offset, document->records[i]->raw, length);
		offset += length;
	}
	serialized[total] = '\0';
	free(*result);
	*result = serialized;
	*size = total;
	return 0;
}

int
ini_document_write(const ini_document_t *document, FILE *stream)
{
	if (document == NULL || stream == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < ini_record_list_count(document->records); i++) {
		const char *raw = document->records[i]->raw;
		size_t length = strlen(raw);

		if (length != 0 && fwrite(raw, 1, length, stream) != length)
			return IO_ERROR_IO;
	}
	return ferror(stream) ? IO_ERROR_IO : 0;
}

ini_section_t *
ini_document_root(ini_document_t *document)
{
	return document == NULL ? NULL : &document->root;
}

size_t
ini_document_section_count(const ini_document_t *document)
{
	return document == NULL ? 0 : ini_section_list_count(document->sections);
}

ini_section_t *
ini_document_section_at(ini_document_t *document, size_t index)
{
	if (document == NULL || index >= ini_document_section_count(document))
		return NULL;
	return document->sections[index];
}

ini_section_t *
ini_document_find_section(ini_document_t *document, const char *name)
{
	struct ini_section key = {0};
	size_t index;

	if (document == NULL || name == NULL)
		return NULL;
	key.name = (char *)name;
	if (!ini_section_index_find(document->index, &key, &index))
		return NULL;
	while (index + 1 < ini_section_index_count(document->index)
	    && ini_section_compare(document->index[index + 1], &key) == 0)
		index++;
	return document->index[index];
}

const char *
ini_section_name(const ini_section_t *section)
{
	return section == NULL ? NULL : section->name;
}

size_t
ini_section_count(const ini_section_t *section)
{
	return section == NULL ? 0 : name_value_list_count(section->entries);
}

const struct name_value *
ini_section_at(const ini_section_t *section, size_t index)
{
	if (section == NULL || index >= ini_section_count(section))
		return NULL;
	return section->entries[index];
}

static size_t
section_last_index(const ini_section_t *section, const char *key,
    bool *found)
{
	struct name_value search = {key, ""};
	size_t index = 0;

	*found = section != NULL && key != NULL
	    && name_value_index_find(section->index, &search, &index);
	if (!*found)
		return index;
	while (index + 1 < name_value_index_count(section->index)
	    && name_value_compare(section->index[index + 1], &search) == 0)
		index++;
	return index;
}

static struct name_value *
section_find_item(const ini_section_t *section, const char *key)
{
	bool found;
	size_t index = section_last_index(section, key, &found);

	return found ? section->index[index] : NULL;
}

const char *
ini_section_find(const ini_section_t *section, const char *key)
{
	struct name_value *item = section_find_item(section, key);

	return item == NULL ? NULL : item->value;
}

bool
ini_section_has_key(const ini_section_t *section, const char *key)
{
	return section_find_item(section, key) != NULL;
}

int
ini_section_copy_pairs(const ini_section_t *section, name_value_list_t *result)
{
	name_value_list_t copy = NULL;

	if (section == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < ini_section_count(section); i++) {
		if (!name_value_list_append(&copy, section->entries[i]->name,
		    section->entries[i]->value)) {
			name_value_list_free(&copy);
			return IO_ERROR_OUT_OF_MEMORY;
		}
	}
	name_value_list_free(result);
	*result = copy;
	return 0;
}

const char *
ini_section_get_string(const ini_section_t *section, const char *key,
    const char *default_value)
{
	const char *value = ini_section_find(section, key);

	return value == NULL || *value == '\0' ? default_value : value;
}

int64_t
ini_section_get_int64(const ini_section_t *section, const char *key,
    int64_t default_value)
{
	int64_t value;
	const char *text = ini_section_find(section, key);

	return text != NULL && value_parse_int64(text, &value) ? value : default_value;
}

int64_t
ini_section_get_bounded_int64(const ini_section_t *section, const char *key,
	int64_t default_value, int64_t minimum, int64_t maximum)
{
	int64_t value;

	if (minimum > maximum || default_value < minimum
	    || default_value > maximum)
		return default_value;
	value = ini_section_get_int64(section, key, default_value);
	return value < minimum || value > maximum ? default_value : value;
}

uint64_t
ini_section_get_uint64(const ini_section_t *section, const char *key,
    uint64_t default_value)
{
	uint64_t value;
	const char *text = ini_section_find(section, key);

	return text != NULL && value_parse_uint64(text, &value) ? value : default_value;
}

double
ini_section_get_double(const ini_section_t *section, const char *key,
    double default_value)
{
	double value;
	const char *text = ini_section_find(section, key);

	return text != NULL && value_parse_double(text, &value) ? value : default_value;
}

bool
ini_section_get_bool(const ini_section_t *section, const char *key,
    bool default_value)
{
	bool value;
	const char *text = ini_section_find(section, key);

	return text != NULL && value_parse_bool(text, &value) ? value : default_value;
}

double
ini_section_get_duration(const ini_section_t *section, const char *key,
    double default_value)
{
	double value;
	const char *text = ini_section_find(section, key);

	return text != NULL && value_parse_duration(text, &value)
	    ? value : default_value;
}

time_t
ini_section_get_datetime(const ini_section_t *section, const char *key,
    time_t default_value)
{
	time_t value;
	const char *text = ini_section_find(section, key);

	return text != NULL && value_parse_datetime(text, &value)
	    ? value : default_value;
}

size_t
ini_section_get_enum_names(const ini_section_t *section, const char *key,
    const char *const *names, size_t name_count, size_t default_value)
{
	const char *text = ini_section_find(section, key);
	uint64_t numeric;

	if (text == NULL)
		return default_value;
	for (size_t i = 0; i < name_count; i++) {
		if (text_equal(text, names[i]))
			return i;
	}
	if (!value_parse_uint64(text, &numeric) || numeric > SIZE_MAX)
		return default_value;
	return (size_t)numeric;
}

size_t
ini_section_get_enum(const ini_section_t *section, const char *key,
    str_list_t names, size_t default_value)
{
	return ini_section_get_enum_names(section, key,
	    (const char *const *)names, str_list_count(names), default_value);
}

int
ini_section_get_string_list(const ini_section_t *section, const char *key,
    const char *delimiters, str_list_t *result)
{
	const char *value;

	if (section == NULL || key == NULL || delimiters == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	value = ini_section_find(section, key);
	if (value == NULL) {
		str_list_free(result);
		return 0;
	}
	return str_list_split(result, value, delimiters)
	    ? 0 : IO_ERROR_OUT_OF_MEMORY;
}

void
ini_free_int64_list(int64_t *values)
{
	free(values);
}

int
ini_section_get_int64_list(const ini_section_t *section, const char *key,
    const char *delimiters, int64_t **result, size_t *count)
{
	str_list_t fields = NULL;
	int64_t *values = NULL;
	size_t field_count;
	int error;

	if (result == NULL || count == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	error = ini_section_get_string_list(section, key, delimiters, &fields);
	if (error != 0)
		return error;
	field_count = str_list_count(fields);
	if (field_count > SIZE_MAX / sizeof(*values)) {
		str_list_free(&fields);
		return IO_ERROR_OUT_OF_RANGE;
	}
	if (field_count != 0) {
		values = malloc(field_count * sizeof(*values));
		if (values == NULL) {
			str_list_free(&fields);
			return IO_ERROR_OUT_OF_MEMORY;
		}
	}
	for (size_t i = 0; i < field_count; i++) {
		if (!value_parse_int64(fields[i], &values[i])) {
			free(values);
			str_list_free(&fields);
			return IO_ERROR_INVALID_ARGUMENT;
		}
	}
	str_list_free(&fields);
	free(*result);
	*result = values;
	*count = field_count;
	return 0;
}

uint64_t
ini_section_get_bitfield(const ini_section_t *section, const char *key,
    const struct ini_bit_name *names, size_t name_count,
    uint64_t default_value)
{
	str_list_t fields = NULL;
	uint64_t result = 0;
	const char *text = ini_section_find(section, key);

	if (text == NULL || (name_count != 0 && names == NULL)
	    || !str_list_split(&fields, text, "|,"))
		return default_value;
	for (size_t i = 0; i < str_list_count(fields); i++) {
		char *field = string_trim_ascii(fields[i]);
		uint64_t numeric;
		bool matched = false;

		for (size_t n = 0; n < name_count; n++) {
			if (text_equal(field, names[n].name)) {
				result |= names[n].bit;
				matched = true;
				break;
			}
		}
		if (!matched) {
			if (!value_parse_uint64(field, &numeric)) {
				str_list_free(&fields);
				return default_value;
			}
			result |= numeric;
		}
	}
	str_list_free(&fields);
	return result;
}

static bool
valid_name(const char *name, const char *forbidden)
{
	return st_unicode_name_valid(name, 1024, 2048, forbidden);
}

bool
ini_key_name_valid(const char *name)
{
	return valid_name(name, "=:") && name[0] != ';' && name[0] != '[';
}

static bool
value_needs_literal(const char *value)
{
	size_t length = strlen(value);

	if (length > 0 && (ascii_is_space((unsigned char)value[0])
	    || ascii_is_space((unsigned char)value[length - 1])))
		return true;
	for (size_t i = 0; i < length; i++) {
		unsigned char character = (unsigned char)value[i];

		if (character < 0x20 || character == 0x7f)
			return true;
	}
	return false;
}

static char *
encode_literal(const char *value)
{
	size_t length = strlen(value);
	size_t capacity;
	char *encoded;
	size_t output = 0;
	static const char hex[] = "0123456789abcdef";

	if (length > (SIZE_MAX - 3) / 4)
		return NULL;
	capacity = length * 4 + 3;
	encoded = malloc(capacity);
	if (encoded == NULL)
		return NULL;
	encoded[output++] = '"';
	for (size_t i = 0; i < length; i++) {
		unsigned char character = (unsigned char)value[i];

		switch (character) {
		case '\\': encoded[output++] = '\\'; encoded[output++] = '\\'; break;
		case '"': encoded[output++] = '\\'; encoded[output++] = '"'; break;
		case '\n': encoded[output++] = '\\'; encoded[output++] = 'n'; break;
		case '\r': encoded[output++] = '\\'; encoded[output++] = 'r'; break;
		case '\t': encoded[output++] = '\\'; encoded[output++] = 't'; break;
		case '\v': encoded[output++] = '\\'; encoded[output++] = 'v'; break;
		case '\f': encoded[output++] = '\\'; encoded[output++] = 'f'; break;
		default:
			if (character < 0x20 || character == 0x7f) {
				encoded[output++] = '\\';
				encoded[output++] = 'x';
				encoded[output++] = hex[character >> 4];
				encoded[output++] = hex[character & 15];
			} else
				encoded[output++] = (char)character;
		}
	}
	encoded[output++] = '"';
	encoded[output] = '\0';
	return encoded;
}

static size_t
record_index(const struct ini_document *document,
    const struct ini_record *record)
{
	for (size_t i = 0; i < ini_record_list_count(document->records); i++) {
		if (document->records[i] == record)
			return i;
	}
	return SIZE_MAX;
}

static size_t
section_source_index(const struct ini_section *section,
    const struct name_value *item)
{
	for (size_t i = ini_section_count(section); i > 0; i--) {
		if (section->entries[i - 1] == item)
			return i - 1;
	}
	return SIZE_MAX;
}

static void
remove_index_item(name_value_index_t index, const struct name_value *item)
{
	for (size_t i = 0; i < name_value_index_count(index); i++) {
		if (index[i] == item) {
			(void)name_value_index_take(index, i);
			return;
		}
	}
}

static int
replace_record_value(struct ini_record *record, const char *value,
    bool literal, const struct ini_write_style *style)
{
	char *encoded = literal ? encode_literal(value) : strdup(value);
	const char *separator = literal ? style->literal_separator
	    : style->value_separator;
	size_t separator_length;
	size_t whitespace_length;
	size_t encoded_length;
	size_t suffix_length;
	size_t total;
	char *raw;

	if (encoded == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	separator_length = strlen(separator);
	whitespace_length = record->value_begin - record->separator - 1;
	encoded_length = strlen(encoded);
	suffix_length = strlen(record->raw + record->value_end);
	if (record->separator > SIZE_MAX - separator_length
	    || record->separator + separator_length > SIZE_MAX - whitespace_length
	    || record->separator + separator_length + whitespace_length
	    > SIZE_MAX - encoded_length
	    || record->separator + separator_length + whitespace_length
	    + encoded_length > SIZE_MAX - suffix_length - 1) {
		free(encoded);
		return IO_ERROR_OUT_OF_RANGE;
	}
	total = record->separator + separator_length + whitespace_length
	    + encoded_length + suffix_length;
	raw = malloc(total + 1);
	if (raw == NULL) {
		free(encoded);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	memcpy(raw, record->raw, record->separator);
	memcpy(raw + record->separator, separator, separator_length);
	memcpy(raw + record->separator + separator_length,
	    record->raw + record->separator + 1, whitespace_length);
	memcpy(raw + record->separator + separator_length + whitespace_length,
	    encoded, encoded_length);
	memcpy(raw + record->separator + separator_length + whitespace_length
	    + encoded_length, record->raw + record->value_end, suffix_length + 1);
	free(encoded);
	free(record->raw);
	record->raw = raw;
	record->value_begin = record->separator + separator_length
	    + whitespace_length;
	record->value_end = record->value_begin + encoded_length;
	record->literal = literal;
	return 0;
}

static struct ini_record *
new_key_record(const struct ini_section *section, const char *key,
    const char *value, bool literal, const struct ini_write_style *style)
{
	const char *separator = literal ? style->literal_separator
	    : style->value_separator;
	char *encoded = literal ? encode_literal(value) : strdup(value);
	size_t prefix_length = strlen(style->key_prefix);
	size_t key_length = strlen(key);
	size_t padding = style->key_width > key_length
	    ? style->key_width - key_length : 0;
	size_t separator_length = strlen(separator);
	size_t encoded_length;
	size_t newline_length = strlen(section->document->newline);
	size_t total;
	char *raw;
	struct ini_record *record;
	size_t cursor = 0;

	if (encoded == NULL)
		return NULL;
	encoded_length = strlen(encoded);
	if (prefix_length > SIZE_MAX - key_length
	    || prefix_length + key_length > SIZE_MAX - padding
	    || prefix_length + key_length + padding > SIZE_MAX - separator_length
	    || prefix_length + key_length + padding + separator_length
	    > SIZE_MAX - encoded_length
	    || prefix_length + key_length + padding + separator_length
	    + encoded_length > SIZE_MAX - newline_length - 1) {
		free(encoded);
		return NULL;
	}
	total = prefix_length + key_length + padding + separator_length
	    + encoded_length + newline_length;
	raw = malloc(total + 1);
	if (raw == NULL) {
		free(encoded);
		return NULL;
	}
	memcpy(raw + cursor, style->key_prefix, prefix_length);
	cursor += prefix_length;
	memcpy(raw + cursor, key, key_length);
	cursor += key_length;
	memset(raw + cursor, ' ', padding);
	cursor += padding;
	memcpy(raw + cursor, separator, separator_length);
	cursor += separator_length;
	memcpy(raw + cursor, encoded, encoded_length);
	cursor += encoded_length;
	memcpy(raw + cursor, section->document->newline, newline_length);
	cursor += newline_length;
	raw[cursor] = '\0';
	free(encoded);
	record = record_create(INI_RECORD_KEY, raw);
	free(raw);
	if (record == NULL)
		return NULL;
	record->separator = prefix_length + key_length + padding;
	record->value_begin = record->separator + separator_length;
	record->value_end = record->value_begin + encoded_length;
	record->literal = literal;
	return record;
}

static size_t
key_record_insertion_index(const struct ini_section *section)
{
	const struct ini_document *document = section->document;
	size_t position = ini_record_list_count(document->records);

	if (section->name == NULL) {
		for (size_t i = 0; i < ini_record_list_count(document->records); i++) {
			if (document->records[i]->kind == INI_RECORD_SECTION) {
				position = i;
				break;
			}
		}
	} else {
		size_t header = record_index(document, section->header);

		for (size_t i = header + 1;
		    i < ini_record_list_count(document->records); i++) {
			if (document->records[i]->kind == INI_RECORD_SECTION) {
				position = i;
				break;
			}
		}
	}
	while (position > 0
	    && document->records[position - 1]->kind == INI_RECORD_TRIVIA)
		position--;
	return position;
}

static int
prepare_raw_termination(const struct ini_record *record,
    const char *document_newline, char **replacement)
{
	const char *newline;
	size_t length;
	size_t newline_length;
	char *raw;

	*replacement = NULL;
	length = strlen(record->raw);
	if (length > 0 && record->raw[length - 1] == '\n')
		return 0;
	newline = document_newline;
	newline_length = strlen(newline);
	if (length > 0 && record->raw[length - 1] == '\r') {
		newline = "\n";
		newline_length = 1;
	}
	if (length > SIZE_MAX - newline_length - 1)
		return IO_ERROR_OUT_OF_RANGE;
	raw = malloc(length + newline_length + 1);
	if (raw == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	memcpy(raw, record->raw, length);
	memcpy(raw + length, newline, newline_length + 1);
	*replacement = raw;
	return 0;
}

static int
prepare_record_termination(const struct ini_document *document,
    size_t insertion, char **replacement)
{
	*replacement = NULL;
	if (insertion == 0)
		return 0;
	return prepare_raw_termination(document->records[insertion - 1],
	    document->newline, replacement);
}

static void
commit_record_termination(struct ini_document *document, size_t insertion,
    char *replacement)
{
	struct ini_record *record;

	if (replacement == NULL)
		return;
	record = document->records[insertion - 1];
	free(record->raw);
	record->raw = replacement;
}

int
ini_section_set_string(ini_section_t *section, const char *key,
    const char *value, const struct ini_write_style *style)
{
	struct name_value *item;
	char *normalized;
	size_t normalized_size;
	int normalize_error;
	bool literal;

	if (section == NULL || !ini_key_name_valid(key)
	    || value == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	normalize_error = st_unicode_text_normalize(&normalized,
	    &normalized_size, value, strlen(value), NULL);
	if (normalize_error != 0)
		return normalize_error;
	value = normalized;
	if (style == NULL)
		style = &ini_default_style;
	if (style->key_prefix == NULL || style->value_separator == NULL
	    || *style->value_separator == '\0' || style->literal_separator == NULL
	    || *style->literal_separator == '\0'
	    || style->value_separator[1] != '\0'
	    || style->literal_separator[1] != '\0')
		goto invalid;
	literal = value_needs_literal(value);
	item = section_find_item(section, key);
	if (item != NULL) {
		size_t source_index = section_source_index(section, item);
		struct ini_record *record;
		char *copy;
		int error;

		if (source_index == SIZE_MAX)
			goto io_error;
		record = section->entry_records[source_index];
		copy = strdup(value);
		if (copy == NULL)
			goto out_of_memory;
		error = replace_record_value(record, value, literal, style);
		if (error != 0) {
			free(copy);
			free(normalized);
			return error;
		}
		free((char *)item->value);
		item->value = copy;
		free(normalized);
		return 0;
	}
	{
		struct ini_record *record = new_key_record(section, key, value,
		    literal, style);
		struct name_value *new_item;
		char *terminated_raw = NULL;
		size_t count = ini_section_count(section);
		size_t record_count = ini_record_list_count(section->document->records);
		size_t insertion = key_record_insertion_index(section);
		int error;

		if (record == NULL)
			goto out_of_memory;
		error = prepare_record_termination(section->document, insertion,
		    &terminated_raw);
		if (error != 0) {
			record_free(record);
			free(normalized);
			return error;
		}
		new_item = name_value_create(key, value);
		if (new_item == NULL) {
			free(terminated_raw);
			record_free(record);
			goto out_of_memory;
		}
		if (!name_value_array_reserve(&section->entries, count + 1)
		    || !name_value_index_reserve(&section->index, count + 1)
		    || !ini_record_list_reserve(&section->entry_records, count + 1)
		    || !ini_record_list_reserve(&section->document->records,
		    record_count + 1)) {
			name_value_free_item(new_item);
			free(terminated_raw);
			record_free(record);
			goto out_of_memory;
		}
		commit_record_termination(section->document, insertion,
		    terminated_raw);
		(void)name_value_array_append(&section->entries, new_item);
		(void)name_value_index_insert_sorted(&section->index, new_item);
		(void)ini_record_list_append(&section->entry_records, record);
		(void)ini_record_list_insert(&section->document->records, insertion,
		    record);
	}
	free(normalized);
	return 0;

invalid:
	free(normalized);
	return IO_ERROR_INVALID_ARGUMENT;
io_error:
	free(normalized);
	return IO_ERROR_IO;
out_of_memory:
	free(normalized);
	return IO_ERROR_OUT_OF_MEMORY;
}

int
ini_section_set_int64(ini_section_t *section, const char *key, int64_t value,
    const struct ini_write_style *style)
{
	char buffer[32];

	return value_format_int64(buffer, sizeof(buffer), value)
	    ? ini_section_set_string(section, key, buffer, style)
	    : IO_ERROR_OUT_OF_RANGE;
}

int
ini_section_set_uint64(ini_section_t *section, const char *key,
    uint64_t value, const struct ini_write_style *style)
{
	char buffer[32];

	return value_format_uint64(buffer, sizeof(buffer), value)
	    ? ini_section_set_string(section, key, buffer, style)
	    : IO_ERROR_OUT_OF_RANGE;
}

int
ini_section_set_double(ini_section_t *section, const char *key, double value,
    const struct ini_write_style *style)
{
	char buffer[64];

	return value_format_double(buffer, sizeof(buffer), value)
	    ? ini_section_set_string(section, key, buffer, style)
	    : IO_ERROR_OUT_OF_RANGE;
}

int
ini_section_set_bool(ini_section_t *section, const char *key, bool value,
    const struct ini_write_style *style)
{
	return ini_section_set_string(section, key, value ? "true" : "false",
	    style);
}

int
ini_section_set_duration(ini_section_t *section, const char *key,
    double seconds, const struct ini_write_style *style)
{
	char buffer[64];

	return value_format_duration(buffer, sizeof(buffer), seconds)
	    ? ini_section_set_string(section, key, buffer, style)
	    : IO_ERROR_OUT_OF_RANGE;
}

int
ini_section_set_datetime(ini_section_t *section, const char *key,
    time_t value, bool include_time, const struct ini_write_style *style)
{
	char buffer[32];

	return value_format_datetime(buffer, sizeof(buffer), value, include_time)
	    ? ini_section_set_string(section, key, buffer, style)
	    : IO_ERROR_OUT_OF_RANGE;
}

int
ini_section_set_enum_names(ini_section_t *section, const char *key,
    const char *const *names, size_t name_count, size_t value,
    const struct ini_write_style *style)
{
	if (value < name_count)
		return ini_section_set_string(section, key, names[value], style);
	return ini_section_set_uint64(section, key, (uint64_t)value, style);
}

int
ini_section_set_enum(ini_section_t *section, const char *key,
    str_list_t names, size_t value, const struct ini_write_style *style)
{
	return ini_section_set_enum_names(section, key,
	    (const char *const *)names, str_list_count(names), value, style);
}

int
ini_section_set_string_list(ini_section_t *section, const char *key,
    str_list_t values, const char *separator,
    const struct ini_write_style *style)
{
	char *joined;
	int error;

	if (separator == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	joined = str_list_join(values, separator);
	if (joined == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	error = ini_section_set_string(section, key, joined, style);
	str_list_free_item(joined);
	return error;
}

int
ini_section_set_int64_list(ini_section_t *section, const char *key,
    const int64_t *values, size_t count, const char *separator,
    const struct ini_write_style *style)
{
	str_list_t strings = NULL;
	char buffer[32];
	int error;

	if ((count != 0 && values == NULL) || separator == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < count; i++) {
		if (!value_format_int64(buffer, sizeof(buffer), values[i])
		    || !str_list_append(&strings, buffer)) {
			str_list_free(&strings);
			return IO_ERROR_OUT_OF_MEMORY;
		}
	}
	error = ini_section_set_string_list(section, key, strings, separator,
	    style);
	str_list_free(&strings);
	return error;
}

int
ini_section_set_bitfield(ini_section_t *section, const char *key,
    const struct ini_bit_name *names, size_t name_count, uint64_t value,
    const struct ini_write_style *style)
{
	str_list_t fields = NULL;
	char numeric[32];
	char *joined;
	const char *separator;
	int error;

	if (section == NULL || key == NULL || (name_count != 0 && names == NULL))
		return IO_ERROR_INVALID_ARGUMENT;
	if (style == NULL)
		style = &ini_default_style;
	separator = style->bit_separator;
	if (separator == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < name_count; i++) {
		if ((value & names[i].bit) != 0) {
			if (!str_list_append(&fields, names[i].name))
				goto memory_error;
			value &= ~names[i].bit;
		}
	}
	if (value != 0) {
		if (!value_format_uint64(numeric, sizeof(numeric), value)
		    || !str_list_append(&fields, numeric))
			goto memory_error;
	}
	joined = str_list_join(fields, separator);
	str_list_free(&fields);
	if (joined == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	error = ini_section_set_string(section, key, joined, style);
	str_list_free_item(joined);
	return error;

memory_error:
	str_list_free(&fields);
	return IO_ERROR_OUT_OF_MEMORY;
}

size_t
ini_section_remove_key(ini_section_t *section, const char *key)
{
	size_t removed = 0;

	if (section == NULL || key == NULL)
		return 0;
	for (size_t i = ini_section_count(section); i > 0; i--) {
		size_t index = i - 1;
		struct name_value *item = section->entries[index];

		if (text_equal(item->name, key)) {
			struct ini_record *record = section->entry_records[index];
			size_t raw_index = record_index(section->document, record);

			remove_index_item(section->index, item);
			(void)ini_record_list_take(section->entry_records, index);
			if (raw_index != SIZE_MAX)
				(void)ini_record_list_take(section->document->records,
				    raw_index);
			name_value_free_item(name_value_array_take(section->entries,
			    index));
			record_free(record);
			removed++;
		}
	}
	return removed;
}

int
ini_document_add_section(ini_document_t *document, const char *name,
    const struct ini_write_style *style, ini_section_t **result)
{
	char *name_copy;
	char *raw;
	struct ini_record *header;
	struct ini_record *separator_record = NULL;
	struct ini_section *section;
	char *terminated_raw = NULL;
	size_t name_length;
	size_t newline_length;
	size_t total;
	int error;

	if (document == NULL || !valid_name(name, "[]"))
		return IO_ERROR_INVALID_ARGUMENT;
	if (style == NULL)
		style = &ini_default_style;
	if (style->section_separator == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	name_copy = strdup(name);
	if (name_copy == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	name_length = strlen(name);
	newline_length = strlen(document->newline);
	if (name_length > SIZE_MAX - newline_length - 3) {
		free(name_copy);
		return IO_ERROR_OUT_OF_RANGE;
	}
	total = name_length + newline_length + 2;
	raw = malloc(total + 1);
	if (raw == NULL) {
		free(name_copy);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	raw[0] = '[';
	memcpy(raw + 1, name, name_length);
	raw[name_length + 1] = ']';
	memcpy(raw + name_length + 2, document->newline, newline_length + 1);
	header = record_create(INI_RECORD_SECTION, raw);
	free(raw);
	if (header == NULL) {
		free(name_copy);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	if (ini_record_list_count(document->records) > 0
	    && *style->section_separator != '\0') {
		const char *separator = style->section_separator;

		if (strcmp(separator, "\n") == 0 || strcmp(separator, "\r\n") == 0)
			separator = document->newline;
		separator_record = record_create(INI_RECORD_TRIVIA,
		    separator);
		if (separator_record == NULL) {
			record_free(header);
			free(name_copy);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		char *separator_raw = NULL;

		error = prepare_raw_termination(separator_record,
		    document->newline, &separator_raw);
		if (error != 0) {
			record_free(separator_record);
			record_free(header);
			free(name_copy);
			return error;
		}
		if (separator_raw != NULL) {
			free(separator_record->raw);
			separator_record->raw = separator_raw;
		}
	}
	error = prepare_record_termination(document,
	    ini_record_list_count(document->records), &terminated_raw);
	if (error != 0) {
		record_free(separator_record);
		record_free(header);
		free(name_copy);
		return error;
	}
	if (!ini_record_list_reserve(&document->records,
	    ini_record_list_count(document->records) + 1
	    + (separator_record != NULL))) {
		free(terminated_raw);
		record_free(separator_record);
		record_free(header);
		free(name_copy);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	error = append_section(document, name_copy, header, &section);
	if (error != 0) {
		free(terminated_raw);
		record_free(separator_record);
		record_free(header);
		free(name_copy);
		return error;
	}
	commit_record_termination(document,
	    ini_record_list_count(document->records), terminated_raw);
	if (separator_record != NULL)
		(void)ini_record_list_append(&document->records, separator_record);
	(void)ini_record_list_append(&document->records, header);
	if (result != NULL)
		*result = section;
	return 0;
}

static void
remove_section_index(struct ini_document *document,
    const struct ini_section *section)
{
	for (size_t i = 0; i < ini_section_index_count(document->index); i++) {
		if (document->index[i] == section) {
			(void)ini_section_index_take(document->index, i);
			break;
		}
	}
	for (size_t i = 0; i < ini_section_list_count(document->sections); i++) {
		if (document->sections[i] == section) {
			(void)ini_section_list_take(document->sections, i);
			break;
		}
	}
}

static void
rebuild_section_index(struct ini_document *document)
{
	ini_section_index_clear(document->index);
	for (size_t i = 0; i < ini_section_list_count(document->sections); i++)
		(void)ini_section_index_insert_sorted(&document->index,
		    document->sections[i]);
}

bool
ini_document_remove_section(ini_document_t *document, ini_section_t *section)
{
	if (document == NULL || section == NULL || section == &document->root
	    || section->document != document)
		return false;
	for (size_t i = 0; i < ini_section_count(section); i++) {
		size_t index = record_index(document, section->entry_records[i]);

		if (index != SIZE_MAX) {
			struct ini_record *record = ini_record_list_take(document->records,
			    index);
			record_free(record);
		}
	}
	{
		size_t index = record_index(document, section->header);

		if (index != SIZE_MAX) {
			struct ini_record *record = ini_record_list_take(document->records,
			    index);
			record_free(record);
		}
	}
	remove_section_index(document, section);
	section->header = NULL;
	section_free(section);
	return true;
}

bool
ini_document_rename_section(ini_document_t *document, ini_section_t *section,
    const char *name)
{
	char *copy;
	char *raw;
	size_t length;
	size_t newline_length;

	if (document == NULL || section == NULL || section == &document->root
	    || section->document != document || !valid_name(name, "[]"))
		return false;
	copy = strdup(name);
	if (copy == NULL)
		return false;
	length = strlen(name);
	newline_length = strlen(document->newline);
	if (length > SIZE_MAX - newline_length - 3) {
		free(copy);
		return false;
	}
	raw = malloc(length + newline_length + 3);
	if (raw == NULL) {
		free(copy);
		return false;
	}
	raw[0] = '[';
	memcpy(raw + 1, name, length);
	raw[length + 1] = ']';
	memcpy(raw + length + 2, document->newline, newline_length + 1);
	for (size_t i = 0; i < ini_section_index_count(document->index); i++) {
		if (document->index[i] == section) {
			(void)ini_section_index_take(document->index, i);
			break;
		}
	}
	free(section->name);
	section->name = copy;
	free(section->header->raw);
	section->header->raw = raw;
	rebuild_section_index(document);
	return true;
}
