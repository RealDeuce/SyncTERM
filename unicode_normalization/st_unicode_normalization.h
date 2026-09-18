/* Canonical Unicode normalization primitives. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_UNICODE_NORMALIZATION_H_
#define ST_UNICODE_NORMALIZATION_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

uint8_t st_unicode_canonical_combining_class(uint32_t codepoint);

/*
 * Returns the full canonical decomposition length, or zero for a non-scalar.
 * If capacity is sufficient, writes the decomposition to output.  A scalar
 * with no decomposition produces itself.  Output may be NULL only when
 * capacity is zero.
 */
size_t st_unicode_canonical_decompose(uint32_t codepoint, uint32_t *output,
	size_t capacity);

/* Returns true only when the pair has a canonical NFC composition. */
bool st_unicode_canonical_compose(uint32_t first, uint32_t second,
	uint32_t *composition);

#endif
