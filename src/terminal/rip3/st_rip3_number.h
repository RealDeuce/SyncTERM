/* RIP3 numeric representations; independent of legacy WORD coercion. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_NUMBER_H
#define ST_RIP3_NUMBER_H

#include <stddef.h>
#include <stdint.h>

/* Decode a nonempty digit span in explicit radix 36 or 64. Base36 accepts
 * either letter case; base64 distinguishes case and ends in #, &. No signs,
 * whitespace, terminators or legacy '-' pseudo-digits are consumed. A caller
 * selects the radix/field bounds and handles shortening/reserved parameters.
 * The representation result is not truncated to a legacy signed WORD.
 * Both operations leave their outputs unchanged on error. */
int st_rip3_number_decode(uint64_t *result, const void *bytes, size_t size, unsigned radix);
/* Write exactly width digits, zero padded, without a NUL terminator. */
int st_rip3_number_encode(char *output, size_t capacity, uint64_t value, size_t width, unsigned radix);

#endif
