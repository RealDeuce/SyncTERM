/****************************************************************************
 * @format.tab-size 4		(Plain Text/Source Code File Header)			*
 * @format.use-tabs true	(see http://www.synchro.net/ptsc_hdr.html)		*
 *																			*
 * Copyright (c) 2026 Stephen Hurd							*
 *																			*
 * SPDX-License-Identifier: BSD-2-Clause								*
 ****************************************************************************/

/*
 * cterm_prestel.c — Prestel / Viewdata and BBC Micro (BEEB) emulation.
 *
 * Both emulations share the Prestel-style 7-bit serial attributes
 * (before/after control code handling, held mosaics, double-height,
 * conceal, separated / contiguous mosaics).  BEEB layers the BBC Micro
 * VDU byte stream on top: VDU 23 (reprogram display) with 9 trailing
 * bytes, VDU 28 (Active Position Set) with 2 trailing bytes.  Prestel
 * additionally has a programmable memory area accessed via an ESC 1 …
 * ESC 2 … state machine that stores and plays back alphanumeric blocks
 * on demand (the cterm->prestel_prog_state machine).
 *
 * The attribute helpers (cterm_prestel_*) are invoked from three
 * different call sites outside this file — cterm_set_position(),
 * cterm_reset(), and cterm_adjust_currpos() all need to re-evaluate
 * Prestel line state after motion or reset — so they are non-static
 * with the canonical cterm_ prefix.  The accumulators / dispatchers
 * here plug into the two-check byte-loop form in cterm_write.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cterm_render.h"
#include "cterm.h"
#include "cterm_internal.h"
#include "cterm_prestel.h"

/* ------------------------------------------------------------------------
 * Serial-attribute helpers
 * ------------------------------------------------------------------------ */

void
cterm_prestel_new_line(struct cterminal *cterm)
{
	cterm->extattr &= ~(CTERM_EXTATTR_PRESTEL_CONCEAL | CTERM_EXTATTR_PRESTEL_DOUBLE_HEIGHT | CTERM_EXTATTR_PRESTEL_HOLD | CTERM_EXTATTR_PRESTEL_MOSAIC | CTERM_EXTATTR_PRESTEL_SEPARATED);
	cterm->attr = 7;
	cterm_render_attribute_colours(cterm, cterm->attr, &cterm->fg_color, &cterm->bg_color);
	cterm->cell_flags &= ~(ST_CONSOLE_CELL_SAA5050
	    | ST_CONSOLE_CELL_DOUBLE_HEIGHT | ST_CONSOLE_CELL_SEPARATED
	    | ST_CONSOLE_CELL_TERMINAL_FONT | ST_CONSOLE_CELL_PIXEL_GRAPHICS
	    | ST_CONSOLE_CELL_CONCEAL | ST_CONSOLE_CELL_REVEAL
	    | ST_CONSOLE_CELL_SAA5050_CONTROL_MASK);
	cterm->cell_flags |= ST_CONSOLE_CELL_SAA5050;
	if (cterm->emulation == CTERM_EMULATION_PRESTEL)
		cterm->cell_flags |= ST_CONSOLE_CELL_TERMINAL_FONT;
	cterm->prestel_last_mosaic = 0;
	cterm_render_set_attribute(cterm, cterm->attr);
	cterm_render_set_colours(cterm, cterm->fg_color, cterm->bg_color);
}

void
cterm_prestel_apply_ctrl_before(struct cterminal *cterm, uint8_t ch)
{
	switch (ch) {
		case 73: /* Steady */
			cterm->attr &= 0x7f;
			cterm_render_attribute_colours(cterm, cterm->attr, &cterm->fg_color, &cterm->bg_color);
			if (cterm->extattr & CTERM_EXTATTR_PRESTEL_DOUBLE_HEIGHT)
				cterm->cell_flags |= ST_CONSOLE_CELL_DOUBLE_HEIGHT;
			break;
		case 76: /* Normal Height */
			cterm->extattr &= ~(CTERM_EXTATTR_PRESTEL_HOLD);
			cterm->prestel_last_mosaic = 0;
			cterm->extattr &= ~(CTERM_EXTATTR_PRESTEL_DOUBLE_HEIGHT);
			cterm->cell_flags &= ~ST_CONSOLE_CELL_DOUBLE_HEIGHT;
			break;
		case 88: /* Conceal Display */
			cterm->attr |= 0x08;
			cterm->extattr |= CTERM_EXTATTR_PRESTEL_CONCEAL;
			break;
		case 89: /* Contiguous Mosaics */
			if (!(cterm->extattr & CTERM_EXTATTR_PRESTEL_HOLD))
				cterm->prestel_last_mosaic = 0;
			cterm->extattr &= ~(CTERM_EXTATTR_PRESTEL_SEPARATED);
			cterm->cell_flags &= ~ST_CONSOLE_CELL_SEPARATED;
			break;
		case 90: /* Separated Mosaics */
			if (!(cterm->extattr & CTERM_EXTATTR_PRESTEL_HOLD))
				cterm->prestel_last_mosaic = 0;
			cterm->extattr |= CTERM_EXTATTR_PRESTEL_SEPARATED;
			cterm->cell_flags |= ST_CONSOLE_CELL_SEPARATED;
			break;
		case 92: /* Black Background */
			cterm_set_bgattr(cterm, CTERM_COLOR_BLACK);
			break;
		case 93: /* New Background */
			cterm_set_bgattr(cterm, cterm->attr & 0x07);
			break;
		case 94: /* Hold Mosaics */
			cterm->extattr |= CTERM_EXTATTR_PRESTEL_HOLD;
			break;
	}
	cterm->cell_flags |= ST_CONSOLE_CELL_SAA5050;
	if (cterm->emulation == CTERM_EMULATION_PRESTEL)
		cterm->cell_flags |= ST_CONSOLE_CELL_TERMINAL_FONT;
}

void
cterm_prestel_colour(struct cterminal *cterm, bool alpha, int colour)
{
	cterm->attr &= 0xF7;
	cterm_set_fgattr(cterm, colour);
	cterm->extattr &= ~(CTERM_EXTATTR_PRESTEL_CONCEAL);
	if (alpha)
		cterm->extattr &= ~(CTERM_EXTATTR_PRESTEL_HOLD | CTERM_EXTATTR_PRESTEL_MOSAIC);
	else
		cterm->extattr |= CTERM_EXTATTR_PRESTEL_MOSAIC;
	if (!(cterm->extattr & CTERM_EXTATTR_PRESTEL_HOLD))
		cterm->prestel_last_mosaic = 0;
}

void
cterm_prestel_apply_ctrl_after(struct cterminal *cterm, uint8_t ch)
{
	switch (ch) {
		case 65: cterm_prestel_colour(cterm, true, CTERM_COLOR_RED); break;
		case 66: cterm_prestel_colour(cterm, true, CTERM_COLOR_GREEN); break;
		case 67: cterm_prestel_colour(cterm, true, CTERM_COLOR_YELLOW); break;
		case 68: cterm_prestel_colour(cterm, true, CTERM_COLOR_BLUE); break;
		case 69: cterm_prestel_colour(cterm, true, CTERM_COLOR_MAGENTA); break;
		case 70: cterm_prestel_colour(cterm, true, CTERM_COLOR_CYAN); break;
		case 71: cterm_prestel_colour(cterm, true, CTERM_COLOR_WHITE); break;
		case 72: /* Flash */
			cterm->attr |= 0x80;
			cterm_render_attribute_colours(cterm, cterm->attr, &cterm->fg_color, &cterm->bg_color);
			if (cterm->extattr & CTERM_EXTATTR_PRESTEL_DOUBLE_HEIGHT)
				cterm->cell_flags |= ST_CONSOLE_CELL_DOUBLE_HEIGHT;
			break;
		case 77: /* Double Height */
			cterm->extattr &= ~(CTERM_EXTATTR_PRESTEL_HOLD);
			cterm->prestel_last_mosaic = 0;
			cterm->extattr |= CTERM_EXTATTR_PRESTEL_DOUBLE_HEIGHT;
			cterm->cell_flags |= ST_CONSOLE_CELL_DOUBLE_HEIGHT;
			break;
		case 81: cterm_prestel_colour(cterm, false, CTERM_COLOR_RED); break;
		case 82: cterm_prestel_colour(cterm, false, CTERM_COLOR_GREEN); break;
		case 83: cterm_prestel_colour(cterm, false, CTERM_COLOR_YELLOW); break;
		case 84: cterm_prestel_colour(cterm, false, CTERM_COLOR_BLUE); break;
		case 85: cterm_prestel_colour(cterm, false, CTERM_COLOR_MAGENTA); break;
		case 86: cterm_prestel_colour(cterm, false, CTERM_COLOR_CYAN); break;
		case 87: cterm_prestel_colour(cterm, false, CTERM_COLOR_WHITE); break;
		case 95: /* Release Mosaics */
			cterm->prestel_last_mosaic = 0;
			cterm->extattr &= ~(CTERM_EXTATTR_PRESTEL_HOLD);
			break;
	}
	cterm->cell_flags |= ST_CONSOLE_CELL_SAA5050;
	if (cterm->emulation == CTERM_EMULATION_PRESTEL)
		cterm->cell_flags |= ST_CONSOLE_CELL_TERMINAL_FONT;
}

void
cterm_prestel_apply_ctrl(struct cterminal *cterm, uint8_t ch)
{
	cterm_prestel_apply_ctrl_before(cterm, ch);
	cterm_prestel_apply_ctrl_after(cterm, ch);
}

void
cterm_prestel_get_state(struct cterminal *cterm)
{
	struct st_console_cell *line;
	int sx, sy;
	int tx, ty;

	SCR_XY(&sx, &sy);
	TERM_XY(&tx, &ty);
	line = malloc(sizeof(*line) * tx);
	if (line == NULL) {
		fprintf(stderr, "malloc() in cterm_prestel_get_state()\n");
		return;
	}
	cterm_prestel_new_line(cterm);
	if (tx > 1) {
		cterm_render_read_cells(cterm, cterm->x, sy, cterm->x + tx - 2, sy, line);
		for (int i = 0; i < (tx - 1); i++) {
			uint8_t ch = cterm_render_cell_byte(cterm, &line[i]);
			if ((line[i].flags
			    & ST_CONSOLE_CELL_SAA5050_CONTROL_MASK) != 0) {
				ch = (uint8_t)((line[i].flags
				    & ST_CONSOLE_CELL_SAA5050_CONTROL_MASK)
				    >> ST_CONSOLE_CELL_SAA5050_CONTROL_SHIFT);
				cterm_prestel_apply_ctrl(cterm, ch);
			}
			else {
				if (ch >= 160) {
					cterm->prestel_last_mosaic = ch;
					if (cterm->extattr & CTERM_EXTATTR_PRESTEL_SEPARATED)
						cterm->prestel_last_mosaic &= 0x7f;
				}
			}
		}
	}
	cterm_render_set_attribute(cterm, cterm->attr);
	cterm_render_set_colours(cterm, cterm->fg_color, cterm->bg_color);
	free(line);
}

void
cterm_prestel_fix_line(struct cterminal *cterm, int x, int y, bool restore, bool force)
{
	int sy = y;
	int sx = 1;
	int ex = 1;
	struct st_console_cell *line;
	unsigned int extattr = cterm->extattr;
	uint32_t fg_color = cterm->fg_color;
	uint32_t bg_color = cterm->bg_color;
	uint32_t cell_flags = cterm->cell_flags;
	unsigned char attr = cterm->attr;
	uint8_t prestel_last_mosaic = cterm->prestel_last_mosaic;
	bool dblheight = false;
	(void)force;

	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_SCREEN, &sy, &sx);
	ex = sx + TERM_MAXX - 1;
	line = malloc(sizeof(*line) * (ex - sx + 1));
	if (line == NULL) {
		fprintf(stderr, "malloc() in cterm_prestel_fix_line()\n");
		return;
	}
	cterm_render_read_cells(cterm, sx, sy, ex, sy, line);
	cterm_prestel_new_line(cterm);
	for (int i = 0; i < TERM_MAXX; i++) {
		uint8_t byte;
		uint8_t control = 0;
		uint16_t hyperlink;

		if ((line[i].flags & ST_CONSOLE_CELL_SAA5050) == 0)
			continue;
		byte = cterm_render_cell_byte(cterm, &line[i]);
		hyperlink = line[i].hyperlink;
		if ((line[i].flags & ST_CONSOLE_CELL_SAA5050_CONTROL_MASK) != 0) {
			if (i == (x - 1)) {
				extattr = cterm->extattr;
				fg_color = cterm->fg_color;
				bg_color = cterm->bg_color;
				cell_flags = cterm->cell_flags;
				attr = cterm->attr;
				prestel_last_mosaic = cterm->prestel_last_mosaic;
			}
			control = (uint8_t)((line[i].flags
			    & ST_CONSOLE_CELL_SAA5050_CONTROL_MASK)
			    >> ST_CONSOLE_CELL_SAA5050_CONTROL_SHIFT);
			cterm_prestel_apply_ctrl_before(cterm, control);
			if (cterm->extattr & CTERM_EXTATTR_PRESTEL_DOUBLE_HEIGHT)
				dblheight = true;
			if (byte < 128 && (cterm->extattr & CTERM_EXTATTR_PRESTEL_HOLD)) {
				if (cterm->prestel_last_mosaic != 0) {
					byte = cterm->prestel_last_mosaic | 0x80;
					if (cterm->prestel_last_mosaic & 0x80)
						cterm->cell_flags &= ~ST_CONSOLE_CELL_SEPARATED;
					else
						cterm->cell_flags |= ST_CONSOLE_CELL_SEPARATED;
				}
				else
					byte = control - 64;
			}
			if (byte >= 160 && ((cterm->extattr & CTERM_EXTATTR_PRESTEL_HOLD) == 0))
				byte = control;
			if (byte >= 160 && (cterm->extattr & CTERM_EXTATTR_PRESTEL_HOLD)) {
				if ((!!(cterm->prestel_last_mosaic & 0x80))
				    == (!!(cterm->cell_flags & ST_CONSOLE_CELL_SEPARATED))) {
					if (cterm->prestel_last_mosaic & 0x80)
						cterm->cell_flags &= ~ST_CONSOLE_CELL_SEPARATED;
					else
						cterm->cell_flags |= ST_CONSOLE_CELL_SEPARATED;
				}
			}
			cterm_render_make_cell(cterm, &line[i], byte, cterm->attr,
			    cterm->fg_color, cterm->bg_color,
			    cterm_render_attribute_font(cterm, cterm->attr), hyperlink,
			    cterm->cell_flags
			    | ST_CONSOLE_CELL_SAA5050_CONTROL(control));
			cterm_prestel_apply_ctrl_after(cterm, control);
			if (cterm->extattr & CTERM_EXTATTR_PRESTEL_DOUBLE_HEIGHT)
				dblheight = true;
		}
		else {
			if (i == (x - 1)) {
				extattr = cterm->extattr;
				fg_color = cterm->fg_color;
				bg_color = cterm->bg_color;
				cell_flags = cterm->cell_flags;
				attr = cterm->attr;
				prestel_last_mosaic = cterm->prestel_last_mosaic;
			}
			if (cterm->extattr & CTERM_EXTATTR_PRESTEL_DOUBLE_HEIGHT)
				dblheight = true;
			if (byte >= 160 && ((cterm->extattr & CTERM_EXTATTR_PRESTEL_MOSAIC) == 0))
				byte &= 0x7f;
			else if ((cterm->extattr & CTERM_EXTATTR_PRESTEL_MOSAIC)
			    && ((byte >= 32 && byte < 64)
			     || (byte >= 96 && byte < 128))) {
				byte |= 0x80;
				if (cterm->extattr & CTERM_EXTATTR_PRESTEL_SEPARATED)
					cterm->cell_flags |= ST_CONSOLE_CELL_SEPARATED;
				else
					cterm->cell_flags &= ~ST_CONSOLE_CELL_SEPARATED;
			}
			else if ((cterm->extattr & CTERM_EXTATTR_PRESTEL_MOSAIC)
			    && byte >= 0xc0 && byte <= 0xdf)
				byte &= 0x7f;
			else if (cterm->extattr & CTERM_EXTATTR_PRESTEL_SEPARATED)
				cterm->cell_flags |= ST_CONSOLE_CELL_SEPARATED;
			else
				cterm->cell_flags &= ~ST_CONSOLE_CELL_SEPARATED;
			cterm_render_make_current_cell(cterm, &line[i], byte,
			    hyperlink);
			if (byte >= 160) {
				cterm->prestel_last_mosaic = byte;
				if (cterm->extattr & CTERM_EXTATTR_PRESTEL_SEPARATED)
					cterm->prestel_last_mosaic &= 0x7f;
			}
		}
	}
	cterm_render_write_cells(cterm, sx, sy, ex, sy, line);
	free(line);
	if (dblheight)
		cterm_prestel_fix_line(cterm, x, y + 1, false, true);
	if (restore) {
		cterm->extattr = extattr;
		cterm->fg_color = fg_color;
		cterm->bg_color = bg_color;
		cterm->cell_flags = cell_flags;
		cterm->attr = attr;
		cterm->prestel_last_mosaic = prestel_last_mosaic;
	}
}

void
cterm_prestel_move(struct cterminal *cterm, int xadj, int yadj)
{
	int tx, ty;

	TERM_XY(&tx, &ty);
	tx += xadj;
	ty += yadj;
	while (tx < cterm->left_margin) {
		ty--;
		tx += TERM_MAXX;
	}
	while (tx > cterm->right_margin) {
		ty++;
		tx -= TERM_MAXX;
	}
	while (ty < cterm->top_margin) {
		if (cterm->emulation == CTERM_EMULATION_BEEB) {
			cterm_scrolldown(cterm);
			ty++;
		}
		else {
			ty += TERM_MAXY;
		}
	}
	while (ty > cterm->bottom_margin) {
		if (cterm->emulation == CTERM_EMULATION_BEEB) {
			cterm_scrollup(cterm);
			ty--;
		}
		else {
			ty -= TERM_MAXY;
		}
	}
	cterm_render_set_position(cterm, tx, ty);
	cterm_prestel_get_state(cterm);
}

void
cterm_prestel_send_memory(struct cterminal *cterm, uint8_t mem, char *retbuf, size_t retsize)
{
	char app[2] = {0, 0};
	(void)retbuf;
	(void)retsize;

	for (int x = 0; x < PRESTEL_MEM_SLOT_SIZE; x++) {
		if (cterm->prestel_data[mem][x] != '?') {
			app[0] = cterm->prestel_data[mem][x];
			cterm_respond(cterm, app, 1);
		}
	}
}

void
cterm_prestel_handle_escaped(struct cterminal *cterm, uint8_t ctrl)
{
	struct st_console_cell tmpvc[1] = {0};
	uint8_t byte;
	uint32_t flags;
	int sx, sy, x, y;

	if (ctrl < 0x40 || ctrl > 0x5f) {
		cterm->escbuf[0] = 0;
		cterm->sequence = 0;
		cterm->accumulator = NULL;
		return;
	}
	cterm_prestel_apply_ctrl_before(cterm, ctrl);
	cterm_render_set_attribute(cterm, cterm->attr);
	cterm_render_set_colours(cterm, cterm->fg_color, cterm->bg_color);
	cterm->escbuf[0] = 0;
	cterm->sequence = 0;
	cterm->accumulator = NULL;
	flags = cterm->cell_flags | ST_CONSOLE_CELL_SAA5050_CONTROL(ctrl);
	if ((cterm->extattr & CTERM_EXTATTR_PRESTEL_HOLD) && (cterm->prestel_last_mosaic != 0)) {
		byte = cterm->prestel_last_mosaic;
		if (byte & 0x80)
			flags &= ~ST_CONSOLE_CELL_SEPARATED;
		else
			flags |= ST_CONSOLE_CELL_SEPARATED;
		byte |= 0x80;
	}
	else
		byte = ' ';
	cterm_render_make_cell(cterm, &tmpvc[0], byte, cterm->attr,
	    cterm->fg_color, cterm->bg_color,
	    cterm_render_attribute_font(cterm, cterm->attr),
	    cterm_render_get_hyperlink(cterm), flags);
	SCR_XY(&sx, &sy);
	CURR_XY(&x, &y);
	cterm_render_write_cells(cterm, sx, sy, sx, sy, tmpvc);
	cterm_prestel_apply_ctrl_after(cterm, ctrl);
	cterm_render_set_attribute(cterm, cterm->attr);
	cterm_render_set_colours(cterm, cterm->fg_color, cterm->bg_color);
	cterm_advance_char(cterm, &x, &y, 1);
}

/* ------------------------------------------------------------------------
 * Prestel sequence accumulator
 *
 * Drives the Prestel ESC handling and the programmable-memory state
 * machine (ESC 1 … ESC 2 … programs slots of alphanumeric data that can
 * be played back with ENQ or a program-block).  The plain-ESC case
 * simply hands the byte to cterm_prestel_handle_escaped() which applies
 * the serial-attribute control.
 * ------------------------------------------------------------------------ */
bool
cterm_accumulate_prestel_seq(struct cterminal *cterm, unsigned char byte, int *speed)
{
	unsigned char ch[2] = {byte, 0};
	(void)speed;

	if (cterm->prestel_prog_state == PRESTEL_PROG_NONE) {
		if (byte == '1') {
			cterm->prestel_prog_state = PRESTEL_PROG_1;
		}
		else {
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			cterm_prestel_handle_escaped(cterm, byte);
		}
		return true;
	}

	if (byte == 0)
		return true;
	switch (cterm->prestel_prog_state) {
		case PRESTEL_PROG_1:
			if (byte == 0x1b) {
				cterm->prestel_prog_state = PRESTEL_PROG_1_ESC;
			}
			else {
				cterm->prestel_prog_state = PRESTEL_PROG_NONE;
				cterm->escbuf[0] = 0;
				cterm->sequence = 0;
				cterm->accumulator = NULL;
			}
			break;
		case PRESTEL_PROG_1_ESC:
			if (byte == '2') {
				cterm->prestel_prog_state = PRESTEL_PROG_2;
				cterm->prestel_mem = 0;
			}
			else {
				cterm->prestel_prog_state = PRESTEL_PROG_NONE;
				cterm->escbuf[0] = 0;
				cterm->sequence = 0;
				cterm->accumulator = NULL;
			}
			break;
		case PRESTEL_PROG_2:
			if (byte == 5) {
				cterm_prestel_send_memory(cterm, cterm->prestel_mem, NULL, 0);
				cterm->prestel_prog_state = PRESTEL_PROG_NONE;
				cterm->escbuf[0] = 0;
				cterm->sequence = 0;
				cterm->accumulator = NULL;
			}
			else if (byte == 0x1b) {
				cterm->prestel_prog_state = PRESTEL_PROG_2_ESC;
			}
			else {
				cterm->prestel_prog_state = PRESTEL_PROG_NONE;
				cterm->escbuf[0] = 0;
				cterm->sequence = 0;
				cterm->accumulator = NULL;
			}
			break;
		case PRESTEL_PROG_2_ESC:
			if (byte == '3') {
				cterm->prestel_mem++;
				if (cterm->prestel_mem >= PRESTEL_MEM_SLOTS) {
					cterm->prestel_prog_state = PRESTEL_PROG_NONE;
					cterm->escbuf[0] = 0;
					cterm->sequence = 0;
					cterm->accumulator = NULL;
				}
				else {
					cterm->prestel_prog_state = PRESTEL_PROG_2;
				}
			}
			else if (byte == '4') {
				cterm->prestel_prog_state = PRESTEL_PROG_PROGRAM_BLOCK;
			}
			else {
				cterm->prestel_prog_state = PRESTEL_PROG_NONE;
				cterm->escbuf[0] = 0;
				cterm->sequence = 0;
				cterm->accumulator = NULL;
			}
			break;
		case PRESTEL_PROG_PROGRAM_BLOCK:
			ch[1] = 0;
			switch (byte) {
				case ':':
					ch[0] = '0';
					/* fallthrough */
				case '1': case '2': case '3': case '4': case '5':
				case '6': case '7': case '8': case '9':
				case ';': case '?':
					ustrcat(cterm->escbuf, ch);
					if (strlen(cterm->escbuf) >= PRESTEL_MEM_SLOT_SIZE) {
						memcpy(cterm->prestel_data[cterm->prestel_mem], cterm->escbuf, PRESTEL_MEM_SLOT_SIZE);
						cterm_prestel_send_memory(cterm, cterm->prestel_mem, NULL, 0);
						cterm->prestel_prog_state = PRESTEL_PROG_NONE;
						cterm->escbuf[0] = 0;
						cterm->sequence = 0;
						cterm->accumulator = NULL;
					}
					break;
				default:
					cterm->prestel_prog_state = PRESTEL_PROG_NONE;
					cterm->escbuf[0] = 0;
					cterm->sequence = 0;
					cterm->accumulator = NULL;
					break;
			}
			break;
		default:
			cterm->prestel_prog_state = PRESTEL_PROG_NONE;
			break;
	}
	return true;
}

/* ------------------------------------------------------------------------
 * BEEB trailing-byte handlers (VDU 23 / VDU 28)
 *
 * Installed by cterm_dispatch_beeb when VDU 23 or VDU 28 arrives.  The
 * generic cterm_accumulate_trailing collects the trailing bytes into
 * cterm->seq_param_int[] and calls the stored handler once the count
 * is exhausted.
 *
 * VDU 23 (reprogram display) is 9 trailing bytes; only VDU 23,1,0 / 1,1
 * are acted on (cursor off / on), the rest are discarded.  VDU 28
 * (Active Position Set) is 2 trailing bytes: column and row, each
 * biased by ASCII SP (0x20).
 * ------------------------------------------------------------------------ */
static void
cterm_beeb_vdu23(struct cterminal *cterm, int *speed)
{
	(void)speed;
	if (cterm->seq_param_int[0] == 1
	    && cterm->seq_param_int[2] == 0 && cterm->seq_param_int[3] == 0
	    && cterm->seq_param_int[4] == 0 && cterm->seq_param_int[5] == 0
	    && cterm->seq_param_int[6] == 0 && cterm->seq_param_int[7] == 0
	    && cterm->seq_param_int[8] == 0) {
		switch (cterm->seq_param_int[1]) {
			case 0:
				cterm->cursor = CTERM_CURSOR_HIDDEN;
				cterm_render_set_cursor(cterm, cterm->cursor);
				break;
			case 1:
				cterm->cursor = CTERM_CURSOR_UNDERLINE;
				cterm_render_set_cursor(cterm, cterm->cursor);
				break;
		}
	}
}

static void
cterm_beeb_vdu28(struct cterminal *cterm, int *speed)
{
	(void)speed;
	/* VDU 28 operands arrive as 8-bit characters (0-255); the '0' offset
	 * keeps the result well inside int range. */
	cterm_render_set_position(cterm, (int)(cterm->seq_param_int[1] - ' ') + 1,
	    (int)(cterm->seq_param_int[0] - ' ') + 1);
}

/* ------------------------------------------------------------------------
 * Prestel single-byte dispatcher
 *
 * Handles Prestel C0 controls (APB/APF/APD/APU/APH/CS/cursor-on-off/ENQ)
 * and the printable path including raw C1 attribute bytes (0x80-0x9F)
 * and mosaic translation for the G1 upper range.
 * ------------------------------------------------------------------------ */
void
cterm_dispatch_prestel(struct cterminal *cterm, unsigned char byte, int *speed)
{
	unsigned char ch[2] = {byte, 0};
	(void)speed;

	switch (byte) {
		case 0:		/* NUL */
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			break;
		case 5:		/* ENQ */
			cterm_prestel_send_memory(cterm, 0, NULL, 0);
			break;
		case 8:		/* APB (Active Position Backward) */
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			cterm_prestel_move(cterm, -1, 0);
			break;
		case 9:		/* APF (Active Position Forward) */
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			cterm_prestel_move(cterm, 1, 0);
			break;
		case 10:	/* APD (Active Position Down) */
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			cterm_prestel_move(cterm, 0, 1);
			break;
		case 11:	/* APU (Active Position Up) */
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			cterm_prestel_move(cterm, 0, -1);
			break;
		case 12:	/* CS (Clear Screen) */
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			cterm->screen.prestel_reveal = false;
			cterm_prestel_new_line(cterm);
			cterm_clearscreen(cterm, (char)cterm->attr);
			cterm_render_set_position(cterm, CURR_MINX, CURR_MINY);
			break;
		case 17:	/* Cursor on */
			cterm->cursor = CTERM_CURSOR_UNDERLINE;
			cterm_render_set_cursor(cterm, cterm->cursor);
			break;
		case 20:	/* Cursor off */
			cterm->cursor = CTERM_CURSOR_HIDDEN;
			cterm_render_set_cursor(cterm, cterm->cursor);
			break;
		case 27:	/* ESC — enter sequence mode */
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			cterm->accumulator = cterm_accumulate_prestel_seq;
			cterm->sequence = 1;
			break;
		case 30:	/* APH (Active Position Home) */
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			cterm_render_set_position(cterm, CURR_MINX, CURR_MINY);
			cterm_prestel_new_line(cterm);
			break;
		default:
			/* CR, LF, G0 printable: queue */
			if (byte == 13 || byte == 10 || (byte >= 32 && byte <= 127)) {
				if (cterm->extattr & CTERM_EXTATTR_PRESTEL_MOSAIC) {
					if ((byte < 64 && byte >= 32) || (byte >= 96 && byte < 128))
						ch[0] = byte | 0x80;
					else
						ch[0] = byte;
					if (ch[0] >= 160) {
						cterm->prestel_last_mosaic = ch[0];
						if (cterm->extattr & CTERM_EXTATTR_PRESTEL_SEPARATED)
							cterm->prestel_last_mosaic &= 0x7f;
					}
				}
				*cterm->print_pos++ = ch[0];
				*cterm->print_pos = 0;
			}
			else if (byte >= 0x80 && byte <= 0x9f) {
				/* Raw C1 attribute control */
				cterm_uctputs(cterm, cterm->print_buf);
				cterm->print_buf[0] = 0;
				cterm->print_pos = cterm->print_buf;
				cterm_prestel_handle_escaped(cterm, byte - 64);
			}
			else if (byte < 32) {
				/* ECMA-48 §2.3.1: C0 characters that don't display. */
			}
			else {
				/* G1 upper range: store as-is for Unicode replacement */
				*cterm->print_pos++ = ch[0];
				*cterm->print_pos = 0;
			}
			break;
	}
}

/* ------------------------------------------------------------------------
 * BEEB single-byte dispatcher
 *
 * Like Prestel, but with BBC Micro-specific twists: BEL is handled as a
 * real beep, VDU 23/28 prime the sequence accumulator for trailing
 * bytes, and ASCII substitutions (# ↔ _ ↔ `) mimic the BBC VDU.
 * ------------------------------------------------------------------------ */
void
cterm_dispatch_beeb(struct cterminal *cterm, unsigned char byte, int *speed)
{
	unsigned char ch[2] = {byte, 0};
	(void)speed;

	switch (byte) {
		case 0:		/* NUL */
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			break;
		case 7:		/* BEL */
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			if (!cterm->quiet) {
				cterm_render_write_byte(cterm, 7);
			}
			break;
		case 8:		/* APB */
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			cterm_prestel_move(cterm, -1, 0);
			break;
		case 9:		/* APF */
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			cterm_prestel_move(cterm, 1, 0);
			break;
		case 10:	/* APD */
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			cterm_prestel_move(cterm, 0, 1);
			break;
		case 11:	/* APU */
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			cterm_prestel_move(cterm, 0, -1);
			break;
		case 12:	/* CS */
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			cterm->screen.prestel_reveal = false;
			cterm_prestel_new_line(cterm);
			cterm_clearscreen(cterm, (char)cterm->attr);
			cterm_render_set_position(cterm, CURR_MINX, CURR_MINY);
			break;
		case 23:	/* VDU 23 — reprogram display (9 trailing bytes) */
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			cterm->seq_trailing_handler = cterm_beeb_vdu23;
			cterm->seq_trailing_remain = 9;
			cterm->seq_param_count = 0;
			cterm->accumulator = cterm_accumulate_trailing;
			break;
		case 28:	/* VDU 28 — APS (2 trailing bytes) */
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			cterm->seq_trailing_handler = cterm_beeb_vdu28;
			cterm->seq_trailing_remain = 2;
			cterm->seq_param_count = 0;
			cterm->accumulator = cterm_accumulate_trailing;
			break;
		case 30:	/* APH */
			cterm_uctputs(cterm, cterm->print_buf);
			cterm->print_buf[0] = 0;
			cterm->print_pos = cterm->print_buf;
			cterm_render_set_position(cterm, CURR_MINX, CURR_MINY);
			cterm_prestel_new_line(cterm);
			break;
		case 127:	/* Destructive backspace */
			*cterm->print_pos++ = '\b';
			*cterm->print_pos++ = ' ';
			*cterm->print_pos++ = '\b';
			*cterm->print_pos = 0;
			break;
		default:
			/* Mode 7 VDU ASCII substitutions */
			if (byte == '#')
				ch[0] = '_';
			else if (byte == '_')
				ch[0] = '`';
			else if (byte == '`')
				ch[0] = '#';
			if (byte == 13 || byte == 10 || (byte >= 32 && byte <= 127)) {
				if (cterm->extattr & CTERM_EXTATTR_PRESTEL_MOSAIC) {
					if ((byte < 64 && byte >= 32) || (byte >= 96 && byte < 128))
						ch[0] = ch[0] | 0x80;
					if (ch[0] >= 160) {
						cterm->prestel_last_mosaic = ch[0];
						if (cterm->extattr & CTERM_EXTATTR_PRESTEL_SEPARATED)
							cterm->prestel_last_mosaic &= 0x7f;
					}
				}
				*cterm->print_pos++ = ch[0];
				*cterm->print_pos = 0;
			}
			else if (byte >= 0x80 && byte <= 0x9f) {
				cterm_uctputs(cterm, cterm->print_buf);
				cterm->print_buf[0] = 0;
				cterm->print_pos = cterm->print_buf;
				cterm_prestel_handle_escaped(cterm, byte - 64);
			}
			else if (byte < 32) {
				/* ignore */
			}
			else {
				*cterm->print_pos++ = ch[0];
				*cterm->print_pos = 0;
			}
			break;
	}
}
