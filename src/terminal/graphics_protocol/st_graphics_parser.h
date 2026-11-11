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
	/* text/boundary/field/raw/next are synchronous; only command may return IN_PROGRESS.
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
	/* Optional incremental BYTES consumer. Offset is relative to this field;
	 * data remains opaque and is also retained for the final argument view.
	 * Report accepted bytes even on error. A successful callback must make
	 * progress or set end=true to finish this field early. Unaccepted suffix
	 * bytes remain input. This callback is synchronous and must not reenter. */
	int (*raw)(void *, const struct st_graphics_command_view *,
	    const struct st_graphics_parameter *, size_t offset,
	    const uint8_t *, size_t size, size_t *accepted, bool *end);
};

typedef struct st_graphics_parser st_graphics_parser_t;

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
int st_graphics_parser_resume(st_graphics_parser_t *);
/* Explicit owner policy may end an active BYTES field with the bytes already
 * accepted, then run normal field/command completion. This does not fabricate
 * payload or change finish()'s rejection of incomplete commands. */
int st_graphics_parser_end_bytes(st_graphics_parser_t *,
	const struct st_graphics_parser_sink *);
/* Explicit source EOF flushes held ordinary attention-prefix text. A partial
 * command is rejected, never completed with fabricated bytes. Not a read flush. */
int st_graphics_parser_finish(st_graphics_parser_t *,
	const struct st_graphics_parser_sink *);
bool st_graphics_parser_in_command(const st_graphics_parser_t *);

#endif
