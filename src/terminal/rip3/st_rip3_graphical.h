/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_GRAPHICAL_H
#define ST_RIP3_GRAPHICAL_H

#include "st_rip3_host_variables.h"
#include "st_rip3_condition.h"
#include <stdbool.h>

struct st_rip3_graphical;
enum st_rip3_graphical_consumer { ST_RIP3_GRAPHICAL_FILENAME, ST_RIP3_GRAPHICAL_LABEL,
	ST_RIP3_GRAPHICAL_TEXT, ST_RIP3_GRAPHICAL_METRIC };
enum st_rip3_graphical_state { ST_RIP3_GRAPHICAL_IDLE, ST_RIP3_GRAPHICAL_RUNNING,
	ST_RIP3_GRAPHICAL_PROMPT, ST_RIP3_GRAPHICAL_COMPLETE, ST_RIP3_GRAPHICAL_FAILED };

/* One interpreter owns the worker and borrowed services. It owns source and
 * working bytes and reserves only user operations it started. The sink observes
 * diagnostics; no host reply or click program is emitted. No renderer/table
 * lock may be held across calls. The owner freezes consumer drawing state and
 * gates later interpreter/input work until completion, failure or cancellation.
 * Raw button bounds and the metric TEXTDATA bypass belong to their consumers. */
int st_rip3_graphical_create(struct st_rip3_graphical **result,
	const struct st_rip3_host_variables *variables, const struct st_rip3_host_sink *sink,
	bool converted_codepage);
void st_rip3_graphical_destroy(struct st_rip3_graphical *worker);
void st_rip3_graphical_cancel(struct st_rip3_graphical *worker);
void st_rip3_graphical_start(struct st_rip3_graphical *worker,
	enum st_rip3_graphical_consumer consumer, const uint8_t *bytes, size_t length);
void st_rip3_graphical_process(struct st_rip3_graphical *worker, size_t budget);
enum st_rip3_graphical_state st_rip3_graphical_state(const struct st_rip3_graphical *worker);
bool st_rip3_graphical_pending(const struct st_rip3_graphical *worker);
const struct st_rip3_condition_bytes *st_rip3_graphical_result(const struct st_rip3_graphical *worker);

#endif
