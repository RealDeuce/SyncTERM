/* Incremental terminal-emulation parser and semantic console renderer. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_CTERM_H_
#define ST_CTERM_H_

#include "st_audio_runtime.h"
#include "st_codepage.h"
#include "st_console.h"
#include "st_font_catalog.h"
#include "st_hyperlink.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct cterminal st_cterm_t;

enum st_cterm_emulation {
	ST_CTERM_ANSI_BBS,
	ST_CTERM_PETSCII,
	ST_CTERM_ATASCII,
	ST_CTERM_PRESTEL,
	ST_CTERM_BBC,
	ST_CTERM_ATARI_ST_VT52,
};

enum st_cterm_encoding {
	ST_CTERM_ENCODING_UTF8,
	ST_CTERM_ENCODING_CODEPAGE,
};

enum st_cterm_petscii_machine {
	ST_CTERM_PETSCII_C64,
	ST_CTERM_PETSCII_C128_40,
	ST_CTERM_PETSCII_C128_80,
};

enum st_cterm_output_origin {
	ST_CTERM_OUTPUT_RESPONSE,
	ST_CTERM_OUTPUT_USER,
};

typedef void (*st_cterm_output_callback_t)(void *context,
	enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size);
typedef void (*st_cterm_apc_callback_t)(void *context, const char *text,
	size_t size);
typedef void (*st_cterm_size_callback_t)(void *context, uint32_t columns,
	uint32_t rows, uint32_t pixel_width, uint32_t pixel_height);

struct st_cterm_config {
	/* All pointers are borrowed and must outlive the terminal. */
	st_console_t *console;
	st_font_catalog_t *fonts;
	st_audio_runtime_t *audio;
	st_hyperlink_registry_t *hyperlinks;
	enum st_cterm_emulation emulation;
	enum st_cterm_encoding encoding;
	enum st_codepage codepage;
	/* Optional 16-entry attribute-to-canonical-palette mapping. */
	const uint16_t *palette_mapping;
	/* Used only by PETSCII to select C64/C128 colour and font semantics. */
	enum st_cterm_petscii_machine petscii_machine;
	uint8_t font_selector;
	/* Zero uses every console row; otherwise reserves the remaining rows. */
	uint32_t terminal_rows;
	size_t scrollback_lines;
	bool quiet;
	st_cterm_output_callback_t output;
	void *output_context;
	st_cterm_apc_callback_t apc;
	void *apc_context;
	st_cterm_size_callback_t size_changed;
	void *size_context;
};

/*
 * Creation clears the selected terminal region. UTF-8 is accepted only by
 * ANSI-BBS and Atari-ST VT52; byte-table emulations require their codepage.
 * Result is unchanged on failure.
 */
int st_cterm_create(st_cterm_t **result, const struct st_cterm_config *config);
void st_cterm_destroy(st_cterm_t **terminal);

/*
 * Consumes wire bytes incrementally. UTF-8 decoder and grapheme state persist
 * across calls. C0 controls flush an incomplete sequence as one replacement
 * before the parser applies the control. consumed is always written.
 */
int st_cterm_write(st_cterm_t *terminal, const void *bytes, size_t size,
	size_t *consumed);
/* Disconnect/reset boundary: replacement for a truncated UTF-8 sequence. */
int st_cterm_finish(st_cterm_t *terminal);
int st_cterm_reset_terminal(st_cterm_t *terminal);

enum st_cterm_encoding st_cterm_encoding(const st_cterm_t *terminal);
enum st_codepage st_cterm_codepage(const st_cterm_t *terminal);
int st_cterm_set_encoding(st_cterm_t *terminal,
	enum st_cterm_encoding encoding, enum st_codepage codepage);

/*
 * Allocates a chronological snapshot containing staged scrollback followed by
 * the visible terminal rows. The caller owns *cells and frees it with free().
 * Output pointers are unchanged on failure.
 */
int st_cterm_copy_history(st_cterm_t *terminal,
	struct st_console_cell **cells, uint32_t *columns, size_t *rows);

/* Encoded key bytes are delivered with ST_CTERM_OUTPUT_USER origin. */
bool st_cterm_handle_key(st_cterm_t *terminal, uint16_t key_code);
bool st_cterm_handle_doorway_key(st_cterm_t *terminal, uint8_t scan_code);
bool st_cterm_handle_physical_keys(st_cterm_t *terminal,
	const struct st_console_event *events, size_t count);
bool st_cterm_handle_mouse(st_cterm_t *terminal,
	const struct st_console_event *event);
bool st_cterm_handle_focus(st_cterm_t *terminal, bool focused);

#endif
