/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_VARIABLES_H_
#define ST_RIP3_VARIABLES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_variables;

struct st_rip3_variable_value {
	size_t length;
	/* Original protocol bytes, plus a convenience terminator at [length].
	 * Explicit length preserves embedded NUL/CP437 bytes; unused bytes are zero. */
	uint8_t bytes[256];
};

struct st_rip3_variable_entry {
	char name[21]; /* Canonical uppercase ASCII identifier. */
	struct st_rip3_variable_value value;
	bool require_confirmation;
};

struct st_rip3_variable_snapshot {
	struct st_rip3_variable_entry *entries;
	size_t count;
};

/* Owned key/value table. One session thread owns each table. Session and
 * persistent stores can use separate instances; lifetime, prompts, approvals,
 * built-in precedence and disk I/O are interpreter/client policies. Nothing in
 * this module expands values, prompts, transmits data or interprets directives. */
int st_rip3_variables_create(struct st_rip3_variables **result);
void st_rip3_variables_destroy(struct st_rip3_variables *variables);
/* Names are 1..20 ASCII letters/digits/underscore, beginning with a letter.
 * Case folds independently of locale. Oversized values truncate to the first
 * 255 protocol bytes as specified; names are rejected rather than truncated.
 * Existing values are replaced only after all validation/allocation succeeds. */
int st_rip3_variables_set(struct st_rip3_variables *variables,
	const char *name, size_t name_length, const uint8_t *value, size_t value_length);
/* Update the live definition and, if supplied, its distinct persistent
 * record. A failed write leaves both records intact. */
int st_rip3_variables_define(struct st_rip3_variables *variables,
	struct st_rip3_variables *persistent, const char *name, size_t name_length,
	const uint8_t *value, size_t value_length, bool require_confirmation);
int st_rip3_variables_get(const struct st_rip3_variables *variables,
	const char *name, size_t name_length, struct st_rip3_variable_value *result);
int st_rip3_variables_get_entry(const struct st_rip3_variables *variables,
	const char *name, size_t name_length, struct st_rip3_variable_entry *result);
int st_rip3_variables_remove(struct st_rip3_variables *variables,
	const char *name, size_t name_length);
int st_rip3_variables_count(const struct st_rip3_variables *variables, size_t *result);
/* Owned, sorted, independent records for a client persistence adapter. A new
 * table can be prepared atomically from a snapshot; malformed/duplicate names
 * or lengths reject the entire import. These are memory snapshots, not a file
 * format or a claim of on-disk persistence. Destroy successful outputs before
 * reuse. All error paths leave caller results unchanged. */
int st_rip3_variables_snapshot(const struct st_rip3_variables *variables,
	struct st_rip3_variable_snapshot *result);
void st_rip3_variables_snapshot_free(struct st_rip3_variable_snapshot *snapshot);
int st_rip3_variables_from_snapshot(struct st_rip3_variables **result,
	const struct st_rip3_variable_snapshot *snapshot);

#endif
