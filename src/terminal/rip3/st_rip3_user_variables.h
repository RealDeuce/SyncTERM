/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_USER_VARIABLES_H_
#define ST_RIP3_USER_VARIABLES_H_

#include "st_rip3_variable_reference.h"
#include "st_rip3_variables.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_user_variables;

enum st_rip3_user_variable_state {
	ST_RIP3_USER_VARIABLE_IDLE,
	ST_RIP3_USER_VARIABLE_PROMPT,
	ST_RIP3_USER_VARIABLE_COMPLETE,
};

enum st_rip3_user_variable_diagnostic {
	ST_RIP3_USER_VARIABLE_INVALID,
	ST_RIP3_USER_VARIABLE_BUSY,
	ST_RIP3_USER_VARIABLE_OPERATION_FAILED,
	ST_RIP3_USER_VARIABLE_RESOURCE_FAILED,
	ST_RIP3_USER_VARIABLE_CANCELLATION_DISABLED,
};

enum st_rip3_user_variable_source {
	ST_RIP3_USER_VARIABLE_EXISTING,
	ST_RIP3_USER_VARIABLE_DEFAULT,
	ST_RIP3_USER_VARIABLE_RESPONSE,
};

struct st_rip3_user_variable_host {
	/* Two distinct, borrowed tables, outliving this processor. Session lookup
	 * precedes persistent lookup regardless of the requested destination.
	 * This component writes memory; the client owns persistent disk flushing. */
	struct st_rip3_variables *session, *persistent;
	void *context;
	/* Optional format/editor adapter, called once for each successful logical
	 * value before storage/output. It owns mode/conversion enforcement, using
	 * source to distinguish retrieval/defaults from entered responses. It must
	 * preserve input and return an owned value of at most 255 bytes. Without
	 * it, only ANY/unconverted values are supported. Prompt UI, world-coordinate
	 * placement, width, masking and format policy remain client responsibilities.
	 * No callback may reenter processing or mutate the bound stores. */
	int (*format)(void *context, const struct st_rip3_variable_reference *reference,
		enum st_rip3_user_variable_source source,
		const struct st_rip3_variable_value *input, struct st_rip3_variable_value *result);
	void (*diagnostic)(void *context, enum st_rip3_user_variable_diagnostic kind);
};

/* One interpreter thread owns the processor. Built-in/template dispatch and
 * reference decoding precede it. Factory/accessor services are separate from
 * the void, nonfatal processing boundary. No dialog, network or disk I/O. */
int st_rip3_user_variables_create(struct st_rip3_user_variables **result,
	const struct st_rip3_user_variable_host *host);
void st_rip3_user_variables_destroy(struct st_rip3_user_variables *variables);
/* Start one decoded reference. Every failure is diagnosed/consumed here.
 * Prompt state owns copies of the reference and initial value, so source
 * storage may be released. A busy start preserves the pending operation.
 * Nonbusy starts discard any previous result; invalid starts become idle. */
void st_rip3_user_variables_process(struct st_rip3_user_variables *variables,
	const struct st_rip3_variable_reference *reference);
/* Start a decoded 1D definition with the same stores and editor. The
 * caller supplies SET and the destination flag, and owns prompt recovery. */
void st_rip3_user_variables_define(struct st_rip3_user_variables *variables,
	const struct st_rip3_variable_reference *reference, bool interactive, bool utf8, bool converted);
/* Respond only in PROMPT state. Values truncate to 255 protocol bytes before
 * the format adapter. Cancellation ignores value/length. '*' rejects user
 * cancellation and retains the prompt; accepted empty input remains legal.
 * No write/output occurs before successful formatting and destination storage.
 * Errors clear the pending operation, leaving both stores unchanged. */
void st_rip3_user_variables_respond(struct st_rip3_user_variables *variables,
	const uint8_t *value, size_t length, bool cancelled);
/* Forced cancellation for shutdown/EOF: discard pending/result state without
 * a protocol replacement or store write, even for required prompts. */
void st_rip3_user_variables_abort(struct st_rip3_user_variables *variables);
enum st_rip3_user_variable_state st_rip3_user_variables_state(
	const struct st_rip3_user_variables *variables);
/* Borrowed views valid until the next mutating call. A prompt view exposes the
 * copied reference and initial logical value; NULL outside PROMPT. A result
 * view is NULL outside COMPLETE. Successful suppression is a result of length
 * zero, successful empty emission is literal NULL, cancellation is NONE only
 * without either transparent flag. Neither sentinel is ever stored. */
const struct st_rip3_variable_reference *st_rip3_user_variables_prompt(
	const struct st_rip3_user_variables *variables);
const struct st_rip3_variable_value *st_rip3_user_variables_initial(
	const struct st_rip3_user_variables *variables);
const struct st_rip3_variable_value *st_rip3_user_variables_result(
	const struct st_rip3_user_variables *variables);

#endif
