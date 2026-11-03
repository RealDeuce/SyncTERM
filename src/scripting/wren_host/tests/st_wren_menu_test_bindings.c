/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Compile the actual binder with narrowly controlled native return points.
 * No replacement or fault controls are linked into the application. */
#include "wren_host_internal.h"
#include "st_wren_menu_test_bindings.h"
#include "st_settings.h"
#include "st_unicode_text.h"
#include "st_theme.h"

#include <stdio.h>

int st_wren_test_font_insert_status;
int st_wren_test_font_rename_status;
int st_wren_test_font_activation_status;
int st_wren_test_settings_save_status;
int st_wren_test_emoji_status;
unsigned st_wren_test_activation_calls;
unsigned st_wren_test_font_save_calls;
bool st_wren_test_sort_update_failure;
bool st_wren_test_prepare_save_failure;
int st_wren_test_list_open_status;
int st_wren_test_list_sort_status;
int st_wren_test_model_build_status;
const struct st_bbs_diagnostic *st_wren_test_diagnostic;
int st_wren_test_diagnostic_copy_status;
int st_wren_test_theme_catalog_status;
int st_wren_test_theme_create_status;
int st_wren_test_theme_open_status;
int st_wren_test_theme_copy_status;
int st_wren_test_theme_refresh_status;
const char *st_wren_test_theme_warning;
int st_wren_test_theme_activate_status;
int st_wren_test_theme_save_result;
int st_wren_test_theme_restore_status;
unsigned st_wren_test_theme_save_calls;
unsigned st_wren_test_theme_activate_calls;
bool st_wren_test_theme_settings_copy_failure;

static bool
test_settings_copy(struct st_settings *result, const struct st_settings *source)
{
	return !st_wren_test_theme_settings_copy_failure && st_settings_copy(result, source);
}

static int
test_theme_catalog(struct st_theme_manager *manager, char *error, size_t size)
{
	return st_wren_test_theme_catalog_status != 0 ? st_wren_test_theme_catalog_status
	    : st_theme_catalog_refresh(manager, error, size);
}

static int
test_theme_create(struct st_theme_document **result, struct st_theme_manager *manager,
	char *error, size_t size)
{
	return st_wren_test_theme_create_status != 0 ? st_wren_test_theme_create_status
	    : st_theme_document_create(result, manager, error, size);
}

static int
test_theme_open(struct st_theme_document **result, struct st_theme_manager *manager,
	const char *filename, char *error, size_t size)
{
	return st_wren_test_theme_open_status != 0 ? st_wren_test_theme_open_status
	    : st_theme_document_open(result, manager, filename, error, size);
}

static int
test_theme_copy(struct st_theme_document **result, struct st_theme_manager *manager,
	const char *package, char *error, size_t size)
{
	return st_wren_test_theme_copy_status != 0 ? st_wren_test_theme_copy_status
	    : st_theme_cloud_copy_document(result, manager, package, error, size);
}

static int
test_theme_refresh(struct st_theme_manager *manager, bool fetch,
	const char *selected, char *warning, size_t size)
{
	if (st_wren_test_theme_warning != NULL) {
		(void)snprintf(warning, size, "%s", st_wren_test_theme_warning);
		return st_wren_test_theme_refresh_status;
	}
	return st_wren_test_theme_refresh_status != 0 ? st_wren_test_theme_refresh_status
	    : st_theme_cloud_refresh(manager, fetch, selected, warning, size);
}

static enum st_theme_document_error
test_theme_save(struct st_theme_document *document, const char *filename,
	bool force, char *error, size_t size)
{
	++st_wren_test_theme_save_calls;
	return st_wren_test_theme_save_result != 0
	    ? (enum st_theme_document_error)st_wren_test_theme_save_result
	    : st_theme_document_save(document, filename, force, error, size);
}

static int
test_theme_activate(struct st_theme_document *document, char *error, size_t size)
{
	++st_wren_test_theme_activate_calls;
	return st_wren_test_theme_activate_status != 0 ? st_wren_test_theme_activate_status
	    : st_theme_document_activate(document, error, size);
}

static int
test_theme_restore(struct st_theme_manager *manager, const struct st_settings *settings,
	char *error, size_t size)
{
	return st_wren_test_theme_restore_status != 0 ? st_wren_test_theme_restore_status
	    : st_theme_apply_settings(manager, settings, error, size);
}

static int
test_settings_save_current(void)
{
	return st_wren_test_settings_save_status != 0 ? st_wren_test_settings_save_status
	    : st_settings_save();
}

static int
test_diagnostic_normalize(char **result, size_t *result_size,
	const void *bytes, size_t size, bool *changed)
{
	return st_wren_test_diagnostic_copy_status != 0
	    ? st_wren_test_diagnostic_copy_status
	    : st_unicode_text_normalize(result, result_size, bytes, size, changed);
}

static int
test_list_open(st_bbs_list_t **result, const struct st_app_paths *paths,
	const void *password, size_t size)
{
	return st_wren_test_list_open_status != 0 ? st_wren_test_list_open_status
	    : st_bbs_list_open(result, paths, password, size);
}

static int
test_list_sort(st_bbs_list_t *list, const int *fields, size_t count)
{
	return st_wren_test_list_sort_status != 0 ? st_wren_test_list_sort_status
	    : st_bbs_list_sort(list, fields, count);
}

static int
test_model_build(struct st_wren_bbs_model *model, const st_bbs_list_t *list)
{
	int status = st_wren_bbs_model_build(model, list);
	return status == 0 ? st_wren_test_model_build_status : status;
}

static const struct st_bbs_diagnostic *
test_diagnostic_at(const st_bbs_list_t *list, size_t index)
{
	return st_wren_test_diagnostic != NULL ? st_wren_test_diagnostic
	    : st_bbs_list_diagnostic_at(list, index);
}

static bool
test_prepare_save(struct st_wren_bbs_model *model,
	const struct st_wren_bbs_record *record)
{
	return !st_wren_test_prepare_save_failure
	    && st_wren_bbs_model_prepare_save(model, record);
}

static int
test_font_insert(struct st_settings_font_draft *draft, size_t index,
	const char *name)
{
	return st_wren_test_font_insert_status != 0
	    ? st_wren_test_font_insert_status
	    : st_settings_font_draft_insert(draft, index, name);
}

static int
test_font_rename(struct st_settings_font_draft *draft, size_t index,
	const char *name)
{
	return st_wren_test_font_rename_status != 0
	    ? st_wren_test_font_rename_status
	    : st_settings_font_draft_rename(draft, index, name);
}

static int
test_font_save(struct st_settings_font_draft *draft)
{
	++st_wren_test_font_save_calls;
	return st_settings_font_draft_save(draft);
}

static int
test_sort_update(size_t index, const char *name, const char *order)
{
	int status = st_settings_sort_profile_update(index, name, order);
	/* The native API can fail after publishing the changed collection. */
	return status == 0 && st_wren_test_sort_update_failure ? -1 : status;
}

static int
test_font_activation(struct st_wren_host *host)
{
	++st_wren_test_activation_calls;
	return st_wren_test_font_activation_status != 0
	    ? st_wren_test_font_activation_status : st_wren_reload_custom_fonts(host);
}

static int
test_settings_save(const struct st_settings *settings)
{
	return st_wren_test_settings_save_status != 0
	    ? st_wren_test_settings_save_status : st_settings_save_value(settings);
}

static int
test_color_emoji(st_console_t *console, bool enabled)
{
	return st_wren_test_emoji_status != 0
	    ? st_wren_test_emoji_status : st_console_set_color_emoji(console, enabled);
}

#define st_wren_bbs_model_prepare_save test_prepare_save
#define st_theme_catalog_refresh test_theme_catalog
#define st_theme_document_create test_theme_create
#define st_theme_document_open test_theme_open
#define st_theme_cloud_copy_document test_theme_copy
#define st_theme_cloud_refresh test_theme_refresh
#define st_theme_document_save test_theme_save
#define st_theme_document_activate test_theme_activate
#define st_theme_apply_settings test_theme_restore
#define st_settings_save test_settings_save_current
#define st_settings_copy test_settings_copy
#define st_bbs_list_open test_list_open
#define st_bbs_list_sort test_list_sort
#define st_wren_bbs_model_build test_model_build
#define st_bbs_list_diagnostic_at test_diagnostic_at
#define st_unicode_text_normalize test_diagnostic_normalize
#define st_settings_font_draft_save test_font_save
#define st_settings_sort_profile_update test_sort_update
#define st_settings_font_draft_insert test_font_insert
#define st_settings_font_draft_rename test_font_rename
#define st_wren_reload_custom_fonts test_font_activation
#define st_settings_save_value test_settings_save
#define st_console_set_color_emoji test_color_emoji
#include "../wren_bind_menu.c"
