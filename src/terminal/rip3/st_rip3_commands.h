/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_COMMANDS_H_
#define ST_RIP3_COMMANDS_H_

#include "st_rip3_clipboard.h"

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
struct st_rip3_text_context;

/* Borrowed UI request, made on the session thread for file/decode failures.
 * name and area live only during show(). Explicit areas are viewport-relative
 * inclusive device coordinates; NULL asks the UI to choose a suitable box for
 * a file whose source dimensions are unavailable. This callback must
 * not reenter command processing or mutate its bindings. */
struct st_rip3_bitmap_error_sink {
	void *context;
	void (*show)(void *context, const char *name, unsigned port,
		const struct st_rip3_bitmap_destination *area);
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
	/* Borrowed input gate. The owning source retains bytes during 3D delays. */
	struct st_rip3_input *input;
	/* Session-owned immediate-query evaluator, resumed by the input source's prepare
	 * callback before further bytes. Uses this source's same input gate. */
	struct st_rip3_query *query;
	struct st_rip3_clipboard_selection *clipboard;
	/* Explicit client policy for captures/definitions, in offscreen device pixels. */
	const uint64_t *offscreen_pixel_budget;
	const struct st_rip3_bitmap_error_sink *bitmap_errors;
	struct st_rip3_text_context *text;
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
 * Implements J, n, f, Q, a, c, k, numeric W, Y, literal-name y, q, =, E, L, R, B, K, C, G, o,
 * X/j in all five modes, m, classic literal T and nonzero-font @,
 * the existing font-zero horizontal-left @ geometry,
 * axis-aligned A/V/O/I/i/_ curves, &/-/]/+/[ skewed contours,
 * Z/z/t/x integer Beziers and P/l/p/< polygon paths,
 * 2Y style selection, N, U/u, s, all S presets 00..0B, 1w, v, 2p, 2s,
 * rectangle-copy 2C, screen/offscreen 2P, clipboard 1C/1P/1W, 2W bitmap export,
 * ordinary/transparent 1b bitmap loads, ordinary 1I icons, wire 1i and literal 1p JPEGs,
 * 3D delays, 3e record edits,
 * comments, terminal grouping no-ops and resumable immediate 1ESC queries.
 * Numeric commands use the current ENV record,
 * documented shortening/reserved omission and J/n invalid-value defaults.
 * Q/a edit the bound palette table's current entry, independently of ENV
 * reference activation; ENV supplies the radix only. All fields are validated
 * before changing the palette, whose own protection applies.
 * '=' has fixed 1/1/4/2 numeric fields. Flags 0/1 use device thickness; 2/3
 * convert using X scale only. Clamp before/after scaling, retain the low byte,
 * and normalize cosmetic zero to effective width one. Only flag value 1 paints
 * off pixels. Stock masks or nonblank custom 16-bit patterns feed the existing
 * device kernels; protected STYLE entries retain their values on rejection.
 * X/j project WORLD coordinates once. X attempts one context-absolute pixel
 * without the stored viewport offset; j adds that origin once and paints a
 * solid square with radius floor(effective thickness/2). Both clip against
 * the stored viewport/physical surface, ignore line/fill patterns and retain
 * wide coordinates until clipping. Every covered pixel receives W once;
 * destination-NOT skips source ink lookup. Repeated commands operate again
 * on the latest destination, independently of thickness and line flags.
 * S/s use canonical 8x8 masks and fixed drawing-context (0,0) phase in device
 * pixels. Shape projection, port/viewport changes and adjacent primitives do
 * not restart or resize the tile. Screen ports share screen phase; offscreen
 * bitmaps have their own local context. Copies move already painted pixels.
 * Port commands use the bound PORT table and current ENV world/width/radix,
 * without activating or updating ENV references. World geometry requires the
 * ENV and PORT device canvases to agree. 2P projects half-open extent boundaries,
 * treats offscreen near coordinates as zero, and applies all initial flags
 * independently. Protected redefinition selects zero. Client policy rejects
 * reserved flags, nonpositive dimensions, unsupported offscreen sizes and wholly
 * external screen definitions. Offscreen definitions require a pixel budget, counting
 * the final table without the replaced target; resource failures retain the old
 * table/current/clipboard state. This rollback policy is not a protocol guarantee.
 * Flag 1 never assigns the clipboard pointer. For 2C, projected
 * all-zero source/destination boundaries mean the full viewport; otherwise
 * zero destination ends synthesize an equal-size destination. Half-open extents
 * select unscaled versus fit before clipping. Equal copies trim together;
 * unequal copies clip independently and fit. Undefined/inactive operands are
 * successful no-ops. Indexed transfers do not establish full RGB compliance.
 * v normalizes WORLD corners, projects once and stores a DEVICE viewport
 * without adding the screen-port origin or clamping to the declared bounds.
 * Projected all-zero coordinates deactivate while retaining geometry/image
 * state. A nonzero replacement reactivates. Physical raster clipping does not
 * change the stored viewport or fit scale. The current coordinate plane still
 * rejects projected endpoints outside int32_t before mutation; complete wide
 * producer support and scene/context integration remain pending.
 * 2s has exactly three port/flag digits, with no reserved field. Only low
 * flags 1/2/4/8 act; higher bits are accepted. Apply source-protect/unprotect,
 * select/create the destination, then destination-protect/unprotect on the
 * actual current port. Unprotect wins within each pair. Undefined targets
 * become full-screen ports; failed creation selects zero and retains source
 * effects. Zero ignores protection writes. CUR(PORT,n) shares this selector.
 * Cursor/style/ENV activation and disabled-graphics routing remain separate.
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
 * Coordinates are raw DEVICE offsets, decoded with current ENV width/radix.
 * Screen sources use half-open crops, independent far clipping and full/remainder
 * zero forms. Offscreen sources export the entire backing bitmap after explicit
 * rectangle validation. Defined protected/inactive sources remain readable;
 * undefined/empty sources diagnose without replacing the file. Reserved digits
 * and literal filename must be present. Encoding/storage share 1W's path.
 * 1i normalizes current-WORLD corners and stores projected DEVICE boundaries
 * with a further far-edge unit, without clipping to the viewport. Flags 0..63
 * replace the old flags. Protected/inactive current ports can be changed.
 * Four flags digits and up to twelve reserved zeros are accepted; omitted
 * reserved digits permit last-field shortening. Raw stored zero is a separate
 * internal inhibit state: all-zero wire input stores (0,0,1,1), still enabled.
 * World conversion failure and invalid fields retain all style/selection state.
 * 1b reads a BMP through media lookup, appending .BMP only for extensionless
 * names. It maps to the bound palette and fits the inclusive destination, or
 * uses native file dimensions when both raw lower-right fields are zero.
 * Transfer modes 0..4 operate on the current 256-entry device model. Key
 * indices 0..255 translate through the drawing palette's normal direct-index
 * limit 236 before adoption. Indexed keys use raw bit versus zero/nonzero for
 * 1-bit, nearest mapped indexes for 4/8-bit; 24-bit keys compare exact RGB.
 * 16-bit keyed loads fail without painting; 32-bit keys select no pixels.
 * Indexed palette adoption replaces the corresponding prefix of the bound
 * current table, preserves source indexes and suppresses dithering. Protection
 * rejects adoption before painting; failed staging retains both palette and
 * pixels. Flag 4 otherwise enables ordered dithering in the current palette,
 * with key classification independent of dithered output and transfer inversion.
 * LOAD_BITMAP accepts flags 0..63; higher values reject before file access.
 * After the initial draw and palette commit, flag 1 duplicates the original
 * full requested rectangle. Zero uses the shared clipboard capture lifecycle;
 * other valid non-current ports are deleted if defined, then allocated/copied
 * without assigning the clipboard. Explicit deletion clears a matching pointer;
 * protection retains it and skips wallpaper, with initial draw effects retained.
 * Allocation/copy failures do not roll back initial drawing or deleted targets.
 * Same-current and >35 explicit selectors skip duplication. Flags 16/32 copy
 * the composited seed through the then-current viewport, using COPY and optional
 * alternating half-width row offsets. Flag 32 alone does nothing. Complete
 * physical-device/color-profile and port-coordinate integration remains separate.
 * Active loads require a bitmap
 * error UI sink; file failures request a box without ending parsing. Actual
 * application UI integration is separate. Inactive viewports ignore the load.
 * 1C normalizes both corners and projects XY through the current WORLD, adds
 * the wire far-edge unit, then invokes shared clipboard capture. It requires
 * a mutable session-owned clipboard selection and explicit pixel budget.
 * Assigned targets are reused; otherwise the first unused nonzero slot wins.
 * Protected targets/zero reject unchanged. Unprotected targets are deleted
 * before allocation; failure clears the pointer without restoring old data.
 * Same-source replacement selects zero and copies from that live current port.
 * The helper adds a further low-WORD allocation unit and stretches the clipped
 * source to the new bitmap. Copy/trim failure retains a clear new clipboard and
 * is not propagated. Wrapped zero allocation counts remain unsupported.
 * 1I projects XY through the current WORLD and selects
 * native file dimensions (scale=0) or fixed 640x350-reference image dimensions
 * (scale=1), rounding positive counts down. It uses the current palette without
 * adoption or dithering, with the same five transfer modes and viewport clipping.
 * Active loads require the bitmap-error sink. Valid file/decode failures request
 * a placeholder on the current port for both clipboard=0 and 1, without clearing
 * the pointer, deleting/replacing any port or changing selection. Render failures
 * in the clipboard=0 path make the same request. Allocation failures may prevent
 * the UI from drawing but never clear clipboard state. Invalid names do not
 * request a placeholder. Clipboard=1 uses shared GET_IMAGE after painting, with
 * the renderer's original unclipped rectangle, including wholly outside areas.
 * Its protected-target rejection/allocation failure is after painting and does
 * not request a placeholder. Missing capture bindings decline before painting
 * a valid file, without suppressing ordinary load failures. Legacy ICN conversion
 * remains pending under the adopted #19 contract. All UI requests are void and
 * every service status stays inside this nonfatal parser boundary.
 * 1p reads an exact bare UTF-8 filename through MEDIA lookup and the baseline
 * JPEG decoder, then uses the current port's explicitly bound DEVICE image
 * style and current palette. Painting always uses COPY; no STYLE binding or
 * background color is consulted. Initial clearing is black COPY over the fitted
 * rectangle only, unless flag 4 skips it. Aspect margins retain their contents.
 * JPEG flag 8 changes no palette; flag 2 has no file-deletion effect. Inactive
 * viewports and the explicit internal inhibit state are no-ops before consulting
 * asset/palette bindings. That inhibit state is not wire 1i's all-zero rectangle.
 * After successful JPEG painting, flags 16/32 repeat the stored image-area
 * pixels with COPY and optional staggered rows. Raw boundaries, not the fitted
 * JPEG rectangle, define the tile. Post-draw wallpaper failures retain the
 * initial paint and remain operation/resource diagnostics inside this parser.
 * Macro expansion, delivery and whole-scene reset/ownership integration remain
 * separate pending work in the adopted #21/#24 contract.
 * Invalid names and load/decode/initial-staging failures retain the scene and produce
 * only a nonfatal diagnostic; no file is deleted or changed.
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
 * Zero is diagnosed without pixels; one validates then draws nothing; two
 * retain ordinary pen behavior. The maximum direct counts are 1295/4095.
 * These commands do not fill interiors or mutate pen/style/port/ENV state.
 * Filled p shares the ordinary count/XY layout and reads the current STYLE
 * border setting. Its integral wide path uses exact even-odd crossings,
 * the current fill/mode, and optional patterned COPY borders. All used fill
 * and border inks resolve before vertex/edge/mask allocations and painting.
 * < uses C:2, N1:2 and later Ni:XY with Ni>=2 and 1+C+2V<=8193.
 * Its contours share an even-odd interior and prepared patterned COPY border.
 * Y validates all four two-digit current-radix fields before editing the bound
 * STYLE's classic font attributes. Conflicting justification flags discard the
 * complete command. This does not activate an extended font or text cursor.
 * Successful Y selects the classic font family in STYLE; rejected/protected
 * edits preserve both family and attributes, independently of text bindings.
 * 2Y accepts one style digit and one/two final flag digits. Source flags 4/8
 * apply before selection/defaulting, destination flags 1/2 afterward, with
 * unprotect last in each pair. High bits are ignored. Invalid slots >=36
 * are ignored before any flag effects. Protect-zero diagnostics do not stop
 * selection or remaining flags; failures are consumed at the void boundary.
 * 2Y and CUR(STYLE,n) activate represented attributes/font family without
 * changing PORT, ENV, graphics cursor, encoding or immutable font overrides.
 * m stores a WORLD-converted point plus the viewport origin in the current
 * PORT's context-absolute integer cursor, without needing a text binding.
 * T renders at that cursor relative to the current viewport, then advances X
 * by the signed low WORD of the whole integer string metric for every classic
 * orientation/justification; Y is retained. Raster advances remain fractional.
 * Nonzero classic-font @ uses its separately converted drawing anchor, but
 * stores raw input X/Y plus viewport origin and that integer width, matching
 * the shipped mixed-unit cursor defect. The font-zero cached-outline metric
 * branch remains pending; its existing horizontal-left geometry stays limited.
 * Empty T retains the cursor silently; empty @ diagnoses without moving it.
 * E, STYLE/ENV selection and viewport changes do not move the stored cursor.
 * Literal text uses the explicit CP437/UTF-8 transport binding, mapping UTF-8
 * to CP437 with one fallback per unmappable scalar. Macro expansion, outline
 * activation and application routing remain separate. Classic fonts come from
 * STYLE and original bundled canvas data, with an optional immutable override.
 * All used inks/owned coverage prepare before painting; failures preserve cursor
 * state and remain inside the void parser. Inactive viewports skip valid text
 * and MOVE before consulting text/style/encoding services. Protected content
 * can draw and MOVE. PORT backups retain live cursors rather than saved values.
 * S/s replace the fill mask/colour and retain the caller's explicit device
 * pattern phase (#12). Custom rows use the current radix, top-to-bottom/MSB-left;
 * colour fields share c/k encoding. Conflicting stock masks remain unsupported.
 * B normalizes opposing corners, includes the last device row/column and draws
 * without a border. Both used fill inks resolve before painting; solid and
 * background-only fills skip unused ink lookup. NOT ignores both inks.
 * K reads the current STYLE border setting. Its interior excludes
 * the projected right/bottom edges. Degenerate rectangles draw only enabled
 * borders. Border coverage is prepared before filling, then applied as solid
 * COPY with Draw Color/current thickness, independently of stored masks/gaps.
 * NOT ignores fill inks but still resolves an enabled COPY border's color.
 * N decodes one/two current-base digits: zero disables borders, nonzero enables
 * them in the current STYLE, respecting its protection even on inactive ports.
 * ENV/PORT selection and protection do not own this setting. Fixed new-style
 * defaults enable it, and STYLE snapshots preserve it. Scene reset wiring
 * and complete extended-style activation remain separate.
 * y consumes 26 fixed UltraNum characters and an owned, truncated 39-byte
 * literal name. It retains all flags/compatibility bytes, accepts size zero,
 * validates rotations/positive spacing and converts optional world baseline
 * magnitude once at definition. Ignored fields have no stored state. q ORs
 * current-base attributes 0..15 into an unprotected outline STYLE; Y selects
 * classic mode while retaining dormant outline fields. No font lookup,
 * outline text production or name expansion is implied by these state edits.
 * U/u consume five complete XY fields, normalize corners and scale radius
 * with X. Corner semiaxes fit each device span independently; the original
 * low-WORD Y-radius sign/zero behavior is retained. U always strokes; u uses
 * its STYLE border setting and current brush. U honors the requested mask
 * at every width, including custom masks, and applies W. u fills with W then
 * COPYs a solid border. Line flags never partition brush/border contributions.
 * Curve/side coverage is prepared before filling; failures remain nonfatal.
 * C/G map the radius horizontally and use equal device radii on the square-
 * pixel canvases; ellipses map X/Y radii independently. Curves use solid pen
 * thickness, ignoring the pen pattern. G/o read their STYLE border setting;
 * all ink lookup and border scratch allocation precede their fills. I/i/_
 * use W for the region and solid COPY for every enabled border, independently
 * of stored line patterns/gaps and W. N0 suppresses every radial/closing part.
 * Axis angles compare integer endpoint rays: A/V equal endpoints draw a W
 * point; I/i draw a COPY radius only with N1, and _ is empty. Full Pie retains
 * its internal radius only with N1. O consumes angles but always draws a full
 * ellipse. Numeric/topology behavior remains distinct from output composition.
 * Filled skewed -/+/[ paths use the shared even-odd polygon fill with W and
 * optional patterned COPY borders, including opaque/transparent gaps. Their
 * line masks do not restrict the selected fill mode. Standalone repeated-turn
 * XOR/NOT paths remain unsupported until genuine visits replace union coverage.
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
