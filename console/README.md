# `st_console` contract

The user-facing console-library chapter is in `../Console.adoc`.  This file
records the implementation contract and backend details.

`st_console` is SyncTERM's character-generator and desktop/host abstraction.
It is not a compatibility implementation of Borland `conio`, and terminal
parsing (`cterm`) is not part of this component.

## Instances and threads

An `st_console_t` owns its cells, current text state, palettes, event queue,
presentation state, and backend.  Public operations on a live instance may be
called concurrently.  Read-only state queries share a reader lock; mutations,
presentation, and backend operations are serialized by its writer lock.
Destruction is the usual lifetime boundary: the owner must prevent new calls
and allow active calls to return before calling `st_console_destroy`.

Backends that can create independent displays may support multiple instances.
The ncurses interface used here has process-global terminal state, so a second
ncurses instance is rejected with `IO_ERROR_BUSY`.  This limitation does not
turn the public API into a singleton.

Desktop applications link the `st_console_main` target.  Its interface
definition renames the application's `main(int, char **)`, while the target
supplies the real process entry point.  The real main thread owns SDL and
AppKit initialization, windows, rendering, event dispatch, and shutdown;
application logic runs on a joined secondary thread.  The same entry wrapper
services both managers on macOS, so selecting SDL or Quartz requires no
backend-specific setup in the caller.  Multiple SDL and Quartz console
instances receive separate windows.  SDL headers are a build capability; the
optional SDL library and usable video driver are checked at runtime.  AppKit
and Quartz are part of the macOS programming environment and are linked
normally.

The X11 backend owns one display connection, event thread, window, bitmap
presenter, clipboard, and command queue per console instance.  Xlib is loaded
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
pointer, clipboard objects, and shared-memory buffers per instance.  A black
toplevel surface contains a positioned video subsurface, allowing viewporter
scaling to preserve both the aspect-ratio offset and margins without composing
a window-sized image in software.  Busy buffers retain their pending damage;
release copies only that damage from the presenter's current image.  Wayland
and xkbcommon are loaded at runtime.  Generated core, xdg-shell, and viewporter
stubs are build-tree products of `wayland-scanner`, not checked-in sources.

The GDI backend owns a window and message thread per instance.  Presenter
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
never calls a frontend video callback: `st_console_libretro_frame_size` and
`st_console_libretro_frame_copy` provide a locked copy of the last completed
XRGB frame to the enclosing libretro run callback.  Frontend input adapters use
the ordinary `st_console_post_event` interface, so libretro ABI ownership stays
outside this component.

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

`st_console_write_cells` is the semantic Unicode edge: it resolves each
codepoint through the supplied font before storing the cell.  A legacy decoder
first uses `st_console_font_for_flags` to turn its normal/bright/blink font
selection and the cell attributes into that supplied font identifier.  This
keeps bright and blink usable as either display attributes or font selectors,
as selected by the decoder.  `st_console_write_complete_cells` is the
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

The high bit of a color selects direct RGB.  Without that tag, the low eight
bits are a palette index.  Palette identity is preserved through the console;
a text backend does not silently convert indexed cells into RGB cells.

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
comes from generated Unicode 17 East Asian Width `W`/`F` and
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
the two blink-state rasters.  Indexed cell colors are resolved through the
current palette during rasterization; direct RGB cell colors lose their
representation tag and all pixel planes contain packed `0x00RRGGBB`.  Pixel
writes first make intersecting character rasters current, then
modify those same final-raster planes and mark the cells `PIXEL_GRAPHICS`.
Untouched pixels in a cell therefore remain intact.  A later cell-content
change regenerates the cell, while a forced text readback deliberately
regenerates every cell.  Headless instances retain these planes for testing
and off-screen use.  A text backend can provide character readback but rejects
pixel mutations with `IO_ERROR_NOT_SUPPORTED`, since accepting an invisible
draw would be a false success.

Bitmap presentation retains each software-scaling stage.  Dirty cell runs,
blink runs, and old/new cursor rectangles propagate through those stages and
only the resulting damage is uploaded.  Integer stages multiply the rectangle,
pointy and xBR stages expand it by their one- and two-pixel dependency radii,
and interpolation stages use precomputed inverse coordinate maps.
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

Each instance has one ordered event queue.  Translated 16-bit key codes and
physical evdev-style key records are distinct event types; neither is treated
as a substitute for the other.  Text produced by an input method is another
event type.  Backends post only information they actually receive.

Physical presses and releases update per-instance held state.  Posting focus
loss enqueues releases for every held physical key before the focus event and
clears that state.  Clearing queued events does not alter held state.
Non-text key codes use the backend-independent `ST_CONSOLE_KEY_*` values;
printable SDL input is delivered separately as decoded Unicode text.  Physical
SDL input uses evdev key numbers and the `ST_CONSOLE_MODIFIER_*` masks.  SDL
mouse coordinates are expressed in the console's logical pixel grid and its
button state uses the `ST_CONSOLE_MOUSE_BUTTON_*` masks.

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
