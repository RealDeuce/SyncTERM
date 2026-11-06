/****************************************************************************
 * @format.tab-size 4		(Plain Text/Source Code File Header)			*
 * @format.use-tabs true	(see http://www.synchro.net/ptsc_hdr.html)		*
 *																			*
 * Copyright (c) 2026 Stephen Hurd							*
 *																			*
 * SPDX-License-Identifier: BSD-2-Clause								*
 ****************************************************************************/

/*
 * cterm_dec.c — DEC-private control-sequence handlers.
 *
 * Implements the CSI `?` and other DEC-extension sequences originating on
 * DEC VT-series terminals:
 *   - Cursor save/restore:  DECSC, DECRC
 *   - Private modes:        DECSM, DECRM (CSI ? h / l), plus the
 *                           save_dec_modes / restore_dec_modes helpers
 *                           driving XTerm-style mode stacks
 *   - Rectangular-area ops: DECERA (erase), DECFRA (fill), DECCRA (copy),
 *                           DECIC (insert col), DECDC (delete col),
 *                           DECCARA (change-attrs), DECRARA (reverse-attrs)
 *   - Area qualifiers:      DECSACE (select character edit extent)
 *   - Checksum / macros:    DECCKSR, DECMACRO
 *   - Mode queries:         DECRQM_ansi, DECRQM_dec
 *   - Tab report:           DECTABSR
 *   - Cursor style:         DECSCUSR
 *   - Left/right margins:   DECSLRM (CSI Pl ; Pr s when DECLRMM set)
 *
 * Handlers share the cterm_handle_* signature used by the dispatch table.
 */

#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "cterm_render.h"
#include "cterm.h"
#include "cterm_internal.h"
#include "cterm_dec.h"
#include "cterm_ecma48.h"	/* cterm_handle_sm/rm chained from DECSM/DECRM */
#include "cterm_cterm.h"	/* SyncTERM DECSM extensions chained from DEC */
#include "io_error.h"

void
cterm_handle_decscusr(struct cterminal *cterm, int *speed)	/* CSI SP q */
{
	(void)speed;
	cterm_seq_default(cterm, 0, 1);
	uint64_t style = cterm->seq_param_int[0];
	if (style < 1 || style > 4)
		style = 1;
	cterm->cursor_style = (int)style;
	cterm->cursor = style <= 2 ? CTERM_CURSOR_BLOCK : CTERM_CURSOR_UNDERLINE;
	cterm_render_set_cursor(cterm, cterm->cursor);
}

void
cterm_handle_decrqm_public(struct cterminal *cterm, int *speed)	/* CSI Ps $ p */
{
	int pm = 0;
	(void)speed;
	if (cterm->seq_param_count != 1)
		return;
	cterm_seq_default(cterm, 0, 0);
	switch (cterm->seq_param_int[0]) {
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
		case 15:
		case 17:
		case 18:
			pm = 4;	/* permanently set (see ECMA-48 modes) */
			break;
		case 14:
			pm = cterm->fetm ? 1 : 2;
			break;
		case 16:
			pm = cterm->ttm ? 1 : 2;
			break;
		case 21:
		case 22:
			pm = 3;	/* permanently reset */
			break;
	}
	cterm_respond_printf(cterm, "\x1b[%u;%d$y",
	    (unsigned)cterm->seq_param_int[0], pm);
}

void
cterm_handle_dectabsr(struct cterminal *cterm, int *speed)	/* CSI Ps $ w — tab report */
{
	int i;
	(void)speed;
	cterm_seq_default(cterm, 0, 0);
	if (cterm->seq_param_int[0] != 2)
		return;
	cterm_respond(cterm, "\x1bP2$u", 5);
	for (i = 0; i < cterm->tab_count && cterm->tabs[i] <= cterm->width; i++) {
		cterm_respond_printf(cterm, "%s%d", i == 0 ? "" : "/", cterm->tabs[i]);
	}
	cterm_respond(cterm, "\x1b\\", 2);
}

void
cterm_handle_decera(struct cterminal *cterm, int *speed)	/* CSI Pt;Pl;Pb;Pr $ z */
{
	int t, l, b, r;
	(void)speed;
	cterm_seq_default(cterm, 0, 1);
	cterm_seq_default(cterm, 1, 1);
	cterm_seq_default(cterm, 2, cterm->height);
	cterm_seq_default(cterm, 3, cterm->width);
	t = (int)cterm->seq_param_int[0];
	l = (int)cterm->seq_param_int[1];
	b = (int)cterm->seq_param_int[2];
	r = (int)cterm->seq_param_int[3];
	if (t > b || l > r)
		return;
	if (t < CURR_MINY)
		t = CURR_MINY;
	if (l < CURR_MINX)
		l = CURR_MINX;
	if (b > CURR_MAXY)
		b = CURR_MAXY;
	if (r > CURR_MAXX)
		r = CURR_MAXX;
	cterm_coord_conv_xy(cterm, CTERM_COORD_CURR, CTERM_COORD_SCREEN, &l, &t);
	cterm_coord_conv_xy(cterm, CTERM_COORD_CURR, CTERM_COORD_SCREEN, &r, &b);
	cterm_fill_rect(cterm, l, t, r, b, ' ');
}

void
cterm_handle_decfra(struct cterminal *cterm, int *speed)	/* CSI Pch;Pt;Pl;Pb;Pr $ x */
{
	int t, l, b, r;
	uint8_t ch;
	(void)speed;
	cterm_seq_default(cterm, 0, 0);
	ch = (uint8_t)cterm->seq_param_int[0];
	if (ch < 0x20 || ch == 0x7F)
		return;
	cterm_seq_default(cterm, 1, 1);
	cterm_seq_default(cterm, 2, 1);
	cterm_seq_default(cterm, 3, cterm->height);
	cterm_seq_default(cterm, 4, cterm->width);
	t = (int)cterm->seq_param_int[1];
	l = (int)cterm->seq_param_int[2];
	b = (int)cterm->seq_param_int[3];
	r = (int)cterm->seq_param_int[4];
	if (t > b || l > r)
		return;
	if (t < CURR_MINY)
		t = CURR_MINY;
	if (l < CURR_MINX)
		l = CURR_MINX;
	if (b > CURR_MAXY)
		b = CURR_MAXY;
	if (r > CURR_MAXX)
		r = CURR_MAXX;
	cterm_coord_conv_xy(cterm, CTERM_COORD_CURR, CTERM_COORD_SCREEN, &l, &t);
	cterm_coord_conv_xy(cterm, CTERM_COORD_CURR, CTERM_COORD_SCREEN, &r, &b);
	cterm_fill_rect(cterm, l, t, r, b, ch);
}

void
cterm_handle_deccra(struct cterminal *cterm, int *speed)	/* CSI Pts;...;Ppd $ v */
{
	int st, sl, sb, sr, dt, dl;
	int sw, sh, i;
	struct st_console_cell *buf;
	(void)speed;
	cterm_seq_default(cterm, 0, 1);
	cterm_seq_default(cterm, 1, 1);
	cterm_seq_default(cterm, 2, cterm->height);
	cterm_seq_default(cterm, 3, cterm->width);
	/* Pps (param 4) = source page, ignore */
	cterm_seq_default(cterm, 5, 1);
	cterm_seq_default(cterm, 6, 1);
	/* Ppd (param 7) = dest page, ignore */
	st = (int)cterm->seq_param_int[0];
	sl = (int)cterm->seq_param_int[1];
	sb = (int)cterm->seq_param_int[2];
	sr = (int)cterm->seq_param_int[3];
	dt = (int)cterm->seq_param_int[5];
	dl = (int)cterm->seq_param_int[6];
	if (st > sb || sl > sr)
		return;
	if (st < CURR_MINY)
		st = CURR_MINY;
	if (sl < CURR_MINX)
		sl = CURR_MINX;
	if (sb > CURR_MAXY)
		sb = CURR_MAXY;
	if (sr > CURR_MAXX)
		sr = CURR_MAXX;
	if (dt < CURR_MINY)
		dt = CURR_MINY;
	if (dl < CURR_MINX)
		dl = CURR_MINX;
	sw = sr - sl + 1;
	sh = sb - st + 1;
	if (dt + sh - 1 > CURR_MAXY)
		sh = CURR_MAXY - dt + 1;
	if (dl + sw - 1 > CURR_MAXX)
		sw = CURR_MAXX - dl + 1;
	if (sw <= 0 || sh <= 0)
		return;
	cterm_coord_conv_xy(cterm, CTERM_COORD_CURR, CTERM_COORD_SCREEN, &sl, &st);
	cterm_coord_conv_xy(cterm, CTERM_COORD_CURR, CTERM_COORD_SCREEN, &dl, &dt);
	buf = calloc(sw, sizeof(*buf));
	if (buf) {
		for (i = 0; i < sh; i++) {
			cterm_render_read_cells(cterm, sl, st + i, sl + sw - 1, st + i, buf);
			cterm_render_write_cells(cterm, dl, dt + i, dl + sw - 1, dt + i, buf);
		}
		free(buf);
	}
}

void
cterm_handle_decic(struct cterminal *cterm, int *speed)	/* CSI Pn ' } */
{
	int cx, cy;
	int n, scx, scy, srx, sry, width, keep, c, r;
	struct st_console_cell *rowbuf;
	struct st_console_cell blank = {0};
	(void)speed;
	cterm_seq_default(cterm, 0, 1);
	TERM_XY(&cx, &cy);
	if (cx < TERM_MINX || cx > TERM_MAXX
	    || cy < TERM_MINY || cy > TERM_MAXY)
		return;
	n = (int)cterm->seq_param_int[0];
	scx = cx;
	scy = TERM_MINY;
	srx = TERM_MAXX;
	sry = TERM_MAXY;
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_SCREEN, &scx, &scy);
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_SCREEN, &srx, &sry);
	width = srx - scx + 1;
	if (n >= width) {
		cterm_fill_rect(cterm, scx, scy, srx, sry, ' ');
		return;
	}
	keep = width - n;
	rowbuf = calloc(width, sizeof(*rowbuf));
	if (rowbuf == NULL)
		return;
	cterm_render_make_current_cell(cterm, &blank, ' ', 0);
	for (r = scy; r <= sry; r++) {
		cterm_render_read_cells(cterm, scx, r, srx, r, rowbuf);
		memmove(&rowbuf[n], &rowbuf[0], keep * sizeof(*rowbuf));
		for (c = 0; c < n; c++)
			rowbuf[c] = blank;
		cterm_render_write_cells(cterm, scx, r, srx, r, rowbuf);
	}
	free(rowbuf);
}

void
cterm_handle_decdc(struct cterminal *cterm, int *speed)	/* CSI Pn ' ~ */
{
	int cx, cy;
	int n, scx, scy, srx, sry, width, keep, c, r;
	struct st_console_cell *rowbuf;
	struct st_console_cell blank = {0};
	(void)speed;
	cterm_seq_default(cterm, 0, 1);
	TERM_XY(&cx, &cy);
	if (cx < TERM_MINX || cx > TERM_MAXX
	    || cy < TERM_MINY || cy > TERM_MAXY)
		return;
	n = (int)cterm->seq_param_int[0];
	scx = cx;
	scy = TERM_MINY;
	srx = TERM_MAXX;
	sry = TERM_MAXY;
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_SCREEN, &scx, &scy);
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_SCREEN, &srx, &sry);
	width = srx - scx + 1;
	if (n >= width) {
		cterm_fill_rect(cterm, scx, scy, srx, sry, ' ');
		return;
	}
	keep = width - n;
	rowbuf = calloc(width, sizeof(*rowbuf));
	if (rowbuf == NULL)
		return;
	cterm_render_make_current_cell(cterm, &blank, ' ', 0);
	for (r = scy; r <= sry; r++) {
		cterm_render_read_cells(cterm, scx, r, srx, r, rowbuf);
		memmove(&rowbuf[0], &rowbuf[n], keep * sizeof(*rowbuf));
		for (c = keep; c < width; c++)
			rowbuf[c] = blank;
		cterm_render_write_cells(cterm, scx, r, srx, r, rowbuf);
	}
	free(rowbuf);
}

void
cterm_handle_decsace(struct cterminal *cterm, int *speed)	/* CSI Ps * x */
{
	(void)speed;
	cterm_seq_default(cterm, 0, 0);
	cterm->decsace = (uint8_t)cterm->seq_param_int[0];
}

void
cterm_handle_deccara(struct cterminal *cterm, int *speed)	/* CSI Pt;Pl;Pb;Pr;...$ r */
{
	int top, left, bottom, right;
	int st, sl, sb, sr, bufsz;
	(void)speed;

	cterm_seq_default(cterm, 0, 1);
	cterm_seq_default(cterm, 1, 1);
	cterm_seq_default(cterm, 2, cterm->height);
	cterm_seq_default(cterm, 3, cterm->width);
	top = (int)cterm->seq_param_int[0];
	left = (int)cterm->seq_param_int[1];
	bottom = (int)cterm->seq_param_int[2];
	right = (int)cterm->seq_param_int[3];
	if (cterm->extattr & CTERM_EXTATTR_ORIGINMODE) {
		top += cterm->top_margin - 1;
		bottom += cterm->top_margin - 1;
		left += cterm->left_margin - 1;
		right += cterm->left_margin - 1;
	}
	if (top < 1)
		top = 1;
	if (left < 1)
		left = 1;
	if (bottom > cterm->height)
		bottom = cterm->height;
	if (right > cterm->width)
		right = cterm->width;
	if (top > bottom || (cterm->decsace != 1 && left > right))
		return;

	{
		unsigned char save_attr = cterm->attr;
		uint32_t save_fg = cterm->fg_color;
		uint32_t save_bg = cterm->bg_color;
		bool save_neg = cterm->negative;
		uint32_t save_flags = cterm->cell_flags;
		char *save_fg_tc = cterm->fg_tc_str ? strdup(cterm->fg_tc_str) : NULL;
		char *save_bg_tc = cterm->bg_tc_str ? strdup(cterm->bg_tc_str) : NULL;
		struct st_console_cell *rowbuf;
		st = top; sl = left; sb = bottom; sr = right;
		cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_SCREEN, &sl, &st);
		cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_SCREEN, &sr, &sb);
		bufsz = (cterm->decsace == 1) ? cterm->width : (sr - sl + 1);
		rowbuf = calloc(bufsz, sizeof(*rowbuf));
		if (rowbuf) {
			for (int r = st; r <= sb; r++) {
				int c_start, c_end;
				if (cterm->decsace == 1) {
					c_start = (r == st) ? sl : cterm->x;
					c_end = (r == sb) ? sr : cterm->x + cterm->width - 1;
				}
				else {
					c_start = sl;
					c_end = sr;
				}
				cterm_render_read_cells(cterm, c_start, r, c_end, r, rowbuf);
				int cnt = c_end - c_start + 1;
				for (int c = 0; c < cnt; c++) {
					uint8_t byte = cterm_render_cell_byte(cterm,
					    &rowbuf[c]);
					cterm->attr = cterm_render_cell_attribute(cterm,
					    &rowbuf[c]);
					cterm->fg_color = rowbuf[c].foreground;
					cterm->bg_color = rowbuf[c].background;
					cterm->cell_flags = rowbuf[c].flags
					    & ~(ST_CONSOLE_CELL_BRIGHT
					    | ST_CONSOLE_CELL_BLINK
					    | ST_CONSOLE_CELL_LEGACY_FOREGROUND
					    | ST_CONSOLE_CELL_LEGACY_BACKGROUND);
					cterm->negative = false;
					CTERM_FREE_AND_CLEAR(cterm->fg_tc_str);
					CTERM_FREE_AND_CLEAR(cterm->bg_tc_str);
					for (int p = 4; p < cterm->seq_param_count; p++)
						cterm_apply_sgr(cterm, &p);
					cterm_render_make_current_cell(cterm, &rowbuf[c],
					    byte, rowbuf[c].hyperlink);
				}
				cterm_render_write_cells(cterm, c_start, r, c_end, r, rowbuf);
			}
			free(rowbuf);
		}
		CTERM_FREE_AND_CLEAR(cterm->fg_tc_str);
		CTERM_FREE_AND_CLEAR(cterm->bg_tc_str);
		cterm->attr = save_attr;
		cterm->fg_color = save_fg;
		cterm->bg_color = save_bg;
		cterm->cell_flags = save_flags;
		cterm->negative = save_neg;
		cterm->fg_tc_str = save_fg_tc;
		cterm->bg_tc_str = save_bg_tc;
	}
}

void
cterm_handle_decrara(struct cterminal *cterm, int *speed)	/* CSI Pt;Pl;Pb;Pr;...$ t */
{
	int top, left, bottom, right;
	int st, sl, sb, sr, bufsz;
	struct st_console_cell *rowbuf;
	(void)speed;
	cterm_seq_default(cterm, 0, 1);
	cterm_seq_default(cterm, 1, 1);
	cterm_seq_default(cterm, 2, cterm->height);
	cterm_seq_default(cterm, 3, cterm->width);
	top = (int)cterm->seq_param_int[0];
	left = (int)cterm->seq_param_int[1];
	bottom = (int)cterm->seq_param_int[2];
	right = (int)cterm->seq_param_int[3];
	if (cterm->extattr & CTERM_EXTATTR_ORIGINMODE) {
		top += cterm->top_margin - 1;
		bottom += cterm->top_margin - 1;
		left += cterm->left_margin - 1;
		right += cterm->left_margin - 1;
	}
	if (top < 1)
		top = 1;
	if (left < 1)
		left = 1;
	if (bottom > cterm->height)
		bottom = cterm->height;
	if (right > cterm->width)
		right = cterm->width;
	if (top > bottom)
		return;
	if (cterm->decsace != 1 && left > right)
		return;
	st = top;
	sl = left;
	sb = bottom;
	sr = right;
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_SCREEN, &sl, &st);
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_SCREEN, &sr, &sb);
	bufsz = (cterm->decsace == 1) ? cterm->width : (sr - sl + 1);
	rowbuf = calloc(bufsz, sizeof(*rowbuf));
	if (rowbuf == NULL)
		return;
	for (int r = st; r <= sb; r++) {
		int c_start, c_end;
		if (cterm->decsace == 1) {
			c_start = (r == st) ? sl : cterm->x;
			c_end = (r == sb) ? sr : cterm->x + cterm->width - 1;
		}
		else {
			c_start = sl;
			c_end = sr;
		}
		cterm_render_read_cells(cterm, c_start, r, c_end, r, rowbuf);
		int cnt = c_end - c_start + 1;
		for (int c = 0; c < cnt; c++) {
			uint8_t attribute = cterm_render_cell_attribute(cterm,
			    &rowbuf[c]);
			for (int p = 4; p < cterm->seq_param_count; p++) {
				cterm_seq_default(cterm, p, 0);
				switch (cterm->seq_param_int[p]) {
					case 0:
					attribute ^= 0x88;
					cterm_render_attribute_colours(cterm, attribute, &rowbuf[c].foreground, NULL);
					cterm_render_attribute_colours(cterm, attribute, NULL, &rowbuf[c].background);
						{
							uint32_t tfg = rowbuf[c].foreground;
							rowbuf[c].foreground = rowbuf[c].background;
							rowbuf[c].background = tfg;
						}
						break;
					case 1:
					case 22:
					attribute ^= 0x08;
					cterm_render_attribute_colours(cterm, attribute, &rowbuf[c].foreground, NULL);
						break;
					case 5:
					case 25:
					attribute ^= 0x80;
					cterm_render_attribute_colours(cterm, attribute, NULL, &rowbuf[c].background);
						break;
					case 7:
					case 27:
					{
						uint8_t a = attribute;
						uint8_t fg_nibble = a & 0x0f;
						uint8_t bg_nibble = (a >> 4) & 0x0f;
						attribute = (fg_nibble << 4) | bg_nibble;
						uint32_t tfg = rowbuf[c].foreground;
						rowbuf[c].foreground = rowbuf[c].background;
						rowbuf[c].background = tfg;
						break;
					}
				}
			}
			cterm_render_make_cell(cterm, &rowbuf[c],
			    cterm_render_cell_byte(cterm, &rowbuf[c]), attribute,
			    rowbuf[c].foreground, rowbuf[c].background,
			    cterm_render_attribute_font(cterm, attribute),
			    rowbuf[c].hyperlink, rowbuf[c].flags
			    & ~(ST_CONSOLE_CELL_BRIGHT | ST_CONSOLE_CELL_BLINK
			    | ST_CONSOLE_CELL_LEGACY_FOREGROUND
			    | ST_CONSOLE_CELL_LEGACY_BACKGROUND));
		}
		cterm_render_write_cells(cterm, c_start, r, c_end, r, rowbuf);
	}
	free(rowbuf);
}

void
cterm_handle_decmacro(struct cterminal *cterm, int *speed)	/* CSI Pn * z */
{
	(void)speed;
	if (cterm->seq_param_count <= 0 || cterm->seq_param_int[0] > 63)
		return;
	uint64_t n = cterm->seq_param_int[0];
	if (!cterm->macros[n])
		return;
	if (cterm->in_macro & (UINT64_C(1) << n))
		return;
	size_t remaining = cterm->macro_lens[n];
	if (remaining == 0)
		return;
	/* Playback can reset or redefine its own stored definition. */
	char *copy = malloc(remaining);
	if (copy == NULL) {
		cterm_render_record_error(cterm, IO_ERROR_OUT_OF_MEMORY);
		return;
	}
	memcpy(copy, cterm->macros[n], remaining);
	cterm->escbuf[0] = 0;
	cterm->sequence = 0;
	cterm->accumulator = NULL;
	cterm->in_macro |= (UINT64_C(1) << n);
	size_t consumed;
	int status = cterm_input_write(cterm, copy, remaining, &consumed, CTERM_INPUT_REPLAY);
	cterm_render_record_error(cterm, status);
	free(copy);
	cterm->in_macro &= ~(UINT64_C(1) << n);
}

void
cterm_handle_deccksr(struct cterminal *cterm, int *speed)	/* CSI Pid;1;Pt;Pl;Pb;Pr * y */
{
	(void)speed;
	if (cterm->seq_param_count < 6)
		return;
	if (cterm->seq_param_int[0] == UINT64_MAX
	    || cterm->seq_param_int[0] > UINT16_MAX
	    || cterm->seq_param_int[1] != 1
	    || cterm->seq_param_int[2] == UINT64_MAX
	    || cterm->seq_param_int[3] == UINT64_MAX
	    || cterm->seq_param_int[4] == UINT64_MAX
	    || cterm->seq_param_int[5] == UINT64_MAX
	    || cterm->seq_param_int[2] > cterm->seq_param_int[4]
	    || cterm->seq_param_int[3] > cterm->seq_param_int[5]
	    || cterm->seq_param_int[2] == 0
	    || cterm->seq_param_int[3] == 0
	    || cterm->seq_param_int[4] == 0
	    || cterm->seq_param_int[5] == 0)
		return;
	{
		uint32_t p_left   = (uint32_t)cterm->seq_param_int[3];
		uint32_t p_top    = (uint32_t)cterm->seq_param_int[2];
		uint32_t p_right  = (uint32_t)cterm->seq_param_int[5];
		uint32_t p_bottom = (uint32_t)cterm->seq_param_int[4];
		struct st_console_rect rect;
		struct st_console_cell *cells;
		size_t count;
		uint16_t crc;

		if (p_right > (uint32_t)cterm->width
		    || p_bottom > (uint32_t)cterm->height)
			return;
		rect = (struct st_console_rect){
			.x = (uint32_t)(cterm->x - 1) + p_left - 1,
			.y = (uint32_t)(cterm->y - 1) + p_top - 1,
			.width = p_right - p_left + 1,
			.height = p_bottom - p_top + 1,
		};
		count = (size_t)rect.width * rect.height;
		cells = malloc(count * sizeof(*cells));
		if (cells == NULL)
			return;
		if (st_console_read_cells(cterm->screen.console, &rect, cells,
		    rect.width) == 0) {
			/*
			 * st_console_cell has an explicit zeroed reserved member and a
			 * no-padding layout assertion. The checksum is therefore the raw
			 * row-major modern cell rectangle, including every defined byte.
			 */
			crc = cterm_render_crc16(cells,
			    count * sizeof(*cells), 0);
			cterm_respond_printf(cterm, "\x1bP%u!~%04X\x1b\\",
			    (unsigned)cterm->seq_param_int[0], crc);
		}
		free(cells);
	}
}

void
cterm_handle_decrqm_dec(struct cterminal *cterm, int *speed)	/* CSI ? $ p */
{
	int pm = 0;
	int vidflags;
	(void)speed;
	if (cterm->seq_param_count != 1)
		return;
	cterm_seq_default(cterm, 0, 0);
	vidflags = cterm_render_get_video_flags(cterm);
	switch (cterm->seq_param_int[0]) {
		case 6:
			pm = (cterm->extattr & CTERM_EXTATTR_ORIGINMODE) ? 1 : 2;
			break;
		case 7:
			pm = (cterm->extattr & CTERM_EXTATTR_AUTOWRAP) ? 1 : 2;
			break;
		case 25:
			pm = (cterm->cursor == CTERM_CURSOR_UNDERLINE) ? 1 : 2;
			break;
		case 31:
			pm = (vidflags & ST_CONSOLE_VIDEO_ALT_CHARS) ? 1 : 2;
			break;
		case 32:
			pm = (vidflags & ST_CONSOLE_VIDEO_NO_BRIGHT) ? 1 : 2;
			break;
		case 33:
			pm = (vidflags & ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND) ? 1 : 2;
			break;
		case 34:
			pm = (vidflags & ST_CONSOLE_VIDEO_BLINK_ALT_CHARS) ? 1 : 2;
			break;
		case 35:
			pm = (vidflags & ST_CONSOLE_VIDEO_NO_BLINK) ? 1 : 2;
			break;
		case 67:
			pm = (cterm->extattr & CTERM_EXTATTR_DECBKM) ? 1 : 2;
			break;
		case 69:
			pm = (cterm->extattr & CTERM_EXTATTR_DECLRMM) ? 1 : 2;
			break;
		case 80:
			pm = (cterm->extattr & CTERM_EXTATTR_SXSCROLL) ? 2 : 1;
			break;
		case 2004:
			pm = (cterm->extattr & CTERM_EXTATTR_BRACKETPASTE) ? 1 : 2;
			break;
		case 9:
		case 1000:
		case 1001:
		case 1002:
		case 1003:
		case 1004:
		case 1005:
		case 1006:
		case 1007:
		case 1015:
		case 1016:
			if (cterm->mouse_state_query)
				pm = cterm->mouse_state_query((int)cterm->seq_param_int[0],
				    cterm->mouse_state_query_cbdata) ? 1 : 2;
			break;
	}
	cterm_respond_printf(cterm, "\x1b[?%u;%d$y",
	    (unsigned)cterm->seq_param_int[0], pm);
}

void
cterm_handle_save_dec_modes(struct cterminal *cterm, int *speed)	/* CSI ? s */
{
	int flags;
	int i;
	(void)speed;
	flags = cterm_render_get_video_flags(cterm);
	if (cterm->seq_param_count == 0) {
		cterm->saved_mode_mask |= (CTERM_SAVEMODE_AUTOWRAP
		    | CTERM_SAVEMODE_CURSOR | CTERM_SAVEMODE_ALTCHARS
		    | CTERM_SAVEMODE_NOBRIGHT | CTERM_SAVEMODE_BGBRIGHT
		    | CTERM_SAVEMODE_ORIGIN | CTERM_SAVEMODE_SIXEL_SCROLL
		    | CTERM_SAVEMODE_MOUSE_X10 | CTERM_SAVEMODE_MOUSE_NORMAL
		    | CTERM_SAVEMODE_MOUSE_HIGHLIGHT
		    | CTERM_SAVEMODE_MOUSE_BUTTONTRACK
		    | CTERM_SAVEMODE_MOUSE_ANY | CTERM_SAVEMODE_MOUSE_FOCUS
		    | CTERM_SAVEMODE_MOUSE_UTF8 | CTERM_SAVEMODE_MOUSE_SGR
		    | CTERM_SAVEMODE_MOUSE_ALTSCROLL
		    | CTERM_SAVEMODE_MOUSE_URXVT
		    | CTERM_SAVEMODE_MOUSE_SGR_PIXELS
		    | CTERM_SAVEMODE_DECLRMM | CTERM_SAVEMODE_DECBKM);
		cterm->saved_mode &= ~cterm->saved_mode_mask;
		cterm->saved_mode |= (cterm->extattr & CTERM_EXTATTR_AUTOWRAP) ? CTERM_SAVEMODE_AUTOWRAP : 0;
		cterm->saved_mode |= (cterm->cursor == CTERM_CURSOR_UNDERLINE) ? CTERM_SAVEMODE_CURSOR : 0;
		cterm->saved_mode |= (flags & ST_CONSOLE_VIDEO_ALT_CHARS) ? CTERM_SAVEMODE_ALTCHARS : 0;
		cterm->saved_mode |= (flags & ST_CONSOLE_VIDEO_NO_BRIGHT) ? CTERM_SAVEMODE_NOBRIGHT : 0;
		cterm->saved_mode |= (flags & ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND) ? CTERM_SAVEMODE_BGBRIGHT : 0;
		cterm->saved_mode |= (flags & ST_CONSOLE_VIDEO_BLINK_ALT_CHARS) ? CTERM_SAVEMODE_BLINKALTCHARS : 0;
		cterm->saved_mode |= (flags & ST_CONSOLE_VIDEO_NO_BLINK) ? CTERM_SAVEMODE_NOBLINK : 0;
		cterm->saved_mode |= (cterm->extattr & CTERM_EXTATTR_ORIGINMODE) ? CTERM_SAVEMODE_ORIGIN : 0;
		cterm->saved_mode |= (cterm->extattr & CTERM_EXTATTR_SXSCROLL) ? CTERM_SAVEMODE_SIXEL_SCROLL : 0;
		cterm->saved_mode |= (cterm->mouse_state_query(9, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_X10 : 0);
		cterm->saved_mode |= (cterm->mouse_state_query(1000, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_NORMAL : 0);
		cterm->saved_mode |= (cterm->mouse_state_query(1001, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_HIGHLIGHT : 0);
		cterm->saved_mode |= (cterm->mouse_state_query(1002, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_BUTTONTRACK : 0);
		cterm->saved_mode |= (cterm->mouse_state_query(1003, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_ANY : 0);
		cterm->saved_mode |= (cterm->mouse_state_query(1004, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_FOCUS : 0);
		cterm->saved_mode |= (cterm->mouse_state_query(1005, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_UTF8 : 0);
		cterm->saved_mode |= (cterm->mouse_state_query(1006, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_SGR : 0);
		cterm->saved_mode |= (cterm->mouse_state_query(1007, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_ALTSCROLL : 0);
		cterm->saved_mode |= (cterm->mouse_state_query(1015, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_URXVT : 0);
		cterm->saved_mode |= (cterm->mouse_state_query(1016, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_SGR_PIXELS : 0);
		cterm->saved_mode |= (cterm->extattr & CTERM_EXTATTR_DECLRMM) ? CTERM_SAVEMODE_DECLRMM : 0;
		cterm->saved_mode |= (cterm->extattr & CTERM_EXTATTR_DECBKM) ? CTERM_SAVEMODE_DECBKM : 0;
		cterm_apply_window(cterm);
		return;
	}
	for (i = 0; i < cterm->seq_param_count; i++) {
		cterm_seq_default(cterm, i, 0);
		switch (cterm->seq_param_int[i]) {
			case 6:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_ORIGIN;
				cterm->saved_mode &= ~CTERM_SAVEMODE_ORIGIN;
				cterm->saved_mode |= (cterm->extattr & CTERM_EXTATTR_ORIGINMODE) ? CTERM_SAVEMODE_ORIGIN : 0;
				cterm_apply_window(cterm);
				break;
			case 7:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_AUTOWRAP;
				cterm->saved_mode &= ~CTERM_SAVEMODE_AUTOWRAP;
				cterm->saved_mode |= (cterm->extattr & CTERM_EXTATTR_AUTOWRAP) ? CTERM_SAVEMODE_AUTOWRAP : 0;
				break;
			case 9:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_MOUSE_X10;
				cterm->saved_mode &= ~CTERM_SAVEMODE_MOUSE_X10;
				cterm->saved_mode |= (cterm->mouse_state_query(9, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_X10 : 0);
				break;
			case 25:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_CURSOR;
				cterm->saved_mode &= ~CTERM_SAVEMODE_CURSOR;
				cterm->saved_mode |= (cterm->cursor == CTERM_CURSOR_UNDERLINE) ? CTERM_SAVEMODE_CURSOR : 0;
				break;
			case 31:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_ALTCHARS;
				cterm->saved_mode &= ~CTERM_SAVEMODE_ALTCHARS;
				cterm->saved_mode |= (flags & ST_CONSOLE_VIDEO_ALT_CHARS) ? CTERM_SAVEMODE_ALTCHARS : 0;
				break;
			case 32:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_NOBRIGHT;
				cterm->saved_mode &= ~CTERM_SAVEMODE_NOBRIGHT;
				cterm->saved_mode |= (flags & ST_CONSOLE_VIDEO_NO_BRIGHT) ? CTERM_SAVEMODE_NOBRIGHT : 0;
				break;
			case 33:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_BGBRIGHT;
				cterm->saved_mode &= ~CTERM_SAVEMODE_BGBRIGHT;
				cterm->saved_mode |= (flags & ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND) ? CTERM_SAVEMODE_BGBRIGHT : 0;
				break;
			case 34:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_BLINKALTCHARS;
				cterm->saved_mode &= ~CTERM_SAVEMODE_BLINKALTCHARS;
				cterm->saved_mode |= (flags & ST_CONSOLE_VIDEO_BLINK_ALT_CHARS) ? CTERM_SAVEMODE_BLINKALTCHARS : 0;
				break;
			case 35:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_NOBLINK;
				cterm->saved_mode &= ~CTERM_SAVEMODE_NOBLINK;
				cterm->saved_mode |= (flags & ST_CONSOLE_VIDEO_NO_BLINK) ? CTERM_SAVEMODE_NOBLINK : 0;
				break;
			case 67:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_DECBKM;
				cterm->saved_mode &= ~CTERM_SAVEMODE_DECBKM;
				cterm->saved_mode |= (cterm->extattr & CTERM_EXTATTR_DECBKM) ? CTERM_SAVEMODE_DECBKM : 0;
				break;
			case 69:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_DECLRMM;
				cterm->saved_mode &= ~CTERM_SAVEMODE_DECLRMM;
				cterm->saved_mode |= (cterm->extattr & CTERM_EXTATTR_DECLRMM) ? CTERM_SAVEMODE_DECLRMM : 0;
				break;
			case 80:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_SIXEL_SCROLL;
				cterm->saved_mode &= ~CTERM_SAVEMODE_SIXEL_SCROLL;
				cterm->saved_mode |= (cterm->extattr & CTERM_EXTATTR_SXSCROLL) ? CTERM_SAVEMODE_SIXEL_SCROLL : 0;
				break;
			case 1000:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_MOUSE_NORMAL;
				cterm->saved_mode &= ~CTERM_SAVEMODE_MOUSE_NORMAL;
				cterm->saved_mode |= (cterm->mouse_state_query(1000, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_NORMAL : 0);
				break;
			case 1001:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_MOUSE_HIGHLIGHT;
				cterm->saved_mode &= ~CTERM_SAVEMODE_MOUSE_HIGHLIGHT;
				cterm->saved_mode |= (cterm->mouse_state_query(1001, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_HIGHLIGHT : 0);
				break;
			case 1002:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_MOUSE_BUTTONTRACK;
				cterm->saved_mode &= ~CTERM_SAVEMODE_MOUSE_BUTTONTRACK;
				cterm->saved_mode |= (cterm->mouse_state_query(1002, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_BUTTONTRACK : 0);
				break;
			case 1003:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_MOUSE_ANY;
				cterm->saved_mode &= ~CTERM_SAVEMODE_MOUSE_ANY;
				cterm->saved_mode |= (cterm->mouse_state_query(1003, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_ANY : 0);
				break;
			case 1004:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_MOUSE_FOCUS;
				cterm->saved_mode &= ~CTERM_SAVEMODE_MOUSE_FOCUS;
				cterm->saved_mode |= (cterm->mouse_state_query(1004, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_FOCUS : 0);
				break;
			case 1005:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_MOUSE_UTF8;
				cterm->saved_mode &= ~CTERM_SAVEMODE_MOUSE_UTF8;
				cterm->saved_mode |= (cterm->mouse_state_query(1005, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_UTF8 : 0);
				break;
			case 1006:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_MOUSE_SGR;
				cterm->saved_mode &= ~CTERM_SAVEMODE_MOUSE_SGR;
				cterm->saved_mode |= (cterm->mouse_state_query(1006, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_SGR : 0);
				break;
			case 1007:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_MOUSE_ALTSCROLL;
				cterm->saved_mode &= ~CTERM_SAVEMODE_MOUSE_ALTSCROLL;
				cterm->saved_mode |= (cterm->mouse_state_query(1007, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_ALTSCROLL : 0);
				break;
			case 1015:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_MOUSE_URXVT;
				cterm->saved_mode &= ~CTERM_SAVEMODE_MOUSE_URXVT;
				cterm->saved_mode |= (cterm->mouse_state_query(1015, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_URXVT : 0);
				break;
			case 1016:
				cterm->saved_mode_mask |= CTERM_SAVEMODE_MOUSE_SGR_PIXELS;
				cterm->saved_mode &= ~CTERM_SAVEMODE_MOUSE_SGR_PIXELS;
				cterm->saved_mode |= (cterm->mouse_state_query(1016, cterm->mouse_state_query_cbdata) ? CTERM_SAVEMODE_MOUSE_SGR_PIXELS : 0);
				break;
		}
	}
}

void
cterm_handle_restore_dec_modes(struct cterminal *cterm, int *speed)	/* CSI ? u */
{
	int flags, i;
	uint32_t oldfg, oldbg;
	bool updfg, updbg;
	(void)speed;
	flags = cterm_render_get_video_flags(cterm);
	cterm_render_attribute_colours(cterm, cterm->attr, &oldfg, &oldbg);
	updfg = (oldfg == cterm->fg_color);
	updbg = (oldbg == cterm->bg_color);
	if (cterm->seq_param_count == 0) {
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_ORIGIN) {
			if (cterm->saved_mode & CTERM_SAVEMODE_ORIGIN)
				cterm->extattr |= CTERM_EXTATTR_ORIGINMODE;
			else
				cterm->extattr &= ~CTERM_EXTATTR_ORIGINMODE;
			cterm_apply_window(cterm);
		}
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_AUTOWRAP) {
			if (cterm->saved_mode & CTERM_SAVEMODE_AUTOWRAP)
				cterm->extattr |= CTERM_EXTATTR_AUTOWRAP;
			else
				cterm->extattr &= ~CTERM_EXTATTR_AUTOWRAP;
		}
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_SIXEL_SCROLL) {
			if (cterm->saved_mode & CTERM_SAVEMODE_SIXEL_SCROLL)
				cterm->extattr |= CTERM_EXTATTR_SXSCROLL;
			else
				cterm->extattr &= ~CTERM_EXTATTR_SXSCROLL;
		}
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_CURSOR) {
			cterm->cursor = (cterm->saved_mode & CTERM_SAVEMODE_CURSOR) ? CTERM_CURSOR_UNDERLINE : CTERM_CURSOR_HIDDEN;
			cterm_render_set_cursor(cterm, cterm->cursor);
		}
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_ALTCHARS) {
			if (cterm->saved_mode & CTERM_SAVEMODE_ALTCHARS)
				flags |= ST_CONSOLE_VIDEO_ALT_CHARS;
			else
				flags &= ~ST_CONSOLE_VIDEO_ALT_CHARS;
		}
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_BLINKALTCHARS) {
			if (cterm->saved_mode & CTERM_SAVEMODE_BLINKALTCHARS)
				flags |= ST_CONSOLE_VIDEO_BLINK_ALT_CHARS;
			else
				flags &= ~ST_CONSOLE_VIDEO_BLINK_ALT_CHARS;
		}
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_NOBRIGHT) {
			if (cterm->saved_mode & CTERM_SAVEMODE_NOBRIGHT)
				flags |= ST_CONSOLE_VIDEO_NO_BRIGHT;
			else
				flags &= ~ST_CONSOLE_VIDEO_NO_BRIGHT;
		}
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_NOBLINK) {
			if (cterm->saved_mode & CTERM_SAVEMODE_NOBLINK)
				flags |= ST_CONSOLE_VIDEO_NO_BLINK;
			else
				flags &= ~ST_CONSOLE_VIDEO_NO_BLINK;
		}
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_BGBRIGHT) {
			if (cterm->saved_mode & CTERM_SAVEMODE_BGBRIGHT)
				flags |= ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND;
			else
				flags &= ~ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND;
		}
		cterm_render_set_video_flags(cterm, flags);
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_X10)
			cterm->mouse_state_change(9, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_X10, cterm->mouse_state_change_cbdata);
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_NORMAL)
			cterm->mouse_state_change(1000, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_NORMAL, cterm->mouse_state_change_cbdata);
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_HIGHLIGHT)
			cterm->mouse_state_change(1001, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_HIGHLIGHT, cterm->mouse_state_change_cbdata);
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_BUTTONTRACK)
			cterm->mouse_state_change(1002, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_BUTTONTRACK, cterm->mouse_state_change_cbdata);
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_ANY)
			cterm->mouse_state_change(1003, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_ANY, cterm->mouse_state_change_cbdata);
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_FOCUS)
			cterm->mouse_state_change(1004, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_FOCUS, cterm->mouse_state_change_cbdata);
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_UTF8)
			cterm->mouse_state_change(1005, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_UTF8, cterm->mouse_state_change_cbdata);
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_SGR)
			cterm->mouse_state_change(1006, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_SGR, cterm->mouse_state_change_cbdata);
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_ALTSCROLL)
			cterm->mouse_state_change(1007, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_ALTSCROLL, cterm->mouse_state_change_cbdata);
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_URXVT)
			cterm->mouse_state_change(1015, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_URXVT, cterm->mouse_state_change_cbdata);
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_SGR_PIXELS)
			cterm->mouse_state_change(1016, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_SGR_PIXELS, cterm->mouse_state_change_cbdata);
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_DECBKM) {
			if (cterm->saved_mode & CTERM_SAVEMODE_DECBKM)
				cterm->extattr |= CTERM_EXTATTR_DECBKM;
			else
				cterm->extattr &= ~CTERM_EXTATTR_DECBKM;
		}
		if (cterm->saved_mode_mask & CTERM_SAVEMODE_DECLRMM) {
			if (cterm->saved_mode & CTERM_SAVEMODE_DECLRMM)
				cterm->extattr |= CTERM_EXTATTR_DECLRMM;
			else
				cterm->extattr &= ~CTERM_EXTATTR_DECLRMM;
		}
		goto finish;
	}
	for (i = 0; i < cterm->seq_param_count; i++) {
		cterm_seq_default(cterm, i, 0);
		switch (cterm->seq_param_int[i]) {
			case 6:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_ORIGIN) {
					if (cterm->saved_mode & CTERM_SAVEMODE_ORIGIN)
						cterm->extattr |= CTERM_EXTATTR_ORIGINMODE;
					else
						cterm->extattr &= ~CTERM_EXTATTR_ORIGINMODE;
				}
				cterm_apply_window(cterm);
				break;
			case 7:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_AUTOWRAP) {
					if (cterm->saved_mode & CTERM_SAVEMODE_AUTOWRAP)
						cterm->extattr |= CTERM_EXTATTR_AUTOWRAP;
					else
						cterm->extattr &= ~CTERM_EXTATTR_AUTOWRAP;
				}
				break;
			case 9:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_X10)
					cterm->mouse_state_change(9, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_X10, cterm->mouse_state_change_cbdata);
				break;
			case 25:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_CURSOR) {
					cterm->cursor = (cterm->saved_mode & CTERM_SAVEMODE_CURSOR) ? CTERM_CURSOR_UNDERLINE : CTERM_CURSOR_HIDDEN;
					cterm_render_set_cursor(cterm, cterm->cursor);
				}
				break;
			case 31:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_ALTCHARS) {
					if (cterm->saved_mode & CTERM_SAVEMODE_ALTCHARS)
						flags |= ST_CONSOLE_VIDEO_ALT_CHARS;
					else
						flags &= ~ST_CONSOLE_VIDEO_ALT_CHARS;
					cterm_render_set_video_flags(cterm, flags);
				}
				break;
			case 32:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_NOBRIGHT) {
					if (cterm->saved_mode & CTERM_SAVEMODE_NOBRIGHT)
						flags |= ST_CONSOLE_VIDEO_NO_BRIGHT;
					else
						flags &= ~ST_CONSOLE_VIDEO_NO_BRIGHT;
					cterm_render_set_video_flags(cterm, flags);
				}
				break;
			case 33:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_BGBRIGHT) {
					if (cterm->saved_mode & CTERM_SAVEMODE_BGBRIGHT)
						flags |= ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND;
					else
						flags &= ~ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND;
					cterm_render_set_video_flags(cterm, flags);
				}
				break;
			case 34:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_BLINKALTCHARS) {
					if (cterm->saved_mode & CTERM_SAVEMODE_BLINKALTCHARS)
						flags |= ST_CONSOLE_VIDEO_BLINK_ALT_CHARS;
					else
						flags &= ~ST_CONSOLE_VIDEO_BLINK_ALT_CHARS;
					cterm_render_set_video_flags(cterm, flags);
				}
				break;
			case 35:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_NOBLINK) {
					if (cterm->saved_mode & CTERM_SAVEMODE_NOBLINK)
						flags |= ST_CONSOLE_VIDEO_NO_BLINK;
					else
						flags &= ~ST_CONSOLE_VIDEO_NO_BLINK;
					cterm_render_set_video_flags(cterm, flags);
				}
				break;
			case 67:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_DECBKM) {
					if (cterm->saved_mode & CTERM_SAVEMODE_DECBKM)
						cterm->extattr |= CTERM_EXTATTR_DECBKM;
					else
						cterm->extattr &= ~CTERM_EXTATTR_DECBKM;
				}
				break;
			case 69:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_DECLRMM) {
					if (cterm->saved_mode & CTERM_SAVEMODE_DECLRMM)
						cterm->extattr |= CTERM_EXTATTR_DECLRMM;
					else
						cterm->extattr &= ~CTERM_EXTATTR_DECLRMM;
				}
				break;
			case 80:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_SIXEL_SCROLL) {
					if (cterm->saved_mode & CTERM_SAVEMODE_SIXEL_SCROLL)
						cterm->extattr |= CTERM_EXTATTR_SXSCROLL;
					else
						cterm->extattr &= ~CTERM_EXTATTR_SXSCROLL;
				}
				break;
			case 1000:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_NORMAL)
					cterm->mouse_state_change(1000, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_NORMAL, cterm->mouse_state_change_cbdata);
				break;
			case 1001:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_HIGHLIGHT)
					cterm->mouse_state_change(1001, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_HIGHLIGHT, cterm->mouse_state_change_cbdata);
				break;
			case 1002:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_BUTTONTRACK)
					cterm->mouse_state_change(1002, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_BUTTONTRACK, cterm->mouse_state_change_cbdata);
				break;
			case 1003:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_ANY)
					cterm->mouse_state_change(1003, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_ANY, cterm->mouse_state_change_cbdata);
				break;
			case 1004:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_FOCUS)
					cterm->mouse_state_change(1004, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_FOCUS, cterm->mouse_state_change_cbdata);
				break;
			case 1005:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_UTF8)
					cterm->mouse_state_change(1005, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_UTF8, cterm->mouse_state_change_cbdata);
				break;
			case 1006:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_SGR)
					cterm->mouse_state_change(1006, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_SGR, cterm->mouse_state_change_cbdata);
				break;
			case 1007:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_ALTSCROLL)
					cterm->mouse_state_change(1007, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_ALTSCROLL, cterm->mouse_state_change_cbdata);
				break;
			case 1015:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_URXVT)
					cterm->mouse_state_change(1015, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_URXVT, cterm->mouse_state_change_cbdata);
				break;
			case 1016:
				if (cterm->saved_mode_mask & CTERM_SAVEMODE_MOUSE_SGR_PIXELS)
					cterm->mouse_state_change(1016, cterm->saved_mode & CTERM_SAVEMODE_MOUSE_SGR_PIXELS, cterm->mouse_state_change_cbdata);
				break;
		}
	}
finish:
	if (updfg || updbg) {
		cterm_render_attribute_colours(cterm, cterm->attr, updfg ? &cterm->fg_color : NULL,
		    updbg ? &cterm->bg_color : NULL);
		if (updfg) {
			if (cterm->negative)
				CTERM_FREE_AND_CLEAR(cterm->bg_tc_str);
			else
				CTERM_FREE_AND_CLEAR(cterm->fg_tc_str);
		}
		if (updbg) {
			if (cterm->negative)
				CTERM_FREE_AND_CLEAR(cterm->fg_tc_str);
			else
				CTERM_FREE_AND_CLEAR(cterm->bg_tc_str);
		}
	}
}

void
cterm_handle_decsc(struct cterminal *cterm, int *speed)	/* ESC 7 */
{
	(void)speed;
	CURR_XY(&cterm->save_xpos, &cterm->save_ypos);
}

void
cterm_handle_decrc(struct cterminal *cterm, int *speed)	/* ESC 8 */
{
	(void)speed;
	if (cterm->save_ypos > 0 && cterm->save_ypos <= cterm->height
	    && cterm->save_xpos > 0 && cterm->save_xpos <= cterm->width) {
		if (cterm->save_ypos < CURR_MINY || cterm->save_ypos > CURR_MAXY
		    || cterm->save_xpos < CURR_MINX || cterm->save_xpos > CURR_MAXX)
			return;
		cterm_render_set_position(cterm, cterm->save_xpos, cterm->save_ypos);
	}
}

void
cterm_handle_decsm(struct cterminal *cterm, int *speed)	/* CSI ? h */
{
	int i;
	(void)speed;
	/* Process DEC / XTerm DECSET modes.  Parameters already consumed by a
	 * prior handler in the cascade are skipped. */
	for (i = 0; i < cterm->seq_param_count; i++) {
		if (cterm_seq_param_consumed(cterm, i))
			continue;
		cterm_seq_default(cterm, i, 0);
		switch (cterm->seq_param_int[i]) {
			case 6:
				cterm_clear_lcf(cterm);
				cterm->extattr |= CTERM_EXTATTR_ORIGINMODE;
				cterm_clear_selection(cterm);
				cterm_apply_window(cterm);
				break;
			case 7:
				cterm->extattr |= CTERM_EXTATTR_AUTOWRAP;
				break;
			case 25:
				cterm->cursor = CTERM_CURSOR_UNDERLINE;
				cterm_render_set_cursor(cterm, cterm->cursor);
				break;
			case 67:
				cterm->extattr |= CTERM_EXTATTR_DECBKM;
				break;
			case 69:
				cterm->extattr |= CTERM_EXTATTR_DECLRMM;
				break;
			case 9:
			case 1000:
			case 1001:
			case 1002:
			case 1003:
			case 1004:
			case 1005:
			case 1006:
			case 1007:
			case 1015:
			case 1016:
				if (cterm->mouse_state_change)
					cterm->mouse_state_change((int)cterm->seq_param_int[i], 1,
					    cterm->mouse_state_change_cbdata);
				break;
			case 2004:
				cterm->extattr |= CTERM_EXTATTR_BRACKETPASTE;
				break;
			default:
				/* Unknown to this layer; leave unmarked so future
				 * layers (if any) can claim it. */
				continue;
		}
		cterm_seq_consume_param(cterm, i);
	}
}

void
cterm_handle_decrm(struct cterminal *cterm, int *speed)	/* CSI ? l */
{
	int i;
	(void)speed;
	for (i = 0; i < cterm->seq_param_count; i++) {
		if (cterm_seq_param_consumed(cterm, i))
			continue;
		cterm_seq_default(cterm, i, 0);
		switch (cterm->seq_param_int[i]) {
			case 6:
				cterm_clear_lcf(cterm);
				cterm->extattr &= ~CTERM_EXTATTR_ORIGINMODE;
				cterm_clear_selection(cterm);
				cterm_apply_window(cterm);
				break;
			case 7:
				cterm_clear_lcf(cterm);
				cterm->extattr &= ~CTERM_EXTATTR_AUTOWRAP;
				break;
			case 25:
				cterm->cursor = CTERM_CURSOR_HIDDEN;
				cterm_render_set_cursor(cterm, cterm->cursor);
				break;
			case 67:
				cterm->extattr &= ~CTERM_EXTATTR_DECBKM;
				break;
			case 69:
				cterm->extattr &= ~CTERM_EXTATTR_DECLRMM;
				break;
			case 9:
			case 1000:
			case 1001:
			case 1002:
			case 1003:
			case 1004:
			case 1005:
			case 1006:
			case 1007:
			case 1015:
			case 1016:
				if (cterm->mouse_state_change)
					cterm->mouse_state_change((int)cterm->seq_param_int[i], 0,
					    cterm->mouse_state_change_cbdata);
				break;
			case 2004:
				cterm->extattr &= ~CTERM_EXTATTR_BRACKETPASTE;
				break;
			default:
				continue;
		}
		cterm_seq_consume_param(cterm, i);
	}
}

void
cterm_handle_decslrm(struct cterminal *cterm, int *speed)	/* CSI Pl ; Pr s */
{
	int col, max_col;
	(void)speed;
	cterm_seq_default(cterm, 0, ABS_MINX);
	cterm_seq_default(cterm, 1, ABS_MAXX);
	col = (int)cterm->seq_param_int[0];
	if (col == 0)
		col = cterm->left_margin;
	max_col = (int)cterm->seq_param_int[1];
	if (max_col == 0)
		max_col = cterm->right_margin;
	if (col >= ABS_MINX && max_col > col && max_col <= ABS_MAXX) {
		cterm->left_margin = col;
		cterm->right_margin = max_col;
		cterm_clear_selection(cterm);
		cterm_apply_window(cterm);
		cterm_render_set_position(cterm, CURR_MINX, CURR_MINY);
	}
}

void
cterm_handle_decstbm(struct cterminal *cterm, int *speed)	/* CSI Pt ; Pb r */
{
	int row, max_row;
	(void)speed;
	cterm_clear_lcf(cterm);
	cterm_seq_default(cterm, 0, 1);
	cterm_seq_default(cterm, 1, cterm->height);
	row = (int)cterm->seq_param_int[0];
	max_row = (int)cterm->seq_param_int[1];
	if (row >= ABS_MINY && max_row > row && max_row <= ABS_MAXY) {
		cterm->top_margin = row;
		cterm->bottom_margin = max_row;
		cterm_clear_selection(cterm);
		cterm_apply_window(cterm);
		cterm_render_set_position(cterm, CURR_MINX, CURR_MINY);
	}
}

/* DECSSDT — Select Status Display Type (VT320+): CSI Ps $ ~
 *   Ps=0  no status line
 *   Ps=1  indicator (default; SyncTERM's native clock/state row)
 *   Ps=2  host-writable status line
 * When dispatched on a sub-cterm, bubbles to the parent so the main
 * terminal owns the state.  Out-of-range Ps is silently ignored (match
 * xterm).  A change fires status_display_cb for the host application
 * to resize the main display and create/destroy the sub-cterm. */
void
cterm_handle_decssdt(struct cterminal *cterm, int *speed)
{
	int old, ps;

	if (cterm->parent) {
		cterm->parent->seq_param_count = cterm->seq_param_count;
		cterm->parent->seq_param_int[0] = cterm->seq_param_int[0];
		cterm_handle_decssdt(cterm->parent, speed);
		return;
	}
	(void)speed;
	cterm_seq_default(cterm, 0, 1);
	ps = (int)cterm->seq_param_int[0];
	if (ps < 0 || ps > 2)
		return;
	if (ps == cterm->status_display_type)
		return;
	old = cterm->status_display_type;
	if (cterm->status_display_active == 1)
		cterm->status_display_active = 0;
	cterm->status_display_type = ps;
	if (cterm->status_display_cb)
		cterm->status_display_cb(cterm, old, ps, cterm->status_display_cbdata);
}

/* DECSASD — Select Active Status Display (VT320+): CSI Ps $ }
 *   Ps=0  main display receives writes (default)
 *   Ps=1  status display receives writes (valid only when DECSSDT=2)
 * Ps=1 is silently ignored until DECSSDT=2 has created a status subterminal.
 * Bubbles to the parent on subterminal dispatch. */
void
cterm_handle_decsasd(struct cterminal *cterm, int *speed)
{
	int ps;

	if (cterm->parent) {
		cterm->parent->seq_param_count = cterm->seq_param_count;
		cterm->parent->seq_param_int[0] = cterm->seq_param_int[0];
		cterm_handle_decsasd(cterm->parent, speed);
		return;
	}
	(void)speed;
	cterm_seq_default(cterm, 0, 0);
	ps = (int)cterm->seq_param_int[0];
	if (ps < 0 || ps > 1)
		return;
	if (ps == 1 && cterm->status_display_type != 2)
		return;
	cterm->status_display_active = ps;
}

/* ========================================================================
 * DCS string parsers — Sixel + DECDMAC
 *
 * Both are DEC extensions transported inside the ECMA-48 DCS framing
 * (ESC P … ESC \).  The generic string accumulator in cterm.c captures
 * the payload into cterm->strbuf; cterm_dec_dcs_byte() is invoked once
 * per byte so these parsers can work incrementally (sixel image data
 * can be megabytes; DECDMAC macro bodies similarly).  On ST the final
 * pass runs via cterm_dec_dcs_finish() below.
 * ======================================================================== */

/* Scan s for the length of a prefix of bytes in [min..max] excluding
 * `exclude`.  Used by the sixel/macro intro parsers to locate the final
 * byte past a run of parameter digits. */
static size_t
range_span_exclude(char exclude, const char *s, char min, char max)
{
	size_t ret = 0;

	while ((s[ret] >= min && s[ret] <= max) && s[ret] != exclude)
		ret++;
	return ret;
}

static bool
sixel_numeric_command_terminated(const char *p, const char *end, bool finish, bool semicolons)
{
	for (p++; p <= end; p++) {
		if (*p >= '0' && *p <= '9')
			continue;
		if (semicolons && *p == ';')
			continue;
		return true;
	}
	return finish;
}

static void
sixel_set_mask_range(struct st_pixel_mask *mask, int start, int count)
{
	int end;
	int byte;
	int edge_bits;

	if (count <= 0)
		return;

	end = start + count;
	byte = start / 8;
	if (end / 8 > byte)
		memset(&mask->bits[byte], 0xff, end / 8 - byte);
	edge_bits = end % 8;
	if (edge_bits)
		mask->bits[end / 8] |= 0xff << (8 - edge_bits);
}

static void
sixel_init_current_line(struct cterminal *cterm)
{
	int fill_height;
	int fill_width;
	int i;
	int j;
	int row_pos;

	if (cterm->sx_pixels == NULL || cterm->sx_mask == NULL || cterm->sx_mask->bits == NULL)
		return;

	memset(cterm->sx_mask->bits, 0, (cterm->sx_mask->width * cterm->sx_mask->height + 7) / 8);

	if (cterm->sx_trans || cterm->sx_width <= 0 || cterm->sx_height <= 0)
		return;

	fill_height = cterm->sx_height > 6 ? 6 : cterm->sx_height;
	fill_width = cterm->sx_width * cterm->sx_ih;
	if (fill_width > (int)cterm->sx_pixels->width - cterm->sx_left)
		fill_width = (int)cterm->sx_pixels->width - cterm->sx_left;
	if (fill_width <= 0)
		return;

	for (i = 0; i < fill_height * cterm->sx_iv; i++) {
		row_pos = i * cterm->sx_pixels->width + cterm->sx_left;
		for (j = 0; j < fill_width; j++) {
			cterm->sx_pixels->primary[row_pos + j] = cterm->sx_bg;
		}
		sixel_set_mask_range(cterm->sx_mask, row_pos, fill_width);
	}

	if (cterm->sx_left + fill_width - 1 > cterm->sx_row_max_x)
		cterm->sx_row_max_x = cterm->sx_left + fill_width - 1;
}

static void
sixel_present_current_line(struct cterminal *cterm)
{
	struct st_pixel_blit blit;

	if (cterm->sx_row_max_x < cterm->sx_left)
		return;
	blit = (struct st_pixel_blit){
		.source_x = (uint32_t)cterm->sx_left,
		.width = (uint32_t)(cterm->sx_row_max_x - cterm->sx_left + 1),
		.height = (uint32_t)(6 * cterm->sx_iv),
		.destination_x = cterm->sx_left,
		.destination_y = cterm->sx_y,
		.scale_x = 1,
		.scale_y = 1,
		.mask_x = (uint32_t)cterm->sx_left,
	};
	cterm_render_blit_pixels(cterm, cterm->sx_pixels, cterm->sx_mask,
	    &blit);
}

void cterm_parse_sixel_string(struct cterminal *cterm, bool finish)
{
	char *p = cterm->strbuf;
	char *end;
	int i, j, k;
	int pos;
	uint32_t screen_width;
	uint32_t screen_height;
	uint32_t cell_width;
	uint32_t cell_height;
	int palette_offset = 0;

	if (st_console_cell_pixel_size(cterm->screen.console, &cell_width,
	    &cell_height) != 0 || cell_width == 0 || cell_height == 0)
		return;
	if (st_console_size(cterm->screen.console, &screen_width,
	    &screen_height) != 0)
		return;

	if (cterm->screen.capabilities & ST_CONSOLE_CAP_EXTENDED_PALETTE)
		palette_offset = 16;

	if (cterm->strbuflen == 0) {
		if (finish)
			goto all_done;
		return;
	}

	end = p+cterm->strbuflen-1;

	/*
	 * TODO: Sixel interaction with scrolling margins...
	 * This one is interesting because there is no real terminal
	 * which supported left/right margins *and* sixel graphics.
	 */
	while (p <= end) {
		if (*p >= '?' && *p <= '~') {
			unsigned data = *p - '?';

			cterm->sx_pixels_sent = 1;
			if (cterm->sx_pixels == NULL) {
				cterm->sx_pixels = malloc(sizeof(struct st_pixel_buffer));
				if (cterm->sx_pixels != NULL) {
					cterm->sx_pixels->primary = malloc(sizeof(cterm->sx_pixels->primary[0]) * cterm->sx_iv * (int)(screen_width * cell_width) * 6);
					if (cterm->sx_pixels->primary != NULL) {
						cterm->sx_pixels->alternate = NULL;
						cterm->sx_pixels->width = (int)(screen_width * cell_width);
						cterm->sx_pixels->height = cterm->sx_iv * 6;
						cterm->sx_mask = malloc(sizeof(struct st_pixel_mask));
						if (cterm->sx_mask != NULL) {
							cterm->sx_mask->width = cterm->sx_pixels->width;
							cterm->sx_mask->height = cterm->sx_pixels->height;
							cterm->sx_mask->bits = malloc((cterm->sx_iv * 6 * (int)(screen_width * cell_width) + 7)/8);
							if (cterm->sx_mask->bits != NULL)
								memset(cterm->sx_mask->bits, 0, (cterm->sx_iv * 6 * (int)(screen_width * cell_width) + 7)/8);
							else {
								CTERM_FREE_AND_CLEAR(cterm->sx_mask);
								free(cterm->sx_pixels->primary);
								CTERM_FREE_AND_CLEAR(cterm->sx_pixels);
							}
						}
						else {
							free(cterm->sx_pixels->primary);
							CTERM_FREE_AND_CLEAR(cterm->sx_pixels);
						}
					}
					else {
						CTERM_FREE_AND_CLEAR(cterm->sx_pixels);
					}
				}
			}
			if (cterm->sx_pixels == NULL) {
				cterm_log(cterm, ST_CTERM_LOG_ERROR, "Unable to allocate sixel data");
				return;
			}
			/* sx_pixels->width is uint32_t; the sum of our int coords
			 * can't be negative in well-formed sixel data, but casting
			 * to uint32_t makes any out-of-band negative wrap to a huge
			 * value that fails the <= check (safer than using the
			 * negative as a pixel index). */
			if (cterm->sx_x == cterm->sx_left && cterm->sx_height && cterm->sx_width && cterm->sx_first_pass && (uint32_t)(cterm->sx_x + cterm->sx_ih) <= cterm->sx_pixels->width) {
				/* Fill in the background of the line */
				for (i = 0; i < (cterm->sx_height > 6 ? 6 : cterm->sx_height); i++) {
					for (j = 0; j < cterm->sx_iv; j++) {
						for (k = 0; k < cterm->sx_ih; k++) {
							pos = i * cterm->sx_iv * cterm->sx_pixels->width + j * cterm->sx_pixels->width + cterm->sx_x + k;
							cterm->sx_pixels->primary[pos] = cterm->sx_bg;
							cterm->sx_mask->bits[pos/8] |= (0x80 >> (pos % 8));
						}
					}
				}
			}
			if (cterm->sx_x + cterm->sx_ih <= (int)(screen_width * cell_width)) {
				for (i=0; i<6; i++) {
					if (data & (1<<i)) {
						for (j = 0; j < cterm->sx_iv; j++) {
							for (k = 0; k < cterm->sx_ih; k++) {
								pos = i * cterm->sx_iv * cterm->sx_pixels->width + j * cterm->sx_pixels->width + cterm->sx_x + k;
								cterm->sx_pixels->primary[pos] = cterm->sx_fg;
								cterm->sx_mask->bits[pos/8] |= (0x80 >> (pos % 8));
							}
						}
					}
					else {
						if (cterm->sx_first_pass && !cterm->sx_trans) {
							for (j = 0; j < cterm->sx_iv; j++) {
								for (k = 0; k < cterm->sx_ih; k++) {
									pos = i * cterm->sx_iv * cterm->sx_pixels->width + j * cterm->sx_pixels->width + cterm->sx_x + k;
									cterm->sx_pixels->primary[pos] = cterm->sx_bg;
									cterm->sx_mask->bits[pos/8] |= (0x80 >> (pos % 8));
								}
							}
						}
						else {
							for (j = 0; j < cterm->sx_iv; j++) {
								for (k = 0; k < cterm->sx_ih; k++) {
									pos = i * cterm->sx_iv * cterm->sx_pixels->width + j * cterm->sx_pixels->width + cterm->sx_x + k;
									if (cterm->sx_first_pass)
										cterm->sx_mask->bits[pos/8] &= ~(0x80 >> (pos % 8));
								}
							}
						}
					}
				}
				if (cterm->sx_x + cterm->sx_ih - 1 > cterm->sx_row_max_x)
					cterm->sx_row_max_x = cterm->sx_x + cterm->sx_ih - 1;
			}

			cterm->sx_x+=cterm->sx_ih;
			if (cterm->sx_repeat)
				cterm->sx_repeat--;
			if (!cterm->sx_repeat)
				p++;
		}
		else {
			switch(*p) {
				case '"':	// Raster Attributes
					if (!sixel_numeric_command_terminated(p, end, finish, true))
						goto keep_tail;
					if (!cterm->sx_pixels_sent) {
						/* Parse through strtoul (which rejects `-`), then
						 * narrow to int. The clamp block below caps each
						 * value to screen dimensions, so the narrowing is
						 * semantically safe. */
						p++;
						cterm->sx_iv = (int)strtoul(p, &p, 10);
						cterm->sx_height = cterm->sx_width = 0;
						if (*p == ';') {
							p++;
							cterm->sx_ih = (int)strtoul(p, &p, 10);
						}
						if (*p == ';') {
							p++;
							cterm->sx_width = (int)strtoul(p, &p, 10);
						}
						if (*p == ';') {
							p++;
							cterm->sx_height = (int)strtoul(p, &p, 10);
						}
						{
							int max_w = (int)(screen_width * cell_width);
							int max_h = (int)(screen_height * cell_height);
							if (cterm->sx_iv < 1)
								cterm->sx_iv = 1;
							if (cterm->sx_iv > max_h)
								cterm->sx_iv = max_h;
							if (cterm->sx_ih < 1)
								cterm->sx_ih = 1;
							if (cterm->sx_ih > max_w)
								cterm->sx_ih = max_w;
						}
						sixel_init_current_line(cterm);
					}
					else
						p++;
					break;
				case '!':	// Repeat
					if (!sixel_numeric_command_terminated(p, end, finish, false))
						goto keep_tail;
					p++;
					if (!*p)
						break;
					/* Parse wide, clamp, narrow. sx_repeat is int. */
					cterm->sx_repeat = (int)strtoul(p, &p, 10);
					if (cterm->sx_repeat > 0x7fff || cterm->sx_repeat < 0)
						cterm->sx_repeat = 0x7fff;
					break;
				case '#':	// Colour Introducer
					if (!sixel_numeric_command_terminated(p, end, finish, true))
						goto keep_tail;
					p++;
					if (!*p)
						break;
					cterm->sx_fg = strtoul(p, &p, 10) + CTERM_PALETTE_SIZE + palette_offset;
					/* Do we want to redefine it while we're here? */
					if (*p == ';') {
						unsigned long t,r,g,b;

						p++;
						r=g=b=0;
						t = strtoul(p, &p, 10);
						if (*p == ';') {
							p++;
							r = strtoul(p, &p, 10);
						}
						if (*p == ';') {
							p++;
							g = strtoul(p, &p, 10);
						}
						if (*p == ';') {
							p++;
							b = strtoul(p, &p, 10);
						}
						if (t == 2) {	// Only support RGB
							/* Sixel RGB components are percentages 0-100;
							 * clamp defensively then scale into the 16-bit
							 * palette range. The clamp also makes the
							 * final (uint16_t) cast guaranteed safe. */
							if (r > 100) r = 100;
							if (g > 100) g = 100;
							if (b > 100) b = 100;
							cterm_setpalette(cterm, cterm->sx_fg,
							    (uint16_t)(UINT16_MAX * r / 100),
							    (uint16_t)(UINT16_MAX * g / 100),
							    (uint16_t)(UINT16_MAX * b / 100));
						}
					}
					break;
				case '$':	// Graphics Carriage Return
					cterm->sx_x = cterm->sx_left;
					cterm->sx_first_pass = 0;
					p++;
					break;
				case '-':	// Graphics New Line
					{
						int max_row = TERM_MAXY;
						cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_CURR, &max_row, NULL);
						sixel_present_current_line(cterm);
						cterm->sx_row_max_x = 0;

						if ((!(cterm->extattr & CTERM_EXTATTR_SXSCROLL)) && (((cterm->sx_y + 6 * cterm->sx_iv) + 6*cterm->sx_iv - 1) >= (cterm->y + max_row - 1) * (int)cell_height)) {
							p++;
							break;
						}

						cterm->sx_x = cterm->sx_left;
						cterm->sx_y += 6*cterm->sx_iv;
						if (cterm->sx_height)
							cterm->sx_height -= cterm->sx_height > 6 ? 6 : cterm->sx_height;
						while ((cterm->sx_y + 6 * cterm->sx_iv - 1) >= (cterm->y + max_row - 1) * (int)cell_height) {
							cterm_cond_scrollup(cterm);
							cterm->sx_y -= cell_height;
						}
						cterm->sx_first_pass = 1;
						sixel_init_current_line(cterm);
						p++;
					}
					break;
				default:
					p++;
			}
		}
	}
	cterm->strbuflen = 0;
	if (finish)
		goto all_done;
	return;

keep_tail:
	if (p > cterm->strbuf) {
		cterm->strbuflen = (size_t)(end - p + 1);
		memmove(cterm->strbuf, p, cterm->strbuflen);
		cterm->strbuf[cterm->strbuflen] = 0;
	}
	return;

all_done:
	if (cterm->sx_row_max_x) {
		sixel_present_current_line(cterm);
	}

	(void)cterm_render_set_hold_update(cterm, cterm->sx_hold_update);

	/* Finish off the background */
	cterm->sx_x = cterm->sx_left;
	cterm->sx_y += 6 * cterm->sx_iv;
	if (cterm->sx_height)
		cterm->sx_height -= cterm->sx_height > 6 ? 6 : cterm->sx_height;

	if (cterm->sx_height && cterm->sx_width) {
		struct st_pixel_buffer px;

		px.primary = malloc(sizeof(px.primary[0])*cterm->sx_width*cterm->sx_height*cterm->sx_iv*cterm->sx_ih);
		if (px.primary) {
			struct st_pixel_blit blit;

			px.alternate = NULL;
			px.height = cterm->sx_height;
			px.width = cterm->sx_width;
			for (i = 0; i<cterm->sx_height*cterm->sx_iv; i++) {
				for (j = 0; j < cterm->sx_width*cterm->sx_ih; j++)
					px.primary[i*cterm->sx_width*cterm->sx_ih + j] = cterm->sx_bg;
			}
			blit = (struct st_pixel_blit){
				.width = (uint32_t)cterm->sx_width,
				.height = (uint32_t)cterm->sx_height,
				.destination_x = cterm->sx_x,
				.destination_y = cterm->sx_y,
				.scale_x = 1,
				.scale_y = 1,
			};
			cterm_render_blit_pixels(cterm, &px, NULL, &blit);
			free(px.primary);
		}
		else {
			cterm_log(cterm, ST_CTERM_LOG_ERROR, "Unable to allocate sixel data");
		}
	}

	if (cterm->extattr & CTERM_EXTATTR_SXSCROLL) {
		cterm->sx_x = cterm->sx_x / cell_width + 1;
		cterm->sx_x -= (cterm->x - 1);

		cterm->sx_y = (cterm->sx_y - 1) / cell_height + 1;
		cterm->sx_y -= (cterm->y - 1);

		cterm_set_position(cterm, cterm->sx_x, cterm->sx_y);
	}
	else {
		cterm_set_position(cterm, cterm->sx_start_x, cterm->sx_start_y);
	}
	cterm->cursor = cterm->sx_orig_cursor;
	cterm_render_set_cursor(cterm, cterm->cursor);
	cterm->sixel = SIXEL_INACTIVE;
	if (cterm->sx_pixels)
		CTERM_FREE_AND_CLEAR(cterm->sx_pixels->primary);
	if (cterm->sx_mask)
		CTERM_FREE_AND_CLEAR(cterm->sx_mask->bits);
	CTERM_FREE_AND_CLEAR(cterm->sx_pixels);
	CTERM_FREE_AND_CLEAR(cterm->sx_mask);
}

static int
is_hex(char ch)
{
	if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))
		return 1;
	return 0;
}

static int
get_hexstrlen(char *str, char *end)
{
	int ret = 0;

	for (;str <= end; str++) {
		if (is_hex(*str)) {
			if (str == end)
				return -1;
			if (!is_hex(*(str+1)))
				return -1;
			ret++;
			str++;
		}
		else
			return ret;
	}
	return ret;
}

static int
nibble_val(char ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'a' && ch <= 'f')
		return (ch - 'a') + 10;
	if (ch >= 'A' && ch <= 'F')
		return (ch - 'A') + 10;
	return -1;
}

static void
get_hexstr(char *str, char *end, char *out)
{
	for (;str <= end; str++) {
		if (is_hex(*str)) {
			if (str == end)
				return;
			if (!is_hex(*(str+1)))
				return;
			*(out++) = (char)(nibble_val(*str) << 4 | nibble_val(*(str + 1)));
			str++;
		}
		else
			return;
	}
}

#define MAX_MACRO_LEN 0xfffff
void
cterm_parse_macro_string(struct cterminal *cterm, bool finish)
{
	char *p = cterm->strbuf;
	char *end = NULL;
	int i;

	if (cterm->strbuflen == 0) {
		if (finish)
			goto all_done;
		return;
	}

	end = p+cterm->strbuflen-1;

	if (*end >= '\x08' && *end <= '\x0d') {
		cterm->strbuflen--;
		if (finish)
			goto all_done;
		return;
	}
	if (cterm->macro_encoding == MACRO_ENCODING_ASCII) {
		unsigned char byte = (unsigned char)*end;
		if ((byte >= 0x20 && byte <= 0x7e) || byte >= 0xa0) {
			if (finish)
				goto all_done;
			return;
		}
	}
	if (cterm->macro_encoding == MACRO_ENCODING_HEX &&
	    (is_hex(*end) || (*end == '!') || (*end == ';'))) {
		if (finish)
			goto all_done;
		return;
	}

	cterm->macro = MACRO_INACTIVE;
	return;

all_done:
	if (cterm->macro_del == MACRO_DELETE_ALL) {
		for (i = 0; i < (int)(sizeof(cterm->macros)
		    / sizeof(cterm->macros[0])); i++) {
			CTERM_FREE_AND_CLEAR(cterm->macros[i]);
			cterm->macro_lens[i] = 0;
		}
	}
	else {
		CTERM_FREE_AND_CLEAR(cterm->macros[cterm->macro_num]);
		cterm->macro_lens[cterm->macro_num] = 0;
	}
	if (cterm->strbuflen == 0)
		return;
	if (cterm->strbuflen > MAX_MACRO_LEN)
		return;
	if (cterm->macro_encoding == MACRO_ENCODING_ASCII) {
		cterm->macros[cterm->macro_num] = malloc(cterm->strbuflen + 1);
		if (cterm->macros[cterm->macro_num]) {
			cterm->macro_lens[cterm->macro_num] = cterm->strbuflen;
			memcpy(cterm->macros[cterm->macro_num], cterm->strbuf, cterm->strbuflen);
			cterm->macros[cterm->macro_num][cterm->strbuflen] = 0;
		}
	}
	else {
		// Hex string...
		int plen;
		unsigned long ul;
		size_t mlen = 0;
		char *out;

		if (end == NULL)
			return;
		// First, calculate the required length...
		for (p = cterm->strbuf; p <= end;) {
			if (*p == '!') {
				if (p == end)
					return;
				p++;
				if (p == end)
					return;
				if (*p == ';')
					ul = 1;
				else {
					if (memchr(p, ';', cterm->strbuflen - (p - cterm->strbuf)) == NULL)
						return;
					ul = strtoul(p, &p, 10);
					if (*p != ';')
						return;
					if (ul > MAX_MACRO_LEN)
						return;
					p++;
				}
				plen = get_hexstrlen(p, end);
				if (plen < 0)
					return;
				if (plen > MAX_MACRO_LEN)
					return;
				p += plen * 2;
				mlen += ul * plen;
				if (mlen > MAX_MACRO_LEN)
					return;
				if (p <= end) {
					if (*p == ';')
						p++;
					else
						return;
				}
			}
			else {
				plen = get_hexstrlen(p, end);
				if (plen < 0)
					return;
				if (plen > MAX_MACRO_LEN)
					return;
				p += plen * 2;
				mlen += plen;
				if (mlen > MAX_MACRO_LEN)
					return;
			}
		}
		cterm->macros[cterm->macro_num] = malloc(mlen + 1);
		if (cterm->macros[cterm->macro_num] == NULL)
			return;
		cterm->macro_lens[cterm->macro_num] = mlen;
		out = cterm->macros[cterm->macro_num];
		for (p = cterm->strbuf; p <= end;) {
			if (*p == '!') {
				p++;
				if (*p == ';')
					ul = 1;
				else {
					ul = strtoul(p, &p, 10);
					if (ul > MAX_MACRO_LEN) {
						CTERM_FREE_AND_CLEAR(cterm->macros[cterm->macro_num]);
						return;
					}
					p++;
				}
				plen = get_hexstrlen(p, end);
				if (plen < 0) {
					CTERM_FREE_AND_CLEAR(cterm->macros[cterm->macro_num]);
					return;
				}
				if (plen > MAX_MACRO_LEN) {
					CTERM_FREE_AND_CLEAR(cterm->macros[cterm->macro_num]);
					return;
				}
				for (unsigned long j = 0; j < ul; j++) {
					get_hexstr(p, end, out);
					out += plen;
				}
				p += plen * 2;
				if (p <= end && *p == ';')
					p++;
			}
			else {
				plen = get_hexstrlen(p, end);
				if (plen < 0) {
					CTERM_FREE_AND_CLEAR(cterm->macros[cterm->macro_num]);
					return;
				}
				if (plen > MAX_MACRO_LEN) {
					CTERM_FREE_AND_CLEAR(cterm->macros[cterm->macro_num]);
					return;
				}
				get_hexstr(p, end, out);
				out += plen;
				p += plen * 2;
			}
		}
	}
}

void cterm_parse_sixel_intro(struct cterminal *cterm)
{
	size_t i;

	if (cterm->sixel != SIXEL_POSSIBLE)
		return;

	i = range_span_exclude(':', cterm->strbuf, '0', ';');

	if (i >= cterm->strbuflen)
		return;

	if (cterm->strbuf[i] == 'q') {
		int ratio, hgrid;
		uint32_t cell_width;
		uint32_t cell_height;
		char *p;

		cterm->sixel = SIXEL_STARTED;
		cterm->sx_repeat = 0;
		cterm->sx_pixels_sent = 0;
		cterm->sx_first_pass = 1;
		cterm->sx_height = 0;
		cterm->sx_width = 0;
		cterm->sx_hold_update = cterm_render_hold_update(cterm);
		(void)cterm_render_set_hold_update(cterm, 0);

		if (st_console_cell_pixel_size(cterm->screen.console, &cell_width,
		    &cell_height) != 0 || cell_width == 0 || cell_height == 0) {
			cterm->sixel = SIXEL_INACTIVE;
			return;
		}
		cterm_render_attribute_colours(cterm, cterm->attr, &cterm->sx_fg,
		    &cterm->sx_bg);
		if (cterm->extattr & CTERM_EXTATTR_SXSCROLL) {
			TERM_XY(&cterm->sx_start_x, &cterm->sx_start_y);
			cterm->sx_left = cterm->sx_x = (cterm->sx_start_x - 1) * cell_width;
			cterm->sx_y = (cterm->sx_start_y - 1) * cell_height;
		}
		else {
			cterm->sx_x = cterm->sx_left = cterm->sx_y = 0;
			TERM_XY(&cterm->sx_start_x, &cterm->sx_start_y);
		}
		cterm->sx_orig_cursor = cterm->cursor;
		cterm->cursor = CTERM_CURSOR_HIDDEN;
		cterm_render_set_cursor(cterm, cterm->cursor);
		cterm_set_position(cterm, TERM_MINX, TERM_MINY);
		(void)cterm_render_set_hold_update(cterm, 1);
		cterm->sx_trans = hgrid = 0;
		ratio = strtoul(cterm->strbuf, &p, 10);
		if (*p == ';') {
			p++;
			cterm->sx_trans = (int)strtoul(p, &p, 10);
		}
		if (*p == ';') {
			p++;
			hgrid = strtoul(p, &p, 10);
		}
		switch (ratio) {
			default:
			case 0:
			case 1:
				cterm->sx_iv = 2;
				cterm->sx_ih = 1;
				break;
			case 2:
				cterm->sx_iv = 5;
				cterm->sx_ih = 1;
				break;
			case 3:
			case 4:
				cterm->sx_iv = 3;
				cterm->sx_ih = 1;
				break;
			case 5:
			case 6:
				cterm->sx_iv = 2;
				cterm->sx_ih = 1;
				break;
			case 7:
			case 8:
			case 9:
				cterm->sx_iv = 1;
				cterm->sx_ih = 1;
				break;
		}
		cterm->strbuflen = 0;
	}
	else if (cterm->strbuf[i] != 'q')
		cterm->sixel = SIXEL_INACTIVE;
}

void cterm_parse_macro_intro(struct cterminal *cterm)
{
	size_t i;

	if (cterm->macro != MACRO_POSSIBLE)
		return;

	i = range_span_exclude(':', cterm->strbuf, '0', ';');

	if (i >= cterm->strbuflen)
		return;

	if (cterm->strbuf[i] != '!') {
		cterm->macro = MACRO_INACTIVE;
		return;
	}
	i++;
	if (i >= cterm->strbuflen)
		return;

	if (cterm->strbuf[i] == 'z') {
		char *p;
		unsigned long res;

		// Parse parameters...
		cterm->macro_num = -1;
		cterm->macro_del = MACRO_DELETE_OLD;
		cterm->macro_encoding = MACRO_ENCODING_ASCII;
		res = strtoul(cterm->strbuf, &p, 10);
		if (res <= 63)
			cterm->macro_num = (int)res;
		if (*p == ';') {
			p++;
			res = strtoul(p, &p, 10);
			if (res <= 1)
				cterm->macro_del = (int)res;
			else
				cterm->macro_del = -1;
		}
		if (*p == ';') {
			p++;
			res = strtoul(p, &p, 10);
			if (res <= 1)
				cterm->macro_encoding = (int)res;
			else
				cterm->macro_encoding = -1;
		}
		if (cterm->macro_num < 0 || cterm->macro_num > 63)
			cterm->macro = MACRO_INACTIVE;
		else if (cterm->macro_del < 0 || cterm->macro_del > 1)
			cterm->macro = MACRO_INACTIVE;
		else if (cterm->macro_encoding < 0 || cterm->macro_encoding > 1)
			cterm->macro = MACRO_INACTIVE;
		else {
			cterm->macro = MACRO_STARTED;
			cterm->strbuflen = 0;
		}
	}
	else if (cterm->strbuf[i] != 'z')
		cterm->macro = MACRO_INACTIVE;
}

/* ------------------------------------------------------------------------
 * cterm_dec_dcs_byte — called by the generic command-string accumulator
 * in cterm.c after each byte is appended to cterm->strbuf.  Drives the
 * two incremental-parse state machines: sixel (SIXEL_POSSIBLE →
 * SIXEL_STARTED after the 'q' final byte, then streaming image data)
 * and DECDMAC (MACRO_POSSIBLE → MACRO_STARTED after "!z", then
 * streaming ASCII/hex body).  Buffered DCS strings (DECRQSS, CTerm:Font:)
 * need no per-byte work and fall through.
 * ------------------------------------------------------------------------ */
void
cterm_dec_dcs_byte(struct cterminal *cterm)
{
	switch (cterm->sixel) {
		case SIXEL_STARTED:
			cterm_parse_sixel_string(cterm, false);
			break;
		case SIXEL_POSSIBLE:
			cterm_parse_sixel_intro(cterm);
			break;
	}
	switch (cterm->macro) {
		case MACRO_STARTED:
			cterm_parse_macro_string(cterm, false);
			break;
		case MACRO_POSSIBLE:
			cterm_parse_macro_intro(cterm);
			break;
	}
}

/* ------------------------------------------------------------------------
 * cterm_dec_dcs_begin — DEC-specific setup at DCS start
 *
 * The DCS framing is ECMA-48, but everything we actually do inside it
 * is a DEC (or DEC-family) extension: sixel, DECDMAC, DECRQSS, plus
 * the SyncTERM CTerm:Font extension.  The generic cterm_handle_dcs_start
 * in cterm.c calls us so we can prime the sixel / macro detectors in
 * SIXEL_POSSIBLE / MACRO_POSSIBLE state; if the incoming DCS isn't one
 * of those they fall through to SIXEL_INACTIVE / MACRO_INACTIVE as
 * bytes arrive and the CTerm:Font / DECRQSS branches pick it up on ST.
 * ------------------------------------------------------------------------ */
void
cterm_dec_dcs_begin(struct cterminal *cterm)
{
	cterm->sixel = SIXEL_POSSIBLE;
	cterm->macro = MACRO_POSSIBLE;
}

/* ------------------------------------------------------------------------
 * cterm_dec_dcs_finish — DCS-content dispatcher invoked on ST.
 *
 * Called from the generic cterm_handle_st() in cterm.c when the active
 * string type is DCS.  Decides between sixel finalisation, DECDMAC
 * finalisation, DECRQSS (VT-420 status-string query), and the CTerm:Font
 * SyncTERM extension (delegated to cterm_cterm_handle_font_dcs).
 * ------------------------------------------------------------------------ */
void
cterm_dec_dcs_finish(struct cterminal *cterm)
{
	char	tmp[3072];

	if (cterm->sixel == SIXEL_STARTED) {
		cterm_parse_sixel_string(cterm, true);
	}
	else if (cterm->macro == MACRO_STARTED) {
		cterm_parse_macro_string(cterm, true);
	}
	else if (strncmp(cterm->strbuf, "CTerm:Font:", 11) == 0) {
		cterm_cterm_handle_font_dcs(cterm);
	}
	else if (strncmp(cterm->strbuf, "$q", 2) == 0) {
		/* DECRQSS — VT-420 Request Selection or Setting */
		switch (cterm->strbuf[2]) {
			case 'm':
				if (cterm->strbuf[3] == 0) {
					strcpy(tmp, "\x1bP1$r0");
					if (cterm->attr & 8)
						strcat(tmp, ";1");
					if (cterm->attr & 128)
						strcat(tmp, ";5");
					if (cterm->negative)
						strcat(tmp, ";7");
					if (cterm->fg_tc_str == NULL) {
						switch ((cterm->attr >> (cterm->negative ? 4 : 0)) & 7) {
							case 0: strcat(tmp, ";30"); break;
							case 1: strcat(tmp, ";34"); break;
							case 2: strcat(tmp, ";32"); break;
							case 3: strcat(tmp, ";36"); break;
							case 4: strcat(tmp, ";31"); break;
							case 5: strcat(tmp, ";35"); break;
							case 6: strcat(tmp, ";33"); break;
						}
					}
					if (cterm->bg_tc_str == NULL) {
						switch ((cterm->attr >> (cterm->negative ? 0 : 4)) & 7) {
							case 1: strcat(tmp, ";44"); break;
							case 2: strcat(tmp, ";42"); break;
							case 3: strcat(tmp, ";46"); break;
							case 4: strcat(tmp, ";41"); break;
							case 5: strcat(tmp, ";45"); break;
							case 6: strcat(tmp, ";43"); break;
							case 7: strcat(tmp, ";47"); break;
						}
					}
					if (cterm->fg_tc_str) {
						strcat(tmp, ";");
						strcat(tmp, cterm->fg_tc_str);
					}
					if (cterm->bg_tc_str) {
						strcat(tmp, ";");
						strcat(tmp, cterm->bg_tc_str);
					}
					strcat(tmp, "m\x1b\\");
					cterm_respond(cterm, tmp, strlen(tmp));
				}
				else {
					cterm_respond(cterm, "\x1bP0$r\x1b\\", 7);
				}
				break;
			case 'r':
				if (cterm->strbuf[3] == 0) {
					sprintf(tmp, "\x1bP1$r%d;%dr\x1b\\",
					    cterm->top_margin, cterm->bottom_margin);
					cterm_respond(cterm, tmp, strlen(tmp));
				}
				else {
					cterm_respond(cterm, "\x1bP0$r\x1b\\", 7);
				}
				break;
			case 's':
				if (cterm->strbuf[3] == 0) {
					sprintf(tmp, "\x1bP1$r%d;%ds\x1b\\",
					    cterm->left_margin, cterm->right_margin);
					cterm_respond(cterm, tmp, strlen(tmp));
				}
				else {
					cterm_respond(cterm, "\x1bP0$r\x1b\\", 7);
				}
				break;
			case 't':
				if (cterm->strbuf[3] == 0) {
					sprintf(tmp, "\x1bP1$r%dt\x1b\\", cterm->height);
					cterm_respond(cterm, tmp, strlen(tmp));
				}
				else {
					cterm_respond(cterm, "\x1bP0$r\x1b\\", 7);
				}
				break;
			case '$':
				if (cterm->strbuf[3] == '|' && cterm->strbuf[4] == 0) {
					sprintf(tmp, "\x1bP1$r%d$|\x1b\\", cterm->width);
					cterm_respond(cterm, tmp, strlen(tmp));
				}
				else if (cterm->strbuf[3] == '~' && cterm->strbuf[4] == 0) {
					/* DECSSDT query — read from topmost parent so sub
					 * reports the terminal's state, not its own zero. */
					struct cterminal *owner = cterm->parent ? cterm->parent : cterm;
					sprintf(tmp, "\x1bP1$r%d$~\x1b\\", owner->status_display_type);
					cterm_respond(cterm, tmp, strlen(tmp));
				}
				else if (cterm->strbuf[3] == '}' && cterm->strbuf[4] == 0) {
					/* DECSASD query */
					struct cterminal *owner = cterm->parent ? cterm->parent : cterm;
					sprintf(tmp, "\x1bP1$r%d$}\x1b\\", owner->status_display_active);
					cterm_respond(cterm, tmp, strlen(tmp));
				}
				else {
					cterm_respond(cterm, "\x1bP0$r\x1b\\", 7);
				}
				break;
			case '*':
				if (cterm->strbuf[3] == '|' && cterm->strbuf[4] == 0) {
					sprintf(tmp, "\x1bP1$r%d$|\x1b\\", cterm->height);
					cterm_respond(cterm, tmp, strlen(tmp));
				}
				else if (cterm->strbuf[3] == 'x' && cterm->strbuf[4] == 0) {
					sprintf(tmp, "\x1bP1$r%d*x\x1b\\", cterm->decsace);
					cterm_respond(cterm, tmp, strlen(tmp));
				}
				else if (cterm->strbuf[3] == 'r' && cterm->strbuf[4] == 0) {
					struct cterminal *owner = cterm->parent ? cterm->parent : cterm;
					uint32_t bps = owner->api_speed_query != NULL
					    ? owner->api_speed_query(owner->api_speed_context)
					    : (uint32_t)owner->decscs_speed;
					int ps2 = 0;
					switch (bps) {
						case 300:    ps2 = 1; break;
						case 600:    ps2 = 2; break;
						case 1200:   ps2 = 3; break;
						case 2400:   ps2 = 4; break;
						case 4800:   ps2 = 5; break;
						case 9600:   ps2 = 6; break;
						case 19200:  ps2 = 7; break;
						case 38400:  ps2 = 8; break;
						case 57600:  ps2 = 9; break;
						case 76800:  ps2 = 10; break;
						case 115200: ps2 = 11; break;
					}
					sprintf(tmp, "\x1bP1$r;%d*r\x1b\\", ps2);
					cterm_respond(cterm, tmp, strlen(tmp));
				}
				else {
					cterm_respond(cterm, "\x1bP0$r\x1b\\", 7);
				}
				break;
			case ' ':
				if (cterm->strbuf[3] == 'q' && cterm->strbuf[4] == 0) {
					int style;
					struct st_console_custom_cursor custom = {0};
					if (cterm->screen.capabilities & ST_CONSOLE_CAP_CUSTOM_CURSOR) {
						cterm_render_get_custom_cursor(cterm, &custom);
						if (cterm->cursor == CTERM_CURSOR_BLOCK)
							style = custom.blink ? 1 : 2;
						else
							style = custom.blink ? 3 : 4;
					}
					else {
						style = (cterm->cursor == CTERM_CURSOR_BLOCK) ? 2 : 4;
					}
					sprintf(tmp, "\x1bP1$r%d q\x1b\\", style);
					cterm_respond(cterm, tmp, strlen(tmp));
				}
				else {
					cterm_respond(cterm, "\x1bP0$r\x1b\\", 7);
				}
				break;
			default:
				cterm_respond(cterm, "\x1bP0$r\x1b\\", 7);
				break;
		}
	}
	cterm->sixel = SIXEL_INACTIVE;
}

/* ------------------------------------------------------------------------
 * cterm_dec_apply_sgr — DEC's slot in the SGR cascade.
 *
 * DEC has no currently-implemented SGR parameters of its own, so every
 * param falls through to the ECMA-48 base.  The placeholder keeps the
 * chain explicit and gives a home for future DEC-specific SGR values.
 * ------------------------------------------------------------------------ */
void
cterm_dec_apply_sgr(struct cterminal *cterm, int *pi)
{
	cterm_ecma48_apply_sgr(cterm, pi);
}
