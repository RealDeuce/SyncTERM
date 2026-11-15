/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_variables.h"

#include "io_error.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct variable {
	struct variable *next;
	size_t hash;
	struct st_rip3_variable_entry entry;
};

struct st_rip3_variables {
	struct variable **buckets;
	size_t capacity, count;
};

static int
identifier(char result[21], size_t *hash, const char *name, size_t length)
{
	size_t value = 0;

	if (name == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (length == 0 || length > 20)
		return IO_ERROR_OUT_OF_RANGE;
	for (size_t i = 0; i < length; i++) {
		unsigned char ch = (unsigned char)name[i];
		bool letter;

		if (ch >= 'a' && ch <= 'z')
			ch = (unsigned char)(ch - 'a' + 'A');
		letter = ch >= 'A' && ch <= 'Z';
		if ((!letter && i == 0)
		    || (!letter && ch != '_' && !(ch >= '0' && ch <= '9')))
			return IO_ERROR_INVALID_FORMAT;
		result[i] = (char)ch;
		/* Unsigned size_t wrap is intentional; this is not a wire hash. */
		value = value * 33 + ch;
	}
	result[length] = 0;
	*hash = value;
	return 0;
}

static struct variable *
find(const struct st_rip3_variables *variables, const char *name, size_t hash)
{
	if (variables->capacity != 0) {
		for (struct variable *entry = variables->buckets[hash & (variables->capacity - 1)];
		    entry != NULL; entry = entry->next) {
			if (entry->hash == hash && strcmp(entry->entry.name, name) == 0)
				return entry;
		}
	}
	return NULL;
}

static int
grow(struct st_rip3_variables *variables)
{
	struct variable **buckets;
	size_t capacity;

	if (variables->capacity != 0
	    && variables->count < variables->capacity - variables->capacity / 4)
		return 0;
	if (variables->capacity > SIZE_MAX / 2)
		return IO_ERROR_OUT_OF_RANGE;
	capacity = variables->capacity == 0 ? 16 : variables->capacity * 2;
	if (capacity > SIZE_MAX / sizeof(*buckets))
		return IO_ERROR_OUT_OF_RANGE;
	buckets = calloc(capacity, sizeof(*buckets));
	if (buckets == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	/* Allocate first. Re-linking has no failure points and one owner thread. */
	for (size_t i = 0; i < variables->capacity; i++) {
		struct variable *entry = variables->buckets[i];

		while (entry != NULL) {
			struct variable *next = entry->next;
			size_t bucket = entry->hash & (capacity - 1);

			entry->next = buckets[bucket];
			buckets[bucket] = entry;
			entry = next;
		}
	}
	free(variables->buckets);
	variables->buckets = buckets;
	variables->capacity = capacity;
	return 0;
}

int
st_rip3_variables_create(struct st_rip3_variables **result)
{
	struct st_rip3_variables *variables;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	variables = calloc(1, sizeof(*variables));
	if (variables == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	*result = variables;
	return 0;
}

void
st_rip3_variables_destroy(struct st_rip3_variables *variables)
{
	if (variables == NULL)
		return;
	for (size_t i = 0; i < variables->capacity; i++) {
		struct variable *entry = variables->buckets[i];

		while (entry != NULL) {
			struct variable *next = entry->next;

			free(entry);
			entry = next;
		}
	}
	free(variables->buckets);
	free(variables);
}

int
st_rip3_variables_set(struct st_rip3_variables *variables,
    const char *name, size_t name_length, const uint8_t *value, size_t value_length)
{
	struct st_rip3_variable_value replacement = {0};
	struct variable *entry;
	char canonical[21];
	size_t hash;
	int status;

	if (variables == NULL || (value == NULL && value_length != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	status = identifier(canonical, &hash, name, name_length);
	if (status != 0)
		return status;
	if (value_length > 255)
		value_length = 255;
	replacement.length = value_length;
	if (value_length != 0)
		memcpy(replacement.bytes, value, value_length);
	entry = find(variables, canonical, hash);
	if (entry != NULL) {
		entry->entry.value = replacement;
		return 0;
	}
	entry = calloc(1, sizeof(*entry));
	if (entry == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	entry->hash = hash;
	memcpy(entry->entry.name, canonical, name_length + 1);
	entry->entry.value = replacement;
	status = grow(variables);
	if (status != 0) {
		free(entry);
		return status;
	}
	entry->next = variables->buckets[hash & (variables->capacity - 1)];
	variables->buckets[hash & (variables->capacity - 1)] = entry;
	variables->count++;
	return 0;
}

int
st_rip3_variables_get(const struct st_rip3_variables *variables,
    const char *name, size_t name_length, struct st_rip3_variable_value *result)
{
	const struct variable *entry;
	char canonical[21];
	size_t hash;
	int status;

	if (variables == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = identifier(canonical, &hash, name, name_length);
	if (status != 0)
		return status;
	entry = find(variables, canonical, hash);
	if (entry == NULL)
		return IO_ERROR_NOT_FOUND;
	*result = entry->entry.value;
	return 0;
}

int
st_rip3_variables_remove(struct st_rip3_variables *variables,
    const char *name, size_t name_length)
{
	struct variable **link, *entry;
	char canonical[21];
	size_t hash;
	int status;

	if (variables == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = identifier(canonical, &hash, name, name_length);
	if (status != 0)
		return status;
	if (variables->capacity == 0)
		return IO_ERROR_NOT_FOUND;
	link = &variables->buckets[hash & (variables->capacity - 1)];
	while (*link != NULL && ((*link)->hash != hash
	    || strcmp((*link)->entry.name, canonical) != 0))
		link = &(*link)->next;
	entry = *link;
	if (entry == NULL)
		return IO_ERROR_NOT_FOUND;
	*link = entry->next;
	free(entry);
	variables->count--;
	return 0;
}

int
st_rip3_variables_count(const struct st_rip3_variables *variables, size_t *result)
{
	if (variables == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*result = variables->count;
	return 0;
}

static int
compare_entry(const void *first, const void *last)
{
	const struct st_rip3_variable_entry *a = first, *b = last;

	return strcmp(a->name, b->name);
}

int
st_rip3_variables_snapshot(const struct st_rip3_variables *variables,
    struct st_rip3_variable_snapshot *result)
{
	struct st_rip3_variable_snapshot snapshot = {0};
	size_t next = 0;

	if (variables == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (variables->count > SIZE_MAX / sizeof(*snapshot.entries))
		return IO_ERROR_OUT_OF_RANGE;
	if (variables->count != 0) {
		snapshot.entries = calloc(variables->count, sizeof(*snapshot.entries));
		if (snapshot.entries == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		for (size_t i = 0; i < variables->capacity; i++) {
			for (const struct variable *entry = variables->buckets[i];
			    entry != NULL; entry = entry->next)
				snapshot.entries[next++] = entry->entry;
		}
		qsort(snapshot.entries, next, sizeof(*snapshot.entries), compare_entry);
	}
	snapshot.count = next;
	*result = snapshot;
	return 0;
}

void
st_rip3_variables_snapshot_free(struct st_rip3_variable_snapshot *snapshot)
{
	if (snapshot == NULL)
		return;
	free(snapshot->entries);
	*snapshot = (struct st_rip3_variable_snapshot){0};
}

int
st_rip3_variables_from_snapshot(struct st_rip3_variables **result,
    const struct st_rip3_variable_snapshot *snapshot)
{
	struct st_rip3_variables *variables = NULL;
	int status;

	if (result == NULL || snapshot == NULL
	    || (snapshot->entries == NULL && snapshot->count != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (snapshot->count > SIZE_MAX / sizeof(*snapshot->entries))
		return IO_ERROR_OUT_OF_RANGE;
	status = st_rip3_variables_create(&variables);
	if (status != 0)
		return status;
	for (size_t i = 0; i < snapshot->count; i++) {
		const struct st_rip3_variable_entry *entry = &snapshot->entries[i];
		const char *end = memchr(entry->name, 0, sizeof(entry->name));
		char canonical[21];
		size_t hash, length;

		status = IO_ERROR_INVALID_FORMAT;
		if (end == NULL || entry->value.length > 255)
			goto failed;
		length = (size_t)(end - entry->name);
		if (identifier(canonical, &hash, entry->name, length) != 0
		    || find(variables, canonical, hash) != NULL)
			goto failed;
		status = st_rip3_variables_set(variables, canonical, length,
		    entry->value.bytes, entry->value.length);
		if (status != 0)
			goto failed;
	}
	*result = variables;
	return 0;
failed:
	st_rip3_variables_destroy(variables);
	return status;
}
