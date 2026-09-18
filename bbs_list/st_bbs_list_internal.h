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

struct st_bbs_owned_entry {
	struct st_bbs_entry public;
	struct st_profile profile;
	char *source_name;
};

struct st_bbs_owned_diagnostic {
	struct st_bbs_diagnostic public;
	char *source_name;
	char *section_name;
};

struct st_bbs_list {
	struct st_bbs_owned_entry **entries;
	struct st_bbs_owned_diagnostic **diagnostics;
	struct st_profile defaults;
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
};

bool st_bbs_name_equal(const char *left, const char *right);
bool st_bbs_name_valid(const char *name);
int st_bbs_decode_profile(struct st_profile *profile,
    const ini_section_t *section, const char *name, bool system,
    const char *download_path);
int st_bbs_encode_profile(ini_section_t *section,
    const struct st_profile *profile, bool defaults);
bool st_bbs_profiles_equal(const struct st_profile *left,
    const struct st_profile *right);
int st_bbs_list_rebuild(st_bbs_list_t *list);
int st_bbs_cache_rename(st_bbs_list_t *list, const char *old_name,
    const char *new_name);
int st_bbs_cache_remove(st_bbs_list_t *list, const char *name);
int st_bbs_cache_sweep(st_bbs_list_t *list);

#endif
