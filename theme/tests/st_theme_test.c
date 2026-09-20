/* Tests for local SyncTERM themes and editable theme documents. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_theme.h"
#include "theme_internal.h"

#include "filesystem.h"
#include "io_error.h"
#include "path.h"
#include "st_app_paths.h"
#include "st_settings.h"
#include "string_compat.h"
#include "xp_crypto.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, \
		    #expression); \
		return 1; \
	} \
} while (0)

static int
write_text(const char *path, const char *text)
{
	FILE *stream = NULL;
	int status = fs_open(&stream, path, "wb");

	if (status != 0)
		return status;
	if (fwrite(text, 1, strlen(text), stream) != strlen(text)
	    || fclose(stream) != 0)
		return -1;
	return 0;
}

static bool
file_contains(const char *path, const char *text)
{
	FILE *stream = NULL;
	char buffer[4096];
	int status = fs_open(&stream, path, "rb");

	if (status != 0)
		return false;
	size_t size = fread(buffer, 1, sizeof(buffer) - 1, stream);
	bool okay = !ferror(stream) && fclose(stream) == 0;
	buffer[size] = '\0';
	return okay && strstr(buffer, text) != NULL;
}

static const struct st_theme_style *
style(const struct st_theme *theme, const char *role)
{
	for (size_t i = 0; i < theme->style_count; ++i)
		if (strcmp(theme->styles[i].role, role) == 0)
			return &theme->styles[i];
	return NULL;
}

static const struct st_theme_glyph *
glyph(const struct st_theme *theme, const char *name)
{
	for (size_t i = 0; i < theme->glyph_count; ++i)
		if (strcmp(theme->glyphs[i].name, name) == 0)
			return &theme->glyphs[i];
	return NULL;
}

static bool
document_glyph(const struct st_theme_document *document, const char *name,
	struct st_theme_document_glyph *result)
{
	for (size_t i = 0; i < st_theme_document_glyph_count(document); ++i) {
		if (st_theme_document_glyph_at(document, i, result)
		    && strcmp(result->name, name) == 0)
			return true;
	}
	return false;
}

static int
test_catalog_refresh_contract(const char *directory)
{
	struct st_app_paths paths = {.themes_directory = (char *)directory};
	struct st_settings settings;
	struct st_theme_manager *manager = NULL;
	char *theme_path = NULL;
	char error[256] = "";
	int status;

	(void)fs_remove_file(directory);
	CHECK(fs_create_directories(directory) == 0);
	CHECK(path_join_alloc(&theme_path, directory, "catalog.ini") == 0);
	CHECK(write_text(theme_path, "Name=Catalog Theme\n") == 0);
	st_settings_init(&settings);
	CHECK(st_theme_manager_create(&manager, &paths, &settings, error,
	    sizeof(error)) == 0);
	CHECK(st_theme_catalog_refresh(manager, error, sizeof(error)) == 0);
	CHECK(st_theme_catalog_count(manager) == 2);
	CHECK(strcmp(st_theme_catalog_at(manager, 1)->filename, "catalog.ini") == 0);
	CHECK(fs_remove_file(theme_path) == 0);
	CHECK(fs_remove_directory(directory) == 0);
	CHECK(write_text(directory, "not a directory\n") == 0);
	status = st_theme_catalog_refresh(manager, error, sizeof(error));
	CHECK(status != 0);
	CHECK(st_theme_catalog_count(manager) == 2);
	CHECK(strcmp(st_theme_catalog_at(manager, 1)->filename, "catalog.ini") == 0);
	CHECK(fs_remove_file(directory) == 0);
	CHECK(st_theme_catalog_refresh(manager, error, sizeof(error)) == 0);
	CHECK(st_theme_catalog_count(manager) == 1);
	CHECK(st_theme_catalog_at(manager, 0)->filename[0] == '\0');
	st_theme_manager_free(&manager);
	st_settings_dispose(&settings);
	free(theme_path);
	return 0;
}

static int
test_short_read_closes_stream(const char *path)
{
	FILE *stream = NULL;
	char *data = NULL;

	CHECK(write_text(path, "x") == 0);
	CHECK(fs_open(&stream, path, "rb") == 0);
	CHECK(theme_read_stream_exact(stream, 2, &data) == IO_ERROR_IO);
	CHECK(data == NULL);
	CHECK(fs_remove_file(path) == 0);
	return 0;
}

static int
test_failed_document_construction(struct st_theme_manager *manager)
{
	struct st_theme_style *default_style = find_style(manager->default_theme,
	    "default");
	struct st_theme_document *document = NULL;
	char error[256] = "";
	int saved_font;
	int status;

	CHECK(default_style != NULL);
	saved_font = default_style->font;
	default_style->font = -1;
	status = st_theme_document_create(&document, manager, error, sizeof(error));
	default_style->font = saved_font;
	CHECK(status != 0);
	CHECK(document == NULL);
	return 0;
}

static int
test_branch_dirty(struct st_theme_manager *manager, const char *path)
{
	struct st_theme_document *document = NULL;
	char error[256] = "";
	bool dirty;

	CHECK(write_text(path, "Name=History\n") == 0);
	CHECK(st_theme_document_open(&document, manager, "saved.ini", error,
	    sizeof(error)) == 0);
	CHECK(st_theme_document_set_metadata(document, ST_THEME_METADATA_AUTHOR,
	    "saved", error, sizeof(error)) == 0);
	CHECK(st_theme_document_save(document, NULL, false, error, sizeof(error))
	    == ST_THEME_DOCUMENT_OK);
	CHECK(!st_theme_document_dirty(document));
	CHECK(!st_theme_document_can_undo(document));
	CHECK(!st_theme_document_can_redo(document));
	CHECK(st_theme_document_set_metadata(document, ST_THEME_METADATA_AUTHOR,
	    "different", error, sizeof(error)) == 0);
	dirty = st_theme_document_dirty(document);
	CHECK(st_theme_document_can_undo(document));
	CHECK(st_theme_document_undo(document));
	CHECK(!st_theme_document_dirty(document));
	CHECK(st_theme_document_can_redo(document));
	CHECK(st_theme_document_redo(document));
	CHECK(st_theme_document_dirty(document));
	CHECK(write_text(path, "Name=Externally changed\n") == 0);
	CHECK(st_theme_document_save(document, NULL, false, error, sizeof(error))
	    == ST_THEME_DOCUMENT_CONFLICT);
	CHECK(st_theme_document_dirty(document));
	CHECK(st_theme_document_can_undo(document));
	st_theme_document_free(&document);
	if (!dirty)
		fprintf(stderr, "unsaved history branch reported clean\n");
	CHECK(dirty);
	return 0;
}

static int
test_reset_last_override(struct st_theme_manager *manager, const char *path)
{
	struct st_theme_document *document = NULL;
	char error[256] = "";
	int expected_style;
	uint32_t expected_glyph;

	CHECK(write_text(path,
	    "Name=Reset\n"
	    "[Style:frame]\n; retained style comment\n"
	    "Foreground=0x123456\nVendorStyle=kept\n"
	    "[Glyph:arrow.up]\n; retained glyph comment\n"
	    "ASCII=65\nVendorGlyph=kept\n") == 0);
	CHECK(st_theme_document_open(&document, manager, "saved.ini", error,
	    sizeof(error)) == 0);
	CHECK(st_theme_document_set_style(document, "frame",
	    ST_THEME_STYLE_FOREGROUND, ST_THEME_VALUE_DEFAULT, 0, error,
	    sizeof(error)) == 0);
	CHECK(st_theme_document_set_glyph(document, "arrow.up",
	    ST_THEME_GLYPH_ASCII, ST_THEME_VALUE_DEFAULT, 0, error,
	    sizeof(error)) == 0);
	expected_style = style(st_theme_document_preview(document), "frame")
	    ->foreground;
	expected_glyph = glyph(st_theme_document_preview(document), "arrow.up")
	    ->codepoint;
	CHECK(st_theme_document_save(document, NULL, false, error, sizeof(error))
	    == ST_THEME_DOCUMENT_OK);
	CHECK(file_contains(path, "; retained style comment"));
	CHECK(file_contains(path, "VendorStyle=kept"));
	CHECK(file_contains(path, "; retained glyph comment"));
	CHECK(file_contains(path, "VendorGlyph=kept"));
	CHECK(!file_contains(path, "Foreground="));
	CHECK(!file_contains(path, "ASCII="));
	st_theme_document_free(&document);
	CHECK(st_theme_document_open(&document, manager, "saved.ini", error,
	    sizeof(error)) == 0);
	CHECK(style(st_theme_document_preview(document), "frame")->foreground
	    == expected_style);
	CHECK(glyph(st_theme_document_preview(document), "arrow.up")->codepoint
	    == expected_glyph);
	CHECK(st_theme_document_add_style(document, "custom.empty", error,
	    sizeof(error)) == 0);
	CHECK(st_theme_document_save(document, NULL, false, error, sizeof(error))
	    == ST_THEME_DOCUMENT_OK);
	CHECK(file_contains(path, "[Style:custom.empty]"));
	st_theme_document_free(&document);
	CHECK(st_theme_document_open(&document, manager, "saved.ini", error,
	    sizeof(error)) == 0);
	CHECK(style(st_theme_document_preview(document), "custom.empty") != NULL);
	st_theme_document_free(&document);
	return 0;
}

int
main(void)
{
	char cache[sizeof(ST_THEME_TEST_DIRECTORY) + 16];
	char cloud_directory[sizeof(ST_THEME_TEST_DIRECTORY) + 40];
	char manifest[sizeof(ST_THEME_TEST_DIRECTORY) + 56];
	char cloud_theme[sizeof(ST_THEME_TEST_DIRECTORY) + 56];
	struct st_app_paths paths = {
		.themes_directory = ST_THEME_TEST_DIRECTORY,
		.system_cache_directory = cache,
	};
	struct st_settings settings;
	struct st_theme_manager *manager = NULL;
	struct st_theme_document *document = NULL;
	char *custom = NULL;
	char *invalid = NULL;
	char *saved = NULL;
	char *catalog_directory = NULL;
	char *short_read = NULL;
	char error[256] = "";
	uint64_t generation;

	CHECK(fs_create_directories(ST_THEME_TEST_DIRECTORY) == 0);
	(void)snprintf(cache, sizeof(cache), "%s/cache", ST_THEME_TEST_DIRECTORY);
	(void)snprintf(cloud_directory, sizeof(cloud_directory),
	    "%s/themes/official", cache);
	(void)snprintf(manifest, sizeof(manifest), "%s/manifest.lst",
	    cloud_directory);
	(void)snprintf(cloud_theme, sizeof(cloud_theme), "%s/blue.ini",
	    cloud_directory);
	CHECK(fs_create_directories(cloud_directory) == 0);
	CHECK(path_join_alloc(&custom, ST_THEME_TEST_DIRECTORY, "custom.ini") == 0);
	CHECK(path_join_alloc(&invalid, ST_THEME_TEST_DIRECTORY, "invalid.ini") == 0);
	CHECK(path_join_alloc(&saved, ST_THEME_TEST_DIRECTORY, "saved.ini") == 0);
	CHECK(path_join_alloc(&catalog_directory, ST_THEME_TEST_DIRECTORY,
	    "catalog-transaction") == 0);
	CHECK(path_join_alloc(&short_read, ST_THEME_TEST_DIRECTORY,
	    "short-read.tmp") == 0);
	CHECK(write_text(custom,
	    "; retained comment\n"
	    "Name=Custom Theme\nAuthor=Tester\n\n"
	    "[Style:frame]\nForeground=0x123456\n\n"
	    "[Glyph:arrow.up]\nASCII=65\n\n"
	    "[Glyph:custom.ascii]\nASCII=63\n\n"
	    "[Glyph:custom.cp437]\nCP437=16\nASCII=62\n\n"
	    "[Glyph:custom.unicode]\nCodepoint=9731\nCP437=16\nASCII=42\n\n"
	    "[Glyph:custom.codepoint]\nCodepoint=9733\nASCII=43\n")
	    == 0);
	CHECK(write_text(invalid,
	    "Name=Broken\nName=Duplicate\n") == 0);
	CHECK(write_text(cloud_theme,
	    "Name=Cloud Theme\n[Style:frame]\nForeground=0x654321\n") == 0);
	CHECK(write_text(manifest,
	    "Repository=official\n\n[Theme:blue]\nName=Cloud Theme\n"
	    "URI=https://example.invalid/blue.ini\n"
	    "SHA256=1891d225f61ef1aaa8df82e47f540561a4829fb5cc62535a34b1454f9bb0bfd5\n") == 0);
	(void)fs_remove_file(saved);
	st_settings_init(&settings);
	settings.theme_frame_color = 16;
	settings.theme_text_color = 16;
	settings.theme_background_color = 8;
	settings.theme_inverse_color = 8;
	settings.theme_lightbar_color = 16;
	settings.theme_lightbar_background_color = 8;
	CHECK(st_theme_manager_create(&manager, &paths, &settings, error,
	    sizeof(error)) == 0);
	CHECK(test_failed_document_construction(manager) == 0);
	CHECK(st_theme_default(manager)->style_count == 48);
	CHECK(st_theme_active(manager)->style_count == 55);
	CHECK(style(st_theme_active(manager), "default")->legacy_attr == 0x1f);
	CHECK(style(st_theme_active(manager), "classic.header")->legacy_attr
	    == 0x31);
	CHECK(style(st_theme_active(manager), "classic.console")->legacy_attr
	    == 0x3e);
	CHECK(st_theme_catalog_refresh(manager, error, sizeof(error)) == 0);
	CHECK(st_theme_catalog_count(manager) == 3);
	generation = st_theme_generation(manager);
	CHECK(st_theme_preview(manager, "custom.ini", error, sizeof(error)) == 0);
	CHECK(st_theme_generation(manager) > generation);
	CHECK(style(st_theme_active(manager), "frame")->foreground == 0x123456);
	CHECK(glyph(st_theme_active(manager), "arrow.up")->ascii == 'A');
	CHECK(glyph(st_theme_active(manager), "arrow.up")->codepoint == 0x2191);
	CHECK(glyph(st_theme_active(manager), "custom.ascii")->cp437 == '?');
	CHECK(glyph(st_theme_active(manager), "custom.ascii")->codepoint == '?');
	CHECK(glyph(st_theme_active(manager), "custom.cp437")->codepoint == 0x25ba);
	CHECK(glyph(st_theme_active(manager), "custom.unicode")->codepoint == 0x2603);
	CHECK(glyph(st_theme_active(manager), "custom.codepoint")->cp437 == '+');
	CHECK(glyph(st_theme_active(manager), "custom.codepoint")->codepoint
	    == 0x2605);
	st_theme_cancel_preview(manager);
	CHECK(style(st_theme_active(manager), "frame")->foreground == 0xffff55);
	CHECK(write_text(invalid,
	    "Name=Broken\n[Glyph:bad]\nASCII=63\nCodepoint=55296\n") == 0);
	CHECK(st_theme_preview(manager, "invalid.ini", error, sizeof(error)) != 0);
	CHECK(write_text(invalid,
	    "Name=Broken\n[Glyph:bad]\nASCII=63\nCodepoint=1114112\n") == 0);
	CHECK(st_theme_preview(manager, "invalid.ini", error, sizeof(error)) != 0);
	CHECK(write_text(invalid,
	    "Name=Broken\n[Glyph:bad]\nASCII=63\nCodepoint=65\n"
	    "Codepoint=66\n") == 0);
	CHECK(st_theme_preview(manager, "invalid.ini", error, sizeof(error)) != 0);
	CHECK(write_text(invalid,
	    "Name=Broken\n[Glyph:bad]\nCodepoint=9731\n") == 0);
	CHECK(st_theme_preview(manager, "invalid.ini", error, sizeof(error)) != 0);

	CHECK(st_theme_document_open(&document, manager, "custom.ini", error,
	    sizeof(error)) == 0);
	struct st_theme_document_glyph editable_glyph;
	CHECK(document_glyph(document, "custom.unicode", &editable_glyph));
	CHECK(editable_glyph.values[ST_THEME_GLYPH_CODEPOINT] == 0x2603);
	CHECK((editable_glyph.present & (1u << ST_THEME_GLYPH_CODEPOINT)) != 0);
	CHECK(st_theme_document_set_glyph(document, "custom.unicode",
	    ST_THEME_GLYPH_CODEPOINT, ST_THEME_VALUE_DEFAULT, 0, error,
	    sizeof(error)) == 0);
	CHECK(glyph(st_theme_document_preview(document), "custom.unicode")
	    ->codepoint == 0x25ba);
	CHECK(st_theme_document_undo(document));
	CHECK(glyph(st_theme_document_preview(document), "custom.unicode")
	    ->codepoint == 0x2603);
	CHECK(st_theme_document_redo(document));
	CHECK(st_theme_document_set_glyph(document, "custom.unicode",
	    ST_THEME_GLYPH_CODEPOINT, ST_THEME_VALUE_EXPLICIT, 0x1f642, error,
	    sizeof(error)) == 0);
	CHECK(st_theme_document_add_glyph(document, "custom.added", '?', error,
	    sizeof(error)) == 0);
	CHECK(glyph(st_theme_document_preview(document), "custom.added")->codepoint
	    == '?');
	CHECK(st_theme_document_set_glyph(document, "custom.added",
	    ST_THEME_GLYPH_CP437, ST_THEME_VALUE_EXPLICIT, 16, error,
	    sizeof(error)) == 0);
	CHECK(glyph(st_theme_document_preview(document), "custom.added")->codepoint
	    == 0x25ba);
	CHECK(st_theme_document_set_glyph(document, "custom.added",
	    ST_THEME_GLYPH_CODEPOINT, ST_THEME_VALUE_EXPLICIT, 0x2606, error,
	    sizeof(error)) == 0);
	CHECK(glyph(st_theme_document_preview(document), "custom.added")->codepoint
	    == 0x2606);
	CHECK(strcmp(st_theme_document_metadata(document, ST_THEME_METADATA_NAME),
	    "Custom Theme") == 0);
	CHECK(st_theme_document_set_metadata(document, ST_THEME_METADATA_VERSION,
	    "2", error, sizeof(error)) == 0);
	CHECK(st_theme_document_can_undo(document));
	CHECK(st_theme_document_undo(document));
	CHECK(st_theme_document_metadata(document, ST_THEME_METADATA_VERSION)
	    == NULL);
	CHECK(st_theme_document_redo(document));
	CHECK(st_theme_document_add_style(document, "custom.notice", error,
	    sizeof(error)) == 0);
	CHECK(st_theme_document_set_style(document, "custom.notice",
	    ST_THEME_STYLE_FOREGROUND, ST_THEME_VALUE_EXPLICIT, 0xabcdef,
	    error, sizeof(error)) == 0);
	CHECK(st_theme_document_save(document, "saved.ini", false, error,
	    sizeof(error)) == ST_THEME_DOCUMENT_OK);
	CHECK(file_contains(saved, "; retained comment"));
	CHECK(file_contains(saved, "Codepoint=128578"));
	CHECK(file_contains(saved, "[Glyph:custom.added]"));
	CHECK(file_contains(saved, "Codepoint=9734"));
	CHECK(!st_theme_document_dirty(document));
	st_theme_document_free(&document);
	CHECK(st_theme_document_open(&document, manager, "saved.ini", error,
	    sizeof(error)) == 0);
	CHECK(strcmp(st_theme_document_metadata(document, ST_THEME_METADATA_VERSION),
	    "2") == 0);
	CHECK(style(st_theme_document_preview(document), "custom.notice")
	    ->foreground == 0xabcdef);
	CHECK(write_text(saved, "Name=Externally changed\n") == 0);
	CHECK(st_theme_document_save(document, NULL, false, error, sizeof(error))
	    == ST_THEME_DOCUMENT_CONFLICT);
	CHECK(st_theme_cloud_refresh(manager, false, "", error, sizeof(error)) == 0);
	CHECK(st_theme_cloud_count(manager) == 1);
	CHECK(st_theme_cloud_at(manager, 0)->cached);
	CHECK(st_theme_cloud_preview(manager, "official/blue", error,
	    sizeof(error)) == 0);
	CHECK(style(st_theme_active(manager), "frame")->foreground
	    == 0x654321);
	st_theme_cancel_preview(manager);
	st_theme_document_free(&document);
	CHECK(st_theme_cloud_copy_document(&document, manager,
	    "official/blue", error, sizeof(error)) == 0);
	CHECK(strcmp(st_theme_document_metadata(document,
	    ST_THEME_METADATA_NAME), "Cloud Theme") == 0);
	settings.theme_frame_color = 1;
	settings.theme_text_color = 7;
	settings.theme_background_color = 0;
	settings.theme_inverse_color = 7;
	settings.theme_lightbar_color = 15;
	settings.theme_lightbar_background_color = 1;
	CHECK(st_theme_apply_settings(manager, &settings, error,
	    sizeof(error)) == 0);
	CHECK(style(st_theme_active(manager), "default")->legacy_attr == 0x07);
	CHECK(style(st_theme_active(manager), "frame")->legacy_attr == 0x01);
	CHECK(style(st_theme_active(manager), "list.item.focused")->legacy_attr
	    == 0x1f);
	CHECK(style(st_theme_active(manager), "classic.header")->legacy_attr
	    == 0x70);
	CHECK(style(st_theme_active(manager), "classic.console")->legacy_attr
	    == 0x7e);
	CHECK(style(st_theme_active(manager), "classic.console.error")->legacy_attr
	    == 0x7c);
	settings.theme_frame_color = UINT_MAX;
	settings.theme_text_color = UINT_MAX;
	settings.theme_background_color = UINT_MAX;
	settings.theme_inverse_color = UINT_MAX;
	settings.theme_lightbar_color = UINT_MAX;
	settings.theme_lightbar_background_color = UINT_MAX;
	CHECK(st_theme_apply_settings(manager, &settings, error,
	    sizeof(error)) == 0);
	CHECK(style(st_theme_active(manager), "default")->legacy_attr == 0x1f);
	CHECK(style(st_theme_active(manager), "frame")->legacy_attr == 0x1e);
	CHECK(style(st_theme_active(manager), "list.item.focused")->legacy_attr
	    == 0x71);
	CHECK(style(st_theme_active(manager), "classic.header")->legacy_attr
	    == 0x31);

	st_theme_document_free(&document);
	st_theme_manager_free(&manager);
	settings.theme_file = strdup("custom.ini");
	CHECK(settings.theme_file != NULL);
	CHECK(st_theme_manager_create(&manager, &paths, &settings, error,
	    sizeof(error)) == 0);
	CHECK(style(st_theme_active(manager), "frame")->foreground == 0x123456);
	st_theme_manager_free(&manager);
	free(settings.theme_file);
	settings.theme_file = NULL;
	settings.theme_package = strdup("official/blue");
	CHECK(settings.theme_package != NULL);
	CHECK(st_theme_manager_create(&manager, &paths, &settings, error,
	    sizeof(error)) == 0);
	CHECK(style(st_theme_active(manager), "frame")->foreground == 0x654321);
	int failure = test_branch_dirty(manager, saved);
	failure += test_reset_last_override(manager, saved);
	st_theme_manager_free(&manager);
	st_settings_dispose(&settings);
	failure += test_catalog_refresh_contract(catalog_directory);
	failure += test_short_read_closes_stream(short_read);
	free(custom);
	free(invalid);
	free(saved);
	free(catalog_directory);
	free(short_read);
	return failure;
}
