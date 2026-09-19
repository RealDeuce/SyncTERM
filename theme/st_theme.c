/* SyncTERM menu themes and local theme files. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_theme.h"

#include "filesystem.h"
#include "ini.h"
#include "io_error.h"
#include "path.h"
#include "st_app_paths.h"
#include "st_codepage.h"
#include "st_settings.h"
#include "st_utf8.h"
#include "string_compat.h"
#include "string_util.h"
#include "theme_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct style_template {
	const char *role;
	int font;
	int legacy_attr;
	int foreground;
	int background;
};

struct glyph_template {
	const char *name;
	uint8_t cp437;
	uint8_t ascii;
};

#define S(role, font, attribute, foreground, background) \
	{role, font, attribute, foreground, background}

static const struct style_template default_styles[] = {
	S("default", 0, 0x1f, 0xffffff, 0x0000a8),
	S("default.inactive", -1, 0x3f, 0xffffff, 0x00aaaa),
	S("frame", -1, 0x1e, 0xffff55, -1),
	S("frame.inactive", -1, 0x3f, 0xffffff, 0x00aaaa),
	S("title", -1, 0x1e, 0xffff55, -1),
	S("title.inactive", -1, 0x3f, 0xffffff, 0x00aaaa),
	S("menu.item", -1, 0x1f, -1, -1),
	S("menu.item.focused", -1, 0x70, 0x000000, 0xaaaaaa),
	S("menu.item.disabled", -1, 0x18, 0x808080, -1),
	S("list.item", -1, 0x1f, -1, -1),
	S("list.item.focused", -1, 0x70, 0x000000, 0xaaaaaa),
	S("list.item.disabled", -1, 0x18, 0x808080, -1),
	S("list.item.focused.inactive", -1, 0x3e, 0xffff55, 0x00aaaa),
	S("input", -1, 0x70, 0x000000, 0xaaaaaa),
	S("input.focused", -1, 0x70, 0x000000, 0xffffff),
	S("button", -1, 0x1f, -1, -1),
	S("button.hotkey", -1, 0x1e, 0xffff55, -1),
	S("button.focused", -1, 0x70, 0x000000, 0xaaaaaa),
	S("button.focused.hotkey", -1, 0x78, 0x555555, -1),
	S("statusbar", -1, 0x30, 0x000000, 0x00aaaa),
	S("disconnected", -1, 0x4f, 0xffffff, 0xaa0000),
	S("checkbox", -1, 0x1f, -1, -1),
	S("checkbox.focused", -1, 0x70, 0x000000, 0xaaaaaa),
	S("radio.item", -1, 0x1f, -1, -1),
	S("radio.item.focused", -1, 0x70, 0x000000, 0xaaaaaa),
	S("spinbox", -1, 0x70, 0x000000, 0xaaaaaa),
	S("spinbox.focused", -1, 0x70, 0x000000, 0xffffff),
	S("menubar", -1, 0x70, 0x000000, 0xaaaaaa),
	S("menubar.item", -1, 0x70, 0x000000, 0xaaaaaa),
	S("menubar.item.focused", -1, 0x07, 0xaaaaaa, 0x000000),
	S("scrollbar.track", -1, 0x17, 0xaaaaaa, 0x0000a8),
	S("scrollbar.thumb", -1, 0x1f, 0xffffff, 0x0000a8),
	S("progress.fill", -1, 0x1b, 0x55ffff, 0x0000a8),
	S("logview.info", -1, 0x1f, 0xffffff, 0x0000a8),
	S("logview.notice", -1, 0x1e, 0xffff55, 0x0000a8),
	S("logview.warning", -1, 0x1d, 0xff55ff, 0x0000a8),
	S("logview.error", -1, 0x1c, 0xff5555, 0x0000a8),
	S("help", -1, 0x1f, 0xffffff, 0x0000a8),
	S("help.text", -1, -1, -1, -1),
	S("help.bold", -1, 0x1e, 0xffff55, -1),
	S("help.code", -1, 0x1e, 0xffff55, -1),
	S("help.heading.1", -1, 0x31, 0x0000a8, 0x00aaaa),
	S("help.heading.2", -1, 0x31, 0x0000a8, 0x00aaaa),
	S("help.heading.3", -1, 0x31, 0x0000a8, 0x00aaaa),
	S("help.bullet", -1, 0x1f, 0xffffff, -1),
	S("popup", -1, 0x4e, 0xffff55, 0xaa0000),
	S("popup.frame", -1, 0x4f, 0xffffff, 0xaa0000),
	S("popup.frame.inactive", -1, 0x47, 0xaaaaaa, -1),
};

#undef S

#define G(name, cp437, ascii) {name, cp437, ascii}

static const struct glyph_template default_glyphs[] = {
	G("background", 0x20, ' '),
	G("frame.display.topLeft", 0xda, '+'),
	G("frame.display.top", 0xc4, '-'),
	G("frame.display.topRight", 0xbf, '+'),
	G("frame.display.left", 0xb3, '|'),
	G("frame.display.right", 0xb3, '|'),
	G("frame.display.bottomLeft", 0xc0, '+'),
	G("frame.display.bottom", 0xc4, '-'),
	G("frame.display.bottomRight", 0xd9, '+'),
	G("frame.display.tee.left", 0xc3, '+'),
	G("frame.display.tee.right", 0xb4, '+'),
	G("frame.display.title.left", 0xb4, '-'),
	G("frame.display.title.right", 0xc3, '-'),
	G("frame.display.tee.top", 0xc2, '+'),
	G("frame.display.tee.bottom", 0xc1, '+'),
	G("frame.display.cross", 0xc5, '+'),
	G("frame.display.separator", 0xc4, '-'),
	G("frame.control.topLeft", 0xc9, '+'),
	G("frame.control.top", 0xcd, '='),
	G("frame.control.topRight", 0xbb, '+'),
	G("frame.control.left", 0xba, '|'),
	G("frame.control.right", 0xba, '|'),
	G("frame.control.bottomLeft", 0xc8, '+'),
	G("frame.control.bottom", 0xcd, '='),
	G("frame.control.bottomRight", 0xbc, '+'),
	G("frame.control.tee.left", 0xcc, '+'),
	G("frame.control.tee.right", 0xb9, '+'),
	G("frame.control.title.left", 0xb5, '='),
	G("frame.control.title.right", 0xc6, '='),
	G("frame.control.tee.top", 0xcb, '+'),
	G("frame.control.tee.bottom", 0xca, '+'),
	G("frame.control.cross", 0xce, '+'),
	G("frame.control.separator", 0xcd, '='),
	G("scrollbar.track", 0xb0, ':'),
	G("scrollbar.thumb", 0xdb, '#'),
	G("scrollbar.up", 0x1e, '^'),
	G("scrollbar.down", 0x1f, 'v'),
	G("scrollbar.separator", 0xb3, '|'),
	G("arrow.up", 0x18, '^'), G("arrow.down", 0x19, 'v'),
	G("arrow.right", 0x10, '>'), G("arrow.left", 0x11, '<'),
	G("focus.left", 0x10, '>'), G("focus.right", 0x11, '<'),
	G("check.on", 0xfb, 'X'), G("check.off", 0x20, ' '),
	G("radio.on", 0x07, '*'), G("radio.off", 0x09, 'o'),
	G("tag.on", 0xaf, '>'), G("tag.off", 0x20, ' '),
};

#undef G

static void
set_error(char *error, size_t error_size, const char *format, ...)
{
	va_list arguments;

	if (error == NULL || error_size == 0)
		return;
	va_start(arguments, format);
	(void)vsnprintf(error, error_size, format, arguments);
	va_end(arguments);
}

static bool
replace_string(char **destination, const char *source)
{
	char *copy = source == NULL ? NULL : strdup(source);

	if (source != NULL && copy == NULL)
		return false;
	free(*destination);
	*destination = copy;
	return true;
}

void
theme_free(struct st_theme *theme)
{
	if (theme == NULL)
		return;
	free(theme->filename);
	free(theme->package);
	free(theme->name);
	free(theme->author);
	free(theme->description);
	free(theme->version);
	for (size_t i = 0; i < theme->style_count; ++i)
		free(theme->styles[i].role);
	free(theme->styles);
	for (size_t i = 0; i < theme->glyph_count; ++i)
		free(theme->glyphs[i].name);
	free(theme->glyphs);
	free(theme);
}

struct st_theme *
theme_clone(const struct st_theme *source)
{
	struct st_theme *theme = calloc(1, sizeof(*theme));

	if (theme == NULL)
		return NULL;
	if (!replace_string(&theme->filename, source->filename)
	    || !replace_string(&theme->package, source->package)
	    || !replace_string(&theme->name, source->name)
	    || !replace_string(&theme->author, source->author)
	    || !replace_string(&theme->description, source->description)
	    || !replace_string(&theme->version, source->version))
		goto failed;
	if (source->style_count != 0) {
		theme->styles = calloc(source->style_count, sizeof(theme->styles[0]));
		if (theme->styles == NULL)
			goto failed;
		for (size_t i = 0; i < source->style_count; ++i) {
			theme->styles[i] = source->styles[i];
			theme->styles[i].role = strdup(source->styles[i].role);
			if (theme->styles[i].role == NULL)
				goto failed;
			++theme->style_count;
		}
	}
	if (source->glyph_count != 0) {
		theme->glyphs = calloc(source->glyph_count, sizeof(theme->glyphs[0]));
		if (theme->glyphs == NULL)
			goto failed;
		for (size_t i = 0; i < source->glyph_count; ++i) {
			theme->glyphs[i] = source->glyphs[i];
			theme->glyphs[i].name = strdup(source->glyphs[i].name);
			if (theme->glyphs[i].name == NULL)
				goto failed;
			++theme->glyph_count;
		}
	}
	return theme;

failed:
	theme_free(theme);
	return NULL;
}

static struct st_theme *
build_default_theme(void)
{
	struct st_theme *theme = calloc(1, sizeof(*theme));

	if (theme == NULL)
		return NULL;
	theme->name = strdup("SyncTERM Default");
	theme->styles = calloc(sizeof(default_styles) / sizeof(default_styles[0]),
	    sizeof(theme->styles[0]));
	theme->glyphs = calloc(sizeof(default_glyphs) / sizeof(default_glyphs[0]),
	    sizeof(theme->glyphs[0]));
	if (theme->name == NULL || theme->styles == NULL || theme->glyphs == NULL)
		goto failed;
	for (size_t i = 0; i < sizeof(default_styles) / sizeof(default_styles[0]);
	    ++i) {
		theme->styles[i] = (struct st_theme_style){
			.role = strdup(default_styles[i].role),
			.font = default_styles[i].font,
			.legacy_attr = default_styles[i].legacy_attr,
			.foreground = default_styles[i].foreground,
			.background = default_styles[i].background,
		};
		if (theme->styles[i].role == NULL)
			goto failed;
		++theme->style_count;
	}
	for (size_t i = 0; i < sizeof(default_glyphs) / sizeof(default_glyphs[0]);
	    ++i) {
		theme->glyphs[i] = (struct st_theme_glyph){
			.name = strdup(default_glyphs[i].name),
			.cp437 = default_glyphs[i].cp437,
			.ascii = default_glyphs[i].ascii,
			.present = (1u << ST_THEME_GLYPH_CP437)
			    | (1u << ST_THEME_GLYPH_ASCII),
		};
		if (theme->glyphs[i].name == NULL
		    || !resolve_glyph(&theme->glyphs[i]))
			goto failed;
		++theme->glyph_count;
	}
	return theme;

failed:
	theme_free(theme);
	return NULL;
}

struct st_theme_style *
find_style(struct st_theme *theme, const char *role)
{
	for (size_t i = 0; i < theme->style_count; ++i)
		if (strcmp(theme->styles[i].role, role) == 0)
			return &theme->styles[i];
	return NULL;
}

struct st_theme_style *
add_style(struct st_theme *theme, const char *role)
{
	if (theme->style_count == SIZE_MAX / sizeof(theme->styles[0]))
		return NULL;
	struct st_theme_style *styles = realloc(theme->styles,
	    (theme->style_count + 1) * sizeof(theme->styles[0]));
	if (styles == NULL)
		return NULL;
	theme->styles = styles;
	struct st_theme_style *style = &styles[theme->style_count];
	*style = (struct st_theme_style){
		.role = strdup(role),
		.font = ST_THEME_INHERIT,
		.legacy_attr = ST_THEME_INHERIT,
		.foreground = ST_THEME_INHERIT,
		.background = ST_THEME_INHERIT,
	};
	if (style->role == NULL)
		return NULL;
	++theme->style_count;
	return style;
}

struct st_theme_glyph *
find_glyph(struct st_theme *theme, const char *name)
{
	for (size_t i = 0; i < theme->glyph_count; ++i)
		if (strcmp(theme->glyphs[i].name, name) == 0)
			return &theme->glyphs[i];
	return NULL;
}

struct st_theme_glyph *
add_glyph(struct st_theme *theme, const char *name)
{
	if (theme->glyph_count == SIZE_MAX / sizeof(theme->glyphs[0]))
		return NULL;
	struct st_theme_glyph *glyphs = realloc(theme->glyphs,
	    (theme->glyph_count + 1) * sizeof(theme->glyphs[0]));
	if (glyphs == NULL)
		return NULL;
	theme->glyphs = glyphs;
	struct st_theme_glyph *glyph = &glyphs[theme->glyph_count];
	*glyph = (struct st_theme_glyph){.name = strdup(name)};
	if (glyph->name == NULL)
		return NULL;
	++theme->glyph_count;
	return glyph;
}

bool
resolve_glyph(struct st_theme_glyph *glyph)
{
	if (glyph == NULL
	    || (glyph->present & (1u << ST_THEME_GLYPH_ASCII)) == 0)
		return false;
	if ((glyph->present & (1u << ST_THEME_GLYPH_CP437)) == 0)
		glyph->cp437 = glyph->ascii;
	if ((glyph->present & (1u << ST_THEME_GLYPH_CODEPOINT)) != 0)
		return st_unicode_scalar_valid(glyph->codepoint);
	glyph->codepoint = st_codepage_to_unicode(ST_CODEPAGE_CP437,
	    glyph->cp437, true);
	return true;
}

static int
classic_colour(unsigned value, unsigned sentinel, int fallback)
{
	return value == sentinel ? fallback : (int)value;
}

static bool
set_complete_style(struct st_theme *theme, const char *role, int foreground,
	int background)
{
	static const int rgb[] = {
		0x000000, 0x0000aa, 0x00aa00, 0x00aaaa,
		0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
		0x555555, 0x5555ff, 0x55ff55, 0x55ffff,
		0xff5555, 0xff55ff, 0xffff55, 0xffffff,
	};
	struct st_theme_style *style = find_style(theme, role);

	if (style == NULL)
		style = add_style(theme, role);
	if (style == NULL)
		return false;
	style->font = 0;
	style->legacy_attr = (background << 4) | foreground;
	style->foreground = rgb[foreground];
	style->background = rgb[background];
	return true;
}

struct st_theme *
build_classic_theme(const struct st_theme *default_theme,
	const struct st_settings *settings)
{
	static const char *const normal[] = {
		"default", "menu.item", "list.item", "button", "checkbox",
		"radio.item", "help", "help.bullet",
	};
	static const char *const inactive[] = {
		"default.inactive", "frame.inactive", "title.inactive",
	};
	static const char *const selected[] = {
		"menu.item.focused", "list.item.focused", "input",
		"input.focused", "button.focused", "button.focused.hotkey",
		"checkbox.focused", "radio.item.focused", "spinbox",
		"spinbox.focused",
	};
	struct st_theme *theme = theme_clone(default_theme);
	int frame = classic_colour(settings->theme_frame_color, 16, 14);
	int text = classic_colour(settings->theme_text_color, 16, 15);
	int background = classic_colour(settings->theme_background_color, 8, 1);
	int inverse = classic_colour(settings->theme_inverse_color, 8, 3);
	int lightbar = classic_colour(settings->theme_lightbar_color, 16, 1);
	int lightbar_background = classic_colour(
	    settings->theme_lightbar_background_color, 8, 7);

	if (theme == NULL || !replace_string(&theme->filename, "")
	    || !replace_string(&theme->name, "Classic Theme")
	    || !replace_string(&theme->author, "SyncTERM")
	    || !replace_string(&theme->description,
	    "The built-in SyncTERM theme using the configured Classic colours"))
		goto failed;
	for (size_t i = 0; i < sizeof(normal) / sizeof(normal[0]); ++i)
		if (!set_complete_style(theme, normal[i], text, background))
			goto failed;
	for (size_t i = 0; i < sizeof(inactive) / sizeof(inactive[0]); ++i)
		if (!set_complete_style(theme, inactive[i], text, inverse))
			goto failed;
	for (size_t i = 0; i < sizeof(selected) / sizeof(selected[0]); ++i)
		if (!set_complete_style(theme, selected[i], lightbar,
		    lightbar_background))
			goto failed;
	if (!set_complete_style(theme, "list.item.focused.inactive", frame, inverse)
	    || !set_complete_style(theme, "frame", frame, background)
	    || !set_complete_style(theme, "title", frame, background)
	    || !set_complete_style(theme, "button.hotkey", frame, background)
	    || !set_complete_style(theme, "statusbar", 0, inverse)
	    || !set_complete_style(theme, "classic.backdrop", inverse, background)
	    || !set_complete_style(theme, "classic.header", background, inverse)
	    || !set_complete_style(theme, "classic.console", 14, inverse)
	    || !set_complete_style(theme, "classic.console.error", 12, inverse)
	    || !set_complete_style(theme, "classic.comment", text, inverse)
	    || !set_complete_style(theme, "classic.hint", 0, inverse)
	    || !set_complete_style(theme, "classic.hotkey", background, inverse)
	    || !set_complete_style(theme, "help.bold", frame, background)
	    || !set_complete_style(theme, "help.code", frame, background)
	    || !set_complete_style(theme, "help.heading.1", background, inverse)
	    || !set_complete_style(theme, "help.heading.2", background, inverse)
	    || !set_complete_style(theme, "help.heading.3", background, inverse))
		goto failed;
	return theme;

failed:
	theme_free(theme);
	return NULL;
}

static bool
parse_number(const char *text, long minimum, long maximum, bool inherit,
	int *result)
{
	size_t begin;
	size_t end;
	unsigned base = 10;
	unsigned long value = 0;
	bool any = false;

	string_ascii_trim_span(text, strlen(text), &begin, &end);
	if (inherit && ascii_case_span_equal(text + begin, end - begin,
	    "inherit")) {
		*result = ST_THEME_INHERIT;
		return true;
	}
	if (begin < end && text[begin] == '+')
		++begin;
	if (end - begin >= 2 && text[begin] == '0'
	    && (text[begin + 1] == 'x' || text[begin + 1] == 'X')) {
		base = 16;
		begin += 2;
	}
	else if (end - begin > 1 && text[begin] == '0') {
		base = 8;
		++begin;
	}
	for (size_t i = begin; i < end; ++i) {
		unsigned digit;
		unsigned char character = (unsigned char)text[i];

		if (character >= '0' && character <= '9')
			digit = character - '0';
		else if (character >= 'a' && character <= 'f')
			digit = character - 'a' + 10;
		else if (character >= 'A' && character <= 'F')
			digit = character - 'A' + 10;
		else
			return false;
		if (digit >= base || value > ((unsigned long)maximum - digit) / base)
			return false;
		value = value * base + digit;
		any = true;
	}
	if (!any || value < (unsigned long)minimum
	    || value > (unsigned long)maximum)
		return false;
	*result = (int)value;
	return true;
}

static bool
metadata_destination(const char *key, struct st_theme *theme,
	char ***destination, unsigned *bit)
{
	static const char *const names[] = {
		"Name", "Author", "Description", "Version",
	};
	char **destinations[] = {
		&theme->name, &theme->author, &theme->description, &theme->version,
	};

	for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		if (ascii_case_compare(key, names[i]) == 0) {
			*destination = destinations[i];
			*bit = 1u << i;
			return true;
		}
	}
	return false;
}

static bool
parse_metadata(ini_document_t *document, struct st_theme *theme, char *error,
	size_t error_size)
{
	ini_section_t *root = ini_document_root(document);
	unsigned seen = 0;

	for (size_t i = 0; i < ini_section_count(root); ++i) {
		const struct name_value *pair = ini_section_at(root, i);
		char **destination;
		unsigned bit;

		if (!metadata_destination(pair->name, theme, &destination, &bit))
			continue;
		if ((seen & bit) != 0) {
			set_error(error, error_size, "duplicate metadata key: %s",
			    pair->name);
			return false;
		}
		seen |= bit;
		if (!replace_string(destination, pair->value)) {
			set_error(error, error_size, "out of memory");
			return false;
		}
	}
	if ((seen & 1u) == 0 || theme->name == NULL || theme->name[0] == '\0') {
		set_error(error, error_size, "missing required Name metadata");
		return false;
	}
	return true;
}

static bool
parse_style(ini_section_t *section, struct st_theme *theme, char *error,
	size_t error_size)
{
	const char *section_name = ini_section_name(section);
	const char *role = section_name + 6;
	struct st_theme_style *style;
	unsigned seen = 0;

	if (*role == '\0') {
		set_error(error, error_size, "empty Style role");
		return false;
	}
	style = find_style(theme, role);
	if (style == NULL)
		style = add_style(theme, role);
	if (style == NULL) {
		set_error(error, error_size, "out of memory");
		return false;
	}
	for (size_t i = 0; i < ini_section_count(section); ++i) {
		const struct name_value *pair = ini_section_at(section, i);
		int *destination = NULL;
		long maximum = 0;
		unsigned bit = 0;

		if (ascii_case_compare(pair->name, "Font") == 0) {
			destination = &style->font;
			maximum = 255;
			bit = 1u;
		}
		else if (ascii_case_compare(pair->name, "LegacyAttr") == 0) {
			destination = &style->legacy_attr;
			maximum = 255;
			bit = 2u;
		}
		else if (ascii_case_compare(pair->name, "Foreground") == 0) {
			destination = &style->foreground;
			maximum = 0xffffff;
			bit = 4u;
		}
		else if (ascii_case_compare(pair->name, "Background") == 0) {
			destination = &style->background;
			maximum = 0xffffff;
			bit = 8u;
		}
		if (destination == NULL)
			continue;
		if ((seen & bit) != 0) {
			set_error(error, error_size, "duplicate %s in [%s]",
			    pair->name, section_name);
			return false;
		}
		seen |= bit;
		if (!parse_number(pair->value, 0, maximum, true, destination)) {
			set_error(error, error_size, "invalid %s in [%s]",
			    pair->name, section_name);
			return false;
		}
	}
	return true;
}

static bool
parse_glyph(ini_section_t *section, struct st_theme *theme, char *error,
	size_t error_size)
{
	const char *section_name = ini_section_name(section);
	const char *name = section_name + 6;
	struct st_theme_glyph *glyph;
	bool is_new;
	unsigned seen = 0;

	if (*name == '\0') {
		set_error(error, error_size, "empty Glyph name");
		return false;
	}
	glyph = find_glyph(theme, name);
	is_new = glyph == NULL;
	if (glyph == NULL)
		glyph = add_glyph(theme, name);
	if (glyph == NULL) {
		set_error(error, error_size, "out of memory");
		return false;
	}
	for (size_t i = 0; i < ini_section_count(section); ++i) {
		const struct name_value *pair = ini_section_at(section, i);
		long minimum = 0;
		long maximum = 255;
		unsigned bit = 0;
		unsigned field;
		int parsed;

		if (ascii_case_compare(pair->name, "CP437") == 0) {
			field = ST_THEME_GLYPH_CP437;
		}
		else if (ascii_case_compare(pair->name, "ASCII") == 0) {
			field = ST_THEME_GLYPH_ASCII;
			minimum = 0x20;
			maximum = 0x7e;
		}
		else if (ascii_case_compare(pair->name, "Codepoint") == 0) {
			field = ST_THEME_GLYPH_CODEPOINT;
			maximum = 0x10ffff;
		}
		else
			continue;
		bit = 1u << field;
		if ((seen & bit) != 0) {
			set_error(error, error_size, "duplicate %s in [%s]",
			    pair->name, section_name);
			return false;
		}
		seen |= bit;
		if (!parse_number(pair->value, minimum, maximum, false, &parsed)) {
			set_error(error, error_size, "invalid %s in [%s]",
			    pair->name, section_name);
			return false;
		}
		if (field == ST_THEME_GLYPH_CODEPOINT) {
			if (!st_unicode_scalar_valid((uint32_t)parsed)) {
				set_error(error, error_size, "invalid %s in [%s]",
				    pair->name, section_name);
				return false;
			}
			glyph->codepoint = (uint32_t)parsed;
		}
		else if (field == ST_THEME_GLYPH_CP437)
			glyph->cp437 = (uint8_t)parsed;
		else
			glyph->ascii = (uint8_t)parsed;
		glyph->present |= bit;
	}
	if (is_new && (seen & (1u << ST_THEME_GLYPH_ASCII)) == 0) {
		set_error(error, error_size,
		    "new glyph [%s] requires ASCII", section_name);
		return false;
	}
	if (!resolve_glyph(glyph)) {
		set_error(error, error_size, "glyph [%s] has no ASCII fallback",
		    section_name);
		return false;
	}
	return true;
}

static bool
default_complete(const struct st_theme *theme)
{
	for (size_t i = 0; i < theme->style_count; ++i) {
		const struct st_theme_style *style = &theme->styles[i];

		if (strcmp(style->role, "default") == 0)
			return style->font >= 0 && style->legacy_attr >= 0
			    && style->foreground >= 0 && style->background >= 0;
	}
	return false;
}

int
load_path(const struct st_theme_manager *manager, const char *path,
	const char *filename, struct st_theme **result, char *error,
	size_t error_size)
{
	ini_document_t *document = NULL;
	struct st_theme *theme = NULL;
	FILE *stream = NULL;
	int status;

	(void)manager;
	status = fs_open(&stream, path, "rb");
	if (status != 0) {
		set_error(error, error_size, "unable to open theme file");
		return status;
	}
	status = ini_document_read(&document, stream);
	if (fclose(stream) != 0 && status == 0)
		status = IO_ERROR_IO;
	if (status != 0) {
		set_error(error, error_size, "unable to read theme file");
		return status;
	}
	theme = theme_clone(manager->default_theme);
	if (theme == NULL) {
		status = IO_ERROR_OUT_OF_MEMORY;
		goto failed;
	}
	if (!replace_string(&theme->filename, filename)
	    || !replace_string(&theme->name, NULL)
	    || !replace_string(&theme->author, NULL)
	    || !replace_string(&theme->description, NULL)
	    || !replace_string(&theme->version, NULL)) {
		status = IO_ERROR_OUT_OF_MEMORY;
		goto failed;
	}
	if (!parse_metadata(document, theme, error, error_size)) {
		status = IO_ERROR_INVALID_FORMAT;
		goto failed;
	}
	for (size_t i = 0; i < ini_document_section_count(document); ++i) {
		ini_section_t *section = ini_document_section_at(document, i);
		const char *name = ini_section_name(section);
		bool style = ascii_case_starts_with(name, "Style:");
		bool glyph = ascii_case_starts_with(name, "Glyph:");

		if (!style && !glyph)
			continue;
		for (size_t prior = 0; prior < i; ++prior) {
			const char *prior_name = ini_section_name(
			    ini_document_section_at(document, prior));
			if (ascii_case_compare(name, prior_name) == 0) {
				set_error(error, error_size, "duplicate section [%s]", name);
				status = IO_ERROR_INVALID_FORMAT;
				goto failed;
			}
		}
		if (!(style ? parse_style(section, theme, error, error_size)
		    : parse_glyph(section, theme, error, error_size))) {
			status = IO_ERROR_INVALID_FORMAT;
			goto failed;
		}
	}
	if (!default_complete(theme)) {
		set_error(error, error_size, "the default style must be complete");
		status = IO_ERROR_INVALID_FORMAT;
		goto failed;
	}
	ini_document_free(&document);
	*result = theme;
	return 0;

failed:
	if (status == IO_ERROR_OUT_OF_MEMORY)
		set_error(error, error_size, "out of memory");
	ini_document_free(&document);
	theme_free(theme);
	return status;
}

bool
st_theme_valid_filename(const char *filename)
{
	const char *extension;

	if (!path_untrusted_component_valid(filename))
		return false;
	extension = strrchr(filename, '.');
	return extension != NULL && ascii_case_compare(extension, ".ini") == 0;
}

int
load_file(const struct st_theme_manager *manager, const char *filename,
	struct st_theme **result, char *error, size_t error_size)
{
	char *path = NULL;
	int status;

	if (!st_theme_valid_filename(filename)) {
		set_error(error, error_size, "invalid theme filename");
		return IO_ERROR_INVALID_ARGUMENT;
	}
	status = path_join_alloc(&path, manager->directory, filename);
	if (status == 0)
		status = load_path(manager, path, filename, result, error, error_size);
	free(path);
	return status;
}

void
install_theme(struct st_theme_manager *manager, struct st_theme *theme)
{
	theme_free(manager->committed_theme);
	manager->committed_theme = theme;
	theme_free(manager->preview_theme);
	manager->preview_theme = NULL;
	++manager->generation;
}

int
st_theme_manager_create(struct st_theme_manager **result,
	const struct st_app_paths *paths, const struct st_settings *settings,
	char *error, size_t error_size)
{
	struct st_theme_manager *manager;
	struct st_theme *selected = NULL;

	if (result == NULL || paths == NULL || paths->themes_directory == NULL
	    || settings == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	manager = calloc(1, sizeof(*manager));
	if (manager == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	st_settings_init(&manager->classic_settings);
	manager->directory = strdup(paths->themes_directory);
	manager->system_cache_directory = paths->system_cache_directory == NULL
	    ? NULL : strdup(paths->system_cache_directory);
	manager->default_theme = build_default_theme();
	if (manager->directory == NULL || (paths->system_cache_directory != NULL
	    && manager->system_cache_directory == NULL)
	    || manager->default_theme == NULL
	    || !st_settings_copy(&manager->classic_settings, settings)) {
		st_theme_manager_free(&manager);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	manager->committed_theme = build_classic_theme(manager->default_theme,
	    settings);
	if (manager->committed_theme == NULL) {
		st_theme_manager_free(&manager);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	manager->generation = 1;
	if (settings->theme_package != NULL && settings->theme_package[0] != '\0') {
		if (st_theme_cloud_load_package(manager, settings->theme_package,
		    &selected, error, error_size) == 0)
			install_theme(manager, selected);
	}
	else if (settings->theme_file != NULL && settings->theme_file[0] != '\0') {
		if (load_file(manager, settings->theme_file, &selected, error,
		    error_size) == 0)
			install_theme(manager, selected);
	}
	*result = manager;
	return 0;
}

static void
free_catalog(struct st_theme_manager *manager)
{
	for (size_t i = 0; i < manager->catalog_count; ++i) {
		free(manager->catalog[i].filename);
		free(manager->catalog[i].name);
		free(manager->catalog[i].author);
		free(manager->catalog[i].description);
		free(manager->catalog[i].version);
		free(manager->catalog[i].error);
	}
	free(manager->catalog);
	manager->catalog = NULL;
	manager->catalog_count = 0;
}

void
st_theme_manager_free(struct st_theme_manager **manager_ptr)
{
	struct st_theme_manager *manager;

	if (manager_ptr == NULL || *manager_ptr == NULL)
		return;
	manager = *manager_ptr;
	free_catalog(manager);
	st_theme_cloud_dispose(manager);
	theme_free(manager->preview_theme);
	theme_free(manager->committed_theme);
	theme_free(manager->default_theme);
	st_settings_dispose(&manager->classic_settings);
	free(manager->directory);
	free(manager->system_cache_directory);
	free(manager);
	*manager_ptr = NULL;
}

const struct st_theme *
st_theme_active(const struct st_theme_manager *manager)
{
	if (manager == NULL)
		return NULL;
	return manager->preview_theme != NULL ? manager->preview_theme
	    : manager->committed_theme;
}

const struct st_theme *
st_theme_default(const struct st_theme_manager *manager)
{
	return manager == NULL ? NULL : manager->default_theme;
}

uint64_t
st_theme_generation(const struct st_theme_manager *manager)
{
	return manager == NULL ? 0 : manager->generation;
}

int
st_theme_apply_settings(struct st_theme_manager *manager,
	const struct st_settings *settings, char *error, size_t error_size)
{
	struct st_theme *theme = NULL;
	int status;

	if (manager == NULL || settings == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (settings->theme_package != NULL && settings->theme_package[0] != '\0') {
		status = st_theme_cloud_load_package(manager, settings->theme_package,
		    &theme, error, error_size);
	}
	else if (settings->theme_file == NULL || settings->theme_file[0] == '\0') {
		theme = build_classic_theme(manager->default_theme, settings);
		status = theme == NULL ? IO_ERROR_OUT_OF_MEMORY : 0;
	}
	else
		status = load_file(manager, settings->theme_file, &theme, error,
		    error_size);
	if (status != 0)
		return status;
	if (!st_settings_copy(&manager->classic_settings, settings)) {
		theme_free(theme);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	install_theme(manager, theme);
	return 0;
}

static int
append_catalog(struct st_theme_manager *manager, const char *filename,
	const struct st_theme *theme, const char *load_error)
{
	if (manager->catalog_count == SIZE_MAX / sizeof(manager->catalog[0]))
		return IO_ERROR_OUT_OF_MEMORY;
	struct st_theme_catalog_entry *catalog = realloc(manager->catalog,
	    (manager->catalog_count + 1) * sizeof(manager->catalog[0]));
	if (catalog == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	manager->catalog = catalog;
	struct st_theme_catalog_entry *entry = &catalog[manager->catalog_count];
	*entry = (struct st_theme_catalog_entry){
		.filename = strdup(filename),
		.name = strdup(theme != NULL && theme->name != NULL ? theme->name
		    : filename),
		.author = theme != NULL && theme->author != NULL
		    ? strdup(theme->author) : NULL,
		.description = theme != NULL && theme->description != NULL
		    ? strdup(theme->description) : NULL,
		.version = theme != NULL && theme->version != NULL
		    ? strdup(theme->version) : NULL,
		.error = load_error != NULL ? strdup(load_error) : NULL,
	};
	if (entry->filename == NULL || entry->name == NULL
	    || (theme != NULL && theme->author != NULL && entry->author == NULL)
	    || (theme != NULL && theme->description != NULL
	    && entry->description == NULL)
	    || (theme != NULL && theme->version != NULL && entry->version == NULL)
	    || (load_error != NULL && entry->error == NULL)) {
		free(entry->filename);
		free(entry->name);
		free(entry->author);
		free(entry->description);
		free(entry->version);
		free(entry->error);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	++manager->catalog_count;
	return 0;
}

static int
catalog_compare(const void *left_ptr, const void *right_ptr)
{
	const struct st_theme_catalog_entry *left = left_ptr;
	const struct st_theme_catalog_entry *right = right_ptr;

	if (left->filename[0] == '\0')
		return right->filename[0] == '\0' ? 0 : -1;
	if (right->filename[0] == '\0')
		return 1;
	int compared = ascii_case_compare(left->name, right->name);
	return compared != 0 ? compared : ascii_case_compare(left->filename,
	    right->filename);
}

int
st_theme_catalog_refresh(struct st_theme_manager *manager, char *error,
	size_t error_size)
{
	struct st_theme_directory directory = {0};
	struct st_theme *classic;
	int status;

	if (manager == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	free_catalog(manager);
	classic = build_classic_theme(manager->default_theme,
	    &manager->classic_settings);
	if (classic == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	status = append_catalog(manager, "", classic, NULL);
	theme_free(classic);
	if (status != 0)
		return status;
	status = st_theme_directory_read(&directory, manager->directory);
	if (status == IO_ERROR_NOT_FOUND)
		return 0;
	if (status != 0) {
		set_error(error, error_size, "unable to enumerate the themes directory");
		return status;
	}
	for (size_t i = 0; i < directory.count; ++i) {
		struct st_theme *theme = NULL;
		char load_error[256] = "";

		if (!st_theme_valid_filename(directory.entries[i].name))
			continue;
		status = load_file(manager, directory.entries[i].name, &theme,
		    load_error, sizeof(load_error));
		status = append_catalog(manager, directory.entries[i].name, theme,
		    status == 0 ? NULL : load_error);
		theme_free(theme);
		if (status != 0)
			break;
	}
	st_theme_directory_dispose(&directory);
	if (status == 0)
		qsort(manager->catalog, manager->catalog_count,
		    sizeof(manager->catalog[0]), catalog_compare);
	return status;
}

size_t
st_theme_catalog_count(const struct st_theme_manager *manager)
{
	return manager == NULL ? 0 : manager->catalog_count;
}

const struct st_theme_catalog_entry *
st_theme_catalog_at(const struct st_theme_manager *manager, size_t index)
{
	return manager == NULL || index >= manager->catalog_count ? NULL
	    : &manager->catalog[index];
}

int
st_theme_preview(struct st_theme_manager *manager, const char *filename,
	char *error, size_t error_size)
{
	struct st_theme *theme = NULL;
	int status;

	if (manager == NULL || filename == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (filename[0] == '\0') {
		theme = build_classic_theme(manager->default_theme,
		    &manager->classic_settings);
		status = theme == NULL ? IO_ERROR_OUT_OF_MEMORY : 0;
	}
	else
		status = load_file(manager, filename, &theme, error, error_size);
	if (status != 0)
		return status;
	theme_free(manager->preview_theme);
	manager->preview_theme = theme;
	++manager->generation;
	return 0;
}

void
st_theme_cancel_preview(struct st_theme_manager *manager)
{
	if (manager == NULL || manager->preview_theme == NULL)
		return;
	theme_free(manager->preview_theme);
	manager->preview_theme = NULL;
	++manager->generation;
}

int
st_theme_select(struct st_theme_manager *manager, const char *filename,
	char *error, size_t error_size)
{
	struct st_theme *theme = NULL;
	int status;

	if (manager == NULL || filename == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (filename[0] == '\0') {
		theme = build_classic_theme(manager->default_theme,
		    &manager->classic_settings);
		status = theme == NULL ? IO_ERROR_OUT_OF_MEMORY : 0;
	}
	else
		status = load_file(manager, filename, &theme, error, error_size);
	if (status == 0)
		install_theme(manager, theme);
	return status;
}

int
st_theme_delete(struct st_theme_manager *manager, const char *filename,
	char *error, size_t error_size)
{
	char *path = NULL;
	int status;

	if (manager == NULL || !st_theme_valid_filename(filename)) {
		set_error(error, error_size, "invalid theme filename");
		return IO_ERROR_INVALID_ARGUMENT;
	}
	status = path_join_alloc(&path, manager->directory, filename);
	if (status == 0)
		status = fs_remove_file(path);
	free(path);
	if (status != 0)
		set_error(error, error_size, "unable to delete theme file");
	return status;
}
