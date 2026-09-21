/* Mutable menu view of the disk-backed BBS directory. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_bbs_model.h"

#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct model_sort_context {
	const int *fields;
	size_t count;
};

static _Thread_local struct model_sort_context current_sort;

static void
free_record(struct st_wren_bbs_record *record)
{
	if (record == NULL)
		return;
	free_record(record->shadowed);
	st_profile_dispose(&record->profile);
	free(record);
}

void
st_wren_bbs_model_init(struct st_wren_bbs_model *model)
{
	memset(model, 0, sizeof(*model));
	st_profile_init(&model->defaults.profile);
	st_profile_init(&model->transient.profile);
	model->generation = 1;
	model->transient_generation = 1;
}

void
st_wren_bbs_model_dispose(struct st_wren_bbs_model *model)
{
	if (model == NULL)
		return;
	for (size_t i = 0; i < model->count; ++i)
		free_record(model->entries[i]);
	free(model->entries);
	st_profile_dispose(&model->defaults.profile);
	st_profile_dispose(&model->transient.profile);
	memset(model, 0, sizeof(*model));
}

static struct st_wren_bbs_record *
copy_record(const struct st_profile *profile, enum st_bbs_source source)
{
	struct st_wren_bbs_record *record = calloc(1, sizeof(*record));

	if (record == NULL)
		return NULL;
	st_profile_init(&record->profile);
	if (!st_profile_copy(&record->profile, profile)) {
		free_record(record);
		return NULL;
	}
	record->source = source;
	(void)snprintf(record->persisted_name, sizeof(record->persisted_name),
	    "%s", profile->name);
	return record;
}

static struct st_wren_bbs_record *
copy_entry(const struct st_bbs_entry *entry)
{
	const struct st_bbs_entry *shadowed;
	struct st_wren_bbs_record *record;

	if (entry == NULL)
		return NULL;
	record = copy_record(entry->profile, entry->source);
	if (record == NULL)
		return NULL;
	shadowed = st_bbs_entry_shadowed(entry);
	if (shadowed != NULL) {
		record->shadowed = copy_entry(shadowed);
		if (record->shadowed == NULL) {
			free_record(record);
			return NULL;
		}
	}
	return record;
}

static bool
reserve_entries(struct st_wren_bbs_model *model, size_t count)
{
	struct st_wren_bbs_record **replacement;
	size_t capacity;

	if (count <= model->capacity)
		return true;
	capacity = model->capacity == 0 ? 16 : model->capacity;
	while (capacity < count) {
		if (capacity > SIZE_MAX / 2)
			return false;
		capacity *= 2;
	}
	if (capacity > SIZE_MAX / sizeof(*replacement))
		return false;
	replacement = realloc(model->entries, capacity * sizeof(*replacement));
	if (replacement == NULL)
		return false;
	model->entries = replacement;
	model->capacity = capacity;
	return true;
}

int
st_wren_bbs_model_build(struct st_wren_bbs_model *model,
    const st_bbs_list_t *list)
{
	const struct st_profile *defaults;
	size_t count;

	if (model == NULL || list == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	count = st_bbs_list_count(list);
	if (!reserve_entries(model, count))
		return IO_ERROR_OUT_OF_MEMORY;
	defaults = st_bbs_list_defaults(list);
	if (defaults == NULL
	    || !st_profile_copy(&model->defaults.profile, defaults))
		return IO_ERROR_OUT_OF_MEMORY;
	model->defaults.source = ST_BBS_SOURCE_PERSONAL;
	for (size_t i = 0; i < count; ++i) {
		const struct st_bbs_entry *entry = st_bbs_list_at(list, i);
		struct st_wren_bbs_record *record;

		if ((record = copy_entry(entry)) == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		model->entries[model->count++] = record;
	}
	model->loaded = true;
	return 0;
}

void
st_wren_bbs_model_replace(struct st_wren_bbs_model *destination,
    struct st_wren_bbs_model *replacement)
{
	uint64_t generation = destination->generation + 1;
	uint64_t transient_generation = destination->transient_generation + 1;

	st_wren_bbs_model_dispose(destination);
	*destination = *replacement;
	memset(replacement, 0, sizeof(*replacement));
	destination->generation = generation;
	destination->transient_generation = transient_generation;
}

size_t
st_wren_bbs_model_count(const struct st_wren_bbs_model *model)
{
	return model == NULL ? 0 : model->count;
}

struct st_wren_bbs_record *
st_wren_bbs_model_at(const struct st_wren_bbs_model *model, size_t index)
{
	return model == NULL || index >= model->count ? NULL : model->entries[index];
}

static bool
contains(const struct st_wren_bbs_model *model,
    const struct st_wren_bbs_record *record)
{
	for (size_t i = 0; i < model->count; ++i) {
		if (model->entries[i] == record)
			return true;
	}
	return false;
}

bool
st_wren_bbs_model_valid(const struct st_wren_bbs_model *model,
    const struct st_wren_bbs_record *record, enum st_wren_bbs_kind kind,
    uint64_t generation)
{
	if (model == NULL || record == NULL || !model->loaded)
		return false;
	if (kind == ST_WREN_BBS_DEFAULTS)
		return generation == model->generation && record == &model->defaults;
	if (kind == ST_WREN_BBS_TRANSIENT)
		return model->transient_valid
		    && generation == model->transient_generation
		    && record == &model->transient;
	return generation == model->generation && contains(model, record);
}

bool
st_wren_bbs_model_name_available(const struct st_wren_bbs_model *model,
    const char *name)
{
	if (model == NULL || !model->loaded || !st_bbs_name_valid(name))
		return false;
	for (size_t i = 0; i < model->count; ++i) {
		if (model->entries[i]->source == ST_BBS_SOURCE_PERSONAL
		    && st_bbs_name_equal(model->entries[i]->profile.name, name))
			return false;
	}
	return true;
}

static int
compare_records(const void *left_pointer, const void *right_pointer)
{
	const struct st_wren_bbs_record *left =
	    *(const struct st_wren_bbs_record *const *)left_pointer;
	const struct st_wren_bbs_record *right =
	    *(const struct st_wren_bbs_record *const *)right_pointer;

	return st_bbs_compare_profiles(&left->profile, left->source,
	    &right->profile, right->source, current_sort.fields,
	    current_sort.count);
}

void
st_wren_bbs_model_sort(struct st_wren_bbs_model *model,
    const int *fields, size_t field_count)
{
	current_sort.fields = fields;
	current_sort.count = field_count;
	qsort(model->entries, model->count, sizeof(*model->entries),
	    compare_records);
	current_sort.fields = NULL;
	current_sort.count = 0;
}

static size_t
record_index(const struct st_wren_bbs_model *model,
    const struct st_wren_bbs_record *record)
{
	for (size_t i = 0; i < model->count; ++i) {
		if (model->entries[i] == record)
			return i;
	}
	return SIZE_MAX;
}

struct st_wren_bbs_record *
st_wren_bbs_model_create(struct st_wren_bbs_model *model, const char *name,
    const struct st_profile *source, const int *fields, size_t field_count)
{
	struct st_wren_bbs_record *record;
	struct st_wren_bbs_record *shadowed = NULL;
	size_t shadowed_index = SIZE_MAX;

	if (model == NULL || source == NULL
	    || !st_wren_bbs_model_name_available(model, name))
		return NULL;
	for (size_t i = 0; i < model->count; ++i) {
		if (model->entries[i]->source != ST_BBS_SOURCE_PERSONAL
		    && st_bbs_name_equal(model->entries[i]->profile.name, name)) {
			shadowed = model->entries[i];
			shadowed_index = i;
			break;
		}
	}
	if (shadowed == NULL
	    && (model->count >= ST_BBS_LIST_MAX_ENTRIES
	    || !reserve_entries(model, model->count + 1)))
		return NULL;
	record = copy_record(source, ST_BBS_SOURCE_PERSONAL);
	if (record == NULL)
		return NULL;
	(void)snprintf(record->profile.name, sizeof(record->profile.name), "%s",
	    name);
	record->profile.source = ST_PROFILE_SOURCE_USER;
	record->profile.added = time(NULL);
	record->profile.connected = 0;
	record->profile.calls = 0;
	record->persisted_name[0] = '\0';
	record->dirty = true;
	record->new_entry = true;
	record->shadowed = shadowed;
	if (shadowed == NULL)
		model->entries[model->count++] = record;
	else
		model->entries[shadowed_index] = record;
	model->generation++;
	st_wren_bbs_model_invalidate_transient(model);
	st_wren_bbs_model_sort(model, fields, field_count);
	return record;
}

bool
st_wren_bbs_model_rename(struct st_wren_bbs_model *model,
    struct st_wren_bbs_record *record, const char *name,
    const int *fields, size_t field_count)
{
	if (model == NULL || record == NULL
	    || record->source != ST_BBS_SOURCE_PERSONAL
	    || !st_bbs_name_valid(name) || !contains(model, record))
		return false;
	for (size_t i = 0; i < model->count; ++i) {
		if (model->entries[i] != record
		    && st_bbs_name_equal(model->entries[i]->profile.name, name))
			return false;
	}
	(void)snprintf(record->profile.name, sizeof(record->profile.name), "%s",
	    name);
	record->dirty = true;
	st_wren_bbs_model_sort(model, fields, field_count);
	return true;
}

bool
st_wren_bbs_model_prepare_save(struct st_wren_bbs_model *model,
    const struct st_wren_bbs_record *record)
{
	return model != NULL && record != NULL && contains(model, record)
	    && (record->shadowed == NULL
	    || st_bbs_name_equal(record->profile.name,
	    record->shadowed->profile.name)
	    || reserve_entries(model, model->count + 1));
}

void
st_wren_bbs_model_saved(struct st_wren_bbs_model *model,
    struct st_wren_bbs_record *record, const int *fields,
    size_t field_count)
{
	if (record->shadowed != NULL
	    && !st_bbs_name_equal(record->profile.name,
	    record->shadowed->profile.name)) {
		model->entries[model->count++] = record->shadowed;
		record->shadowed = NULL;
	}
	(void)snprintf(record->persisted_name, sizeof(record->persisted_name),
	    "%s", record->profile.name);
	record->new_entry = false;
	record->dirty = false;
	st_wren_bbs_model_sort(model, fields, field_count);
}

bool
st_wren_bbs_model_delete(struct st_wren_bbs_model *model,
    struct st_wren_bbs_record *record, const int *fields,
    size_t field_count)
{
	size_t index = record_index(model, record);

	if (index == SIZE_MAX)
		return false;
	if (record->shadowed != NULL) {
		model->entries[index] = record->shadowed;
		record->shadowed = NULL;
	}
	else {
		memmove(&model->entries[index], &model->entries[index + 1],
		    (model->count - index - 1) * sizeof(*model->entries));
		model->count--;
	}
	free_record(record);
	model->generation++;
	st_wren_bbs_model_invalidate_transient(model);
	st_wren_bbs_model_sort(model, fields, field_count);
	return true;
}

bool
st_wren_bbs_model_set_transient(struct st_wren_bbs_model *model,
    const struct st_profile *profile)
{
	struct st_profile copy;

	st_profile_init(&copy);
	if (!st_profile_copy(&copy, profile)) {
		st_profile_dispose(&copy);
		return false;
	}
	st_profile_dispose(&model->transient.profile);
	memset(&model->transient, 0, sizeof(model->transient));
	model->transient.profile = copy;
	model->transient.source = ST_BBS_SOURCE_PERSONAL;
	model->transient_generation++;
	model->transient_valid = true;
	return true;
}

void
st_wren_bbs_model_invalidate_transient(struct st_wren_bbs_model *model)
{
	st_profile_dispose(&model->transient.profile);
	st_profile_init(&model->transient.profile);
	model->transient_generation++;
	model->transient_valid = false;
}
