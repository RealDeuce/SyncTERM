/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_CLUSTER_COLUMNS_H_
#define ST_CLUSTER_COLUMNS_H_

#include "st_unicode_grapheme.h"
#include "st_unicode_width.h"
#include "st_utf8.h"

/* NFC grapheme bytes. Keep isolated non-ignorable marks drawable, but do not
 * let attached marks, selectors or joiners replace the base's width.
 */
static uint8_t
cluster_columns(const uint8_t *bytes, size_t size)
{
	uint8_t isolated = 0;
	while (size != 0) {
		uint32_t scalar;
		size_t consumed;
		enum st_unicode_grapheme_break property;
		uint8_t columns;

		if (st_utf8_decode_one(&scalar, &consumed, bytes, size) != 0)
			break;
		bytes += consumed;
		size -= consumed;
		property = st_unicode_grapheme_break(scalar);
		columns = st_unicode_columns(scalar);
		if (property != ST_UNICODE_GRAPHEME_EXTEND
		    && property != ST_UNICODE_GRAPHEME_ZWJ && columns != 0)
			return columns;
		if (!st_unicode_default_ignorable(scalar)
		    && (st_unicode_nonspacing_mark(scalar) || columns != 0))
			isolated = 1;
	}
	return isolated;
}

#endif
