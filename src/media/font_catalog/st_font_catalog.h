/* Console-specific registration of built-in and custom terminal fonts. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_FONT_CATALOG_H_
#define ST_FONT_CATALOG_H_

#include "st_codepage.h"
#include "st_console.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ST_FONT_CATALOG_SELECTOR_COUNT (UINT8_MAX + 1U)
#define ST_FONT_CATALOG_FIRST_CUSTOM 46U

typedef struct st_font_catalog st_font_catalog_t;

/* Failure origin is kept separate from the portable I/O status: an invalid
 * bitmap or unsupported mapping is not a failure to read the file. */
enum st_font_load_failure {
	ST_FONT_LOAD_NONE,
	ST_FONT_LOAD_FILE,
	ST_FONT_LOAD_BITMAP,
	ST_FONT_LOAD_CODEPAGE,
	ST_FONT_LOAD_REGISTRATION,
};
struct st_font_load_error {
	enum st_font_load_failure failure;
	int status;
	int native_error;
};

/*
 * A catalog borrows its console, registers every compatible built-in font,
 * and owns only its selector tables. Registered console fonts retain their
 * own copied/static bitmap lifetime independently of the catalog.
 */
int st_font_catalog_create(st_font_catalog_t **result, st_console_t *console);
void st_font_catalog_destroy(st_font_catalog_t *catalog);

/*
 * Registers exactly 256 glyphs from an owned temporary file image. Width and
 * height must match the console cell. The default loader uses CP437 semantic
 * mapping; the codepage variant maps each custom glyph for one session.
 * A failed load leaves the previous selector registration unchanged.
 */
int st_font_catalog_load_custom(st_font_catalog_t *catalog, uint8_t selector,
	const char *path);
int st_font_catalog_load_custom_codepage(st_font_catalog_t *catalog,
	uint8_t selector, const char *path, enum st_codepage codepage);
/* Shared loader used by hosts that retain their own selector tables. */
int st_font_catalog_register_custom(st_console_t *console, const char *path,
	uint16_t *identifier);
int st_font_catalog_register_custom_codepage(st_console_t *console,
	const char *path, enum st_codepage codepage, uint16_t *identifier,
	struct st_font_load_error *error);
bool st_font_catalog_identifier(const st_font_catalog_t *catalog,
	uint8_t selector, uint16_t *identifier);
bool st_font_catalog_selector(const st_font_catalog_t *catalog,
	uint16_t identifier, uint8_t *selector);
/* Returns ST_CODEPAGE_COUNT for an unavailable selector. */
enum st_codepage st_font_catalog_codepage(const st_font_catalog_t *catalog,
	uint8_t selector);
st_console_t *st_font_catalog_console(const st_font_catalog_t *catalog);

#endif
