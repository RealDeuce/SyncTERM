/* Borrowed CHR body views and bounded original two-byte stroke records. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BGI_CHR_H_
#define ST_BGI_CHR_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct st_bgi_chr {
	const uint8_t *body;
	size_t size;
	uint16_t count, strokes;
	uint8_t first;
};
struct st_bgi_chr_glyph {
	const uint8_t *body;
	size_t size, records;
	uint16_t offset;
	uint8_t width;
	bool present, finished;
};
enum st_bgi_chr_opcode { ST_BGI_CHR_END, ST_BGI_CHR_IGNORE, ST_BGI_CHR_MOVE, ST_BGI_CHR_DRAW };
struct st_bgi_chr_record { enum st_bgi_chr_opcode opcode; int x,y; };
/* Shared format decoding, independent of font version/layout/advances. */
struct st_bgi_chr_record st_bgi_chr_decode(uint8_t x, uint8_t y);
/* No allocation/copy. Views borrow their byte storage for their lifetime.
 * Tables/records are checked at each reached access, preserving decode
 * prefixes. Invalid calls leave output views/records unchanged. */
int st_bgi_chr_bind(struct st_bgi_chr *, const uint8_t *body, size_t size);
/* Separate accessors preserve metric-before-program error ordering.
 * The view may retain metadata copied earlier from a different body. */
int st_bgi_chr_width(const struct st_bgi_chr *, uint8_t code, uint8_t *width);
int st_bgi_chr_program(const struct st_bgi_chr *, uint8_t code, struct st_bgi_chr_glyph *);
/* Original modulo-BYTE code/first and low-count comparison; WORD table
 * addresses and stroke-base addition. A zero metric can still have strokes. */
int st_bgi_chr_glyph(const struct st_bgi_chr *, uint8_t code, struct st_bgi_chr_glyph *);
/* END is returned once as a record, then available=false. Its coordinates
 * do not replace the caller's last MOVE/DRAW point used for pen advance.
 * Escaped native extents or an endless WORD-address cycle fail boundedly. */
int st_bgi_chr_next(struct st_bgi_chr_glyph *, struct st_bgi_chr_record *, bool *available);
#endif
