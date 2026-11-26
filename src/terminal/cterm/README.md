# Terminal input and text placement

The connected session owns graphics dispatch. Its graphics devices send ordinary
text and emulation controls to CTerm. CTerm selects the active main or status
display, handles parser dispatch and text boundaries, and reports consumption
in original bytes. Macro playback and local UTF-8 injection share the same
input driver.

The wire decoder supplies a Unicode scalar or malformed byte sequence for text.
Byte-oriented graphics adapters retain the original spelling for their grammar
and forward ordinary text through CTerm's byte entry point. Codepage text maps
directly to a Unicode scalar. Byte-oriented emulations interpret their controls
and character-set transformations before presenting text. Sequence parameters,
font uploads and stored macro definitions retain their bytes. Receive filters
and text capture see decoded text and controls.
The semantic capture callback also tags opaque protocol bytes so text-only
consumers can omit them. ANSI capture instead observes the original connection
bytes in the session layer before wire hooks, RIP conversion, and decoding.
Semantic UTF-8 injection bypasses the receive filter; replay invokes neither
receive filtering nor capture a second time. CTerm's own raw log records its
input bytes; ASCII/text logging writes presented UTF-8 and terminal text controls.

Each display owns one incremental Unicode presenter regardless of its wire
encoding. Receive-buffer boundaries preserve incomplete decoding and grapheme
state. Controls, display/style changes, RIP commands, encoding changes,
resizing, reset, and finish commit pending text. Resizing preserves an
incomplete UTF-8 scalar and partial RIP command; it commits placement without
ending the input stream. Unsupported wire encodings
for byte emulations remain unsupported; internal UTF-8 does not change their
wire protocols or add bidirectional-text handling.

`cterm_text.c` owns printable text placement for every source. It resolves a
normalized UTF-8 grapheme to a font drawable, then applies terminal margins,
wrapping, scrolling, scrollback, and wide-cell pairing. The console receives
resolved cells, so console text convenience functions cannot advance the
cursor independently. Emulation attributes, fonts, mosaic mappings, and
movement policies still belong to their existing handlers. Cell editing and
Prestel serial-attribute control cells remain explicit protocol operations.

CTerm tracks the position, width and wrap state of the provisional glyph.
When a revision narrows it or wraps it onto the next line, each vacated cell
receives a space in the current style. The cursor follows the revised glyph's
width. A scroll moves the tracked position and remains in effect. REP retains
an owned UTF-8 grapheme. Macro playback owns a copy of its
input so reset or redefinition cannot invalidate the executing bytes.

The parent owns the status subterminal. A subterminal retired during nested
playback moves to the parent's retirement list while its input execution depth is
nonzero. The driver destroys it after that stack unwinds, including its
presenter and repeat state. Parser errors are transferred to the
owner before destruction.

`cterm_text` tests compare equivalent codepage and UTF-8 input, full buffers,
single-byte writes, and every split of short fixtures. They cover retained
cells, cursor/margin/wrap state, scrollback, responses, byte emulations,
grapheme revision, raw uploads, replay, and shutdown/error boundaries.
