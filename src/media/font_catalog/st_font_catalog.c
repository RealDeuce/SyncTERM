/* Console-specific registration of built-in and custom terminal fonts. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_font_catalog.h"

#include "filesystem.h"
#include "io_error.h"
#include "st_builtin_fonts.h"
#include "st_presentation_font.h"
#include "st_unicode_fonts.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct st_font_catalog {
	st_console_t *console;
	uint16_t identifiers[ST_FONT_CATALOG_SELECTOR_COUNT];
	bool valid[ST_FONT_CATALOG_SELECTOR_COUNT];
	enum st_codepage codepages[ST_FONT_CATALOG_SELECTOR_COUNT];
	uint8_t selector_by_identifier[UINT16_MAX + 1U];
	bool identifier_valid[UINT16_MAX + 1U];
};

int
st_font_catalog_create(st_font_catalog_t **result, st_console_t *console)
{
	st_font_catalog_t *catalog;
	struct st_console_font font;
	uint32_t width;
	uint32_t height;

	if (result == NULL || console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_console_cell_pixel_size(console, &width, &height) != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	catalog = calloc(1, sizeof(*catalog));
	if (catalog == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	catalog->console = console;
	for (size_t selector = 0; selector < st_builtin_font_count()
	    && selector < ST_FONT_CATALOG_SELECTOR_COUNT; ++selector) {
		uint16_t identifier;
		int status;

		if (!st_builtin_font_get(selector, width, height, &font))
			continue;
		if (width == 8 && (height == 8 || height == 14)) {
			struct st_presentation_font presentation = {0};
			/* Keep these fonts usable without a separately installed fallback. */
			status = st_presentation_font_create(&presentation, &font);
			if (status != 0) {
				free(catalog);
				return status;
			}
			status = st_console_register_font_copy(console,
			    &presentation.font, &identifier);
			st_presentation_font_destroy(&presentation);
		}
		else
			status = st_console_register_static_font(console, &font, &identifier);
		if (status != 0) {
			free(catalog);
			return status;
		}
		catalog->identifiers[selector] = identifier;
		catalog->valid[selector] = true;
		catalog->codepages[selector] = st_builtin_font_codepage(selector);
		catalog->selector_by_identifier[identifier] = (uint8_t)selector;
		catalog->identifier_valid[identifier] = true;
	}
	if (width == 8 && (height == 8 || height == 14 || height == 16)) {
		uint16_t fallback;
		int status = st_console_get_fallback_font(console, width, height, &fallback);
		if (status == IO_ERROR_NOT_FOUND) {
			struct st_presentation_font presentation = {0};
			if (height == 8) font = st_unscii_8_font;
			else if (height == 16) font = st_unscii_16_font;
			else if (!st_builtin_font_get(0, width, height, &font)) {
				free(catalog);
				return IO_ERROR_NOT_SUPPORTED;
			}
			status = st_presentation_font_create_fallback(&presentation, &font);
			if (status == 0)
				status = st_console_register_font_copy(console, &presentation.font, &fallback);
			st_presentation_font_destroy(&presentation);
			if (status == 0) status = st_console_set_fallback_font(console, fallback);
		}
		if (status != 0) {
			free(catalog);
			return status;
		}
	}
	if (!catalog->valid[0]) {
		catalog->identifiers[0] = 0;
		catalog->valid[0] = true;
		catalog->selector_by_identifier[0] = 0;
		catalog->identifier_valid[0] = true;
	}
	*result = catalog;
	return 0;
}

void
st_font_catalog_destroy(st_font_catalog_t *catalog)
{
	free(catalog);
}

int
st_font_catalog_register_custom(st_console_t *console, const char *path,
	uint16_t *identifier)
{
	return st_font_catalog_register_custom_codepage(console, path,
	    ST_CODEPAGE_CP437, identifier, NULL);
}

static int
load_failure(struct st_font_load_error *error, enum st_font_load_failure failure,
	int status, int native_error)
{
	if (error != NULL)
		*error = (struct st_font_load_error){failure, status, native_error};
	return status;
}

int
st_font_catalog_register_custom_codepage(st_console_t *console,
	const char *path, enum st_codepage codepage, uint16_t *identifier,
	struct st_font_load_error *error)
{
	struct st_console_font font, template;
	uint8_t *bitmap;
	FILE *stream;
	uint32_t width, height;
	size_t row_stride, glyph_stride, size;
	int status, native_error;
	int64_t file_size;

	if (error != NULL)
		*error = (struct st_font_load_error){0};
	if (console == NULL || path == NULL || identifier == NULL
	    || codepage < ST_CODEPAGE_CP437 || codepage >= ST_CODEPAGE_COUNT)
		return load_failure(error, ST_FONT_LOAD_REGISTRATION,
		    IO_ERROR_INVALID_ARGUMENT, 0);
	status = st_console_cell_pixel_size(console, &width, &height);
	if (status != 0)
		return load_failure(error, ST_FONT_LOAD_REGISTRATION, status, 0);
	row_stride = ((size_t)width + 7) / 8;
	if (height == 0 || row_stride == 0 || row_stride > SIZE_MAX / height)
		return load_failure(error, ST_FONT_LOAD_BITMAP, IO_ERROR_OUT_OF_RANGE, 0);
	glyph_stride = row_stride * height;
	if (glyph_stride > SIZE_MAX / 256)
		return load_failure(error, ST_FONT_LOAD_BITMAP, IO_ERROR_OUT_OF_RANGE, 0);
	size = glyph_stride * 256;
	errno = 0;
	status = fs_open(&stream, path, "rb");
	native_error = errno;
	if (status != 0)
		return load_failure(error, ST_FONT_LOAD_FILE, status, native_error);
	errno = 0;
	file_size = fs_stream_size(stream);
	native_error = errno;
	if (file_size < 0) {
		(void)fclose(stream);
		return load_failure(error, ST_FONT_LOAD_FILE, (int)file_size, native_error);
	}
	if ((uint64_t)file_size != size) {
		(void)fclose(stream);
		return load_failure(error, ST_FONT_LOAD_BITMAP, IO_ERROR_INVALID_ENCODING, 0);
	}
	bitmap = malloc(size);
	if (bitmap == NULL) {
		(void)fclose(stream);
		return load_failure(error, ST_FONT_LOAD_REGISTRATION, IO_ERROR_OUT_OF_MEMORY, 0);
	}
	errno = 0;
	if (fread(bitmap, 1, size, stream) != size) {
		native_error = errno;
		free(bitmap);
		(void)fclose(stream);
		return load_failure(error, ST_FONT_LOAD_FILE, IO_ERROR_IO, native_error);
	}
	errno = 0;
	status = fclose(stream);
	native_error = errno;
	if (status != 0) {
		free(bitmap);
		return load_failure(error, ST_FONT_LOAD_FILE,
		    native_error == 0 ? IO_ERROR_IO : io_error_from_errno(native_error), native_error);
	}
	font = (struct st_console_font){
		.bitmap = bitmap, .bitmap_size = size, .width = width, .height = height,
		.glyph_count = 256, .row_stride = row_stride, .glyph_stride = glyph_stride,
	};
	for (size_t i = 0; i < st_builtin_font_count(); ++i) {
		if (st_builtin_font_codepage(i) != codepage
		    || (!st_builtin_font_get(i, 8, 16, &template)
		    && !st_builtin_font_get(i, 8, 14, &template)
		    && !st_builtin_font_get(i, 8, 8, &template)
		    && !st_builtin_font_get(i, 12, 20, &template)))
			continue;
		font.mapping = template.mapping;
		font.mapping_count = template.mapping_count;
		break;
	}
	if (font.mapping == NULL) {
		free(bitmap);
		return load_failure(error, ST_FONT_LOAD_CODEPAGE, IO_ERROR_NOT_SUPPORTED, 0);
	}
	status = st_console_register_font_copy(console, &font, identifier);
	free(bitmap);
	return status == 0 ? 0 : load_failure(error, ST_FONT_LOAD_REGISTRATION, status, 0);
}

int
st_font_catalog_load_custom(st_font_catalog_t *catalog, uint8_t selector,
	const char *path)
{
	return st_font_catalog_load_custom_codepage(catalog, selector, path,
	    ST_CODEPAGE_CP437);
}

int
st_font_catalog_load_custom_codepage(st_font_catalog_t *catalog,
	uint8_t selector, const char *path, enum st_codepage codepage)
{
	uint16_t identifier;
	int status;

	if (catalog == NULL || selector < ST_FONT_CATALOG_FIRST_CUSTOM)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_font_catalog_register_custom_codepage(catalog->console,
	    path, codepage, &identifier, NULL);
	if (status != 0)
		return status;
	if (catalog->valid[selector])
		catalog->identifier_valid[catalog->identifiers[selector]] = false;
	catalog->identifiers[selector] = identifier;
	catalog->valid[selector] = true;
	catalog->codepages[selector] = codepage;
	catalog->selector_by_identifier[identifier] = selector;
	catalog->identifier_valid[identifier] = true;
	return 0;
}

bool
st_font_catalog_identifier(const st_font_catalog_t *catalog,
	uint8_t selector, uint16_t *identifier)
{
	if (catalog == NULL || identifier == NULL || !catalog->valid[selector])
		return false;
	*identifier = catalog->identifiers[selector];
	return true;
}

bool
st_font_catalog_selector(const st_font_catalog_t *catalog,
	uint16_t identifier, uint8_t *selector)
{
	if (catalog == NULL || selector == NULL
	    || !catalog->identifier_valid[identifier])
		return false;
	*selector = catalog->selector_by_identifier[identifier];
	return true;
}

enum st_codepage
st_font_catalog_codepage(const st_font_catalog_t *catalog, uint8_t selector)
{
	return catalog != NULL && catalog->valid[selector]
	    ? catalog->codepages[selector] : ST_CODEPAGE_COUNT;
}

st_console_t *
st_font_catalog_console(const st_font_catalog_t *catalog)
{
	return catalog == NULL ? NULL : catalog->console;
}
