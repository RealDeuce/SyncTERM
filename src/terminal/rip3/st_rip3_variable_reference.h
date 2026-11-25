/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_VARIABLE_REFERENCE_H_
#define ST_RIP3_VARIABLE_REFERENCE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum st_rip3_variable_directive {
	ST_RIP3_VARIABLE_REQUIRED = 1u << 0,
	ST_RIP3_VARIABLE_PERSISTENT = 1u << 1,
	ST_RIP3_VARIABLE_SESSION = 1u << 2,
	ST_RIP3_VARIABLE_MASKED = 1u << 3,
	ST_RIP3_VARIABLE_SET = 1u << 4,
	ST_RIP3_VARIABLE_GET = 1u << 5,
};

enum st_rip3_variable_mode {
	ST_RIP3_VARIABLE_ANY,
	ST_RIP3_VARIABLE_ALPHA,
	ST_RIP3_VARIABLE_NUMBER,
	ST_RIP3_VARIABLE_ALPHANUM,
};

enum st_rip3_variable_conversion {
	ST_RIP3_VARIABLE_UNCONVERTED,
	ST_RIP3_VARIABLE_TONAME,
	ST_RIP3_VARIABLE_TOUPPER,
	ST_RIP3_VARIABLE_TOLOWER,
};

enum { ST_RIP3_PROMPT_TEXT_LIMIT = 100 };

struct st_rip3_variable_prompt_text {
	size_t length;
	/* Inline references retain up to 100 characters, encoded as UTF-8
	 * when the caller has decoded the wire. Also holds RIP_DEFINE text. */
	uint8_t bytes[ST_RIP3_PROMPT_TEXT_LIMIT * 4 + 1];
};

struct st_rip3_variable_reference {
	char name[21];
	unsigned directives;
	enum st_rip3_variable_mode mode;
	enum st_rip3_variable_conversion conversion;
	bool has_position, has_x, has_y, has_width, has_question, has_default;
	int32_t x, y;
	uint32_t width;
	struct st_rip3_variable_prompt_text question, default_value;
};

/* Decoded representation of ONE complete $...$ user-variable reference after
 * the owning RIP parser resolves macro expansion and built-in/template dispatch.
 * The internal decoder does not
 * expand text, prompt, access tables or select lifetime/transparent-mode policy.
 * ASCII names/options fold independently of locale. Explicit question/default
 * presence survives even when empty; each truncates to 100 characters.
 * Omitted coordinates mean centered; an entirely omitted position is distinct.
 * Unsigned decimal coordinates are limited to 65535 and widths fit uint32.
 *
 * Prefix '=' selects the session-storage bit; post-name '=default' remains a
 * separate field. Repetitions accumulate idempotently; '%' is invalid. Both
 * persistent '+' and session '=' bits are retained for the user-variable processor, where
 * persistent storage wins under the adopted PR #89 policy. The decoder performs
 * no storage or prompt action. No whitespace or undocumented
 * format aliases are accepted. Malformed syntax fails atomically; stream
 * framing, escaping and error recovery belong to the owning RIP parser.
 * The status-returning decoder is private to that parser and its unit tests. */

#endif
