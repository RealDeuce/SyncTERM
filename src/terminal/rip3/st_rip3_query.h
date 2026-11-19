/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_QUERY_H_
#define ST_RIP3_QUERY_H_

#include <stdbool.h>
#include <stddef.h>

struct st_rip3_query;
struct st_rip3_host_variables;
struct st_rip3_host_sink;

/* One session thread owns the evaluator and its pending host text. Bindings,
 * callbacks and their contexts are borrowed and must outlive it. The input
 * gate in variables must be the same gate used by the owning source driver.
 * Bindings cannot change or reenter this object from callbacks. Host state
 * is read when each reference executes, including after a suspension.
 *
 * Commands submit complete immediate expressions through the private parser
 * boundary. The evaluator copies and preflights the entire expression before
 * effects, retaining its own bytes even when the command buffer is reused.
 * This branch supports literals, documented control notation and ordinary
 * known built-ins plus user references when variables.user_variables is bound.
 * Known built-ins never fall back to same-named user values after rejection.
 * The optional processor is reserved exclusively while this query is pending;
 * the owner supplies its dialog/editor and resumes responses through it. A
 * pending last-token prompt still makes pending() true. User values and built-in
 * replies pass through unchanged, without recursive interpretation in this pass.
 * Angle/template/pick-list/playback dispatch, outer transport unquoting and
 * complete host two-pass processing remain separate; unsupported introducers
 * reject before partial output. Extended syntax on known built-in names likewise
 * rejects rather than entering the user path.
 * Syntax/operation errors never return to a terminal/session failure path. */
int st_rip3_query_create(struct st_rip3_query **result,
	const struct st_rip3_host_variables *variables, const struct st_rip3_host_sink *sink);
void st_rip3_query_destroy(struct st_rip3_query *query);
/* Release pending text and abort only a user operation begun by this query.
 * Do not reset the borrowed input gate or another consumer's pending prompt.
 * A value already accepted into a store is not rolled back. The session owner
 * coordinates cancellation/reset of its queue, parser, dialog and input gate. */
void st_rip3_query_cancel(struct st_rip3_query *query);
bool st_rip3_query_pending(const struct st_rip3_query *query);
/* Resume at most budget tokens, stopping at a delay or pending prompt. The owner
 * resumes this before fetching more source bytes. A final D may leave a wait
 * even after pending() becomes false; the input driver must recheck its gate.
 * When pending and ready, schedule another pump even without new input bytes.
 * No evaluator allocation, sleeping, threads or terminal-error status during
 * resume. User format/store adapters may allocate; failures stay nonfatal. A
 * resumed user result consumes one budget token, including suppressed output. */
void st_rip3_query_process(struct st_rip3_query *query, size_t budget);

#endif
