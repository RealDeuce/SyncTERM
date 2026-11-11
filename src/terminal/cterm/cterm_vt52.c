/****************************************************************************
 * @format.tab-size 4		(Plain Text/Source Code File Header)			*
 * @format.use-tabs true	(see http://www.synchro.net/ptsc_hdr.html)		*
 *																			*
 * Copyright (c) 2026 Stephen Hurd							*
 *																			*
 * SPDX-License-Identifier: BSD-2-Clause								*
 ****************************************************************************/

/*
 * cterm_vt52.c — DEC VT52 / Atari ST VT52 emulation.
 *
 * VT52 uses a simpler escape grammar than ECMA-48: ESC followed by a
 * single command byte, optionally followed by a fixed number of raw
 * trailing bytes.  Each command is a handler registered in
 * `cterm_st_vt52_dispatch[]`, keyed by the same 21-bit encoding used for
 * ANSI/ECMA-48: commands in the C1 Fe range (0x40-0x5F) use
 * SEQ_C1(ch); commands in the Fp/Fs ranges (0x30-0x3F, 0x60-0x7E)
 * use `SEQ_ESC | SEQ_FINAL(ch)`.  Trailing-byte commands (ESC Y, ESC
 * b, ESC c) set the entry's `trailing` field; the generic
 * cterm_accumulate_trailing accumulator in cterm.c collects the raw
 * bytes into seq_param_int[] before calling the handler.
 *
 * Handlers read their payload from cterm->seq_param_int[0..n-1] (for
 * ESC Y: row, col; for ESC b / c: color byte).
 *
 * Atari ST extensions (GEMDOS/TOS) layer on top of standard VT52 with
 * no conflicts — one shared table covers both.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cterm_render.h"
#include "cterm_igs.h"
#include "io_error.h"
#include "cterm.h"
#include "cterm_internal.h"
#include "cterm_vt52.h"
#include "cterm_ecma48.h"	/* cterm_accumulate_ecma_seq installed on ESC */

void
cterm_vt52_reset_console(struct cterminal *cterm)
{
	cterm->vt52_foreground = cterm->attr & 15u;
	cterm->vt52_background = cterm->attr >> 4;
	cterm->vt52_cursor_saved = false;
	cterm->vt52_cursor_hide_depth = 1;
	if (cterm->igs_engine != NULL)
		cterm->cursor = CTERM_CURSOR_HIDDEN;
}

uint32_t
cterm_vt52_background_color(const struct cterminal *cterm)
{
	/* Atari blanking uses the raw background even in reverse video. */
	return cterm->screen.mode_palette[cterm->vt52_background & 15u];
}

void
cterm_vt52_render_cursor(struct cterminal *cterm, int cursor)
{
	const struct st_console_custom_cursor custom = {
		.range = 1,
		.visible = cursor != CTERM_CURSOR_HIDDEN && cterm->vt52_cursor_hide_depth == 0,
		.blink = true,
		.index_base = CTERM_IGS_PALETTE_FIRST,
		.index_xor = (uint16_t)cterm_igs_plane_mask(cterm->igs_engine->resolution),
	};
	(void)cterm_render_set_custom_cursor(cterm, &custom);
}

/* ------------------------------------------------------------------------
 * VT52 command handlers
 * ------------------------------------------------------------------------ */

static void
cterm_vt52_alt_keypad_on(struct cterminal *cterm, int *speed)	/* ESC = */
{
	(void)speed;
	cterm->extattr |= CTERM_EXTATTR_ALTERNATE_KEYPAD;
}

static void
cterm_vt52_alt_keypad_off(struct cterminal *cterm, int *speed)	/* ESC > */
{
	(void)speed;
	cterm->extattr &= ~CTERM_EXTATTR_ALTERNATE_KEYPAD;
}

static void
cterm_vt52_cursor_up(struct cterminal *cterm, int *speed)	/* ESC A */
{
	(void)speed;
	cterm_adjust_currpos(cterm, 0, -1, 0);
}

static void
cterm_vt52_cursor_down(struct cterminal *cterm, int *speed)	/* ESC B */
{
	(void)speed;
	cterm_adjust_currpos(cterm, 0, 1, 0);
}

static void
cterm_vt52_cursor_right(struct cterminal *cterm, int *speed)	/* ESC C */
{
	(void)speed;
	cterm_adjust_currpos(cterm, 1, 0, 0);
}

static void
cterm_vt52_cursor_left(struct cterminal *cterm, int *speed)	/* ESC D */
{
	(void)speed;
	cterm_adjust_currpos(cterm, -1, 0, 0);
}

static void
cterm_vt52_noop(struct cterminal *cterm, int *speed)	/* ESC F, G, [, \ */
{
	(void)cterm;
	(void)speed;
	/* Graphics mode / hold-screen mode — not supported by Atari ST. */
}

static void
cterm_vt52_home(struct cterminal *cterm, int *speed)	/* ESC H */
{
	(void)cterm;
	(void)speed;
	cterm_render_set_position(cterm, CURR_MINX, CURR_MINY);
}

static void
cterm_vt52_scroll_rows(struct cterminal *cterm, int row, int next_x, bool down)
{
	int left = CURR_MINX, top = row, right = CURR_MAXX, bottom = CURR_MAXY;
	cterm_coord_conv_xy(cterm, CTERM_COORD_CURR, CTERM_COORD_SCREEN, &left, &top);
	cterm_coord_conv_xy(cterm, CTERM_COORD_CURR, CTERM_COORD_SCREEN, &right, &bottom);
	uint32_t background = cterm_vt52_background_color(cterm);
	cterm_vt52_render_cursor(cterm, CTERM_CURSOR_HIDDEN);
	bool moved;
	if (top == bottom) {
		int status = cterm_render_fill_blank(cterm, left, top, right, bottom, background);
		cterm_render_record_error(cterm, status);
		moved = status == 0;
	} else {
		struct st_console_cell blank;
		int status = cterm_render_make_current_cell(cterm, &blank, ' ', 0);
		cterm_render_record_error(cterm, status);
		blank.background = background;
		int source_top = down ? top : top + 1;
		int source_bottom = down ? bottom - 1 : bottom;
		int destination_top = down ? top + 1 : top;
		moved = status == 0 && cterm_render_move_cells_clear(cterm,
		    left, source_top, right, source_bottom, left, destination_top, &blank);
	}
	if (moved) {
		if (!down)
			cterm_text_scrolled(cterm, top, bottom);
		cterm_render_set_position(cterm, next_x, row);
	}
	cterm_vt52_render_cursor(cterm, cterm->cursor);
}

static void
cterm_vt52_reverse_lf(struct cterminal *cterm, int *speed)	/* ESC I */
{
	(void)speed;
	if (cterm->igs_engine != NULL) {
		int x, y;
		CURR_XY(&x, &y);
		if (y == CURR_MINY)
			cterm_vt52_scroll_rows(cterm, y, x, true);
		else
			cterm_render_set_position(cterm, x, y - 1);
		return;
	}
	cterm_adjust_currpos(cterm, 0, -1, 1);
}

enum vt52_erase_range {
	VT52_ERASE_WHOLE,
	VT52_ERASE_TO_START,
	VT52_ERASE_TO_END,
};

static int
cterm_vt52_blank(struct cterminal *cterm, int left, int top, int right, int bottom)
{
	cterm_coord_conv_xy(cterm, CTERM_COORD_CURR, CTERM_COORD_SCREEN, &left, &top);
	cterm_coord_conv_xy(cterm, CTERM_COORD_CURR, CTERM_COORD_SCREEN, &right, &bottom);
	return cterm_render_fill_blank(cterm, left, top, right, bottom,
	    cterm_vt52_background_color(cterm));
}

static void
cterm_vt52_erase_row(struct cterminal *cterm, enum vt52_erase_range range)
{
	int x, y;
	CURR_XY(&x, &y);
	int left = range == VT52_ERASE_TO_END ? x : CURR_MINX;
	int right = range == VT52_ERASE_TO_START ? x : CURR_MAXX;

	/* Hide and show the presenter overlay without editing the retained
	 * depth or the indexed pixels under the old cursor. */
	cterm_vt52_render_cursor(cterm, CTERM_CURSOR_HIDDEN);
	int status = cterm_vt52_blank(cterm, left, y, right, y);
	if (status == 0 && range == VT52_ERASE_WHOLE) {
		/* TOS moves with the depth word left by the erase wrapper's
		 * counted show. Bound the host column and keep depth saturated. */
		int column = CURR_MINX + cterm->vt52_cursor_hide_depth;
		cterm_render_set_position(cterm, column < CURR_MAXX ? column : CURR_MAXX, y);
	}
	cterm_render_record_error(cterm, status);
	cterm_vt52_render_cursor(cterm, cterm->cursor);
}

static void
cterm_vt52_erase_screen(struct cterminal *cterm, enum vt52_erase_range range)
{
	int x, y, status = 0;
	CURR_XY(&x, &y);
	cterm_vt52_render_cursor(cterm, CTERM_CURSOR_HIDDEN);
	switch (range) {
	case VT52_ERASE_WHOLE:
		cterm_page_to_scrollback(cterm);
		cterm_render_set_position(cterm, CURR_MINX, CURR_MINY);
		status = cterm_vt52_blank(cterm, CURR_MINX, CURR_MINY, CURR_MAXX, CURR_MAXY);
		break;
	case VT52_ERASE_TO_START:
		if (y > CURR_MINY)
			status = cterm_vt52_blank(cterm, CURR_MINX, CURR_MINY, CURR_MAXX, y - 1);
		if (status == 0)
			status = cterm_vt52_blank(cterm, CURR_MINX, y, x, y);
		break;
	case VT52_ERASE_TO_END:
		status = cterm_vt52_blank(cterm, x, y, CURR_MAXX, y);
		if (status == 0 && y < CURR_MAXY)
			status = cterm_vt52_blank(cterm, CURR_MINX, y + 1, CURR_MAXX, CURR_MAXY);
		break;
	}
	cterm_render_record_error(cterm, status);
	cterm_vt52_render_cursor(cterm, cterm->cursor);
}

static void
cterm_vt52_erase_to_end(struct cterminal *cterm, int *speed)	/* ESC J */
{
	int x, y, i;
	(void)speed;
	if (cterm->igs_engine != NULL) {
		cterm_vt52_erase_screen(cterm, VT52_ERASE_TO_END);
		return;
	}
	cterm_render_clear_line(cterm);
	CURR_XY(&x, &y);
	for (i = y + 1; i <= TERM_MAXY; i++) {
		cterm_set_position(cterm, TERM_MINY, i);
		cterm_render_clear_line(cterm);
	}
	cterm_render_set_position(cterm, x, y);
}

static void
cterm_vt52_erase_eol(struct cterminal *cterm, int *speed)	/* ESC K */
{
	(void)speed;
	if (cterm->igs_engine != NULL)
		cterm_vt52_erase_row(cterm, VT52_ERASE_TO_END);
	else
		cterm_render_clear_line(cterm);
}

static void
cterm_vt52_goto_xy(struct cterminal *cterm, int *speed)	/* ESC Y row col */
{
	int x, y;
	(void)speed;
	if (cterm->igs_engine != NULL) {
		/* Native parameter subtraction is an unsigned word operation. Host
		 * rows exclude a reserved status line, so clamp within that viewport. */
		unsigned row = (uint16_t)(cterm->seq_param_int[0]-32);
		unsigned col = (uint16_t)(cterm->seq_param_int[1]-32);
		if (row >= (unsigned)cterm->height) row = (unsigned)cterm->height-1;
		if (col >= (unsigned)cterm->width) col = (unsigned)cterm->width-1;
		cterm_render_set_position(cterm,CURR_MINX+(int)col,CURR_MINY+(int)row);
		return;
	}
	y = (int)cterm->seq_param_int[0] - 32;
	if (y < 0)
		return;
	if (y > 23)
		CURR_XY(NULL, &y);
	x = (int)cterm->seq_param_int[1] - 32;
	if (x < 0)
		return;
	if (x > 79)
		x = 79;
	cterm_render_set_position(cterm, CURR_MINX + x, CURR_MINY + y);
}

/* ---- GEMDOS / Atari ST TOS extensions --------------------------------- */

static void
cterm_vt52_clear_screen(struct cterminal *cterm, int *speed)	/* ESC E */
{
	(void)speed;
	if (cterm->igs_engine != NULL) {
		cterm_vt52_erase_screen(cterm, VT52_ERASE_WHOLE);
		return;
	}
	cterm_clearscreen(cterm, (char)cterm->attr);
	cterm_render_set_position(cterm, CURR_MINX, CURR_MINY);
}

static void
cterm_vt52_insert_line(struct cterminal *cterm, int *speed)	/* ESC L */
{
	int x, y, x2, y2, maxx, maxy;
	(void)speed;
	if (cterm->igs_engine != NULL) {
		CURR_XY(NULL, &y);
		cterm_vt52_scroll_rows(cterm, y, CURR_MINX, true);
		return;
	}

	TERM_XY(&x, &y);
	if (y < TERM_MINY || y > TERM_MAXY || x < TERM_MINX || x > TERM_MAXX)
		return;
	x2 = TERM_MINX;
	y2 = y;
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_SCREEN, &x2, &y2);
	maxx = TERM_MAXX;
	maxy = TERM_MAXY;
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_SCREEN, &maxx, &maxy);
	cterm_render_move_cells(cterm, x2, y2, maxx, maxy - 1, x2, y2 + 1);
	cterm_set_position(cterm, TERM_MINX, y);
	cterm_clear_line(cterm);
	cterm_set_position(cterm, x, y);
}

static void
cterm_vt52_delete_line(struct cterminal *cterm, int *speed)	/* ESC M */
{
	(void)speed;
	if (cterm->igs_engine == NULL) {
		cterm_dellines(cterm, 1);
		return;
	}

	int y;
	CURR_XY(NULL, &y);
	cterm_vt52_scroll_rows(cterm, y, CURR_MINX, false);
}

static void
cterm_vt52_do_attr(struct cterminal *cterm, char attr, bool bg)
{
	uint16_t raw = (uint16_t)((uint8_t)attr-32);
	if (bg) cterm->vt52_background = raw;
	else cterm->vt52_foreground = raw;
	if (cterm->negative)
		bg = !bg;
	if (bg) {
		cterm->attr = (attr & 0x0F) << 4 | (cterm->attr & 0x0F);
		cterm_render_attribute_colours(cterm, cterm->attr, NULL, &cterm->bg_color);
	}
	else {
		cterm->attr = (attr & 0x0F) | (cterm->attr & 0xF0);
		cterm_render_attribute_colours(cterm, cterm->attr, &cterm->fg_color, NULL);
	}
	cterm_render_set_attribute(cterm, cterm->attr);
	cterm_render_set_colours(cterm, cterm->fg_color, cterm->bg_color);
}

static void
cterm_vt52_set_fg(struct cterminal *cterm, int *speed)	/* ESC b <color> */
{
	(void)speed;
	cterm_vt52_do_attr(cterm, (char)cterm->seq_param_int[0], false);
}

static void
cterm_vt52_set_bg(struct cterminal *cterm, int *speed)	/* ESC c <color> */
{
	(void)speed;
	cterm_vt52_do_attr(cterm, (char)cterm->seq_param_int[0], true);
}

static void
cterm_vt52_clear_to_begin(struct cterminal *cterm, int *speed)	/* ESC d */
{
	int x, y, i;
	(void)speed;
	if (cterm->igs_engine != NULL) {
		cterm_vt52_erase_screen(cterm, VT52_ERASE_TO_START);
		return;
	}
	cterm_clear2bol(cterm);
	CURR_XY(&x, &y);
	for (i = y - 1; i >= TERM_MINY; i--) {
		cterm_set_position(cterm, TERM_MINX, i);
		cterm_render_clear_line(cterm);
	}
	cterm_render_set_position(cterm, x, y);
}

static void
cterm_vt52_cursor_on(struct cterminal *cterm, int *speed)	/* ESC e */
{
	(void)speed;
	if (cterm->igs_engine != NULL) {
		if (cterm->vt52_cursor_hide_depth == 0)
			return;
		cterm->vt52_cursor_hide_depth = 0;
		cterm->cursor = CTERM_CURSOR_BLOCK;
	} else
		cterm->cursor = CTERM_CURSOR_UNDERLINE;
	cterm_render_set_cursor(cterm, cterm->cursor);
}

static void
cterm_vt52_cursor_off(struct cterminal *cterm, int *speed)	/* ESC f */
{
	(void)speed;
	if (cterm->igs_engine != NULL && cterm->vt52_cursor_hide_depth < UINT16_MAX)
		++cterm->vt52_cursor_hide_depth;
	cterm->cursor = CTERM_CURSOR_HIDDEN;
	cterm_render_set_cursor(cterm, cterm->cursor);
}

static void
cterm_vt52_save_cursor(struct cterminal *cterm, int *speed)	/* ESC j */
{
	(void)speed;
	CURR_XY(&cterm->save_xpos, &cterm->save_ypos);
	if (cterm->igs_engine != NULL)
		cterm->vt52_cursor_saved = true;
}

static void
cterm_vt52_restore_cursor(struct cterminal *cterm, int *speed)	/* ESC k */
{
	if (cterm->igs_engine != NULL) {
		/* TOS tests and clears the flag before using the stored words. */
		bool saved = cterm->vt52_cursor_saved;
		cterm->vt52_cursor_saved = false;
		if (saved && cterm->save_xpos >= CURR_MINX && cterm->save_xpos <= CURR_MAXX
		    && cterm->save_ypos >= CURR_MINY && cterm->save_ypos <= CURR_MAXY)
			cterm_render_set_position(cterm, cterm->save_xpos, cterm->save_ypos);
		else
			cterm_vt52_home(cterm, speed);
		return;
	}
	if (cterm->save_ypos > 0 && cterm->save_ypos <= cterm->height
	    && cterm->save_xpos > 0 && cterm->save_xpos <= cterm->width) {
		if (cterm->save_ypos < CURR_MINY || cterm->save_ypos > CURR_MAXY
		    || cterm->save_xpos < CURR_MINX || cterm->save_xpos > CURR_MAXX)
			return;
		cterm_render_set_position(cterm, cterm->save_xpos, cterm->save_ypos);
	}
}

static void
cterm_vt52_clear_line(struct cterminal *cterm, int *speed)	/* ESC l */
{
	int x, y;
	(void)speed;
	if (cterm->igs_engine != NULL) {
		cterm_vt52_erase_row(cterm, VT52_ERASE_WHOLE);
		return;
	}
	CURR_XY(&x, &y);
	cterm_render_set_position(cterm, CURR_MINX, y);
	cterm_render_clear_line(cterm);
	cterm_render_set_position(cterm, x, y);
}

static void
cterm_vt52_clear_to_bol(struct cterminal *cterm, int *speed)	/* ESC o */
{
	(void)speed;
	if (cterm->igs_engine != NULL)
		cterm_vt52_erase_row(cterm, VT52_ERASE_TO_START);
	else
		cterm_clear2bol(cterm);
}

static void
cterm_vt52_reverse_on(struct cterminal *cterm, int *speed)	/* ESC p */
{
	(void)speed;
	cterm_set_negative(cterm, true);
	cterm_render_set_attribute(cterm, cterm->attr);
	cterm_render_set_colours(cterm, cterm->fg_color, cterm->bg_color);
}

static void
cterm_vt52_reverse_off(struct cterminal *cterm, int *speed)	/* ESC q */
{
	(void)speed;
	cterm_set_negative(cterm, false);
	cterm_render_set_attribute(cterm, cterm->attr);
	cterm_render_set_colours(cterm, cterm->fg_color, cterm->bg_color);
}

static void
cterm_vt52_autowrap_on(struct cterminal *cterm, int *speed)	/* ESC v */
{
	(void)speed;
	cterm->extattr |= CTERM_EXTATTR_AUTOWRAP;
}

static void
cterm_vt52_autowrap_off(struct cterminal *cterm, int *speed)	/* ESC w */
{
	(void)speed;
	cterm->extattr &= ~CTERM_EXTATTR_AUTOWRAP;
}

/* IGS uses the TOS single-byte escape grammar. Keep the existing standalone
 * VT52/ECMA compatibility path, and reuse its handlers/raw-parameter reader. */
static bool
accumulate_native(struct cterminal *cterm, unsigned char byte, int *speed)
{
	cterm->accumulator = NULL; cterm->sequence = 0; cterm->seq_phase = SEQ_PHASE_IDLE;
	if (byte < 0x30 || byte > 0x7e || byte == '=' || byte == '>') return true;
	uint32_t key = byte >= 0x40 && byte <= 0x5f ? SEQ_C1(byte) : SEQ_ESC | SEQ_FINAL(byte);
	const struct seq_dispatch *entry = bsearch(&key,cterm_st_vt52_dispatch,
	    cterm_st_vt52_dispatch_len,sizeof(*cterm_st_vt52_dispatch),cterm_seq_dispatch_cmp);
	if (entry == NULL) return true;
	if (entry->trailing != 0) {
		cterm->seq_param_count = 0; cterm->seq_trailing_remain = entry->trailing;
		cterm->seq_trailing_handler = entry->handler; cterm->accumulator = cterm_accumulate_trailing;
		cterm_text_forget(cterm);
	} else entry->handler(cterm,speed);
	return true;
}

int
cterm_vt52_output(struct cterminal *cterm, const uint8_t *bytes, size_t size)
{
	int status = cterm_text_boundary(cterm), speed = 0;
	enum st_codepage saved = cterm->screen.codepage;
	bool expand = cterm->lf_expand;
	cterm->screen.codepage = ST_CODEPAGE_ATARI_ST;
	cterm->lf_expand = false;
	for (size_t i = 0; status == 0 && i < size; ++i) {
		/* Native printable bytes always occupy one cell, including DEL;
		 * they do not enter Unicode control/grapheme interpretation. */
		if (cterm->accumulator == NULL && bytes[i] >= 32)
			cterm_text_literal(cterm,bytes[i]);
		else if (!cterm_parse_byte(cterm,bytes[i],&speed)) status = IO_ERROR_PROTOCOL;
		if (status == 0) status = cterm_text_boundary(cterm);
		if (status == 0) status = cterm_render_take_error(cterm);
	}
	cterm->screen.codepage = saved;
	cterm->lf_expand = expand;
	return status;
}

/* ------------------------------------------------------------------------
 * cterm_dispatch_vt52 — VT52 single-byte dispatcher.
 *
 * VT52 has different C0 semantics than ECMA-48: byte 7 (BEL) beeps,
 * bytes 11 (VT) and 12 (FF) act like LF (down + scroll), ESC primes
 * the sequence parser, and the rest of the C0 range (0-6, 14-26,
 * 28-31) is silently dropped.  All other bytes are queued into the
 * print buffer.  Sequence parsing shares cterm_accumulate_ecma_seq()
 * via the cterm_st_vt52_dispatch[] table.
 * ------------------------------------------------------------------------ */
void
cterm_dispatch_vt52(struct cterminal *cterm, unsigned char byte, int *speed)
{
	(void)speed;

	if (cterm->igs_engine != NULL && byte == 9) {
		/* TOS moves to the next eight-cell boundary, clipping at the edge;
		 * terminal tab stops and right-edge wrapping do not participate. */
		int x,y; CURR_XY(&x,&y);
		x = CURR_MINX+((x-CURR_MINX) & ~7)+8;
		if (x > CURR_MAXX) x = CURR_MAXX;
		cterm_render_set_position(cterm,x,y);
		return;
	}

	switch (byte) {
		case 7:		/* BEL */
			cterm_text_forget(cterm);
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			if (cterm->log == CTERM_LOG_ASCII && cterm->logfile != NULL)
				fputs("\x07", cterm->logfile);
			if (!cterm->quiet) {
				cterm_render_beep(cterm);
			}
			break;
		case 11:	/* VT — treated as LF */
		case 12:	/* FF — treated as LF */
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			cterm_adjust_currpos(cterm, 0, 1, true);
			break;
		case 27:	/* ESC — start a sequence, prime seq_feed() */
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			cterm->accumulator = cterm->igs_engine != NULL ? accumulate_native : cterm_accumulate_ecma_seq;
			cterm->sequence = 1;
			cterm->seq_phase = SEQ_PHASE_C1;
			cterm->seq_key = SEQ_ESC;
			cterm->seq_param_len = 0;
			cterm->seq_params[0] = 0;
			cterm->seq_param_count = 0;
			cterm->seq_overflow = false;
			break;
		case 0:
		case 1: case 2: case 3: case 4: case 5: case 6:
		case 14: case 15: case 16: case 17: case 18: case 19:
		case 20: case 21: case 22: case 23: case 24: case 25:
		case 26:
		case 28: case 29: case 30: case 31:
			/* Silently dropped. */
			break;
		default:
			*cterm->print_pos++ = byte;
			*cterm->print_pos = 0;
	}
}

/* ------------------------------------------------------------------------
 * VT52 dispatch table (sorted ascending by 21-bit key).
 * ------------------------------------------------------------------------ */
const struct seq_dispatch cterm_st_vt52_dispatch[] = {
	/* The table must be sorted ascending by `key` for bsearch().
	 * Keys are built from 6 intro bits (top) | FINAL | PRIV | INTERM.
	 * ESC + Fp/Fs (SEQ_ESC | SEQ_FINAL) all share the ESC intro, so
	 * their keys land in the SEQ_ESC band (0x0D8xxx).  ESC + Fe uses
	 * SEQ_C1(ch) which shifts (ch - 0x40 + 32) into the intro slot, so
	 * those keys land higher (0x108000+).  Fp/Fs entries therefore come
	 * before C1 entries. */
	/* --- ESC + Fp (0x30-0x3F) --- */
	{ cterm_vt52_alt_keypad_on,  SEQ_ESC | SEQ_FINAL('='), 0 },	/* 0x0D8E00 */
	{ cterm_vt52_alt_keypad_off, SEQ_ESC | SEQ_FINAL('>'), 0 },	/* 0x0D8F00 */
	/* --- ESC + Fs (0x60-0x7E) --- */
	{ cterm_vt52_set_fg,         SEQ_ESC | SEQ_FINAL('b'), 1 },	/* 0x0DB300 */
	{ cterm_vt52_set_bg,         SEQ_ESC | SEQ_FINAL('c'), 1 },	/* 0x0DB400 */
	{ cterm_vt52_clear_to_begin, SEQ_ESC | SEQ_FINAL('d'), 0 },
	{ cterm_vt52_cursor_on,      SEQ_ESC | SEQ_FINAL('e'), 0 },
	{ cterm_vt52_cursor_off,     SEQ_ESC | SEQ_FINAL('f'), 0 },
	{ cterm_vt52_save_cursor,    SEQ_ESC | SEQ_FINAL('j'), 0 },
	{ cterm_vt52_restore_cursor, SEQ_ESC | SEQ_FINAL('k'), 0 },
	{ cterm_vt52_clear_line,     SEQ_ESC | SEQ_FINAL('l'), 0 },
	{ cterm_vt52_clear_to_bol,   SEQ_ESC | SEQ_FINAL('o'), 0 },
	{ cterm_vt52_reverse_on,     SEQ_ESC | SEQ_FINAL('p'), 0 },
	{ cterm_vt52_reverse_off,    SEQ_ESC | SEQ_FINAL('q'), 0 },
	{ cterm_vt52_autowrap_on,    SEQ_ESC | SEQ_FINAL('v'), 0 },
	{ cterm_vt52_autowrap_off,   SEQ_ESC | SEQ_FINAL('w'), 0 },
	/* --- ESC + Fe (C1, 0x40-0x5F) --- */
	{ cterm_vt52_cursor_up,    SEQ_C1('A'), 0 },	/* 0x108000 */
	{ cterm_vt52_cursor_down,  SEQ_C1('B'), 0 },
	{ cterm_vt52_cursor_right, SEQ_C1('C'), 0 },
	{ cterm_vt52_cursor_left,  SEQ_C1('D'), 0 },
	{ cterm_vt52_clear_screen, SEQ_C1('E'), 0 },
	{ cterm_vt52_noop,         SEQ_C1('F'), 0 },	/* graphics */
	{ cterm_vt52_noop,         SEQ_C1('G'), 0 },	/* graphics */
	{ cterm_vt52_home,         SEQ_C1('H'), 0 },
	{ cterm_vt52_reverse_lf,   SEQ_C1('I'), 0 },
	{ cterm_vt52_erase_to_end, SEQ_C1('J'), 0 },
	{ cterm_vt52_erase_eol,    SEQ_C1('K'), 0 },
	{ cterm_vt52_insert_line,  SEQ_C1('L'), 0 },
	{ cterm_vt52_delete_line,  SEQ_C1('M'), 0 },
	{ cterm_vt52_goto_xy,      SEQ_C1('Y'), 2 },
	{ cterm_vt52_noop,         SEQ_C1('['), 0 },	/* hold screen */
	{ cterm_vt52_noop,         SEQ_C1('\\'), 0 },	/* hold screen off */
};
const size_t cterm_st_vt52_dispatch_len = SEQ_ARRAY_SIZE(cterm_st_vt52_dispatch);
