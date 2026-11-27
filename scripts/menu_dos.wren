// DOS directory presentation; connection policy remains in main_menu.
// Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import "syncterm" for Screen, Text, Key, KeyEvent, Mouse, MouseEvent
import "syncterm_menu" for Menu
import "ui_widget" for Widget, Rect
import "ui_draw" for Painter

class DosTable {
  // Names allow 30 Unicode scalars / 60 UTF-8 bytes. Fifteen ASCII
  // scalars plus fifteen three-byte wide scalars occupy 45 cells.
  static nameWidth { 45 }
  static minimumWidth { nameWidth + 1 + 8 }
  static fit(text, width) {
    if (width <= 0) return ""
    text = text.toString
    var out = ""
    var used = 0
    for (part in Text.graphemes(text)) {
      var n = Screen.checked_(Screen.textWidth(part, 0))
      if (used + n > width) break
      out = out + part
      used = used + n
    }
    while (used < width) {
      out = out + " "
      used = used + 1
    }
    return out
  }

  static terminal(profile) {
    var mode = profile.screenMode
    var encoding = profile.encoding
    var terminal = "ANSI"
    if ((mode >= 17 && mode <= 19) || encoding == 24 || encoding == 25) terminal = "PETSCII"
    if (mode == 20 || mode == 21 || encoding == 23) terminal = "ATASCII"
    if (mode == 25 || mode == 26 || encoding == 26) terminal = "Viewdata"
    if ((mode >= 27 && mode <= 29) || encoding == 28) terminal = "VT52"
    var graphics = profile.graphics
    if (graphics > 0 && graphics < 8) terminal = terminal + "/" + ["", "RIP1", "RIP3", "Sky1.3", "Sky3.1", "IGS", "BIG", "DDO"][graphics]
    return terminal
  }

  static columns(width) {
    width = width.max(minimumWidth)
    var columns = [["Name", nameWidth], ["Address", 8]]
    var used = minimumWidth
    for (column in [["Port", 5], ["Type", 7], ["Term/Graphics", 14], ["Encoding", 10], ["Calls", 6], ["Last Call", 10]]) {
      if (used + column[1] + 1 > width) break
      columns.add(column)
      used = used + column[1] + 1
    }
    columns[1][1] = columns[1][1] + width - used
    return columns
  }

  static viewport(text, offset, width) {
    var out = ""
    var x = 0
    for (part in Text.graphemes(text)) {
      var n = Screen.checked_(Screen.textWidth(part, 0))
      if (x >= offset + width) break
      if (x >= offset) {
        out = out + part
      } else if (x + n > offset) {
        out = out + fit("", x + n - offset)
      }
      x = x + n
    }
    return fit(out, width)
  }

  static values(entry) {
    var p = entry.profile
    var last = entry.lastCallText
    return [entry.name, p.addr, p.port, MenuUiName.connection(p.connType), terminal(p),
        MenuUiName.encoding(p.encoding), entry.calls, last]
  }

  static line(values, width) {
    var columns = columns(width)
    var cells = []
    for (i in 0...columns.count) cells.add(fit(values[i], columns[i][1]))
    return cells.join(" ")
  }
  static header(width) { line(columns(width).map {|c| c[0] }.toList, width) }
  static row(entry, width) { line(values(entry), width) }
}

class MenuUiName {
  static connection(value) {
    for (row in Menu.connectionTypes) if (row[0] == value) return row[1]
    return value.toString
  }
  static encoding(value) {
    for (row in Menu.encodings) if (row[0] == value) return row[1]
    return value.toString
  }
}

class DosHeading is Widget {
  construct new(list) {
    super()
    _list = list
    _offset = null
    focusable = false
  }
  draw() {
    var offset = _list.horizontalOffset
    if (offset != _offset) {
      _offset = offset
      markDirty()
    }
    return super.draw()
  }
  onPaint_() {
    Painter.fill(surface, Rect.new(0, 0, bounds.w, 1), " ", style("title"))
    var text = DosTable.viewport(DosTable.header(_list.innerWidth), _list.horizontalOffset, _list.innerWidth)
    Painter.text(surface, _list.contentX_, 0, text, style("title"), _list.innerWidth)
  }
}

class DosBackdrop is Widget {
  construct new() {
    super()
    focusable = false
    activitySensitive = false
  }
  onPaint_() {
    Painter.fill(surface, Rect.new(0, 0, bounds.w, bounds.h), " ", style("default"))
    Painter.fill(surface, Rect.new(0, 0, bounds.w, 1), " ", style("classic.header"))
    var titleWidth = bounds.w - (bounds.w >= 80 ? 30 : 4)
    Painter.text(surface, 2, 0, Menu.applicationTitle, style("classic.header"), titleWidth.max(0))
    if (bounds.w >= 80) Painter.text(surface, bounds.w - 26, 0, Menu.timeText, style("classic.header"), 24)
  }
}

class DosFooter is Widget {
  construct new(hints, helpAvailable, onFocus, onCommit, onCancel) {
    super()
    activitySensitive = false
    _hints = hints
    _paintedHints = null
    _comment = ""
    _regions = []
    _selectedHint = 0
    _onActivate = null
  }
  onActivate=(fn) { _onActivate = fn }
  cursorVisible { false }
  comment=(text) {
    _comment = text
    markDirty()
  }
  draw() {
    var hints = _hints.call()
    if (hints != _paintedHints) {
      _paintedHints = hints
      markDirty()
    }
    return super.draw()
  }
  layoutHints_() {
    _regions = []
    var x = 1
    var y = 1
    var hints = _paintedHints
    if (hints == null) return
    for (hint in hints) {
      var length = hint[0].count + hint[1].count + 3
      if (x + length >= bounds.w && x > 1) {
        x = 1
        y = y + 1
      }
      if (y >= bounds.h) break
      _regions.add([Rect.new(x, y, (length - 2).min(bounds.w - x), 1), hint])
      x = x + length
    }
    _selectedHint = _selectedHint.min((_regions.count - 1).max(0))
  }
  onPaint_() {
    layoutHints_()
    Painter.fill(surface, Rect.new(0, 0, bounds.w, bounds.h), " ", style("classic.hint"))
    Painter.fill(surface, Rect.new(0, 0, bounds.w, 1), " ", style("classic.comment"))
    var width = Screen.checked_(Screen.textWidth(_comment, 0))
    var available = (bounds.w - 4).max(1)
    var offset = 2 + ((available - width) / 2).floor.max(0)
    Painter.text(surface, offset, 0, _comment, style("classic.comment"), available - (offset - 2))
    for (i in 0..._regions.count) {
      var rect = _regions[i][0]
      var hint = _regions[i][1]
      var active = focused && i == _selectedHint
      var labelStyle = style(active ? "button.focused" : "classic.hint")
      var keyStyle = style(active ? "button.focused.hotkey" : "classic.hotkey")
      Painter.text(surface, rect.x, rect.y, hint[0], keyStyle, rect.w)
      Painter.text(surface, rect.x + hint[0].count, rect.y, " %(hint[1])", labelStyle, (rect.w - hint[0].count).max(0))
    }
  }
  handle(ev) {
    if (ev is KeyEvent && _regions.count > 0) {
      var c = ev.shortcut_
      if (c == Key.left || c == Key.right || c == Key.up || c == Key.down) {
        var delta = c == Key.left || c == Key.up ? -1 : 1
        _selectedHint = (_selectedHint + delta + _regions.count) % _regions.count
        markDirty()
        return true
      }
      if (c == Key.enter || ev.codepoint == 0x20) {
        if (_onActivate != null) _onActivate.call(_regions[_selectedHint][1][0])
        return true
      }
    }
    if (ev is MouseEvent && ev.event == Mouse.button1Click && bounds.contains(ev.startX, ev.startY)) {
      for (region in _regions) {
        if (region[0].contains(ev.startX - bounds.x, ev.startY - bounds.y)) {
          if (_onActivate != null) _onActivate.call(region[1][0])
          return true
        }
      }
      return true
    }
    return false
  }
}
