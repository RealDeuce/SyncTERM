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
 * boundary. The evaluator owns the whole source across blocks/suspensions and
 * preflights its supported structure and direct-text syntax before effects.
 * Definitions retain raw ordinary text until use; later chain composition may
 * discard a feed without executing its variables. Caller buffers may be reused.
 * This branch supports literals, documented control notation and ordinary
 * known built-ins plus user references when variables.user_variables is bound.
 * Known built-ins never fall back to same-named user values after rejection.
 * The optional processor is reserved exclusively while this query is pending;
 * the owner supplies its dialog/editor and resumes responses through it. A
 * pending last-token prompt still makes pending() true. User values and built-in
 * replies are skipped for the remainder of their producing ordinary pass.
 * Exactly two ordinary passes build each response before transmission. Pass one
 * defers outer control/backslash decoding while retaining dollar-local quoting;
 * pass two decodes ASCII control pairs and outer quotes. No third pass or angle/
 * bracket rescan occurs on inserted values. A final D also delays transmission.
 * Complete [] direct blocks, [N:] definitions and [ABC] chains are segmented
 * before ordinary expansion, independently of preceding ESC/backslashes. Slot
 * definitions/chains require variables.templates; direct blocks do not. Each
 * response has its own two passes. Chaining uses intended $?$ composition,
 * correcting the native wrong-buffer defect, with at most 36 listed slots and
 * a 4096-byte composed response before expansion. Required empty groups request
 * selection through the borrowed callback and abort the current host program.
 * Literal/unrecognized brackets pass through; incomplete slots diagnose without
 * suppressing bytes. Angle preprocessing, $?N$ embedding (limit timing under
 * #93), pick lists, playback and button-associated aggregation remain separate.
 * Unsupported extended syntax on known names never enters the user path.
 * Syntax/operation errors never return to a terminal/session failure path. */
int st_rip3_query_create(struct st_rip3_query **result,
	const struct st_rip3_host_variables *variables, const struct st_rip3_host_sink *sink);
void st_rip3_query_destroy(struct st_rip3_query *query);
/* Release pending text and abort only a user operation begun by this query.
 * Do not reset the borrowed input gate or another consumer's pending prompt.
 * Previously executed actions/accepted store values are not rolled back. The owner
 * coordinates cancellation/reset of its queue, parser, dialog and input gate. */
void st_rip3_query_cancel(struct st_rip3_query *query);
bool st_rip3_query_pending(const struct st_rip3_query *query);
/* Resume at most budget tokens, stopping at a delay or pending prompt. The owner
 * resumes this before fetching more source bytes. A final D may leave a wait
 * even after pending() becomes false; the input driver must recheck its gate.
 * When pending and ready, schedule another pump even without new input bytes.
 * Response buffers and user adapters may allocate during resume. Resource errors
 * discard the unfinished response nonfatally; no buffered prefix is transmitted.
 * Previously executed active effects are not rolled back. No sleeping, threads
 * or terminal-error status. A resumed user result consumes one budget token,
 * including suppressed output. Preparing one command block consumes one token;
 * ordinary pass transitions themselves consume none. */
void st_rip3_query_process(struct st_rip3_query *query, size_t budget);

#endif
