# Console capability gaps

This file records backend behavior that is deliberately not inferred from an
operating-system name, compiler name, or an optimistic terminal escape.  An
item stays here until the implementation has a reliable capability probe or
an explicit backend contract.

## ANSI terminals

- Terminal palette mutation is not implemented.  Re-emitting an indexed
  foreground or background value does not change the palette, and converting
  indexed cells to direct RGB would destroy their index semantics.  OSC 4 is
  not sent until the backend can determine that the connected terminal accepts
  it and can restore the previous palette when the instance closes.
- The automatic ANSI path uses terminfo's colour count and `RGB` flag.  If the
  terminfo entry does not advertise direct colour, `COLORTERM=truecolor` or
  `COLORTERM=24bit` is accepted as an explicit host declaration.  Indexed
  cells remain indexed even on a direct-colour terminal; literal RGB cells use
  direct colour only when it is available.  An explicit output callback must
  declare basic, indexed, or direct-colour support.
- ANSI text output is selected explicitly: Unicode presentation writes UTF-8,
  while CP437 presentation encodes the cell's retained Unicode scalar through
  the locale-independent CP437 table and substitutes `?` when necessary.  The
  command-line forms are `-iA`/`-iAU` and `-iAC`, respectively.  No terminal
  query can prove that an independently configured terminal uses the selected
  encoding.
- ANSI has no portable way to select a registered bitmap font.  Unicode mode
  emits the cell's Unicode scalar as UTF-8; CP437 mode encodes that same scalar
  rather than treating its bitmap glyph index as a byte.  Both skip the stored
  continuation cell.
  SyncTERM deliberately derives one- versus two-column width from its font
  data without consulting the process locale; a host terminal whose width
  policy disagrees may position that glyph differently.  Cursor visibility
  uses the broadly implemented DEC private
  sequence; the requested block/underline/bar shape uses DECSCUSR without a
  reliable query and may be ignored by the connected terminal.
- ANSI input decodes C0 keys, UTF-8 or CP437 text, conventional CSI/SS3 keys,
  xterm modifier parameters, and DoorWay `NUL scan` and `NUL E0 scan` keys.
  There is still no safe detection for modifyOtherKeys, CSI-u, kitty keyboard
  reporting, focus reporting, bracketed paste, SGR mouse reporting, or
  key-release reporting.  The backend does not enable those modes or invent
  physical-key and release events.
- ANSI clipboard access is not implemented.  OSC 52 availability, permission,
  size limits, query support, and reply routing all require a capability and
  security contract.
- Opening URLs and changing fullscreen state have no general ANSI-terminal
  mechanism and are not exposed as successful operations by this backend.

## ncurses

- The build can determine whether curses declarations are available, and the
  backend will resolve the actual implementation at runtime.  Wide-character
  output remains a separate declaration-and-symbol capability so an
  ASCII-only curses implementation does not masquerade as a wide one.
- ncurses is process-global in the interfaces being used.  Multiple concurrent
  ncurses console instances are rejected until an implementation can prove
  that its screen-specific API isolates terminal state and all called entry
  points.
- ncurses 6.1 extended pairs are detected by resolving `alloc_pair`,
  `find_pair`, and `init_extended_pair`, and the ABI-6 `wattr_set` option is
  used so pair identifiers are not narrowed to signed 16-bit.  Direct RGB is
  enabled only when the active terminfo entry supplies a valid `RGB`
  capability whose component widths agree with its `colors` limit.  Older
  implementations retain the basic-pair fallback.
- Mutable indexed palette entries within the terminal's reported `colors`
  limit use `color_content`/`init_color`.  The original value is saved on the
  first mutation and restored when the instance closes.  Cell colour indexes
  are only 0 through 255, so the standard functions do not narrow them; the
  extended `int` interface remains necessary for direct colours and pairs.
- Curses reports key presses but generally cannot report physical identity or
  releases.  Those fields remain unavailable rather than being synthesized.
- Wide curses input identifies ordinary characters separately from `KEY_*`
  values and is reported as text.  With a narrow curses implementation, only
  US-ASCII bytes have an encoding-independent text meaning; other returned
  values are retained as translated key codes rather than guessed Unicode.
- Registered bitmap fonts and final-raster pixel edits cannot be represented
  by curses.  A Unicode scalar uses the terminal font and continuation cells
  are skipped.  Curses still applies its own locale-selected width policy, so
  it can disagree with the registered bitmap's one- or two-cell width.
  Curses exposes normal and high-visibility cursor
  modes, but not a portable distinction between underline and bar cursors.
- Title, clipboard, URL, pointer, and fullscreen operations have no ncurses
  contract used here and return `IO_ERROR_NOT_SUPPORTED`.

## X11

- The backend uses the X input method selected for the caller's current
  `LC_CTYPE`.  An embedding application that never selects its user locale may
  therefore receive only the input support provided by Xlib's C locale.
- Opening a URL has no X11 protocol.  The backend does not guess a desktop
  launcher or spawn an external program and reports that operation as
  unsupported.

## Wayland

- The core Wayland data-device protocol permits a client to claim the
  clipboard only with a serial from a keyboard or pointer event.  Setting the
  clipboard before an instance has received such an event reports
  `IO_ERROR_NOT_SUPPORTED`; no fabricated or stale serial is sent.
- Opening a URL has no core Wayland or xdg-shell request.  The backend does not
  infer a particular desktop portal or launcher and reports the operation as
  unsupported.
