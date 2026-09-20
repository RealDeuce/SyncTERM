/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_settings.h"

#include "ini.h"
#include "string_compat.h"
#include "string_util.h"

#include <stdlib.h>
#include <string.h>

struct st_settings st_settings;

ARRAY_LIST_DEFINE(st_settings_font_list, struct st_settings_font)

static const struct name_value default_sort_profiles[] = {
	{ "Name", "29,1" },
	{ "Last Connected", "29,3,1" },
	{ "Most Called", "29,4,1" },
	{ "Date Added", "29,2,1" },
};

static bool
ini_name_valid(const char *name, const char *forbidden, bool allow_empty)
{
	size_t length;

	if (name == NULL)
		return false;
	length = strlen(name);
	if (length == 0)
		return allow_empty;
	return !ascii_is_space((unsigned char)name[0])
	    && !ascii_is_space((unsigned char)name[length - 1])
	    && string_span_is_printable_ascii(name, length)
	    && strpbrk(name, forbidden) == NULL;
}

static bool
copy_path(char **result, const char *source)
{
	if (source == NULL) {
		*result = NULL;
		return true;
	}
	*result = strdup(source);
	return *result != NULL;
}

static bool
copy_web_downloads(name_value_list_t *result, name_value_list_t source)
{
	size_t i;

	for (i = 0; i < name_value_list_count(source); ++i) {
		if (!name_value_list_append(result, source[i]->name,
		    source[i]->value))
			return false;
	}
	return true;
}

static void
font_free(struct st_settings_font *font)
{
	if (font == NULL)
		return;
	free(font->name);
	for (size_t i = 0; i < ST_SETTINGS_FONT_PATH_COUNT; ++i)
		free(font->path[i]);
	free(font);
}

static struct st_settings_font *
font_copy(const struct st_settings_font *source)
{
	struct st_settings_font *copy = calloc(1, sizeof(*copy));

	if (copy == NULL)
		return NULL;
	copy->name = strdup(source->name);
	if (copy->name == NULL)
		goto failed;
	for (size_t i = 0; i < ST_SETTINGS_FONT_PATH_COUNT; ++i) {
		if (source->path[i] == NULL)
			continue;
		copy->path[i] = strdup(source->path[i]);
		if (copy->path[i] == NULL)
			goto failed;
	}
	return copy;

failed:
	font_free(copy);
	return NULL;
}

static bool
copy_fonts(st_settings_font_list_t *result, st_settings_font_list_t source)
{
	for (size_t i = 0; i < st_settings_font_list_count(source); ++i) {
		struct st_settings_font *copy = font_copy(source[i]);

		if (copy == NULL || !st_settings_font_list_append(result, copy)) {
			font_free(copy);
			return false;
		}
	}
	return true;
}

static void
free_fonts(st_settings_font_list_t *fonts)
{
	for (size_t i = 0; i < st_settings_font_list_count(*fonts); ++i)
		font_free((*fonts)[i]);
	st_settings_font_list_free_storage(fonts);
}

static bool
append_default_sort_profiles(name_value_list_t *profiles)
{
	for (size_t i = 0;
	    i < sizeof(default_sort_profiles) / sizeof(default_sort_profiles[0]);
	    ++i) {
		if (!name_value_list_append(profiles, default_sort_profiles[i].name,
		    default_sort_profiles[i].value))
			return false;
	}
	return true;
}

void
st_settings_init(struct st_settings *settings)
{
	if (settings != NULL)
		memset(settings, 0, sizeof(*settings));
}

void
st_settings_dispose(struct st_settings *settings)
{
	if (settings == NULL)
		return;
	free(settings->modem_device);
	free(settings->list_path);
	free(settings->stored_list_path);
	free(settings->tls_client_certificate);
	free(settings->tls_client_key);
	free(settings->theme_file);
	free(settings->theme_package);
	free(settings->sort_order);
	free(settings->active_sort_profile);
	name_value_list_free(&settings->web_downloads);
	name_value_list_free(&settings->sort_profiles);
	free_fonts(&settings->custom_fonts);
	memset(settings, 0, sizeof(*settings));
}

bool
st_settings_copy(struct st_settings *result,
	const struct st_settings *source)
{
	struct st_settings copy;

	if (result == NULL || source == NULL)
		return false;
	if (result == source)
		return true;
	copy = *source;
	copy.modem_device = NULL;
	copy.list_path = NULL;
	copy.stored_list_path = NULL;
	copy.tls_client_certificate = NULL;
	copy.tls_client_key = NULL;
	copy.theme_file = NULL;
	copy.theme_package = NULL;
	copy.sort_order = NULL;
	copy.active_sort_profile = NULL;
	copy.web_downloads = NULL;
	copy.sort_profiles = NULL;
	copy.custom_fonts = NULL;
	if (!copy_web_downloads(&copy.web_downloads, source->web_downloads)
	    || !copy_web_downloads(&copy.sort_profiles, source->sort_profiles)
	    || !copy_fonts(&copy.custom_fonts, source->custom_fonts)
	    || !copy_path(&copy.modem_device, source->modem_device)
	    || !copy_path(&copy.list_path, source->list_path)
	    || !copy_path(&copy.stored_list_path, source->stored_list_path)
	    || !copy_path(&copy.tls_client_certificate,
	    source->tls_client_certificate)
	    || !copy_path(&copy.tls_client_key, source->tls_client_key)
	    || !copy_path(&copy.theme_file, source->theme_file)
	    || !copy_path(&copy.theme_package, source->theme_package)
	    || !copy_path(&copy.sort_order, source->sort_order)
	    || !copy_path(&copy.active_sort_profile,
	    source->active_sort_profile)) {
		st_settings_dispose(&copy);
		return false;
	}
	st_settings_dispose(result);
	*result = copy;
	return true;
}

size_t
st_settings_sort_profile_count(void)
{
	return name_value_list_count(st_settings.sort_profiles);
}

const struct name_value *
st_settings_sort_profile_at(size_t index)
{
	return index < st_settings_sort_profile_count()
	    ? st_settings.sort_profiles[index] : NULL;
}

size_t
st_settings_active_sort_profile(void)
{
	for (size_t i = 0; i < st_settings_sort_profile_count(); ++i) {
		if (st_settings.active_sort_profile != NULL
		    && ascii_case_compare(st_settings.sort_profiles[i]->name,
		    st_settings.active_sort_profile) == 0)
			return i;
	}
	return 0;
}

int
st_settings_sort_profile_select(size_t index)
{
	const struct name_value *profile = st_settings_sort_profile_at(index);
	char *name;
	char *order;

	if (profile == NULL)
		return -1;
	name = strdup(profile->name);
	order = strdup(profile->value);
	if (name == NULL || order == NULL) {
		free(name);
		free(order);
		return -1;
	}
	free(st_settings.active_sort_profile);
	free(st_settings.sort_order);
	st_settings.active_sort_profile = name;
	st_settings.sort_order = order;
	return 0;
}

int
st_settings_sort_profile_add(size_t index, const char *name,
	const char *order)
{
	struct name_value *profile;

	if (!ini_key_name_valid(name) || order == NULL
	    || index > st_settings_sort_profile_count())
		return -1;
	profile = name_value_create(name, order);
	if (profile == NULL)
		return -1;
	if (!name_value_array_insert(&st_settings.sort_profiles, index, profile)) {
		name_value_free_item(profile);
		return -1;
	}
	return 0;
}

int
st_settings_sort_profile_update(size_t index, const char *name,
	const char *order)
{
	struct name_value *replacement;
	struct name_value *old;
	bool active;

	if (index >= st_settings_sort_profile_count()
	    || !ini_key_name_valid(name)
	    || order == NULL)
		return -1;
	old = st_settings.sort_profiles[index];
	active = st_settings.active_sort_profile != NULL
	    && ascii_case_compare(old->name, st_settings.active_sort_profile) == 0;
	replacement = name_value_create(name, order);
	if (replacement == NULL)
		return -1;
	st_settings.sort_profiles[index] = replacement;
	name_value_free_item(old);
	if (active && st_settings_sort_profile_select(index) < 0)
		return -1;
	return 0;
}

int
st_settings_sort_profile_remove(size_t index)
{
	size_t count = st_settings_sort_profile_count();
	bool active;

	if (index >= count)
		return -1;
	active = index == st_settings_active_sort_profile();
	if (!name_value_list_remove(st_settings.sort_profiles, index))
		return -1;
	if (count == 1) {
		if (!append_default_sort_profiles(&st_settings.sort_profiles))
			return -1;
		return st_settings_sort_profile_select(0);
	}
	if (active) {
		if (index >= count - 1)
			--index;
		return st_settings_sort_profile_select(index);
	}
	return 0;
}

size_t
st_settings_font_count(void)
{
	return st_settings_font_list_count(st_settings.custom_fonts);
}

const struct st_settings_font *
st_settings_font_at(size_t index)
{
	return index < st_settings_font_count()
	    ? st_settings.custom_fonts[index] : NULL;
}

int
st_settings_font_insert(size_t index, const char *name)
{
	struct st_settings_font *font;

	if (!ini_name_valid(name, "[]", false)
	    || index > st_settings_font_count()
	    || st_settings_font_count() >= ST_SETTINGS_CUSTOM_FONT_MAX)
		return -1;
	font = calloc(1, sizeof(*font));
	if (font == NULL || (font->name = strdup(name)) == NULL
	    || !st_settings_font_list_insert(&st_settings.custom_fonts, index,
	    font)) {
		font_free(font);
		return -1;
	}
	return 0;
}

int
st_settings_font_rename(size_t index, const char *name)
{
	char *replacement;

	if (index >= st_settings_font_count()
	    || !ini_name_valid(name, "[]", true))
		return -1;
	replacement = strdup(name);
	if (replacement == NULL)
		return -1;
	free(st_settings.custom_fonts[index]->name);
	st_settings.custom_fonts[index]->name = replacement;
	return 0;
}

int
st_settings_font_set_path(size_t index, enum st_settings_font_path slot,
	const char *path)
{
	char *replacement = NULL;

	if (index >= st_settings_font_count()
	    || slot < 0 || slot >= ST_SETTINGS_FONT_PATH_COUNT)
		return -1;
	if (path != NULL && path[0] != '\0' && (replacement = strdup(path)) == NULL)
		return -1;
	free(st_settings.custom_fonts[index]->path[slot]);
	st_settings.custom_fonts[index]->path[slot] = replacement;
	return 0;
}

int
st_settings_font_remove(size_t index)
{
	struct st_settings_font *font;

	if (index >= st_settings_font_count())
		return -1;
	font = st_settings_font_list_take(st_settings.custom_fonts, index);
	font_free(font);
	return 0;
}
