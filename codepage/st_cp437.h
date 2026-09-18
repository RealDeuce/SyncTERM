/* Locale-independent IBM Code Page 437 conversion. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_CP437_H_
#define ST_CP437_H_

#include <stdbool.h>
#include <stdint.h>

/*
 * Bytes 0x00 through 0x1f and 0x7f are controls unless graphic_controls
 * is true, in which case the IBM display glyphs are returned.
 */
uint32_t st_cp437_to_unicode(uint8_t value, bool graphic_controls);

/* Uses IBM display glyphs for the control-byte range. */
bool st_cp437_from_unicode(uint8_t *result, uint32_t codepoint);

#endif
