/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_QUERY_H_
#define ST_RIP3_QUERY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
 * boundary. The evaluator owns the whole source across blocks/suspensions.
 * Outer angle preprocessing runs first, using the adopted eight-pass/16-frame
 * scanner with owned resumable comparisons and real built-in/user/template
 * providers. Skipped IF/ELSEIF headers validate without operand calls; inactive
 * named/template lookups retain their documented effects. Required prompts and
 * active delays suspend before later source reads or provider requests. Named
 * results rescan immediately; angle templates defer within a pass. Completed
 * preprocessing precedes structural block/ordinary preflight. It has no 4096
 * response cap; each later response receives that separate cap. Unchanged
 * angle-free input can bypass the worker without changing the stage result.
 * Preflight then checks supported structure and the first direct replacement.
 * Ordinary suffix syntax is checked when reached, after replacement clipping.
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
 * a 4096-byte composed response before expansion. $?N$ embeds a stored slot in
 * either ordinary pass. Every response is clipped to 4096 working bytes before
 * pass one and after every replacement; discarded suffixes never execute or
 * return after later decoding/shrinking. Definitions retain at most 4096 bytes.
 * A trailing outer backslash stays literal. A live radio group without a
 * selected member requests selection through the borrowed callback and aborts
 * the current host program, regardless of slot bytes. Selected empty radios
 * are valid. Grouped source definitions retain their original stable owner
 * across suspension; stale writes are skipped, never ordinary assignments.
 * Literal/unrecognized brackets pass through; incomplete slots diagnose without
 * suppressing bytes. Pick lists suspend ordinary expansion until the owner
 * provides a choice. Graphical consumers use their own one-pass worker.
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
/* True only for a prompt started by this query, never a busy borrowed
 * processor's unrelated prompt. Used by the owning session UI boundary. */
bool st_rip3_query_prompt_pending(const struct st_rip3_query *query);

enum { ST_RIP3_PICK_LIST_MAX_CHOICES = 64, ST_RIP3_PICK_LIST_DISPLAY_LIMIT = 60 };

struct st_rip3_pick_list_text {
	/* A normalized codepage byte can occupy up to four UTF-8 bytes. */
	char bytes[ST_RIP3_PICK_LIST_DISPLAY_LIMIT * 4 + 1];
	uint64_t highlighted; /* One bit per displayed character, low bit first. */
};

struct st_rip3_pick_list {
	struct st_rip3_pick_list_text question;
	struct st_rip3_pick_list_text labels[ST_RIP3_PICK_LIST_MAX_CHOICES];
	size_t count;
	bool required, has_x, has_y;
	uint16_t x, y;
};

/* Borrowed until selection or cancellation. Index count requests Escape.
 * Required lists ignore Escape. A UI failure aborts only this expression. */
const struct st_rip3_pick_list *st_rip3_query_pick_list(const struct st_rip3_query *query);
bool st_rip3_query_choose(struct st_rip3_query *query, size_t index);
void st_rip3_query_pick_list_failed(struct st_rip3_query *query, int status);
void st_rip3_query_set_codepage_converted(struct st_rip3_query *query, bool converted);
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
