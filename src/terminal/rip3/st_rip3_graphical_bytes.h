/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_GRAPHICAL_BYTES_H
#define ST_RIP3_GRAPHICAL_BYTES_H

#include "st_utf8.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The terminal adapter normalizes each original codepage byte to one UTF-8
 * scalar. Recover that byte budget without changing stored/renderer encoding.
 * Native byte input and actual UTF-8 wire input retain ordinary byte limits.
 * Invalid normalized data is left for the consumer's existing encoding check. */
static inline size_t
st_rip3_graphical_character(const uint8_t *bytes, size_t length)
{
	if (bytes[0]<0x80) return 1;
	uint32_t scalar;size_t used;
	return st_utf8_decode_one(&scalar,&used,bytes,length)==0 ? used : 1;
}
static inline size_t
st_rip3_graphical_prefix(const uint8_t *bytes, size_t length, size_t budget, bool converted)
{
	if (length<=budget) return length;
	if (!converted) return budget;
	size_t at=0;
	while (at<length && budget!=0) {at+=st_rip3_graphical_character(bytes+at,length-at);budget--;}
	return at;
}
static inline size_t
st_rip3_graphical_units(const uint8_t *bytes, size_t length, bool converted)
{
	if (!converted) return length;
	size_t at=0,units=0;
	while (at<length) {at+=st_rip3_graphical_character(bytes+at,length-at);units++;}
	return units;
}
#endif
