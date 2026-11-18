/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_styles.h"
#include "st_rip3_backup.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

struct style_entry {
	struct st_rip3_style_info info;
	struct st_rip3_style value;
};

struct st_rip3_styles {
	unsigned current;
	struct style_entry entries[36];
};

static bool
valid(const struct st_rip3_style *value)
{
	struct st_rip3_classic_text_style font;

	return value->drawing_color <= 0xffffff && value->background_color <= 0xffffff
	    && value->fill_color <= 0xffffff && value->pen.pattern != 0 && value->line_type <= 4
	    && value->pen.thickness > 0 && value->pen.thickness <= INT32_MAX
	    && value->font_mode >= ST_RIP3_FONT_CLASSIC && value->font_mode <= ST_RIP3_FONT_OUTLINE
	    && value->write_mode >= ST_RIP3_COPY && value->write_mode <= ST_RIP3_NOT
	    && st_rip3_classic_text_style_set(&font, value->font.font,
	    value->font.direction, value->font.magnification, value->font.flags) == 0;
}

int
st_rip3_style_defaults(struct st_rip3_style *result)
{
	if (result == NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_style value = {.drawing_color=15,.fill_color=15,
	    .pen={.pattern=0xffff,.thickness=1},.font={0,0,1,0},
	    .write_mode=ST_RIP3_COPY,.borders=true};
	(void)st_rip3_fill_solid(&value.fill,true);
	*result = value;
	return 0;
}

int
st_rip3_styles_create(struct st_rip3_styles **result, const struct st_rip3_style *initial)
{
	struct st_rip3_styles *styles;

	if (result == NULL || initial == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!valid(initial))
		return IO_ERROR_OUT_OF_RANGE;
	styles = calloc(1, sizeof(*styles));
	if (styles == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	styles->entries[0].info.defined = true;
	styles->entries[0].value = *initial;
	*result = styles;
	return 0;
}

void
st_rip3_styles_destroy(struct st_rip3_styles *styles)
{
	free(styles);
}

int
st_rip3_styles_current(const struct st_rip3_styles *styles, unsigned *result)
{
	if (styles == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*result = styles->current;
	return 0;
}

int
st_rip3_styles_info(const struct st_rip3_styles *styles, unsigned number, struct st_rip3_style_info *result)
{
	if (styles == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	*result = styles->entries[number].info;
	return 0;
}

int
st_rip3_styles_get(const struct st_rip3_styles *styles, unsigned number, struct st_rip3_style *result)
{
	if (styles == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	if (!styles->entries[number].info.defined)
		return IO_ERROR_NOT_FOUND;
	*result = styles->entries[number].value;
	return 0;
}

int
st_rip3_styles_define(struct st_rip3_styles *styles, unsigned number, const struct st_rip3_style *value)
{
	if (styles == NULL || value == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36 || !valid(value))
		return IO_ERROR_OUT_OF_RANGE;
	if (styles->entries[number].info.protected)
		return IO_ERROR_PERMISSION_DENIED;
	styles->entries[number].value = *value;
	styles->entries[number].info.defined = true;
	return 0;
}

int
st_rip3_styles_set(struct st_rip3_styles *styles, unsigned number, const struct st_rip3_style *value)
{
	if (styles == NULL || value == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	if (!styles->entries[number].info.defined)
		return IO_ERROR_NOT_FOUND;
	return st_rip3_styles_define(styles, number, value);
}

int
st_rip3_styles_switch(struct st_rip3_styles *styles, unsigned number)
{
	if (styles == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	if (!styles->entries[number].info.defined) {
		(void)st_rip3_style_defaults(&styles->entries[number].value);
		styles->entries[number].info.defined = true;
	}
	styles->current = number;
	return 0;
}

int
st_rip3_styles_protect(struct st_rip3_styles *styles, unsigned number, bool protect)
{
	if (styles == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number == 0 || number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	if (styles->entries[number].info.defined)
		styles->entries[number].info.protected = protect;
	return 0;
}

static int
style_clone(const void *object, void **result)
{
	struct st_rip3_styles *snapshot;

	if (object == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	snapshot = malloc(sizeof(*snapshot));
	if (snapshot == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	*snapshot = *(const struct st_rip3_styles *)object;
	*result = snapshot;
	return 0;
}

static int
style_restore(void *object, const void *saved)
{
	if (object == NULL || saved == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_styles *styles = object;
	unsigned current = styles->current;
	*styles = *(const struct st_rip3_styles *)saved;
	return st_rip3_styles_switch(styles,current);
}

static void
style_destroy(void *object)
{
	free(object);
}

int
st_rip3_styles_backup_create(struct st_rip3_backup **result)
{
	static const struct st_rip3_backup_ops ops = {style_clone,style_restore,style_destroy};

	return st_rip3_backup_create(result, &ops);
}
