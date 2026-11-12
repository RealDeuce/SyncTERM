/* Incremental byte grammar shared by the IGS family of graphics protocols. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_GRAPHICS_PARSER_H_
#define ST_GRAPHICS_PARSER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum st_graphics_argument_kind {
	ST_GRAPHICS_END,
	ST_GRAPHICS_NUMBER,
	ST_GRAPHICS_TEXT,
	ST_GRAPHICS_BYTES,
};

enum st_graphics_axis {
	ST_GRAPHICS_UNSCALED,
	ST_GRAPHICS_X,
	ST_GRAPHICS_Y,
};

enum st_graphics_operand_source {
	ST_GRAPHICS_CONSTANT,
	ST_GRAPHICS_LOOP_X,
	ST_GRAPHICS_LOOP_Y,
	ST_GRAPHICS_RANDOM_R,
	ST_GRAPHICS_RANDOM_R2,
	ST_GRAPHICS_ARRAY,
};

enum st_graphics_operand_operation {
	ST_GRAPHICS_VALUE,
	ST_GRAPHICS_STEP_PLUS,
	ST_GRAPHICS_STEP_MINUS,
	ST_GRAPHICS_MINUS_STEP,
};

struct st_graphics_operand {
	enum st_graphics_operand_source source;
	enum st_graphics_operand_operation operation;
	unsigned value;
};

struct st_graphics_parameter {
	enum st_graphics_argument_kind kind;
	enum st_graphics_axis axis;
	/* TEXT: maximum stored bytes; BYTES: exact acquired bytes. */
	size_t size;
	uint8_t terminator;
	bool skip_crlf;
	/* IGS's @ alone denotes a literal @ rather than an empty string. */
	bool empty_is_terminator;
};

struct st_graphics_argument {
	/* A supplied NUMBER has already been evaluated by the source owner;
	 * its operand is unused and must not be scaled/evaluated a second time. */
	bool has_value;
	int value;
	/* TEXT explicitly ended by its source owner before a delimiter/limit.
	 * The bytes contain only acquired payload, even for an empty field. */
	bool ended_early;
	enum st_graphics_argument_kind kind;
	enum st_graphics_axis axis;
	struct st_graphics_operand operand;
	const uint8_t *bytes;
	size_t size;
};

struct st_graphics_command_view {
	uint8_t code;
	bool chained;
	const struct st_graphics_argument *arguments;
	size_t count;
};

struct st_graphics_command {
	uint8_t code;
	const struct st_graphics_parameter *parameters;
	size_t count;
	/* After the fixed parameters, choose each additional field from prior
	 * arguments, or return ST_GRAPHICS_END. Must not execute effects. */
	int (*next)(void *context, const struct st_graphics_command_view *,
	    struct st_graphics_parameter *result);
};

struct st_graphics_dialect {
	/* Attention framing is separate from field acquisition and execution.
	 * Buffered dialects can call begin() after selecting a stored block. */
	const uint8_t *attention;
	size_t attention_size; /* 1..8 bytes */
	bool chain_indicator;
	uint8_t chain;
	bool number_skip_crlf;
	unsigned number_digits; /* 1..9 */
};

struct st_graphics_parser_config {
	const struct st_graphics_dialect *dialect;
	const struct st_graphics_command *commands;
	size_t command_count;
	size_t max_arguments;
	size_t max_payload;
};

struct st_graphics_parser_sink {
	void *context;
	int (*text)(void *, const uint8_t *, size_t);
	/* text/boundary/field/raw/next are synchronous; command/number/read_number may yield.
	 * Callbacks must not reenter the parser. A boundary commits pending text
	 * before acquiring a command. */
	int (*boundary)(void *);
	/* Views are borrowed until return, or until resume/cancel if accepted
	 * with IO_ERROR_IN_PROGRESS. Accepted commands are never called twice. */
	int (*command)(void *, const struct st_graphics_command_view *);
	void (*unknown)(void *, uint8_t code);
	/* Once per accepted field, before acquiring its first byte (also for
	 * zero-byte fields). Use for deadlines/handshakes; arity next stays pure.
	 * The prior-argument view and parameter are borrowed for this call. */
	int (*field)(void *, const struct st_graphics_command_view *,
	    const struct st_graphics_parameter *);
	/* Optional source for an already evaluated NUMBER, after field entry.
	 * Return zero with supplied=true to append value without consuming wire
	 * bytes. Zero/false assigns the entire field to the wire reader, with
	 * no further number calls for that field. IN_PROGRESS/false yields and
	 * retries this callback on feed(), without repeating field entry.
	 * Other errors cancel grammar; no value is appended on an error/yield.
	 * Views are borrowed; callbacks must not reenter the parser. */
	int (*number)(void *, const struct st_graphics_command_view *,
	    const struct st_graphics_parameter *, int *value, bool *supplied);
	/* Optional wire NUMBER reader, used only after number declines a field.
	 * The owner initializes its reader in field(); it owns numeric grammar,
	 * arithmetic and deadlines. Report accepted bytes even on error/yield.
	 * On success, complete=true appends an already evaluated value; otherwise
	 * consume input or wait for more bytes. IN_PROGRESS requires complete=false
	 * and preserves this field; retry on feed(), never resume(). The callback
	 * also receives empty input, allowing local deadlines/source waits to end
	 * a field without fabricating wire bytes. Zero progress with nonempty input
	 * and neither completion nor yield is a protocol error. Views are borrowed;
	 * callbacks must not reenter. Cancel/EOF never call this reader to complete
	 * a partial value. Keep this callback stable throughout a wire field. */
	int (*read_number)(void *, const struct st_graphics_command_view *,
	    const struct st_graphics_parameter *, const uint8_t *, size_t size,
	    size_t *accepted, int *value, bool *complete);
	/* Optional incremental BYTES consumer. Offset is relative to this field;
	 * data remains opaque and is also retained for the final argument view.
	 * Report accepted bytes even on error. A successful callback must make
	 * progress or set end=true to finish this field early. Unaccepted suffix
	 * bytes remain input. This callback is synchronous and must not reenter. */
	int (*raw)(void *, const struct st_graphics_command_view *,
	    const struct st_graphics_parameter *, size_t offset,
	    const uint8_t *, size_t size, size_t *accepted, bool *end);
	/* Optional owner handler for an unrecognized identifier, after unknown's
	 * notification. Receives an empty argument view and the same accepted
	 * command/yield/resume contract. No payload is guessed or consumed. */
	int (*unknown_command)(void *, const struct st_graphics_command_view *);
};

typedef struct st_graphics_parser st_graphics_parser_t;

/* An accepted command's return boundary, without its arguments or payload.
 * An owner may run a bounded nested source through the same parser, then
 * restore this boundary. This stores no grammar stack or protocol policy. */
struct st_graphics_continuation { bool chained; };

/* Config, dialect, descriptors and their arrays are borrowed until destroy.
 * All calls are serialized by the terminal owner. No thread or clock is owned
 * here; the caller implements reader deadlines with cancel(). */
int st_graphics_parser_create(st_graphics_parser_t **,
	const struct st_graphics_parser_config *);
void st_graphics_parser_destroy(st_graphics_parser_t *);
void st_graphics_parser_cancel(st_graphics_parser_t *);
/* Begin a command source whose outer attention/framing was already consumed.
 * Allowed only at an idle boundary. It still consumes any chain indicator. */
int st_graphics_parser_begin(st_graphics_parser_t *,
	const struct st_graphics_parser_sink *);
/* Zero means all input was accepted (a field may still be incomplete).
 * IN_PROGRESS leaves following bytes unread. Parsing/callback errors cancel
 * the pending grammar, retaining already emitted effects. consumed is set on
 * every call with a non-NULL consumed pointer. Invalid API arguments leave
 * parser state unchanged. */
int st_graphics_parser_feed(st_graphics_parser_t *,
	const struct st_graphics_parser_sink *, const uint8_t *, size_t,
	size_t *consumed);
/* Resume an accepted command; a waiting number source retries on feed(). */
int st_graphics_parser_resume(st_graphics_parser_t *);
/* Take requires an accepted SUSPENDED command and ends its borrowed view's
 * lifetime, leaving the parser idle. Restore requires idle with no held
 * attention prefix and returns to SUSPENDED; resume() then applies the saved
 * chain boundary. Invalid calls preserve grammar and the output token. */
int st_graphics_parser_take_continuation(st_graphics_parser_t *,
	struct st_graphics_continuation *);
int st_graphics_parser_restore_continuation(st_graphics_parser_t *,
	const struct st_graphics_continuation *);
/* Start a known command whose code/chain were acquired by the source owner.
 * At OUTSIDE (without a held attention prefix) or SUSPENDED only. Replacing
 * an accepted suspended command ends its borrowed view's lifetime. Calls
 * boundary once; command/field work starts on the next feed(), including
 * feed(NULL,0). An unknown code without an unknown_command handler returns NOT_SUPPORTED without changing the
 * grammar. Boundary errors also leave grammar/view ownership unchanged. */
int st_graphics_parser_start_command(st_graphics_parser_t *,
	const struct st_graphics_parser_sink *, uint8_t code, bool chained);
/* Explicit owner policy may end an active BYTES field with the bytes already
 * accepted, then run normal field/command completion. This does not fabricate
 * payload or change finish()'s rejection of incomplete commands. */
int st_graphics_parser_end_bytes(st_graphics_parser_t *,
	const struct st_graphics_parser_sink *);
/* End an active TEXT field with exactly the acquired payload and mark its
 * argument ended_early. No terminator (including empty_is_terminator) is
 * synthesized. Normal next/command work runs immediately and may yield.
 * Other states/fields are rejected without changing grammar or ownership. */
int st_graphics_parser_end_text(st_graphics_parser_t *,
	const struct st_graphics_parser_sink *);
/* Explicit source EOF flushes held ordinary attention-prefix text. A partial
 * command is rejected, never completed with fabricated bytes. Not a read flush. */
int st_graphics_parser_finish(st_graphics_parser_t *,
	const struct st_graphics_parser_sink *);
bool st_graphics_parser_in_command(const st_graphics_parser_t *);
/* Remaining header bytes: identifier and optional chain (0..2). */
unsigned st_graphics_parser_header_remaining(const st_graphics_parser_t *);
/* Source-owner header completion (for example, after a local pair deadline).
 * Preserve an acquired identifier; missing_code is used only before any
 * identifier arrived. The owner supplies the final chain choice. No wire
 * bytes or field values are synthesized. Invalid states leave grammar intact;
 * otherwise normal field/command work runs, including suspension/errors. */
int st_graphics_parser_complete_header(st_graphics_parser_t *,
	const struct st_graphics_parser_sink *, uint8_t missing_code, bool chained);

#endif
