/* One UTF-8 grapheme presenter and placement policy for every input encoding. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cterm.h"
#include "cterm_input.h"
#include "cterm_internal.h"
#include "cterm_prestel.h"
#include "cterm_text.h"
#include "io_error.h"
#include "st_utf8.h"

static void
log_missing(struct cterminal *cterm, const struct st_unicode_presentation_event *event)
{
	char   scalars[768];
	size_t offset = 0, used = 0;
	if (!cterm_log_enabled(cterm, ST_CTERM_LOG_DEBUG))
		return;
	while (offset < event->size && used + 16 < sizeof(scalars)) {
		uint32_t scalar;
		size_t   consumed;
		if (st_utf8_decode_one(&scalar, &consumed, event->bytes + offset, event->size - offset) != 0)
			break;
		int length = snprintf(scalars + used, sizeof(scalars) - used, "%sU+%04" PRIX32,
		    used == 0 ? "" : " ", scalar);
		if (length < 0)
			break;
		used += (size_t)length;
		offset += consumed;
	}
	scalars[used] = '\0';
	cterm_log(cterm, ST_CTERM_LOG_DEBUG,
	    "Missing glyph (.noglyph): font=%u column=%u row=%u; %s%s (%zu UTF-8 bytes)",
	    (unsigned)cterm->screen.missing_font, (unsigned)cterm->screen.provisional_x + 1,
	    (unsigned)cterm->screen.provisional_y + 1, scalars, offset < event->size ? " ..." : "", event->size);
}

static int
remember(struct cterminal *cterm, const uint8_t *bytes, size_t size)
{
	char *text = realloc(cterm->last_text, size);
	if (text == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	memcpy(text, bytes, size);
	cterm->last_text      = text;
	cterm->last_text_size = size;
	return 0;
}

static int
place(struct cterminal *cterm, const struct st_unicode_presentation_event *event, bool provisional,
    uint32_t glyph_flags)
{
	struct st_console_style style;
	struct st_console_cell  cells[2];
	struct st_console_rect  area    = {.height = 1};
	uint8_t                 columns = event->columns;
	uint8_t                 old_columns =
	    event->kind == ST_UNICODE_PRESENTATION_REVISE ? cterm->screen.provisional_columns : 0;
	int  x, y, right = TERM_MAXX;
	int  status;
	bool missing = false;

	if (columns == 0)
		return 0;
	if (!cterm->started)
		cterm_start(cterm);
	for (int slot = 0; slot < 4; ++slot)
		cterm_render_set_font_selector(cterm, cterm->altfont[slot], false, slot + 1);
	cterm_render_set_attribute(cterm, cterm->attr);
	cterm_render_set_colours(cterm, cterm->fg_color, cterm->bg_color);
	cterm_render_set_cursor(cterm, cterm->cursor);
	cterm_apply_window(cterm);
	if (old_columns != 0) {
		/* Scrolling tracks the glyph's screen position independently of the cursor. */
		cterm->xpos = (int)cterm->screen.provisional_x + 1;
		cterm->ypos = (int)cterm->screen.provisional_y + 1;
		cterm_coord_conv_xy(cterm, CTERM_COORD_SCREEN, CTERM_COORD_ABSTERM, &cterm->xpos, &cterm->ypos);
		cterm->last_column_flag           = cterm->screen.provisional_lcf;
		cterm->screen.provisional_columns = 0;
	}
	x = cterm->xpos;
	y = cterm->ypos;
	cterm_coord_conv_xy(cterm, CTERM_COORD_ABSTERM, CTERM_COORD_CURR, &x, &y);
	cterm_render_set_position(cterm, x, y);
	status = st_console_get_style(cterm->screen.console, &style);
	if (status != 0)
		return status;
	cells[0] = (struct st_console_cell) {
	    .foreground = style.foreground,
	    .background = style.background,
	    .flags      = style.flags | glyph_flags,
	    .font       = style.font,
	    .hyperlink  = style.hyperlink,
	};
	if (event->kind == ST_UNICODE_PRESENTATION_DECODE_ERROR) {
		cells[0].glyph_font = 0;
		status              = st_console_font_lookup_decode_error(cterm->screen.console, &cells[0].glyph);
	}
	else
		status = st_console_font_lookup_cluster_ex(cterm->screen.console, cells[0].font,
		    (const char *)event->bytes, event->size, columns, &cells[0].glyph_font, &cells[0].glyph,
		    &missing);
	if (status != 0)
		return status;
	status =
	    st_console_font_glyph_columns(cterm->screen.console, cells[0].glyph_font, cells[0].glyph, &columns);
	if (status != 0)
		return status;
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_CURR, &right, NULL);
	if (right > CURR_MAXX || x > right)
		right = CURR_MAXX;
	if (cterm->screen.addressing_window.width != 0 && x > right) {
		right = cterm->width;
		cterm_coord_conv_xy(cterm, CTERM_COORD_ABSTERM, CTERM_COORD_CURR, &right, NULL);
	}
	if ((cterm->last_column_flag & (CTERM_LCF_ENABLED | CTERM_LCF_SET)) == (CTERM_LCF_ENABLED | CTERM_LCF_SET)
	    && x == right) {
		cterm_advance_char(cterm, &x, &y, 1);
		cterm_clear_lcf(cterm);
	}
	if (columns > right - x + 1) {
		if (old_columns != 0) {
			/* The narrow prefix already overwrote the margin cell. */
			const struct st_console_rect vacated = {
			    cterm->screen.provisional_x,
			    cterm->screen.provisional_y,
			    1,
			    1,
			};
			struct st_console_cell blank;
			status = cterm_render_make_current_cell(cterm, &blank, ' ', style.hyperlink);
			if (status == 0)
				status = st_console_write_cells(cterm->screen.console, &vacated, &blank, 1);
			if (status != 0)
				return status;
		}
		if ((cterm->extattr & CTERM_EXTATTR_AUTOWRAP) == 0 || right < columns)
			return 0;
		x = right;
		cterm_render_set_position(cterm, x, y);
		/* Crossing the margin must wrap even when deferred wrap is enabled. */
		int lcf = cterm->last_column_flag;
		cterm->last_column_flag &= ~CTERM_LCF_ENABLED;
		cterm_advance_char(cterm, &x, &y, 1);
		cterm->last_column_flag = lcf & ~CTERM_LCF_SET;
	}
	cterm_render_set_position(cterm, x, y);
	status = st_console_get_position(cterm->screen.console, &area.x, &area.y);
	if (status != 0)
		return status;
	area.width = columns;
	if (columns == 2) {
		cells[1] = cells[0];
		cells[1].flags |= ST_CONSOLE_CELL_CONTINUATION;
	}
	else if (old_columns > columns) {
		/* Overwrite the old continuation, but advance only past the new glyph. */
		status = cterm_render_make_current_cell(cterm, &cells[1], ' ', style.hyperlink);
		if (status != 0)
			return status;
		area.width = old_columns;
	}
	status = st_console_write_cells(cterm->screen.console, &area, cells, area.width);
	if (status != 0)
		return status;
	cterm->screen.missing_glyph       = missing;
	cterm->screen.missing_font        = cells[0].font;
	cterm->screen.provisional_x       = area.x;
	cterm->screen.provisional_y       = area.y;
	cterm->screen.provisional_lcf     = cterm->last_column_flag;
	cterm->screen.provisional_columns = provisional ? columns : 0;
	if (missing && !provisional)
		log_missing(cterm, event);
	x = (int)area.x + 1;
	y = (int)area.y + 1;
	cterm_coord_conv_xy(cterm, CTERM_COORD_SCREEN, CTERM_COORD_CURR, &x, &y);
	for (uint8_t i = 0; i < columns; ++i)
		cterm_advance_char(cterm, &x, &y, 1);
	cterm_coord_get_xy(cterm, CTERM_COORD_ABSTERM, &cterm->xpos, &cterm->ypos);
	if (cterm->emulation == CTERM_EMULATION_PRESTEL || cterm->emulation == CTERM_EMULATION_BEEB)
		cterm_prestel_fix_line(cterm, x, y, true, false);
	return cterm_render_take_error(cterm);
}

static int
present(void *context, const struct st_unicode_presentation_event *event)
{
	struct cterminal *cterm = context;
	int               status;

	if (event->kind == ST_UNICODE_PRESENTATION_COMMIT) {
		if (cterm->log == CTERM_LOG_ASCII && cterm->logfile != NULL)
			fwrite(event->bytes, event->size, 1, cterm->logfile);
		/* Report the final cluster once, never a temporary incomplete glyph. */
		if (cterm->screen.provisional_columns != 0 && cterm->screen.missing_glyph)
			log_missing(cterm, event);
		cterm->screen.provisional_columns = 0;
		if (cterm->device.present != NULL) {
			bool handled;
			return cterm->device.present(cterm->device.context, event, &handled);
		}
		return 0;
	}
	if (event->kind == ST_UNICODE_PRESENTATION_BEGIN) {
		cterm->screen.provisional_columns   = 0;
		cterm->screen.provisional_discarded = false;
	}
	if (event->columns == 0)
		return 0;
	if (event->kind == ST_UNICODE_PRESENTATION_DECODE_ERROR) {
		cterm_log(cterm, ST_CTERM_LOG_WARNING, "Invalid UTF-8 input; displaying decode-error glyph");
		cterm_text_forget(cterm);
		status = cterm_input_decode_error(cterm);
		if (status != 0)
			return status;
	}
	else {
		status = remember(cterm, event->bytes, event->size);
		if (status != 0)
			return status;
	}
	if (cterm->device.present != NULL) {
		bool handled;
		status = cterm->device.present(cterm->device.context, event, &handled);
		if (status != 0 || handled)
			return status;
	}
	if (event->kind == ST_UNICODE_PRESENTATION_REVISE && cterm->screen.provisional_discarded)
		return 0;
	status = place(cterm, event, event->kind != ST_UNICODE_PRESENTATION_DECODE_ERROR, 0);
	if (status == 0 && cterm->device.present_cell != NULL)
		status = cterm->device.present_cell(cterm->device.context);
	return status;
}

int
cterm_text_create(struct cterminal *cterm)
{
	return cterm->screen.unicode != NULL
		   ? 0
		   : st_unicode_presentation_create(&cterm->screen.unicode, present, cterm);
}

int
cterm_text_boundary(struct cterminal *cterm)
{
	int status;
	++cterm->input_depth;
	status = cterm->screen.unicode == NULL ? 0 : st_unicode_presentation_boundary(cterm->screen.unicode);
	--cterm->input_depth;
	return status;
}

int
cterm_text_commit(struct cterminal *cterm)
{
	int status;
	++cterm->input_depth;
	status = cterm->screen.unicode == NULL ? 0 : st_unicode_presentation_commit(cterm->screen.unicode);
	--cterm->input_depth;
	return status;
}

int
cterm_text_feed(struct cterminal *cterm, const void *text, size_t size, size_t *consumed)
{
	int status = cterm_text_create(cterm);
	*consumed  = 0;
	return status != 0 ? status : st_unicode_presentation_feed(cterm->screen.unicode, text, size, consumed);
}

void
cterm_text_byte(struct cterminal *cterm, uint8_t byte)
{
	char      text[4];
	size_t    consumed;
	ptrdiff_t size =
	    st_utf8_encode_one(text, sizeof(text), st_codepage_to_unicode(cterm->screen.codepage, byte, true));
	cterm_render_record_error(cterm,
	    size < 0 ? IO_ERROR_INVALID_ENCODING : cterm_text_feed(cterm, text, (size_t)size, &consumed));
}

void
cterm_text_literal(struct cterminal *cterm, uint8_t byte)
{
	cterm_text_literal_scalar(cterm, st_codepage_to_unicode(cterm->screen.codepage, byte, true));
}

static void
place_literal(struct cterminal *cterm, uint32_t scalar, uint32_t glyph_flags, bool received)
{
	char                                 text[4];
	ptrdiff_t                            size  = st_utf8_encode_one(text, sizeof(text), scalar);
	struct st_unicode_presentation_event event = {
	    .kind    = ST_UNICODE_PRESENTATION_BEGIN,
	    .bytes   = (const uint8_t *)text,
	    .size    = size > 0 ? (size_t)size : 0,
	    .columns = 1,
	};
	int status = cterm_text_boundary(cterm);
	cterm_coord_get_xy(cterm, CTERM_COORD_ABSTERM, &cterm->xpos, &cterm->ypos);
	if (status == 0)
		status = place(cterm, &event, false, glyph_flags);
	if (status == 0 && received && cterm->log == CTERM_LOG_ASCII && cterm->logfile != NULL)
		fwrite(text, event.size, 1, cterm->logfile);
	cterm_render_record_error(cterm, status);
}

void
cterm_text_literal_scalar(struct cterminal *cterm, uint32_t scalar)
{
	place_literal(cterm, scalar, 0, false);
}

void
cterm_text_atascii_byte(struct cterminal *cterm, uint8_t byte)
{
	uint32_t flags = 0;
	if (cterm->screen.codepage == ST_CODEPAGE_ATASCII) {
		/* The bundled font is already in ATASCII order. The high bit
		 * reverses only this cell, including when ESC quoted the byte. */
		if (byte & 0x80)
			flags = ST_CONSOLE_CELL_REVERSE;
		byte &= 0x7f;
	}
	/* The byte parser has selected the session palette, including after reset. */
	cterm_render_attribute_colours(cterm, cterm->attr, &cterm->fg_color, &cterm->bg_color);
	place_literal(cterm, st_codepage_to_unicode(cterm->screen.codepage, byte, true), flags, true);
}

void
cterm_text_repeat(struct cterminal *cterm, size_t count)
{
	int    status = cterm_text_boundary(cterm);
	size_t size   = cterm->last_text_size;
	char  *text;
	if (status != 0 || size == 0 || count == 0) {
		cterm_render_record_error(cterm, status);
		return;
	}
	/* The callback updates last_text, so replay from independent storage. */
	text = malloc(size);
	if (text == NULL) {
		cterm_render_record_error(cterm, IO_ERROR_OUT_OF_MEMORY);
		return;
	}
	memcpy(text, cterm->last_text, size);
	for (size_t i = 0; i < count && status == 0; ++i) {
		size_t consumed;
		status = cterm_text_feed(cterm, text, size, &consumed);
		if (status == 0)
			status = cterm_text_boundary(cterm);
	}
	free(text);
	cterm_render_record_error(cterm, status);
}

void
cterm_text_forget(struct cterminal *cterm)
{
	free(cterm->last_text);
	cterm->last_text      = NULL;
	cterm->last_text_size = 0;
}

void
cterm_text_destroy(struct cterminal *cterm)
{
	st_unicode_presentation_destroy(&cterm->screen.unicode);
	cterm_text_forget(cterm);
}

void
cterm_text_scrolled(struct cterminal *cterm, int top, int bottom)
{
	if (cterm->screen.provisional_columns == 0 || cterm->screen.provisional_y < (uint32_t)(top - 1)
	    || cterm->screen.provisional_y >= (uint32_t)bottom)
		return;
	if (cterm->screen.provisional_y == (uint32_t)(top - 1)) {
		cterm->screen.provisional_columns   = 0;
		cterm->screen.provisional_discarded = true;
	}
	else
		--cterm->screen.provisional_y;
}
