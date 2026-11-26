/* MicroANSI file views, independent of the frozen 1.54 bitmap fonts. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_MAF_H
#define ST_RIP3_MAF_H

#include <stddef.h>
#include <stdint.h>

enum st_rip3_maf_mode {
	ST_RIP3_MAF_80X43,
	ST_RIP3_MAF_91X43,
	ST_RIP3_MAF_80X25,
	ST_RIP3_MAF_91X25,
	ST_RIP3_MAF_40X25,
};

struct st_rip3_maf {
	const uint8_t *data;
	size_t size;
	size_t directory;
	size_t resolutions;
};

struct st_rip3_maf_font {
	unsigned width, height;
	size_t row_bytes, glyph_size;
	const uint8_t *glyphs;
};

/* Validates the entire directory and all font extents. Views borrow immutable
 * bytes; the caller keeps them alive until every derived view is retired.
 * Failed operations leave result unchanged. No serialized structs are cast. */
int st_rip3_maf_open(struct st_rip3_maf *result, const void *data, size_t size);
int st_rip3_maf_find(struct st_rip3_maf_font *result,
	const struct st_rip3_maf *file, unsigned width, unsigned height,
	enum st_rip3_maf_mode mode);
/* Borrow the bundled font actually used by a DEVICE raster. Unmatched
 * profiles use the existing 640x480 fallback, independently of WORLD. Both
 * extended-window fitting and the renderer consume these same dimensions. */
int st_rip3_maf_builtin_font(struct st_rip3_maf_font *result,
	unsigned width, unsigned height, enum st_rip3_maf_mode mode);
/* The native strike for a matching regular or small bank. NOT_FOUND lets
 * ordinary windows compute cell size from the nominal grid. */
int st_rip3_maf_device_font(struct st_rip3_maf_font *result,
	unsigned width, unsigned height, enum st_rip3_maf_mode mode);
/* CP437 character zero is blank and has no stored record (returns NULL).
 * Others have glyph_size bytes: row-major, MSB first, row_bytes per row. */
const uint8_t *st_rip3_maf_glyph(const struct st_rip3_maf_font *font,
	uint8_t character);

#endif
