/* Context-owned logical outline snapshot, independent of backend glyph caches. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_OUTLINE_CACHE_H_
#define ST_RIP3_OUTLINE_CACHE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_outline_cache;
struct st_rip3_outline_font;

struct st_rip3_outline_width {
	int64_t advance; /* Wide cached X-width, including every byte's final gap. */
	int16_t cursor;  /* Only this final handoff narrows to a signed WORD. */
};

/* One interpreter/drawing-context thread owns this object across its ports and
 * styles. A new/reset object is invalid and measures zero. The owner resets it
 * on a full drawing reset and destroys it at shutdown. Individual STYLE/PORT
 * changes and backend glyph-bitmap disposal do not reset it. */
int st_rip3_outline_cache_create(struct st_rip3_outline_cache **result);
void st_rip3_outline_cache_destroy(struct st_rip3_outline_cache *cache);
void st_rip3_outline_cache_reset(struct st_rip3_outline_cache *cache);
bool st_rip3_outline_cache_valid(const struct st_rip3_outline_cache *cache);
/* Prepare from a successfully realized font, copying the complete logical map,
 * X DPI, spacing, bbox flag, realized horizontal bbox span and unrotated
 * global geometric height. No font/file or
 * bitmap pointers survive. Commit only after every field/code succeeds; errors
 * preserve the prior snapshot. Selection alone never calls this service.
 * The text owner prepares after its nonempty/viewport gates, before cursor
 * measurement or raster presentation. A later ink failure retains the commit.
 * Internal metric-service statuses must remain inside the parser boundary. */
int st_rip3_outline_cache_prepare(struct st_rip3_outline_cache *cache,
	const struct st_rip3_outline_font *font, unsigned spacing);
/* Bounded C-string measurement: stop at the first NUL or length. All codes are
 * unsigned, including 0x80..0xff. No 255-byte limit or intermediate WORD wrap.
 * NULL cache represents a cold/invalid snapshot; NULL text is valid only for
 * zero length. No allocation. Overflow/errors preserve result and snapshot.
 * This is a service below void parser entry points, not a session error API. */
int st_rip3_outline_cache_measure(struct st_rip3_outline_width *result,
	const struct st_rip3_outline_cache *cache, const uint8_t *text, size_t length);

/* Font-global unrotated height from the same atomic snapshot; cold is zero. */
uint64_t st_rip3_outline_cache_height(const struct st_rip3_outline_cache *cache);

#endif
