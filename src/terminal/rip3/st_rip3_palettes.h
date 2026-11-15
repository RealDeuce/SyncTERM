/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_PALETTES_H_
#define ST_RIP3_PALETTES_H_

#include "st_rip3_surface.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_palettes;

struct st_rip3_palette_info {
	bool defined;
	bool protected;
};

/* 36 owned palette slots, each a complete 256-entry RGB lookup table. Initially
 * only slot zero is defined/current; selecting a new slot establishes the
 * standard palette, independently of the previously selected slot's contents.
 * Slot zero is always defined and cannot be protected. One session owns state. */
int st_rip3_palettes_create(struct st_rip3_palettes **result);
void st_rip3_palettes_destroy(struct st_rip3_palettes *palettes);
int st_rip3_palettes_current(const struct st_rip3_palettes *palettes, unsigned *result);
int st_rip3_palettes_info(const struct st_rip3_palettes *palettes,
	unsigned number, struct st_rip3_palette_info *result);
int st_rip3_palettes_switch(struct st_rip3_palettes *palettes, unsigned number);
int st_rip3_palettes_protect(struct st_rip3_palettes *palettes,
	unsigned number, bool protect);
/* Copies a defined entry to caller-owned storage; undefined reads fail without
 * changing result. No mutable palette/table memory is exposed to callers. */
int st_rip3_palettes_get(const struct st_rip3_palettes *palettes,
	unsigned number, struct st_rip3_palette *result);
/* Edit the current palette. Protected entries reject all changes. Underlying
 * setters validate the whole input span before writing any RGB values. */
int st_rip3_palettes_desktop(struct st_rip3_palettes *palettes, size_t start,
	const unsigned *master_colors, size_t count);
int st_rip3_palettes_rgb(struct st_rip3_palettes *palettes, size_t start,
	unsigned bits, const uint32_t *colors, size_t count);
int st_rip3_palettes_defaults(struct st_rip3_palettes *palettes);
int st_rip3_palettes_color(const struct st_rip3_palettes *palettes,
	bool direct, uint32_t color, uint8_t *result);
/* Complete owned copy for the palette backup area: current slot, in-use and
 * protection bits, and all RGB tables. Restoring explicitly replaces the
 * whole live table, including protected entries as required by the backup
 * contract. This is not an intra-table entry copy. Result remains unchanged
 * on clone failure. Destroy snapshots using palettes_destroy. */
int st_rip3_palettes_clone(struct st_rip3_palettes **result,
	const struct st_rip3_palettes *palettes);
int st_rip3_palettes_restore(struct st_rip3_palettes *palettes,
	const struct st_rip3_palettes *snapshot);

#endif
