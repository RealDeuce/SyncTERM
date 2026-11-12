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
#include "st_amiga_font.h"
#include "st_font_catalog.h"
#include "st_hyperlink.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct cterminal st_cterm_t;

/* Owner-thread streaming of validated received RIP scene bytes. begin/end
 * isolate file parsing from the live wire parser. end(false) discards an
 * unfinished command after cancellation or an invalid transfer. */
int st_cterm_rip_receive_begin(st_cterm_t *terminal);
int st_cterm_rip_receive_data(st_cterm_t *terminal, const uint8_t *bytes, size_t size);
int st_cterm_rip_receive_end(st_cterm_t *terminal, bool success);

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

enum st_cterm_graphics_mode {
	ST_CTERM_GRAPHICS_OFF = 0,
	ST_CTERM_GRAPHICS_RIP_1 = 1,
	ST_CTERM_GRAPHICS_RIP_3 = 2,
	ST_CTERM_GRAPHICS_SKYPIX_13 = 3,
	ST_CTERM_GRAPHICS_SKYPIX_31 = 4,
	/* IGS integration is under development; command coverage is documented
	 * in docs/IGS_CONFORMANCE.md. */
	ST_CTERM_GRAPHICS_IGS_221 = 5,
	/* BIG command coverage is tracked in docs/BIG_CONFORMANCE.md. */
	ST_CTERM_GRAPHICS_BIG_103 = 6,
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

struct st_cterm_rip_choice {
	const char *label;
	uint32_t hotkey; /* Unicode scalar, zero when no hotkey is specified. */
};
/* Synchronous, borrowed UTF-8 labels. Do not reenter the terminal. Set
 * selected to -1 for optional cancellation, or an index in [0,count).
 * Required lists ignore ordinary dismissal; close/error returns nonzero. */
typedef int (*st_cterm_rip_choice_callback_t)(void *context,
	const char *question, const struct st_cterm_rip_choice *choices,
	size_t count, bool required, int *selected);

struct st_cterm_rip_transfer_request {
	bool upload;
	unsigned protocol, file_type;
	const char *directory, *filename;
};
/* Runs synchronously on the terminal owner at the terminating CR. Accepting
 * must claim the connection before returning; borrowed strings cannot escape
 * this callback. Protocol I/O and UI run outside the RIP parser. */
typedef int (*st_cterm_rip_transfer_callback_t)(void *context,
	const struct st_cterm_rip_transfer_request *request);

#define ST_CTERM_INPUT_REPLACEMENT_CAPACITY 256
/* Decoded Unicode scalar from received terminal text, including controls.
 * Protocol payloads and sequence parameters belong to their byte parser and
 * bypass this text hook. Macro playback does not invoke receive hooks again.
 * A replacement is semantic UTF-8 and bypasses the wire/RIP parser and this
 * filter.  Returning consumed with an empty replacement drops the scalar. */
typedef int (*st_cterm_input_filter_t)(void *context, uint32_t scalar,
	bool *consumed, char *replacement, size_t capacity,
	size_t *replacement_size);
/* Text is decoded UTF-8 after semantic hooks. With protocol=true, bytes are
 * opaque parser input for text-only consumers to omit. This is not a wire
 * capture: observe original connection bytes before calling CTerm for that.
 * Playback is omitted. */
typedef int (*st_cterm_capture_t)(void *context, const char *bytes,
	size_t size, bool protocol);

enum st_cterm_log_level {
	ST_CTERM_LOG_ERROR = 3,
	ST_CTERM_LOG_WARNING = 4,
	ST_CTERM_LOG_INFO = 6,
	ST_CTERM_LOG_DEBUG = 7,
};
/* Synchronous diagnostics on the serialized CTerm owner. The message is
 * borrowed for the call; callbacks must not reenter the terminal. */
typedef void (*st_cterm_log_callback_t)(void *context,
	enum st_cterm_log_level level, const char *message);

/* Called at the second bang, with following wire bytes unread. Return 0 for
 * a completed cache hit, IO_ERROR_IN_PROGRESS for an owned transfer, or an error. */
typedef int (*st_cterm_skypix_transfer_callback_t)(void *context, unsigned mode,
	unsigned width, unsigned height, const uint8_t *filename, size_t size);

struct st_cterm_config {
	/* Pointers are borrowed through destruction, except palette arrays which
	 * are copied during creation. */
	st_console_t *console;
	st_font_catalog_t *fonts;
	st_audio_runtime_t *audio;
	st_hyperlink_registry_t *hyperlinks;
	enum st_cterm_emulation emulation;
	enum st_cterm_encoding encoding;
	/* Graphics require a bitmap console. RIP/SkyPix/BIG require ANSI-BBS; IGS
	 * requires Atari ST VT52 and native ST geometry. RIP needs at least
	 * 640x350 pixels; BIG defaults to 640x350 with 8x14 console cells; SkyPix
	 * uses the selected canvas and wire encoding. */
	enum st_cterm_graphics_mode graphics_mode;
	/* Optional per-BBS RIP asset directory; borrowed for terminal lifetime. */
	const char *rip_directory;
	const char *amiga_font_directory;
	/* Default strike is borrowed through destruction; NULL selects Pearl 8. */
	const st_amiga_font_t *amiga_font;
	/* Zero uses the terminal grid extent, excluding the status row. */
	uint32_t skypix_width, skypix_height;
	st_cterm_skypix_transfer_callback_t skypix_transfer;
	void *skypix_transfer_context;
	/* Enables built-in SyncTERM APC commands independently of RIP. Both
	 * strings are borrowed through destruction; version is NUL-terminated.
	 * NULL directory retains callback-only handling. CTSV maps UTF-8 alpha/beta
	 * to a/b and filters other bytes; NULL version suppresses the reply. */
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
	/* Optional session-owned emulated speed, in BPS (zero is unlimited).
	 * DECRQSS queries the effective speed; DECSCS requests a change, which
	 * the owner may ignore when throttling is unsupported. Without these
	 * callbacks, the parser retains its own DECSCS state. */
	uint32_t (*speed_query)(void *context);
	void (*speed_changed)(void *context, uint32_t bps);
	void *speed_context;
	/* Without a callback, interactive RIP_DEFINE leaves the variable unchanged. */
	st_cterm_rip_prompt_callback_t rip_prompt;
	void *rip_prompt_context;
	st_cterm_rip_choice_callback_t rip_choice;
	void *rip_choice_context;
	st_cterm_rip_transfer_callback_t rip_transfer;
	void *rip_transfer_context;
	st_cterm_input_filter_t input_filter;
	bool (*input_filter_active)(void *context);
	void *input_filter_context;
	/* Observe effective text and original protocol bytes without filtering. */
	st_cterm_capture_t capture;
	bool (*capture_active)(void *context);
	void *capture_context;
	/* Syslog-style maximum priority (0..7). No callback disables logging. */
	int log_level;
	st_cterm_log_callback_t diagnostic;
	void *diagnostic_context;
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
 * Codepage text is converted at the input boundary and shares UTF-8 text
 * presentation. Counts refer to original input bytes, not converted UTF-8.
 * RIP/IGS/BIG command rejections warn and continue with the unread stream;
 * they do not return command/parser errors to the session owner.
 */
int st_cterm_write(st_cterm_t *terminal, const void *bytes, size_t size,
	size_t *consumed);
/* Direct semantic UTF-8 input, bypassing RIP and wire-byte filters and
 * independent of the configured wire encoding. */
int st_cterm_write_utf8(st_cterm_t *terminal, const void *bytes, size_t size,
	size_t *consumed);
/* UTF-8 expanded by an outer protocol handler. Applies semantic input hooks
 * and capture while bypassing RIP and the connection's wire encoding. */
int st_cterm_write_utf8_input(st_cterm_t *terminal, const void *bytes,
	size_t size, size_t *consumed);
/* Queue legacy U8 mono 22050-Hz effects on the terminal's shared SFX stream.
 * An unavailable audio device leaves terminal output unaffected. */
bool st_cterm_play_fx_u8(st_cterm_t *terminal, const uint8_t *samples, size_t size);
/* Disconnect/reset boundary: replacement for a truncated UTF-8 sequence. */
int st_cterm_finish(st_cterm_t *terminal);
/* Nonblocking asynchronous APC notification polling. Call on the same
 * serialized owner as write(), even when no input arrives. Responses use
 * the synchronous output callback. NULL is IO_ERROR_INVALID_ARGUMENT. */
int st_cterm_poll(st_cterm_t *terminal);
/* Owner-thread timing gate. The caller retains bytes after a write
 * consumed count while paused; no input suffix is queued by this API. */
bool st_cterm_input_paused(st_cterm_t *terminal);
/* Load a completed native ILBM brush; NULL/zero cancels a pending request. */
int st_cterm_skypix_receive(st_cterm_t *terminal, const void *bytes, size_t size);
/* Owner-thread raw type-3 load. Borrows bytes during the call, copies only
 * the declared signed PCM, and retains the previous sample on failure.
 * Maximum declared PCM length is 4 MiB; trailing bytes are ignored.
 * Returns BUSY during playback; reset/destruction releases the owned copy. */
int st_cterm_skypix_load_sample(st_cterm_t *terminal, const void *bytes, size_t size);
/* After disconnect only: finish pending output and, without a status row,
 * scroll the whole terminal once to make room for a local notice. Dimensions
 * and status layout remain unchanged; capture completed history beforehand. */
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
	enum st_cterm_graphics_mode graphics_mode;
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
enum st_cterm_font_mask {
	ST_CTERM_FONT_NORMAL = 1,
	ST_CTERM_FONT_BRIGHT = 2,
	ST_CTERM_FONT_BLINK = 4,
	ST_CTERM_FONT_BRIGHT_BLINK = 8,
	ST_CTERM_FONT_ALL = 15,
};
/* Validate all inputs, finish pending output, then change exactly the masked
 * assignments. Retained cells keep their resolved fonts. Zero is invalid. */
int st_cterm_set_fonts(st_cterm_t *terminal, uint8_t selector, unsigned mask);
/* Copies the current registration, including remote font variants. */
bool st_cterm_font_info(const st_cterm_t *terminal, uint8_t selector,
	uint16_t *identifier, enum st_codepage *codepage);
/* Registers without selecting. Occupied selectors (including remote variants
 * unavailable at this geometry) return ALREADY_EXISTS before reading a file. */
int st_cterm_register_font(st_cterm_t *terminal, uint8_t selector,
	const char *path, enum st_codepage codepage, uint16_t *identifier,
	struct st_font_load_error *error);
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
/* Local keyboard preference; terminal reset does not change it. Default true. */
void st_cterm_set_atari_st_key_clicks(st_cterm_t *terminal, bool enabled);
bool st_cterm_atascii_inverse(const st_cterm_t *terminal);
/* Effective protocol: 0 off, 1 RIP, 2 SkyPix, 3 IGS, 4 BIG pointer, 9 X10, 1000/1002/1003 XTerm. */
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
/* Clone one chronological history/live row's optional RGB pixel strip.
 * A text-only row returns NULL; output is unchanged on failure. */
int st_cterm_copy_history_pixels(st_cterm_t *terminal, size_t row,
	struct st_pixel_buffer **result);

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
/* An explicitly entered byte in the terminal's current codepage. */
bool st_cterm_handle_codepage_key(st_cterm_t *terminal, uint8_t byte);
/* RIP hotkeys may consume printable text before it is sent to the wire. */
bool st_cterm_handle_rip_text(st_cterm_t *terminal, uint32_t codepoint);
bool st_cterm_handle_doorway_key(st_cterm_t *terminal, uint8_t scan_code);
bool st_cterm_handle_physical_keys(st_cterm_t *terminal,
	const struct st_console_event *events, size_t count);
bool st_cterm_handle_mouse(st_cterm_t *terminal,
	const struct st_console_event *event);
/* Observe native pointer state before local hooks/forwarding gates. */
void st_cterm_observe_mouse(st_cterm_t *, const struct st_console_event *);
/* Synchronize a locally handled or disabled button event without reporting. */
void st_cterm_sync_mouse_buttons(st_cterm_t *terminal, uint32_t buttons);
bool st_cterm_handle_focus(st_cterm_t *terminal, bool focused);

#endif
