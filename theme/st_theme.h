/* SyncTERM menu themes and local theme files. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_THEME_H_
#define ST_THEME_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ST_THEME_INHERIT (-1)

struct st_app_paths;
struct st_settings;
struct st_theme_manager;
struct st_theme_document;
struct st_theme_prepared;

struct st_theme_style {
	char *role;
	int font;
	int legacy_attr;
	int foreground;
	int background;
};

struct st_theme_glyph {
	char *name;
	uint32_t codepoint;
	unsigned present;
	uint8_t cp437;
	uint8_t ascii;
};

struct st_theme {
	char *filename;
	char *package;
	char *name;
	char *author;
	char *description;
	char *version;
	struct st_theme_style *styles;
	size_t style_count;
	struct st_theme_glyph *glyphs;
	size_t glyph_count;
};

struct st_theme_catalog_entry {
	char *filename;
	char *name;
	char *author;
	char *description;
	char *version;
	char *error;
};

struct st_cloud_theme_entry {
	char *package;
	char *name;
	char *author;
	char *description;
	char *version;
	char *uri;
	char *sha256;
	bool cached;
	bool update_available;
	char *error;
	struct st_theme *theme;
};

int st_theme_manager_create(struct st_theme_manager **result,
	const struct st_app_paths *paths, const struct st_settings *settings,
	char *error, size_t error_size);
void st_theme_manager_free(struct st_theme_manager **manager);

const struct st_theme *st_theme_active(const struct st_theme_manager *manager);
const struct st_theme *st_theme_default(const struct st_theme_manager *manager);
uint64_t st_theme_generation(const struct st_theme_manager *manager);

/* Rebuilds and installs the selected theme from a settings snapshot. */
int st_theme_apply_settings(struct st_theme_manager *manager,
	const struct st_settings *settings, char *error, size_t error_size);
/* Prepare all fallible work without changing the active theme. */
int st_theme_prepare_settings(struct st_theme_prepared **result,
	struct st_theme_manager *manager, const struct st_settings *settings,
	char *error, size_t error_size);
/* Installs and consumes a prepared theme. */
void st_theme_install_prepared(struct st_theme_manager *manager,
	struct st_theme_prepared **prepared);
void st_theme_prepared_free(struct st_theme_prepared **prepared);

/*
 * Replaces the catalog only after a successful refresh.  A missing themes
 * directory is a successful Classic-only catalog.  Returned entries are
 * borrowed and remain valid until the next successful refresh or manager
 * destruction.
 */
int st_theme_catalog_refresh(struct st_theme_manager *manager,
	char *error, size_t error_size);
size_t st_theme_catalog_count(const struct st_theme_manager *manager);
const struct st_theme_catalog_entry *st_theme_catalog_at(
	const struct st_theme_manager *manager, size_t index);

/* Empty filename means the built-in Classic theme. */
int st_theme_preview(struct st_theme_manager *manager, const char *filename,
	char *error, size_t error_size);
void st_theme_cancel_preview(struct st_theme_manager *manager);
int st_theme_select(struct st_theme_manager *manager, const char *filename,
	char *error, size_t error_size);
int st_theme_delete(struct st_theme_manager *manager, const char *filename,
	char *error, size_t error_size);

int st_theme_cloud_refresh(struct st_theme_manager *manager, bool fetch,
	const char *selected_package, char *warning, size_t warning_size);
size_t st_theme_cloud_count(const struct st_theme_manager *manager);
const struct st_cloud_theme_entry *st_theme_cloud_at(
	const struct st_theme_manager *manager, size_t index);
bool st_theme_valid_package(const char *package);
int st_theme_cloud_cache(struct st_theme_manager *manager,
	const char *package, char *error, size_t error_size);
int st_theme_cloud_preview(struct st_theme_manager *manager,
	const char *package, char *error, size_t error_size);
int st_theme_cloud_select(struct st_theme_manager *manager,
	const char *package, char *error, size_t error_size);
int st_theme_cloud_delete(struct st_theme_manager *manager,
	const char *package, char *error, size_t error_size);
int st_theme_cloud_copy_document(struct st_theme_document **result,
	struct st_theme_manager *manager, const char *package, char *error,
	size_t error_size);

bool st_theme_valid_filename(const char *filename);

enum st_theme_style_field {
	ST_THEME_STYLE_FONT,
	ST_THEME_STYLE_LEGACY_ATTR,
	ST_THEME_STYLE_FOREGROUND,
	ST_THEME_STYLE_BACKGROUND,
	ST_THEME_STYLE_FIELD_COUNT,
};

enum st_theme_metadata_field {
	ST_THEME_METADATA_NAME,
	ST_THEME_METADATA_AUTHOR,
	ST_THEME_METADATA_DESCRIPTION,
	ST_THEME_METADATA_VERSION,
	ST_THEME_METADATA_FIELD_COUNT,
};

enum st_theme_glyph_field {
	ST_THEME_GLYPH_CP437,
	ST_THEME_GLYPH_ASCII,
	ST_THEME_GLYPH_CODEPOINT,
	ST_THEME_GLYPH_FIELD_COUNT,
};

enum st_theme_value_mode {
	ST_THEME_VALUE_DEFAULT,
	ST_THEME_VALUE_INHERIT,
	ST_THEME_VALUE_EXPLICIT,
};

enum st_theme_document_error {
	ST_THEME_DOCUMENT_OK,
	ST_THEME_DOCUMENT_INVALID,
	ST_THEME_DOCUMENT_CONFLICT,
	ST_THEME_DOCUMENT_EXISTS,
	ST_THEME_DOCUMENT_READ_ONLY,
	ST_THEME_DOCUMENT_IO,
};

struct st_theme_document_style {
	const char *role;
	bool builtin;
	unsigned present;
	int values[ST_THEME_STYLE_FIELD_COUNT];
};

struct st_theme_document_glyph {
	const char *name;
	bool builtin;
	unsigned present;
	uint32_t values[ST_THEME_GLYPH_FIELD_COUNT];
};

/* Documents borrow manager and must be destroyed before it. */
int st_theme_document_create(struct st_theme_document **result,
	struct st_theme_manager *manager, char *error, size_t error_size);
int st_theme_document_open(struct st_theme_document **result,
	struct st_theme_manager *manager, const char *filename, char *error,
	size_t error_size);
int st_theme_document_copy_path(struct st_theme_document **result,
	struct st_theme_manager *manager, const char *path, char *error,
	size_t error_size);
void st_theme_document_free(struct st_theme_document **document);
const char *st_theme_document_filename(const struct st_theme_document *document);
const char *st_theme_document_metadata(const struct st_theme_document *document,
	unsigned field);
int st_theme_document_set_metadata(struct st_theme_document *document,
	unsigned field, const char *value, char *error, size_t error_size);
size_t st_theme_document_style_count(const struct st_theme_document *document);
bool st_theme_document_style_at(const struct st_theme_document *document,
	size_t index, struct st_theme_document_style *result);
int st_theme_document_set_style(struct st_theme_document *document,
	const char *role, unsigned field, enum st_theme_value_mode mode, int value,
	char *error, size_t error_size);
int st_theme_document_add_style(struct st_theme_document *document,
	const char *role, char *error, size_t error_size);
int st_theme_document_remove_style(struct st_theme_document *document,
	const char *role, char *error, size_t error_size);
size_t st_theme_document_glyph_count(const struct st_theme_document *document);
bool st_theme_document_glyph_at(const struct st_theme_document *document,
	size_t index, struct st_theme_document_glyph *result);
int st_theme_document_set_glyph(struct st_theme_document *document,
	const char *name, unsigned field, enum st_theme_value_mode mode,
	uint32_t value, char *error, size_t error_size);
int st_theme_document_add_glyph(struct st_theme_document *document,
	const char *name, int ascii, char *error, size_t error_size);
int st_theme_document_remove_glyph(struct st_theme_document *document,
	const char *name, char *error, size_t error_size);
const struct st_theme *st_theme_document_preview(
	const struct st_theme_document *document);
bool st_theme_document_dirty(const struct st_theme_document *document);
bool st_theme_document_can_undo(const struct st_theme_document *document);
bool st_theme_document_can_redo(const struct st_theme_document *document);
bool st_theme_document_undo(struct st_theme_document *document);
bool st_theme_document_redo(struct st_theme_document *document);
int st_theme_document_import(struct st_theme_document *document,
	const char *filename, char *error, size_t error_size);
/*
 * Save writes through the destination rather than replacing its directory
 * entry. A successful save establishes the current state as clean and clears
 * history. A write or close failure can leave partial destination contents.
 */
enum st_theme_document_error st_theme_document_save(
	struct st_theme_document *document, const char *filename, bool force,
	char *error, size_t error_size);

#endif
