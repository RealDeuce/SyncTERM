/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_CHR_H_
#define ST_RIP3_CHR_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_chr {
	const uint8_t *body;
	size_t size, characters, strokes;
	unsigned first_character;
	unsigned scan_flag; /* Raw header byte; painting semantics are separate. */
	int capital, baseline, descender; /* Font units, positive y upward. */
	char name[5];
};

struct st_rip3_chr_glyph {
	const uint8_t *program;
	size_t program_size; /* Two-byte records, excluding terminating END. */
	unsigned advance;
	bool has_scan;
};

enum st_rip3_chr_operation {
	ST_RIP3_CHR_MOVE,
	ST_RIP3_CHR_DRAW,
	ST_RIP3_CHR_SCAN,
};

struct st_rip3_chr_record {
	enum st_rip3_chr_operation operation;
	int x, y; /* Absolute signed-seven-bit coordinates, y upward. */
};

/* Independent full-file validation, without legacy BYTE/WORD wrapping.
 * Validates every referenced program before publishing a borrowed immutable
 * view. Repeated/aliased glyph offsets and zero advances are supported.
 * Views require the original bytes to remain alive and unchanged. Results
 * and iterator cursors remain unchanged on failure. */
int st_rip3_chr_open(struct st_rip3_chr *result, const void *data, size_t size);
/* Original RIPtel assets for font IDs 1..10. These do not use the frozen
 * 1.54 font bank (notably, the RIPtel LITT/LCOM payloads are later versions). */
int st_rip3_chr_builtin(struct st_rip3_chr *result, unsigned font_number);
int st_rip3_chr_glyph(struct st_rip3_chr_glyph *result,
	const struct st_rip3_chr *font, uint8_t character);
/* NOT_FOUND marks program end. The iterator exposes raw SCAN records.
 * The classic renderer ignores their payload and continues at the same pen position. */
int st_rip3_chr_next(struct st_rip3_chr_record *result,
	const struct st_rip3_chr_glyph *glyph, size_t *cursor);

#endif
