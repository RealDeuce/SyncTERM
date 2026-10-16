/* Console-specific registration of built-in and custom terminal fonts. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_font_catalog.h"

#include "filesystem.h"
#include "io_error.h"
#include "st_builtin_fonts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct st_font_catalog {
	st_console_t *console;
	uint16_t identifiers[ST_FONT_CATALOG_SELECTOR_COUNT];
	bool valid[ST_FONT_CATALOG_SELECTOR_COUNT];
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
		status = st_console_register_static_font(console, &font, &identifier);
		if (status != 0) {
			free(catalog);
			return status;
		}
		catalog->identifiers[selector] = identifier;
		catalog->valid[selector] = true;
		catalog->selector_by_identifier[identifier] = (uint8_t)selector;
		catalog->identifier_valid[identifier] = true;
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
	    ST_CODEPAGE_CP437, identifier);
}

int
st_font_catalog_register_custom_codepage(st_console_t *console,
	const char *path, enum st_codepage codepage, uint16_t *identifier)
{
	struct st_console_font font;
	struct st_console_font template;
	uint8_t *bitmap;
	FILE *stream;
	uint32_t width;
	uint32_t height;
	size_t row_stride;
	size_t glyph_stride;
	size_t size;
	int status;

	if (console == NULL || path == NULL || identifier == NULL
	    || codepage < ST_CODEPAGE_CP437 || codepage >= ST_CODEPAGE_COUNT
	    || st_console_cell_pixel_size(console, &width, &height) != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	row_stride = ((size_t)width + 7) / 8;
	if (height != 0 && row_stride > SIZE_MAX / height)
		return IO_ERROR_OUT_OF_RANGE;
	glyph_stride = row_stride * height;
	if (glyph_stride > SIZE_MAX / 256)
		return IO_ERROR_OUT_OF_RANGE;
	size = glyph_stride * 256;
	if (fs_open(&stream, path, "rb") < 0)
		return IO_ERROR_NOT_FOUND;
	if (fs_stream_size(stream) != (int64_t)size) {
		(void)fclose(stream);
		return IO_ERROR_INVALID_ENCODING;
	}
	bitmap = malloc(size);
	if (bitmap == NULL) {
		(void)fclose(stream);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	if (fread(bitmap, 1, size, stream) != size) {
		free(bitmap);
		(void)fclose(stream);
		return IO_ERROR_IO;
	}
	(void)fclose(stream);
	font = (struct st_console_font){
		.bitmap = bitmap,
		.bitmap_size = size,
		.width = width,
		.height = height,
		.glyph_count = 256,
		.row_stride = row_stride,
		.glyph_stride = glyph_stride,
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
		return IO_ERROR_NOT_SUPPORTED;
	}
	status = st_console_register_font_copy(console, &font, identifier);
	free(bitmap);
	return status;
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
	    path, codepage, &identifier);
	if (status != 0)
		return status;
	if (catalog->valid[selector])
		catalog->identifier_valid[catalog->identifiers[selector]] = false;
	catalog->identifiers[selector] = identifier;
	catalog->valid[selector] = true;
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
	(void)catalog;
	if (selector >= ST_FONT_CATALOG_FIRST_CUSTOM)
		return ST_CODEPAGE_CP437;
	return st_builtin_font_codepage(selector);
}

st_console_t *
st_font_catalog_console(const st_font_catalog_t *catalog)
{
	return catalog == NULL ? NULL : catalog->console;
}
