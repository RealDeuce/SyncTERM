/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_TEXT_COMMANDS_H_
#define ST_RIP3_TEXT_COMMANDS_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

struct st_rip3_classic_font;
struct st_rip3_outline_cache;
struct st_rip3_rff;

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
	/* Optional context-owned logical snapshot, shared across ports/styles.
	 * NULL means cold/invalid, whose cached width is zero. A successful outline
	 * preparation creates it on first use. The client destroys the resulting
	 * object at shutdown and resets it on full drawing reset; individual font/
	 * port/style selection retains it. */
	struct st_rip3_outline_cache *outline_cache;
	/* Optional immutable RFF view. Exact matching style names use this file;
	 * other names use the bundled catalogue, without substituting another face.
	 * The client owns file lookup/loading and the view's backing bytes. */
	const struct st_rip3_rff *outline_font;
};

/* Numeric prefix is decoded once before providers can change BASEMATH/COORDSIZE.
 * Parser-owned continuation; private statuses never leave the void session API. */
struct st_rip3_text_argument {bool positioned;uint64_t x,y;};
struct st_rip3_command_host;
int st_rip3_text_argument_prepare(const struct st_rip3_command_host *host,
	const uint8_t *command, size_t length, struct st_rip3_text_argument *argument, size_t *offset);
int st_rip3_text_commands_expanded(const struct st_rip3_command_host *host,
	const struct st_rip3_text_argument *argument, const uint8_t *text, size_t length, bool *operation);

#endif
