/* Tests for console-specific terminal font catalogs. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_font_catalog.h"

#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, \
		    #expression); \
		return EXIT_FAILURE; \
	} \
} while (0)

int
main(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 8, .rows = 2, .cell_width = 8, .cell_height = 16,
	};
	static const uint8_t zeros[4096] = {0};
	const char path[] = "st_font_catalog_test.f16";
	st_console_t *console = NULL;
	st_font_catalog_t *catalog = NULL;
	uint16_t identifier;
	uint8_t selector;
	FILE *stream;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(st_font_catalog_create(&catalog, console) == 0);
	CHECK(st_font_catalog_console(catalog) == console);
	CHECK(st_font_catalog_identifier(catalog, 0, &identifier));
	CHECK(st_font_catalog_selector(catalog, identifier, &selector)
	    && selector == 0);
	CHECK(!st_font_catalog_identifier(catalog,
	    ST_FONT_CATALOG_FIRST_CUSTOM, &identifier));
	CHECK(st_font_catalog_load_custom(catalog,
	    ST_FONT_CATALOG_FIRST_CUSTOM, "missing-font-file")
	    == IO_ERROR_NOT_FOUND);
	CHECK(!st_font_catalog_identifier(catalog,
	    ST_FONT_CATALOG_FIRST_CUSTOM, &identifier));
	stream = fopen(path, "wb");
	CHECK(stream != NULL);
	CHECK(fwrite(zeros, 1, sizeof(zeros), stream) == sizeof(zeros));
	CHECK(fclose(stream) == 0);
	CHECK(st_font_catalog_load_custom(catalog,
	    ST_FONT_CATALOG_FIRST_CUSTOM, path) == 0);
	CHECK(st_font_catalog_identifier(catalog,
	    ST_FONT_CATALOG_FIRST_CUSTOM, &identifier));
	CHECK(st_font_catalog_selector(catalog, identifier, &selector)
	    && selector == ST_FONT_CATALOG_FIRST_CUSTOM);
	{
		uint16_t retained = identifier;

		stream = fopen(path, "wb");
		CHECK(stream != NULL);
		CHECK(fputc(0, stream) == 0 && fclose(stream) == 0);
		CHECK(st_font_catalog_load_custom(catalog,
		    ST_FONT_CATALOG_FIRST_CUSTOM, path)
		    == IO_ERROR_INVALID_ENCODING);
		CHECK(st_font_catalog_identifier(catalog,
		    ST_FONT_CATALOG_FIRST_CUSTOM, &identifier)
		    && identifier == retained);
	}
	CHECK(remove(path) == 0);
	CHECK(st_font_catalog_codepage(catalog,
	    ST_FONT_CATALOG_FIRST_CUSTOM) == ST_CODEPAGE_CP437);
	st_font_catalog_destroy(catalog);
	st_console_destroy(console);
	return EXIT_SUCCESS;
}
