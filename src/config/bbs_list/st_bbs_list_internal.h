/* Internal BBS directory definitions. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_BBS_LIST_INTERNAL_H_
#define ST_BBS_LIST_INTERNAL_H_

#include "st_bbs_list.h"

#include "ini.h"

#include <stdbool.h>

#define ST_BBS_CACHE_KEY_MAX 70U
#define ST_BBS_CACHE_DIRECTORY_KEY_MAX (ST_BBS_CACHE_KEY_MAX + 1 + 3 * sizeof(size_t))

struct st_bbs_owned_entry {
	struct st_bbs_entry public;
	struct st_profile profile;
	char *source_name;
	struct st_bbs_owned_entry *shadowed;
	size_t references;
};

struct st_bbs_owned_diagnostic {
	struct st_bbs_diagnostic public;
	char *source_name;
	char *section_name;
};

struct st_bbs_view {
	struct st_bbs_owned_entry **entries;
	struct st_bbs_owned_entry **names;
	struct st_bbs_owned_diagnostic **diagnostics;
	struct st_bbs_source_snapshot **snapshots;
	struct st_profile defaults;
};

struct st_bbs_list {
	struct st_bbs_view view;
	ini_document_t *personal_document;
	struct st_list_crypto_info crypto;
	struct st_list_crypto_options write_options;
	char *personal_path;
	char *shared_path;
	char *cache_path;
	char *system_cache_path;
	char *cached_system_list;
	char *download_path;
	unsigned char *password;
	size_t password_size;
	uint64_t generation;
	int *sort_fields;
	size_t sort_field_count;
	bool sort_configured;
	uint8_t picker_key[32];
	bool picker_key_available;
};

bool st_bbs_cache_key(char *result, size_t result_size, const char *name);
/* Map a normalized INI section name to the bounded, valid profile name. */
bool st_bbs_loaded_name(char result[ST_PROFILE_NAME_MAX + 1], const char *name);
ini_section_t *st_bbs_find_profile_section(ini_document_t *document, const char *name);
bool st_bbs_cache_key_recognized(const char *key);
/* Platform mechanisms; callbacks receive one basename at a time. */
int st_bbs_cache_visit(const char *directory,
	int (*visit)(const char *key, void *context), void *context);
int st_bbs_cache_remove_tree(const char *path);
int st_bbs_decode_profile(struct st_profile *profile,
    const ini_section_t *section, const char *name, bool system,
    const char *download_path);
int st_bbs_encode_profile(ini_section_t *section,
    const struct st_profile *profile, bool defaults);
bool st_bbs_profiles_equal(const struct st_profile *left,
    const struct st_profile *right);
int st_bbs_view_prepare(const st_bbs_list_t *list,
    ini_document_t *personal_document, struct st_bbs_view *view);
int st_bbs_view_prepare_update(const st_bbs_list_t *list,
    const char *old_name, const struct st_profile *profile,
    ini_document_t *personal_document, struct st_bbs_view *view);
void st_bbs_view_dispose(struct st_bbs_view *view);
void st_bbs_view_publish(st_bbs_list_t *list, struct st_bbs_view *view);
int st_bbs_view_sort(struct st_bbs_view *view, const int *fields,
    size_t field_count);
int st_bbs_cache_rename(st_bbs_list_t *list, const char *old_name,
    const char *new_name);
int st_bbs_cache_remove(st_bbs_list_t *list, const char *name);
int st_bbs_cache_sweep(st_bbs_list_t *list);
void st_bbs_picker_key_load(st_bbs_list_t *list,
	ini_document_t *document, const struct st_list_crypto_info *info);

#endif
