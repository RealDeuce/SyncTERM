// DOS directory presentation; connection policy remains in main_menu.
// Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import "syncterm" for Screen, Text
import "syncterm_menu" for Menu
import "ui_widget" for Widget, Rect
import "ui_draw" for Painter
import "ui_pane" for Pane
import "ui_list" for ListView

class DosTable {
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
    if (graphics > 0 && graphics < 7) terminal = terminal + "/" + ["", "RIP1", "RIP3", "Sky1.3", "Sky3.1", "IGS", "BIG"][graphics]
    return terminal
  }

  static columns(width) {
    if (width < 60) return [["Name", (width * 0.42).floor], ["Address", width - (width * 0.42).floor - 1]]
    var columns = [["Name", 18], ["Address", 23], ["Port", 5], ["Type", 7], ["Term/Graphics", 14]]
    if (width >= 96) columns.add(["Encoding", 10])
    if (width >= 110) columns.add(["Calls", 6])
    if (width >= 126) columns.add(["Last Call", 10])
    var used = columns.count - 1
    for (c in columns) used = used + c[1]
    var extra = width - used
    columns[1][1] = columns[1][1] + extra
    if (columns[1][1] < 8) {
      columns[0][1] = columns[0][1] - (8 - columns[1][1])
      columns[1][1] = 8
    }
    return columns
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
  static details(entry) {
    if (entry == null) return "Insert adds a personal connection. F4 opens Settings."
    var p = entry.profile
    return "%(p.addr)  Port %(p.port)  %(MenuUiName.connection(p.connType))  %(terminal(p))  %(entry.sourceName == null ? "Personal" : entry.sourceName)"
  }
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
    focusable = false
  }
  onPaint_() {
    Painter.fill(surface, Rect.new(0, 0, bounds.w, 1), " ", style("title"))
    Painter.text(surface, _list.contentX_, 0, DosTable.header(_list.innerWidth), style("title"), _list.innerWidth)
  }
}

class DosSettingsPane is Pane {
  construct new(app, connected) {
    super()
    _app = app
    _result = null
    _list = ListView.new()
    _rows = []
    _connected = connected
    title = "Settings"
    focused = true
    keyHints = [["F1", "Help"], ["Enter", "Open"], ["Esc", "Back"]]
    onClose = Fn.new { _app.popModal() }
    add(_list)
  }
  rows=(rows) {
    _rows = rows
    _list.items = rows.map {|r| r[1] }.toList
    _list.onSelect = Fn.new {|index, item|
      _result = _rows[index][0]
      _app.popModal()
    }
  }
  result { _result }
  draw() {
    var size = Screen.checked_(Screen.size)
    bounds = Rect.new(1, 1, size[0], size[1])
    _list.bounds = innerBounds
    return super.draw()
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
    Painter.text(surface, 2, 0, Menu.applicationTitle, style("classic.header"), (bounds.w - 42).max(0))
    if (bounds.w >= 132) Painter.text(surface, bounds.w - 66, 0, Menu.timeText, style("classic.header"), 24)
  }
}

class DosFooter is Widget {
  construct new(hints, helpAvailable, onFocus, onCommit, onCancel) {
    super()
    focusable = false
    activitySensitive = false
    _hints = hints
    _paintedHints = null
    _details = ""
  }
  comment=(text) {
    _details = text
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
  onPaint_() {
    Painter.fill(surface, Rect.new(0, 0, bounds.w, bounds.h), " ", style("classic.hint"))
    Painter.text(surface, 1, 0, _details, style("classic.comment"), bounds.w - 2)
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
      Painter.text(surface, x, y, hint[0], style("classic.hotkey"), bounds.w - x)
      x = x + hint[0].count
      Painter.text(surface, x, y, " %(hint[1])  ", style("classic.hint"), bounds.w - x)
      x = x + hint[1].count + 3
    }
  }
}
