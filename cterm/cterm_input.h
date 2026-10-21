/* Private input sources; every source shares routing and text placement. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_INPUT_H_
#define CTERM_INPUT_H_

#include <stddef.h>

struct cterminal;
enum cterm_input_source {
	CTERM_INPUT_WIRE,
	CTERM_INPUT_SEMANTIC_UTF8,
	CTERM_INPUT_REPLAY,
};

int cterm_input_write(struct cterminal *terminal, const void *bytes,
	size_t size, size_t *consumed, enum cterm_input_source source);
int cterm_input_decode_error(struct cterminal *terminal);

#endif
