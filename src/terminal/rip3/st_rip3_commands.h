/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_COMMANDS_H_
#define ST_RIP3_COMMANDS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_environments;
struct st_rip3_audio;
struct st_rip3_resource_paths;
struct st_rip3_palettes;
struct st_rip3_ports;
struct st_rip3_styles;
struct st_rip3_input;
struct st_rip3_query;
struct st_rip3_bitmap_destination;

/* Borrowed UI request, made on the session thread for file/decode failures.
 * name and area live only during show(). Explicit areas are viewport-relative
 * inclusive device coordinates; NULL asks the UI to choose a suitable box for
 * a verbatim load whose source dimensions are unavailable. This callback must
 * not reenter command processing or mutate its bindings. */
struct st_rip3_bitmap_error_sink {
	void *context;
	void (*show)(void *context, const char *name, unsigned port,
		const struct st_rip3_bitmap_destination *area);
};

/* Explicit resolved clipboard pointer. Selection/GET_IMAGE allocation, reset,
 * protection and backup ownership remain under #42. This binding supplies the
 * pointer only; PUT_IMAGE/WRITE_ICON neither select it nor edit its state. */
struct st_rip3_clipboard_selection {
	bool set;
	unsigned port; /* 1..35 when set; the pointed port may be reconfigured. */
};

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
	/* Explicit resolved scene setting. N ownership/switch/backup semantics
	 * are unresolved under #24; no default is inferred from a missing binding. */
	const bool *borders;
	/* Borrowed input gate. The owning source retains bytes during 3D delays. */
	struct st_rip3_input *input;
	/* Session-owned immediate-query evaluator, resumed by the input source's prepare
	 * callback before further bytes. Uses this source's same input gate. */
	struct st_rip3_query *query;
	const struct st_rip3_clipboard_selection *clipboard;
	const struct st_rip3_bitmap_error_sink *bitmap_errors;
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
 * Implements J, n, f, Q, a, c, k, numeric W, Y, E, L, R, B, K, C, G, o,
 * canonical full O/V and full/equal A, ordinary P/l/p paths,
 * s, S presets 00/01/03/06/08/09/0A, 1w, v, 2p, 2s,
 * rectangle-fit 2C, onscreen 2P, clipboard 1P/1W, 2W bitmap export,
 * ordinary/transparent 1b bitmap loads, 3D delays, 3e record edits,
 * comments, terminal grouping no-ops and resumable immediate 1ESC queries.
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
 * 2s accepts the complete three-digit port/flags form used by the corpus.
 * It selects the bound port and applies consistent source/destination protect
 * flags, skipping protection edits to port zero. A missing destination with
 * zero flags falls back to zero. Conflicting flags, flags on an undefined
 * destination and disputed longer reserved forms are declined before changes.
 * It does not activate missing cursor/style/ENV context under #24.
 * 1P pastes the bound clipboard port's full current viewport without scaling,
 * placing its top-left at the projected XY in the current viewport. The five
 * transfer modes use source inversion for mode 4. Cleared/same-port pointers
 * are no-ops. Missing bindings, dangling pointers and inactive clipboard
 * viewports are unsupported; GET_IMAGE
 * selection/reset/backup/protection policy stays under #42. Clipboard/port/ENV
 * configuration and uncovered pixels remain unchanged; content protection
 * does not prevent copying. All arguments validate before any pixels change.
 * 1W validates its current-radix reserved zero, captures the explicitly bound
 * clipboard viewport and current bound palette, and writes an 8-bit BMP through
 * the trusted host-directory writer. Filename bytes/extensions remain literal,
 * with bare UTF-8 name validation and caseless atomic overwrite. Cleared
 * pointers are no-ops; missing bindings and dangling/inactive clipboard ports
 * are unsupported (#42).
 * Current port, protection, pixels, palette and ENV references are retained.
 * Source capture/encoding finish before any filesystem replacement is attempted.
 * 2W exports a named port without selecting it or using the clipboard pointer.
 * Raw all-zero coordinates select its full viewport; zero lower-right fields
 * select the remainder from the projected upper-left. Explicit inside regions
 * retain inclusive endpoints. Undefined ports/outside origins are no-ops.
 * Partially external lower-rights and inactive sources remain unsupported under
 * #10. Reserved digits and literal filename must be present; current ENV width
 * and radix govern all numeric fields. Encoding/storage share 1W's path.
 * 1b reads a BMP through media lookup, appending .BMP only for extensionless
 * names. It maps to the bound palette and fits the inclusive destination, or
 * uses native file dimensions when both raw lower-right fields are zero.
 * Transfer modes 0..4 and indexed/mapped-RGB transparency are supported.
 * Indexed palette adoption replaces the corresponding prefix of the bound
 * current table, preserves source indexes and suppresses dithering. Protection
 * rejects adoption before painting; failed staging retains both palette and
 * pixels. Flag 4 otherwise enables ordered dithering in the current palette,
 * with indexed transparency before mapping and RGB transparency after mapping.
 * Same-port duplication is ignored; actual duplication and unknown flags remain
 * unsupported. Palette
 * adoption is ignored for true-colour files. Active loads require a bitmap
 * error UI sink; file failures request a box without ending parsing. Actual
 * application UI integration is separate. Inactive viewports ignore the load.
 * Drawing commands use the bound STYLE record's explicit resolved pen and
 * write mode. c/k preserve raw colour values: current-radix palette indices
 * or four-digit UltraNum RGB (8 bits/channel), with last-field shortening.
 * L/R project world geometry without narrowing far endpoints before clipping;
 * direct RGB ink is matched through the bound current palette at drawing.
 * Inactive viewports skip drawing; NOT requires no colour lookup. Protected
 * STYLE records reject attribute edits but permit drawing, as do protected
 * PORT records. Bootstrap/ENV activation and wire pen thickness are separate
 * unresolved policies (#11/#24); W period termination remains unsupported (#7).
 * P/l decode the ordinary two-digit direct vertex count and all XY fields
 * before rendering. P closes its outline; l retains an open path. Both use
 * the bound resolved pen, retain phase across edges and apply each pixel once.
 * P needs at least three vertices and l at least two for this supported branch;
 * smaller counts and the unreachable stated 4096 encoding remain under #13.
 * These commands do not fill interiors or mutate pen/style/port/ENV state.
 * Filled p shares the ordinary count/XY layout and requires an explicit border
 * binding when active. Its integral wide path uses exact even-odd crossings,
 * the current fill/mode, and optional patterned COPY borders. All used fill
 * and border inks resolve before vertex/edge/mask allocations and painting.
 * The same unresolved count boundaries apply; no full 4096 encoding is inferred.
 * Y validates all four two-digit current-radix fields before editing the bound
 * STYLE's classic font attributes. Conflicting justification flags discard the
 * complete command. This does not activate an extended font or text cursor.
 * S/s replace the fill mask/colour and retain the caller's explicit device
 * pattern phase (#12). Custom rows use the current radix, top-to-bottom/MSB-left;
 * colour fields share c/k encoding. Conflicting stock masks remain unsupported.
 * B normalizes opposing corners, includes the last device row/column and draws
 * without a border. Both used fill inks resolve before painting; solid and
 * background-only fills skip unused ink lookup. NOT ignores both inks.
 * K requires an explicit borders binding when active. Its interior excludes
 * the projected right/bottom edges. Degenerate rectangles draw only enabled
 * borders; border coverage is prepared before filling and then applied in
 * solid COPY using the resolved pen thickness (the N rule, pending #33).
 * Border ink still needs resolution in NOT mode. N and border context/save
 * ownership remain unsupported under #24; no missing-binding default is used.
 * C/G map the radius horizontally and use equal device radii on the square-
 * pixel canvases; ellipses map X/Y radii independently. Curves use solid pen
 * thickness, ignoring the pen pattern. G/o require the explicit border binding;
 * all ink lookup and border scratch allocation precede their fills. O/V accept
 * only the documented start=0/end=360 form; other angle pairs remain under #14.
 * A also accepts start=0/end=360 and validates equal-angle bodies as no-ops
 * before accessing rendering services. Other A pairs remain under #14.
 * Argumentless E replaces the active viewport with resolved background ink in
 * COPY, independently of pen/fill/write mode. It retains attributes and table
 * references, skips inactive ink lookup and allows protected contents to clear.
 * Native text-window overlap/context integration remains separate.
 * 3e stores one to four current-radix digits in the current ENV baud-rate field,
 * respecting record protection and preserving all other values/references.
 * Playback pacing and reset persistence require future playback orchestration.
 * 3D validates one to four current-radix tick digits and starts the bound input
 * gate for ticks/60 seconds. It reads ENV without editing even protected records.
 * The gate stops subsequent queued RIP/text delivery without blocking this call;
 * the owner pumps its source when ready. No gate binding means unsupported.
 * Immediate 1ESC mode zero uses the bound owned evaluator with zero reserved
 * digits and an unused window selector. It accepts literals, control notation
 * and ordinary known built-ins, preserving its position across D delays. Full
 * expression preflight rejects unsupported macro/prefix/user/template/pick-list
 * forms before any side effects. Deferred query modes remain unsupported.
 * Nested variable errors remain parser-owned and nonfatal; private evaluator
 * allocation/busy/preflight statuses are consumed at this command boundary.
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
