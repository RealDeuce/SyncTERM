/* Editable, source-preserving SyncTERM theme documents. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_theme.h"

#include "filesystem.h"
#include "ini.h"
#include "io_error.h"
#include "path.h"
#include "st_codepage.h"
#include "st_utf8.h"
#include "string_compat.h"
#include "string_util.h"
#include "theme_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const metadata_keys[] = {
	"Name", "Author", "Description", "Version",
};
static const char *const style_keys[] = {
	"Font", "LegacyAttr", "Foreground", "Background",
};
static const char *const glyph_keys[] = {"CP437", "ASCII", "Codepoint"};

struct document_style {
	char *role;
	bool builtin;
	bool removed;
	unsigned present;
	int values[ST_THEME_STYLE_FIELD_COUNT];
};

struct document_glyph {
	char *name;
	bool builtin;
	bool removed;
	unsigned present;
	unsigned resolved_present;
	uint32_t values[ST_THEME_GLYPH_FIELD_COUNT];
};

struct document_state {
	char *metadata[ST_THEME_METADATA_FIELD_COUNT];
	unsigned metadata_present;
	struct document_style *styles;
	size_t style_count;
	struct document_glyph *glyphs;
	size_t glyph_count;
	bool replace_definitions;
};

struct st_theme_document {
	struct st_theme_manager *manager;
	char *filename;
	ini_document_t *source;
	char *original;
	size_t original_size;
	struct document_state *states;
	size_t state_count;
	size_t position;
	struct st_theme *preview;
};

static void
set_document_error(char *error, size_t error_size, const char *message)
{
	if (error != NULL && error_size != 0)
		(void)snprintf(error, error_size, "%s", message);
}

static void
state_dispose(struct document_state *state)
{
	if (state == NULL)
		return;
	for (unsigned i = 0; i < ST_THEME_METADATA_FIELD_COUNT; ++i)
		free(state->metadata[i]);
	for (size_t i = 0; i < state->style_count; ++i)
		free(state->styles[i].role);
	free(state->styles);
	for (size_t i = 0; i < state->glyph_count; ++i)
		free(state->glyphs[i].name);
	free(state->glyphs);
	*state = (struct document_state){0};
}

static bool
state_clone(struct document_state *result, const struct document_state *source)
{
	struct document_state value = {
		.metadata_present = source->metadata_present,
		.replace_definitions = source->replace_definitions,
	};

	for (unsigned i = 0; i < ST_THEME_METADATA_FIELD_COUNT; ++i) {
		if (source->metadata[i] == NULL)
			continue;
		value.metadata[i] = strdup(source->metadata[i]);
		if (value.metadata[i] == NULL)
			goto failed;
	}
	if (source->style_count != 0) {
		value.styles = calloc(source->style_count, sizeof(value.styles[0]));
		if (value.styles == NULL)
			goto failed;
		for (size_t i = 0; i < source->style_count; ++i) {
			value.styles[i] = source->styles[i];
			value.styles[i].role = strdup(source->styles[i].role);
			if (value.styles[i].role == NULL)
				goto failed;
			++value.style_count;
		}
	}
	if (source->glyph_count != 0) {
		value.glyphs = calloc(source->glyph_count, sizeof(value.glyphs[0]));
		if (value.glyphs == NULL)
			goto failed;
		for (size_t i = 0; i < source->glyph_count; ++i) {
			value.glyphs[i] = source->glyphs[i];
			value.glyphs[i].name = strdup(source->glyphs[i].name);
			if (value.glyphs[i].name == NULL)
				goto failed;
			++value.glyph_count;
		}
	}
	*result = value;
	return true;

failed:
	state_dispose(&value);
	return false;
}

static struct document_state *
current_state(struct st_theme_document *document)
{
	return &document->states[document->position];
}

static const struct document_state *
current_state_const(const struct st_theme_document *document)
{
	return &document->states[document->position];
}

static int
style_index(const struct document_state *state, const char *role)
{
	for (size_t i = 0; i < state->style_count; ++i)
		if (strcmp(state->styles[i].role, role) == 0)
			return (int)i;
	return -1;
}

static int
glyph_index(const struct document_state *state, const char *name)
{
	for (size_t i = 0; i < state->glyph_count; ++i)
		if (strcmp(state->glyphs[i].name, name) == 0)
			return (int)i;
	return -1;
}

static void
resolve_document_glyph(struct document_glyph *glyph)
{
	if ((glyph->resolved_present & (1u << ST_THEME_GLYPH_CP437)) == 0)
		glyph->values[ST_THEME_GLYPH_CP437]
		    = glyph->values[ST_THEME_GLYPH_ASCII];
	if ((glyph->resolved_present & (1u << ST_THEME_GLYPH_CODEPOINT)) == 0) {
		glyph->values[ST_THEME_GLYPH_CODEPOINT]
		    = (glyph->resolved_present & (1u << ST_THEME_GLYPH_CP437)) != 0
		    ? st_codepage_to_unicode(ST_CODEPAGE_CP437,
		    (uint8_t)glyph->values[ST_THEME_GLYPH_CP437], true)
		    : glyph->values[ST_THEME_GLYPH_ASCII];
	}
}

static bool
section_field_present(ini_document_t *source, const char *prefix,
	const char *name, const char *key)
{
	size_t length = strlen(prefix) + strlen(name) + 1;
	char *section_name = malloc(length);
	bool present = false;

	if (section_name == NULL)
		return false;
	(void)snprintf(section_name, length, "%s%s", prefix, name);
	ini_section_t *section = ini_document_find_section(source, section_name);
	if (section != NULL)
		present = ini_section_has_key(section, key);
	free(section_name);
	return present;
}

static bool
state_from_theme(struct document_state *result,
	const struct st_theme_manager *manager, const struct st_theme *theme,
	ini_document_t *source, bool all_present)
{
	struct document_state value = {0};
	const char *metadata[] = {
		theme->name, theme->author, theme->description, theme->version,
	};

	for (unsigned i = 0; i < ST_THEME_METADATA_FIELD_COUNT; ++i) {
		if (metadata[i] != NULL) {
			value.metadata[i] = strdup(metadata[i]);
			if (value.metadata[i] == NULL)
				goto failed;
		}
		if (all_present || (source != NULL && ini_section_has_key(
		    ini_document_root(source), metadata_keys[i])))
			value.metadata_present |= 1u << i;
	}
	value.styles = calloc(theme->style_count, sizeof(value.styles[0]));
	value.glyphs = calloc(theme->glyph_count, sizeof(value.glyphs[0]));
	if ((theme->style_count != 0 && value.styles == NULL)
	    || (theme->glyph_count != 0 && value.glyphs == NULL))
		goto failed;
	for (size_t i = 0; i < theme->style_count; ++i) {
		const struct st_theme_style *style = &theme->styles[i];
		struct document_style *destination = &value.styles[i];

		destination->role = strdup(style->role);
		if (destination->role == NULL)
			goto failed;
		destination->builtin = find_style(manager->default_theme,
		    style->role) != NULL;
		destination->values[ST_THEME_STYLE_FONT] = style->font;
		destination->values[ST_THEME_STYLE_LEGACY_ATTR] = style->legacy_attr;
		destination->values[ST_THEME_STYLE_FOREGROUND] = style->foreground;
		destination->values[ST_THEME_STYLE_BACKGROUND] = style->background;
		for (unsigned field = 0; field < ST_THEME_STYLE_FIELD_COUNT; ++field) {
			if (all_present || (source != NULL && section_field_present(source,
			    "Style:", style->role, style_keys[field])))
				destination->present |= 1u << field;
		}
		++value.style_count;
	}
	for (size_t i = 0; i < theme->glyph_count; ++i) {
		const struct st_theme_glyph *glyph = &theme->glyphs[i];
		struct document_glyph *destination = &value.glyphs[i];

		destination->name = strdup(glyph->name);
		if (destination->name == NULL)
			goto failed;
		destination->builtin = find_glyph(manager->default_theme,
		    glyph->name) != NULL;
		destination->values[ST_THEME_GLYPH_CP437] = glyph->cp437;
		destination->values[ST_THEME_GLYPH_ASCII] = glyph->ascii;
		destination->values[ST_THEME_GLYPH_CODEPOINT] = glyph->codepoint;
		destination->resolved_present = glyph->present;
		for (unsigned field = 0; field < ST_THEME_GLYPH_FIELD_COUNT; ++field) {
			if (all_present || (source != NULL && section_field_present(source,
			    "Glyph:", glyph->name, glyph_keys[field])))
				destination->present |= 1u << field;
		}
		resolve_document_glyph(destination);
		destination->resolved_present |= destination->present;
		++value.glyph_count;
	}
	*result = value;
	return true;

failed:
	state_dispose(&value);
	return false;
}

static struct st_theme *
preview_from_state(const struct st_theme_document *document,
	const struct document_state *state)
{
	struct st_theme *theme = theme_clone(document->manager->default_theme);

	if (theme == NULL)
		return NULL;
	free(theme->filename);
	theme->filename = strdup(document->filename);
	free(theme->name);
	theme->name = state->metadata[ST_THEME_METADATA_NAME] == NULL ? NULL
	    : strdup(state->metadata[ST_THEME_METADATA_NAME]);
	free(theme->author);
	theme->author = state->metadata[ST_THEME_METADATA_AUTHOR] == NULL ? NULL
	    : strdup(state->metadata[ST_THEME_METADATA_AUTHOR]);
	free(theme->description);
	theme->description = state->metadata[ST_THEME_METADATA_DESCRIPTION] == NULL
	    ? NULL : strdup(state->metadata[ST_THEME_METADATA_DESCRIPTION]);
	free(theme->version);
	theme->version = state->metadata[ST_THEME_METADATA_VERSION] == NULL ? NULL
	    : strdup(state->metadata[ST_THEME_METADATA_VERSION]);
	if (theme->filename == NULL || (state->metadata[ST_THEME_METADATA_NAME]
	    != NULL && theme->name == NULL)
	    || (state->metadata[ST_THEME_METADATA_AUTHOR] != NULL
	    && theme->author == NULL)
	    || (state->metadata[ST_THEME_METADATA_DESCRIPTION] != NULL
	    && theme->description == NULL)
	    || (state->metadata[ST_THEME_METADATA_VERSION] != NULL
	    && theme->version == NULL))
		goto failed;
	for (size_t i = 0; i < state->style_count; ++i) {
		const struct document_style *source = &state->styles[i];
		struct st_theme_style *style;

		if (source->removed)
			continue;
		style = find_style(theme, source->role);
		if (style == NULL)
			style = add_style(theme, source->role);
		if (style == NULL)
			goto failed;
		style->font = source->values[ST_THEME_STYLE_FONT];
		style->legacy_attr = source->values[ST_THEME_STYLE_LEGACY_ATTR];
		style->foreground = source->values[ST_THEME_STYLE_FOREGROUND];
		style->background = source->values[ST_THEME_STYLE_BACKGROUND];
	}
	for (size_t i = 0; i < state->glyph_count; ++i) {
		const struct document_glyph *source = &state->glyphs[i];
		struct st_theme_glyph *glyph;

		if (source->removed)
			continue;
		glyph = find_glyph(theme, source->name);
		if (glyph == NULL)
			glyph = add_glyph(theme, source->name);
		if (glyph == NULL)
			goto failed;
		for (unsigned field = 0; field < ST_THEME_GLYPH_FIELD_COUNT;
		    ++field) {
			unsigned bit = 1u << field;

			if ((source->present & bit) == 0)
				continue;
			if (field == ST_THEME_GLYPH_CP437)
				glyph->cp437 = (uint8_t)source->values[field];
			else if (field == ST_THEME_GLYPH_ASCII)
				glyph->ascii = (uint8_t)source->values[field];
			else
				glyph->codepoint = source->values[field];
			glyph->present |= bit;
		}
		if (!resolve_glyph(glyph))
			goto failed;
	}
	struct st_theme_style *default_style = find_style(theme, "default");
	if (default_style == NULL || default_style->font < 0
	    || default_style->legacy_attr < 0 || default_style->foreground < 0
	    || default_style->background < 0)
		goto failed;
	return theme;

failed:
	theme_free(theme);
	return NULL;
}

static int
commit_state(struct st_theme_document *document, struct document_state *next,
	char *error, size_t error_size)
{
	struct st_theme *preview = preview_from_state(document, next);

	if (preview == NULL) {
		set_document_error(error, error_size,
		    "the theme would have an incomplete default style");
		return IO_ERROR_INVALID_FORMAT;
	}
	for (size_t i = document->position + 1; i < document->state_count; ++i)
		state_dispose(&document->states[i]);
	document->state_count = document->position + 1;
	if (document->state_count == SIZE_MAX / sizeof(document->states[0])) {
		theme_free(preview);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	struct document_state *states = realloc(document->states,
	    (document->state_count + 1) * sizeof(document->states[0]));
	if (states == NULL) {
		theme_free(preview);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	document->states = states;
	document->states[document->state_count] = *next;
	*next = (struct document_state){0};
	document->position = document->state_count++;
	theme_free(document->preview);
	document->preview = preview;
	return 0;
}

static int
read_original(const char *path, char **result, size_t *result_size)
{
	return theme_read_file(path, result, result_size);
}

static int
document_allocate(struct st_theme_document **result,
	struct st_theme_manager *manager, const char *filename,
	ini_document_t *source, struct document_state *initial, char *original,
	size_t original_size)
{
	struct st_theme_document *document = calloc(1, sizeof(*document));

	if (document == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	document->filename = strdup(filename);
	document->states = calloc(1, sizeof(document->states[0]));
	if (document->filename == NULL || document->states == NULL) {
		st_theme_document_free(&document);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	document->manager = manager;
	document->states[0] = *initial;
	document->state_count = 1;
	document->preview = preview_from_state(document, &document->states[0]);
	if (document->preview == NULL) {
		document->states[0] = (struct document_state){0};
		st_theme_document_free(&document);
		return IO_ERROR_INVALID_FORMAT;
	}
	document->source = source;
	document->original = original;
	document->original_size = original_size;
	*initial = (struct document_state){0};
	*result = document;
	return 0;
}

int
st_theme_document_create(struct st_theme_document **result,
	struct st_theme_manager *manager, char *error, size_t error_size)
{
	ini_document_t *source = NULL;
	struct document_state initial = {0};
	struct st_theme *theme;
	int status;

	if (result == NULL || manager == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = ini_document_create(&source);
	if (status != 0)
		return status;
	theme = theme_clone(manager->default_theme);
	if (theme == NULL) {
		ini_document_free(&source);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	free(theme->name);
	theme->name = strdup("New Theme");
	if (theme->name == NULL || !state_from_theme(&initial, manager, theme,
	    NULL, false)) {
		theme_free(theme);
		ini_document_free(&source);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	initial.metadata_present = 1u << ST_THEME_METADATA_NAME;
	theme_free(theme);
	status = document_allocate(result, manager, "", source, &initial,
	    NULL, 0);
	if (status != 0) {
		state_dispose(&initial);
		ini_document_free(&source);
		set_document_error(error, error_size, "unable to create theme document");
	}
	return status;
}

int
st_theme_document_open(struct st_theme_document **result,
	struct st_theme_manager *manager, const char *filename, char *error,
	size_t error_size)
{
	char *path = NULL;
	char *original = NULL;
	size_t original_size = 0;
	ini_document_t *source = NULL;
	struct document_state initial = {0};
	struct st_theme *theme = NULL;
	FILE *stream = NULL;
	int status;

	if (result == NULL || manager == NULL || !st_theme_valid_filename(filename))
		return IO_ERROR_INVALID_ARGUMENT;
	status = path_join_alloc(&path, manager->directory, filename);
	if (status == 0)
		status = read_original(path, &original, &original_size);
	if (status == 0)
		status = fs_open(&stream, path, "rb");
	if (status == 0)
		status = ini_document_read(&source, stream);
	if (stream != NULL)
		(void)fclose(stream);
	if (status == 0)
		status = load_file(manager, filename, &theme, error, error_size);
	if (status == 0 && !state_from_theme(&initial, manager, theme, source,
	    false))
		status = IO_ERROR_OUT_OF_MEMORY;
	if (status == 0)
		status = document_allocate(result, manager, filename, source, &initial,
		    original, original_size);
	if (status != 0) {
		state_dispose(&initial);
		ini_document_free(&source);
		free(original);
		if (error != NULL && error[0] == '\0')
			set_document_error(error, error_size,
			    "unable to open theme document");
	}
	theme_free(theme);
	free(path);
	return status;
}

int
st_theme_document_copy_path(struct st_theme_document **result,
	struct st_theme_manager *manager, const char *path, char *error,
	size_t error_size)
{
	ini_document_t *source = NULL;
	struct document_state initial = {0};
	struct st_theme *theme = NULL;
	FILE *stream = NULL;
	int status;

	if (result == NULL || manager == NULL || path == NULL || path[0] == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	status = fs_open(&stream, path, "rb");
	if (status == 0)
		status = ini_document_read(&source, stream);
	if (stream != NULL)
		(void)fclose(stream);
	if (status == 0)
		status = load_path(manager, path, "", &theme, error, error_size);
	if (status == 0 && !state_from_theme(&initial, manager, theme, source,
	    false))
		status = IO_ERROR_OUT_OF_MEMORY;
	if (status == 0)
		status = document_allocate(result, manager, "", source, &initial,
		    NULL, 0);
	if (status != 0) {
		state_dispose(&initial);
		ini_document_free(&source);
		if (error != NULL && error[0] == '\0')
			set_document_error(error, error_size,
			    "unable to copy theme document");
	}
	theme_free(theme);
	return status;
}

void
st_theme_document_free(struct st_theme_document **document_ptr)
{
	struct st_theme_document *document;

	if (document_ptr == NULL || *document_ptr == NULL)
		return;
	document = *document_ptr;
	for (size_t i = 0; i < document->state_count; ++i)
		state_dispose(&document->states[i]);
	free(document->states);
	theme_free(document->preview);
	ini_document_free(&document->source);
	free(document->original);
	free(document->filename);
	free(document);
	*document_ptr = NULL;
}

const char *
st_theme_document_filename(const struct st_theme_document *document)
{
	return document == NULL ? NULL : document->filename;
}

const char *
st_theme_document_metadata(const struct st_theme_document *document,
	unsigned field)
{
	if (document == NULL || field >= ST_THEME_METADATA_FIELD_COUNT)
		return NULL;
	return current_state_const(document)->metadata[field];
}

int
st_theme_document_set_metadata(struct st_theme_document *document,
	unsigned field, const char *value, char *error, size_t error_size)
{
	struct document_state next = {0};
	int status;

	if (document == NULL || field >= ST_THEME_METADATA_FIELD_COUNT
	    || value == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (field == ST_THEME_METADATA_NAME && value[0] == '\0') {
		set_document_error(error, error_size, "Name cannot be empty");
		return IO_ERROR_INVALID_ARGUMENT;
	}
	if (!state_clone(&next, current_state(document)))
		return IO_ERROR_OUT_OF_MEMORY;
	free(next.metadata[field]);
	next.metadata[field] = value[0] == '\0' ? NULL : strdup(value);
	if (value[0] != '\0' && next.metadata[field] == NULL) {
		state_dispose(&next);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	if (value[0] == '\0')
		next.metadata_present &= ~(1u << field);
	else
		next.metadata_present |= 1u << field;
	status = commit_state(document, &next, error, error_size);
	state_dispose(&next);
	return status;
}

size_t
st_theme_document_style_count(const struct st_theme_document *document)
{
	size_t count = 0;

	if (document == NULL)
		return 0;
	const struct document_state *state = current_state_const(document);
	for (size_t i = 0; i < state->style_count; ++i)
		if (!state->styles[i].removed)
			++count;
	return count;
}

bool
st_theme_document_style_at(const struct st_theme_document *document,
	size_t index, struct st_theme_document_style *result)
{
	if (document == NULL || result == NULL)
		return false;
	const struct document_state *state = current_state_const(document);
	for (size_t i = 0; i < state->style_count; ++i) {
		const struct document_style *style = &state->styles[i];

		if (style->removed)
			continue;
		if (index-- != 0)
			continue;
		*result = (struct st_theme_document_style){
			.role = style->role,
			.builtin = style->builtin,
			.present = style->present,
		};
		memcpy(result->values, style->values, sizeof(result->values));
		return true;
	}
	return false;
}

static bool
valid_custom_name(const char *name)
{
	bool segment = false;

	if (name == NULL || name[0] == '\0')
		return false;
	for (const unsigned char *p = (const unsigned char *)name; *p != '\0';
	    ++p) {
		if (*p == '.') {
			if (!segment)
				return false;
			segment = false;
		}
		else if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')
		    || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')
			segment = true;
		else
			return false;
	}
	return segment;
}

int
st_theme_document_set_style(struct st_theme_document *document,
	const char *role, unsigned field, enum st_theme_value_mode mode, int value,
	char *error, size_t error_size)
{
	struct document_state next = {0};
	int status;

	if (document == NULL || role == NULL || field >= ST_THEME_STYLE_FIELD_COUNT
	    || mode > ST_THEME_VALUE_EXPLICIT)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!state_clone(&next, current_state(document)))
		return IO_ERROR_OUT_OF_MEMORY;
	int index = style_index(&next, role);
	if (index < 0 || next.styles[index].removed) {
		state_dispose(&next);
		set_document_error(error, error_size, "style does not exist");
		return IO_ERROR_NOT_FOUND;
	}
	struct document_style *style = &next.styles[index];
	if (mode == ST_THEME_VALUE_DEFAULT) {
		struct st_theme_style *compiled = find_style(
		    document->manager->default_theme, role);
		style->present &= ~(1u << field);
		if (compiled == NULL)
			style->values[field] = ST_THEME_INHERIT;
		else {
			const int values[] = {compiled->font, compiled->legacy_attr,
			    compiled->foreground, compiled->background};
			style->values[field] = values[field];
		}
	}
	else if (mode == ST_THEME_VALUE_INHERIT) {
		if (strcmp(role, "default") == 0) {
			state_dispose(&next);
			set_document_error(error, error_size,
			    "the default style cannot inherit fields");
			return IO_ERROR_INVALID_ARGUMENT;
		}
		style->present |= 1u << field;
		style->values[field] = ST_THEME_INHERIT;
	}
	else {
		int maximum = field < ST_THEME_STYLE_FOREGROUND ? 255 : 0xffffff;
		if (value < 0 || value > maximum) {
			state_dispose(&next);
			set_document_error(error, error_size,
			    "style value is out of range");
			return IO_ERROR_INVALID_ARGUMENT;
		}
		style->present |= 1u << field;
		style->values[field] = value;
	}
	status = commit_state(document, &next, error, error_size);
	state_dispose(&next);
	return status;
}

int
st_theme_document_add_style(struct st_theme_document *document,
	const char *role, char *error, size_t error_size)
{
	struct document_state next = {0};
	int status;

	if (document == NULL || !valid_custom_name(role)) {
		set_document_error(error, error_size, "invalid style name");
		return IO_ERROR_INVALID_ARGUMENT;
	}
	if (!state_clone(&next, current_state(document)))
		return IO_ERROR_OUT_OF_MEMORY;
	int index = style_index(&next, role);
	if (index >= 0 && !next.styles[index].removed) {
		state_dispose(&next);
		set_document_error(error, error_size, "style already exists");
		return IO_ERROR_ALREADY_EXISTS;
	}
	if (index >= 0) {
		next.styles[index].removed = false;
		next.styles[index].present = 0;
		for (unsigned field = 0; field < ST_THEME_STYLE_FIELD_COUNT; ++field)
			next.styles[index].values[field] = ST_THEME_INHERIT;
	}
	else {
		struct document_style *styles = realloc(next.styles,
		    (next.style_count + 1) * sizeof(next.styles[0]));
		if (styles == NULL) {
			state_dispose(&next);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		next.styles = styles;
		struct document_style *style = &styles[next.style_count];
		*style = (struct document_style){.role = strdup(role)};
		if (style->role == NULL) {
			state_dispose(&next);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		for (unsigned field = 0; field < ST_THEME_STYLE_FIELD_COUNT; ++field)
			style->values[field] = ST_THEME_INHERIT;
		++next.style_count;
	}
	status = commit_state(document, &next, error, error_size);
	state_dispose(&next);
	return status;
}

int
st_theme_document_remove_style(struct st_theme_document *document,
	const char *role, char *error, size_t error_size)
{
	struct document_state next = {0};
	int status;

	if (document == NULL || role == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!state_clone(&next, current_state(document)))
		return IO_ERROR_OUT_OF_MEMORY;
	int index = style_index(&next, role);
	if (index < 0 || next.styles[index].removed) {
		state_dispose(&next);
		set_document_error(error, error_size, "style does not exist");
		return IO_ERROR_NOT_FOUND;
	}
	if (next.styles[index].builtin) {
		state_dispose(&next);
		set_document_error(error, error_size,
		    "built-in styles cannot be removed");
		return IO_ERROR_INVALID_ARGUMENT;
	}
	next.styles[index].removed = true;
	status = commit_state(document, &next, error, error_size);
	state_dispose(&next);
	return status;
}

size_t
st_theme_document_glyph_count(const struct st_theme_document *document)
{
	size_t count = 0;

	if (document == NULL)
		return 0;
	const struct document_state *state = current_state_const(document);
	for (size_t i = 0; i < state->glyph_count; ++i)
		if (!state->glyphs[i].removed)
			++count;
	return count;
}

bool
st_theme_document_glyph_at(const struct st_theme_document *document,
	size_t index, struct st_theme_document_glyph *result)
{
	if (document == NULL || result == NULL)
		return false;
	const struct document_state *state = current_state_const(document);
	for (size_t i = 0; i < state->glyph_count; ++i) {
		const struct document_glyph *glyph = &state->glyphs[i];

		if (glyph->removed)
			continue;
		if (index-- != 0)
			continue;
		*result = (struct st_theme_document_glyph){
			.name = glyph->name,
			.builtin = glyph->builtin,
			.present = glyph->present,
			.values = {glyph->values[0], glyph->values[1],
			    glyph->values[2]},
		};
		return true;
	}
	return false;
}

int
st_theme_document_set_glyph(struct st_theme_document *document,
	const char *name, unsigned field, enum st_theme_value_mode mode,
	uint32_t value, char *error, size_t error_size)
{
	struct document_state next = {0};
	int status;

	if (document == NULL || name == NULL || field >= ST_THEME_GLYPH_FIELD_COUNT
	    || (mode != ST_THEME_VALUE_DEFAULT
	    && mode != ST_THEME_VALUE_EXPLICIT))
		return IO_ERROR_INVALID_ARGUMENT;
	if (!state_clone(&next, current_state(document)))
		return IO_ERROR_OUT_OF_MEMORY;
	int index = glyph_index(&next, name);
	if (index < 0 || next.glyphs[index].removed) {
		state_dispose(&next);
		set_document_error(error, error_size, "glyph does not exist");
		return IO_ERROR_NOT_FOUND;
	}
	struct document_glyph *glyph = &next.glyphs[index];
	if (mode == ST_THEME_VALUE_DEFAULT) {
		struct st_theme_glyph *compiled = find_glyph(
		    document->manager->default_theme, name);
		unsigned bit = 1u << field;

		if (compiled == NULL && field == ST_THEME_GLYPH_ASCII) {
			state_dispose(&next);
			set_document_error(error, error_size,
			    "a custom glyph requires an ASCII fallback");
			return IO_ERROR_INVALID_ARGUMENT;
		}
		glyph->present &= ~bit;
		if (compiled != NULL) {
			glyph->values[field] = field == ST_THEME_GLYPH_CP437
			    ? compiled->cp437 : field == ST_THEME_GLYPH_ASCII
			    ? compiled->ascii : compiled->codepoint;
			glyph->resolved_present = (glyph->resolved_present & ~bit)
			    | (compiled->present & bit);
		}
		else
			glyph->resolved_present &= ~bit;
	}
	else {
		uint32_t minimum = field == ST_THEME_GLYPH_ASCII ? 0x20 : 0;
		uint32_t maximum = field == ST_THEME_GLYPH_ASCII ? 0x7e
		    : field == ST_THEME_GLYPH_CP437 ? 0xff : 0x10ffff;
		if (value < minimum || value > maximum
		    || (field == ST_THEME_GLYPH_CODEPOINT
		    && !st_unicode_scalar_valid(value))) {
			state_dispose(&next);
			set_document_error(error, error_size,
			    "glyph value is out of range");
			return IO_ERROR_INVALID_ARGUMENT;
		}
		glyph->present |= 1u << field;
		glyph->resolved_present |= 1u << field;
		glyph->values[field] = value;
	}
	resolve_document_glyph(glyph);
	status = commit_state(document, &next, error, error_size);
	state_dispose(&next);
	return status;
}

int
st_theme_document_add_glyph(struct st_theme_document *document,
	const char *name, int ascii, char *error, size_t error_size)
{
	struct document_state next = {0};
	int status;

	if (document == NULL || !valid_custom_name(name)
	    || ascii < 0x20 || ascii > 0x7e) {
		set_document_error(error, error_size, "invalid glyph");
		return IO_ERROR_INVALID_ARGUMENT;
	}
	if (!state_clone(&next, current_state(document)))
		return IO_ERROR_OUT_OF_MEMORY;
	int index = glyph_index(&next, name);
	if (index >= 0 && !next.glyphs[index].removed) {
		state_dispose(&next);
		set_document_error(error, error_size, "glyph already exists");
		return IO_ERROR_ALREADY_EXISTS;
	}
	if (index >= 0) {
		next.glyphs[index].removed = false;
		next.glyphs[index].present = 1u << ST_THEME_GLYPH_ASCII;
		next.glyphs[index].resolved_present
		    = 1u << ST_THEME_GLYPH_ASCII;
		next.glyphs[index].values[ST_THEME_GLYPH_ASCII] = (uint8_t)ascii;
		resolve_document_glyph(&next.glyphs[index]);
	}
	else {
		struct document_glyph *glyphs = realloc(next.glyphs,
		    (next.glyph_count + 1) * sizeof(next.glyphs[0]));
		if (glyphs == NULL) {
			state_dispose(&next);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		next.glyphs = glyphs;
		glyphs[next.glyph_count] = (struct document_glyph){
			.name = strdup(name),
			.present = 1u << ST_THEME_GLYPH_ASCII,
			.resolved_present = 1u << ST_THEME_GLYPH_ASCII,
		};
		glyphs[next.glyph_count].values[ST_THEME_GLYPH_ASCII]
		    = (uint8_t)ascii;
		resolve_document_glyph(&glyphs[next.glyph_count]);
		if (glyphs[next.glyph_count].name == NULL) {
			state_dispose(&next);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		++next.glyph_count;
	}
	status = commit_state(document, &next, error, error_size);
	state_dispose(&next);
	return status;
}

int
st_theme_document_remove_glyph(struct st_theme_document *document,
	const char *name, char *error, size_t error_size)
{
	struct document_state next = {0};
	int status;

	if (document == NULL || name == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!state_clone(&next, current_state(document)))
		return IO_ERROR_OUT_OF_MEMORY;
	int index = glyph_index(&next, name);
	if (index < 0 || next.glyphs[index].removed) {
		state_dispose(&next);
		set_document_error(error, error_size, "glyph does not exist");
		return IO_ERROR_NOT_FOUND;
	}
	if (next.glyphs[index].builtin) {
		state_dispose(&next);
		set_document_error(error, error_size,
		    "built-in glyphs cannot be removed");
		return IO_ERROR_INVALID_ARGUMENT;
	}
	next.glyphs[index].removed = true;
	status = commit_state(document, &next, error, error_size);
	state_dispose(&next);
	return status;
}

const struct st_theme *
st_theme_document_preview(const struct st_theme_document *document)
{
	return document == NULL ? NULL : document->preview;
}

bool
st_theme_document_dirty(const struct st_theme_document *document)
{
	return document != NULL && (document->filename[0] == '\0'
	    || document->position != 0);
}

bool
st_theme_document_can_undo(const struct st_theme_document *document)
{
	return document != NULL && document->position != 0;
}

bool
st_theme_document_can_redo(const struct st_theme_document *document)
{
	return document != NULL && document->position + 1 < document->state_count;
}

static bool
move_history(struct st_theme_document *document, size_t position)
{
	struct st_theme *preview = preview_from_state(document,
	    &document->states[position]);

	if (preview == NULL)
		return false;
	document->position = position;
	theme_free(document->preview);
	document->preview = preview;
	return true;
}

bool
st_theme_document_undo(struct st_theme_document *document)
{
	return st_theme_document_can_undo(document)
	    && move_history(document, document->position - 1);
}

bool
st_theme_document_redo(struct st_theme_document *document)
{
	return st_theme_document_can_redo(document)
	    && move_history(document, document->position + 1);
}

int
st_theme_document_import(struct st_theme_document *document,
	const char *filename, char *error, size_t error_size)
{
	struct st_theme *theme = NULL;
	struct document_state imported = {0};
	struct document_state next = {0};
	int status;

	if (document == NULL || filename == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (filename[0] == '\0') {
		theme = build_classic_theme(document->manager->default_theme,
		    &document->manager->classic_settings);
		status = theme == NULL ? IO_ERROR_OUT_OF_MEMORY : 0;
	}
	else
		status = load_file(document->manager, filename, &theme, error,
		    error_size);
	if (status != 0)
		return status;
	if (!state_from_theme(&imported, document->manager, theme, NULL, true)
	    || !state_clone(&next, current_state(document))) {
		state_dispose(&imported);
		state_dispose(&next);
		theme_free(theme);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	for (size_t i = 0; i < next.style_count; ++i)
		free(next.styles[i].role);
	free(next.styles);
	for (size_t i = 0; i < next.glyph_count; ++i)
		free(next.glyphs[i].name);
	free(next.glyphs);
	next.styles = imported.styles;
	next.style_count = imported.style_count;
	next.glyphs = imported.glyphs;
	next.glyph_count = imported.glyph_count;
	next.replace_definitions = true;
	imported.styles = NULL;
	imported.style_count = 0;
	imported.glyphs = NULL;
	imported.glyph_count = 0;
	status = commit_state(document, &next, error, error_size);
	state_dispose(&next);
	state_dispose(&imported);
	theme_free(theme);
	return status;
}

static struct document_style *
find_document_style(struct document_state *state, const char *name)
{
	int index = style_index(state, name);
	return index < 0 ? NULL : &state->styles[index];
}

static struct document_glyph *
find_document_glyph(struct document_state *state, const char *name)
{
	int index = glyph_index(state, name);
	return index < 0 ? NULL : &state->glyphs[index];
}

static int
remove_definition_sections(ini_document_t *output)
{
	for (size_t i = ini_document_section_count(output); i != 0; --i) {
		ini_section_t *section = ini_document_section_at(output, i - 1);
		const char *name = ini_section_name(section);

		if ((ascii_case_starts_with(name, "Style:")
		    || ascii_case_starts_with(name, "Glyph:"))
		    && !ini_document_remove_section(output, section))
			return IO_ERROR_IO;
	}
	return 0;
}

static int
build_output(struct st_theme_document *document, ini_document_t **result)
{
	ini_document_t *output = NULL;
	struct document_state *state = current_state(document);
	int status = ini_document_clone(&output, document->source);

	if (status != 0)
		return status;
	ini_section_t *root = ini_document_root(output);
	for (unsigned i = 0; i < ST_THEME_METADATA_FIELD_COUNT; ++i) {
		if ((state->metadata_present & (1u << i)) != 0)
			status = ini_section_set_string(root, metadata_keys[i],
			    state->metadata[i], &ini_default_style);
		else
			(void)ini_section_remove_key(root, metadata_keys[i]);
		if (status != 0)
			goto failed;
	}
	if (state->replace_definitions) {
		status = remove_definition_sections(output);
		if (status != 0)
			goto failed;
	}
	for (size_t i = ini_document_section_count(output); i != 0; --i) {
		ini_section_t *section = ini_document_section_at(output, i - 1);
		const char *name = ini_section_name(section);
		bool remove = false;

		if (ascii_case_starts_with(name, "Style:")) {
			struct document_style *style = find_document_style(state, name + 6);
			remove = style == NULL || style->removed;
		}
		else if (ascii_case_starts_with(name, "Glyph:")) {
			struct document_glyph *glyph = find_document_glyph(state, name + 6);
			remove = glyph == NULL || glyph->removed;
		}
		if (remove)
			(void)ini_document_remove_section(output, section);
	}
	for (size_t i = 0; i < state->style_count; ++i) {
		struct document_style *style = &state->styles[i];
		ini_section_t *section;
		char *name;

		if (style->removed)
			continue;
		size_t size = strlen(style->role) + 7;
		name = malloc(size);
		if (name == NULL) {
			status = IO_ERROR_OUT_OF_MEMORY;
			goto failed;
		}
		(void)snprintf(name, size, "Style:%s", style->role);
		section = ini_document_find_section(output, name);
		if (section == NULL && (style->present != 0 || !style->builtin))
			status = ini_document_add_section(output, name, &ini_default_style,
			    &section);
		free(name);
		if (status != 0)
			goto failed;
		if (section == NULL)
			continue;
		for (unsigned field = 0; field < ST_THEME_STYLE_FIELD_COUNT; ++field) {
			if ((style->present & (1u << field)) == 0) {
				(void)ini_section_remove_key(section, style_keys[field]);
				continue;
			}
			if (style->values[field] == ST_THEME_INHERIT)
				status = ini_section_set_string(section, style_keys[field],
				    "inherit", &ini_default_style);
			else
				status = ini_section_set_int64(section, style_keys[field],
				    style->values[field], &ini_default_style);
			if (status != 0)
				goto failed;
		}
	}
	for (size_t i = 0; i < state->glyph_count; ++i) {
		struct document_glyph *glyph = &state->glyphs[i];
		ini_section_t *section;
		char *name;

		if (glyph->removed)
			continue;
		size_t size = strlen(glyph->name) + 7;
		name = malloc(size);
		if (name == NULL) {
			status = IO_ERROR_OUT_OF_MEMORY;
			goto failed;
		}
		(void)snprintf(name, size, "Glyph:%s", glyph->name);
		section = ini_document_find_section(output, name);
		if (section == NULL && (glyph->present != 0 || !glyph->builtin))
			status = ini_document_add_section(output, name, &ini_default_style,
			    &section);
		free(name);
		if (status != 0)
			goto failed;
		if (section == NULL)
			continue;
		for (unsigned field = 0; field < ST_THEME_GLYPH_FIELD_COUNT; ++field) {
			if ((glyph->present & (1u << field)) == 0)
				(void)ini_section_remove_key(section, glyph_keys[field]);
			else
				status = ini_section_set_int64(section, glyph_keys[field],
				    glyph->values[field], &ini_default_style);
			if (status != 0)
				goto failed;
		}
	}
	*result = output;
	return 0;

failed:
	ini_document_free(&output);
	return status;
}

static bool
same_filename(const char *left, const char *right)
{
#ifdef _WIN32
	return ascii_case_compare(left, right) == 0;
#else
	return strcmp(left, right) == 0;
#endif
}

static void
clear_history(struct st_theme_document *document)
{
	struct document_state saved = document->states[document->position];

	document->states[document->position] = (struct document_state){0};
	for (size_t i = 0; i < document->state_count; ++i)
		state_dispose(&document->states[i]);
	document->states[0] = saved;
	document->state_count = 1;
	document->position = 0;
}

static enum st_theme_document_error
write_document(const char *path, const char *data, size_t size, bool exclusive,
	char *error, size_t error_size)
{
	FILE *stream = NULL;
	int status = fs_open(&stream, path, exclusive ? "wbx" : "wb");

	if (status == IO_ERROR_ALREADY_EXISTS && exclusive) {
		set_document_error(error, error_size,
		    "the theme file already exists");
		return ST_THEME_DOCUMENT_EXISTS;
	}
	if (status != 0) {
		set_document_error(error, error_size, "unable to open theme file");
		return ST_THEME_DOCUMENT_IO;
	}
	bool wrote = size == 0 || fwrite(data, 1, size, stream) == size;
	if (fclose(stream) != 0)
		wrote = false;
	if (!wrote) {
		set_document_error(error, error_size,
		    "unable to write theme file");
		return ST_THEME_DOCUMENT_IO;
	}
	return ST_THEME_DOCUMENT_OK;
}

enum st_theme_document_error
st_theme_document_save(struct st_theme_document *document,
	const char *filename, bool force, char *error, size_t error_size)
{
	ini_document_t *output = NULL;
	char *path = NULL;
	char *serialized = NULL;
	char *saved_filename = NULL;
	size_t serialized_size = 0;
	char *current = NULL;
	size_t current_size = 0;
	bool same;
	int status;

	if (document == NULL)
		return ST_THEME_DOCUMENT_INVALID;
	if (filename == NULL || filename[0] == '\0')
		filename = document->filename;
	if (!st_theme_valid_filename(filename)) {
		set_document_error(error, error_size, "invalid theme filename");
		return ST_THEME_DOCUMENT_INVALID;
	}
	status = path_join_alloc(&path, document->manager->directory, filename);
	if (status != 0)
		return ST_THEME_DOCUMENT_IO;
	status = fs_create_directories(document->manager->directory);
	if (status != 0) {
		free(path);
		set_document_error(error, error_size,
		    "unable to create the themes directory");
		return ST_THEME_DOCUMENT_IO;
	}
	same = document->filename[0] != '\0'
	    && same_filename(document->filename, filename);
	if (!force && same) {
		status = read_original(path, &current, &current_size);
		if (status != 0 || current_size != document->original_size
		    || memcmp(current, document->original, current_size) != 0) {
			free(current);
			free(path);
			set_document_error(error, error_size,
			    "the theme file changed after it was opened");
			return ST_THEME_DOCUMENT_CONFLICT;
		}
		free(current);
	}
	else if (!force && !same) {
		struct fs_info info;
		status = fs_get_info(&info, path);
		if (status == 0) {
			free(path);
			set_document_error(error, error_size,
			    "the theme file already exists");
			return ST_THEME_DOCUMENT_EXISTS;
		}
		if (status != IO_ERROR_NOT_FOUND) {
			free(path);
			set_document_error(error, error_size,
			    "unable to inspect the theme destination");
			return ST_THEME_DOCUMENT_IO;
		}
	}
	status = build_output(document, &output);
	if (status == 0)
		status = ini_document_serialize(output, &serialized, &serialized_size);
	if (status == 0) {
		saved_filename = strdup(filename);
		if (saved_filename == NULL)
			status = IO_ERROR_OUT_OF_MEMORY;
	}
	enum st_theme_document_error write_result = ST_THEME_DOCUMENT_IO;
	if (status == 0)
		write_result = write_document(path, serialized, serialized_size,
		    !force && !same, error, error_size);
	free(path);
	if (status != 0 || write_result != ST_THEME_DOCUMENT_OK) {
		ini_document_free(&output);
		free(serialized);
		free(saved_filename);
		if (status != 0)
			set_document_error(error, error_size, "unable to save theme file");
		return status != 0 ? ST_THEME_DOCUMENT_IO : write_result;
	}
	free(document->filename);
	document->filename = saved_filename;
	ini_document_free(&document->source);
	document->source = output;
	free(document->original);
	document->original = serialized;
	document->original_size = serialized_size;
	clear_history(document);
	return ST_THEME_DOCUMENT_OK;
}
