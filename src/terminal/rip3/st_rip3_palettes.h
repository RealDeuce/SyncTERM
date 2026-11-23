/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_PALETTES_H_
#define ST_RIP3_PALETTES_H_

#include "st_rip3_surface.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_palettes;
struct st_rip3_backup;

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
/* First use selects and defines the slot even when protection prevents
 * restoring its defaults. In that case its RGB survives and the status
 * reports the rejected reset. */
int st_rip3_palettes_switch(struct st_rip3_palettes *palettes, unsigned number);
int st_rip3_palettes_protect(struct st_rip3_palettes *palettes,
	unsigned number, bool protect);
/* Wire switch flags can protect a currently selected undefined slot. */
int st_rip3_palettes_protect_current(struct st_rip3_palettes *palettes, bool protect);
/* Copies a defined entry to caller-owned storage; undefined reads fail without
 * changing result. No mutable palette/table memory is exposed to callers. */
int st_rip3_palettes_get(const struct st_rip3_palettes *palettes,
	unsigned number, struct st_rip3_palette *result);
/* The selected slot's RGB values remain usable after reset clears its in-use
 * bit. This view neither defines nor reselects it; numbered get still requires
 * a defined slot for PALENTRY and live-entry copy semantics. */
int st_rip3_palettes_current_palette(const struct st_rip3_palettes *palettes,
	struct st_rip3_palette *result);
/* Screen RESTORE overwrites only current RGB, including protected/undefined
 * slots. Retain current selection and all definition/protection flags. */
int st_rip3_palettes_restore_screen(struct st_rip3_palettes *,const struct st_rip3_palette *);
/* Specific/current reset rejects protection, restores RGB defaults and clears
 * nonzero definition without changing selection. Table/soft reset skips
 * protected slots; hard reset also removes protection and selects zero.
 * These operations do not clear the separately owned backup area. */
int st_rip3_palettes_reset_entry(struct st_rip3_palettes *palettes, unsigned number);
int st_rip3_palettes_reset(struct st_rip3_palettes *palettes, bool hard);
/* Validate the defined source and every destination before any mutation.
 * Protected sources are readable; protected/self/out-of-range destinations
 * reject the whole operation. Copy definition/protection, clearing protection
 * at destination zero. Selection is retained; duplicate destinations are legal. */
int st_rip3_palettes_copy_entries(struct st_rip3_palettes *palettes, unsigned source,
	const unsigned *destinations, size_t count);
/* Edit the current palette. Protected entries reject all changes. Underlying
 * setters validate the whole input span before writing any RGB values. */
int st_rip3_palettes_desktop(struct st_rip3_palettes *palettes, size_t start,
	const unsigned *master_colors, size_t count);
int st_rip3_palettes_rgb(struct st_rip3_palettes *palettes, size_t start,
	unsigned bits, const uint32_t *colors, size_t count);
int st_rip3_palettes_defaults(struct st_rip3_palettes *palettes);
int st_rip3_palettes_color(const struct st_rip3_palettes *palettes,
	bool direct, uint32_t color, uint8_t *result);
/* Packed N-bit RGB components (blue low, then green/red), normalized to
 * eight-bit channels with nearest rounding before current-palette matching.
 * Bits zero means a palette index; valid direct precision is 1..8. The wire
 * code is at most 24 bits; unused bits above the three components are ignored. */
int st_rip3_palettes_color_bits(const struct st_rip3_palettes *palettes,
	unsigned bits, uint32_t color, uint8_t *result);
/* Complete owned copy for the palette backup area: current slot, in-use and
 * protection bits, and all RGB tables. Restoring explicitly replaces the
 * whole live table, including protected entries as required by the backup
 * contract. This is not an intra-table entry copy. Result remains unchanged
 * on clone failure. Destroy snapshots using palettes_destroy. */
int st_rip3_palettes_clone(struct st_rip3_palettes **result,
	const struct st_rip3_palettes *palettes);
int st_rip3_palettes_restore(struct st_rip3_palettes *palettes,
	const struct st_rip3_palettes *snapshot);

/* A palette-table backup area using complete owned snapshots. Its save/restore
 * operations accept live st_rip3_palettes objects. Destroy with backup_destroy. */
/* Header table deletion skips zero, the retained slot and protected entries. */
void st_rip3_palettes_header_prune(struct st_rip3_palettes *, unsigned keep);
int st_rip3_palettes_backup_create(struct st_rip3_backup **result);

#endif
