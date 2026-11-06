/****************************************************************************
 * @format.tab-size 4		(Plain Text/Source Code File Header)			*
 * @format.use-tabs true	(see http://www.synchro.net/ptsc_hdr.html)		*
 *																			*
 * Copyright (c) 2026 Stephen Hurd							*
 *																			*
 * SPDX-License-Identifier: BSD-2-Clause								*
 ****************************************************************************/

/*
 * cterm_cterm.c — SyncTERM / CTerm / XTerm-private control-sequence handlers.
 *
 * Holds handlers for sequences specific to CTerm (the SyncTERM BBS terminal
 * emulation) plus a few XTerm device-attribute queries SyncTERM reuses:
 *   - CSI `=` music toggle (syncterm_music) and CSI `=` s SM/RM variants
 *   - BananANSI music (CSI N, repurposed ECMA-48 EF)
 *   - SCO cursor save/restore (SCOSC, SCORC) and the CSI s
 *     dispatcher that picks SCOSC vs DECSLRM based on DECLRMM
 *   - ANSI music (CSI M) dispatcher that picks music vs DL
 *   - Font load / select:   font_load, fnt/tsr, decscs (SCS designation)
 *   - ANSI music setup
 *   - Query sequences:      xterm_gfx_query, ext_state_query,
 *                           macro_query, decrqm_syncterm
 *   - Extended device-attr: syncterm_ext_da
 *   - CSI t (RGB palette set for FG/BG)
 *
 * Every handler uses the cterm_handle_* signature and is registered in
 * cterm_ansi_dispatch[] via keys from cterm_internal.h.
 */

#include "string_util.h"
#include "string_compat.h"
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "cterm_render.h"
#include "cterm.h"
#include "cterm_apc.h"
#include "cterm_internal.h"
#include "cterm_cterm.h"
#include "cterm_ecma48.h"	/* cterm_handle_sm/rm chained from SyncTERM ext */
#include "cterm_dec.h"	/* cterm_handle_decsm/decrm chained from ext */

void
cterm_handle_syncterm_music(struct cterminal *cterm, int *speed)	/* CSI | */
{
	(void)speed;
	cterm->music = 1;
	cterm->accumulator = cterm_accumulate_music;
}

void
cterm_handle_fnt(struct cterminal *cterm, int *speed)	/* CSI SP D — Font Select */
{
	(void)speed;
	cterm_seq_default(cterm, 0, 0);
	cterm_seq_default(cterm, 1, 0);
	switch (cterm->seq_param_int[0]) {
		case 0:
		case 1:
		case 2:
		case 3:
			if (cterm_render_set_font_selector(cterm, (int)cterm->seq_param_int[1], false,
			    (int)cterm->seq_param_int[0] + 1) == 0)
				cterm->setfont_result = 1;
			else
				cterm->setfont_result = 0;
			if (cterm->setfont_result == 0)
				cterm->altfont[cterm->seq_param_int[0]] =
				    (int)cterm->seq_param_int[1];
			break;
	}
}

void
cterm_handle_tsr(struct cterminal *cterm, int *speed)	/* CSI SP d — delete tab stop */
{
	(void)speed;
	if (cterm->seq_param_count > 0)
		cterm_delete_tabstop(cterm, (int)cterm->seq_param_int[0]);
}

void
cterm_handle_decscs(struct cterminal *cterm, int *speed)	/* CSI Ps1;Ps2 * r — Set Comm Speed */
{
	int newspeed = -1;
	cterm_seq_default(cterm, 0, 0);
	cterm_seq_default(cterm, 1, 0);
	if (cterm->seq_param_int[0] < 2) {
		switch (cterm->seq_param_int[1]) {
			case 0:  newspeed = 0; break;
			case 1:  newspeed = 300; break;
			case 2:  newspeed = 600; break;
			case 3:  newspeed = 1200; break;
			case 4:  newspeed = 2400; break;
			case 5:  newspeed = 4800; break;
			case 6:  newspeed = 9600; break;
			case 7:  newspeed = 19200; break;
			case 8:  newspeed = 38400; break;
			case 9:  newspeed = 57600; break;
			case 10: newspeed = 76800; break;
			case 11: newspeed = 115200; break;
		}
	}
	if (newspeed >= 0) {
		struct cterminal *owner = cterm->parent ? cterm->parent : cterm;
		if (speed != NULL)
			*speed = newspeed;
		owner->decscs_speed = newspeed;
		if (owner->api_speed_changed != NULL)
			owner->api_speed_changed(owner->api_speed_context,
			    (uint32_t)newspeed);
	}
	else
		cterm_log_sequence(cterm, "Invalid DECSCS parameters");
}

void
cterm_handle_ansi_music_setup(struct cterminal *cterm, int *speed)	/* CSI = M */
{
	(void)speed;
	cterm_seq_default(cterm, 0, 0);
	switch (cterm->seq_param_int[0]) {
		case 0:
			cterm->music_enable = CTERM_MUSIC_SYNCTERM;
			break;
		case 1:
			cterm->music_enable = CTERM_MUSIC_BANSI;
			break;
		case 2:
			cterm->music_enable = CTERM_MUSIC_ENABLED;
			break;
	}
}

void
cterm_handle_syncterm_sm_ext(struct cterminal *cterm, int *speed)	/* CSI = h */
{
	int i;
	(void)speed;
	for (i = 0; i < cterm->seq_param_count; i++) {
		cterm_seq_default(cterm, i, 0);
		switch (cterm->seq_param_int[i]) {
			case 1:
				cterm->pk_mode = true;
				if (cterm->key_event_mode_change != NULL)
					cterm->key_event_mode_change(1,
					    cterm->key_event_mode_change_cbdata);
				break;
			case 2:
				cterm->suppress_translated_keys = true;
				break;
			case 4:
				cterm->last_column_flag |= CTERM_LCF_ENABLED;
				cterm->last_column_flag &= ~CTERM_LCF_SET;
				break;
			case 5:
				cterm->last_column_flag |=
				    CTERM_LCF_FORCED | CTERM_LCF_ENABLED;
				break;
			case 255:
				cterm->doorway_mode = 1;
				break;
		}
	}
}

void
cterm_handle_syncterm_rm_ext(struct cterminal *cterm, int *speed)	/* CSI = l */
{
	int i;
	(void)speed;
	for (i = 0; i < cterm->seq_param_count; i++) {
		cterm_seq_default(cterm, i, 0);
		switch (cterm->seq_param_int[i]) {
			case 1:
				cterm->pk_mode = false;
				memset(cterm->pk_reported, 0, sizeof(cterm->pk_reported));
				if (cterm->key_event_mode_change != NULL)
					cterm->key_event_mode_change(0,
					    cterm->key_event_mode_change_cbdata);
				break;
			case 2:
				cterm->suppress_translated_keys = false;
				break;
			case 4:
				if ((cterm->last_column_flag & CTERM_LCF_FORCED) == 0)
					cterm->last_column_flag = 0;
				break;
			case 255:
				cterm->doorway_mode = 0;
				break;
		}
	}
}

void
cterm_handle_xterm_gfx_query(struct cterminal *cterm, int *speed)	/* CSI ? S */
{
	uint32_t cell_width;
	uint32_t cell_height;
	(void)speed;
	cterm_seq_default(cterm, 0, 0);
	cterm_seq_default(cterm, 1, 0);
	if (cterm->seq_param_int[0] != 2 || cterm->seq_param_int[1] != 1)
		return;
	if (st_console_cell_pixel_size(cterm->screen.console, &cell_width,
	    &cell_height) == 0)
		cterm_respond_printf(cterm, "\x1b[?2;0;%u;%uS",
		    cell_width * (uint32_t)cterm->width,
		    cell_height * (uint32_t)cterm->height);
}

void
cterm_handle_ext_state_query(struct cterminal *cterm, int *speed)	/* CSI = n */
{
	char tmp[3072];
	int vidflags;
	uint32_t cell_width;
	uint32_t cell_height;
	(void)speed;
	tmp[0] = 0;
	cterm_seq_default(cterm, 0, 1);
	/* Cases 1..6 are single-parameter queries; case 7 (audio DSR) accepts
	 * sub-parameters (query-all / query-one channel).  Reject extra
	 * parameters only for the single-parameter cases. */
	if (cterm->seq_param_int[0] != 7 && cterm->seq_param_count > 1)
		return;
	switch (cterm->seq_param_int[0]) {
		case 1:
			sprintf(tmp, "\x1b[=1;%u;%u;%u;%u;%u;%un",
			    CTERM_FIRST_DYNAMIC_FONT,
			    (uint8_t)cterm->setfont_result,
			    (uint8_t)cterm->altfont[0],
			    (uint8_t)cterm->altfont[1],
			    (uint8_t)cterm->altfont[2],
			    (uint8_t)cterm->altfont[3]);
			break;
		case 2:
			vidflags = cterm_render_get_video_flags(cterm);
			strcpy(tmp, "\x1b[=2");
			if (cterm->extattr & CTERM_EXTATTR_ORIGINMODE)
				strcat(tmp, ";6");
			if (cterm->extattr & CTERM_EXTATTR_AUTOWRAP)
				strcat(tmp, ";7");
			if (cterm->mouse_state_query(9, cterm->mouse_state_query_cbdata))
				strcat(tmp, ";9");
			if (cterm->cursor == CTERM_CURSOR_UNDERLINE)
				strcat(tmp, ";25");
			if (vidflags & ST_CONSOLE_VIDEO_ALT_CHARS)
				strcat(tmp, ";31");
			if (vidflags & ST_CONSOLE_VIDEO_NO_BRIGHT)
				strcat(tmp, ";32");
			if (vidflags & ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND)
				strcat(tmp, ";33");
			if (vidflags & ST_CONSOLE_VIDEO_BLINK_ALT_CHARS)
				strcat(tmp, ";34");
			if (vidflags & ST_CONSOLE_VIDEO_NO_BLINK)
				strcat(tmp, ";35");
			if (cterm->extattr & CTERM_EXTATTR_DECLRMM)
				strcat(tmp, ";69");
			/* DEC mode 80 is set when sixel scrolling is disabled. */
			if (!(cterm->extattr & CTERM_EXTATTR_SXSCROLL))
				strcat(tmp, ";80");
			if (cterm->mouse_state_query(1000, cterm->mouse_state_query_cbdata))
				strcat(tmp, ";1000");
			if (cterm->mouse_state_query(1001, cterm->mouse_state_query_cbdata))
				strcat(tmp, ";1001");
			if (cterm->mouse_state_query(1002, cterm->mouse_state_query_cbdata))
				strcat(tmp, ";1002");
			if (cterm->mouse_state_query(1003, cterm->mouse_state_query_cbdata))
				strcat(tmp, ";1003");
			if (cterm->mouse_state_query(1004, cterm->mouse_state_query_cbdata))
				strcat(tmp, ";1004");
			if (cterm->mouse_state_query(1005, cterm->mouse_state_query_cbdata))
				strcat(tmp, ";1005");
			if (cterm->mouse_state_query(1006, cterm->mouse_state_query_cbdata))
				strcat(tmp, ";1006");
			if (cterm->mouse_state_query(1007, cterm->mouse_state_query_cbdata))
				strcat(tmp, ";1007");
			if (cterm->mouse_state_query(1015, cterm->mouse_state_query_cbdata))
				strcat(tmp, ";1015");
			if (cterm->mouse_state_query(1016, cterm->mouse_state_query_cbdata))
				strcat(tmp, ";1016");
			if (strlen(tmp) == 4)
				strcat(tmp, ";");
			strcat(tmp, "n");
			break;
		case 3:	/* Query font char dimensions */
			if (st_console_cell_pixel_size(cterm->screen.console,
			    &cell_width, &cell_height) == 0)
				sprintf(tmp, "\x1b[=3;%u;%un",
				    cell_height, cell_width);
			break;
		case 4:
			sprintf(tmp, "\x1b[=4;%dn",
			    (cterm->last_column_flag & CTERM_LCF_ENABLED) ? 1 : 0);
			break;
		case 5:
			sprintf(tmp, "\x1b[=5;%dn",
			    (cterm->last_column_flag & CTERM_LCF_FORCED) ? 1 : 0);
			break;
		case 6:
			sprintf(tmp, "\x1b[=6;%dn", true ? 1 : 0);
			break;
		case 7:
			cterm_apc_audio_query(cterm, cterm->seq_param_count,
			    cterm->seq_param_int);
			break;
	}
	if (*tmp)
		cterm_respond(cterm, tmp, strlen(tmp));
}

void
cterm_handle_macro_query(struct cterminal *cterm, int *speed)	/* CSI ? n */
{
	char tmp[3072];
	size_t i, k;
	(void)speed;
	cterm_seq_default(cterm, 0, 1);
	tmp[0] = 0;
	switch (cterm->seq_param_int[0]) {
		case 62:	/* Query macro space available */
			if (cterm->seq_param_count > 1)
				return;
			strcpy(tmp, "\x1b[32767*{");
			break;
		case 63:	/* Query macro space "checksum" */
		{
			uint16_t crc = 0;
			if (cterm->seq_param_count > 2)
				return;
			cterm_seq_default(cterm, 1, 1);
			for (k = 0; k < (sizeof(cterm->macros) / sizeof(cterm->macros[0])); k++) {
				if (cterm->macros[k]) {
					for (i = 0; i <= cterm->macro_lens[k]; i++)
						crc = cterm_render_crc16(
						    &(uint8_t){(uint8_t)cterm->macros[k][i]}, 1, crc);
				}
				else {
					crc = cterm_render_crc16(&(uint8_t){0}, 1, crc);
				}
			}
			snprintf(tmp, sizeof(tmp), "\x1bP%u!~%04X\x1b\\",
			    (unsigned)cterm->seq_param_int[1], crc);
			break;
		}
	}
	if (*tmp)
		cterm_respond(cterm, tmp, strlen(tmp));
}

void
cterm_handle_font_load(struct cterminal *cterm, int *speed)	/* CSI = { */
{
	(void)speed;
	cterm_seq_default(cterm, 0, 255);
	cterm_seq_default(cterm, 1, 0);
	if (cterm->seq_param_int[0] > 255) {
		cterm_log_sequence(cterm, "Invalid font slot");
		return;
	}
	cterm->font_read = 0;
	cterm->font_slot = (int)cterm->seq_param_int[0];
	switch (cterm->seq_param_int[1]) {
		case 0:  cterm->font_size = 4096; break;
		case 1:  cterm->font_size = 3584; break;
		case 2:  cterm->font_size = 2048; break;
		default: cterm->font_size = 0;   break;
	}
	if (cterm->font_size > 0)
		cterm->accumulator = cterm_accumulate_font;
	else
		cterm_log_sequence(cterm, "Invalid font size parameter");
}

void
cterm_handle_decrqm_syncterm(struct cterminal *cterm, int *speed)	/* CSI = $ p */
{
	int pm = 0;
	(void)speed;
	if (cterm->seq_param_count != 1)
		return;
	cterm_seq_default(cterm, 0, 0);
	switch (cterm->seq_param_int[0]) {
		case 1:
			pm = cterm->pk_mode ? 1 : 2;
			break;
		case 2:
			pm = cterm->suppress_translated_keys ? 1 : 2;
			break;
		case 4:
			pm = (cterm->last_column_flag & CTERM_LCF_ENABLED) ? 1 : 2;
			break;
		case 5:
			pm = (cterm->last_column_flag & CTERM_LCF_FORCED) ? 3 : 2;
			break;
		case 255:
			pm = cterm->doorway_mode ? 1 : 2;
			break;
	}
	cterm_respond_printf(cterm, "\x1b[=%u;%d$y",
	    (unsigned)cterm->seq_param_int[0], pm);
}

void
cterm_handle_syncterm_ext_da(struct cterminal *cterm, int *speed)	/* CSI < c */
{
	char tmp[128];
	(void)speed;
	cterm_seq_default(cterm, 0, 0);
	tmp[0] = 0;
	if (cterm->seq_param_int[0] != 0)
		return;
	strcpy(tmp, "\x1b[<0");
	if (cterm->screen.capabilities & ST_CONSOLE_CAP_LOADABLE_FONTS)
		strcat(tmp, ";1");
	if (cterm->screen.capabilities & ST_CONSOLE_CAP_BRIGHT_BACKGROUND)
		strcat(tmp, ";2");
	if (cterm->screen.capabilities & ST_CONSOLE_CAP_PALETTE_CHANGE)
		strcat(tmp, ";3");
	if (cterm->screen.capabilities & ST_CONSOLE_CAP_PIXELS)
		strcat(tmp, ";4");
	if (cterm->screen.capabilities & ST_CONSOLE_CAP_LOADABLE_FONTS)
		strcat(tmp, ";5");
	if (cterm->screen.capabilities & ST_CONSOLE_CAP_EXTENDED_PALETTE)
		strcat(tmp, ";6");
	if (true)
		strcat(tmp, ";7");
	strcat(tmp, ";8");
	strcat(tmp, "c");
	cterm_respond(cterm, tmp, strlen(tmp));
}

void
cterm_handle_bansi_music(struct cterminal *cterm, int *speed)	/* CSI N */
{
	(void)speed;
	/* BananANSI style... does NOT start with MF or MB.
	 * This still conflicts with ANSI erase field. */
	if (cterm->music_enable >= CTERM_MUSIC_BANSI) {
		cterm->music = 2;
		cterm->accumulator = cterm_accumulate_music;
	}
}

void
cterm_handle_ansi_music(struct cterminal *cterm, int *speed)	/* CSI M */
{
	/* CSI M with no parameters enters "ANSI" music when music is
	 * enabled; otherwise it is the standard ECMA-48 DL. */
	if (cterm->music_enable == CTERM_MUSIC_ENABLED
	    && cterm->seq_param_count == 0) {
		cterm->music = 1;
		cterm->accumulator = cterm_accumulate_music;
		return;
	}
	cterm_handle_dl(cterm, speed);
}

void
cterm_handle_scosc(struct cterminal *cterm, int *speed)		/* CSI s (SCO) */
{
	(void)speed;
	CURR_XY(&cterm->save_xpos, &cterm->save_ypos);
}

void
cterm_handle_scorc(struct cterminal *cterm, int *speed)		/* CSI u (SCO) */
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
cterm_handle_csi_s(struct cterminal *cterm, int *speed)		/* CSI s */
{
	/* CSI s is DECSLRM when DECLRMM (left/right-margin mode) is set,
	 * otherwise SCOSC (save cursor position). */
	if (cterm->extattr & CTERM_EXTATTR_DECLRMM)
		cterm_handle_decslrm(cterm, speed);
	else
		cterm_handle_scosc(cterm, speed);
}

void
cterm_handle_csi_t(struct cterminal *cterm, int *speed)	/* CSI Pc ; Pr ; Pg ; Pb t */
{
	/* CTerm private: set FG or BG RGB palette entry. Pc=0 sets BG,
	 * Pc=1 sets FG; the remaining three parameters are 8-bit R/G/B. */
	uint32_t *c = NULL;
	uint32_t nc;
	(void)speed;
	if (cterm->seq_param_count < 4)
		return;
	if ((cterm->seq_param_int[0] == 0) ^ cterm->negative)
		c = &cterm->bg_color;
	else if ((cterm->seq_param_int[0] == 1) ^ cterm->negative)
		c = &cterm->fg_color;
	if (c == NULL)
		return;
	nc = cterm_render_map_rgb((uint16_t)(cterm->seq_param_int[1] << 8),
	    (uint16_t)(cterm->seq_param_int[2] << 8),
	    (uint16_t)(cterm->seq_param_int[3] << 8));
	if (nc != UINT32_MAX)
		*c = nc;
	cterm_render_set_colours(cterm, cterm->fg_color, cterm->bg_color);
}

/* ---- DECSET / DECRESET cascade (CSI ? h / CSI ? l) ---------------------
 * SyncTERM-private DEC private modes layer.  Processes CTerm-specific
 * modes (console video flags 31-35, sixel-scroll 80) first, marks those
 * parameters consumed, then forwards the remaining parameters to the
 * DEC / XTerm handler in cterm_dec.c.  A final palette refresh is done
 * after the chain completes because `ST_CONSOLE_VIDEO_*` flags change how the
 * current attribute maps to fg/bg colour. */

static void
cterm_decset_reset_common(struct cterminal *cterm, bool set)
{
	uint32_t oldfg, oldbg;
	bool updfg, updbg;
	int i, flags;

	cterm_render_attribute_colours(cterm, cterm->attr, &oldfg, &oldbg);
	updfg = (oldfg == cterm->fg_color);
	updbg = (oldbg == cterm->bg_color);

	for (i = 0; i < cterm->seq_param_count; i++) {
		if (cterm_seq_param_consumed(cterm, i))
			continue;
		cterm_seq_default(cterm, i, 0);
		flags = cterm_render_get_video_flags(cterm);
		switch (cterm->seq_param_int[i]) {
			case 31:
				if (set)
					flags |= ST_CONSOLE_VIDEO_ALT_CHARS;
				else
					flags &= ~ST_CONSOLE_VIDEO_ALT_CHARS;
				cterm_render_set_video_flags(cterm, flags);
				break;
			case 32:
				if (set)
					flags |= ST_CONSOLE_VIDEO_NO_BRIGHT;
				else
					flags &= ~ST_CONSOLE_VIDEO_NO_BRIGHT;
				cterm_render_set_video_flags(cterm, flags);
				break;
			case 33:
				if (set)
					flags |= ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND;
				else
					flags &= ~ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND;
				cterm_render_set_video_flags(cterm, flags);
				break;
			case 34:
				if (set)
					flags |= ST_CONSOLE_VIDEO_BLINK_ALT_CHARS;
				else
					flags &= ~ST_CONSOLE_VIDEO_BLINK_ALT_CHARS;
				cterm_render_set_video_flags(cterm, flags);
				break;
			case 35:
				if (set)
					flags |= ST_CONSOLE_VIDEO_NO_BLINK;
				else
					flags &= ~ST_CONSOLE_VIDEO_NO_BLINK;
				cterm_render_set_video_flags(cterm, flags);
				break;
			case 80:
				if (set)
					cterm->extattr &= ~CTERM_EXTATTR_SXSCROLL;
				else
					cterm->extattr |= CTERM_EXTATTR_SXSCROLL;
				break;
			default:
				continue;
		}
		cterm_seq_consume_param(cterm, i);
	}

	/* Chain to the DEC / XTerm handler for any unconsumed params. */
	if (set)
		cterm_handle_decsm(cterm, NULL);
	else
		cterm_handle_decrm(cterm, NULL);

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
cterm_handle_decsm_cterm(struct cterminal *cterm, int *speed)	/* CSI ? h */
{
	(void)speed;
	cterm_decset_reset_common(cterm, true);
}

void
cterm_handle_decrm_cterm(struct cterminal *cterm, int *speed)	/* CSI ? l */
{
	(void)speed;
	cterm_decset_reset_common(cterm, false);
}

/* ---- DSR cascade (CSI n) ------------------------------------------------
 * CTerm adds param 255 ("report screen bottom-right as CPR") on top of
 * the ECMA-48 DSR.  Handle that here, mark it consumed, then let the
 * ECMA-48 handler take care of 5 / 6. */
void
cterm_handle_dsr_cterm(struct cterminal *cterm, int *speed)	/* CSI n */
{
	if (cterm->seq_param_count > 0
	    && !cterm_seq_param_consumed(cterm, 0)
	    && cterm->seq_param_int[0] == 255) {
		cterm_respond_printf(cterm, "\x1b[%d;%dR", CURR_MAXY, CURR_MAXX);
		cterm_seq_consume_param(cterm, 0);
	}
	cterm_handle_dsr(cterm, speed);
}

/* ========================================================================
 * String-type dispatchers invoked from the generic cterm_handle_st()
 * shell in cterm.c.
 * ======================================================================== */

/* ------------------------------------------------------------------------
 * cterm_cterm_handle_font_dcs — CTerm:Font:<slot>:<base64-payload>
 *
 * SyncTERM-private DCS extension that loads a user-defined font into a
 * slot.  Called from cterm_dec_dcs_finish() when the DCS strbuf starts
 * with "CTerm:Font:".  The prefix + colon have already been matched;
 * this helper parses the slot number and decodes the base64 body.
 * ------------------------------------------------------------------------ */
void
cterm_cterm_handle_font_dcs(struct cterminal *cterm)
{
	char *p, *p2;
	int decoded;
	size_t size;
	unsigned long slot;

	slot = strtoul(cterm->strbuf + 11, &p, 10);
	if (slot < CTERM_FIRST_DYNAMIC_FONT || slot > 255)
		return;
	cterm->font_slot = (int)slot;
	if (p && *p == ':') {
		p++;
		decoded = cterm_render_base64_decode(cterm->fontbuf,
		    sizeof(cterm->fontbuf), p);
		if (decoded < 0)
			return;
		size = (size_t)decoded;
		p2 = malloc(size);
		if (p2) {
			memcpy(p2, cterm->fontbuf, size);
			cterm_render_replace_font(cterm, (uint8_t)cterm->font_slot,
			    strdup("Remote Defined Font"), p2, size);
		}
	}
}

/* ------------------------------------------------------------------------
 * cterm_cterm_osc_finish — OSC-content dispatcher
 *
 * ECMA-48 defines the OSC framing but leaves the contents up to the
 * host registration.  The sequences we handle here are XTerm's
 * "Change Color(s)" family (OSC 4 — palette set/query, OSC 104 —
 * palette reset, OSC 10/11 — default fg/bg query) and the XTerm/
 * SyncTERM hyperlink extension (OSC 8).
 * ------------------------------------------------------------------------ */
void
cterm_cterm_osc_finish(struct cterminal *cterm)
{
	char *p, *p2;
	size_t i;
	int palette_offset = 0;

	if (cterm->screen.capabilities & ST_CONSOLE_CAP_EXTENDED_PALETTE)
		palette_offset = 16;

	/* OSC 4 — Change colour palette */
	if (cterm->strbuf[0] == '4' && cterm->strbuf[1] == ';') {
		unsigned long index = ULONG_MAX;
		char *seqlast;
		p2 = &cterm->strbuf[2];
		while ((p = strtok_r(p2, ";", &seqlast)) != NULL) {
			p2 = NULL;
			if (index == ULONG_MAX) {
				index = strtoul(p, NULL, 10);
				if (index == ULONG_MAX || index > 13200)
					break;
			}
			else {
				if (p[0] == '?' && p[1] == 0) {
					uint8_t qr, qg, qb;
					if (cterm_render_get_palette(cterm, index + palette_offset, &qr, &qg, &qb))
						cterm_respond_printf(cterm,
						    "\033]4;%lu;rgb:%02x/%02x/%02x\033\\",
						    index, qr, qg, qb);
				}
				else if (strncmp(p, "rgb:", 4) == 0) {
					char *p3, *p4, *collast;
					uint16_t rgb[3];
					int ccount = 0;
					bool broken = false;
					p4 = &p[4];
					while (ccount < 3
					    && (p3 = strtok_r(p4, "/", &collast)) != NULL) {
						p4 = NULL;
						/* Parse into unsigned long (strtoul's return type) so
						 * overflow can be detected, then narrow to uint16_t
						 * after the range check. The 1/2/3-digit cases
						 * expand the parsed nibbles into a 16-bit value;
						 * each expansion stays within UINT16_MAX by
						 * construction (1 nibble → 4 nibbles, etc.). */
						unsigned long v;
						v = strtoul(p3, NULL, 16);
						if (v > UINT16_MAX)
							break;
						switch (strlen(p3)) {
							case 1:
								rgb[ccount] = (uint16_t)(v | (v << 4) | (v << 8) | (v << 12));
								break;
							case 2:
								rgb[ccount] = (uint16_t)(v | (v << 8));
								break;
							case 3:
								rgb[ccount] = (uint16_t)((v & 0x0f) | (v << 4));
								break;
							case 4:
								rgb[ccount] = (uint16_t)v;
								break;
							default:
								broken = true;
								break;
						}
						ccount++;
					}
					if (ccount == 3 && !broken)
						cterm_setpalette(cterm, index + palette_offset,
						    rgb[0], rgb[1], rgb[2]);
				}
				else
					break;
				index = ULONG_MAX;
			}
		}
	}
	/* OSC 104 — reset colour palette */
	else if (strncmp("104", cterm->strbuf, 3) == 0) {
		if (strlen(cterm->strbuf) == 3) {
			for (i = 0; i < CTERM_PALETTE_SIZE; i++)
				(void)cterm_resetpalette(cterm, (uint32_t)i);
		}
		else if (cterm->strbuf[3] == ';') {
			char *seqlast;
			unsigned long pi;
			p2 = &cterm->strbuf[4];
			while ((p = strtok_r(p2, ";", &seqlast)) != NULL) {
				p2 = NULL;
				pi = strtoul(p, NULL, 10);
				if (pi < CTERM_PALETTE_SIZE)
					(void)cterm_resetpalette(cterm, (uint32_t)pi);
			}
		}
	}
	/* OSC 10 / 11 — default fg / bg colour query */
	else if (cterm->strbuf[0] == '1'
	    && (cterm->strbuf[1] == '0' || cterm->strbuf[1] == '1')
	    && cterm->strbuf[2] == ';'
	    && cterm->strbuf[3] == '?'
	    && cterm->strbuf[4] == 0) {
		uint32_t colour = cterm->strbuf[1] == '0'
		    ? cterm->default_fg_palette : cterm->default_bg_palette;
		cterm_respond_printf(cterm, "\x1b]1%c;rgb:%02x/%02x/%02x\x1b\\",
		    cterm->strbuf[1],
		    colour >> 16 & 0xff, colour >> 8 & 0xff, colour & 0xff);
	}
	/* OSC 8 — hyperlinks (XTerm extension) */
	else if (cterm->strbuf[0] == '8' && cterm->strbuf[1] == ';') {
		char *params = &cterm->strbuf[2];
		char *uri = strchr(params, ';');
		if (uri) {
			*uri++ = '\0';
			if (*uri) {
				char *id_param = NULL;
				char *ptmp = params;
				char *tok;
				char *savelast;
				while ((tok = strtok_r(ptmp, ":", &savelast)) != NULL) {
					ptmp = NULL;
					if (cterm_render_casecmp_n(tok, "id=", 3) == 0)
						id_param = tok + 3;
				}
				cterm_render_set_hyperlink(cterm,
				    cterm_render_add_hyperlink(cterm, uri, id_param));
			}
			else {
				cterm_render_set_hyperlink(cterm, 0);
			}
		}
	}
}

/* ========================================================================
 * SyncTERM / CTerm / XTerm extensions — ANSI music, extended colour,
 * and the accumulators for doorway mode, music, and font loading.
 * None of these are ECMA-48; they all live here because they're the
 * SyncTERM / CTerm extensions.
 * ======================================================================== */

/* ---- ANSI music data tables ---- */
static const char *octave="C#D#EF#G#A#B";

/* Characters allowed in music strings... if one that is NOT in here is
 * found, CTerm leaves music capture mode and tosses the buffer away */
static const char *musicchars="aAbBcCdDeEfFgGlLmMnNoOpPsStT0123456789.-+#<> ";
static const unsigned note_frequency[]={	/* Hz*1000 */
/* Octave 0 (Note 1) */
	 65406
	,69296
	,73416
	,77782
	,82407
	,87307
	,92499
	,97999
	,103820
	,110000
	,116540
	,123470
/* Octave 1 */
	,130810
	,138590
	,146830
	,155560
	,164810
	,174610
	,185000
	,196000
	,207650
	,220000
	,233080
	,246940
/* Octave 2 */
	,261620
	,277180
	,293660
	,311130
	,329630
	,349230
	,369990
	,392000
	,415300
	,440000
	,466160
	,493880
/* Octave 3 */
	,523250
	,554370
	,587330
	,622250
	,659260
	,698460
	,739990
	,783990
	,830610
	,880000
	,932330
	,987770
/* Octave 4 */
	,1046500
	,1108700
	,1174700
	,1244500
	,1318500
	,1396900
	,1480000
	,1568000
	,1661200
	,1760000
	,1864600
	,1975500
/* Octave 5 */
	,2093000
	,2217500
	,2349300
	,2489000
	,2637000
	,2793800
	,2959900
	,3136000
	,3322400
	,3520000
	,3729300
	,3951100
/* Octave 6 */
	,4186000
	,4435000
	,4698600
	,4978000
	,5274000
	,5587000
	,5920000
	,6272000
	,6644000
	,7040000
	,7458600
	,7902200
};

/* Consume every decimal digit without a temporary buffer or narrowing an
 * out-of-range strtoul result. The caller's normal range clamp still applies.
 * Leave the cursor on the last digit for play_music's shared loop increment. */
static int
music_number(char **cursor)
{
	int value = 0;
	char *p = *cursor;

	while (ascii_is_digit((unsigned char)p[1])) {
		int digit = *++p - '0';
		if (value > (INT_MAX - digit) / 10)
			value = INT_MAX;
		else
			value = value * 10 + digit;
	}
	*cursor = p;
	return value;
}

/* ---- ANSI music: streaming via xp_audio mixer ----
 * Each MML note is rendered via xptone_makewave into a small buffer and
 * appended to a per-terminal audio stream. The mixer drains the stream at
 * device rate. Foreground notes block the parser until their data has been
 * played; background notes let the parser race ahead, limited only by the
 * stream's ring buffer capacity. */
static void play_music(struct cterminal *cterm)
{
	int		i;
	char	*p;
	char	*out;
	int		offset;
	char	note;
	int		notelen;
	int		dotted;
	int		notenum;
	int		had_foreground = 0;

	if(cterm->quiet) {
		cterm->music=0;
		cterm->accumulator = NULL;
		cterm->musicbuf[0]=0;
		return;
	}

	/* Open the audio device and a music stream on first use. Both are kept
	 * open for the life of the terminal — they are torn down in cterm_end. */
	if (cterm->music_stream == NULL) {
		audio_engine_t *engine;

		if (cterm->screen.audio == NULL
		    || st_audio_runtime_acquire(cterm->screen.audio, &engine) != 0)
			goto done;
		/* -12 dB base gives tones generated at full scale the same
		 * headroom reduction that xptone_makewave used to bake in. */
		if (audio_stream_create(&cterm->music_stream, engine,
		    -12.0f, -12.0f) != 0) {
			cterm->music_stream = NULL;
			goto done;
		}
	}

	p=cterm->musicbuf;
	if(cterm->music==1) {
		switch(ascii_to_upper((unsigned char)*p)) {
			case 'F':
				cterm->musicfore=true;
				p++;
				break;
			case 'B':
				cterm->musicfore=false;
				p++;
				break;
			case 'N':
				if(!ascii_is_digit((unsigned char)*(p+1))) {
					cterm->noteshape=CTERM_MUSIC_NORMAL;
					p++;
				}
				break;
			case 'L':
				cterm->noteshape=CTERM_MUSIC_LEGATO;
				p++;
				break;
			case 'S':
				cterm->noteshape=CTERM_MUSIC_STACATTO;
				p++;
				break;
		}
	}
	for(;*p;p++) {
		notenum=0;
		offset=0;
		switch(ascii_to_upper((unsigned char)*p)) {
			case 'M':
				p++;
				switch(ascii_to_upper((unsigned char)*p)) {
					case 'F':
						cterm->musicfore=true;
						break;
					case 'B':
						cterm->musicfore=false;
						break;
					case 'N':
						cterm->noteshape=CTERM_MUSIC_NORMAL;
						break;
					case 'L':
						cterm->noteshape=CTERM_MUSIC_LEGATO;
						break;
					case 'S':
						cterm->noteshape=CTERM_MUSIC_STACATTO;
						break;
					default:
						p--;
				}
				break;
			case 'T':						/* Tempo */
				cterm->tempo=music_number(&p);
				if(cterm->tempo>255)
					cterm->tempo=255;
				if(cterm->tempo<32)
					cterm->tempo=32;
				break;
			case 'O':						/* Octave */
				cterm->octave=music_number(&p);
				if(cterm->octave>6)
					cterm->octave=6;
				break;
			case 'L':
				cterm->notelen=music_number(&p);
				if(cterm->notelen<1)
					cterm->notelen=1;
				if(cterm->notelen>64)
					cterm->notelen=64;
				break;
			case 'N':						/* Note by number */
				notenum=music_number(&p);
				if(notenum==0) {
					notenum=-1;
					offset=1;
				}
				/* Fall-through */
			case 'A':						/* Notes in current octave by letter */
			case 'B':
			case 'C':
			case 'D':
			case 'E':
			case 'F':
			case 'G':
			case 'P':
				note=ascii_to_upper((unsigned char)*p);
				notelen=cterm->notelen;
				dotted=0;
				i=1;
				while(i) {
					i=0;
					if(*(p+1)=='+' || *(p+1)=='#') {	/* SHARP */
						offset+=1;
						p++;
						i=1;
					}
					if(*(p+1)=='-') {					/* FLAT */
						offset-=1;
						p++;
						i=1;
					}
					if(*(p+1)=='.') {					/* Dotted note (1.5*notelen) */
						dotted=1;
						p++;
						i=1;
					}
					if(ascii_is_digit((unsigned char)*(p+1))) {
						notelen=music_number(&p);
						i=1;
					}
				}
				if(note=='P') {
					notenum=-1;
					offset=0;
				}
				if(notenum==0) {
					out=(char*)strchr(octave,note);
					if(out==NULL) {
						notenum=-1;
						offset=1;
					}
					else {
						notenum=cterm->octave*12+1;
						notenum+=(int)(out-octave); /* Index in the 12-note octave. */
					}
				}
				/* A saturated numbered note plus a sharp must stay silent
				 * rather than overflowing back into the playable range. */
				if (offset > 0 && notenum > INT_MAX - offset)
					notenum = INT_MAX;
				else
					notenum += offset;
				{
					int      duration_ms;
					int      pause_ms;
					double   freq = 0.0;
					int      note_frames;
					int      pause_frames;
					int      total_frames;
					int16_t *buf;

					if (notelen < 1)  notelen = 1;
					if (notelen > 64) notelen = 64;
					duration_ms = dotted ? (360000 / cterm->tempo) : (240000 / cterm->tempo);
					duration_ms /= notelen;
					switch (cterm->noteshape) {
						case CTERM_MUSIC_STACATTO:
							pause_ms = duration_ms / 4;
							break;
						case CTERM_MUSIC_LEGATO:
							pause_ms = 0;
							break;
						default:
							pause_ms = duration_ms / 8;
							break;
					}
					duration_ms -= pause_ms;
					if (notenum >= 0 && notenum < 72)
						freq = (double)note_frequency[notenum] / 1000.0;
					note_frames  = (int)(AUDIO_SAMPLE_RATE * (uint32_t)duration_ms / 1000);
					pause_frames = (int)(AUDIO_SAMPLE_RATE * (uint32_t)pause_ms / 1000);
					total_frames = note_frames + pause_frames;
					if (total_frames > 0) {
						buf = malloc((size_t)total_frames * AUDIO_FRAME_SIZE);
						if (buf) {
							if (note_frames > 0)
								(void)audio_generate_tone(buf,
								    (size_t)note_frames, freq,
								    AUDIO_WAVE_SINE_SAW_HARM, true);
							if (pause_frames > 0)
								memset(buf + (size_t)note_frames * AUDIO_CHANNELS, 0,
								       (size_t)pause_frames * AUDIO_FRAME_SIZE);
							/* append takes ownership of buf. */
							if (audio_stream_append_take(cterm->music_stream,
							    buf, (size_t)total_frames, NULL) != 0)
								free(buf);
							if (cterm->musicfore)
								had_foreground = 1;
						}
					}
				}
				break;
			case '<':							/* Down one octave */
				cterm->octave--;
				if(cterm->octave<0)
					cterm->octave=0;
				break;
			case '>':							/* Up one octave */
				cterm->octave++;
				if(cterm->octave>6)
					cterm->octave=6;
				break;
		}
	}
done:
	/* If any foreground notes were emitted, block until the device has
	 * actually played the tail out before handing control back to the
	 * caller (the BBS session / parser). For background-only MML, fall
	 * through so output keeps streaming without added delay. */
	if (had_foreground && cterm->music_stream != NULL)
		(void)audio_stream_drain(cterm->music_stream);
	cterm->music=0;
	cterm->accumulator = NULL;
	cterm->musicbuf[0]=0;
}

/* Lazily open the audio device and the fx_stream. Returns false if either
 * step fails — caller must treat the fx as a no-op in that case. */
static bool
cterm_fx_ensure_open(struct cterminal *cterm)
{
	if (cterm->fx_stream != NULL)
		return true;
	if (cterm->screen.audio == NULL)
		return false;
	/* -12 dB base matches music_stream: synth output is attenuated
	 * here rather than baked into xptone_makewave, and ooii/rip
	 * samples ride at the same level so no one call-site is
	 * dramatically louder than another. */
	{
		audio_engine_t *engine;

		if (st_audio_runtime_acquire(cterm->screen.audio, &engine) != 0
		    || audio_stream_create(&cterm->fx_stream, engine,
		    -12.0f, -12.0f) != 0) {
			cterm->fx_stream = NULL;
			return false;
		}
	}
	if (cterm->fx_stream == NULL) {
		return false;
	}
	return true;
}

bool
cterm_play_fx(struct cterminal *cterm, const int16_t *frames, size_t nframes)
{
	if (!cterm || !frames || nframes == 0)
		return false;
	if (!cterm_fx_ensure_open(cterm))
		return false;
	return audio_stream_append(cterm->fx_stream, frames, nframes, NULL) == 0;
}

bool
cterm_play_fx_tone(struct cterminal *cterm, double freq, uint32_t duration_ms, uint32_t shape)
{
	size_t   nframes;
	int16_t *buf;

	if (!cterm || duration_ms == 0)
		return false;
	if (!cterm_fx_ensure_open(cterm))
		return false;
	nframes = (size_t)AUDIO_SAMPLE_RATE * duration_ms / 1000;
	if (nframes == 0)
		return true;
	buf = malloc(nframes * AUDIO_FRAME_SIZE);
	if (!buf)
		return false;
	if (audio_generate_tone(buf, nframes, freq,
	    shape <= AUDIO_WAVE_SINE_SAW_HARM
	    ? (enum audio_wave_shape)shape : AUDIO_WAVE_SINE, true) != 0
	    || audio_stream_append_take(cterm->fx_stream, buf, nframes,
	    NULL) != 0) {
		free(buf);
		return false;
	}
	return true;
}

bool
cterm_play_fx_u8(struct cterminal *cterm, const unsigned char *samp, size_t size)
{
	if (!cterm || !samp || size == 0)
		return false;
	if (!cterm_fx_ensure_open(cterm))
		return false;
	return audio_stream_append_u8_mono_22050(cterm->fx_stream, samp, size,
	    NULL) == 0;
}

/* ---- Extended-colour SGR parameter helpers (truecolor / 256-colour) ---- */
void cterm_save_extended_colour_seq(struct cterminal *cterm, int fg, int seqoff, int count)
{
	char **str = fg ? &cterm->fg_tc_str : &cterm->bg_tc_str;
	const char **p = cterm->seq_param_strs;

	if (*str)
		CTERM_FREE_AND_CLEAR(*str);

	switch (count) {
		case 1:
			if (cterm_render_asprintf(str, "%s", p[seqoff]) < 0)
				*str = NULL;
			break;
		case 2:
			if (cterm_render_asprintf(str, "%s;%s", p[seqoff], p[seqoff+1]) < 0)
				*str = NULL;
			break;
		case 3:
			if (cterm_render_asprintf(str, "%s;%s;%s", p[seqoff], p[seqoff+1], p[seqoff+2]) < 0)
				*str = NULL;
			break;
		case 5:
			if (cterm_render_asprintf(str, "%s;%s;%s;%s;%s", p[seqoff], p[seqoff+1], p[seqoff+2], p[seqoff+3], p[seqoff+4]) < 0)
				*str = NULL;
			break;
	}
}

void cterm_parse_extended_colour(struct cterminal *cterm, int *i, bool fg)
{
	struct sub_params sub = {0};
	uint32_t nc;
	uint32_t *co;
	int palette_offset = 0;

	if (cterm->screen.capabilities & ST_CONSOLE_CAP_EXTENDED_PALETTE)
		palette_offset = 16;

	if (cterm == NULL || i == NULL)
		return;
	if (fg)
		CTERM_FREE_AND_CLEAR(cterm->fg_tc_str);
	else
		CTERM_FREE_AND_CLEAR(cterm->bg_tc_str);
	co = fg ? (&cterm->fg_color) : (&cterm->bg_color);
	if (*i >= cterm->seq_param_count)
		return;

	/* SGR 38/48 extended-colour R/G/B components are spec'd to 0-255 but
	 * flow through the uint64_t seq_param_int/sub.param_int storage (which
	 * uses UINT64_MAX as a "slot empty" sentinel). sgr_rgb_scale() clamps
	 * and shifts into the 16-bit palette range that map_rgb expects. */
#	define SGR_RGB_SCALE(v) \
		((uint16_t)(((v) > 255u ? 255u : (unsigned)(v)) << 8))

	if (cterm->seq_param_strs[*i][2] == ':') {
		/* CSI 38 : 5 : X m   (indexed)
		 * CSI 38 : 2 : Z? : R : G : B m   (RGB with optional colour-space id) */
		if (cterm_parse_sub_parameters(&sub, cterm, *i)) {
			if (sub.param_count == 3 && sub.param_int[1] == 5) {
				*co = (uint32_t)sub.param_int[2];
				cterm_save_extended_colour_seq(cterm, fg, *i, 1);
			}
			else if (sub.param_count >= 5 && sub.param_int[1] == 2) {
				if (sub.param_count == 5) {
					nc = cterm_render_map_rgb(SGR_RGB_SCALE(sub.param_int[2]),
					    SGR_RGB_SCALE(sub.param_int[3]),
					    SGR_RGB_SCALE(sub.param_int[4]));
					if (nc != UINT32_MAX)
						*co = nc;
					cterm_save_extended_colour_seq(cterm, fg, *i, 1);
				}
				else if (sub.param_count > 5) {
					nc = cterm_render_map_rgb(SGR_RGB_SCALE(sub.param_int[3]),
					    SGR_RGB_SCALE(sub.param_int[4]),
					    SGR_RGB_SCALE(sub.param_int[5]));
					if (nc != UINT32_MAX)
						*co = nc;
					cterm_save_extended_colour_seq(cterm, fg, *i, 1);
				}
			}
		}
	}
	else if ((*i)+1 < cterm->seq_param_count
	    && cterm->seq_param_int[(*i)+1] == 5
	    && cterm->seq_param_strs[(*i)+1][1] == ':') {
		/* CSI 38 ; 5 : X m */
		if (cterm_parse_sub_parameters(&sub, cterm, (*i)+1)) {
			if (sub.param_count == 2)
				*co = (uint32_t)sub.param_int[1];
			cterm_save_extended_colour_seq(cterm, fg, *i, 2);
			(*i)++;
		}
	}
	else if ((*i)+2 < cterm->seq_param_count
	    && cterm->seq_param_int[(*i)+1] == 5) {
		/* CSI 38 ; 5 ; X m */
		*co = (uint32_t)cterm->seq_param_int[(*i)+2] + palette_offset;
		cterm_save_extended_colour_seq(cterm, fg, *i, 3);
		*i += 2;
	}
	else if ((*i)+1 < cterm->seq_param_count
	    && cterm->seq_param_int[(*i)+1] == 2
	    && cterm->seq_param_strs[(*i)+1][1] == ':') {
		/* CSI 38 ; 2 : Z? : R : G : B m */
		if (cterm_parse_sub_parameters(&sub, cterm, (*i)+1)) {
			nc = UINT32_MAX;
			if (sub.param_count > 4)
				nc = cterm_render_map_rgb(SGR_RGB_SCALE(sub.param_int[2]),
				    SGR_RGB_SCALE(sub.param_int[3]),
				    SGR_RGB_SCALE(sub.param_int[4]));
			else if (sub.param_count == 4)
				nc = cterm_render_map_rgb(SGR_RGB_SCALE(sub.param_int[1]),
				    SGR_RGB_SCALE(sub.param_int[2]),
				    SGR_RGB_SCALE(sub.param_int[3]));
			if (nc != UINT32_MAX)
				*co = nc;
			cterm_save_extended_colour_seq(cterm, fg, *i, 2);
			*i += 1;
		}
	}
	else if ((*i)+4 < cterm->seq_param_count
	    && cterm->seq_param_int[(*i)+1] == 2) {
		/* CSI 38 ; 2 ; R ; G ; B m */
		nc = cterm_render_map_rgb(SGR_RGB_SCALE(cterm->seq_param_int[(*i)+2]),
		    SGR_RGB_SCALE(cterm->seq_param_int[(*i)+3]),
		    SGR_RGB_SCALE(cterm->seq_param_int[(*i)+4]));
		if (nc != UINT32_MAX)
			*co = nc;
		cterm_save_extended_colour_seq(cterm, fg, *i, 5);
		*i += 4;
	}
#	undef SGR_RGB_SCALE
	cterm_free_sub_parameters(&sub);
}

/* ---- Accumulators for multi-byte modes (doorway / music / font) ---- */

/* ------------------------------------------------------------------------
 * cterm_accumulate_doorway — doorway-mode one-byte passthrough
 *
 * When doorway mode is active and the previous byte was NUL, the next
 * byte is written to the screen as a literal cell regardless of its
 * control-character value.  Clears the one-shot flag on exit.  Flushes
 * the caller's print-batch buffer before writing the cell.
 * ------------------------------------------------------------------------ */
bool
cterm_accumulate_doorway(struct cterminal *cterm, unsigned char byte, int *speed)
{
	(void)speed;
	cterm_uctputs(cterm, cterm->print_buf);
	cterm->print_buf[0] = 0;
	cterm->print_pos = cterm->print_buf;
	cterm_text_literal(cterm, byte);
	cterm->doorway_char = 0;
	cterm->accumulator = NULL;
	return true;
}

/* ------------------------------------------------------------------------
 * cterm_accumulate_music — ANSI / BANSI music string accumulator
 *
 * Collects bytes into musicbuf until the terminator 0x0E (SO) fires the
 * music playback. Any byte outside the allowed musicchars set aborts
 * the accumulation silently. Pending text is presented before playback,
 * then the rendering batch resumes while the music runs.
 * ------------------------------------------------------------------------ */
bool
cterm_accumulate_music(struct cterminal *cterm, unsigned char byte, int *speed)
{
	int x, y;
	unsigned char ch[2] = {byte, 0};
	(void)speed;

	if (byte == 14) {
		(void)cterm_render_set_hold_update(cterm, 0);
		CURR_XY(&x, &y);
		cterm_render_set_position(cterm, x, y);
		cterm_render_set_cursor(cterm, cterm->cursor);
		(void)cterm_render_set_hold_update(cterm, 1);
		play_music(cterm);
	}
	else if (strchr(musicchars, byte) != NULL) {
		if (strlen((char *)cterm->musicbuf) < sizeof(cterm->musicbuf) - 2)
			ustrcat(cterm->musicbuf, ch);
	}
	else {
		cterm->music = 0;
		cterm->accumulator = NULL;
		cterm->musicbuf[0] = 0;
	}
	return true;
}

/* ------------------------------------------------------------------------
 * cterm_accumulate_font — remote font download accumulator
 *
 * Collects exactly font_size raw bytes into fontbuf, then hands the
 * completed image to cterm_render_replace_font(cterm, ).  No sequence parsing — just bulk
 * byte capture.  Returns true while accumulation is active.
 * ------------------------------------------------------------------------ */
bool
cterm_accumulate_font(struct cterminal *cterm, unsigned char byte, int *speed)
{
	(void)speed;
	cterm->fontbuf[cterm->font_read++] = byte;
	if (cterm->font_read == cterm->font_size) {
		char *buf2 = malloc(cterm->font_size);
		if (buf2 != NULL) {
			memcpy(buf2, cterm->fontbuf, cterm->font_size);
			if (cterm->font_slot >= CTERM_FIRST_DYNAMIC_FONT
			    && cterm->font_slot < 256)
				cterm_render_replace_font(cterm, (uint8_t)cterm->font_slot,
				    strdup("Remote Defined Font"),
				    buf2, cterm->font_size);
			else
				CTERM_FREE_AND_CLEAR(buf2);
		}
		cterm->font_size = 0;
		cterm->accumulator = NULL;
	}
	return true;
}

/* ========================================================================
 * SGR cascade — SyncTERM / XTerm / aixterm additions
 *
 * Catches the SGR parameters ECMA-48 doesn't define:
 *   38, 48       — extended (256-colour / truecolor) fg / bg; delegate
 *                  to cterm_parse_extended_colour which advances *pi
 *                  through the sub-parameters.
 *   90-97        — aixterm "bright" foreground colours.
 *   100-107      — aixterm "bright" background colours.
 * Anything else is passed down the chain to cterm_dec_apply_sgr.
 * ======================================================================== */

static void
set_bright_fg(struct cterminal *cterm, unsigned char colour, int flags)
{
	if (!(flags & ST_CONSOLE_VIDEO_NO_BRIGHT)) {
		cterm_set_fgattr(cterm, colour);
		if (!cterm->skypix)
			cterm->attr |= 8;
		cterm_render_attribute_colours(cterm, cterm->attr,
		    cterm->negative ? NULL : &cterm->fg_color,
		    cterm->negative ? &cterm->fg_color : NULL);
		CTERM_FREE_AND_CLEAR(cterm->fg_tc_str);
	}
}

static void
set_bright_bg(struct cterminal *cterm, unsigned char colour, int flags)
{
	if (flags & ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND) {
		cterm_set_bgattr(cterm, colour);
		if (!cterm->skypix)
			cterm->attr |= 128;
		cterm_render_attribute_colours(cterm, cterm->attr,
		    cterm->negative ? &cterm->bg_color : NULL,
		    cterm->negative ? NULL : &cterm->bg_color);
		CTERM_FREE_AND_CLEAR(cterm->bg_tc_str);
	}
}

void
cterm_cterm_apply_sgr(struct cterminal *cterm, int *pi)
{
	int flags = cterm_render_get_video_flags(cterm);

	switch (cterm->seq_param_int[*pi]) {
		case 38:
			cterm_parse_extended_colour(cterm, pi, true ^ cterm->negative);
			return;
		case 48:
			cterm_parse_extended_colour(cterm, pi, false ^ cterm->negative);
			return;
		case 90:  set_bright_fg(cterm, CTERM_COLOR_BLACK,     flags); return;
		case 91:  set_bright_fg(cterm, CTERM_COLOR_RED,       flags); return;
		case 92:  set_bright_fg(cterm, CTERM_COLOR_GREEN,     flags); return;
		case 93:  set_bright_fg(cterm, CTERM_COLOR_BROWN,     flags); return;
		case 94:  set_bright_fg(cterm, CTERM_COLOR_BLUE,      flags); return;
		case 95:  set_bright_fg(cterm, CTERM_COLOR_MAGENTA,   flags); return;
		case 96:  set_bright_fg(cterm, CTERM_COLOR_CYAN,      flags); return;
		case 97:  set_bright_fg(cterm, CTERM_COLOR_LIGHT_GRAY, flags); return;
		case 100: set_bright_bg(cterm, CTERM_COLOR_BLACK,     flags); return;
		case 101: set_bright_bg(cterm, CTERM_COLOR_RED,       flags); return;
		case 102: set_bright_bg(cterm, CTERM_COLOR_GREEN,     flags); return;
		case 103: set_bright_bg(cterm, CTERM_COLOR_BROWN,     flags); return;
		case 104: set_bright_bg(cterm, CTERM_COLOR_BLUE,      flags); return;
		case 105: set_bright_bg(cterm, CTERM_COLOR_MAGENTA,   flags); return;
		case 106: set_bright_bg(cterm, CTERM_COLOR_CYAN,      flags); return;
		case 107: set_bright_bg(cterm, CTERM_COLOR_LIGHT_GRAY, flags); return;
	}
	cterm_dec_apply_sgr(cterm, pi);
}
