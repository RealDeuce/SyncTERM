/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_HOST_INTERNAL_H_
#define ST_RIP3_HOST_INTERNAL_H_

#include "st_rip3_builtin_reference.h"
#include "st_rip3_variable_reference.h"
#include "st_rip3_table_host.h"
#include "st_rip3_host_variables.h"

/* Private decoder/executor statuses for parser-owned recovery and focused unit
 * tests. Never call these from the terminal/session input path or propagate
 * their statuses to it. The public process entry consumes rejected references
 * internally and continues; these results are not connection errors. */
int st_rip3_builtin_reference_parse(struct st_rip3_builtin_reference *result,
	const uint8_t *text, size_t length);
int st_rip3_variable_reference_parse(struct st_rip3_variable_reference *result,
	const uint8_t *text, size_t length);
int st_rip3_table_host_execute(const struct st_rip3_table_host *host,
	const struct st_rip3_builtin_reference *reference, char *result, size_t capacity);
int st_rip3_host_variables_execute(const struct st_rip3_host_variables *host,
	const struct st_rip3_builtin_reference *reference, char *result, size_t capacity);
int st_rip3_datetime_execute(const struct st_rip3_host_variables *host,
	const struct st_rip3_builtin_reference *reference, char *result, size_t capacity);
const char *st_rip3_datetime_zone(const struct st_rip3_host_variables *host);
int st_rip3_host_number(const char *text, unsigned minimum, unsigned maximum,
	unsigned *result);
int st_rip3_viewport_host_execute(const struct st_rip3_host_variables *host,
	const struct st_rip3_builtin_reference *reference, char *result, size_t capacity);

#endif
