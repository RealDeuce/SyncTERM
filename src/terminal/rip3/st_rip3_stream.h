/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_STREAM_H_
#define ST_RIP3_STREAM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_stream;

struct st_rip3_stream_numbers {
	unsigned base_math, coordinate_width;
	bool direct_rgb;
};

enum st_rip3_stream_diagnostic {
	ST_RIP3_STREAM_INVALID_INPUT,
	ST_RIP3_STREAM_RESOURCE_FAILURE,
	ST_RIP3_STREAM_INVALID_COMMAND,
	ST_RIP3_STREAM_UNSUPPORTED_COMMAND,
};

/* Borrowed callbacks/context, copied at creation. One session thread owns the
 * stream and its callbacks; callbacks must not reenter, reset or destroy it.
 * Byte spans are borrowed only for the invocation. Missing callbacks are safe.
 * CP437/UTF-8 conversion belongs to the client. */
struct st_rip3_stream_sink {
	void *context;
	void (*text)(void *context, const uint8_t *bytes, size_t length);
	/* Complete command ID and arguments, without introducer, delimiter or
	 * physical line endings. CR, LF and FF each terminate commands. A CR also
	 * consumes its optional LF across input calls; ordinary text is unchanged.
	 * For fixed numeric descriptors, only the opcode and collected fields
	 * are passed to the consumer. Numeric backslashes are discarded before
	 * decoding fields. String framing consumes quotes for SOH,
	 * STX and pipe, and passes other quotes on except continuation slashes.
	 * Comments are delivered too and MUST bypass variable expansion. */
	void (*command)(void *context, const uint8_t *bytes, size_t length);
	/* Optional current text-window cursor check, after preceding text has
	 * been delivered. True permits ! at column one after ANSI cursor motion.
	 * Physical line starts and SOH/STX need no client cursor information. */
	bool (*at_line_start)(void *context);
	void (*diagnostic)(void *context, enum st_rip3_stream_diagnostic kind);
	/* The host ANSI decoder can claim a bang before RIP checks for !|.
	 * Read the live framing flag ahead of each byte. Disabled input bypasses
	 * framing and preserves any unfinished RIP command for re-enable. */
	bool (*ansi_owns_bang)(void *context);
	const bool *framing_enabled;
	/* Snapshot live numeric widths at the opcode. Without a callback the
	 * initial environment is used (Base Math 36, XY=2, palette colors). */
	void (*number_settings)(void *context, struct st_rip3_stream_numbers *numbers);
};

/* Creation is a lifetime/resource service, not incoming-data processing. */
int st_rip3_stream_create(struct st_rip3_stream **result,
	const struct st_rip3_stream_sink *sink);
/* Destroy/reset discard unfinished input without executing it or invoking
 * callbacks. Reset starts a new session at line start, retaining buffer space.
 * No EOF-as-delimiter policy is inferred; the owner controls input lifetime. */
void st_rip3_stream_destroy(struct st_rip3_stream *stream);
void st_rip3_stream_reset(struct st_rip3_stream *stream);
/* Arbitrarily split incoming bytes. Errors stay inside the parser, with an
 * optional nonfatal diagnostic. A malformed ordinary numeric field or unknown
 * opcode discards the command and recovers at a pipe or terminating line
 * ending. After allocation failure, string quoting still determines where
 * that command ends. No fixed line limit.
 * NULL bytes with zero length is a no-op; invalid spans preserve parser state.
 * ANSI decoding, expansion, binary transfer modes and terminal routing are
 * external. ESC bytes remain in opaque command spans until their own
 * dispatcher can adjudicate them (#7). */
void st_rip3_stream_process(struct st_rip3_stream *stream,
	const uint8_t *bytes, size_t length);

/* Transfer handoff may take responsibility for the optional LF of a CRLF. */
bool st_rip3_stream_pending_lf(const struct st_rip3_stream *stream);
void st_rip3_stream_clear_pending_lf(struct st_rip3_stream *stream);

#endif
