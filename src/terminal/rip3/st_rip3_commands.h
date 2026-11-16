/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_COMMANDS_H_
#define ST_RIP3_COMMANDS_H_

#include <stddef.h>
#include <stdint.h>

struct st_rip3_environments;
struct st_rip3_audio;
struct st_rip3_resource_paths;
struct st_rip3_palettes;
struct st_rip3_ports;
struct st_rip3_styles;

/* Borrowed session-owned bindings. Missing services are unsupported. One
 * session thread processes commands and owns these objects; its audio engine
 * may concurrently pull PCM under the audio adapter's synchronization. */
struct st_rip3_command_host {
	struct st_rip3_environments *environments;
	struct st_rip3_audio *audio;
	const struct st_rip3_resource_paths *resources;
	struct st_rip3_palettes *palettes;
	struct st_rip3_ports *ports;
	struct st_rip3_styles *styles;
};

enum st_rip3_command_diagnostic {
	ST_RIP3_COMMAND_INVALID,
	ST_RIP3_COMMAND_UNSUPPORTED,
	ST_RIP3_COMMAND_OPERATION_FAILED,
	ST_RIP3_COMMAND_RESOURCE_FAILURE,
};

struct st_rip3_command_sink {
	void *context;
	/* Optional nonfatal observation. No raw command or filename is exposed. */
	void (*diagnostic)(void *context, enum st_rip3_command_diagnostic kind);
};

/* Process one complete command span: command ID plus arguments, without !|,
 * separators or line terminators. The enclosing framer supplies the boundary
 * and resolved text (UTF-8 resource names, preserving control tokens). This
 * entry does not buffer partial input or perform macro expansion.
 *
 * Implements J, n, f, Q, a, c, k, numeric W, L, R, 1w, v, 2p, rectangle-fit
 * 2C, onscreen 2P, comments and terminal grouping no-ops.
 * Numeric commands use the current ENV record,
 * documented shortening/reserved omission and J/n invalid-value defaults.
 * Q/a edit the bound palette table's current entry, independently of ENV
 * reference activation; ENV supplies the radix only. All fields are validated
 * before changing the palette, whose own protection applies.
 * Port commands use the bound PORT table and current ENV world/width/radix,
 * without activating or updating ENV references. World geometry requires the
 * ENV and PORT device canvases to agree. Offscreen 2P and wholly external
 * port/viewport definitions remain unsupported pending #10. For 2C, all-zero
 * source/destination fields mean the full viewport; explicit rectangles clip
 * then fit. A nonzero destination origin with both endpoints zero remains
 * unsupported (#10). Undefined/inactive operands are successful no-ops.
 * Drawing commands use the bound STYLE record's explicit resolved pen and
 * write mode. c/k preserve raw colour values: current-radix palette indices
 * or four-digit UltraNum RGB (8 bits/channel), with last-field shortening.
 * L/R project world geometry without narrowing far endpoints before clipping;
 * direct RGB ink is matched through the bound current palette at drawing.
 * Inactive viewports skip drawing; NOT requires no colour lookup. Protected
 * STYLE records reject attribute edits but permit drawing, as do protected
 * PORT records. Bootstrap/ENV activation and wire pen thickness are separate
 * unresolved policies (#11/#24); W period termination remains unsupported (#7).
 * Full-width f accepts the documented space-separated inline comment;
 * other comment-boundary/framing forms remain unsupported under #7. Filenames
 * retain spaces and literal $OFF$. Nonzero reserved fields remain unsupported.
 *
 * Every rejected command is consumed here, with no failed state latched and
 * no status returned to the terminal/session caller. Later commands still
 * run. Internal service errors never become connection errors. Callbacks must
 * not reenter or mutate bindings during processing. This is not the active
 * terminal route or a complete RIP stream parser. */
void st_rip3_commands_process(const struct st_rip3_command_host *host,
	const struct st_rip3_command_sink *sink, const uint8_t *command, size_t length);

#endif
