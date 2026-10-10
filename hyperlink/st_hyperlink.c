/* Application-lifetime hyperlink registry. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_hyperlink.h"

#include "io_error.h"
#include "st_threads.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct hyperlink_entry {
	char *uri;
	char *parameter;
	bool occupied;
	bool marked;
};

struct hyperlink_root {
	st_hyperlink_root_fn function;
	void *context;
};

struct st_hyperlink_registry {
	struct hyperlink_entry *entries;
	size_t count;
	size_t used;
	size_t capacity;
	size_t free_hint;
	struct hyperlink_root *roots;
	size_t root_count;
	size_t root_capacity;
	st_mutex_t mutex;
};

static char *
copy_span(const char *text, size_t length)
{
	char *copy;

	if (length == SIZE_MAX)
		return NULL;
	copy = malloc(length + 1);
	if (copy == NULL)
		return NULL;
	memcpy(copy, text, length);
	copy[length] = '\0';
	return copy;
}

static bool
span_valid(const char *text, size_t length, bool empty_allowed)
{
	return (text != NULL || length == 0)
	    && (empty_allowed || length != 0)
	    && (length == 0 || memchr(text, '\0', length) == NULL);
}

int
st_hyperlink_registry_create(st_hyperlink_registry_t **result)
{
	st_hyperlink_registry_t *registry;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	registry = calloc(1, sizeof(*registry));
	if (registry == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (st_mutex_init(&registry->mutex, ST_MUTEX_PLAIN)
	    != ST_THREAD_SUCCESS) {
		free(registry);
		return IO_ERROR_IO;
	}
	*result = registry;
	return 0;
}

void
st_hyperlink_registry_destroy(st_hyperlink_registry_t *registry)
{
	if (registry == NULL)
		return;
	for (size_t i = 0; i < registry->count; ++i) {
		free(registry->entries[i].uri);
		free(registry->entries[i].parameter);
	}
	free(registry->entries);
	free(registry->roots);
	st_mutex_destroy(&registry->mutex);
	free(registry);
}

static int
grow_registry(st_hyperlink_registry_t *registry)
{
	size_t capacity = registry->capacity == 0 ? 64 : registry->capacity * 2;
	struct hyperlink_entry *entries;

	if (capacity > UINT16_MAX)
		capacity = UINT16_MAX;
	if (capacity <= registry->capacity)
		return IO_ERROR_OUT_OF_RANGE;
	entries = realloc(registry->entries, capacity * sizeof(*entries));
	if (entries == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	registry->entries = entries;
	memset(entries + registry->capacity, 0,
	    (capacity - registry->capacity) * sizeof(*entries));
	registry->capacity = capacity;
	return 0;
}

static int
mark_identifier(void *context, uint16_t identifier)
{
	st_hyperlink_registry_t *registry = context;

	if (identifier != 0 && (size_t)identifier <= registry->count
	    && registry->entries[identifier - 1].occupied)
		registry->entries[identifier - 1].marked = true;
	return 0;
}

static int
collect_locked(st_hyperlink_registry_t *registry)
{
	int status = 0;
	size_t free_hint = registry->count;

	if (registry->root_count == 0)
		return IO_ERROR_OUT_OF_RANGE;
	for (size_t i = 0; i < registry->count; ++i)
		registry->entries[i].marked = false;
	for (size_t i = 0; i < registry->root_count; ++i) {
		status = registry->roots[i].function(registry->roots[i].context,
		    mark_identifier, registry);
		if (status != 0)
			break;
	}
	if (status != 0)
		return status;
	for (size_t i = 0; i < registry->count; ++i) {
		struct hyperlink_entry *entry = &registry->entries[i];

		if (!entry->occupied) {
			if (free_hint == registry->count)
				free_hint = i;
			continue;
		}
		if (entry->marked)
			continue;
		free(entry->uri);
		free(entry->parameter);
		*entry = (struct hyperlink_entry){0};
		--registry->used;
		if (free_hint == registry->count)
			free_hint = i;
	}
	registry->free_hint = free_hint;
	return 0;
}

int
st_hyperlink_registry_add(st_hyperlink_registry_t *registry,
	const char *uri, size_t uri_length, const char *parameter,
	size_t parameter_length, uint16_t *identifier)
{
	char *uri_copy = NULL;
	char *parameter_copy = NULL;
	int status = 0;

	if (registry == NULL || identifier == NULL
	    || !span_valid(uri, uri_length, false)
	    || !span_valid(parameter, parameter_length, true))
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&registry->mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (parameter_length != 0) {
		for (size_t i = 0; i < registry->count; ++i) {
			const struct hyperlink_entry *entry = &registry->entries[i];

			if (entry->occupied && entry->parameter != NULL
			    && strlen(entry->uri) == uri_length
			    && strlen(entry->parameter) == parameter_length
			    && memcmp(entry->uri, uri, uri_length) == 0
			    && memcmp(entry->parameter, parameter,
			    parameter_length) == 0) {
				*identifier = (uint16_t)(i + 1);
				(void)st_mutex_unlock(&registry->mutex);
				return 0;
			}
		}
	}
	if (registry->used == UINT16_MAX)
		status = collect_locked(registry);
	if (status == 0 && registry->used == UINT16_MAX)
		status = IO_ERROR_OUT_OF_RANGE;
	else if (status == 0 && registry->count == registry->capacity
	    && registry->used == registry->count)
		status = grow_registry(registry);
	if (status == 0) {
		uri_copy = copy_span(uri, uri_length);
		if (uri_copy == NULL)
			status = IO_ERROR_OUT_OF_MEMORY;
	}
	if (status == 0 && parameter_length != 0) {
		parameter_copy = copy_span(parameter, parameter_length);
		if (parameter_copy == NULL)
			status = IO_ERROR_OUT_OF_MEMORY;
	}
	if (status == 0) {
		size_t index = registry->free_hint;

		if (index == registry->count)
			++registry->count;
		registry->entries[index] = (struct hyperlink_entry){
			.uri = uri_copy,
			.parameter = parameter_copy,
			.occupied = true,
		};
		++registry->used;
		while (registry->free_hint < registry->count
		    && registry->entries[registry->free_hint].occupied)
			++registry->free_hint;
		*identifier = (uint16_t)(index + 1);
	}
	else {
		free(uri_copy);
		free(parameter_copy);
	}
	(void)st_mutex_unlock(&registry->mutex);
	return status;
}

int
st_hyperlink_registry_copy(st_hyperlink_registry_t *registry,
	uint16_t identifier, struct st_hyperlink *result)
{
	struct st_hyperlink copy = {0};
	const struct hyperlink_entry *entry;
	int status = 0;

	if (registry == NULL || identifier == 0 || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&registry->mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if ((size_t)identifier > registry->count
	    || !registry->entries[identifier - 1].occupied)
		status = IO_ERROR_NOT_FOUND;
	else {
		entry = &registry->entries[identifier - 1];
		copy.uri = copy_span(entry->uri, strlen(entry->uri));
		if (copy.uri == NULL)
			status = IO_ERROR_OUT_OF_MEMORY;
		else if (entry->parameter != NULL) {
			copy.parameter = copy_span(entry->parameter,
			    strlen(entry->parameter));
			if (copy.parameter == NULL)
				status = IO_ERROR_OUT_OF_MEMORY;
		}
	}
	(void)st_mutex_unlock(&registry->mutex);
	if (status == 0)
		*result = copy;
	else
		st_hyperlink_dispose(&copy);
	return status;
}

int
st_hyperlink_registry_contains(st_hyperlink_registry_t *registry,
	uint16_t identifier, bool *result)
{
	if (registry == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&registry->mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	*result = identifier != 0 && (size_t)identifier <= registry->count
	    && registry->entries[identifier - 1].occupied;
	(void)st_mutex_unlock(&registry->mutex);
	return 0;
}

int
st_hyperlink_registry_register_root(st_hyperlink_registry_t *registry,
	st_hyperlink_root_fn root, void *context)
{
	struct hyperlink_root *roots;
	size_t capacity;
	int status = 0;

	if (registry == NULL || root == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&registry->mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	for (size_t i = 0; i < registry->root_count; ++i) {
		if (registry->roots[i].function == root
		    && registry->roots[i].context == context) {
			status = IO_ERROR_ALREADY_EXISTS;
			goto done;
		}
	}
	if (registry->root_count == registry->root_capacity) {
		capacity = registry->root_capacity == 0
		    ? 4 : registry->root_capacity * 2;
		if (capacity < registry->root_capacity
		    || capacity > SIZE_MAX / sizeof(*roots)) {
			status = IO_ERROR_OUT_OF_RANGE;
			goto done;
		}
		roots = realloc(registry->roots, capacity * sizeof(*roots));
		if (roots == NULL) {
			status = IO_ERROR_OUT_OF_MEMORY;
			goto done;
		}
		registry->roots = roots;
		registry->root_capacity = capacity;
	}
	registry->roots[registry->root_count++] = (struct hyperlink_root){
		.function = root,
		.context = context,
	};
done:
	(void)st_mutex_unlock(&registry->mutex);
	return status;
}

void
st_hyperlink_registry_unregister_root(st_hyperlink_registry_t *registry,
	st_hyperlink_root_fn root, void *context)
{
	if (registry == NULL || root == NULL
	    || st_mutex_lock(&registry->mutex) != ST_THREAD_SUCCESS)
		return;
	for (size_t i = 0; i < registry->root_count; ++i) {
		if (registry->roots[i].function != root
		    || registry->roots[i].context != context)
			continue;
		memmove(&registry->roots[i], &registry->roots[i + 1],
		    (registry->root_count - i - 1) * sizeof(*registry->roots));
		--registry->root_count;
		break;
	}
	(void)st_mutex_unlock(&registry->mutex);
}

int
st_hyperlink_registry_collect(st_hyperlink_registry_t *registry)
{
	int status;

	if (registry == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&registry->mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	status = collect_locked(registry);
	(void)st_mutex_unlock(&registry->mutex);
	return status;
}

void
st_hyperlink_dispose(struct st_hyperlink *hyperlink)
{
	if (hyperlink == NULL)
		return;
	free(hyperlink->uri);
	free(hyperlink->parameter);
	*hyperlink = (struct st_hyperlink){0};
}
