/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_TEXT_COMMANDS_H_
#define ST_RIP3_TEXT_COMMANDS_H_

#include "st_rip3_geometry.h"
#include <stdbool.h>

struct st_rip3_classic_font;

enum st_rip3_text_encoding {
	ST_RIP3_TEXT_CP437,
	ST_RIP3_TEXT_UTF8,
};

/* Explicit live graphics-text binding, owned by the session. Cursor coordinates
 * are viewport-relative device pixels, retaining fractional logical advances.
 * Cursor/PORT ownership and whole-scene reset/backup orchestration remain
 * separate. Missing bindings are unsupported. The current STYLE owns the font
 * family; successful Y selects classic without changing this cursor.
 * Optional immutable font overrides that same numbered classic font; other
 * selected numbers use their original bundled font for the current canvas.
 * Encoding conversion does not expand macros or interpret terminal controls. */
struct st_rip3_text_context {
	struct st_rip3_text_position cursor;
	enum st_rip3_text_encoding encoding;
	const struct st_rip3_classic_font *font;
};

#endif
