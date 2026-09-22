/* Strict, locale-independent textual value conversions. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef SYNCTERM_VALUE_CONVERT_H_
#define SYNCTERM_VALUE_CONVERT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

bool value_parse_int64(const char *text, int64_t *result);
bool value_parse_uint64(const char *text, uint64_t *result);
bool value_parse_bool(const char *text, bool *result);
bool value_parse_double(const char *text, double *result);
bool value_parse_duration(const char *text, double *seconds);
/* Valid YYYYMMDD local dates take precedence; other integers are epochs. */
bool value_parse_datetime(const char *text, time_t *result);

bool value_format_int64(char *buffer, size_t size, int64_t value);
bool value_format_uint64(char *buffer, size_t size, uint64_t value);
bool value_format_double(char *buffer, size_t size, double value);
bool value_format_duration(char *buffer, size_t size, double seconds);
/* Byte values below 1024 are integers; scaled values have one decimal digit. */
bool value_format_byte_estimate(char *buffer, size_t size, uint64_t bytes);
/* Negative values and NaN become zero; positive infinity is written as infy. */
bool value_format_duration_estimate(char *buffer, size_t size,
    double seconds);
bool value_format_datetime(char *buffer, size_t size, time_t value,
    bool include_time);

#endif
