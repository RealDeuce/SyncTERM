/* Persistent SyncTERM settings with field-level three-way merging. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_settings.h"

#include "st_settings_internal.h"
#include "st_unicode_compare.h"
#include "st_emoji_catalog.h"

#include "filesystem.h"
#include "ini.h"
#include "io_error.h"
#include "path.h"
#include "st_app_paths.h"
#include "st_file_lock.h"
#include "string_compat.h"
#include "string_util.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t recent_emoji[ST_SETTINGS_RECENT_EMOJI_MAX];
static size_t recent_emoji_count;
static bool recent_emoji_loaded;

static void
emoji_append(size_t *ids, size_t *count, size_t id)
{
	if (id >= st_emoji_entry_count || *count >= ST_SETTINGS_RECENT_EMOJI_MAX)
		return;
	for (size_t i = 0; i < *count; i++)
		if (ids[i] == id)
			return;
	ids[(*count)++] = id;
}

static void
emoji_read(ini_document_t *document, size_t *ids, size_t *count)
{
	const ini_section_t *section = ini_document_find_section(document, "RecentEmoji");
	if (section == NULL)
		return;
	for (size_t i = 1; i <= ST_SETTINGS_RECENT_EMOJI_MAX; i++) {
		char key[sizeof(size_t) * CHAR_BIT + 1];
		(void)snprintf(key, sizeof(key), "%zu", i);
		emoji_append(ids, count, st_emoji_find(ini_section_get_string(section, key, "")));
	}
}

static bool
names_equal(const char *left, const char *right)
{
	bool equal;

	return st_unicode_caseless_equal(&equal, left, strlen(left), right,
	    strlen(right)) == 0 && equal;
}

static const char *
section_suffix(const char *name, const char *prefix)
{
	const char *colon = strchr(name, ':');
	bool equal;

	if (colon == NULL || st_unicode_caseless_equal(&equal, name,
	    (size_t)(colon - name), prefix, strlen(prefix)) != 0 || !equal)
		return NULL;
	return colon + 1;
}

#ifdef ST_SETTINGS_STORE_TEST_COPY
#include "st_settings_test_copy.h"
#define STORE_SETTINGS_COPY(result, source) \
	st_settings_test_copy((result), (source))
#else
#define STORE_SETTINGS_COPY(result, source) \
	st_settings_copy((result), (source))
#endif

static char *settings_path;
static char *cached_system_list;
static char *personal_list_path;
static ini_document_t *baseline_document;
static struct st_settings baseline_settings;
static bool baseline_initialized;

static const char *screen_modes[] = {
	"Current", "80x25", "LCD80x25", "80x28", "80x30", "80x43",
	"80x50", "80x60", "132x37", "132x52", "132x25", "132x28",
	"132x30", "132x34", "132x43", "132x50", "132x60", "C64",
	"C128-40col", "C128-80col", "Atari", "Atari-XEP80", "Custom",
	"EGA80x25", "VGA80x25", "Prestel", "BBCMicro7", "AtariST40x25",
	"AtariST80x25", "AtariST80x25Mono", "Amiga640x200", NULL
};

static const char *output_modes[] = {
	"Autodetect", "Curses", "Curses437", "CursesAscii", "ANSI", "X11",
	"X11Fullscreen", "WinConsole", "WinConsoleFullscreen", "SDL",
	"SDLFullscreen", "GDI", "GDIFullscreen", "Retro", "Wayland",
	"WaylandFullscreen", "Quartz", "QuartzFullscreen", NULL
};

static const char *cursor_types[] = {
	"Default", "BlinkingUnderline", "SolidUnderline", "BlinkingBlock",
	"SolidBlock", NULL
};

static const char *colors[] = {
	"Black", "Blue", "Green", "Cyan", "Red", "Magenta", "Brown",
	"LightGray", "DarkGray", "LightBlue", "LightGreen", "LightCyan",
	"LightRed", "LightMagenta", "Yellow", "White", "Default", NULL
};

static const char *background_colors[] = {
	"Black", "Blue", "Green", "Cyan", "Red", "Magenta", "Brown",
	"LightGray", "Default", NULL
};

#define ENUM_COUNT(table) (sizeof(table) / sizeof((table)[0]) - 1)
#define ENUM_NAMES(table) (table), ENUM_COUNT(table)

static const struct ini_bit_name audio_modes[] = {
	{ 1U << 6, "CoreAudio" },
	{ 1U << 0, "WASAPI" },
	{ 1U << 0, "WaveOut" },
	{ 1U << 4, "PortAudio" },
	{ 1U << 7, "PipeWire" },
	{ 1U << 3, "SDL" },
	{ 1U << 5, "PulseAudio" },
	{ 1U << 1, "ALSA" },
	{ 1U << 2, "OSS" },
};

struct setting_key {
	const char *section;
	const char *key;
};

static const struct setting_key known_keys[] = {
	{ "SyncTERM", "ConfirmClose" },
	{ "SyncTERM", "PromptSave" },
	{ "SyncTERM", "ScreenMode" },
	{ "SyncTERM", "OutputMode" },
	{ "SyncTERM", "DefaultCursor" },
	{ "SyncTERM", "AudioModes" },
	{ "SyncTERM", "ScrollBackLines" },
	{ "SyncTERM", "TransferSuccessKeypressTimeout" },
	{ "SyncTERM", "TransferFailureKeypressTimeout" },
	{ "SyncTERM", "ModemDevice" },
	{ "SyncTERM", "ModemComRate" },
	{ "SyncTERM", "ModemInit" },
	{ "SyncTERM", "ModemDial" },
	{ "SyncTERM", "ListPath" },
	{ "SyncTERM", "TLSClientCertificate" },
	{ "SyncTERM", "TLSClientKey" },
	{ "SyncTERM", "TERM" },
	{ "SyncTERM", "ScalingFactor" },
	{ "SyncTERM", "BlockyScaling" },
	{ "SyncTERM", "ExternalScaling" },
	{ "SyncTERM", "InvertMouseWheel" },
	{ "SyncTERM", "ColorEmoji" },
	{ "SyncTERM", "LeftJustify" },
	{ "SyncTERM", "KeyDerivationIterations" },
	{ "SyncTERM", "CustomRows" },
	{ "SyncTERM", "CustomColumns" },
	{ "SyncTERM", "CustomFontHeight" },
	{ "SyncTERM", "CustomAspectWidth" },
	{ "SyncTERM", "CustomAspectHeight" },
	{ "SyncTERM", "WindowWidth" },
	{ "SyncTERM", "WindowHeight" },
	{ "SyncTERM", "ThemeFile" },
	{ "SyncTERM", "ThemePackage" },
	{ "SyncTERM", "SortOrder" },
	{ "SyncTERM", "ActiveSortProfile" },
	{ "ClassicTheme", "FrameColour" },
	{ "ClassicTheme", "TextColour" },
	{ "ClassicTheme", "BackgroundColour" },
	{ "ClassicTheme", "InverseColour" },
	{ "ClassicTheme", "LightbarColour" },
	{ "ClassicTheme", "LightbarBackgroundColour" },
};

static int
replace_string(char **result, const char *value)
{
	char *copy = value == NULL || value[0] == '\0' ? NULL : strdup(value);

	if (value != NULL && value[0] != '\0' && copy == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	free(*result);
	*result = copy;
	return 0;
}

static int
replace_text(char **result, const char *value)
{
	char *copy = strdup(value == NULL ? "" : value);

	if (copy == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	free(*result);
	*result = copy;
	return 0;
}

static int
bounded_int(const ini_section_t *section, const char *key, int fallback,
    int minimum, int maximum)
{
	return (int)ini_section_get_bounded_int64(section, key, fallback,
	    minimum, maximum);
}

static uint32_t
bounded_uint32(const ini_section_t *section, const char *key,
    uint32_t fallback)
{
	uint64_t value = ini_section_get_uint64(section, key, fallback);

	return value > UINT32_MAX ? fallback : (uint32_t)value;
}

static unsigned
bounded_enum(const ini_section_t *section, const char *key,
    const char *const *names, size_t name_count, unsigned fallback)
{
	size_t value = ini_section_get_enum_names(section, key, names, name_count,
	    fallback);

	return value < name_count ? (unsigned)value : fallback;
}

static unsigned
classic_color(ini_document_t *document, const char *key,
    const char *const *names, size_t name_count, unsigned fallback)
{
	ini_section_t *section = ini_document_find_section(document,
	    "ClassicTheme");

	if (section == NULL || !ini_section_has_key(section, key))
		section = ini_document_find_section(document, "UIFC");
	if (section == NULL)
		return fallback;
	return bounded_enum(section, key, names, name_count, fallback);
}

static void
load_classic_colors(struct st_settings *value, ini_document_t *document)
{
	value->theme_frame_color = classic_color(document, "FrameColour",
	    ENUM_NAMES(colors), 16);
	value->theme_text_color = classic_color(document, "TextColour",
	    ENUM_NAMES(colors), 16);
	value->theme_background_color = classic_color(document,
	    "BackgroundColour", ENUM_NAMES(background_colors), 8);
	value->theme_inverse_color = classic_color(document, "InverseColour",
	    ENUM_NAMES(background_colors), 8);
	value->theme_lightbar_color = classic_color(document, "LightbarColour",
	    ENUM_NAMES(colors), 16);
	value->theme_lightbar_background_color = classic_color(document,
	    "LightbarBackgroundColour", ENUM_NAMES(background_colors), 8);
}

static void
settings_defaults(struct st_settings *value, const char *personal_list)
{
	st_settings_init(value);
	value->stored_list_path = strdup(personal_list);
	value->list_path = strdup(personal_list);
	value->sort_order = strdup("29,5,1");
	value->active_sort_profile = strdup("Name");
	(void)name_value_list_append(&value->sort_profiles, "Name", "29,1");
	(void)name_value_list_append(&value->sort_profiles, "Last Connected",
	    "29,3,1");
	(void)name_value_list_append(&value->sort_profiles, "Most Called",
	    "29,4,1");
	(void)name_value_list_append(&value->sort_profiles, "Date Added",
	    "29,2,1");
	value->confirm_close = false;
	value->prompt_save = true;
	value->startup_mode = 0;
	value->output_mode = 0;
	value->audio_output_modes = UINT32_MAX;
	value->scrollback_lines = 2000;
	value->transfer_success_keypress_timeout = 0;
	value->transfer_failure_keypress_timeout = 60;
	value->custom_columns = 80;
	value->custom_rows = 25;
	value->custom_font_height = 16;
	value->custom_aspect_width = 4;
	value->custom_aspect_height = 3;
	value->blocky_scaling = true;
	value->color_emoji = true;
	value->theme_frame_color = 16;
	value->theme_text_color = 16;
	value->theme_background_color = 8;
	value->theme_inverse_color = 8;
	value->theme_lightbar_color = 16;
	value->theme_lightbar_background_color = 8;
	(void)strcpy(value->modem_init_string, "AT&F&C1&D2");
	(void)strcpy(value->modem_dial_string, "ATDT");
	(void)strcpy(value->terminal_type, "syncterm");
	(void)strcpy(value->key_derivation_iterations, "scrypt-N15");
}

static int
copy_ini_string(char **destination, const ini_section_t *section,
    const char *key)
{
	return replace_string(destination,
	    ini_section_get_string(section, key, ""));
}

static void
copy_fixed(char *destination, size_t size, const ini_section_t *section,
    const char *key, const char *fallback)
{
	const char *value = ini_section_get_string(section, key, fallback);

	if (strlen(value) >= size)
		value = fallback;
	(void)strcpy(destination, value);
}

static int
decode_settings(struct st_settings *value, ini_document_t *document,
    const char *personal_list, const char *system_cache)
{
	ini_section_t *section;
	ini_section_t *web;
	ini_section_t *sort_profiles;
	const char *list_path;
	bool have_sort_profiles = false;
	int status;

	settings_defaults(value, personal_list);
	if (value->stored_list_path == NULL || value->list_path == NULL
	    || value->sort_order == NULL || value->active_sort_profile == NULL
	    || name_value_list_count(value->sort_profiles) != 4)
		return IO_ERROR_OUT_OF_MEMORY;
	section = ini_document_find_section(document, "SyncTERM");
	if (section != NULL) {
		value->confirm_close = ini_section_get_bool(section, "ConfirmClose",
		    value->confirm_close);
		value->prompt_save = ini_section_get_bool(section, "PromptSave",
		    value->prompt_save);
		value->startup_mode = (int)ini_section_get_enum_names(section,
		    "VideoMode", ENUM_NAMES(screen_modes),
		    (size_t)value->startup_mode);
		value->startup_mode = (int)ini_section_get_enum_names(section,
		    "ScreenMode", ENUM_NAMES(screen_modes),
		    (size_t)value->startup_mode);
		value->output_mode = (int)ini_section_get_enum_names(section,
		    "OutputMode", ENUM_NAMES(output_modes),
		    (size_t)value->output_mode);
		value->default_cursor = (int)ini_section_get_enum_names(section,
		    "DefaultCursor", ENUM_NAMES(cursor_types),
		    (size_t)value->default_cursor);
		value->audio_output_modes = (uint32_t)ini_section_get_bitfield(section,
		    "AudioModes", audio_modes,
		    sizeof(audio_modes) / sizeof(audio_modes[0]),
		    value->audio_output_modes);
		value->scrollback_lines = bounded_int(section, "ScrollBackLines", 2000,
		    0, INT_MAX);
		value->transfer_success_keypress_timeout = bounded_int(section,
		    "TransferSuccessKeypressTimeout", 0, 0, INT_MAX);
		value->transfer_failure_keypress_timeout = bounded_int(section,
		    "TransferFailureKeypressTimeout", 60, 0, INT_MAX);
		value->custom_columns = bounded_int(section, "CustomColumns", 80, 1,
		    INT_MAX);
		value->custom_rows = bounded_int(section, "CustomRows", 25, 1, INT_MAX);
		value->custom_font_height = bounded_int(section, "CustomFontHeight", 16,
		    1, INT_MAX);
		value->custom_aspect_width = bounded_int(section, "CustomAspectWidth", 4,
		    1, INT_MAX);
		value->custom_aspect_height = bounded_int(section, "CustomAspectHeight",
		    3, 1, INT_MAX);
		value->window_width = bounded_int(section, "WindowWidth", 0, 0,
		    INT_MAX);
		value->window_height = bounded_int(section, "WindowHeight", 0, 0,
		    INT_MAX);
		value->scaling_factor = ini_section_get_double(section, "ScalingFactor",
		    0.0);
		if (value->scaling_factor < 0.0)
			value->scaling_factor = 0.0;
		value->blocky_scaling = ini_section_get_bool(section, "BlockyScaling",
		    true);
		value->external_scaling = ini_section_get_bool(section,
		    "ExternalScaling", false);
		value->color_emoji = ini_section_get_bool(section, "ColorEmoji",
		    value->color_emoji);
		value->invert_wheel = ini_section_get_bool(section, "InvertMouseWheel",
		    false);
		value->left_justify = ini_section_get_bool(section, "LeftJustify", false);
		value->modem_rate = bounded_uint32(section, "ModemComRate", 0);
		copy_fixed(value->modem_init_string, sizeof(value->modem_init_string),
		    section, "ModemInit", "AT&F&C1&D2");
		copy_fixed(value->modem_dial_string, sizeof(value->modem_dial_string),
		    section, "ModemDial", "ATDT");
		copy_fixed(value->terminal_type, sizeof(value->terminal_type), section,
		    "TERM", "syncterm");
		copy_fixed(value->key_derivation_iterations,
		    sizeof(value->key_derivation_iterations), section,
		    "KeyDerivationIterations", "scrypt-N15");
		status = copy_ini_string(&value->modem_device, section, "ModemDevice");
		if (status == 0)
			status = copy_ini_string(&value->tls_client_certificate, section,
			    "TLSClientCertificate");
		if (status == 0)
			status = copy_ini_string(&value->tls_client_key, section,
			    "TLSClientKey");
		if (status == 0)
			status = copy_ini_string(&value->theme_file, section, "ThemeFile");
		if (status == 0)
			status = copy_ini_string(&value->theme_package, section,
			    "ThemePackage");
		if (status == 0 && ini_section_has_key(section, "SortOrder"))
			status = replace_text(&value->sort_order,
			    ini_section_get_string(section, "SortOrder", ""));
		if (status == 0 && ini_section_has_key(section,
		    "ActiveSortProfile"))
			status = replace_text(&value->active_sort_profile,
			    ini_section_get_string(section, "ActiveSortProfile", ""));
		if (status < 0)
			return status;
		list_path = ini_section_get_string(section, "ListPath", personal_list);
		if (replace_string(&value->stored_list_path, list_path) < 0)
			return IO_ERROR_OUT_OF_MEMORY;
		if (ascii_case_starts_with(list_path, "http://")
		    || ascii_case_starts_with(list_path, "https://")) {
			value->download_system_list = true;
			list_path = system_cache;
		}
		if (replace_string(&value->list_path, list_path) < 0)
			return IO_ERROR_OUT_OF_MEMORY;
		if (value->theme_package != NULL) {
			free(value->theme_file);
			value->theme_file = NULL;
		}
	}
	load_classic_colors(value, document);
	web = ini_document_find_section(document, "WebLists");
	if (web != NULL) {
		status = ini_section_copy_pairs(web, &value->web_downloads);
		if (status < 0)
			return status;
	}
	sort_profiles = ini_document_find_section(document, "SortProfiles");
	if (sort_profiles != NULL && ini_section_count(sort_profiles) != 0) {
		name_value_list_free(&value->sort_profiles);
		status = ini_section_copy_pairs(sort_profiles, &value->sort_profiles);
		if (status < 0)
			return status;
		have_sort_profiles = true;
	}
	if (have_sort_profiles) {
		bool active_found = false;

		for (size_t i = 0;
		    i < name_value_list_count(value->sort_profiles); ++i) {
			if (names_equal(value->sort_profiles[i]->name,
			    value->active_sort_profile)) {
				active_found = true;
				break;
			}
		}
		if (!active_found && name_value_list_count(value->sort_profiles) != 0) {
			status = replace_string(&value->active_sort_profile,
			    value->sort_profiles[0]->name);
			if (status < 0)
				return status;
		}
	}
	else {
		size_t active = SIZE_MAX;

		for (size_t i = 0;
		    i < name_value_list_count(value->sort_profiles); ++i) {
			if (strcmp(value->sort_profiles[i]->value,
			    value->sort_order) == 0) {
				active = i;
				break;
			}
		}
		if (active == SIZE_MAX && value->sort_order[0] != '\0'
		    && strcmp(value->sort_order, "0") != 0) {
			if (!name_value_list_append(&value->sort_profiles, "Custom",
			    value->sort_order))
				return IO_ERROR_OUT_OF_MEMORY;
		}
		if (active != SIZE_MAX) {
			status = replace_string(&value->active_sort_profile,
			    value->sort_profiles[active]->name);
			if (status < 0)
				return status;
		}
	}
	for (size_t i = 0; i < ini_document_section_count(document); ++i) {
		ini_section_t *font_section = ini_document_section_at(document, i);
		const char *name = ini_section_name(font_section);
		const char *font_name = section_suffix(name, "Font");
		static const char *const keys[] = {
			"Path8x8", "Path8x14", "Path8x16", "Path12x20"
		};

		if (font_name == NULL)
			continue;
		if (!st_settings_font_name_valid(font_name))
			continue;
		{
			bool duplicate = false;

			for (size_t existing = 0;
			    existing < st_settings_font_list_count(value->custom_fonts);
			    ++existing) {
				if (names_equal(value->custom_fonts[existing]->name,
				    font_name)) {
					duplicate = true;
					break;
				}
			}
			if (duplicate)
				continue;
		}
		if (st_settings_font_list_count(value->custom_fonts)
		    >= ST_SETTINGS_CUSTOM_FONT_MAX)
			return IO_ERROR_OUT_OF_RANGE;
		{
			struct st_settings_font *font = calloc(1, sizeof(*font));

			if (font == NULL || (font->name = strdup(font_name)) == NULL
			    || !st_settings_font_list_append(&value->custom_fonts, font)) {
				if (font != NULL) {
					free(font->name);
					free(font);
				}
				return IO_ERROR_OUT_OF_MEMORY;
			}

			for (size_t path = 0;
			    path < ST_SETTINGS_FONT_PATH_COUNT; ++path) {
				const char *text = ini_section_get_string(font_section,
				    keys[path], "");

				if (text[0] != '\0'
				    && (font->path[path] = strdup(text)) == NULL)
					return IO_ERROR_OUT_OF_MEMORY;
			}
		}
	}
	return 0;
}

static int
ensure_section(ini_document_t *document, const char *name,
    ini_section_t **result)
{
	*result = ini_document_find_section(document, name);
	if (*result != NULL)
		return 0;
	return ini_document_add_section(document, name, &ini_default_style,
	    result);
}

#define SET(call) do { status = (call); if (status < 0) return status; } while (0)

static unsigned
valid_color(unsigned value, size_t count, unsigned fallback)
{
	return value < count ? value : fallback;
}

static int
set_classic_colors(ini_document_t *document, const struct st_settings *value)
{
	ini_section_t *classic;
	int status;

	SET(ensure_section(document, "ClassicTheme", &classic));
	SET(ini_section_set_enum_names(classic, "FrameColour", ENUM_NAMES(colors),
	    valid_color(value->theme_frame_color, ENUM_COUNT(colors), 16),
	    &ini_default_style));
	SET(ini_section_set_enum_names(classic, "TextColour", ENUM_NAMES(colors),
	    valid_color(value->theme_text_color, ENUM_COUNT(colors), 16),
	    &ini_default_style));
	SET(ini_section_set_enum_names(classic, "BackgroundColour",
	    ENUM_NAMES(background_colors),
	    valid_color(value->theme_background_color,
	        ENUM_COUNT(background_colors), 8), &ini_default_style));
	SET(ini_section_set_enum_names(classic, "InverseColour",
	    ENUM_NAMES(background_colors),
	    valid_color(value->theme_inverse_color, ENUM_COUNT(background_colors),
	        8), &ini_default_style));
	SET(ini_section_set_enum_names(classic, "LightbarColour",
	    ENUM_NAMES(colors), valid_color(value->theme_lightbar_color,
	        ENUM_COUNT(colors), 16), &ini_default_style));
	SET(ini_section_set_enum_names(classic, "LightbarBackgroundColour",
	    ENUM_NAMES(background_colors),
	    valid_color(value->theme_lightbar_background_color,
	        ENUM_COUNT(background_colors), 8), &ini_default_style));
	return 0;
}

static int
migrate_classic_colors(ini_document_t *document)
{
	struct st_settings value = {0};
	ini_section_t *legacy;
	int status;

	load_classic_colors(&value, document);
	status = set_classic_colors(document, &value);
	if (status < 0)
		return status;
	legacy = ini_document_find_section(document, "UIFC");
	if (legacy != NULL)
		(void)ini_document_remove_section(document, legacy);
	return 0;
}

static int
encode_fonts(ini_document_t *document, st_settings_font_list_t fonts)
{
	ini_section_t *section;

	for (size_t i = ini_document_section_count(document); i > 0; --i) {
		ini_section_t *candidate = ini_document_section_at(document, i - 1);

		if (section_suffix(ini_section_name(candidate), "Font") != NULL)
			(void)ini_document_remove_section(document, candidate);
	}
	for (size_t i = 0; i < st_settings_font_list_count(fonts); ++i) {
		static const char *const keys[] = {
			"Path8x8", "Path8x14", "Path8x16", "Path12x20"
		};
		const struct st_settings_font *font = fonts[i];
		char *section_name;
		size_t name_length = strlen(font->name);
		int status;

		section_name = malloc(name_length + sizeof("Font:"));
		if (section_name == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		memcpy(section_name, "Font:", 5);
		memcpy(section_name + 5, font->name, name_length + 1);
		status = ini_document_add_section(document, section_name,
		    &ini_default_style, &section);
		free(section_name);
		if (status < 0)
			return status;
		for (size_t path = 0; path < ST_SETTINGS_FONT_PATH_COUNT; ++path) {
			if (font->path[path] == NULL)
				continue;
			status = ini_section_set_string(section, keys[path],
			    font->path[path], &ini_default_style);
			if (status < 0)
				return status;
		}
	}
	return 0;
}

static int
encode_settings(ini_document_t *document, const struct st_settings *value)
{
	ini_section_t *section;
	ini_section_t *web;
	ini_section_t *profiles;
	name_value_list_t old_pairs = NULL;
	size_t i;
	int status;

	SET(ensure_section(document, "SyncTERM", &section));
	SET(ini_section_set_bool(section, "ConfirmClose", value->confirm_close,
	    &ini_default_style));
	SET(ini_section_set_bool(section, "PromptSave", value->prompt_save,
	    &ini_default_style));
	SET(ini_section_set_enum_names(section, "ScreenMode", ENUM_NAMES(screen_modes),
	    (size_t)value->startup_mode, &ini_default_style));
	SET(ini_section_set_enum_names(section, "OutputMode", ENUM_NAMES(output_modes),
	    (size_t)value->output_mode, &ini_default_style));
	SET(ini_section_set_enum_names(section, "DefaultCursor", ENUM_NAMES(cursor_types),
	    (size_t)value->default_cursor, &ini_default_style));
	SET(ini_section_set_bitfield(section, "AudioModes", audio_modes,
	    sizeof(audio_modes) / sizeof(audio_modes[0]), value->audio_output_modes,
	    &ini_default_style));
#define SET_INT(key, member) SET(ini_section_set_int64(section, key, \
	    value->member, &ini_default_style))
	SET_INT("ScrollBackLines", scrollback_lines);
	SET_INT("TransferSuccessKeypressTimeout", transfer_success_keypress_timeout);
	SET_INT("TransferFailureKeypressTimeout", transfer_failure_keypress_timeout);
	SET(ini_section_set_uint64(section, "ModemComRate", value->modem_rate,
	    &ini_default_style));
	SET_INT("CustomColumns", custom_columns);
	SET_INT("CustomRows", custom_rows);
	SET_INT("CustomFontHeight", custom_font_height);
	SET_INT("CustomAspectWidth", custom_aspect_width);
	SET_INT("CustomAspectHeight", custom_aspect_height);
	SET_INT("WindowWidth", window_width);
	SET_INT("WindowHeight", window_height);
#undef SET_INT
#define SET_STRING(key, member) SET(ini_section_set_string(section, key, \
	    value->member == NULL ? "" : value->member, &ini_default_style))
	SET_STRING("ModemDevice", modem_device);
	SET_STRING("ListPath", stored_list_path);
	SET_STRING("TLSClientCertificate", tls_client_certificate);
	SET_STRING("TLSClientKey", tls_client_key);
	SET_STRING("ThemeFile", theme_file);
	SET_STRING("ThemePackage", theme_package);
	SET_STRING("SortOrder", sort_order);
	SET_STRING("ActiveSortProfile", active_sort_profile);
#undef SET_STRING
	SET(ini_section_set_string(section, "ModemInit", value->modem_init_string,
	    &ini_default_style));
	SET(ini_section_set_string(section, "ModemDial", value->modem_dial_string,
	    &ini_default_style));
	SET(ini_section_set_string(section, "TERM", value->terminal_type,
	    &ini_default_style));
	SET(ini_section_set_string(section, "KeyDerivationIterations",
	    value->key_derivation_iterations, &ini_default_style));
	SET(ini_section_set_double(section, "ScalingFactor", value->scaling_factor,
	    &ini_default_style));
	SET(ini_section_set_bool(section, "BlockyScaling", value->blocky_scaling,
	    &ini_default_style));
	SET(ini_section_set_bool(section, "ExternalScaling",
	    value->external_scaling, &ini_default_style));
	SET(ini_section_set_bool(section, "ColorEmoji", value->color_emoji,
	    &ini_default_style));
	SET(ini_section_set_bool(section, "InvertMouseWheel", value->invert_wheel,
	    &ini_default_style));
	SET(ini_section_set_bool(section, "LeftJustify", value->left_justify,
	    &ini_default_style));

	SET(set_classic_colors(document, value));

	SET(ensure_section(document, "WebLists", &web));
	SET(ini_section_copy_pairs(web, &old_pairs));
	for (i = 0; i < name_value_list_count(old_pairs); ++i)
		(void)ini_section_remove_key(web, old_pairs[i]->name);
	name_value_list_free(&old_pairs);
	for (i = 0; i < name_value_list_count(value->web_downloads); ++i) {
		SET(ini_section_set_string(web, value->web_downloads[i]->name,
		    value->web_downloads[i]->value, &ini_default_style));
	}

	SET(ensure_section(document, "SortProfiles", &profiles));
	name_value_list_free(&old_pairs);
	SET(ini_section_copy_pairs(profiles, &old_pairs));
	for (i = 0; i < name_value_list_count(old_pairs); ++i)
		(void)ini_section_remove_key(profiles, old_pairs[i]->name);
	name_value_list_free(&old_pairs);
	for (i = 0; i < name_value_list_count(value->sort_profiles); ++i) {
		SET(ini_section_set_string(profiles, value->sort_profiles[i]->name,
		    value->sort_profiles[i]->value, &ini_default_style));
	}
	return 0;
}

#undef SET

static int
read_document(ini_document_t **result, const char *path)
{
	FILE *stream;
	int status = fs_open(&stream, path, "rb");

	if (status == IO_ERROR_NOT_FOUND)
		return ini_document_create(result);
	if (status < 0)
		return status;
	status = ini_document_read(result, stream);
	(void)fclose(stream);
	return status;
}

static int
write_document(const ini_document_t *document, const char *path)
{
	char *data = NULL;
	size_t size = 0;
	FILE *stream;
	int status;

	status = ini_document_serialize(document, &data, &size);
	if (status < 0)
		return status;
	status = fs_open(&stream, path, "wb");
	if (status < 0) {
		free(data);
		return status;
	}
	if (size != 0 && fwrite(data, 1, size, stream) != size)
		status = IO_ERROR_IO;
	if (fclose(stream) != 0 && status == 0)
		status = IO_ERROR_IO;
	free(data);
	return status;
}

static bool
same_value(const char *left, const char *right)
{
	return left == NULL ? right == NULL
	    : right != NULL && strcmp(left, right) == 0;
}

static const char *
document_value(ini_document_t *document, const char *section_name,
    const char *key)
{
	ini_section_t *section = ini_document_find_section(document, section_name);

	return section == NULL ? NULL : ini_section_find(section, key);
}

static int
merge_key(ini_document_t *remote, ini_document_t *remote_compare,
    ini_document_t *base, ini_document_t *local,
    const struct setting_key *key)
{
	const char *base_value = document_value(base, key->section, key->key);
	const char *local_value = document_value(local, key->section, key->key);
	const char *remote_value = document_value(remote_compare, key->section,
	    key->key);
	ini_section_t *section;
	int status;

	if (same_value(base_value, local_value))
		return 0;
	if (!same_value(base_value, remote_value)
	    && !same_value(local_value, remote_value))
		return IO_ERROR_CONFLICT;
	status = ensure_section(remote, key->section, &section);
	if (status < 0)
		return status;
	if (local_value == NULL) {
		(void)ini_section_remove_key(section, key->key);
		return 0;
	}
	return ini_section_set_string(section, key->key, local_value,
	    &ini_default_style);
}

static bool
same_pairs(const ini_section_t *left, const ini_section_t *right)
{
	size_t i;

	if (left == NULL || right == NULL)
		return left == right;
	if (ini_section_count(left) != ini_section_count(right))
		return false;
	for (i = 0; i < ini_section_count(left); ++i) {
		const struct name_value *a = ini_section_at(left, i);
		const struct name_value *b = ini_section_at(right, i);

		if (strcmp(a->name, b->name) != 0
		    || strcmp(a->value, b->value) != 0)
			return false;
	}
	return true;
}

static int
replace_named_section(ini_document_t *remote, ini_document_t *local,
	const char *name)
{
	ini_section_t *destination;
	ini_section_t *source = ini_document_find_section(local, name);
	name_value_list_t old = NULL;
	size_t i;
	int status;

	status = ensure_section(remote, name, &destination);
	if (status < 0)
		return status;
	status = ini_section_copy_pairs(destination, &old);
	if (status < 0)
		return status;
	for (i = 0; i < name_value_list_count(old); ++i)
		(void)ini_section_remove_key(destination, old[i]->name);
	name_value_list_free(&old);
	if (source == NULL)
		return 0;
	for (i = 0; i < ini_section_count(source); ++i) {
		const struct name_value *pair = ini_section_at(source, i);

		status = ini_section_set_string(destination, pair->name, pair->value,
		    &ini_default_style);
		if (status < 0)
			return status;
	}
	return 0;
}

static int
load_locked(const char *personal_list)
{
	ini_document_t *document = NULL;
	struct st_settings baseline;
	struct st_settings value;
	int status;

	st_settings_init(&baseline);
	st_settings_init(&value);
	status = read_document(&document, settings_path);
	if (status == 0)
		status = decode_settings(&value, document, personal_list,
		    cached_system_list);
	if (status == 0 && !STORE_SETTINGS_COPY(&baseline, &value))
		status = IO_ERROR_OUT_OF_MEMORY;
	if (status < 0) {
		ini_document_free(&document);
		st_settings_dispose(&baseline);
		st_settings_dispose(&value);
		return status;
	}
	st_settings_dispose(&st_settings);
	st_settings = value;
	st_settings_init(&value);
	st_settings_dispose(&baseline_settings);
	baseline_settings = baseline;
	st_settings_init(&baseline);
	ini_document_free(&baseline_document);
	baseline_document = document;
	baseline_initialized = true;
	recent_emoji_loaded = false;
	return 0;
}

int
st_settings_load(const struct st_app_paths *paths)
{
	st_file_lock_t *lock = NULL;
	char *new_path;
	char *new_cache;
	char *new_personal;
	int status;

	if (paths == NULL || paths->settings_file == NULL
	    || paths->personal_list == NULL || paths->cached_system_list == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	new_path = strdup(paths->settings_file);
	new_cache = strdup(paths->cached_system_list);
	new_personal = strdup(paths->personal_list);
	if (new_path == NULL || new_cache == NULL || new_personal == NULL) {
		free(new_path);
		free(new_cache);
		free(new_personal);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	st_settings_shutdown();
	settings_path = new_path;
	cached_system_list = new_cache;
	personal_list_path = new_personal;
	status = st_file_lock_acquire(&lock, settings_path, false);
	if (status == IO_ERROR_NOT_FOUND) {
		/* The absent parent is valid until the first save. */
		status = load_locked(paths->personal_list);
	} else if (status == 0) {
		status = load_locked(paths->personal_list);
		st_file_lock_release(lock);
	}
	if (status < 0)
		st_settings_shutdown();
	return status;
}

int
st_settings_reload(void)
{
	st_file_lock_t *lock = NULL;
	int status;

	if (settings_path == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_file_lock_acquire(&lock, settings_path, false);
	if (status == 0) {
		status = load_locked(personal_list_path);
		st_file_lock_release(lock);
	} else if (status == IO_ERROR_NOT_FOUND)
		status = load_locked(personal_list_path);
	return status;
}

int
st_settings_font_draft_reload(struct st_settings_font_draft *draft)
{
	st_file_lock_t *lock = NULL;
	ini_document_t *document = NULL;
	struct st_settings loaded;
	int status;

	if (draft == NULL || settings_path == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	st_settings_init(&loaded);
	status = st_file_lock_acquire(&lock, settings_path, false);
	if (status == 0) {
		status = read_document(&document, settings_path);
		st_file_lock_release(lock);
	}
	else if (status == IO_ERROR_NOT_FOUND)
		status = read_document(&document, settings_path);
	if (status == 0)
		status = decode_settings(&loaded, document, personal_list_path,
		    cached_system_list);
	if (status == 0 && st_settings_font_draft_replace(draft,
	    loaded.custom_fonts) < 0)
		status = IO_ERROR_OUT_OF_MEMORY;
	ini_document_free(&document);
	st_settings_dispose(&loaded);
	return status;
}

void
st_settings_install(struct st_settings *replacement)
{
	if (replacement == NULL)
		return;
	st_settings_dispose(&st_settings);
	st_settings = *replacement;
	st_settings_init(replacement);
}

int
st_settings_save_value(const struct st_settings *value)
{
	ini_document_t *remote = NULL;
	ini_document_t *local = NULL;
	ini_document_t *canonical_base = NULL;
	ini_document_t *canonical_remote = NULL;
	ini_section_t *base_web;
	ini_section_t *local_web;
	ini_section_t *remote_web;
	ini_section_t *base_profiles;
	ini_section_t *local_profiles;
	ini_section_t *remote_profiles;
	st_file_lock_t *lock = NULL;
	struct st_settings installed;
	struct st_settings saved;
	struct st_settings remote_settings;
	st_settings_font_list_t installed_fonts = NULL;
	st_settings_font_list_t saved_fonts = NULL;
	size_t i;
	int status;

	if (!baseline_initialized || settings_path == NULL || value == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = fs_create_parent_directories(settings_path);
	if (status == 0)
		status = st_file_lock_acquire(&lock, settings_path, true);
	if (status < 0)
		return status;
	status = read_document(&remote, settings_path);
	if (status == 0)
		status = ini_document_clone(&canonical_base, baseline_document);
	if (status == 0)
		status = encode_settings(canonical_base, &baseline_settings);
	if (status == 0)
		status = ini_document_clone(&local, canonical_base);
	if (status == 0)
		status = encode_settings(local, value);
	st_settings_init(&remote_settings);
	if (status == 0)
		status = decode_settings(&remote_settings, remote,
		    personal_list_path, cached_system_list);
	if (status == 0)
		status = ini_document_clone(&canonical_remote, remote);
	if (status == 0)
		status = encode_settings(canonical_remote, &remote_settings);
	for (i = 0; status == 0
	    && i < sizeof(known_keys) / sizeof(known_keys[0]); ++i)
		status = merge_key(remote, canonical_remote, canonical_base, local,
		    &known_keys[i]);
	base_web = canonical_base == NULL ? NULL
	    : ini_document_find_section(canonical_base, "WebLists");
	local_web = local == NULL ? NULL
	    : ini_document_find_section(local, "WebLists");
	remote_web = canonical_remote == NULL ? NULL
	    : ini_document_find_section(canonical_remote, "WebLists");
	if (status == 0 && !same_pairs(base_web, local_web)) {
		if (!same_pairs(base_web, remote_web)
		    && !same_pairs(local_web, remote_web))
			status = IO_ERROR_CONFLICT;
		else
			status = replace_named_section(remote, local, "WebLists");
	}
	base_profiles = canonical_base == NULL ? NULL
	    : ini_document_find_section(canonical_base, "SortProfiles");
	local_profiles = local == NULL ? NULL
	    : ini_document_find_section(local, "SortProfiles");
	remote_profiles = canonical_remote == NULL ? NULL
	    : ini_document_find_section(canonical_remote, "SortProfiles");
	if (status == 0 && !same_pairs(base_profiles, local_profiles)) {
		if (!same_pairs(base_profiles, remote_profiles)
		    && !same_pairs(local_profiles, remote_profiles))
			status = IO_ERROR_CONFLICT;
		else
			status = replace_named_section(remote, local, "SortProfiles");
	}
	if (status == 0)
		status = migrate_classic_colors(remote);
	st_settings_init(&installed);
	st_settings_init(&saved);
	/* Ordinary settings snapshots may predate the separate font draft. */
	if (status == 0
	    && (!st_settings_fonts_copy(&installed_fonts,
	    remote_settings.custom_fonts)
	    || !st_settings_fonts_copy(&saved_fonts,
	    remote_settings.custom_fonts)))
		status = IO_ERROR_OUT_OF_MEMORY;
	if (status == 0 && !STORE_SETTINGS_COPY(&installed, value))
		status = IO_ERROR_OUT_OF_MEMORY;
	if (status == 0 && !STORE_SETTINGS_COPY(&saved, value))
		status = IO_ERROR_OUT_OF_MEMORY;
	if (status == 0) {
		st_settings_fonts_free(&installed.custom_fonts);
		installed.custom_fonts = installed_fonts;
		installed_fonts = NULL;
		st_settings_fonts_free(&saved.custom_fonts);
		saved.custom_fonts = saved_fonts;
		saved_fonts = NULL;
	}
	if (status == 0)
		status = write_document(remote, settings_path);
	if (status == 0) {
		st_settings_dispose(&st_settings);
		st_settings = installed;
		st_settings_init(&installed);
		ini_document_free(&baseline_document);
		baseline_document = remote;
		remote = NULL;
		st_settings_dispose(&baseline_settings);
		baseline_settings = saved;
		st_settings_init(&saved);
	}
	st_settings_dispose(&installed);
	st_settings_dispose(&saved);
	st_settings_fonts_free(&installed_fonts);
	st_settings_fonts_free(&saved_fonts);
	st_settings_dispose(&remote_settings);
	ini_document_free(&remote);
	ini_document_free(&local);
	ini_document_free(&canonical_base);
	ini_document_free(&canonical_remote);
	st_file_lock_release(lock);
	return status;
}

int
st_settings_save(void)
{
	return st_settings_save_value(&st_settings);
}

int
st_settings_persist_scaling_factor(double scaling_factor)
{
	st_file_lock_t *lock = NULL;
	ini_document_t *document = NULL;
	ini_section_t *section = NULL;
	int status;

	if (!baseline_initialized || settings_path == NULL
	    || !isfinite(scaling_factor) || scaling_factor < 1.0)
		return IO_ERROR_INVALID_ARGUMENT;
	status = fs_create_parent_directories(settings_path);
	if (status == 0)
		status = st_file_lock_acquire(&lock, settings_path, true);
	if (status == 0)
		status = read_document(&document, settings_path);
	if (status == 0)
		status = ensure_section(document, "SyncTERM", &section);
	if (status == 0)
		status = ini_section_set_double(section, "ScalingFactor",
		    scaling_factor, &ini_default_style);
	if (status == 0)
		status = write_document(document, settings_path);
	ini_document_free(&document);
	st_file_lock_release(lock);
	return status;
}

int
st_settings_font_draft_save(struct st_settings_font_draft *draft)
{
	st_file_lock_t *lock = NULL;
	ini_document_t *remote = NULL;
	ini_document_t *new_baseline_document = NULL;
	struct st_settings remote_before;
	st_settings_font_list_t installed_fonts = NULL;
	st_settings_font_list_t baseline_fonts = NULL;
	st_settings_font_list_t draft_baseline = NULL;
	int status;

	if (draft == NULL || settings_path == NULL || !baseline_initialized)
		return IO_ERROR_INVALID_ARGUMENT;
	st_settings_init(&remote_before);
	status = fs_create_parent_directories(settings_path);
	if (status == 0)
		status = st_file_lock_acquire(&lock, settings_path, true);
	if (status == 0)
		status = read_document(&remote, settings_path);
	if (status == 0)
		status = decode_settings(&remote_before, remote, personal_list_path,
		    cached_system_list);
	if (status == 0
	    && !st_settings_fonts_equal(remote_before.custom_fonts,
	    draft->baseline)
	    && !st_settings_fonts_equal(remote_before.custom_fonts, draft->fonts))
		status = IO_ERROR_CONFLICT;
	if (status == 0)
		status = encode_fonts(remote, draft->fonts);
	if (status == 0)
		status = ini_document_clone(&new_baseline_document,
		    baseline_document);
	if (status == 0)
		status = encode_fonts(new_baseline_document, draft->fonts);
	if (status == 0
	    && (!st_settings_fonts_copy(&installed_fonts, draft->fonts)
	    || !st_settings_fonts_copy(&baseline_fonts, draft->fonts)
	    || !st_settings_fonts_copy(&draft_baseline, draft->fonts)))
		status = IO_ERROR_OUT_OF_MEMORY;
	if (status == 0)
		status = write_document(remote, settings_path);
	if (status == 0) {
		st_settings_fonts_free(&st_settings.custom_fonts);
		st_settings.custom_fonts = installed_fonts;
		installed_fonts = NULL;
		st_settings_fonts_free(&baseline_settings.custom_fonts);
		baseline_settings.custom_fonts = baseline_fonts;
		baseline_fonts = NULL;
		ini_document_free(&baseline_document);
		baseline_document = new_baseline_document;
		new_baseline_document = NULL;
		st_settings_fonts_free(&draft->baseline);
		draft->baseline = draft_baseline;
		draft_baseline = NULL;
	}
	st_settings_fonts_free(&installed_fonts);
	st_settings_fonts_free(&baseline_fonts);
	st_settings_fonts_free(&draft_baseline);
	st_settings_dispose(&remote_before);
	ini_document_free(&new_baseline_document);
	ini_document_free(&remote);
	st_file_lock_release(lock);
	return status;
}

void
st_settings_shutdown(void)
{
	recent_emoji_loaded = false;
	recent_emoji_count = 0;
	free(settings_path);
	free(cached_system_list);
	free(personal_list_path);
	settings_path = NULL;
	cached_system_list = NULL;
	personal_list_path = NULL;
	ini_document_free(&baseline_document);
	st_settings_dispose(&baseline_settings);
	st_settings_dispose(&st_settings);
	baseline_initialized = false;
}

size_t
st_settings_recent_emoji_count(void)
{
	if (!recent_emoji_loaded) {
		recent_emoji_count = 0;
		if (baseline_document != NULL)
			emoji_read(baseline_document, recent_emoji, &recent_emoji_count);
		recent_emoji_loaded = true;
	}
	return recent_emoji_count;
}

const char *
st_settings_recent_emoji_at(size_t index)
{
	return index < st_settings_recent_emoji_count()
	    ? st_emoji_entries[recent_emoji[index]].text : NULL;
}

int
st_settings_remember_emoji(const char *text, bool persist)
{
	size_t id = st_emoji_find(text);
	size_t merged[ST_SETTINGS_RECENT_EMOJI_MAX];
	size_t count = 0;
	ini_document_t *document = NULL;
	ini_section_t *section = NULL;
	st_file_lock_t *lock = NULL;
	int status;

	if (id == st_emoji_entry_count)
		return IO_ERROR_INVALID_ARGUMENT;
	(void)st_settings_recent_emoji_count();
	emoji_append(merged, &count, id);
	for (size_t i = 0; i < recent_emoji_count; i++)
		emoji_append(merged, &count, recent_emoji[i]);
	memcpy(recent_emoji, merged, count * sizeof(*merged));
	recent_emoji_count = count;
	if (!persist)
		return 0;
	if (settings_path == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = fs_create_parent_directories(settings_path);
	if (status == 0)
		status = st_file_lock_acquire(&lock, settings_path, true);
	if (status == 0)
		status = read_document(&document, settings_path);
	if (status == 0) {
		count = 0;
		emoji_append(merged, &count, id);
		emoji_read(document, merged, &count);
		for (size_t i = 0; i < recent_emoji_count; i++)
			emoji_append(merged, &count, recent_emoji[i]);
		ini_section_t *old = ini_document_find_section(document, "RecentEmoji");
		if (old != NULL)
			(void)ini_document_remove_section(document, old);
		status = ensure_section(document, "RecentEmoji", &section);
	}
	for (size_t i = 0; status == 0 && i < count; i++) {
		/* One digit per value bit also bounds the decimal representation. */
		char key[sizeof(size_t) * CHAR_BIT + 1];
		(void)snprintf(key, sizeof(key), "%zu", i + 1);
		status = ini_section_set_string(section, key, st_emoji_entries[merged[i]].text,
		    &ini_default_style);
	}
	if (status == 0)
		status = write_document(document, settings_path);
	if (status == 0) {
		memcpy(recent_emoji, merged, count * sizeof(*merged));
		recent_emoji_count = count;
	}
	ini_document_free(&document);
	st_file_lock_release(lock);
	return status;
}

size_t
st_settings_web_download_count(void)
{
	return name_value_list_count(st_settings.web_downloads);
}

const struct name_value *
st_settings_web_download_at(size_t index)
{
	if (index >= name_value_list_count(st_settings.web_downloads))
		return NULL;
	return st_settings.web_downloads[index];
}

static bool
web_download_name_available(const char *name, size_t except)
{
	for (size_t i = 0; i < st_settings_web_download_count(); ++i) {
		if (i != except && names_equal(st_settings.web_downloads[i]->name,
		    name))
			return false;
	}
	return !names_equal(name, "System List");
}

int
st_settings_web_download_set(size_t index, const char *name, const char *url)
{
	struct name_value *replacement;
	struct name_value *previous;

	if (index >= name_value_list_count(st_settings.web_downloads)
	    || !st_unicode_name_valid(name, 50, 100, "=:")
	    || name[0] == ';' || name[0] == '[' || url == NULL
	    || !web_download_name_available(name, index))
		return IO_ERROR_INVALID_ARGUMENT;
	replacement = name_value_create(name, url);
	if (replacement == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	previous = st_settings.web_downloads[index];
	st_settings.web_downloads[index] = replacement;
	name_value_free_item(previous);
	return 0;
}

int
st_settings_web_download_append(const char *name, const char *url)
{
	if (!st_unicode_name_valid(name, 50, 100, "=:")
	    || name[0] == ';' || name[0] == '[' || url == NULL
	    || !web_download_name_available(name, SIZE_MAX))
		return IO_ERROR_INVALID_ARGUMENT;
	return name_value_list_append(&st_settings.web_downloads, name, url)
	    ? 0 : IO_ERROR_OUT_OF_MEMORY;
}

int
st_settings_web_download_remove(size_t index)
{
	return name_value_list_remove(st_settings.web_downloads, index)
	    ? 0 : IO_ERROR_INVALID_ARGUMENT;
}

int
st_settings_web_download_move(size_t from, size_t to)
{
	name_value_list_t list = st_settings.web_downloads;
	struct name_value *item;
	size_t count = name_value_list_count(list);

	if (from >= count || to >= count)
		return IO_ERROR_INVALID_ARGUMENT;
	if (from == to)
		return 0;
	item = list[from];
	if (from < to)
		memmove(&list[from], &list[from + 1], (to - from) * sizeof(*list));
	else
		memmove(&list[to + 1], &list[to], (from - to) * sizeof(*list));
	list[to] = item;
	return 0;
}
