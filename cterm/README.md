# Terminal input and text placement

`cterm_input_write` is the common input driver for wire input, RIP text
passthrough, semantic UTF-8 injection, and macro playback. It selects the
active main/status display before each input byte and owns parser dispatch,
text boundaries, deferred status destruction, and original-byte consumption
counts. `cterm_parse_byte` interprets one protocol byte; it does not route a
receive buffer or select an encoding.

Ordinary codepage text becomes UTF-8 at the input boundary. Byte-oriented
emulations first interpret their controls and character-set transformations,
then emit UTF-8 into the same presenter. Sequence parameters, font uploads,
macro definitions, and other protocol payloads remain bytes. Receive filters
and text capture see decoded text and controls, while raw payloads bypass them.
Capture additionally receives explicitly tagged protocol bytes for raw logs;
these bytes are observed without invoking Unicode filters or a second parser.
Semantic UTF-8 injection bypasses the receive filter; replay invokes neither
receive filtering nor capture a second time. Raw logging records wire bytes;
ASCII/text logging writes presented UTF-8 and terminal text controls.

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

Provisional graphemes retain the affected row and cursor/wrap state for
revision. Scrolling moves the retained drawable's position; a later combining
scalar follows that position without scrolling twice. REP retains an owned
UTF-8 grapheme rather than an encoded byte. Macro playback owns a copy of its
input so reset or redefinition cannot invalidate the executing bytes.

The parent owns the status subterminal. A subterminal retired during nested
playback moves to the parent's retirement list while its input execution depth is
nonzero. The driver destroys it after that stack unwinds, including its
presenter, saved row, and repeat state. Parser errors are transferred to the
owner before destruction.

`cterm_text` tests compare equivalent codepage and UTF-8 input, full buffers,
single-byte writes, and every split of short fixtures. They cover retained
cells, cursor/margin/wrap state, scrollback, responses, byte emulations,
grapheme revision, raw uploads, replay, and shutdown/error boundaries.
