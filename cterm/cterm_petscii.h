/****************************************************************************
 * @format.tab-size 4		(Plain Text/Source Code File Header)			*
 * @format.use-tabs true	(see http://www.synchro.net/ptsc_hdr.html)		*
 *																			*
 * Copyright (c) 2026 Stephen Hurd							*
 *																			*
 * SPDX-License-Identifier: BSD-2-Clause								*
 ****************************************************************************/

/*
 * cterm_petscii.h — PETSCII control-byte bitmap, handler table, and
 * the C64 reverse-video attribute helpers shared with the byte-dispatch
 * print path in cterm.c.
 */

#ifndef _CTERM_PETSCII_H_
#define _CTERM_PETSCII_H_

#include <stdbool.h>
#include <stdint.h>
#include "cterm.h"

extern const uint8_t cterm_petscii_ctrl_bitmap[32];
extern void (*const cterm_petscii_handlers[256])(struct cterminal *);

void cterm_c64_set_reverse(struct cterminal *cterm, bool on);
uint8_t cterm_c64_get_attr(struct cterminal *cterm);

#endif /* _CTERM_PETSCII_H_ */
