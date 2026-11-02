// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause

// status_default.wren — default Wren-driven status bar.
//
// Matches the historical C update_status() layout:
//
//   " name (flags) (speed) │ Conn   │ Connected: HH:MM:SS │ ALT-Z menu "
//                          ^^                                            ^
//                          fixed col 27..30 indicator overlay            menu's
//                          (log ‼, SFTP ↑, SFTP ↓, mouse M)              last
//                                                                        char
//                                                                        at width-2
//
// Right-anchored: the name field expands to push the menu hint to one
// cell from the right edge.  The 4-cell indicator block stays at fixed
// columns 27..30 (mirroring the C original) so individual indicators
// lighting up or going dark never shift any other content.
//
// To override: drop a same-named file in your auto-load dir, or set
// `Status.callable = ...` from any later script.

import "syncterm" for Transfer
import "syncterm" for Status, Surface, Screen, Session, Capture, CTerm, Host, Conn, ConnType

class StatusDefault {
  // Cell colour palette (RGB; matches the C original's vmem_cell.fg
  // values + the corresponding 4-bit legacy_attr fallback).
  static FG_NORMAL { 0xffff54 }       // bright yellow
  static FG_DIM    { 0x545454 }       // dim grey (mouse-disabled)
  static FG_RED    { 0xff5454 }       // bright red (log error)
  static SEP_CHAR  { "│" }
  static UP_CHAR   { "↑" }
  static DOWN_CHAR { "↓" }
  static LOG_CHAR  { "‼" }
  static M_CHAR    { "M" }

  // Indicator block is 4 cells wide.  Pinned to the right end of the
  // name area (just before the " │ ConnType " separator) so it never
  // wastes space when the name is shorter than the available room.

  // Write Unicode text, clipping at the surface's right edge. Returns
  // the number of display columns written.
  static writeStr_(surf, x, s) {
    var written = 0
    for (ch in s) {
      var col = x + written
      if (col < 0 || col >= surf.width) break
      var columns = Screen.textWidth(ch, surf[col].font)
      if (columns == 0) continue
      if (col + columns > surf.width) break
      surf[col].ch = ch
      written = written + columns
    }
    return written
  }

  // Place one Unicode character, preserving the pre-filled colour.
  static putChar_(surf, col, ch) {
    if (col < 0 || col >= surf.width) return
    surf[col].ch = ch
  }

  // Write " │ " (space, separator, space) starting at `col`.  Returns
  // the number of cells written (3 if it fits, fewer if clipped).
  static writeSep_(surf, col) {
    var w = surf.width
    var written = 0
    if (col < w) {
      putChar_(surf, col, " ")
      written = 1
    }
    if (col + 1 < w) {
      putChar_(surf, col + 1, StatusDefault.SEP_CHAR)
      written = 2
    }
    if (col + 2 < w) {
      putChar_(surf, col + 2, " ")
      written = 3
    }
    return written
  }

  // Right-pad a String with spaces to exactly `n` bytes (assumes
  // ASCII, like the connection-type names).  Truncates if longer.
  static padTo_(s, n) {
    var len = s.bytes.count
    if (len == n) return s
    if (len > n) {
      var bs = s.bytes
      var t = ""
      for (i in 0...n) t = t + String.fromCodePoint(bs[i])
      return t
    }
    var t = s
    var pad = n - len
    for (i in 0...pad) t = t + " "
    return t
  }

  // Compose the BBS name plus mode flags plus inline (speed),
  // matching the C original's nbuf assembly order: SAFE, Logging,
  // (speed), DrWy, OOTerm*, INV.  Speed comes from the live
  // throttleSpeed for network connections (Alt-Up/Down adjustable),
  // or the configured port rate for serial.
  static nameWithFlags_() {
    var n = Session.current.name
    if (Host.safeMode) n = n + " (SAFE)"
    if (Capture.active) n = n + " (Logging)"
    var ct = Session.current.profile.connType
    var isSerial = ct == ConnType.serial || ct == ConnType.serialNoRts
    var rate = isSerial ? Session.current.profile.bpsRate : CTerm.throttleSpeed
    if (rate > 0) n = n + " (%(rate))"
    if (CTerm.doorwayMode) n = n + " (DrWy)"
    var ooii = CTerm.ooiiMode
    if (ooii == 1) n = n + " (OOTerm)"
    if (ooii == 2) n = n + " (OOTerm1)"
    if (ooii == 3) n = n + " (OOTerm2)"
    if (CTerm.atasciiInverse) n = n + " (INV)"
    return n
  }

  // "HH:MM:SS", or "Too Long" past the 100-hour C clamp.
  static elapsedString_() {
    var s = Conn.elapsedSeconds
    if (s >= 360000) return "Too Long"
    if (s < 0) s = 0
    var hh = (s / 3600).floor
    var mm = ((s / 60) % 60).floor
    var ss = (s % 60).floor
    return "%(pad2_(hh)):%(pad2_(mm)):%(pad2_(ss))"
  }
  static pad2_(n) { n < 10 ? "0%(n)" : "%(n)" }

  // Complete text counterpart to the width-dependent visual bar.  This is
  // snapshotted when the disconnect notice appears, so elapsed time does not
  // continue advancing while the user waits or reads the popup.
  static details() {
    var lines = ["BBS: %(Session.current.name)", "Connection: %(Session.current.profile.connTypeName)"]
    var ct = Session.current.profile.connType
    var isSerial = ct == ConnType.serial || ct == ConnType.serialNoRts
    var rate = isSerial ? Session.current.profile.bpsRate : CTerm.throttleSpeed
    if (rate > 0) lines.add("Speed: %(rate) bps")
    lines.add("Connected: %(elapsedString_())")

    var modes = []
    if (Host.safeMode) modes.add("SAFE")
    if (Capture.active) modes.add("Logging")
    if (CTerm.doorwayMode) modes.add("DrWy")
    var ooii = CTerm.ooiiMode
    if (ooii == 1) modes.add("OOTerm")
    if (ooii == 2) modes.add("OOTerm1")
    if (ooii == 3) modes.add("OOTerm2")
    if (CTerm.atasciiInverse) modes.add("INV")
    if (modes.count > 0) lines.add("Modes: " + modes.join(", "))

    var activity = []
    if (Host.logUnreadError) activity.add("unread Wren errors")
    if (Host.logUnread && !Host.logUnreadError) activity.add("unread Wren log")
    if (Host.uploadArrow) activity.add("uploading")
    if (Host.downloadArrow) activity.add("downloading")
    if (activity.count > 0) lines.add("Activity: " + activity.join(", "))
    if (CTerm.mouseMode != 0) {
      lines.add(CTerm.mouseDisabled ? "Mouse: disabled" : "Mouse: enabled")
    }
    return lines.join("\n")
  }

  // Truncate text in display columns, reserving an ellipsis when it fits.
  static truncate_(s, room, font) {
    if (room <= 0) return ""
    if (Screen.textWidth(s, font) <= room) return s
    var limit = room < 4 ? room : room - 3
    var t = ""
    var width = 0
    for (ch in s) {
      var columns = Screen.textWidth(ch, font)
      if (columns == 0) continue
      if (width + columns > limit) break
      t = t + ch
      width = width + columns
    }
    return room < 4 ? t : t + "..."
  }

  // Indicators painted near the right end of the name area, in the
  // historical order: log ‼, SFTP ↑, SFTP ↓, mouse M.  `nameEnd`
  // is the rightmost column of the name area (1-indexed); the M
  // sits one cell in from that so a padding space buffers it from
  // the " │ " separator.  Each cell either gets its glyph + colour
  // or stays as the prefilled space — the indicator block reserves
  // no layout space, so other slots never shift when an indicator
  // lights up or goes dark.
  static paintIndicators_(surf, nameEnd) {
    if (nameEnd < 5) return
    paintLog_(surf,    nameEnd - 4)
    paintArrow_(surf,  nameEnd - 3, StatusDefault.UP_CHAR,   Host.uploadArrow)
    paintArrow_(surf,  nameEnd - 2, StatusDefault.DOWN_CHAR, Host.downloadArrow)
    paintMouse_(surf,  nameEnd - 1)
  }

  static paintLog_(surf, col) {
    if (col < 0 || col >= surf.width) return
    if (Host.logUnreadError) {
      var c = surf[col]
      c.ch = StatusDefault.LOG_CHAR
      c.fgRgb = StatusDefault.FG_RED
      c.legacyAttr = 0x1c
    } else if (Host.logUnread) {
      surf[col].ch = StatusDefault.LOG_CHAR
    }
  }

  static paintArrow_(surf, col, ch, lit) {
    if (lit) putChar_(surf, col, ch)
  }

  static paintMouse_(surf, col) {
    if (col < 0 || col >= surf.width) return
    if (CTerm.mouseMode == 0) return
    var c = surf[col]
    c.ch = StatusDefault.M_CHAR
    if (CTerm.mouseDisabled) {
      c.fgRgb = StatusDefault.FG_DIM
      c.legacyAttr = 0x18
    }
  }

  // Compute total right-side slot width (each non-empty contributes
  // 3 chars for " │ " plus its content).
  static rightWidth_(slots, font) {
    var total = 0
    for (s in slots) {
      if (s != "") total = total + 3 + Screen.textWidth(s, font)
    }
    return total
  }

  // The destination may differ from the visible label of an OSC 8 link.
  // Clip in display columns so Unicode URLs cannot split a wide glyph.
  static renderUri_(surf, uri) {
    var room = (surf.width - 2).max(0)
    if (room == 0) return
    var font = surf[0].font
    var truncated = Screen.textWidth(uri, font) > room
    var limit = truncated && room >= 3 ? room - 3 : room
    var shown = ""
    var width = 0
    for (ch in uri) {
      var columns = Screen.textWidth(ch, font)
      if (columns == 0) continue
      if (width + columns > limit) break
      shown = shown + ch
      width = width + columns
    }
    if (truncated && room >= 3) {
      shown = shown + "..."
      width = width + 3
    }
    var x = 1 + ((room - width) / 2).floor
    for (ch in shown) {
      var columns = Screen.textWidth(ch, font)
      if (columns == 0) continue
      surf[x].ch = ch
      x = x + columns
    }
  }

  // Render entry point: called by the host with a width×1 Surface
  // pre-filled to yellow-on-blue spaces.  Mutate in place.
  static render(surf) {
    if (!Status.enabled) return
    var w = surf.width
    var font = surf[0].font
    var uri = Status.hoverUri
    if (uri != null) {
      renderUri_(surf, uri)
      return
    }
    if (w < 8) return

    // Right-anchored slots after the name field.  At narrow widths
    // (< 80) drop elapsed-time and menu-hint slots.  Conn type is
    // padded to 6 chars so its column doesn't drift when "SSH" gives
    // way to "Telnet".
    var name = (Transfer.active && Transfer.inlineProgress) ? Transfer.snapshot()[1] : nameWithFlags_()
    var conn = padTo_(Session.current.profile.connTypeName, 6)
    var time = w >= 80 ? "Connected: %(elapsedString_())" : ""
    var menu = w >= 80 ? "%(Host.altKeyShort)-Z menu" : ""

    // Total width = 1 (leading space) + nameWidth + rightWidth + 1
    // (trailing space).  Solve for nameWidth.  If the name would
    // have to truncate to fit, drop "Connected: " first (saves 11
    // chars) before resorting to "..." -- the label is decoration,
    // the BBS name is information.
    var slots = [conn, time, menu]
    var rightWidth = rightWidth_(slots, font)
    var nameWidth = w - rightWidth - 2
    if (Screen.textWidth(name, font) > nameWidth && time != "") {
      time = elapsedString_()
      slots = [conn, time, menu]
      rightWidth = rightWidth_(slots, font)
      nameWidth = w - rightWidth - 2
    }
    if (nameWidth < 1) nameWidth = 1

    name = truncate_(name, nameWidth, font)

    // Paint name and pad to nameWidth with spaces.
    var col = 1
    col = col + writeStr_(surf, col, name)
    while (col < 1 + nameWidth) {
      putChar_(surf, col, " ")
      col = col + 1
    }
    // Right-side slots in order.
    for (s in slots) {
      if (s == "") continue
      col = col + writeSep_(surf, col)
      col = col + writeStr_(surf, col, s)
    }

    // Indicator overlay last so the glyphs land on top of the name
    // area's right edge (padding when name is shorter than the
    // available room; the name's tail characters when not).
    paintIndicators_(surf, nameWidth)
  }
}

Status.callable = Fn.new { |surf| StatusDefault.render(surf) }
Status.details = Fn.new { StatusDefault.details() }
