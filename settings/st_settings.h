/* SyncTERM application settings model. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_SETTINGS_H_
#define ST_SETTINGS_H_

#include "name_value_list.h"

#include <stdbool.h>
#include <stdint.h>

#define ST_SETTINGS_VALUE_MAX 1023
#define ST_SETTINGS_KDF_MAX 31
#define ST_SETTINGS_CUSTOM_FONT_MAX 210
#define ST_SETTINGS_FONT_PATH_COUNT 4

enum st_settings_font_path {
	ST_SETTINGS_FONT_8X8,
	ST_SETTINGS_FONT_8X14,
	ST_SETTINGS_FONT_8X16,
	ST_SETTINGS_FONT_12X20,
};

struct st_settings_font {
	char *name;
	char *path[ST_SETTINGS_FONT_PATH_COUNT];
};

ARRAY_LIST_DECLARE(st_settings_font_list, struct st_settings_font)

enum st_cursor_type {
	ST_CURSOR_DEFAULT,
	ST_CURSOR_BLINK_UNDERLINE,
	ST_CURSOR_SOLID_UNDERLINE,
	ST_CURSOR_BLINK_BLOCK,
	ST_CURSOR_SOLID_BLOCK,
};

struct st_settings {
	/* These path fields are owned; NULL represents an absent path. */
	char *modem_device;
	char *list_path;
	char *stored_list_path;
	char *tls_client_certificate;
	char *tls_client_key;
	char *theme_file;
	char *theme_package;
	char *sort_order;
	char *active_sort_profile;
	name_value_list_t web_downloads;
	name_value_list_t sort_profiles;
	st_settings_font_list_t custom_fonts;
	double scaling_factor;
	uint32_t modem_rate;
	uint32_t audio_output_modes;
	unsigned theme_frame_color;
	unsigned theme_text_color;
	unsigned theme_background_color;
	unsigned theme_inverse_color;
	unsigned theme_lightbar_color;
	unsigned theme_lightbar_background_color;
	int confirm_close;
	int startup_mode;
	int output_mode;
	int scrollback_lines;
	int prompt_save;
	int transfer_failure_keypress_timeout;
	int transfer_success_keypress_timeout;
	int custom_columns;
	int custom_rows;
	int custom_font_height;
	int custom_aspect_width;
	int custom_aspect_height;
	int window_width;
	int window_height;
	int left_justify;
	int blocky_scaling;
	int external_scaling;
	int default_cursor;
	char modem_init_string[ST_SETTINGS_VALUE_MAX + 1];
	char modem_dial_string[ST_SETTINGS_VALUE_MAX + 1];
	char terminal_type[ST_SETTINGS_VALUE_MAX + 1];
	char key_derivation_iterations[ST_SETTINGS_KDF_MAX + 1];
	bool invert_wheel;
	bool download_system_list;
};

extern struct st_settings st_settings;

struct st_app_paths;

void st_settings_init(struct st_settings *settings);
void st_settings_dispose(struct st_settings *settings);
/* Result must previously have been initialized; it is replaced on success. */
bool st_settings_copy(struct st_settings *result,
	const struct st_settings *source);

/*
 * The process-global settings store is bound to the supplied application
 * paths until shutdown. Missing files load defaults and are not created.
 * Save performs a locked read/merge/write transaction and reports
 * IO_ERROR_CONFLICT when the same field was changed differently elsewhere.
 */
int st_settings_load(const struct st_app_paths *paths);
int st_settings_reload(void);
int st_settings_save(void);
void st_settings_shutdown(void);

size_t st_settings_web_download_count(void);
const struct name_value *st_settings_web_download_at(size_t index);
/* Names passed to set or append must be valid INI key names. */
int st_settings_web_download_set(size_t index, const char *name,
    const char *url);
int st_settings_web_download_append(const char *name, const char *url);
int st_settings_web_download_remove(size_t index);
int st_settings_web_download_move(size_t from, size_t to);

size_t st_settings_sort_profile_count(void);
const struct name_value *st_settings_sort_profile_at(size_t index);
size_t st_settings_active_sort_profile(void);
int st_settings_sort_profile_select(size_t index);
/* Names passed to add or update must be valid INI key names. */
int st_settings_sort_profile_add(size_t index, const char *name,
	const char *order);
int st_settings_sort_profile_update(size_t index, const char *name,
	const char *order);
int st_settings_sort_profile_remove(size_t index);

size_t st_settings_font_count(void);
const struct st_settings_font *st_settings_font_at(size_t index);
int st_settings_font_insert(size_t index, const char *name);
int st_settings_font_rename(size_t index, const char *name);
int st_settings_font_set_path(size_t index, enum st_settings_font_path slot,
	const char *path);
int st_settings_font_remove(size_t index);

#endif
