/* Private UTF-8 presentation and terminal placement. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_TEXT_H_
#define CTERM_TEXT_H_

#include <stddef.h>
#include <stdint.h>

struct cterminal;

int cterm_text_create(struct cterminal *cterm);
int cterm_text_boundary(struct cterminal *cterm);
int cterm_text_feed(struct cterminal *cterm, const void *text, size_t size,
	size_t *consumed);
void cterm_text_byte(struct cterminal *cterm, uint8_t byte);
void cterm_text_literal(struct cterminal *cterm, uint8_t byte);
void cterm_text_repeat(struct cterminal *cterm, size_t count);
void cterm_text_forget(struct cterminal *cterm);
void cterm_text_destroy(struct cterminal *cterm);
void cterm_text_scrolled(struct cterminal *cterm, int top, int bottom);

#endif
