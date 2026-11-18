/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_TEXT_COMMANDS_H_
#define ST_RIP3_TEXT_COMMANDS_H_

struct st_rip3_classic_font;

enum st_rip3_text_encoding {
	ST_RIP3_TEXT_CP437,
	ST_RIP3_TEXT_UTF8,
};

/* Session-owned transport encoding and optional immutable classic-font override.
 * The current PORT owns the context-absolute integer cursor; STYLE owns font
 * family/attributes. Matching overrides affect raster/metrics for that font
 * number only. Encoding conversion expands no macros or terminal controls. */
struct st_rip3_text_context {
	enum st_rip3_text_encoding encoding;
	const struct st_rip3_classic_font *font;
};

#endif
