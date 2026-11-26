/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_HOST_INTERNAL_H_
#define ST_RIP3_HOST_INTERNAL_H_

#include "st_rip3_builtin_reference.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_table_host.h"
#include "st_rip3_variable_reference.h"

struct st_rip3_ports;

/* Private decoder/executor statuses for parser-owned recovery and focused unit
 * tests. Never call these from the terminal/session input path or propagate
 * their statuses to it. The public process entry consumes rejected references
 * internally and continues; these results are not connection errors. */
int st_rip3_builtin_reference_parse_encoded(struct st_rip3_builtin_reference *result, const uint8_t *text,
    size_t length, bool converted, size_t *rejected_span);
int st_rip3_builtin_reference_parse(struct st_rip3_builtin_reference *result, const uint8_t *text, size_t length);
/* Existing numeric/keyword handlers normalize their own parameter copies.
 * Raw parser slots retain spaces; literal/path handlers must not use this to
 * discard significant whitespace. Empty normalized words remain present. */
int st_rip3_builtin_reference_words(struct st_rip3_builtin_reference *result,
    const struct st_rip3_builtin_reference                           *reference);
int st_rip3_variable_reference_parse(struct st_rip3_variable_reference *result, const uint8_t *text, size_t length,
    bool utf8);
int st_rip3_variable_definition_parse(struct st_rip3_variable_reference *result, const uint8_t *text,
    size_t length, bool utf8, bool converted);
int st_rip3_table_host_execute(const struct st_rip3_table_host *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity);
int st_rip3_host_variables_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity);
size_t st_rip3_directory_normalize(uint8_t *text, size_t length);
int    st_rip3_directory_select(const struct st_rip3_host_directory *directory, const uint8_t *text, size_t length,
    bool converted, bool create);
int    st_rip3_random_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity);
int    st_rip3_local_variables_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity);
int    st_rip3_datetime_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity);

enum {
	ST_RIP3_TIME_ZONE_CAPACITY = 256
};

const char *st_rip3_datetime_zone(const struct st_rip3_host_variables *host, char *zone, size_t capacity);
/* Select a borrowed, complete TERMINFO phrase for immediate parser emission. */
int st_rip3_terminfo_select(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, const char **result);
int st_rip3_host_number(const char *text, unsigned minimum, unsigned maximum, unsigned *result);
int st_rip3_viewport_host_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity);
int st_rip3_port_query_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity);
int st_rip3_port_query_frame(const struct st_rip3_host_variables *host, const struct st_rip3_ports *ports,
    struct st_rip3_frame *result);
int st_rip3_image_style_host_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity);
int st_rip3_environment_host_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity);
int st_rip3_text_window_host_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity);

#endif
