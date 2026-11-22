/* Owned slot templates and intended, bounded chaining composition. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_TEMPLATES_H_
#define ST_RIP3_TEMPLATES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_templates;
struct st_rip3_template_bytes { const uint8_t *bytes; size_t length; };

/* Optional borrowed query bindings. Stable grouped owners supply selection
 * and current associated definitions in creation order.
 * The query calls selection_required on a missing required selection before
 * aborting that host command nonfatally. The callback may show/highlight the
 * group, but must not reenter processing or mutate bindings/slots. */
struct st_rip3_template_host {
	struct st_rip3_templates *slots;
	void *context;
	void (*selection_required)(void *context, unsigned group);
};

/* One session thread owns 36 baselines/publications and grouped logical
 * selection/associations. Mouse fields retain rendering/input ownership.
 * No GUI, network or provider expansion is performed by this table. */
int st_rip3_templates_create(struct st_rip3_templates **result);
void st_rip3_templates_destroy(struct st_rip3_templates *templates);
/* Copy at most the first 4096 unexpanded definition bytes. First NUL within
 * that bound ends a C-string definition. Replacing it retains
 * group membership/selection. Failure preserves the old definition. */
int st_rip3_templates_set(struct st_rip3_templates *templates, unsigned number,
	const uint8_t *bytes, size_t length);
/* Borrowed view, valid until the next lookup, mutation or table destruction.
 * Empty ordinary/check-box slots resolve successfully to zero bytes. A live
 * radio group without selection returns NOT_FOUND, even with ordinary bytes;
 * a selected radio may resolve empty. Failure preserves result. No UI callback here. */
int st_rip3_templates_resolve(const struct st_rip3_templates *templates, unsigned number,
	struct st_rip3_template_bytes *result);
/* Group state is keyed by stable mouse-instance token, never public FIELDID.
 * Logical selection and associated bodies have this single owner. All methods
 * are single-threaded and run no host/render providers. */
enum st_rip3_group_kind { ST_RIP3_GROUP_NONE, ST_RIP3_GROUP_CHECKBOX, ST_RIP3_GROUP_RADIO };
int st_rip3_templates_group_check(const struct st_rip3_templates *,unsigned group,enum st_rip3_group_kind kind);
int st_rip3_templates_button_add(struct st_rip3_templates *,uint64_t token,unsigned group,
	enum st_rip3_group_kind kind,bool selected,uint64_t redraw[128],size_t *count);
void st_rip3_templates_button_remove(struct st_rip3_templates *,uint64_t token);
bool st_rip3_templates_button_selected(const struct st_rip3_templates *,uint64_t token);
int st_rip3_templates_button_activate(struct st_rip3_templates *,uint64_t token,uint64_t redraw[128],size_t *count);
/* Stale associated writes are successful no-ops, never ordinary assignments. */
int st_rip3_templates_associate(struct st_rip3_templates *,uint64_t token,unsigned number,
	const uint8_t *bytes,size_t length);
/* Full drawing reset preserves effective bytes as ordinary baselines before
 * owners are deleted. Allocation failure preserves every publication/owner. */
int st_rip3_templates_preserve(struct st_rip3_templates *);

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
