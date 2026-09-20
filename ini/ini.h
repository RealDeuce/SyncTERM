/* Round-trip-preserving, locale-independent INI documents. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef SYNCTERM_INI_H_
#define SYNCTERM_INI_H_

#include "name_value_list.h"
#include "str_list.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

typedef struct ini_document ini_document_t;
typedef struct ini_section ini_section_t;

/*
 * Section and key names are nonempty printable US-ASCII, do not begin or end
 * with ASCII whitespace, and compare with ASCII-only case folding. Section
 * names cannot contain '[' or ']'. Key names cannot contain '=' or ':', or
 * begin with ';' or '['. Values are NUL-terminated byte strings with no
 * specified encoding. Returned views and borrowed strings are invalidated by
 * the next document mutation or by freeing the document.
 *
 * Repeated physical sections and keys remain distinct in file order. Name
 * lookup selects the last matching section block and the last matching key in
 * that block.
 */

/* A nonempty section_separator is emitted as its own terminated record. */
struct ini_write_style {
	size_t key_width;
	const char *key_prefix;
	const char *section_separator;
	const char *value_separator;
	const char *literal_separator;
	const char *bit_separator;
};

struct ini_bit_name {
	uint64_t bit;
	const char *name;
};

extern const struct ini_write_style ini_default_style;

/* Reports whether name satisfies the key-name grammar described above. */
bool ini_key_name_valid(const char *name);

/*
 * Result pointers are replaced only after a complete successful operation.
 * Reading maps the entire regular file and does not change stream position.
 * Callers must flush pending writes first.
 */
int ini_document_create(ini_document_t **result);
int ini_document_clone(ini_document_t **result,
    const ini_document_t *source);
int ini_document_parse(ini_document_t **result, const void *data, size_t size);
int ini_document_read(ini_document_t **result, FILE *stream);
/* The serialized result is NUL-terminated; size excludes that terminator. */
int ini_document_serialize(const ini_document_t *document, char **result,
    size_t *size);
int ini_document_write(const ini_document_t *document, FILE *stream);
void ini_document_free(ini_document_t **document);

ini_section_t *ini_document_root(ini_document_t *document);
size_t ini_document_section_count(const ini_document_t *document);
ini_section_t *ini_document_section_at(ini_document_t *document,
    size_t index);
ini_section_t *ini_document_find_section(ini_document_t *document,
    const char *name);
int ini_document_add_section(ini_document_t *document, const char *name,
    const struct ini_write_style *style, ini_section_t **result);
bool ini_document_remove_section(ini_document_t *document,
    ini_section_t *section);
bool ini_document_rename_section(ini_document_t *document,
    ini_section_t *section, const char *name);

const char *ini_section_name(const ini_section_t *section);
size_t ini_section_count(const ini_section_t *section);
const struct name_value *ini_section_at(const ini_section_t *section,
    size_t index);
const char *ini_section_find(const ini_section_t *section, const char *key);
bool ini_section_has_key(const ini_section_t *section, const char *key);
int ini_section_copy_pairs(const ini_section_t *section,
    name_value_list_t *result);

const char *ini_section_get_string(const ini_section_t *section,
    const char *key, const char *default_value);
int64_t ini_section_get_int64(const ini_section_t *section, const char *key,
    int64_t default_value);
int64_t ini_section_get_bounded_int64(const ini_section_t *section,
    const char *key, int64_t default_value, int64_t minimum,
    int64_t maximum);
uint64_t ini_section_get_uint64(const ini_section_t *section, const char *key,
    uint64_t default_value);
double ini_section_get_double(const ini_section_t *section, const char *key,
    double default_value);
bool ini_section_get_bool(const ini_section_t *section, const char *key,
    bool default_value);
double ini_section_get_duration(const ini_section_t *section, const char *key,
    double default_value);
time_t ini_section_get_datetime(const ini_section_t *section, const char *key,
    time_t default_value);
size_t ini_section_get_enum(const ini_section_t *section, const char *key,
    str_list_t names, size_t default_value);
size_t ini_section_get_enum_names(const ini_section_t *section,
    const char *key, const char *const *names, size_t name_count,
    size_t default_value);
uint64_t ini_section_get_bitfield(const ini_section_t *section,
    const char *key, const struct ini_bit_name *names, size_t name_count,
    uint64_t default_value);
int ini_section_get_string_list(const ini_section_t *section, const char *key,
    const char *delimiters, str_list_t *result);
int ini_section_get_int64_list(const ini_section_t *section, const char *key,
    const char *delimiters, int64_t **result, size_t *count);
void ini_free_int64_list(int64_t *values);

int ini_section_set_string(ini_section_t *section, const char *key,
    const char *value, const struct ini_write_style *style);
int ini_section_set_int64(ini_section_t *section, const char *key,
    int64_t value, const struct ini_write_style *style);
int ini_section_set_uint64(ini_section_t *section, const char *key,
    uint64_t value, const struct ini_write_style *style);
int ini_section_set_double(ini_section_t *section, const char *key,
    double value, const struct ini_write_style *style);
int ini_section_set_bool(ini_section_t *section, const char *key, bool value,
    const struct ini_write_style *style);
int ini_section_set_duration(ini_section_t *section, const char *key,
    double seconds, const struct ini_write_style *style);
int ini_section_set_datetime(ini_section_t *section, const char *key,
    time_t value, bool include_time, const struct ini_write_style *style);
int ini_section_set_enum(ini_section_t *section, const char *key,
    str_list_t names, size_t value, const struct ini_write_style *style);
int ini_section_set_enum_names(ini_section_t *section, const char *key,
    const char *const *names, size_t name_count, size_t value,
    const struct ini_write_style *style);
int ini_section_set_bitfield(ini_section_t *section, const char *key,
    const struct ini_bit_name *names, size_t name_count, uint64_t value,
    const struct ini_write_style *style);
int ini_section_set_string_list(ini_section_t *section, const char *key,
    str_list_t values, const char *separator,
    const struct ini_write_style *style);
int ini_section_set_int64_list(ini_section_t *section, const char *key,
    const int64_t *values, size_t count, const char *separator,
    const struct ini_write_style *style);

/* Removes every matching key in this physical section block. */
size_t ini_section_remove_key(ini_section_t *section, const char *key);

#endif
