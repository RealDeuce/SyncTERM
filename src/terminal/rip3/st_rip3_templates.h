/* Owned slot templates and intended, bounded chaining composition. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_TEMPLATES_H_
#define ST_RIP3_TEMPLATES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_templates;
struct st_rip3_template_bytes { const uint8_t *bytes; size_t length; };

/* Optional borrowed query bindings. The button/group owner sets required for
 * radio groups and supplies current selected definitions in creation order.
 * The query calls selection_required on a missing required value before
 * aborting that host command nonfatally. The callback may show/highlight the
 * group, but must not reenter processing or mutate bindings/slots. */
struct st_rip3_template_host {
	struct st_rip3_templates *slots;
	void *context;
	void (*selection_required)(void *context, unsigned group);
};

/* One session thread owns 36 slots. New slots are empty and not required.
 * No GUI, network, expansion or button state is owned by this table. */
int st_rip3_templates_create(struct st_rip3_templates **result);
void st_rip3_templates_destroy(struct st_rip3_templates *templates);
/* Copy at most the first 4096 unexpanded definition bytes. First NUL within
 * that bound ends a C-string definition. Replacing it retains
 * the group's required flag. Failure preserves the old definition. */
int st_rip3_templates_set(struct st_rip3_templates *templates, unsigned number,
	const uint8_t *bytes, size_t length);
int st_rip3_templates_required(struct st_rip3_templates *templates, unsigned number, bool required);
/* Borrowed view, valid until this slot changes or the table is destroyed.
 * Empty ordinary/check-box slots resolve successfully to zero bytes; empty
 * required slots return NOT_FOUND, preserving result. No UI callback here. */
int st_rip3_templates_resolve(const struct st_rip3_templates *templates, unsigned number,
	struct st_rip3_template_bytes *result);
/* Compose 1..36 numeric slot indexes in list order, replacing every literal
 * $?$ in a template with its current feed. A template without a marker discards
 * the feed. Insertions are not rescanned within that step. Correct the native
 * wrong-buffer offset defect using intended composition; never emulate unsafe
 * insertion. Only the first 4096 result bytes survive each composition.
 * Outputs use caller storage, capacity>=4096; inputs/results must not overlap.
 * Preflight/stack preparation precedes publication. On failure, result/length
 * are unchanged; missing receives the failed required group only for NOT_FOUND.
 * Internal helper statuses must remain below void parser/session boundaries. */
int st_rip3_templates_chain(const struct st_rip3_templates *templates,
	const uint8_t *numbers, size_t count, const uint8_t *feed, size_t feed_length,
	uint8_t *result, size_t capacity, size_t *length, unsigned *missing);

#endif
