/* Persistent Wren host and embedded-module loader. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"

#include "filesystem.h"
#include "io_error.h"
#include "path.h"
#include "st_app_paths.h"
#include "st_bbs_list.h"
#include "st_builtin_fonts.h"
#include "st_console.h"
#include "st_font_catalog.h"
#include "st_emoji_locale.h"
#include "st_screen_mode.h"
#include "st_session_history.h"
#include "st_settings.h"
#include "st_theme.h"
#include "st_unicode_text.h"
#include "st_utf8.h"
#include "string_compat.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ST_WREN_MAX_MODULE_SIZE (4U * 1024U * 1024U)
#define ST_WREN_FIRST_CUSTOM_FONT ST_BUILTIN_FONT_COUNT

_Static_assert(ST_WREN_CUSTOM_FONT_MAX == ST_SETTINGS_CUSTOM_FONT_MAX,
	"custom font limits must agree");
_Static_assert(ST_BUILTIN_FONT_COUNT == 46U,
	"Wren font selectors reserve 0 through 45 for built-ins");

static int reload_custom_fonts_current(struct st_wren_context *context);

static int
mark_console_hyperlinks(st_console_t *console, st_hyperlink_mark_fn mark,
	void *mark_context)
{
	struct st_console_cell *cells;
	struct st_console_rect rect;
	uint32_t columns;
	uint32_t rows;
	size_t count;
	int status;

	status = st_console_size(console, &columns, &rows);
	if (status != 0)
		return status;
	if (columns == 0 || rows == 0)
		return 0;
	if ((size_t)columns > SIZE_MAX / rows
	    || (size_t)columns * rows > SIZE_MAX / sizeof(*cells))
		return IO_ERROR_OUT_OF_RANGE;
	count = (size_t)columns * rows;
	cells = malloc(count * sizeof(*cells));
	if (cells == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	rect = (struct st_console_rect){0, 0, columns, rows};
	status = st_console_read_cells(console, &rect, cells, columns);
	for (size_t i = 0; status == 0 && i < count; ++i)
		status = mark(mark_context, cells[i].hyperlink);
	free(cells);
	return status;
}

static int
mark_host_hyperlinks(void *context, st_hyperlink_mark_fn mark,
	void *mark_context)
{
	struct st_wren_host *host = context;
	struct st_console_cell *history = NULL;
	uint32_t width;
	size_t lines;
	enum st_screen_mode mode;
	bool has_status;
	int status;

	status = mark_console_hyperlinks(host->menu.console, mark, mark_context);
	if (status == 0 && host->picker.console != host->menu.console)
		status = mark_console_hyperlinks(host->picker.console, mark,
		    mark_context);
	if (status == 0)
		status = st_wren_mark_surface_hyperlinks(host, mark, mark_context);
	if (status != 0 || host->config.session_history == NULL)
		return status;
	status = st_session_history_copy(host->config.session_history, &history,
	    &width, &lines, &mode, &has_status);
	if (status == IO_ERROR_NOT_FOUND)
		return 0;
	if (status != 0)
		return status;
	for (size_t i = 0; status == 0 && i < (size_t)width * lines; ++i)
		status = mark(mark_context, history[i].hyperlink);
	free(history);
	return status;
}

bool
st_wren_text_slot(WrenVM *vm, int slot, const char **text, int *length)
{
	char *normalized = NULL;
	size_t normalized_size = 0;
	const char *source;
	bool changed;
	int source_size;
	int status;

	if (wrenGetSlotType(vm, slot) != WREN_TYPE_STRING) {
		st_wren_abort(vm, "expected a String");
		return false;
	}
	source = wrenGetSlotBytes(vm, slot, &source_size);
	if (source_size < 0) {
		st_wren_abort(vm, "invalid String size");
		return false;
	}
	status = st_unicode_text_normalize(&normalized, &normalized_size, source,
	    (size_t)source_size, &changed);
	if (status != 0 || normalized_size > INT_MAX) {
		free(normalized);
		st_wren_abort(vm, status == IO_ERROR_INVALID_ENCODING
		    ? "String is not valid UTF-8" : "out of memory");
		return false;
	}
	if (changed)
		wrenSetSlotBytes(vm, slot, normalized, normalized_size);
	free(normalized);
	*text = wrenGetSlotBytes(vm, slot, &source_size);
	if (length != NULL)
		*length = source_size;
	return true;
}

static int
load_builtin_fonts(struct st_wren_context *context)
{
	st_font_catalog_t *catalog = NULL;
	struct st_console_style style;
	uint16_t identifier;
	int status;

	status = st_font_catalog_create(&catalog, context->console);
	if (status != 0)
		return status;
	for (size_t selector = 0; selector < st_builtin_font_count(); ++selector) {
		context->fonts.catalog_valid[selector] = false;
		if (!st_font_catalog_identifier(catalog, (uint8_t)selector,
		    &identifier))
			continue;
		context->fonts.catalog[selector] = identifier;
		context->fonts.catalog_valid[selector] = true;
		context->fonts.codepages[selector] = st_font_catalog_codepage(catalog, (uint8_t)selector);
		context->fonts.selector_by_identifier[identifier]
		    = (uint16_t)selector;
	}
	st_font_catalog_destroy(catalog);
	if (!context->fonts.catalog_valid[0]) {
		context->fonts.catalog[0] = 0;
		context->fonts.catalog_valid[0] = true;
		context->fonts.selector_by_identifier[0] = 0;
		return 0;
	}
	status = st_console_get_style(context->console, &style);
	if (status != 0)
		return status;
	style.font = context->fonts.catalog[0];
	return st_console_set_style(context->console, &style);
}

static uint32_t
legacy_colour(uint32_t colour)
{
	static const uint8_t map[16] = {
		0, 4, 2, 6, 1, 5, 3, 7, 8, 12, 10, 14, 9, 13, 11, 15,
	};

	return map[colour & 0x0f];
}

static void
configure_cursor(struct st_console_config *config, int cursor)
{
	uint32_t height = config->cell_height == 0 ? 1 : config->cell_height;

	switch (cursor) {
	case ST_CURSOR_BLINK_UNDERLINE:
	case ST_CURSOR_SOLID_UNDERLINE:
		config->default_cursor.start = height - (height > 8 ? 2U : 1U);
		config->default_cursor.end = height - 1;
		break;
	case ST_CURSOR_BLINK_BLOCK:
	case ST_CURSOR_SOLID_BLOCK:
		config->default_cursor.start = 0;
		config->default_cursor.end = height - 1;
		break;
	case ST_CURSOR_DEFAULT:
	default:
		break;
	}
	config->default_cursor.range = height;
	config->default_cursor.visible = true;
	config->default_cursor.blink = cursor != ST_CURSOR_SOLID_UNDERLINE
	    && cursor != ST_CURSOR_SOLID_BLOCK;
}

static int
install_mode_state(struct st_wren_context *context,
	const struct st_screen_mode_descriptor *mode, bool reconfigure)
{
	struct st_console_config config = mode->console;
	struct st_console_style style;
	struct st_console_cell blank;
	struct st_console_rect screen;
	uint32_t palette[16];
	uint16_t identifier;
	uint8_t attribute = mode->default_attribute;
	int status;

	config.scaling_mode = st_settings.external_scaling
	    ? ST_CONSOLE_SCALING_EXTERNAL : ST_CONSOLE_SCALING_INTERNAL;
	config.color_emoji = st_settings.color_emoji;
	config.scaling_policy = st_settings.blocky_scaling
	    ? ST_PIXEL_SCALING_BLOCKY : ST_PIXEL_SCALING_POINTY;
	configure_cursor(&config, st_settings.default_cursor);
	if (reconfigure) {
		status = st_console_reconfigure(context->console, &config);
		if (status != 0)
			return status;
		memset(&context->fonts, 0, sizeof(context->fonts));
		status = load_builtin_fonts(context);
		if (status == 0)
			status = reload_custom_fonts_current(context);
		if (status != 0)
			return status;
	}
	st_screen_mode_console_palette(mode, palette);
	status = st_console_set_palette(context->console, 0, palette, 16);
	if (status != 0)
		return status;
	context->fonts.slots[0] = mode->font_selector;
	for (size_t i = 1; i < sizeof(context->fonts.slots)
	    / sizeof(context->fonts.slots[0]); ++i)
		context->fonts.slots[i] = 0;
	if (!st_wren_font_identifier(context, mode->font_selector, &identifier))
		return IO_ERROR_NOT_SUPPORTED;
	style = (struct st_console_style){
		.foreground = legacy_colour(attribute & UINT8_C(0x07)),
		.background = legacy_colour((attribute >> 4) & UINT8_C(0x07)),
		.flags = ST_CONSOLE_CELL_LEGACY_FOREGROUND
		    | ST_CONSOLE_CELL_LEGACY_BACKGROUND,
		.font = identifier,
	};
	if ((attribute & UINT8_C(0x08)) != 0)
		style.flags |= ST_CONSOLE_CELL_BRIGHT;
	if ((attribute & UINT8_C(0x80)) != 0)
		style.flags |= ST_CONSOLE_CELL_BLINK;
	status = st_console_set_style(context->console, &style);
	if (status != 0 || !reconfigure)
		return status;
	blank = (struct st_console_cell){
		.foreground = style.foreground,
		.background = style.background,
		.flags = style.flags,
		.font = style.font,
		.glyph_font = style.font,
	};
	{
		uint8_t columns;

		(void)st_console_font_lookup(context->console, blank.font, ' ',
		    &blank.glyph_font, &blank.glyph, &columns);
	}
	screen = (struct st_console_rect){0, 0, config.columns, config.rows};
	return st_console_fill(context->console, &screen, &blank);
}

int
st_wren_host_set_screen_mode(struct st_wren_host *host,
	enum st_screen_mode selected)
{
	struct st_screen_mode_descriptor custom;
	const struct st_screen_mode_descriptor *mode;
	int status;

	if (host == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (selected == ST_SCREEN_MODE_CUSTOM) {
		status = st_screen_mode_custom(&custom,
		    (uint32_t)st_settings.custom_columns,
		    (uint32_t)st_settings.custom_rows,
		    (uint32_t)st_settings.custom_font_height,
		    (uint32_t)st_settings.custom_aspect_width,
		    (uint32_t)st_settings.custom_aspect_height);
		if (status != 0)
			return status;
		mode = &custom;
	}
	else {
		mode = st_screen_mode_get(selected);
		if (mode == NULL || selected == ST_SCREEN_MODE_CURRENT)
			return IO_ERROR_INVALID_ARGUMENT;
	}
	status = install_mode_state(&host->menu, mode, true);
	if (status == 0 && host->picker.console != host->menu.console)
		status = install_mode_state(&host->picker, mode, true);
	else if (status == 0)
		host->picker.fonts = host->menu.fonts;
	if (status == 0) {
		host->current_screen_mode = selected;
		host->current_mode = *mode;
	}
	return status;
}

int
st_wren_host_begin_session_mode(st_wren_host_t *host,
	enum st_screen_mode selected)
{
	struct st_console_config *config;
	int status;

	if (host == NULL || host->session_mode_active)
		return IO_ERROR_INVALID_ARGUMENT;
	host->menu_mode = host->current_mode;
	host->menu_mode_identity = host->current_screen_mode;
	/* CURRENT can represent a grid resized by the window manager rather
	 * than any named mode. Keep its actual geometry and inherited palette. */
	config = &host->menu_mode.console;
	status = st_console_size(host->menu.console, &config->columns,
	    &config->rows);
	if (status == 0)
		status = st_console_cell_pixel_size(host->menu.console,
		    &config->cell_width, &config->cell_height);
	config->pixel_width = 0;
	config->pixel_height = 0;
	if (status == 0 && config->cell_width != 0)
		status = st_console_pixel_size(host->menu.console,
		    &config->pixel_width, &config->pixel_height);
	if (status == 0)
		status = st_console_get_aspect_ratio(host->menu.console,
		    &config->aspect_width, &config->aspect_height);
	if (status == 0)
		status = st_console_get_default_cursor(host->menu.console,
		    &config->default_cursor);
	if (status != 0)
		return status;
	host->session_mode_active = true;
	if (selected == ST_SCREEN_MODE_CURRENT)
		return 0;
	status = st_wren_host_set_screen_mode(host, selected);
	if (status != 0)
		(void)st_wren_host_end_session_mode(host);
	return status;
}

static uint64_t aspect_gcd(uint64_t a, uint64_t b)
{
	while (b != 0) {
		uint64_t next = a % b;
		a = b;
		b = next;
	}
	return a;
}
int st_wren_host_begin_skypix_session(st_wren_host_t *host,
									  const struct st_screen_mode_descriptor *canvas,
									  bool hide_status, const st_amiga_font_t *font)
{
	if (canvas == NULL || font == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	uint32_t width = canvas->console.pixel_width, height = canvas->console.pixel_height;
	uint32_t cw = canvas->console.cell_width, ch = canvas->console.cell_height;
	if (width == 0 || height == 0 || width > INT16_MAX || height > INT16_MAX || cw == 0 ||
		ch == 0 || cw > width || ch > height || (uint64_t)width * height > 4 * 1024 * 1024)
		return IO_ERROR_OUT_OF_RANGE;
	int status = st_wren_host_begin_session_mode(host, ST_SCREEN_MODE_CURRENT);
	if (status != 0)
		return status;
	struct st_screen_mode_descriptor mode = *canvas;
	mode.console.cell_width = cw;
	mode.console.cell_height = ch;
	mode.console.default_cursor = (struct st_console_custom_cursor){
		.start = ch - 1, .end = ch - 1, .range = ch, .blink = true};
	mode.console.columns = (width + cw - 1) / cw;
	mode.console.rows = (height + ch - 1) / ch + (hide_status ? 0 : 1);
	mode.console.pixel_width = mode.console.columns * cw;
	mode.console.pixel_height = mode.console.rows * ch;
	/* Preserve the selected canvas aspect while giving padding and the
	 * separate status row the same pixel scale. */
	if (mode.console.aspect_width != 0 && mode.console.aspect_height != 0) {
		uint64_t a = (uint64_t)mode.console.pixel_width * height * mode.console.aspect_width;
		uint64_t b = (uint64_t)mode.console.pixel_height * width * mode.console.aspect_height;
		uint64_t divisor = aspect_gcd(a, b);
		a /= divisor;
		b /= divisor;
		if (a > UINT32_MAX || b > UINT32_MAX)
			status = IO_ERROR_OUT_OF_RANGE;
		else {
			mode.console.aspect_width = (uint32_t)a;
			mode.console.aspect_height = (uint32_t)b;
		}
	}
	mode.font_selector = 0;
	if (status == 0)
		status = install_mode_state(&host->menu, &mode, true);
	if (status == 0 && host->picker.console != host->menu.console)
		status = install_mode_state(&host->picker, &mode, true);
	else if (status == 0)
		host->picker.fonts = host->menu.fonts;
	if (status == 0) {
		host->current_mode = mode;
		host->current_screen_mode = mode.mode;
	} else
		(void)st_wren_host_end_session_mode(host);
	return status;
}

int
st_wren_host_capture_history_mode(st_wren_host_t *host)
{
	if (host == NULL || !host->session_mode_active) return IO_ERROR_INVALID_ARGUMENT;
	struct st_screen_mode_descriptor mode = host->current_mode;
	struct st_console_config *config = &mode.console;
	int status = st_console_size(host->menu.console, &config->columns, &config->rows);
	if (status == 0) status = st_console_cell_pixel_size(host->menu.console, &config->cell_width, &config->cell_height);
	if (status == 0) status = st_console_pixel_size(host->menu.console, &config->pixel_width, &config->pixel_height);
	if (status == 0) status = st_console_get_aspect_ratio(host->menu.console, &config->aspect_width, &config->aspect_height);
	if (status == 0) { host->history_mode = mode; host->history_mode_valid = true; }
	return status;
}

int
st_wren_host_prepare_history_mode(struct st_wren_host *host)
{
	if (host == NULL || !host->history_mode_valid) return IO_ERROR_NOT_FOUND;
	int status = install_mode_state(&host->menu, &host->history_mode, true);
	if (status == 0 && host->picker.console != host->menu.console)
		status = install_mode_state(&host->picker, &host->history_mode, true);
	else if (status == 0) host->picker.fonts = host->menu.fonts;
	return status;
}

int
st_wren_host_end_session_mode(st_wren_host_t *host)
{
	int status;

	if (host == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!host->session_mode_active)
		return 0;
	status = install_mode_state(&host->menu, &host->menu_mode, true);
	if (status == 0 && host->picker.console != host->menu.console)
		status = install_mode_state(&host->picker, &host->menu_mode, true);
	else if (status == 0)
		host->picker.fonts = host->menu.fonts;
	if (status == 0) {
		host->current_mode = host->menu_mode;
		host->current_screen_mode = host->menu_mode_identity;
		host->session_mode_active = false;
	}
	return status;
}

void
st_wren_host_external_screen_resize(struct st_wren_host *host,
	st_console_t *console, uint32_t columns, uint32_t rows)
{
	st_console_t *other;

	if (host == NULL || console == NULL || columns == 0 || rows == 0)
		return;
	other = console == host->menu.console ? host->picker.console
	    : host->menu.console;
	if (st_console_resize(console, columns, rows) == 0
	    && (other == console || st_console_resize(other, columns, rows) == 0))
		host->current_screen_mode = ST_SCREEN_MODE_CURRENT;
}

static bool
custom_font_slot(uint32_t width, uint32_t height,
	enum st_settings_font_path *slot)
{
	if (width == 8 && height == 8)
		*slot = ST_SETTINGS_FONT_8X8;
	else if (width == 8 && height == 14)
		*slot = ST_SETTINGS_FONT_8X14;
	else if (width == 8 && height == 16)
		*slot = ST_SETTINGS_FONT_8X16;
	else if (width == 12 && height == 20)
		*slot = ST_SETTINGS_FONT_12X20;
	else
		return false;
	return true;
}

bool
st_wren_font_metadata(struct st_wren_context *context, uint16_t selector,
	uint16_t *identifier, enum st_codepage *codepage)
{
	if (selector > UINT8_MAX)
		return false;
	if (context->cterm != NULL && selector != 0)
		return st_cterm_font_info(context->cterm, (uint8_t)selector,
		    identifier, codepage);
	if (!context->fonts.catalog_valid[selector])
		return false;
	*identifier = context->fonts.catalog[selector];
	*codepage = context->fonts.codepages[selector];
	return true;
}

bool
st_wren_font_identifier(struct st_wren_context *context, uint16_t selector,
	uint16_t *identifier)
{
	enum st_codepage codepage;

	return st_wren_font_metadata(context, selector, identifier, &codepage);
}

uint16_t
st_wren_font_selector(struct st_wren_context *context, uint16_t identifier)
{
	if (context->fonts.catalog_valid[0] && context->fonts.catalog[0] == identifier)
		return 0;
	if (context->cterm != NULL) {
		for (size_t i = 0; i <= UINT8_MAX; ++i) {
			uint16_t actual;
			enum st_codepage codepage;
			if (st_cterm_font_info(context->cterm, (uint8_t)i, &actual, &codepage)
			    && actual == identifier)
				return (uint16_t)i;
		}
	}
	return context->fonts.selector_by_identifier[identifier];
}

static int
prepare_custom_fonts(struct st_wren_context *context,
    struct st_wren_font_state *prepared)
{
	uint16_t identifiers[ST_WREN_CUSTOM_FONT_MAX] = {0};
	bool available[ST_WREN_CUSTOM_FONT_MAX] = {0};
	uint32_t width;
	uint32_t height;
	enum st_settings_font_path slot;
	size_t count;
	int status;

	if (context == NULL || prepared == NULL
	    || st_console_cell_pixel_size(context->console,
	    &width, &height) < 0)
		return IO_ERROR_INVALID_ARGUMENT;
	*prepared = context->fonts;
	if (!custom_font_slot(width, height, &slot))
		return 0;
	count = st_settings_font_count();
	for (size_t i = 0; i < count; ++i) {
		const struct st_settings_font *configured = st_settings_font_at(i);
		const char *path = configured->path[slot];

		if (path == NULL)
			continue;
		status = st_font_catalog_register_custom(context->console, path,
		    &identifiers[i]);
		if (status == 0)
			available[i] = true;
		else if (status != IO_ERROR_NOT_FOUND
		    && status != IO_ERROR_INVALID_ENCODING)
			return status;
	}
	for (size_t i = 0; i < ST_WREN_CUSTOM_FONT_MAX; ++i) {
		uint16_t selector = (uint16_t)(ST_WREN_FIRST_CUSTOM_FONT + i);
		uint16_t old_identifier;

		if (prepared->catalog_valid[selector]) {
			old_identifier = prepared->catalog[selector];
			prepared->selector_by_identifier[old_identifier] = 0;
		}
		prepared->catalog_valid[selector] = i < count && available[i];
		if (i < count && available[i]) {
			prepared->catalog[selector] = identifiers[i];
			prepared->codepages[selector] = ST_CODEPAGE_CP437;
			prepared->selector_by_identifier[identifiers[i]] = selector;
		}
	}
	prepared->loaded_count = count;
	return 0;
}

static int
reload_custom_fonts_current(struct st_wren_context *context)
{
	struct st_wren_font_state *prepared = malloc(sizeof(*prepared));
	int status;

	if (prepared == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	status = prepare_custom_fonts(context, prepared);
	if (status == 0)
		context->fonts = *prepared;
	free(prepared);
	return status;
}

int
st_wren_context_load_fonts(struct st_wren_context *context)
{
	int status;

	if (context == NULL || context->console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (context->cterm != NULL) {
		/* Slot zero is the system CP437 font for UI, independently of the
		 * connection font (including remote replacement of terminal slot zero). */
		if (context->console != context->host->menu.console) {
			status = load_builtin_fonts(context);
			if (status != 0)
				return status;
		}
		uint16_t system_font = context->fonts.catalog[0];
		/* Keep script-output assignments independent; import the terminal's
		 * actual registrations rather than registering another CP437 copy. */
		memset(context->fonts.catalog_valid, 0, sizeof(context->fonts.catalog_valid));
		context->fonts.loaded_count = st_settings_font_count();
		for (size_t i = 0; i <= UINT8_MAX; ++i) {
			uint16_t identifier;
			enum st_codepage codepage;
			if (!st_cterm_font_info(context->cterm, (uint8_t)i, &identifier, &codepage))
				continue;
			context->fonts.catalog[i] = identifier;
			context->fonts.catalog_valid[i] = true;
			context->fonts.codepages[i] = codepage;
			context->fonts.selector_by_identifier[identifier] = (uint16_t)i;
		}
		context->fonts.catalog[0] = system_font;
		context->fonts.catalog_valid[0] = true;
		context->fonts.codepages[0] = ST_CODEPAGE_CP437;
		context->fonts.selector_by_identifier[system_font] = 0;
		return 0;
	}

	memset(&context->fonts, 0, sizeof(context->fonts));
	status = load_builtin_fonts(context);
	if (status == 0)
		status = reload_custom_fonts_current(context);
	return status;
}

int
st_wren_reload_custom_fonts(struct st_wren_host *host)
{
	struct st_wren_font_state *menu;
	struct st_wren_font_state *picker;
	int status;

	if (host == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	menu = malloc(sizeof(*menu));
	picker = malloc(sizeof(*picker));
	if (menu == NULL || picker == NULL) {
		free(menu);
		free(picker);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	status = prepare_custom_fonts(&host->menu, menu);
	if (status == 0 && host->picker.console != host->menu.console)
		status = prepare_custom_fonts(&host->picker, picker);
	else if (status == 0) {
		uint16_t slots[5];

		/* The one-console host shares registrations, not VM font selectors. */
		memcpy(slots, host->picker.fonts.slots, sizeof(slots));
		*picker = *menu;
		memcpy(picker->slots, slots, sizeof(slots));
	}
	if (status == 0) {
		host->menu.fonts = *menu;
		host->picker.fonts = *picker;
	}
	free(menu);
	free(picker);
	return status;
}

static bool
module_name_valid(const char *name)
{
	if (name == NULL || *name == '\0')
		return false;
	for (const unsigned char *p = (const unsigned char *)name;
	    *p != '\0'; ++p) {
		if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')
		    || (*p >= '0' && *p <= '9') || *p == '_'))
			return false;
	}
	return true;
}

static char *
read_source(const char *path)
{
	FILE *stream;
	char *source;
	int64_t length;
	size_t read_length;
	bool failed;

	if (fs_open(&stream, path, "rb") != 0)
		return NULL;
	length = fs_stream_size(stream);
	if (length < 0 || (uint64_t)length > ST_WREN_MAX_MODULE_SIZE) {
		fclose(stream);
		return NULL;
	}
	source = malloc((size_t)length + 1);
	if (source == NULL) {
		fclose(stream);
		return NULL;
	}
	read_length = fread(source, 1, (size_t)length, stream);
	failed = read_length != (size_t)length || ferror(stream);
	(void)fclose(stream);
	if (failed) {
		free(source);
		return NULL;
	}
	source[read_length] = '\0';
	return source;
}

static char *
read_named_source(const char *directory, const char *event, const char *name)
{
	char *filename = NULL;
	char *event_directory = NULL;
	char *path = NULL;
	char *source = NULL;
	size_t name_length = strlen(name);

	filename = malloc(name_length + sizeof(".wren"));
	if (filename == NULL)
		return NULL;
	memcpy(filename, name, name_length);
	memcpy(filename + name_length, ".wren", sizeof(".wren"));
	if (event == NULL) {
		if (path_join_alloc(&path, directory, filename) == 0)
			source = read_source(path);
	}
	else if (path_join_alloc(&event_directory, directory, "auto") == 0) {
		char *next = NULL;

		if (path_join_alloc(&next, event_directory, event) == 0
		    && path_join_alloc(&path, next, filename) == 0)
			source = read_source(path);
		free(next);
	}
	free(path);
	free(event_directory);
	free(filename);
	return source;
}

static void
source_complete(WrenVM *vm, const char *name, WrenLoadModuleResult result)
{
	(void)vm;
	(void)name;
	free((void *)result.source);
}

WrenLoadModuleResult
st_wren_load_module(WrenVM *vm, const char *name)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	WrenLoadModuleResult result = {NULL, NULL, NULL};
	const char *event = st_wren_context_from_vm(vm)->event;
	bool picker = event != NULL && strcmp(event, "picker") == 0;
	char *source;

	if (!module_name_valid(name)) {
		result.source = "Fiber.abort(\"invalid module name\")";
		return result;
	}
	if (!picker && (strcmp(name, "syncterm_picker") == 0
	    || strcmp(name, "picker_bootstrap") == 0)) {
		result.source =
		    "Fiber.abort(\"picker modules are isolated from this VM\")";
		return result;
	}
	if (event != NULL && strcmp(event, "menu") != 0
	    && strcmp(name, "syncterm_menu") == 0) {
		result.source =
		    "Fiber.abort(\"menu facilities are unavailable in this VM\")";
		return result;
	}
	if (picker && strcmp(name, "picker_bootstrap") == 0) {
		for (const struct st_embedded_script *script =
		    st_wren_embedded_scripts; script->name != NULL; ++script) {
			if (strcmp(script->name, name) == 0
			    && script->event == NULL) {
				result.source = script->source;
				return result;
			}
		}
		return result;
	}
	source = read_named_source(host->config.paths->scripts_directory, NULL,
	    name);
	if (source == NULL)
		source = read_named_source(host->config.paths->scripts_directory,
		    event == NULL ? "menu" : event, name);
	if (source != NULL) {
		result.source = source;
		result.onComplete = source_complete;
		return result;
	}
	for (const struct st_embedded_script *script = st_wren_embedded_scripts;
	    script->name != NULL; ++script) {
		if (strcmp(script->name, name) == 0
		    && (script->event == NULL
		    || strcmp(script->event,
		    event == NULL ? "menu" : event) == 0)) {
			result.source = script->source;
			return result;
		}
	}
	return result;
}

WrenForeignMethodFn
st_wren_bind_method(WrenVM *vm, const char *module, const char *class_name,
	bool is_static, const char *signature)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	WrenForeignMethodFn method = NULL;

	if (strcmp(module, "syncterm") == 0)
		method = st_wren_session_object_bind_method(class_name, is_static, signature);
	if (method == NULL && strcmp(module, "syncterm") == 0)
		method = st_wren_profile_bind_method(class_name, is_static, signature);
	if (method == NULL && strcmp(module, "syncterm") == 0)
		method = st_wren_async_bind_method(class_name, is_static, signature);
	if (method == NULL && strcmp(module, "syncterm") == 0)
		method = st_wren_cleanup_bind_method(class_name, is_static, signature);
	if (method == NULL && strcmp(module, "syncterm") == 0)
		method = st_wren_fs_bind_method(class_name, is_static, signature);
	if (method == NULL && strcmp(module, "syncterm") == 0)
		method = st_wren_digest_bind_method(class_name, is_static, signature);
	if (method == NULL && strcmp(module, "syncterm") == 0
	    && (st_wren_context_from_vm(vm)->event == NULL
	    || strcmp(st_wren_context_from_vm(vm)->event, "picker") != 0))
		method = st_wren_hyperlink_bind_method(class_name, is_static,
		    signature);
	if (method == NULL && strcmp(module, "syncterm") == 0)
		method = st_wren_sftp_bind_method(class_name, is_static, signature);
	if (method == NULL && strcmp(module, "syncterm") == 0)
		method = st_wren_transfer_bind_method(class_name, is_static, signature);
	if (method == NULL && strcmp(module, "syncterm") == 0
	    && st_wren_context_from_vm(vm)->connected != NULL)
		method = st_wren_status_bind_method(class_name, is_static,
		    signature);
	if (method == NULL && strcmp(module, "syncterm") == 0
	    && st_wren_context_from_vm(vm)->connected != NULL)
		method = st_wren_session_bind_method(class_name, is_static,
		    signature);
	if (method == NULL)
		method = st_wren_picker_bind_method(module, class_name, is_static,
		    signature);
	if (method == NULL && strcmp(module, "syncterm") == 0)
		method = st_wren_pixel_bind_method(class_name, is_static, signature);
	if (method == NULL && strcmp(module, "syncterm") == 0)
		method = st_wren_repl_bind_method(class_name, is_static, signature);
	if (method == NULL && strcmp(module, "syncterm") == 0)
		method = st_wren_core_bind_method(class_name, is_static,
		    signature);
	else if (method == NULL && vm == host->vm
	    && strcmp(module, "syncterm_menu") == 0)
		method = st_wren_menu_bind_method(class_name, is_static, signature);
	/* Shared declarations may name a known capability owned by another VM.
	 * An unknown declaration must remain NULL so Wren rejects the module. */
	if (method == NULL && strcmp(module, "syncterm") == 0
	    && (st_wren_session_bind_method(class_name, is_static, signature) != NULL
	    || st_wren_status_bind_method(class_name, is_static, signature) != NULL
	    || st_wren_hyperlink_bind_method(class_name, is_static, signature) != NULL))
		return st_wren_unsupported;
	return method;
}

static void
unknown_foreign_class(WrenVM *vm)
{
	st_wren_abort(vm, "foreign class has no registered allocator in this VM");
}

WrenForeignClassMethods
st_wren_bind_class(WrenVM *vm, const char *module, const char *class_name)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	WrenForeignClassMethods methods = {NULL, NULL};

	if (strcmp(module, "syncterm") == 0)
		methods = st_wren_session_object_bind_class(class_name);
	if (methods.allocate == NULL && strcmp(module, "syncterm") == 0)
		methods = st_wren_profile_bind_class(class_name);
	if (methods.allocate == NULL && strcmp(module, "syncterm") == 0)
		methods = st_wren_async_bind_class(class_name);
	if (methods.allocate == NULL && strcmp(module, "syncterm") == 0)
		methods = st_wren_cleanup_bind_class(class_name);
	if (methods.allocate == NULL && strcmp(module, "syncterm") == 0)
		methods = st_wren_fs_bind_class(class_name);
	if (methods.allocate == NULL && strcmp(module, "syncterm") == 0)
		methods = st_wren_digest_bind_class(class_name);
	if (methods.allocate == NULL && strcmp(module, "syncterm") == 0)
		methods = st_wren_sftp_bind_class(class_name);
	if (methods.allocate == NULL && strcmp(module, "syncterm") == 0)
		methods = st_wren_transfer_bind_class(class_name);
	if (methods.allocate == NULL && strcmp(module, "syncterm") == 0
	    && st_wren_context_from_vm(vm)->connected != NULL)
		methods = st_wren_session_bind_class(class_name);
	if (methods.allocate == NULL)
		methods = st_wren_picker_bind_class(module, class_name);
	if (methods.allocate != NULL)
		return methods;
	if (strcmp(module, "syncterm") == 0) {
		methods = st_wren_pixel_bind_class(class_name);
		if (methods.allocate != NULL)
			return methods;
		methods = st_wren_core_bind_class(class_name);
	}
	else if (vm == host->vm && strcmp(module, "syncterm_menu") == 0)
		methods = st_wren_menu_bind_class(class_name);
	/* Wren requires an allocator for a foreign constructor and otherwise
	 * asserts or dereferences an unbound method. Reject construction rather
	 * than assigning an unrelated native payload to an unknown class. */
	if (methods.allocate == NULL)
		methods.allocate = unknown_foreign_class;
	return methods;
}

void
st_wren_write_output(WrenVM *vm, const char *text)
{
	st_wren_log_append(vm, 0, text);
}

void
st_wren_report_error(WrenVM *vm, WrenErrorType type, const char *module, int line,
	const char *message)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	const char *kind = type == WREN_ERROR_COMPILE ? "compile" :
	    type == WREN_ERROR_RUNTIME ? "runtime" : "stack";

	st_wren_log_error(vm, type, module, line, message);
	if (st_wren_context_from_vm(vm)->capturing)
		return;

	if (type != WREN_ERROR_STACK_TRACE)
		(void)snprintf(host->last_error, sizeof(host->last_error),
		    "%s %s:%d: %s", kind, module == NULL ? "?" : module, line,
		    message == NULL ? "" : message);
	fprintf(stderr, "[%s] %s %s:%d: %s\n",
	    vm == host->picker_vm ? "wren-picker" : "wren-menu", kind,
	    module == NULL ? "?" : module, line, message == NULL ? "" : message);
}

int
st_wren_interpret_event_scripts(WrenVM *vm, const char *event)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct st_wren_directory_entries user = {0};
	bool success = true;

	/* v1.10a treated an unavailable event directory as an empty one. */
	(void)st_wren_script_inventory(host->config.paths->scripts_directory,
	    event, &user);
	for (const struct st_embedded_script *script = st_wren_embedded_scripts;
	    script->name != NULL; ++script) {
		bool overridden = false;

		if (script->event == NULL || strcmp(script->event, event) != 0)
			continue;
		for (size_t i = 0; i < user.count; ++i) {
			if (strcmp(user.items[i].name, script->name) == 0) {
				overridden = true;
				break;
			}
		}
		if (!overridden && !wrenHasModule(vm, script->name)
		    && wrenInterpret(vm, script->name, script->source)
		    != WREN_RESULT_SUCCESS)
			success = false;
	}
	for (size_t i = 0; i < user.count; ++i) {
		char *source;

		if (wrenHasModule(vm, user.items[i].name))
			continue;
		source = read_named_source(host->config.paths->scripts_directory,
		    event, user.items[i].name);
		if (source == NULL) {
			fprintf(stderr, "[%s] cannot read auto/%s/%s.wren\n",
			    vm == host->picker_vm ? "wren-picker" : "wren-menu",
			    event, user.items[i].name);
			success = false;
			continue;
		}
		if (wrenInterpret(vm, user.items[i].name, source)
		    != WREN_RESULT_SUCCESS)
			success = false;
		free(source);
	}
	st_wren_directory_entries_dispose(&user);
	return success ? 0 : IO_ERROR_INVALID_FORMAT;
}

static const struct st_embedded_script *
embedded_script(const char *name)
{
	for (const struct st_embedded_script *script = st_wren_embedded_scripts;
	    script->name != NULL; ++script) {
		if (script->event == NULL && strcmp(script->name, name) == 0)
			return script;
	}
	return NULL;
}

int
st_wren_interpret_profile_scripts(WrenVM *vm,
	const struct st_profile *profile)
{
	const struct st_wren_host *host;
	const char *next;

	if (vm == NULL || profile == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	host = st_wren_host_from_vm(vm);
	next = profile->wren_scripts;
	while (*next != '\0') {
		const char *comma = strchr(next, ',');
		size_t length = comma == NULL ? strlen(next)
		    : (size_t)(comma - next);
		char name[ST_PROFILE_WREN_SCRIPTS_MAX + 1];
		char *source;
		const struct st_embedded_script *embedded;

		if (length > ST_PROFILE_WREN_SCRIPTS_MAX)
			return IO_ERROR_INVALID_FORMAT;
		if (length != 0) {
			if (!st_profile_script_name_valid(next, length))
				return IO_ERROR_INVALID_FORMAT;
			memcpy(name, next, length);
			name[length] = '\0';
			if (!wrenHasModule(vm, name)) {
				source = read_named_source(
				    host->config.paths->scripts_directory, NULL, name);
				embedded = source == NULL ? embedded_script(name) : NULL;
				if (source == NULL && embedded == NULL)
					return IO_ERROR_NOT_FOUND;
				WrenInterpretResult interpreted = wrenInterpret(vm, name,
				    source == NULL ? embedded->source : source);
				free(source);
				if (interpreted != WREN_RESULT_SUCCESS)
					return IO_ERROR_INVALID_FORMAT;
			}
		}
		if (comma == NULL)
			break;
		next = comma + 1;
	}
	return 0;
}

static WrenHandle *
class_handle(WrenVM *vm, const char *module, const char *class_name)
{
	if (!wrenHasModule(vm, module)
	    || !wrenHasVariable(vm, module, class_name))
		return NULL;
	wrenEnsureSlots(vm, 1);
	wrenGetVariable(vm, module, class_name, 0);
	return wrenGetSlotHandle(vm, 0);
}

static bool
menu_status(struct st_wren_host *host, const char *title,
	const char *const *lines, size_t count)
{
	if (host->menu_ui_class == NULL || host->menu_status == NULL)
		return false;
	wrenEnsureSlots(host->vm, 4);
	wrenSetSlotHandle(host->vm, 0, host->menu_ui_class);
	wrenSetSlotString(host->vm, 1, title);
	wrenSetSlotNewList(host->vm, 2);
	for (size_t i = 0; i < count; ++i) {
		wrenSetSlotString(host->vm, 3, lines[i]);
		wrenInsertInList(host->vm, 2, -1, 3);
	}
	return wrenCall(host->vm, host->menu_status) == WREN_RESULT_SUCCESS;
}

static void
menu_status_clear(struct st_wren_host *host)
{
	if (host->menu_ui_class == NULL || host->menu_status_clear == NULL)
		return;
	wrenEnsureSlots(host->vm, 1);
	wrenSetSlotHandle(host->vm, 0, host->menu_ui_class);
	(void)wrenCall(host->vm, host->menu_status_clear);
}

static int
drain_prompt_input(st_console_t *console, bool *closed,
	bool *resized, struct st_console_event *last_resize)
{
	struct st_console_event event;
	int status;

	while ((status = st_console_event_try(console, &event)) == 0) {
		if (event.type == ST_CONSOLE_EVENT_CLOSE)
			*closed = true;
		else if (event.type == ST_CONSOLE_EVENT_RESIZE) {
			*last_resize = event;
			*resized = true;
		}
	}
	return status == IO_ERROR_WOULD_BLOCK ? 0 : status;
}

static int
host_dialog(st_wren_host_t *host, const char *title,
	const char *question, const char *initial, size_t max_length,
	bool masked, char **result, const char *const *choices, size_t count,
	int *selected, const char *method)
{
	st_console_snapshot_t *snapshot = NULL;
	struct st_console_event resize = {0};
	const struct st_console_event close = {
		.type = ST_CONSOLE_EVENT_CLOSE,
	};
	bool closed, resized = false;
	int status, restore_status;
	WrenHandle *choice_call = NULL;

	if (host == NULL || title == NULL || question == NULL || initial == NULL
	    || result == NULL || max_length > INT_MAX)
		return IO_ERROR_INVALID_ARGUMENT;
	if (host->menu_ui_class == NULL || host->menu_prompt == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_wren_host_status(host, NULL, NULL);
	if (status != 0) return status;
	*result = NULL;
	closed = host->exit_requested;
	status = st_console_snapshot_create(&snapshot, host->config.console);
	if (status != 0)
		return status;
	status = drain_prompt_input(host->config.console, &closed,
	    &resized, &resize);
	if (status != 0 || closed)
		goto finished;
	wrenEnsureSlots(host->vm, 6);
	wrenSetSlotHandle(host->vm, 0, host->menu_ui_class);
	wrenSetSlotString(host->vm, 1, title);
	wrenSetSlotString(host->vm, 2, question);
	if (method != NULL)
		choice_call = wrenMakeCallHandle(host->vm, method);
	else if (choices != NULL) {
		choice_call = wrenMakeCallHandle(host->vm, "choice(_,_,_,_)");
		wrenSetSlotNewList(host->vm, 3);
		for (size_t i = 0; i < count; ++i) {
			wrenSetSlotString(host->vm, 5, choices[i]);
			wrenInsertInList(host->vm, 3, -1, 5);
		}
		wrenSetSlotDouble(host->vm, 4, 0);
	} else {
		wrenSetSlotString(host->vm, 3, initial);
		wrenSetSlotDouble(host->vm, 4, (double)max_length);
		wrenSetSlotBool(host->vm, 5, masked);
	}
	if (wrenCall(host->vm, choice_call != NULL ? choice_call : host->menu_prompt)
	    != WREN_RESULT_SUCCESS)
		status = IO_ERROR_IO;
	else if (choices != NULL && wrenGetSlotType(host->vm, 0) == WREN_TYPE_NUM) {
		double value = wrenGetSlotDouble(host->vm, 0);
		if (!(value >= 0 && value < (double)count) || value != (double)(int)value)
			status = IO_ERROR_INVALID_FORMAT;
		else
			*selected = (int)value;
	} else if (choices == NULL && wrenGetSlotType(host->vm, 0) == WREN_TYPE_STRING) {
		*result = strdup(wrenGetSlotString(host->vm, 0));
		if (*result == NULL)
			status = IO_ERROR_OUT_OF_MEMORY;
	}
	else if (wrenGetSlotType(host->vm, 0) != WREN_TYPE_NULL)
		status = IO_ERROR_INVALID_FORMAT;
finished:
	if (choice_call != NULL)
		wrenReleaseHandle(host->vm, choice_call);
	{
		int drain_status = drain_prompt_input(host->config.console,
		    &closed, &resized, &resize);

		if (status == 0)
			status = drain_status;
	}
	closed = closed || host->exit_requested;
	restore_status = st_console_snapshot_restore(host->config.console,
	    snapshot, NULL);
	st_console_snapshot_destroy(snapshot);
	if (status == 0)
		status = restore_status;
	if (resized) {
		int post_status = st_console_post_event(host->config.console,
		    &resize);

		if (status == 0)
			status = post_status;
	}
	if (closed) {
		int post_status = st_console_post_event(host->config.console,
		    &close);

		if (status == 0)
			status = post_status;
	}
	if (status != 0) {
		free(*result);
		*result = NULL;
	}
	return status;
}

int
st_wren_host_prompt(st_wren_host_t *host, const char *title,
	const char *question, const char *initial, size_t max_length, char **result)
{
	return host_dialog(host, title, question, initial, max_length, false,
	    result, NULL, 0, NULL, NULL);
}

int
st_wren_host_prompt_masked(st_wren_host_t *host, const char *title,
	const char *question, const char *initial, size_t max_length,
	bool masked, char **result)
{
	return host_dialog(host, title, question, initial, max_length, masked,
	    result, NULL, 0, NULL, NULL);
}

int
st_wren_host_choice(st_wren_host_t *host, const char *title,
	const char *question, const char *const *choices, size_t count)
{
	char *unused = NULL;
	int selected = IO_ERROR_CANCELLED, status;
	if (choices == NULL || count == 0 || count > INT_MAX)
		return IO_ERROR_INVALID_ARGUMENT;
	status = host_dialog(host, title, question, "", 0, false, &unused,
	    choices, count, &selected, NULL);
	return status == 0 ? selected : status;
}

int
st_wren_host_alert(st_wren_host_t *host, const char *title, const char *message)
{
	char *unused = NULL;
	int status = host_dialog(host, title, message, "", 0, false, &unused,
	    NULL, 0, NULL, "alert(_,_)");
	free(unused);
	return status;
}

int
st_wren_host_help(st_wren_host_t *host, const char *title, const char *body)
{
	char *unused = NULL;
	int status = host_dialog(host, title, body, "", 0, false, &unused,
	    NULL, 0, NULL, "help(_,_)");
	free(unused);
	return status;
}

int
st_wren_host_disconnected(st_wren_host_t *host, const char *details)
{
	char *unused = NULL;
	int status = host_dialog(host, "Disconnected", details, "", 0, false, &unused,
	    NULL, 0, NULL, "disconnected(_,_)");
	free(unused);
	return status;
}

int
st_wren_host_status(st_wren_host_t *host, const char *title, const char *message)
{
	if (host == NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (message == NULL) {
		if (host->status_snapshot == NULL) return 0;
		menu_status_clear(host);
		int status = st_console_snapshot_restore(host->config.console, host->status_snapshot, NULL);
		st_console_snapshot_destroy(host->status_snapshot);
		host->status_snapshot = NULL;
		return status;
	}
	if (title == NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (host->status_snapshot == NULL) {
		int status = st_console_snapshot_create(&host->status_snapshot, host->config.console);
		if (status != 0) return status;
	}
	if (menu_status(host, title, &message, 1)) return 0;
	(void)st_wren_host_status(host, NULL, NULL);
	return IO_ERROR_IO;
}

bool
st_wren_host_can_trust_tls(st_wren_host_t *host, const struct st_profile *profile)
{
	if (host == NULL || profile == NULL || host->config.safe_mode || profile->name[0] == '\0')
		return false;
	const struct st_bbs_entry *entry = st_bbs_list_find(host->bbs_list, profile->name);
	return entry != NULL && entry->source == ST_BBS_SOURCE_PERSONAL
	    && strcmp(entry->profile->address, profile->address) == 0
	    && entry->profile->port == profile->port
	    && entry->profile->tls_trust_web_pki == profile->tls_trust_web_pki
	    && strcmp(entry->profile->tls_trusted_certificate == NULL ? "" : entry->profile->tls_trusted_certificate,
	    profile->tls_trusted_certificate == NULL ? "" : profile->tls_trusted_certificate) == 0;
}

/* Retain unsaved TLS security decisions across directory reloads.
 * Successful persistence removes the observation. */
struct st_wren_tls_observation {
	struct st_wren_tls_observation *next;
	char name[ST_PROFILE_NAME_MAX + 1];
	char address[ST_PROFILE_ADDRESS_MAX + 1];
	uint16_t port;
	bool web_pki;
	char *certificate;
	int baseline, version;
};

static bool
same_tls_peer(const struct st_wren_tls_observation *observation, const struct st_profile *profile)
{
	return strcmp(observation->name, profile->name) == 0
	    && strcmp(observation->address, profile->address) == 0 && observation->port == profile->port
	    && observation->web_pki == profile->tls_trust_web_pki
	    && strcmp(observation->certificate, profile->tls_trusted_certificate == NULL
	    ? "" : profile->tls_trusted_certificate) == 0;
}

void
st_wren_host_apply_tls_version(st_wren_host_t *host, struct st_profile *profile)
{
	if (host == NULL || profile == NULL) return;
	for (const struct st_wren_tls_observation *item = host->tls_observations; item != NULL; item = item->next)
		if (same_tls_peer(item, profile) && item->baseline == profile->tls_version_floor) {
			profile->tls_version_floor = item->version;
			return;
		}
}

static void
forget_tls_observation(st_wren_host_t *host, const struct st_wren_tls_observation *selected)
{
	struct st_wren_tls_observation **next = &host->tls_observations;
	while (*next != NULL) {
		struct st_wren_tls_observation *item = *next;
		if (selected == NULL || item == selected) {
			*next = item->next;
			free(item->certificate); free(item);
		} else next = &item->next;
	}
}

int
st_wren_host_remember_tls_version(st_wren_host_t *host,
	const struct st_profile *profile, int version)
{
	if (host == NULL || profile == NULL || (version != ST_TLS_VERSION_1_2
	    && version != ST_TLS_VERSION_1_3)) return IO_ERROR_INVALID_ARGUMENT;
	const struct st_bbs_entry *entry = st_bbs_list_find(host->bbs_list, profile->name);
	struct st_wren_tls_observation *item;
	for (item = host->tls_observations; item != NULL; item = item->next)
		if (same_tls_peer(item, profile)) break;
	if (item == NULL) {
		item = calloc(1, sizeof(*item));
		if (item == NULL) return IO_ERROR_OUT_OF_MEMORY;
		item->certificate = strdup(profile->tls_trusted_certificate == NULL ? "" : profile->tls_trusted_certificate);
		if (item->certificate == NULL) { free(item); return IO_ERROR_OUT_OF_MEMORY; }
		(void)snprintf(item->name, sizeof(item->name), "%s", profile->name);
		(void)snprintf(item->address, sizeof(item->address), "%s", profile->address);
		item->port = profile->port; item->web_pki = profile->tls_trust_web_pki;
		item->next = host->tls_observations; host->tls_observations = item;
	}
	item->baseline = entry == NULL ? profile->tls_version_floor : entry->profile->tls_version_floor;
	item->version = version;
	if (!st_wren_host_can_trust_tls(host, profile)) return 0;
	struct st_profile copy;
	st_profile_init(&copy);
	if (!st_profile_copy(&copy, entry->profile)) return IO_ERROR_OUT_OF_MEMORY;
	copy.tls_version_floor = version;
	int status = st_bbs_list_update(host->bbs_list, copy.name, &copy, NULL);
	st_profile_dispose(&copy);
	if (status == 0) forget_tls_observation(host, item);
	return status;
}

int
st_wren_host_trust_tls(st_wren_host_t *host, const struct st_profile *profile,
	const char *pem, char **filename)
{
	struct st_profile copy;
	char base[sizeof(profile->name) + 32], *directory = NULL, *path = NULL;
	FILE *file = NULL;
	int status = IO_ERROR_PERMISSION_DENIED;
	if (filename == NULL || pem == NULL || strstr(pem, "-----BEGIN CERTIFICATE-----") != pem)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!st_wren_host_can_trust_tls(host, profile)) return status;
	struct st_wren_tls_observation *observation;
	for (observation = host->tls_observations; observation != NULL; observation = observation->next)
		if (same_tls_peer(observation, profile)) break;
	const struct st_bbs_entry *entry = st_bbs_list_find(host->bbs_list, profile->name);
	st_profile_init(&copy);
	if (!st_profile_copy(&copy, entry->profile)) return IO_ERROR_OUT_OF_MEMORY;
	ptrdiff_t length = path_parent(NULL, 0, host->config.paths->settings_file);
	if (length < 0) { status = (int)length; goto finished; }
	directory = malloc((size_t)length + 1);
	if (directory == NULL) { status = IO_ERROR_OUT_OF_MEMORY; goto finished; }
	(void)path_parent(directory, (size_t)length + 1, host->config.paths->settings_file);
	char name[sizeof(profile->name)];
	(void)snprintf(name, sizeof(name), "%s", profile->name);
	for (unsigned char *p = (unsigned char *)name; *p != 0; ++p)
		if (*p < 0x20 || strchr("<>:\"/\\|?*", *p) != NULL) *p = '_';
	size_t used = strlen(name);
	while (used > 0 && (name[used - 1] == ' ' || name[used - 1] == '.')) name[--used] = '\0';
	for (unsigned suffix = 0; suffix < 10000; ++suffix) {
		if (suffix == 0) (void)snprintf(base, sizeof(base), "tls-%s.pem", used == 0 ? "certificate" : name);
		else (void)snprintf(base, sizeof(base), "tls-%s(%u).pem", used == 0 ? "certificate" : name, suffix);
		free(path); path = NULL;
		status = path_join_alloc(&path, directory[0] == '\0' ? "." : directory, base);
		if (status != 0) goto finished;
		status = fs_create_parent_directories(path);
		if (status != 0) goto finished;
		status = fs_open(&file, path, "wbx");
		if (status != IO_ERROR_ALREADY_EXISTS) break;
	}
	if (status != 0) goto finished;
	if (fwrite(pem, 1, strlen(pem), file) != strlen(pem)) status = IO_ERROR_IO;
	if (fclose(file) != 0) status = IO_ERROR_IO;
	file = NULL;
	if (status == 0) {
		free(copy.tls_trusted_certificate);
		copy.tls_trusted_certificate = strdup(path);
		copy.tls_trust_web_pki = false;
		copy.tls_version_floor = ST_TLS_VERSION_UNKNOWN;
		status = copy.tls_trusted_certificate == NULL ? IO_ERROR_OUT_OF_MEMORY
		    : st_bbs_list_update(host->bbs_list, copy.name, &copy, NULL);
	}
	if (status != 0) (void)fs_remove_file(path);
	else {
		*filename = path; path = NULL;
		if (observation != NULL) forget_tls_observation(host, observation);
	}
finished:
	free(directory);
	free(path);
	st_profile_dispose(&copy);
	return status;
}

int
st_wren_host_remember_ssh_key(st_wren_host_t *host,
	const struct st_profile *profile, const uint8_t fingerprint[32],
	bool explicit_update)
{
	struct st_profile copy;
	const struct st_bbs_entry *entry;
	int status;
	if (host == NULL || profile == NULL || fingerprint == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (host->config.safe_mode) return 0;
	if (host->bbs_list == NULL) {
		if (!explicit_update) return 0;
		status = st_bbs_list_open(&host->bbs_list, host->config.paths, NULL, 0);
		if (status != 0) return status;
	}
	entry = st_bbs_list_find(host->bbs_list, profile->name);
	if (!explicit_update && (entry == NULL || entry->source != ST_BBS_SOURCE_PERSONAL))
		return 0;
	st_profile_init(&copy);
	if (!st_profile_copy(&copy, entry == NULL ? profile : entry->profile))
		return IO_ERROR_OUT_OF_MEMORY;
	memcpy(copy.ssh_fingerprint, fingerprint, 32);
	copy.ssh_fingerprint_size = 32;
	status = entry != NULL && entry->source == ST_BBS_SOURCE_PERSONAL
	    ? st_bbs_list_update(host->bbs_list, copy.name, &copy, NULL)
	    : st_bbs_list_add(host->bbs_list, &copy, NULL);
	st_profile_dispose(&copy);
	return status;
}

bool
st_wren_host_safe_mode(const st_wren_host_t *host)
{
	return host == NULL || host->config.safe_mode;
}

int
st_wren_host_record_connection(st_wren_host_t *host, struct st_profile *profile)
{
	int status;
	if (host == NULL || profile == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	profile->connected = time(NULL);
	++profile->calls;
	if (host->config.safe_mode || profile->identifier < 0)
		return 0;
	if (host->bbs_list == NULL) {
		status = st_bbs_list_open(&host->bbs_list, host->config.paths, NULL, 0);
		if (status != 0)
			return status;
	}
	status = st_bbs_list_record_connection(host->bbs_list, profile,
	    profile->connected, &profile->calls);
	if (status == 0)
		profile->source = ST_PROFILE_SOURCE_USER;
	return status;
}

static const char *
web_phase_name(enum st_webget_phase phase)
{
	switch (phase) {
		case ST_WEBGET_RESOLVING: return "Resolving";
		case ST_WEBGET_CONNECTING: return "Connecting";
		case ST_WEBGET_TLS: return "TLS setup";
		case ST_WEBGET_REQUESTING: return "Requesting";
		case ST_WEBGET_HEADERS: return "Reading headers";
		case ST_WEBGET_BODY: return "Reading response";
		case ST_WEBGET_VALIDATING: return "Validating";
		case ST_WEBGET_INSTALLING: return "Installing";
	}
	return "Updating";
}

static const char *
web_error_name(int status)
{
	switch (status) {
		case IO_ERROR_INVALID_ARGUMENT: return "Invalid URL";
		case IO_ERROR_NAME_RESOLUTION: return "Name lookup failed";
		case IO_ERROR_CONNECTION_REFUSED: return "Connection refused";
		case IO_ERROR_CONNECTION_RESET: return "Connection reset";
		case IO_ERROR_NETWORK_UNREACHABLE: return "Network unreachable";
		case IO_ERROR_HOST_UNREACHABLE: return "Host unreachable";
		case IO_ERROR_TIMED_OUT: return "Timed out";
		case IO_ERROR_MESSAGE_TOO_LARGE: return "List is too large";
		case IO_ERROR_INVALID_FORMAT: return "Invalid list";
		case IO_ERROR_NOT_SUPPORTED: return "Not supported";
		case IO_ERROR_OUT_OF_MEMORY: return "Out of memory";
		default: return "Update failed";
	}
}

static void
format_byte_count(char *result, size_t size, uint64_t value)
{
	static const char *const units[] = {
		"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB",
	};
	uint64_t whole = value;
	uint64_t remainder = 0;
	size_t unit = 0;

	while (whole >= 1024 && unit + 1 < sizeof(units) / sizeof(units[0])) {
		remainder = whole % 1024;
		whole /= 1024;
		unit++;
	}
	if (unit == 0 || whole >= 10) {
		(void)snprintf(result, size, "%llu %s",
		    (unsigned long long)whole, units[unit]);
	}
	else {
		uint64_t tenth = (remainder * 10 + 512) / 1024;

		if (tenth == 10) {
			whole++;
			tenth = 0;
		}
		(void)snprintf(result, size, "%llu.%llu %s",
		    (unsigned long long)whole, (unsigned long long)tenth,
		    units[unit]);
	}
}

static unsigned
web_percentage(uint64_t received, uint64_t total)
{
	uint64_t whole;
	uint64_t remainder;

	if (total == 0)
		return 0;
	if (received >= total)
		return 100;
	whole = total / 100;
	remainder = total % 100;
	for (unsigned percent = 100; percent > 0; --percent) {
		uint64_t threshold = whole * percent
		    + (remainder * percent + 99) / 100;

		if (received >= threshold)
			return percent;
	}
	return 0;
}

struct web_progress_ui {
	struct st_wren_host *host;
	st_console_snapshot_t *snapshot;
	char (*text)[192];
	const char **lines;
	bool visible;
	bool disabled;
};

enum {
	WEB_PROGRESS_ROW_WIDTH = 67,
};

static void
format_web_progress(char *result, size_t size,
	const struct st_bbs_web_refresh_item *item)
{
	char content[192];
	const char *name = item->name == NULL ? "" : item->name;
	const char *phase;

	if (item->state == ST_BBS_WEB_REFRESH_PENDING) {
		(void)snprintf(content, sizeof(content), "%-20.20s: Waiting",
		    name);
	}
	else if (item->state == ST_BBS_WEB_REFRESH_COMPLETE) {
		(void)snprintf(content, sizeof(content), "%-20.20s: Done", name);
	}
	else if (item->state == ST_BBS_WEB_REFRESH_FAILED) {
		(void)snprintf(content, sizeof(content), "%-20.20s: %.44s", name,
		    web_error_name(item->status));
	}
	else if (item->received != 0 || item->total != 0) {
		char received[24];

		phase = web_phase_name(item->phase);
		format_byte_count(received, sizeof(received), item->received);
		if (item->total_known && item->total != 0) {
			char total[24];

			format_byte_count(total, sizeof(total), item->total);
			(void)snprintf(content, sizeof(content),
			    "%-20.20s: %-20.20s %9s/%-9s %3u%%", name,
			    phase, received, total,
			    web_percentage(item->received, item->total));
		}
		else {
			(void)snprintf(content, sizeof(content),
			    "%-20.20s: %-20.20s %9s", name, phase, received);
		}
	}
	else {
		phase = web_phase_name(item->phase);
		(void)snprintf(content, sizeof(content), "%-20.20s: %-20.20s",
		    name, phase);
	}
	(void)snprintf(result, size, "%-*.*s", WEB_PROGRESS_ROW_WIDTH,
	    WEB_PROGRESS_ROW_WIDTH, content);
}

static void
show_web_progress(const struct st_bbs_web_refresh_item *items, size_t count,
	void *context)
{
	struct web_progress_ui *ui = context;

	if (ui->disabled)
		return;
	if (ui->text == NULL) {
		ui->text = calloc(count, sizeof(ui->text[0]));
		ui->lines = calloc(count, sizeof(ui->lines[0]));
		if (ui->text == NULL || ui->lines == NULL
		    || st_console_snapshot_create(&ui->snapshot,
		    ui->host->config.console) != 0) {
			free(ui->lines);
			free(ui->text);
			ui->lines = NULL;
			ui->text = NULL;
			ui->disabled = true;
			return;
		}
	}
	for (size_t i = 0; i < count; ++i) {
		format_web_progress(ui->text[i], sizeof(ui->text[i]), &items[i]);
		ui->lines[i] = ui->text[i];
	}
	if (!menu_status(ui->host, "Web Cache Update", ui->lines, count)) {
		menu_status_clear(ui->host);
		(void)st_console_snapshot_restore(ui->host->config.console,
		    ui->snapshot, NULL);
		st_console_snapshot_destroy(ui->snapshot);
		ui->snapshot = NULL;
		ui->visible = false;
		ui->disabled = true;
		return;
	}
	ui->visible = true;
}

int
st_wren_host_refresh_web_lists(st_wren_host_t *public_host)
{
	struct st_wren_host *host = public_host;
	struct web_progress_ui ui = { .host = host };
	int status;

	if (host == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_bbs_web_cache_refresh(host->config.paths, show_web_progress,
	    &ui);
	if (ui.visible)
		menu_status_clear(host);
	if (ui.snapshot != NULL) {
		(void)st_console_snapshot_restore(host->config.console, ui.snapshot, NULL);
		st_console_snapshot_destroy(ui.snapshot);
	}
	free(ui.lines);
	free(ui.text);
	return status;
}

int
st_wren_picker_host_init(struct st_wren_host *host)
{
	const struct st_embedded_script *bootstrap;
	const struct st_embedded_script *request;
	WrenConfiguration config;
	int status;

	bootstrap = embedded_script("picker_bootstrap");
	request = embedded_script("syncterm_picker");
	if (bootstrap == NULL || request == NULL)
		return IO_ERROR_NOT_FOUND;
	wrenInitConfiguration(&config);
	config.userData = &host->picker;
	host->picker.event = "picker";
	config.writeFn = st_wren_write_output;
	config.errorFn = st_wren_report_error;
	config.loadModuleFn = st_wren_load_module;
	config.bindForeignMethodFn = st_wren_bind_method;
	config.bindForeignClassFn = st_wren_bind_class;
	host->picker_vm = wrenNewVM(&config);
	if (host->picker_vm == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	host->picker.vm = host->picker_vm;
	if (wrenInterpret(host->picker_vm, bootstrap->name, bootstrap->source)
	    != WREN_RESULT_SUCCESS
	    || wrenInterpret(host->picker_vm, request->name, request->source)
	    != WREN_RESULT_SUCCESS)
		return IO_ERROR_INVALID_FORMAT;
	status = st_wren_input_context_init(host->picker_vm);
	if (status != 0)
		return status;
	host->picker_bootstrap = class_handle(host->picker_vm,
	    "picker_bootstrap", "PickerBootstrap");
	host->picker_run = wrenMakeCallHandle(host->picker_vm, "run(_,_)" );
	host->picker_recover = wrenMakeCallHandle(host->picker_vm,
	    "recover(_,_)" );
	if (host->picker_bootstrap == NULL || host->picker_run == NULL
	    || host->picker_recover == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	(void)st_wren_interpret_event_scripts(host->picker_vm, "picker");
	host->picker_class = class_handle(host->picker_vm, "file_picker",
	    "FilePicker");
	return 0;
}

void
st_wren_picker_host_dispose(struct st_wren_host *host)
{
	if (host == NULL || host->picker_vm == NULL)
		return;
	st_wren_input_context_dispose(host->picker_vm);
	if (host->picker_recover != NULL)
		wrenReleaseHandle(host->picker_vm, host->picker_recover);
	if (host->picker_run != NULL)
		wrenReleaseHandle(host->picker_vm, host->picker_run);
	if (host->picker_class != NULL)
		wrenReleaseHandle(host->picker_vm, host->picker_class);
	if (host->picker_bootstrap != NULL)
		wrenReleaseHandle(host->picker_vm, host->picker_bootstrap);
	st_wren_log_dispose(host->picker_vm);
	st_wren_session_object_dispose(host->picker_vm);
	wrenFreeVM(host->picker_vm);
	host->picker_vm = NULL;
	host->picker.vm = NULL;
	host->picker_recover = NULL;
	host->picker_run = NULL;
	host->picker_class = NULL;
	host->picker_bootstrap = NULL;
}

struct st_wren_host *
st_wren_host_from_vm(WrenVM *vm)
{
	return st_wren_context_from_vm(vm)->host;
}

struct st_wren_context *
st_wren_context_from_vm(WrenVM *vm)
{
	return wrenGetUserData(vm);
}

void
st_wren_abort(WrenVM *vm, const char *message)
{
	wrenSetSlotString(vm, 0, message);
	wrenAbortFiber(vm, 0);
}

void
st_wren_unsupported(WrenVM *vm)
{
	st_wren_abort(vm, "this capability is not available in this VM");
}

int
st_wren_host_create(st_wren_host_t **result,
	const struct st_wren_host_config *config)
{
	struct st_wren_host *host;
	WrenConfiguration wren_config;
	int status;

	if (result == NULL || config == NULL || config->console == NULL
	    || ((config->display == NULL) != (config->picker_console == NULL))
	    || config->paths == NULL || config->paths->scripts_directory == NULL
	    || config->hyperlinks == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	host = calloc(1, sizeof(*host));
	if (host == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	host->config = *config;
	host->menu.host = host;
	host->menu.transfer_log_level = ST_PROFILE_LOG_INFO;
	host->menu.console = config->console;
	host->picker.host = host;
	host->picker.transfer_log_level = ST_PROFILE_LOG_INFO;
	host->picker.console = config->picker_console != NULL
	    ? config->picker_console : config->console;
	status = st_hyperlink_registry_register_root(config->hyperlinks,
	    mark_host_hyperlinks, host);
	if (status != 0) {
		free(host);
		return status;
	}
	host->hyperlink_root_registered = true;
	host->font_generation = 1;
	host->settings_generation = 1;
	st_settings_font_draft_init(&host->font_draft);
	if (st_settings_font_draft_open(&host->font_draft) < 0) {
		st_wren_host_destroy(host);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	host->current_screen_mode = st_settings.startup_mode >= ST_SCREEN_MODE_CURRENT
	    && st_settings.startup_mode <= ST_SCREEN_MODE_AMIGA_640X200
	    ? (enum st_screen_mode)st_settings.startup_mode
	    : ST_SCREEN_MODE_CURRENT;
	st_profile_init(&host->selected);
	st_wren_bbs_model_init(&host->bbs_model);
	status = load_builtin_fonts(&host->menu);
	if (status == 0)
		status = reload_custom_fonts_current(&host->menu);
	if (status == 0 && host->picker.console != host->menu.console) {
		status = load_builtin_fonts(&host->picker);
		if (status == 0)
			status = reload_custom_fonts_current(&host->picker);
	}
	else if (status == 0)
		host->picker.fonts = host->menu.fonts;
	if (status == 0) {
		struct st_screen_mode_descriptor custom;
		const struct st_screen_mode_descriptor *mode
		    = st_screen_mode_get(host->current_screen_mode);

		if (mode != NULL && mode->mode == ST_SCREEN_MODE_CUSTOM) {
			status = st_screen_mode_custom(&custom,
			    (uint32_t)st_settings.custom_columns,
			    (uint32_t)st_settings.custom_rows,
			    (uint32_t)st_settings.custom_font_height,
			    (uint32_t)st_settings.custom_aspect_width,
			    (uint32_t)st_settings.custom_aspect_height);
			if (status == 0)
				mode = &custom;
		}
		else if (mode == NULL)
			mode = st_screen_mode_get(ST_SCREEN_MODE_CURRENT);
		if (status == 0)
			status = install_mode_state(&host->menu, mode, false);
		if (status == 0 && host->picker.console != host->menu.console)
			status = install_mode_state(&host->picker, mode, false);
		if (status == 0)
			host->current_mode = *mode;
	}
	if (status != 0) {
		st_wren_host_destroy(host);
		return status;
	}
	status = st_theme_manager_create(&host->themes, config->paths,
	    &st_settings, host->last_error, sizeof(host->last_error));
	if (status != 0) {
		st_wren_host_destroy(host);
		return status;
	}
	wrenInitConfiguration(&wren_config);
	wren_config.userData = &host->menu;
	host->menu.event = "menu";
	wren_config.writeFn = st_wren_write_output;
	wren_config.errorFn = st_wren_report_error;
	wren_config.loadModuleFn = st_wren_load_module;
	wren_config.bindForeignMethodFn = st_wren_bind_method;
	wren_config.bindForeignClassFn = st_wren_bind_class;
	host->vm = wrenNewVM(&wren_config);
	if (host->vm == NULL) {
		st_wren_host_destroy(host);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	host->menu.vm = host->vm;
	status = st_wren_interpret_event_scripts(host->vm, "menu");
	if (status != 0 || !wrenHasModule(host->vm, "main_menu")
	    || !wrenHasVariable(host->vm, "main_menu", "MainMenu")) {
		st_wren_host_destroy(host);
		return status == 0 ? IO_ERROR_INVALID_FORMAT : status;
	}
	status = st_wren_input_context_init(host->vm);
	if (status != 0) {
		st_wren_host_destroy(host);
		return status;
	}
	wrenEnsureSlots(host->vm, 1);
	wrenGetVariable(host->vm, "main_menu", "MainMenu", 0);
	host->main_menu = wrenGetSlotHandle(host->vm, 0);
	host->run_menu = wrenMakeCallHandle(host->vm, "run(_,_)" );
	if (host->main_menu == NULL || host->run_menu == NULL) {
		st_wren_host_destroy(host);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	host->menu_ui_class = class_handle(host->vm, "menu_host_ui",
	    "MenuHostUI");
	if (host->menu_ui_class != NULL) {
		host->menu_prompt = wrenMakeCallHandle(host->vm,
		    "prompt(_,_,_,_,_)");
		host->menu_status = wrenMakeCallHandle(host->vm, "status(_,_)" );
		host->menu_status_clear = wrenMakeCallHandle(host->vm,
		    "statusClear()" );
		if (host->menu_prompt == NULL || host->menu_status == NULL
		    || host->menu_status_clear == NULL) {
			st_wren_host_destroy(host);
			return IO_ERROR_OUT_OF_MEMORY;
		}
	}
	status = st_wren_picker_host_init(host);
	if (status != 0) {
		st_wren_host_destroy(host);
		return status;
	}
	*result = host;
	return 0;
}

void
st_wren_host_destroy(st_wren_host_t *host)
{
	if (host == NULL)
		return;
	(void)st_wren_host_status(host, NULL, NULL);
	forget_tls_observation(host, NULL);
	if (host->hyperlink_root_registered) {
		st_hyperlink_registry_unregister_root(host->config.hyperlinks,
		    mark_host_hyperlinks, host);
		host->hyperlink_root_registered = false;
	}
	st_wren_picker_host_dispose(host);
	if (host->vm != NULL && host->menu_prompt != NULL)
		wrenReleaseHandle(host->vm, host->menu_prompt);
	if (host->vm != NULL && host->menu_status_clear != NULL)
		wrenReleaseHandle(host->vm, host->menu_status_clear);
	if (host->vm != NULL && host->menu_status != NULL)
		wrenReleaseHandle(host->vm, host->menu_status);
	if (host->vm != NULL && host->menu_ui_class != NULL)
		wrenReleaseHandle(host->vm, host->menu_ui_class);
	if (host->vm != NULL && host->run_menu != NULL)
		wrenReleaseHandle(host->vm, host->run_menu);
	if (host->vm != NULL && host->main_menu != NULL)
		wrenReleaseHandle(host->vm, host->main_menu);
	if (host->vm != NULL) {
		st_wren_input_context_dispose(host->vm);
		st_wren_log_dispose(host->vm);
		st_wren_session_object_dispose(host->vm);
		wrenFreeVM(host->vm);
		host->menu.vm = NULL;
	}
	st_emoji_locale_destroy(host->emoji_locale);
	st_bbs_list_close(host->bbs_list);
	st_wren_bbs_model_dispose(&host->bbs_model);
	st_theme_manager_free(&host->themes);
	st_settings_font_draft_dispose(&host->font_draft);
	st_profile_dispose(&host->selected);
	free(host);
}

int
st_wren_host_run_menu(st_wren_host_t *host, struct st_profile *selected,
	bool *has_selection)
{
	WrenInterpretResult result;

	if (host == NULL || selected == NULL || has_selection == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	host->selected_valid = false;
	wrenEnsureSlots(host->vm, 3);
	wrenSetSlotHandle(host->vm, 0, host->main_menu);
	wrenSetSlotNull(host->vm, 1);
	wrenSetSlotBool(host->vm, 2, false);
	result = wrenCall(host->vm, host->run_menu);
	if (result != WREN_RESULT_SUCCESS)
		return IO_ERROR_INVALID_FORMAT;
	host->selected_valid = wrenGetSlotType(host->vm, 0) != WREN_TYPE_NULL
	    && st_wren_menu_copy_profile(host->vm, 0, &host->selected);
	*has_selection = host->selected_valid;
	if (host->selected_valid && !st_profile_copy(selected, &host->selected))
		return IO_ERROR_OUT_OF_MEMORY;
	return 0;
}

int
st_wren_host_run_connected_menu(struct st_wren_host *host,
	const char *current)
{
	if (host == NULL || host->vm == NULL || host->run_menu == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	wrenEnsureSlots(host->vm, 3);
	wrenSetSlotHandle(host->vm, 0, host->main_menu);
	if (current != NULL && current[0] != '\0')
		wrenSetSlotString(host->vm, 1, current);
	else
		wrenSetSlotNull(host->vm, 1);
	wrenSetSlotBool(host->vm, 2, true);
	return wrenCall(host->vm, host->run_menu) == WREN_RESULT_SUCCESS
	    ? 0 : IO_ERROR_INVALID_FORMAT;
}

bool
st_wren_host_exit_requested(const st_wren_host_t *host)
{
	return host != NULL && host->exit_requested;
}

int
st_wren_host_get_screen_mode(const st_wren_host_t *host,
	enum st_screen_mode *result)
{
	if (host == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*result = host->current_screen_mode;
	return 0;
}

const char *
st_wren_host_last_error(const st_wren_host_t *host)
{
	return host == NULL || host->last_error[0] == '\0' ? NULL :
	    host->last_error;
}
