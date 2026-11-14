/* RIPterm descriptor-driven base-36 fields. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_RIP_ARGS_H_
#define CTERM_RIP_ARGS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Values retain a protocol WORD, interpreted mathematically as signed16.
 * Consumers must explicitly convert bitfields/bytes and bound indexes.
 * Failure leaves cursor/value unchanged; a valid -1 is not an error. */
int cterm_rip_signed_word(uint16_t word);
int cterm_rip_signed_byte(uint8_t byte);
bool cterm_rip_read_number(const char **cursor, size_t width, int *value);
/* Advances by actual consumed bytes, including a shortened final field.
 * A consumer may use the resulting cursor as its text suffix. */
size_t cterm_rip_read_fields(const char **cursor, const uint8_t *widths,
    size_t count, int *values);

#endif
