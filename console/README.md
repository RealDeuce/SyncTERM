# `st_console` contract

The user-facing console-library chapter is in `../Console.adoc`.  This file
records the implementation contract and backend details.

`st_console` is SyncTERM's character-generator and desktop/host abstraction.
It is not a compatibility implementation of Borland `conio`, and terminal
parsing (`cterm`) is not part of this component.

## Displays, consoles, and threads

An `st_console_display_t` owns one physical backend, window or host terminal,
the presentation worker, physical-key state, and the currently active logical
console.  An `st_console_t` owns cells, pixels, current text state, palette,
font registry, cursor, event queue, and desired title and pointer state.  More
than one logical console may be attached to a display, but exactly one receives
new backend events and presentation callbacks at a time.

`st_console_display_activate()` is a synchronous switch.  Pending input is not
transferred: each event remains in the queue belonging to the console active
when it arrived.  Inactive consoles remain fully mutable, and an explicit
present drains their raster work without calling the physical backend.  Font
identifiers and least-recently-used eviction remain per console.

The `st_console_create(result, display_config, console_config)` convenience
constructor owns a display for a single console.  Backend selection, terminal
encoding/I/O, and initial window size/scaling belong to `display_config`.
The logical `console_config` specifies the cell/raster geometry and rendering
policy; it has no physical-output fields that an attachment would ignore.
Multi-console applications create a display explicitly and use
`st_console_create_on_display()` for each logical console.  The display must
outlive its attached consoles.  The owner must prevent new calls and allow
active calls to return before destroying either object.

Desktop applications link the `st_console_main` target.  Its interface
definition renames the application's `main(int, char **)`, while the target
supplies the real process entry point.  The real main thread owns SDL and
AppKit initialization, windows, rendering, event dispatch, and shutdown;
application logic runs on a joined secondary thread.  The same entry wrapper
services both managers on macOS, so selecting SDL or Quartz requires no
backend-specific setup in the caller.  Multiple SDL and Quartz displays
receive separate windows.  SDL headers are a build capability; the
optional SDL library and usable video driver are checked at runtime.  AppKit
and Quartz are part of the macOS programming environment and are linked
normally.

The X11 backend owns one display connection, event thread, window, bitmap
presenter, clipboard, and command queue per display.  Xlib is loaded
at runtime; its headers are only a build capability.  XRender supplies external
final scaling when both its declarations and runtime are available, and
selecting external scaling otherwise fails during console creation.  The
ordinary XImage path precomputes visual component values and writes common
8-, 16-, 24-, and 32-bit pixel layouts directly, retaining `XPutPixel` only as
the uncommon-layout fallback.

X11 physical input uses XKB key names before considering the conventional
evdev keycode offset or a keysym fallback.  Committed text comes from an Xlib
input context as UTF-8, so keyboard layouts, dead keys, compose processing, and
external input methods remain in the host input path.  The application selects
`LC_CTYPE` from its environment before creating a console; the console library
does not change the process locale.  Keysyms are translated directly only for
non-text keys.  Each instance owns its input context and selection and supports
UTF8_STRING plus the ICCCM STRING fallback.

The Wayland backend owns a display connection, worker, xdg toplevel, keyboard,
pointer, clipboard objects, and shared-memory buffers per display.  A black
toplevel surface contains a positioned video subsurface, allowing viewporter
scaling to preserve both the aspect-ratio offset and margins without composing
a window-sized image in software.  Busy buffers retain their pending damage;
release copies only that damage from the presenter's current image.  Wayland
and xkbcommon are loaded at runtime.  Generated core, xdg-shell, and viewporter
stubs are build-tree products of `wayland-scanner`, not checked-in sources.

The GDI backend owns a window and message thread per display.  Presenter
damage becomes a clipped invalidation, while retained XRGB pixels let normal
paint/expose messages redraw without taking the console state lock.  Windows
Unicode messages provide text and physical input is reported with evdev-style
identity.  The native Win32 text backend instead owns the process's active
console screen buffer and input mode, restores both on destruction, and is
therefore limited to one live instance.  It writes only changed horizontal
cell runs, and provides the Windows title, Unicode clipboard, URL-launch, and
console display-mode operations.  Direct RGB is approximated by the console's
16 colours and a glyph outside one UTF-16 code unit is represented by `?`;
bitmap fonts and pixel planes require GDI or another bitmap backend.

Libretro presentation is deliberately pull-based.  The application thread
never calls a frontend video callback: `st_console_display_libretro_frame_size` and
`st_console_display_libretro_frame_copy` provide a locked copy of the last completed
XRGB frame to the enclosing libretro run callback.  Frontend input adapters use
`st_console_display_post_event`, so libretro ABI ownership stays outside this
component.  The frame-copy call takes both destination stride and row capacity:
a switch after a size query can make the destination too small, in which case
copy fails without writing to it.

Automatic selection prefers the native desktop bitmap backend (GDI on Windows
and Quartz on macOS), then Wayland, X11, and SDL where built and usable.  A
text-only configuration prefers the native Win32 console on Windows, then
ncurses and ANSI.  Failure of an optional runtime-loaded desktop stack falls
through without exposing backend state to the caller.

An explicit ANSI output callback is an advanced connection path.  A callback
must consume the complete byte span or return an error, and must not re-enter
the same console while it is being called.  Its `indexed_colours` and
`direct_colour` members independently declare the two output capabilities
because an arbitrary callback cannot be probed.  The ordinary terminal path
selects UTF-8 or CP437 cell output from
`text_presentation`, obtains the terminal size, decodes keyboard input, and
uses DoorWay keyboard reporting when the enclosing terminal implements it.
CP437 output encodes the retained Unicode codepoint through the
locale-independent codepage table and substitutes `?` when it has no mapping;
the resolved bitmap glyph index is not a terminal character code.

## Cells and coordinates

Coordinates and dimensions are zero-based `uint32_t` values.  Rectangles have
nonzero width and height and must fit entirely in the current grid.  Source and
destination row strides are cell counts expressed as `size_t`.

`struct st_console_cell` is a compact 24-byte value.  `codepoint` retains the
Unicode scalar, `font` retains the flag-resolved selected registration, and
`glyph` is its already-resolved bitmap index.  The single
`ST_CONSOLE_CELL_FALLBACK_FONT` bit records when font zero supplies that glyph;
this preserves the selected font for a later character change without adding
another field.  In particular, an SAA5050 control character is stored in the
masked `flags` field; it replaces none of those values.
Single-bit attributes also use named masks in `flags`, not byte-sized Boolean
members or implementation-defined C bit-fields.  No cell contains pointers or
owns a separate allocation.
`ST_CONSOLE_CELL_BRIGHT` and `ST_CONSOLE_CELL_BLINK` retain source attribute
bits 3 and 7 independently of `ST_CONSOLE_CELL_BOLD`.  The two legacy-colour
provenance flags opt base palette indices into the current video-flag policy;
explicit palette and RGB writes do not acquire that policy accidentally.

`st_console_write_cells` is the semantic Unicode edge: it resolves each
codepoint through the supplied font before storing the cell.  A legacy decoder
first uses `st_console_font_for_flags` to turn its normal/bright/blink font
selection and the cell attributes into that supplied font identifier.  This
keeps the source bright and blink bits usable as display attributes and/or
font selectors, as selected by the decoder, without conflating bright with
the independent bold style.  `st_console_write_complete_cells` is the
puttext-style bypass for cells whose font and glyph are already resolved.  It
validates registered glyphs and wide-cell pairs when a bitmap raster is
present, then preserves those choices without Unicode lookup.  A text-only
instance preserves the same complete-cell fields but need not have bitmap
fonts registered because its backend emits the retained codepoints.

A semantic cell may be written before its requested font identifier is
registered, as happens for the console's initial blank cells.  The first
registration of that identifier resolves only those deferred cells.  Reusing
an occupied identifier during least-recently-used replacement does not rescan
or dirty existing cells.

The high bit of a colour selects direct RGB.  Without that tag, values from
zero through 65,535 are palette indexes.  The same representation is used by
cells and `st_pixel_buffer`, so palette identity survives text rasterization,
pixel capture, snapshots, and blits.  Invalid untagged values above 65,535
resolve to black at presentation and produce one diagnostic for that resolve
operation.  A text backend resolves the logical RGB before selecting its
terminal representation; it does not rewrite the stored cell colour.

The default palette contains the RGB-bit-order system colours in entries
0–15, the xterm 256-colour values in entries 16–255, and the legacy C64,
Atari, and Atari ST mode colours in entries 256–281.  Remaining entries start
black.  Palette setters accept any contiguous range through entry 65,535;
the fourth argument can therefore be 65,536 when `first` is zero.

`ST_CONSOLE_CAP_PALETTE_CHANGE` means changing a logical entry can affect
already retained indexed content; `ST_CONSOLE_CAP_EXTENDED_PALETTE` means that
guarantee covers the complete 16-bit index space.  Bitmap and direct-colour
backends provide both.  A mutable 16-colour Win32 console provides palette
change without the extended guarantee.  Ncurses treats packed direct colour,
large indexed colour, and mutable terminal palettes as separate capabilities;
an indexed terminal needs at least 65,535 reported colours plus the extended
colour and pair APIs before it advertises the complete space.
`ST_CONSOLE_CAP_BRIGHT_BACKGROUND` separately reports that the backend can
present the sixteen-colour bright-background interpretation; it says nothing
about mutable or extended palettes.

The per-console video flags preserve the legacy meanings of source attribute
bits 3 and 7.  Bright foreground, blink, bright background, and the two font
selectors are independently controlled.  Presentation changes invalidate
rendered content, while selector changes affect subsequent writes; neither
rewrites cells or their already-resolved font identifiers.
The geometry-owned `ST_CONSOLE_VIDEO_EXPAND` flag is immutable after creation.
For an eight-pixel font in a nine-pixel cell it produces one blank ninth
column; `ST_CONSOLE_VIDEO_LINE_GRAPHICS_EXPAND` instead repeats the eighth
column for glyph indices 0xC0 through 0xDF.

`ST_CONSOLE_CELL_DIRTY` belongs to the implementation.  Region writes ignore
that bit from callers, and region reads do not return it.  Changed cells receive
the bit in the low-bandwidth mutation path and set the instance's `changed`
fast path.  Rasterization also marks every cell it regenerates when a
dependency such as SAA5050 double-height row state widens the original damage.
The bitmap presenter converts contiguous dirty-cell runs to pixel rectangles;
damage is not accumulated into an instance-level rectangle in the rendering
path.

Font registrations use internal 16-bit identifiers and contain sorted Unicode
maps.  There is no implicit codepoint-to-glyph identity map, including for a
256-glyph font.  Font zero is the Unicode fallback.  Lookup checks the selected
font's explicit map first and then font zero's explicit map.  If neither maps
the scalar, font zero supplies a width-matched replacement glyph.  Static
registration borrows process-lifetime immutable data without copying it;
copied registration owns the bitmap and Unicode map until console destruction,
which also permits a connection decoder to supply a mapping with a downloaded
font.  Registrations track their most recent lookup or render.  If all 65,536
slots are occupied, a new registration replaces the least recently used slot,
succeeds, and reports the local replacement on standard error.  Existing cells
are not dirtied merely because that local slot was replaced.

An eight-pixel glyph occupies one cell and a sixteen-pixel glyph occupies two.
Mapped glyph width comes from font data.  Missing-scalar replacement width
comes from generated Unicode 18 East Asian Width `W`/`F` and
`Emoji_Presentation` data, never the process locale or `wcwidth()`.  Ambiguous
and text-default emoji characters remain one column.  The second cell has
`ST_CONSOLE_CELL_CONTINUATION`, and mutations preserve complete pairs.  With
wrapping enabled, a wide glyph at the final column wraps before it is written.
The current tentative no-wrap rule draws nothing and clamps the cursor to the
final column.

Supplying both `cell_width` and `cell_height` enables the software pixel
planes.  Supplying only one is invalid; leaving both zero avoids allocating a
pixel surface for text-only instances.  By default the raster is exactly the
cell grid.  Explicit `pixel_width` and `pixel_height` may make it larger, for
video modes whose raster includes scanlines or columns outside the text image;
both must be supplied and neither may be smaller than the text image.  Text
scrolling leaves that non-text margin stationary.  The primary and alternate
planes are
the two blink-state rasters.  Rasterization preserves indexed and direct cell
colours in those native planes.  The bitmap presenter resolves a complete
native frame into packed RGB before scaling or host upload.  A palette change
therefore needs no text rerasterization and produces one full-frame resolve,
scale, and upload for the batch.  Ordinary cell and pixel damage remains
regional.  Pixel writes first make intersecting character rasters current, then
modify those same final-raster planes and mark the cells `PIXEL_GRAPHICS`.
Untouched pixels in a cell therefore remain intact.  A later cell-content
change regenerates the cell, while a forced text readback deliberately
regenerates every cell.  Headless instances retain these planes for testing
and off-screen use.  A text backend can provide character readback but rejects
pixel mutations with `IO_ERROR_NOT_SUPPORTED`, since accepting an invisible
draw would be a false success.

The cell metadata last represented in the pixel planes is retained in
`drawn_cells`; it is not a second pixel raster.  Comparing it with the live
cells identifies text which still needs rasterization.  Prestel lower halves
also depend on the cell above, while Mode 7 lower halves remain independently
sourced.

Bitmap presentation retains four unscaled RGB images: primary and alternate
text planes, each with and without the cursor.  A drained content batch updates
all four in order and advances one generation.  Selecting a blink or cursor
phase only selects an already composed image and does not advance that
generation.  Each image has its own lazy software-scaling cache, so a warm
phase change performs no scaling.  Damage in the immediately preceding
generation makes all four caches stale but can update a selected cache
regionally; skipped generations and
geometry or scaling-policy changes cause a full rescale.  Integer stages
multiply the rectangle, pointy and xBR stages expand it by their one- and
two-pixel dependency radii, and interpolation stages use precomputed inverse
coordinate maps.  The upload callback receives stable image identity and
generation values so a backend may retain matching host resources.
The zero-initialized and traditional SyncTERM policy is blocky scaling;
`ST_PIXEL_SCALING_POINTY` explicitly selects the old Pointy pipeline.

## Stateful text

The current position is absolute within the grid and must lie inside the active
window.  Changing the window moves the position to its upper-left cell.  Text
style, position, clipping/scroll window, wrapping, clearing, scrolling, and
line insertion/deletion are retained on the same console instance.
Rectangular cell moves have snapshot source semantics for overlap.  The clear
variant fills only the part of the source rectangle not covered by its
destination.  Scrolling and line insertion/deletion move matching cached
character rasters as well as cells, so retained rows do not pass through the
font renderer again.  Only newly exposed cells are rasterized in the ordinary
case; row-wide SAA5050 double-height state is still recomputed and invalidates
rows whose interpretation changed.  A full-width scroll at the top of the
grid advances a circular text-raster origin rather than copying the retained
pixel rows.  The ring covers only the text image, so extra raster scanlines and
columns never participate in the scroll.

`st_console_benchmark` is a standalone performance probe built with the test
targets.  It reports full 80-by-25 font-raster time and the sustained
scroll-plus-new-row path separately.  It is intentionally not a CTest timing
test; results are for comparing builds on the same host, not a platform-wide
pass threshold.

Writing a printable glyph in the final column wraps immediately when wrapping
is enabled.  At the bottom of the window, that immediate wrap scrolls the
window.  Carriage return moves to the window's left edge; line feed retains the
column and advances or scrolls; tab writes spaces to the next eight-column tab
stop; backspace moves left and replaces that cell with a styled space.

UTF-8 decoding is locale-independent and rejects overlong encodings, surrogate
values, incomplete sequences, and values beyond U+10FFFF.  A string is fully
validated before any cells are changed.  Formatted output is deliberately not
part of this API while there is no locale-independent formatting component.

## Presentation and snapshots

Bitmap backends collect ordinary visible state changes for up to 10 ms before
presenting them.  This replaces the legacy blinker thread's implicit batching:
small terminal-parser writes accumulate dirty cells instead of synchronously
rasterizing and crossing into the window system for every write.
The first pending change establishes the deadline; later changes merge without
postponing it.  The same presentation worker owns the legacy animation
schedules for every window bitmap backend: PC cursor/text phases are 133/266
ms, Prestel phases are 314 ms with its asymmetric text visibility, Commodore
uses complementary 333 ms cursor/text phases, Atari leaves its cursor on, and
Atari ST toggles only its cursor at 500 ms.  Missed phase deadlines are skipped
rather than replayed.  Libretro instead selects the current retained image when
the host requests a frame.
`st_console_begin_update` and `st_console_end_update` nest and prevent a batch
from being presented before its outermost end.  Explicit `st_console_present`
is a synchronous forced presentation boundary.  Text backends remain
synchronous so write failures can be returned to the caller.

Snapshot creation is also a drain boundary.  Pending changes are first handed
to the backend and their live dirty flags are consumed, even if a batch is
open.  The opaque snapshot is then copied without dirty flags.  A snapshot can
only be restored to its originating console with identical dimensions, and
restoration marks
all restored cells dirty.  A snapshot also retains the palette, current style,
window, position, cursor state, wrapping mode, and enabled pixel planes.  Font
registrations are instance resources rather than screen contents and are not
copied into a snapshot.

## Input

Each logical console has one ordered event queue.  Translated 16-bit key codes and
physical evdev-style key records are distinct event types; neither is treated
as a substitute for the other.  Text produced by an input method is another
event type.  Backends post only information they actually receive.

Physical presses and releases update display-wide held state.  Posting focus
loss enqueues releases for every held physical key before the focus event and
clears that state.  Clearing queued events does not alter held state.
Non-text key codes use the backend-independent `ST_CONSOLE_KEY_*` values;
printable SDL input is delivered separately as decoded Unicode text.  Physical
SDL input uses evdev key numbers and the `ST_CONSOLE_MODIFIER_*` masks.  Mouse
events carry zero-based cell coordinates in `x` and `y`.  Bitmap backends also
provide zero-based internal-raster coordinates in `pixel_x` and `pixel_y`;
text-only backends set those fields to `-1`.  Button state uses the
`ST_CONSOLE_MOUSE_BUTTON_*` masks, and `modifiers` records the keyboard
modifier state at the time of the mouse event.  Synthetic physical input uses
`st_console_synthesize_physical_key()` to suppress duplicate held-state edges
without changing state when queueing fails.

Bitmap window backends consume Alt+Left and Alt+Right to snap the drawable to
the previous or next integer display scale, Alt+Home centers the window where
the window system permits positioning, and Alt+Enter toggles fullscreen.
Centering, snap limits, maximizing, and fullscreen target the display that
currently contains the window rather than the complete multi-display desktop.
These window-manager commands are not also delivered as application keys;
ordinary Alt-modified keys remain input events.  On Quartz the shortcut
modifier is Option, with Command accepted as a native-Mac alias.
Wayland also consumes Alt+left-button press to request an interactive window
move from the compositor with the pointer event's serial.

Title, clipboard, URL, pointer, and fullscreen operations remain methods on the
console because the object represents the desktop/host connection as well as
the cell grid.  They return `IO_ERROR_NOT_SUPPORTED` independently when the
selected backend lacks that operation; support is not implied wholesale by a
backend name.  Text returned by a backend is released with
`st_console_free_text`, preserving the allocator boundary.

Backend capabilities which cannot yet be established reliably are listed in
[CAPABILITY_GAPS.md](CAPABILITY_GAPS.md).  Unfinished implementation work does
not belong in that list.

<!-- Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause -->
