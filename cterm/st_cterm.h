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
#include <stdio.h>

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

enum st_cterm_rip_version {
	ST_CTERM_RIP_OFF = 0,
	ST_CTERM_RIP_1 = 1,
	ST_CTERM_RIP_3 = 2,
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
/* Called synchronously for RIP_DEFINE. Return zero and a NULL result to
 * cancel, or a malloc-owned string to accept. A nonzero return is an error. */
typedef int (*st_cterm_rip_prompt_callback_t)(void *context,
	const char *question, const char *initial, size_t field_width,
	bool reject_blank, char **result);

#define ST_CTERM_INPUT_REPLACEMENT_CAPACITY 256
/* Decoded Unicode scalar from received terminal text, including controls.
 * A replacement is semantic UTF-8 and bypasses the wire/RIP parser and this
 * filter.  Returning consumed with an empty replacement drops the scalar. */
typedef int (*st_cterm_input_filter_t)(void *context, uint32_t scalar,
	bool *consumed, char *replacement, size_t capacity,
	size_t *replacement_size);
typedef int (*st_cterm_capture_t)(void *context, const char *utf8,
	size_t size);

struct st_cterm_config {
	/* Pointers are borrowed through destruction, except palette arrays which
	 * are copied during creation. */
	st_console_t *console;
	st_font_catalog_t *fonts;
	st_audio_runtime_t *audio;
	st_hyperlink_registry_t *hyperlinks;
	enum st_cterm_emulation emulation;
	enum st_cterm_encoding encoding;
	/* RIP requires ANSI-BBS and at least a 640x350 pixel console. */
	enum st_cterm_rip_version rip_version;
	/* Optional per-BBS RIP asset directory; borrowed for terminal lifetime. */
	const char *rip_directory;
	/* Enables built-in SyncTERM APC commands independently of RIP. Both
	 * strings are borrowed through destruction. NULL directory retains
	 * callback-only handling. Version is NUL-terminated and control-free;
	 * NULL version suppresses the version reply. */
	const char *apc_directory;
	const char *application_version;
	enum st_codepage codepage;
	/* Optional 16-entry attribute-to-canonical-palette mapping. */
	const uint16_t *palette_mapping;
	/* Used only by PETSCII to select C64/C128 colour and font semantics. */
	enum st_cterm_petscii_machine petscii_machine;
	uint8_t font_selector;
	/* Zero uses every available row; otherwise reserves the remainder. */
	uint32_t terminal_rows;
	/* Initially reserve one available row for the DEC indicator status line. */
	bool status_line;
	size_t scrollback_lines;
	bool quiet;
	/* Treat a received LF as CR+LF; false retains the current column. */
	bool expand_lf;
	bool force_lcf;
	bool yellow_is_yellow;
	/* Optional 16-entry native-mode RGB override, copied at creation. */
	const uint32_t *palette_override;
	st_cterm_output_callback_t output;
	void *output_context;
	st_cterm_apc_callback_t apc;
	void *apc_context;
	st_cterm_size_callback_t size_changed;
	void *size_context;
	/* Without a callback, interactive RIP_DEFINE leaves the variable unchanged. */
	st_cterm_rip_prompt_callback_t rip_prompt;
	void *rip_prompt_context;
	st_cterm_input_filter_t input_filter;
	bool (*input_filter_active)(void *context);
	void *input_filter_context;
	/* Effective decoded input after semantic hooks, before text parsing. */
	st_cterm_capture_t capture;
	bool (*capture_active)(void *context);
	void *capture_context;
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
/* Direct semantic UTF-8 input, bypassing RIP and wire-byte filters and
 * independent of the configured wire encoding. */
int st_cterm_write_utf8(st_cterm_t *terminal, const void *bytes, size_t size,
	size_t *consumed);
/* Disconnect/reset boundary: replacement for a truncated UTF-8 sequence. */
int st_cterm_finish(st_cterm_t *terminal);
/* Nonblocking asynchronous APC notification polling. Call on the same
 * serialized owner as write(), even when no input arrives. Responses use
 * the synchronous output callback. NULL is IO_ERROR_INVALID_ARGUMENT. */
int st_cterm_poll(st_cterm_t *terminal);
/* After disconnect only: expose a terminal-style blank bottom row for local
 * chrome, preserving host rows through the normal scrollback path. No size
 * notification is sent to the closed connection. */
int st_cterm_prepare_disconnect(st_cterm_t *terminal);
int st_cterm_reset_terminal(st_cterm_t *terminal);

/* Resizes the owned console geometry and preserves visible and saved cells. */
int st_cterm_resize(st_cterm_t *terminal, uint32_t columns, uint32_t rows);
int st_cterm_size(const st_cterm_t *terminal, uint32_t *columns,
	uint32_t *rows);
int st_cterm_origin(const st_cterm_t *terminal, uint32_t *x, uint32_t *y);

/* Read-only parser state for connected Wren scripts. Values are a snapshot;
 * callers never receive pointers into the terminal. */
struct st_cterm_state {
	int x;
	int y;
	int top_margin;
	int bottom_margin;
	int left_margin;
	int right_margin;
	unsigned int attr;
	uint32_t fg_color;
	uint32_t bg_color;
	bool has_palette_override;
	uint32_t palette_override[16];
	int font_slot;
	int alt_fonts[4];
	int scrollback_lines;
	int scrollback_width;
	int scrollback_pos;
	int scrollback_start;
	enum st_cterm_emulation emulation;
	bool started;
	bool skypix;
	struct {
		bool auto_wrap;
		bool origin_mode;
		bool sx_scroll;
		bool dec_lrmm;
		bool bracket_paste;
		bool dec_bkm;
		bool prestel_mosaic;
		bool prestel_double_height;
		bool prestel_conceal;
		bool prestel_separated;
		bool prestel_hold;
		bool alternate_keypad;
	} ext_attr;
	struct {
		bool set;
		bool enabled;
		bool forced;
	} last_column_flag;
};
int st_cterm_state(const st_cterm_t *terminal, struct st_cterm_state *state);
/* Selects an already registered font for ordinary (non-bold/non-blink) cells.
 * The change affects future output; retained cells keep their resolved glyphs. */
int st_cterm_set_alt_font(st_cterm_t *terminal, uint8_t selector);
/* Register a user-approved 256-glyph font for this terminal's console. */
int st_cterm_register_font(st_cterm_t *terminal, uint8_t selector,
	const char *path, uint16_t *identifier);
/* Writes the visible terminal rectangle as BinaryText; optional SAUCE uses
 * the supplied UTF-8 profile name and user as bounded metadata. */
int st_cterm_save_binary(st_cterm_t *terminal, FILE *stream,
	bool with_sauce, const char *title, const char *author,
	int *native_error);

enum st_cterm_encoding st_cterm_encoding(const st_cterm_t *terminal);
enum st_codepage st_cterm_codepage(const st_cterm_t *terminal);
/* Live state consumed by the connected-session status script. */
int st_cterm_status_display(const st_cterm_t *terminal);
bool st_cterm_doorway_mode(const st_cterm_t *terminal);
void st_cterm_set_doorway_mode(st_cterm_t *terminal, bool enabled);
int st_cterm_music_mode(const st_cterm_t *terminal);
void st_cterm_set_music_mode(st_cterm_t *terminal, int mode);
bool st_cterm_atascii_inverse(const st_cterm_t *terminal);
int st_cterm_mouse_mode(const st_cterm_t *terminal);
bool st_cterm_mouse_sgr_pixels(const st_cterm_t *terminal);
bool st_cterm_bracketed_paste(const st_cterm_t *terminal);
int st_cterm_set_encoding(st_cterm_t *terminal,
	enum st_cterm_encoding encoding, enum st_codepage codepage);

/*
 * Allocates a chronological snapshot containing staged scrollback followed by
 * the visible terminal rows. The caller owns *cells and frees it with free().
 * Output pointers are unchanged on failure.
 */
int st_cterm_copy_history(st_cterm_t *terminal,
	struct st_console_cell **cells, uint32_t *columns, size_t *rows);

/* Logical, oldest-first access to the retained scrollback ring only. */
int st_cterm_scrollback_size(const st_cterm_t *terminal,
	uint32_t *columns, size_t *rows);
int st_cterm_copy_scrollback_rows(const st_cterm_t *terminal,
	struct st_console_cell *destination, uint32_t destination_columns,
	size_t destination_rows, size_t source_row, size_t row_count,
	size_t destination_row);

/* Normalized console keys, including event-time modifiers. Encoded bytes
 * have ST_CTERM_OUTPUT_USER origin; true also includes local-only actions. */
bool st_cterm_handle_key(st_cterm_t *terminal, uint16_t key_code,
	uint32_t modifiers);
/* Unicode keyboard text: RIP hotkeys first, then codepage and emulation
 * keyboard mappings. False leaves semantic transmission (UTF-8 text or an
 * unmappable character) to the session. Not for paste or scripted sends. */
bool st_cterm_handle_text_key(st_cterm_t *terminal, uint32_t codepoint);
/* RIP hotkeys may consume printable text before it is sent to the wire. */
bool st_cterm_handle_rip_text(st_cterm_t *terminal, uint32_t codepoint);
bool st_cterm_handle_doorway_key(st_cterm_t *terminal, uint8_t scan_code);
bool st_cterm_handle_physical_keys(st_cterm_t *terminal,
	const struct st_console_event *events, size_t count);
bool st_cterm_handle_mouse(st_cterm_t *terminal,
	const struct st_console_event *event);
/* A local selection consumed a button event before CTerm could see it. */
void st_cterm_sync_mouse_buttons(st_cterm_t *terminal, uint32_t buttons);
bool st_cterm_handle_focus(st_cterm_t *terminal, bool focused);

#endif
