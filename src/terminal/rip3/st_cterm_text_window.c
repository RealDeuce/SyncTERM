/* Preserve the ANSI interpreter across a RIP3 window's fresh rendering state. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "cterm.h"
#include "cterm_internal.h"
#include "io_error.h"
#include "st_cterm.h"
#include "st_rip3_native_window.h"
#include "string_compat.h"

static int
inherit_interpreter(st_cterm_t *destination, const st_cterm_t *source, bool preserve_tabs, bool fresh)
{
	if (destination == NULL || source == NULL || destination == source || destination->input_depth != 0
	    || source->input_depth != 0 || destination->width < 1 || destination->height < 1
	    || !destination->started || (fresh && (destination->accumulator != NULL || destination->string != 0)))
		return IO_ERROR_INVALID_ARGUMENT;
	if (destination->emulation != CTERM_EMULATION_ANSI_BBS || source->emulation != CTERM_EMULATION_ANSI_BBS
	    || destination->graphics_mode != ST_CTERM_GRAPHICS_RIP_3
	    || source->graphics_mode != ST_CTERM_GRAPHICS_RIP_3)
		return IO_ERROR_NOT_SUPPORTED;
	/* These optional media/download payloads carry resources beyond ordinary
	 * ANSI syntax. Retain their existing owner rather than copying half a
	 * payload and destroying its backing state during a definition. */
	if (source->sixel != SIXEL_INACTIVE || source->macro != MACRO_INACTIVE || source->music != 0
	    || source->font_size != 0 || source->seq_trailing_remain != 0)
		return IO_ERROR_NOT_SUPPORTED;
	if (source->tab_count < 0 || source->vtab_count < 0 || source->tab_width < 0
	    || (source->tab_count != 0 && source->tabs == NULL)
	    || (source->vtab_count != 0 && source->vtabs == NULL)
	    || (source->strbuflen != 0 && source->strbuf == NULL) || source->strbuflen == SIZE_MAX
	    || (source->strbuf != NULL && source->strbuflen >= source->strbufsize))
		return IO_ERROR_INVALID_ARGUMENT;
	int    first    = source->tab_width == 0 ? 0 : (source->tab_width - 1) / 8 + 1;
	int    extra    = preserve_tabs && destination->width > source->tab_width && source->default_tabs
	                      ? (destination->width - 1) / 8 + 1 - first
	                      : 0;
	size_t count    = preserve_tabs ? (size_t)source->tab_count + (size_t)extra : 0;
	size_t vertical = preserve_tabs ? (size_t)source->vtab_count : 0;
	if (count > INT_MAX || count > SIZE_MAX / sizeof(int)
	    || (size_t)source->vtab_count > SIZE_MAX / sizeof(int))
		return IO_ERROR_OUT_OF_RANGE;
	int  *tabs = NULL, *vtabs = NULL;
	char *string = NULL;
	if (count != 0)
		tabs = malloc(count * sizeof(*tabs));
	if (vertical != 0)
		vtabs = malloc(vertical * sizeof(*vtabs));
	if (source->strbuf != NULL)
		string = malloc(source->strbuflen + 1);
	if ((count != 0 && tabs == NULL) || (vertical != 0 && vtabs == NULL)
	    || (source->strbuf != NULL && string == NULL)) {
		free(tabs);
		free(vtabs);
		free(string);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	if (preserve_tabs && source->tab_count != 0)
		memcpy(tabs, source->tabs, (size_t)source->tab_count * sizeof(*tabs));
	for (int i = 0; i < extra; i++)
		tabs[source->tab_count + i] = 1 + 8 * (first + i);
	if (vertical != 0)
		memcpy(vtabs, source->vtabs, vertical * sizeof(*vtabs));
	if (string != NULL) {
		memcpy(string, source->strbuf, source->strbuflen);
		string[source->strbuflen] = 0;
	}
	/* No destination mutation or received-input callback precedes successful
	 * allocation of every owned buffer. The source remains independently usable. */
	if (preserve_tabs) {
		free(destination->tabs);
		free(destination->vtabs);
		destination->tabs      = tabs;
		destination->tab_count = (int)count;
		destination->tab_width =
		    source->tab_width > destination->width ? source->tab_width : destination->width;
		destination->default_tabs = source->default_tabs;
		destination->vtabs        = vtabs;
		destination->vtab_count   = source->vtab_count;
	}
	free(destination->strbuf);
	destination->strbuf     = string;
	destination->strbuflen  = source->strbuflen;
	destination->strbufsize = string == NULL ? 0 : source->strbuflen + 1;
	destination->sequence   = source->sequence;
	destination->string     = source->string;
	destination->escbufsz   = source->escbufsz;
	memcpy(destination->escbuf, source->escbuf, sizeof(destination->escbuf));
	destination->seq_key   = source->seq_key;
	destination->seq_phase = source->seq_phase;
	memcpy(destination->seq_params, source->seq_params, sizeof(destination->seq_params));
	memcpy(destination->seq_param_int, source->seq_param_int, sizeof(destination->seq_param_int));
	destination->seq_param_len      = source->seq_param_len;
	destination->seq_param_count    = source->seq_param_count;
	destination->seq_cur_value      = source->seq_cur_value;
	destination->seq_cur_has_digits = source->seq_cur_has_digits;
	destination->seq_cur_has_sub    = source->seq_cur_has_sub;
	destination->seq_overflow       = source->seq_overflow;
	memcpy(destination->seq_consumed, source->seq_consumed, sizeof(destination->seq_consumed));
	destination->seq_consumed_any = source->seq_consumed_any;
	/* Completed handlers build these aliases afresh from seq_params. Never
	 * retain a pointer into the source object's array across its destruction. */
	memset(destination->seq_param_strs, 0, sizeof(destination->seq_param_strs));
	destination->accumulator       = source->accumulator;
	destination->saved_accumulator = source->saved_accumulator;
	destination->wire_encoding     = source->wire_encoding;
	destination->screen.codepage   = source->screen.codepage;
	/* A fresh RIP3 definition's saved (0,0) is one-based (1,1) to CTerm. */
	if (fresh)
		destination->save_xpos = destination->save_ypos = 1;
	return 0;
}

int
st_cterm_inherit_window_interpreter(st_cterm_t *destination, const st_cterm_t *source, bool preserve_tabs)
{
	int status = inherit_interpreter(destination, source, preserve_tabs, true);
	if (status == 0 && preserve_tabs) {
		destination->g0_designation = source->g0_designation;
		destination->g1_designation = source->g1_designation;
	}
	return status;
}

int
st_cterm_select_window_interpreter(st_cterm_t *destination, const st_cterm_t *source)
{
	/* ANSI syntax follows the connection into the selected window. Tabs and
	 * other per-window fields retain the destination's existing values. */
	return inherit_interpreter(destination, source, false, false);
}

int
st_cterm_default_window_interpreter(st_cterm_t *destination, const st_cterm_t *source)
{
	int status = inherit_interpreter(destination, source, false, true);
	if (status != 0)
		return status;
	/* CUR first use starts with no explicit tab stops, unlike redefinition's
	 * retained map or a terminal reset's regularly spaced default map. */
	free(destination->tabs);
	free(destination->vtabs);
	destination->tabs = destination->vtabs = NULL;
	destination->tab_count = destination->vtab_count = 0;
	destination->tab_width                           = destination->width;
	destination->default_tabs                        = false;
	return 0;
}

int
st_cterm_rip3_ansi_reset(st_cterm_t *terminal, bool active)
{
	if (terminal == NULL || terminal->input_depth != 0 || !terminal->started || terminal->width < 1)
		return IO_ERROR_INVALID_ARGUMENT;
	if (terminal->emulation != CTERM_EMULATION_ANSI_BBS || terminal->graphics_mode != ST_CTERM_GRAPHICS_RIP_3)
		return IO_ERROR_NOT_SUPPORTED;
	size_t count = (size_t)(terminal->width - 1) / 8 + 1;
	if (count > SIZE_MAX / sizeof(int))
		return IO_ERROR_OUT_OF_RANGE;
	int *tabs = malloc(count * sizeof(*tabs));
	if (tabs == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (size_t i = 0; i < count; i++)
		tabs[i] = (int)(1 + 8 * i);
	free(terminal->tabs);
	free(terminal->vtabs);
	terminal->tabs         = tabs;
	terminal->tab_count    = (int)count;
	terminal->tab_width    = terminal->width;
	terminal->default_tabs = true;
	terminal->vtabs        = NULL;
	terminal->vtab_count   = 0;
	free(terminal->fg_tc_str);
	free(terminal->bg_tc_str);
	terminal->fg_tc_str = terminal->bg_tc_str = NULL;
	terminal->attr                            = 7;
	terminal->negative                        = false;
	terminal->faint                           = false;
	terminal->colour_rendition = 0;
	terminal->cell_flags                      = 0;
	cterm_render_attribute_colours(terminal, 7, &terminal->fg_color, &terminal->bg_color);
	terminal->save_xpos = terminal->save_ypos = 1;
	terminal->top_margin                      = 1;
	terminal->bottom_margin                   = terminal->height;
	cterm_apply_window(terminal);
	cterm_render_set_attribute(terminal, terminal->attr);
	cterm_render_set_colours(terminal, terminal->fg_color, terminal->bg_color);
	int status = active ? st_cterm_position_text(terminal, 0, 0) : 0;
	return status != 0 ? status : cterm_render_take_error(terminal);
}

int
st_cterm_copy_window_state(st_cterm_t *destination, const st_cterm_t *source)
{
	if (destination == NULL || source == NULL || destination->width != source->width
	    || destination->height != source->height)
		return IO_ERROR_INVALID_ARGUMENT;
	int status = inherit_interpreter(destination, source, true, true);
	if (status != 0)
		return status;
	char *fg = source->fg_tc_str == NULL ? NULL : strdup(source->fg_tc_str);
	char *bg = source->bg_tc_str == NULL ? NULL : strdup(source->bg_tc_str);
	if ((source->fg_tc_str != NULL && fg == NULL) || (source->bg_tc_str != NULL && bg == NULL)) {
		free(fg);
		free(bg);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	uint32_t *palette = malloc((UINT16_MAX + 1u) * sizeof(*palette));
	if (palette == NULL) {
		free(fg);
		free(bg);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	status = st_console_get_palette(source->screen.console, 0, palette, UINT16_MAX + 1u);
	if (status == 0)
		status = st_console_set_palette(destination->screen.console, 0, palette, UINT16_MAX + 1u);
	free(palette);
	if (status != 0) {
		free(fg);
		free(bg);
		return status;
	}
	/* A fresh private target can be discarded on resource failure. Font IDs
	 * are console-local, so never copy them as unowned integer aliases. */
	uint16_t old[260], mapped[260];
	size_t   count = 0;
	memcpy(destination->screen.font_valid, source->screen.font_valid, sizeof(source->screen.font_valid));
	for (unsigned i = 0; i < 256; i++)
		if (source->screen.font_valid[i]) {
			uint16_t id = source->screen.font_identifiers[i];
			size_t   n  = 0;
			while (n < count && old[n] != id)
				n++;
			if (n == count) {
				status = st_console_copy_font(destination->screen.console, source->screen.console,
				    id, &mapped[n]);
				if (status != 0) {
					free(fg);
					free(bg);
					return status;
				}
				old[count++] = id;
			}
			destination->screen.font_identifiers[i] = mapped[n];
			destination->screen.font_valid[i]       = true;
			destination->screen.font_codepages[i]   = source->screen.font_codepages[i];
		}
	free(destination->fg_tc_str);
	free(destination->bg_tc_str);
	destination->fg_tc_str    = fg;
	destination->bg_tc_str    = bg;
	destination->screen.fonts = source->screen.fonts;
	uint16_t *selections[]    = {&destination->screen.fonts.normal, &destination->screen.fonts.bright,
	    &destination->screen.fonts.blink, &destination->screen.fonts.bright_blink};
	for (size_t i = 0; i < 4; i++) {
		size_t n = 0;
		while (n < count && old[n] != *selections[i])
			n++;
		if (n == count) {
			status = st_console_copy_font(destination->screen.console, source->screen.console,
			    *selections[i], &mapped[n]);
			if (status != 0)
				return status;
			old[count++] = *selections[i];
		}
		*selections[i] = mapped[n];
	}
	destination->g0_designation      = source->g0_designation;
	destination->g1_designation      = source->g1_designation;
	destination->attr                = source->attr;
	destination->fg_color            = source->fg_color;
	destination->bg_color            = source->bg_color;
	destination->cell_flags          = source->cell_flags;
	destination->negative            = source->negative;
	destination->faint               = source->faint;
	destination->colour_rendition    = source->colour_rendition;
	destination->extattr             = source->extattr;
	destination->last_column_flag    = source->last_column_flag;
	destination->saved_mode          = source->saved_mode;
	destination->saved_mode_mask     = source->saved_mode_mask;
	destination->save_xpos           = source->save_xpos;
	destination->save_ypos           = source->save_ypos;
	destination->top_margin          = source->top_margin;
	destination->bottom_margin       = source->bottom_margin;
	destination->left_margin         = source->left_margin;
	destination->right_margin        = source->right_margin;
	destination->cursor              = source->cursor;
	destination->visible_cursor      = source->visible_cursor;
	destination->cursor_style        = source->cursor_style;
	destination->decsace             = source->decsace;
	destination->fetm                = source->fetm;
	destination->ttm                 = source->ttm;
	memcpy(destination->screen.mode_palette, source->screen.mode_palette, sizeof(source->screen.mode_palette));
	memcpy(destination->screen.faint_defaults, source->screen.faint_defaults, sizeof(source->screen.faint_defaults));
	memcpy(destination->screen.selectors, source->screen.selectors, sizeof(source->screen.selectors));
	uint32_t video_flags;
	status = st_console_get_video_flags(source->screen.console, &video_flags);
	if (status == 0)
		status = st_console_set_video_flags(destination->screen.console, video_flags);
	if (status != 0)
		return status;
	cterm_apply_window(destination);
	status = st_cterm_position_text(destination, (uint32_t)(source->xpos - 1), (uint32_t)(source->ypos - 1));
	destination->last_column_flag = source->last_column_flag;
	cterm_render_set_attribute(destination, destination->attr);
	cterm_render_set_colours(destination, destination->fg_color, destination->bg_color);
	cterm_render_set_cursor(destination, destination->cursor);
	return status != 0 ? status : cterm_render_take_error(destination);
}
