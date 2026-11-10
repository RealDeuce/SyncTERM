/****************************************************************************
 * @format.tab-size 4		(Plain Text/Source Code File Header)			*
 * @format.use-tabs true	(see http://www.synchro.net/ptsc_hdr.html)		*
 *																			*
 * Copyright (c) 2026 Stephen Hurd							*
 *																			*
 * SPDX-License-Identifier: BSD-2-Clause								*
 ****************************************************************************/

/*
 * cterm_vt52.h — VT52 / Atari ST VT52 single-byte dispatcher and the
 * per-emulation dispatch table.  The sequence-parsing path is shared
 * with ECMA-48 via seq_feed() / cterm_accumulate_ecma_seq (in cterm.c);
 * only the dispatcher and table differ.
 */

#ifndef _CTERM_VT52_H_
#define _CTERM_VT52_H_

#include <stddef.h>
#include <stdint.h>
#include "cterm.h"
#include "cterm_internal.h"	/* struct seq_dispatch */

void cterm_dispatch_vt52(struct cterminal *cterm, unsigned char byte,
    int *speed);

void cterm_vt52_reset_console(struct cterminal *cterm);
uint32_t cterm_vt52_background_color(const struct cterminal *cterm);

/* BIOS-style output uses the current persistent VT52 parser and Atari bytes;
 * it is generated output, not another pass through wire UTF-8 or input hooks. */
int cterm_vt52_output(struct cterminal *, const uint8_t *, size_t);

extern const struct seq_dispatch cterm_st_vt52_dispatch[];
extern const size_t cterm_st_vt52_dispatch_len;

#endif /* _CTERM_VT52_H_ */
