/* Combined personal and system SyncTERM BBS directory. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_BBS_LIST_H_
#define ST_BBS_LIST_H_

#include "st_list_crypto.h"
#include "st_profile.h"
#include "st_webget.h"

#include <stddef.h>
#include <stdint.h>

#define ST_BBS_LIST_MAX_ENTRIES 100000U

typedef struct st_bbs_list st_bbs_list_t;
struct st_app_paths;

enum st_bbs_source {
	ST_BBS_SOURCE_PERSONAL,
	ST_BBS_SOURCE_SHARED,
	ST_BBS_SOURCE_WEB_SYSTEM,
	ST_BBS_SOURCE_WEB_NAMED,
};

struct st_bbs_entry {
	const struct st_profile *profile;
	enum st_bbs_source source;
	const char *source_name;
};

struct st_bbs_diagnostic {
	enum st_bbs_source source;
	const char *source_name;
	const char *section_name;
	int status;
};

struct st_bbs_mutation_result {
	int cache_status;
};

enum st_bbs_web_refresh_state {
	ST_BBS_WEB_REFRESH_PENDING,
	ST_BBS_WEB_REFRESH_ACTIVE,
	ST_BBS_WEB_REFRESH_COMPLETE,
	ST_BBS_WEB_REFRESH_FAILED,
};

struct st_bbs_web_refresh_item {
	const char *name;
	enum st_bbs_web_refresh_state state;
	enum st_webget_phase phase;
	uint64_t received;
	uint64_t total;
	bool total_known;
	int status;
	enum st_webget_outcome outcome;
};

/* The callback runs on the calling thread with an immutable snapshot. */
typedef void (*st_bbs_web_refresh_progress_fn)(
	const struct st_bbs_web_refresh_item *items, size_t count,
	void *context);

/* Password bytes are copied and cleared when the list is closed. */
int st_bbs_list_open(st_bbs_list_t **result,
    const struct st_app_paths *paths, const void *password,
    size_t password_size);
void st_bbs_list_close(st_bbs_list_t *list);
int st_bbs_list_reload(st_bbs_list_t *list);

size_t st_bbs_list_count(const st_bbs_list_t *list);
const struct st_bbs_entry *st_bbs_list_at(const st_bbs_list_t *list,
    size_t index);
const struct st_bbs_entry *st_bbs_list_find(const st_bbs_list_t *list,
    const char *name);
const struct st_profile *st_bbs_list_defaults(const st_bbs_list_t *list);
uint64_t st_bbs_list_generation(const st_bbs_list_t *list);
const struct st_list_crypto_info *st_bbs_list_crypto_info(
    const st_bbs_list_t *list);

size_t st_bbs_list_diagnostic_count(const st_bbs_list_t *list);
const struct st_bbs_diagnostic *st_bbs_list_diagnostic_at(
    const st_bbs_list_t *list, size_t index);

int st_bbs_list_add(st_bbs_list_t *list, const struct st_profile *profile,
    struct st_bbs_mutation_result *result);
int st_bbs_list_update(st_bbs_list_t *list, const char *old_name,
    const struct st_profile *profile, struct st_bbs_mutation_result *result);
int st_bbs_list_remove(st_bbs_list_t *list, const char *name,
    struct st_bbs_mutation_result *result);
int st_bbs_list_set_defaults(st_bbs_list_t *list,
    const struct st_profile *defaults);

/* Explicitly rewrites plain/v1/v2 storage using current v2 encryption. */
int st_bbs_list_rewrite_encryption(st_bbs_list_t *list,
    const struct st_list_crypto_options *options, const void *password,
    size_t password_size);
/* Rewrites with the password already held by an opened encrypted list. */
int st_bbs_list_rewrite_encryption_current_password(st_bbs_list_t *list,
    const struct st_list_crypto_options *options);

/* Refreshes the URL-backed system and named lists configured in settings. */
int st_bbs_list_refresh_web(st_bbs_list_t *list,
    st_webget_progress_fn progress, void *progress_context);
/* Refreshes one configured named web list and rebuilds the combined view. */
int st_bbs_list_refresh_web_at(st_bbs_list_t *list, size_t index,
	st_webget_progress_fn progress, void *progress_context);
/* Refreshes configured cache files without opening the personal directory. */
int st_bbs_web_cache_refresh(const struct st_app_paths *paths,
	st_bbs_web_refresh_progress_fn progress, void *progress_context);

enum st_bbs_sort_field {
	ST_BBS_SORT_NONE = 0,
	ST_BBS_SORT_NAME = 1,
	ST_BBS_SORT_ADDED = 2,
	ST_BBS_SORT_LAST_CONNECTED = 3,
	ST_BBS_SORT_CALLS = 4,
	ST_BBS_SORT_SOURCE = 5,
	ST_BBS_SORT_ADDRESS = 6,
	ST_BBS_SORT_PORT = 7,
	ST_BBS_SORT_USERNAME = 8,
	ST_BBS_SORT_PASSWORD = 9,
	ST_BBS_SORT_SYSTEM_PASSWORD = 10,
	ST_BBS_SORT_CONNECTION_TYPE = 11,
	ST_BBS_SORT_SCREEN_MODE = 12,
	ST_BBS_SORT_STATUS_HIDDEN = 13,
	ST_BBS_SORT_DOWNLOAD_DIRECTORY = 14,
	ST_BBS_SORT_UPLOAD_DIRECTORY = 15,
	ST_BBS_SORT_LOG_PATH = 16,
	ST_BBS_SORT_TRANSFER_LOG_LEVEL = 17,
	ST_BBS_SORT_BPS_RATE = 18,
	ST_BBS_SORT_MUSIC = 19,
	ST_BBS_SORT_ADDRESS_FAMILY = 20,
	ST_BBS_SORT_FONT = 21,
	ST_BBS_SORT_HIDE_POPUPS = 22,
	ST_BBS_SORT_RIP = 23,
	ST_BBS_SORT_FLOW_CONTROL = 24,
	ST_BBS_SORT_COMMENT = 25,
	ST_BBS_SORT_FORCE_LCF = 26,
	ST_BBS_SORT_YELLOW_IS_YELLOW = 27,
	ST_BBS_SORT_TERMINAL_TYPE = 28,
	ST_BBS_SORT_EXPLICIT = 29,
	/* 30 is accepted as the historical shifted explicit-field alias. */
	ST_BBS_SORT_LF_EXPAND = 31,
};

int st_bbs_sort_field_normalize(int field);
const char *st_bbs_sort_field_name(int field);
bool st_bbs_sort_field_reversed(int field);
int st_bbs_list_sort(st_bbs_list_t *list, const int *fields,
    size_t field_count);

/* Removes cache directories which do not match a current combined entry. */
int st_bbs_list_sweep_orphan_caches(st_bbs_list_t *list);

#endif
