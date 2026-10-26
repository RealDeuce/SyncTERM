/* Exact, locale-independent decimal conversion helpers. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef SYNCTERM_DECIMAL_DOUBLE_H_
#define SYNCTERM_DECIMAL_DOUBLE_H_

#include <stdbool.h>
#include <stddef.h>

bool decimal_parse_double(const char *begin, const char *end, double *result);
bool decimal_format_double(char *buffer, size_t size, double value,
    int precision);
bool decimal_format_fixed(char *buffer, size_t size, double value,
    unsigned places);

#endif
