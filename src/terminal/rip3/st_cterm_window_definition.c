/* Metadata-only RIP3 window saves: no parser, cells, screen or history. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cterm.h"
#include "cterm_internal.h"
#include "io_error.h"
#include "st_cterm.h"
#include "st_rip3_native_window.h"
#include "string_compat.h"

struct st_cterm_window_definition {
	int                              width, height, xpos, ypos, save_xpos, save_ypos;
	bool                             ansi;
	uint32_t                         cell_width, cell_height;
	int                              top_margin, bottom_margin, left_margin, right_margin;
	int                              cursor, cursor_style, visible_cursor, fetm, ttm;
	unsigned char                    attr;
	bool                             negative, faint, default_tabs;
	uint32_t                         fg_color, bg_color, cell_flags, extattr, last_column_flag, video_flags;
	int32_t                          saved_mode, saved_mode_mask;
	uint8_t                          decsace, g0_designation, g1_designation;
	char                            *fg_tc_str, *bg_tc_str;
	int                             *tabs, *vtabs, tab_count, vtab_count, tab_width;
	enum st_codepage                 codepage;
	struct st_console_font_selection fonts;
	uint16_t                         font_indices[256], selectors[5];
	bool                             font_valid[256];
	enum st_codepage                 font_codepages[256];
	uint32_t                         mode_palette[16], faint_defaults[8];
	struct st_console_font_snapshot *resources[260];
	size_t                           count;
};

void
st_cterm_window_definition_destroy(struct st_cterm_window_definition *definition)
{
	if (definition == NULL)
		return;
	for (size_t i = 0; i < definition->count; i++)
		st_console_font_snapshot_destroy(definition->resources[i]);
	free(definition->tabs);
	free(definition->vtabs);
	free(definition->fg_tc_str);
	free(definition->bg_tc_str);
	free(definition);
}

static bool
valid_terminal(const st_cterm_t *terminal)
{
	return terminal != NULL && terminal->input_depth == 0 && terminal->started && terminal->width > 0
	       && terminal->height > 0 && terminal->emulation == CTERM_EMULATION_ANSI_BBS
	       && terminal->graphics_mode == ST_CTERM_GRAPHICS_RIP_3;
}

static int
capture_font(struct st_cterm_window_definition *definition, st_console_t *console, uint16_t identifier,
    uint16_t old[260], uint16_t *index)
{
	size_t at = 0;
	while (at < definition->count && old[at] != identifier)
		at++;
	if (at == definition->count) {
		if (at == 260)
			return IO_ERROR_OUT_OF_RANGE;
		int status = st_console_font_snapshot_create(&definition->resources[at], console, identifier);
		if (status != 0)
			return status;
		old[definition->count++] = identifier;
	}
	*index = (uint16_t)at;
	return 0;
}

static int
create_definition(struct st_cterm_window_definition **result, const st_cterm_t *source, bool ansi)
{
	if (result == NULL || !valid_terminal(source))
		return IO_ERROR_INVALID_ARGUMENT;
	if (source->tab_count < 0 || source->vtab_count < 0 || source->tab_width < 0
	    || (source->tab_count != 0 && source->tabs == NULL)
	    || (source->vtab_count != 0 && source->vtabs == NULL))
		return IO_ERROR_INVALID_ARGUMENT;
	if ((size_t)source->tab_count > SIZE_MAX / sizeof(int)
	    || (size_t)source->vtab_count > SIZE_MAX / sizeof(int))
		return IO_ERROR_OUT_OF_RANGE;
	struct st_cterm_window_definition *d = calloc(1, sizeof(*d));
	if (d == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	d->width               = source->width;
	d->height              = source->height;
	d->xpos                = source->xpos;
	d->ypos                = source->ypos;
	d->ansi                = ansi;
	d->save_xpos           = ansi ? source->xpos : source->save_xpos;
	d->save_ypos           = ansi ? source->ypos : source->save_ypos;
	d->top_margin          = source->top_margin;
	d->bottom_margin       = source->bottom_margin;
	d->left_margin         = source->left_margin;
	d->right_margin        = source->right_margin;
	d->cursor              = source->cursor;
	d->visible_cursor      = source->visible_cursor;
	d->cursor_style        = source->cursor_style;
	d->fetm                = source->fetm;
	d->ttm                 = source->ttm;
	d->decsace             = source->decsace;
	d->g0_designation      = source->g0_designation;
	d->g1_designation      = source->g1_designation;
	d->attr                = source->attr;
	d->negative            = source->negative;
	d->faint               = source->faint;
	d->fg_color            = source->fg_color;
	d->bg_color            = source->bg_color;
	d->cell_flags          = source->cell_flags;
	d->extattr             = source->extattr;
	d->last_column_flag    = source->last_column_flag;
	d->saved_mode          = source->saved_mode;
	d->saved_mode_mask     = source->saved_mode_mask;
	d->tab_count           = source->tab_count;
	d->vtab_count          = source->vtab_count;
	d->tab_width           = source->tab_width;
	d->default_tabs        = source->default_tabs;
	d->codepage            = source->screen.codepage;
	d->fonts               = source->screen.fonts;
	memcpy(d->font_valid, source->screen.font_valid, sizeof(d->font_valid));
	memcpy(d->font_codepages, source->screen.font_codepages, sizeof(d->font_codepages));
	memcpy(d->selectors, source->screen.selectors, sizeof(d->selectors));
	memcpy(d->mode_palette, source->screen.mode_palette, sizeof(d->mode_palette));
	memcpy(d->faint_defaults, source->screen.faint_defaults, sizeof(d->faint_defaults));
	if (d->tab_count != 0)
		d->tabs = malloc((size_t)d->tab_count * sizeof(int));
	if (d->vtab_count != 0)
		d->vtabs = malloc((size_t)d->vtab_count * sizeof(int));
	if (source->fg_tc_str != NULL)
		d->fg_tc_str = strdup(source->fg_tc_str);
	if (source->bg_tc_str != NULL)
		d->bg_tc_str = strdup(source->bg_tc_str);
	if ((d->tab_count != 0 && d->tabs == NULL) || (d->vtab_count != 0 && d->vtabs == NULL)
	    || (source->fg_tc_str != NULL && d->fg_tc_str == NULL)
	    || (source->bg_tc_str != NULL && d->bg_tc_str == NULL)) {
		st_cterm_window_definition_destroy(d);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	if (d->tab_count != 0)
		memcpy(d->tabs, source->tabs, (size_t)d->tab_count * sizeof(int));
	if (d->vtab_count != 0)
		memcpy(d->vtabs, source->vtabs, (size_t)d->vtab_count * sizeof(int));
	int status = st_console_cell_pixel_size(source->screen.console, &d->cell_width, &d->cell_height);
	if (ansi) {
		/* The logical selectors, not console registration IDs, survive. */
		d->fonts.normal = d->fonts.bright = d->fonts.blink = d->fonts.bright_blink = 0;
		memset(d->font_valid, 0, sizeof(d->font_valid));
		if (status != 0) {
			st_cterm_window_definition_destroy(d);
			return status;
		}
		*result = d;
		return 0;
	}
	if (status == 0)
		status = st_console_get_video_flags(source->screen.console, &d->video_flags);
	uint16_t old[260];
	for (unsigned i = 0; status == 0 && i < 256; i++)
		if (d->font_valid[i])
			status = capture_font(d, source->screen.console, source->screen.font_identifiers[i], old,
			    &d->font_indices[i]);
	uint16_t *selections[] = {&d->fonts.normal, &d->fonts.bright, &d->fonts.blink, &d->fonts.bright_blink};
	for (size_t i = 0; status == 0 && i < 4; i++)
		status = capture_font(d, source->screen.console, *selections[i], old, selections[i]);
	if (status != 0) {
		st_cterm_window_definition_destroy(d);
		return status;
	}
	*result = d;
	return 0;
}

int
st_cterm_window_definition_create(struct st_cterm_window_definition **result, const st_cterm_t *source)
{
	return create_definition(result, source, false);
}

int
st_cterm_window_ansi_create(struct st_cterm_window_definition **result, const st_cterm_t *source)
{
	return create_definition(result, source, true);
}

static int
restore_definition(st_cterm_t *destination, const struct st_cterm_window_definition *d,
    const st_cterm_t *resources)
{
	if (!valid_terminal(destination) || d == NULL || destination->width != d->width
	    || destination->height != d->height)
		return IO_ERROR_INVALID_ARGUMENT;
	/* Restore targets are unpublished fresh owners. Discard them on failure;
	 * fonts already registered into a failed target never leak into a live one. */
	int  *tabs = NULL, *vtabs = NULL;
	char *fg = NULL, *bg = NULL;
	if (d->tab_count != 0)
		tabs = malloc((size_t)d->tab_count * sizeof(int));
	if (d->vtab_count != 0)
		vtabs = malloc((size_t)d->vtab_count * sizeof(int));
	if (d->fg_tc_str != NULL)
		fg = strdup(d->fg_tc_str);
	if (d->bg_tc_str != NULL)
		bg = strdup(d->bg_tc_str);
	if ((d->tab_count != 0 && tabs == NULL) || (d->vtab_count != 0 && vtabs == NULL)
	    || (d->fg_tc_str != NULL && fg == NULL) || (d->bg_tc_str != NULL && bg == NULL)) {
		free(tabs);
		free(vtabs);
		free(fg);
		free(bg);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	uint16_t mapped[260];
	int      status     = 0;
	uint16_t logical[5] = {0};
	if (d->ansi) {
		uint32_t cw, ch;
		status = resources == NULL ? IO_ERROR_NOT_FOUND
		                           : st_console_cell_pixel_size(resources->screen.console, &cw, &ch);
		if (status == 0 && (cw != d->cell_width || ch != d->cell_height))
			status = IO_ERROR_NOT_SUPPORTED;
		for (size_t i = 0; status == 0 && i < 5; i++) {
			unsigned selector = d->selectors[i];
			if (selector > UINT8_MAX || !resources->screen.font_valid[selector]) {
				status = IO_ERROR_NOT_FOUND;
				break;
			}
			status = st_console_copy_font(destination->screen.console, resources->screen.console,
			    resources->screen.font_identifiers[selector], &logical[i]);
		}
	}
	for (size_t i = 0; status == 0 && i < d->count; i++)
		status =
		    st_console_font_snapshot_register(destination->screen.console, d->resources[i], &mapped[i]);
	if (status == 0 && !d->ansi)
		status = st_console_set_video_flags(destination->screen.console, d->video_flags);
	if (status != 0) {
		free(tabs);
		free(vtabs);
		free(fg);
		free(bg);
		return status;
	}
	if (d->tab_count != 0)
		memcpy(tabs, d->tabs, (size_t)d->tab_count * sizeof(int));
	if (d->vtab_count != 0)
		memcpy(vtabs, d->vtabs, (size_t)d->vtab_count * sizeof(int));
	free(destination->tabs);
	free(destination->vtabs);
	free(destination->fg_tc_str);
	free(destination->bg_tc_str);
	destination->tabs                = tabs;
	destination->vtabs               = vtabs;
	destination->fg_tc_str           = fg;
	destination->bg_tc_str           = bg;
	destination->tab_count           = d->tab_count;
	destination->vtab_count          = d->vtab_count;
	destination->tab_width           = d->tab_width;
	destination->default_tabs        = d->default_tabs;
	destination->save_xpos           = d->save_xpos;
	destination->save_ypos           = d->save_ypos;
	destination->top_margin          = d->top_margin;
	destination->bottom_margin       = d->bottom_margin;
	destination->left_margin         = d->left_margin;
	destination->right_margin        = d->right_margin;
	destination->cursor              = d->cursor;
	destination->visible_cursor      = d->visible_cursor;
	destination->cursor_style        = d->cursor_style;
	destination->fetm                = d->fetm;
	destination->ttm                 = d->ttm;
	destination->decsace             = d->decsace;
	destination->g0_designation      = d->g0_designation;
	destination->g1_designation      = d->g1_designation;
	destination->attr                = d->attr;
	destination->negative            = d->negative;
	destination->faint               = d->faint;
	destination->fg_color            = d->fg_color;
	destination->bg_color            = d->bg_color;
	destination->cell_flags          = d->cell_flags;
	destination->extattr             = d->extattr;
	if (d->ansi)
		cterm_coord_conv_xy(destination, CTERM_COORD_ABSTERM, CTERM_COORD_CURR, &destination->save_xpos,
		    &destination->save_ypos);
	destination->saved_mode      = d->saved_mode;
	destination->saved_mode_mask = d->saved_mode_mask;
	if (!d->ansi) {
		destination->screen.codepage           = d->codepage;
		destination->screen.fonts              = d->fonts;
		destination->screen.fonts.normal       = mapped[d->fonts.normal];
		destination->screen.fonts.bright       = mapped[d->fonts.bright];
		destination->screen.fonts.blink        = mapped[d->fonts.blink];
		destination->screen.fonts.bright_blink = mapped[d->fonts.bright_blink];
		memcpy(destination->screen.font_valid, d->font_valid, sizeof(d->font_valid));
		memcpy(destination->screen.font_codepages, d->font_codepages, sizeof(d->font_codepages));
		for (unsigned i = 0; i < 256; i++)
			if (d->font_valid[i])
				destination->screen.font_identifiers[i] = mapped[d->font_indices[i]];
		memcpy(destination->screen.selectors, d->selectors, sizeof(d->selectors));
		memcpy(destination->screen.mode_palette, d->mode_palette, sizeof(d->mode_palette));
		memcpy(destination->screen.faint_defaults, d->faint_defaults, sizeof(d->faint_defaults));
	}
	else {
		/* Resolve only the logical selections against live compatible resources.
		 * Unselected resources and connection encoding remain live. */
		for (size_t i = 0; i < 5; i++) {
			unsigned selector                              = d->selectors[i];
			destination->screen.font_valid[selector]       = true;
			destination->screen.font_identifiers[selector] = logical[i];
			destination->screen.font_codepages[selector] = resources->screen.font_codepages[selector];
		}
		memcpy(destination->screen.selectors, d->selectors, sizeof(d->selectors));
		destination->screen.fonts = (struct st_console_font_selection) {.normal = logical[1],
		    .bright                                                             = logical[2],
		    .blink                                                              = logical[3],
		    .bright_blink                                                       = logical[4],
		    .bright_selects_font = d->fonts.bright_selects_font,
		    .blink_selects_font  = d->fonts.blink_selects_font};
	}
	cterm_apply_window(destination);
	status = st_cterm_position_text(destination, (uint32_t)(d->xpos - 1), (uint32_t)(d->ypos - 1));
	destination->last_column_flag = d->last_column_flag;
	cterm_render_set_attribute(destination, destination->attr);
	cterm_render_set_colours(destination, destination->fg_color, destination->bg_color);
	cterm_render_set_cursor(destination, destination->cursor);
	return status != 0 ? status : cterm_render_take_error(destination);
}

int
st_cterm_window_definition_restore(st_cterm_t *destination, const struct st_cterm_window_definition *d)
{
	if (d == NULL || d->ansi)
		return IO_ERROR_INVALID_ARGUMENT;
	return restore_definition(destination, d, NULL);
}

int
st_cterm_window_definition_restore_maximized(st_cterm_t *destination, const struct st_cterm_window_definition *d)
{
	if (!valid_terminal(destination) || d == NULL || d->ansi)
		return IO_ERROR_INVALID_ARGUMENT;
	struct st_cterm_window_definition resized = *d;
	resized.width                             = destination->width;
	resized.height                            = destination->height;
	resized.top_margin                        = 1;
	resized.bottom_margin                     = resized.height;
	resized.left_margin                       = 1;
	resized.right_margin                      = resized.width;
	resized.xpos = resized.ypos = 1;
	resized.last_column_flag    = 0;
	return restore_definition(destination, &resized, NULL);
}

int
st_cterm_window_ansi_restore(st_cterm_t *destination, const struct st_cterm_window_definition *d,
    const st_cterm_t *resources)
{
	if (d == NULL || !d->ansi)
		return IO_ERROR_INVALID_ARGUMENT;
	return restore_definition(destination, d, resources);
}
