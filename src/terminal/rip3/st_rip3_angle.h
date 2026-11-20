/* Owned, resumable outer angle preprocessing; no transport or UI ownership. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_ANGLE_H_
#define ST_RIP3_ANGLE_H_

#include "st_rip3_condition.h"

struct st_rip3_angle;
enum st_rip3_angle_state { ST_RIP3_ANGLE_RUNNING, ST_RIP3_ANGLE_VALUE,
	ST_RIP3_ANGLE_COMPLETE, ST_RIP3_ANGLE_FAILED };
enum st_rip3_angle_diagnostic { ST_RIP3_ANGLE_INVALID, ST_RIP3_ANGLE_OPERATION,
	ST_RIP3_ANGLE_RESOURCE };
enum st_rip3_angle_value_kind { ST_RIP3_ANGLE_REFERENCE, ST_RIP3_ANGLE_TEMPLATE };

struct st_rip3_angle_request {
	enum st_rip3_angle_value_kind kind;
	bool comparison;
	unsigned number;
	struct st_rip3_condition_bytes reference;
};

/* A factory copies the complete bounded C-string source. No provider runs.
 * Failed creation preserves result. One owner thread invokes all methods.
 * The owner supplies the consumer's permitted provider classes and owns its
 * input gate, user operation, cancellation and callback diagnostics. */
int st_rip3_angle_create(struct st_rip3_angle **result, const uint8_t *bytes, size_t length);
void st_rip3_angle_destroy(struct st_rip3_angle *angle);
/* At most budget scanner/reduction steps. Stop at each provider request; no
 * callback, sleep or thread. Source/storage/structure failures become FAILED
 * internally, never returned to the caller as a session error. Sixteen open
 * IF frames and eight whole passes implement the adopted portable contract.
 * Named insertions rescan immediately; template insertions defer one pass.
 * A cyclic named expansion can remain pending; finite budgets keep each call
 * bounded and the owner may destroy/cancel it without executing more work. */
void st_rip3_angle_process(struct st_rip3_angle *angle, size_t budget);
enum st_rip3_angle_state st_rip3_angle_state(const struct st_rip3_angle *angle);
const struct st_rip3_angle_request *st_rip3_angle_request(const struct st_rip3_angle *angle);
/* Borrowed provider bytes are consumed/copied before returning; no provider is
 * replayed. NUL ends a successful value. A comparison failure/overlong value
 * yields false, independently of named insertion failure, which aborts this
 * preprocessing operation. Supply only in VALUE; other calls are no-ops.
 * A resource status for a named insertion is diagnosed as RESOURCE. */
void st_rip3_angle_supply(struct st_rip3_angle *angle,
	const struct st_rip3_condition_bytes *value, int status);
/* Borrowed completed text, or NULL before completion. An unfinished/failed
 * operation never publishes partial output; prior provider effects are not
 * rolled back. No response-size cap is imposed at this earlier stage. */
const struct st_rip3_condition_bytes *st_rip3_angle_result(const struct st_rip3_angle *angle);
enum st_rip3_angle_diagnostic st_rip3_angle_diagnostic(const struct st_rip3_angle *angle);

#endif
