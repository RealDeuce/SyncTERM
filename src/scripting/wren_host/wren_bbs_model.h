/* Mutable menu view of the disk-backed BBS directory. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_WREN_BBS_MODEL_H_
#define ST_WREN_BBS_MODEL_H_

#include "st_bbs_list.h"
#include "st_profile.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum st_wren_bbs_kind {
	ST_WREN_BBS_MODEL_ENTRY,
	ST_WREN_BBS_DEFAULTS,
};

struct st_wren_bbs_record {
	struct st_profile profile;
	uint64_t identity;
	/* Borrowed until the source list's generation changes. */
	const struct st_bbs_entry *source_entry;
	char *name_key;
	size_t name_key_size;
	char persisted_name[ST_PROFILE_NAME_MAX + 1];
	enum st_bbs_source source;
	struct st_wren_bbs_record *shadowed;
	bool dirty;
	bool new_entry;
};

struct st_wren_bbs_model {
	struct st_wren_bbs_record **entries;
	const st_bbs_list_t *source_list;
	uint64_t source_generation;
	size_t count;
	size_t capacity;
	int next_identifier;
	struct st_wren_bbs_record defaults;
	uint64_t generation;
	uint64_t next_identity;
	bool loaded;
};

void st_wren_bbs_model_init(struct st_wren_bbs_model *model);
void st_wren_bbs_model_dispose(struct st_wren_bbs_model *model);
int st_wren_bbs_model_build(struct st_wren_bbs_model *model,
    const st_bbs_list_t *list);
const char *st_wren_bbs_model_search_key(struct st_wren_bbs_model *model,
	struct st_wren_bbs_record *record, size_t *size);
void st_wren_bbs_model_replace(struct st_wren_bbs_model *destination,
    struct st_wren_bbs_model *replacement);

size_t st_wren_bbs_model_count(const struct st_wren_bbs_model *model);
struct st_wren_bbs_record *st_wren_bbs_model_at(
    const struct st_wren_bbs_model *model, size_t index);
bool st_wren_bbs_model_valid(const struct st_wren_bbs_model *model,
    const struct st_wren_bbs_record *record, enum st_wren_bbs_kind kind,
    uint64_t generation, uint64_t identity);
/* Name arguments are NFC text from the Wren host boundary. */
bool st_wren_bbs_model_name_available(struct st_wren_bbs_model *model,
    const char *name);

struct st_wren_bbs_record *st_wren_bbs_model_create(
    struct st_wren_bbs_model *model, const char *name,
    const struct st_profile *source, const int *fields, size_t field_count);
bool st_wren_bbs_model_rename(struct st_wren_bbs_model *model,
    struct st_wren_bbs_record *record, const char *name,
    const int *fields, size_t field_count);
bool st_wren_bbs_model_prepare_save(struct st_wren_bbs_model *model,
    const struct st_wren_bbs_record *record);
void st_wren_bbs_model_saved(struct st_wren_bbs_model *model,
    struct st_wren_bbs_record *record, const int *fields,
    size_t field_count);
bool st_wren_bbs_model_delete(struct st_wren_bbs_model *model,
    struct st_wren_bbs_record *record, const int *fields,
    size_t field_count);
void st_wren_bbs_model_sort(struct st_wren_bbs_model *model,
    const int *fields, size_t field_count);

struct st_wren_bbs_record *st_wren_bbs_model_find(
    const struct st_wren_bbs_model *model, uint64_t generation, uint64_t identity);

#endif
