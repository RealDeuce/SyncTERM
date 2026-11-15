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

struct st_rip3_variable_prompt_text {
	size_t length;
	uint8_t bytes[101]; /* First 100 original bytes plus a convenience NUL. */
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
 * presence survives even when empty; each truncates to 100 protocol bytes.
 * Omitted coordinates mean centered; an entirely omitted position is distinct.
 * Unsigned decimal coordinates fit int32, field widths fit uint32; these are
 * representation bounds, not inferred RIPtel numeric limits/defaults.
 *
 * Prefix '=' and repeated directives return NOT_SUPPORTED: their 3.x meaning
 * conflicts in the pinned reference (upstream #6). Both '+' and '%' may be
 * retained; no precedence is selected here. No whitespace or undocumented
 * format aliases are accepted. Malformed syntax fails atomically; stream
 * framing, escaping and error recovery belong to the owning RIP parser.
 * The status-returning decoder is private to that parser and its unit tests. */

#endif
