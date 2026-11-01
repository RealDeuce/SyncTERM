// Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
// No App import: this pane works inside an existing App or a connected modal.
import "syncterm" for Emoji, Key, KeyEvent, Mouse, MouseEvent, Screen, Text, Input
import "ui_pane" for Pane
import "ui_widget" for Rect
import "ui_draw" for Painter

class EmojiPicker is Pane {
  construct new(dismiss) {
    super()
    title = "Insert Emoji"
    titleAsBar = false
    helpable = false
    focused = true
    _dismiss = dismiss
    _query = ""
    _items = []
    _rows = []
    _itemRows = []
    _selected = 0
    _top = 0
    _columns = 1
    _visibleRows = 8
    _screenW = 0
    _screenH = 0
    _variants = null
    _back = null
    _result = null
    _message = null
    _savedMouse = null
    onClose = Fn.new { cancel_() }
    refresh_()
  }

  result { _result }
  query { _query }
  highlighted { _items.count == 0 ? null : _items[_selected] }
  restart(message) {
    _result = null
    _message = message
    markDirty()
  }

  finish() {
    if (_savedMouse != null) {
      Input.mouseEvents = _savedMouse
      _savedMouse = null
    }
  }

  parent=(value) {
    if (value == null) finish()
    super.parent = value
  }

  close_() {
    finish()
    if (_dismiss != null) _dismiss.call()
  }
  accept_() {
    if (highlighted == null) return
    _result = highlighted
    close_()
  }
  cancel_() {
    if (_back != null) {
      var selected = _back[1]
      _query = _back[0]
      _variants = null
      _back = null
      _message = null
      refresh_()
      for (i in 0..._items.count) {
        if (_items[i].id_ == selected) {
          _selected = i
          break
        }
      }
      reveal_()
      markDirty()
      return
    }
    _result = null
    close_()
  }

  addSection_(name, entries) {
    if (entries.count == 0) return
    _rows.add(name)
    var row = []
    for (entry in entries) {
      if (row.count == _columns) {
        _rows.add(row)
        row = []
      }
      _itemRows.add(_rows.count)
      row.add(_items.count)
      _items.add(entry)
    }
    if (row.count > 0) _rows.add(row)
  }

  refresh_() {
    var old = highlighted
    _items = []
    _rows = []
    _itemRows = []
    if (_variants != null) {
      var matches = _variants.where {|e| Text.caselessContains(e.name, _query) }.toList
      addSection_("Variants", matches)
    } else {
      var recent = Emoji.recent.where {|e| Text.caselessContains(e.name, _query) }.toList
      addSection_("Recently used", recent)
      addSection_(_query.count == 0 ? "All emoji" : "Search results", Emoji.search(_query))
    }
    _selected = 0
    if (old != null) {
      for (i in 0..._items.count) {
        if (_items[i].id_ == old.id_) {
          _selected = i
          break
        }
      }
    }
    _top = 0
    reveal_()
    markDirty()
  }

  reveal_() {
    if (_items.count == 0) return
    _selected = _selected.max(0).min(_items.count - 1)
    var row = _itemRows[_selected]
    if (row < _top) _top = row
    if (row >= _top + _visibleRows) _top = row - _visibleRows + 1
  }
  move_(amount) {
    if (_items.count == 0) return
    _message = null
    _selected = (_selected + amount).max(0).min(_items.count - 1)
    reveal_()
    markDirty()
  }

  variants_() {
    if (_variants != null || highlighted == null) return
    var entries = highlighted.variants
    if (entries.count <= 1) return
    _back = [_query, highlighted.id_]
    _query = ""
    _message = null
    _variants = entries
    refresh_()
  }

  footerActions_ {
    var actions = [["Enter Insert", 0]]
    if (_variants == null && highlighted != null && highlighted.variants.count > 1) {
      actions.add(["F2 Variants", 1])
    }
    actions.add(["Esc Back", 2])
    return actions
  }

  draw() {
    var size = Screen.size
    if (size[0] != _screenW || size[1] != _screenH) {
      _screenW = size[0]
      _screenH = size[1]
      var w = (size[0] - 2).max(8).min(78)
      var h = (size[1] - 2).max(8).min(25)
      bounds = Rect.new(((size[0] - w) / 2).floor + 1,
          ((size[1] - h) / 2).floor + 1, w, h)
      _columns = ((w - 3) / 3).floor.max(1)
      refresh_()
    }
    return super.draw()
  }

  nameLines_(name, width) {
    var lines = []
    var line = ""
    for (ch in name) {
      if (line.count > 0 && Screen.textWidth(line + ch, 0) > width) {
        lines.add(line)
        line = ""
      }
      line = line + ch
    }
    lines.add(line)
    return lines
  }

  onPaint_() {
    if (_savedMouse == null) {
      _savedMouse = Input.mouseEvents
      Input.enableMouseEvent(Mouse.move)
    }
    super.onPaint_()
    var sf = surface
    var width = bounds.w - 2
    Painter.text(sf, 1, 1, "Search: " + _query, style("input.focused"), width)
    var status = _message == null ? "%(_items.count) emoji" : _message
    Painter.text(sf, 1, 2, status, style("default"), width)
    var name = highlighted == null ? "No matching emoji" : highlighted.name
    var lines = nameLines_(name, width)
    var nameStart = (bounds.h - 2 - lines.count).max(4)
    _visibleRows = (nameStart - 3).max(1)
    reveal_()
    for (y in 0..._visibleRows) {
      var rowIndex = _top + y
      if (rowIndex < _rows.count) {
        var row = _rows[rowIndex]
        if (row is String) {
          Painter.text(sf, 1, 3 + y, row, style("title"), width)
        } else {
          Painter.fill(sf, Rect.new(1, 3 + y, width, 1), " ", style("list.item"))
          if (row.contains(_selected)) {
            // Neighbours share one padding cell. Paint the highlight once,
            // before drawing glyphs, so neither neighbour erases that space.
            var column = row.indexOf(_selected)
            Painter.fill(sf, Rect.new(1 + column * 3, 3 + y, 4, 1), " ", style("list.item.focused"))
          }
          for (column in 0...row.count) {
            var index = row[column]
            var st = style(index == _selected ? "list.item.focused" : "list.item")
            var cell = sf.cellAt(2 + column * 3, 3 + y)
            Painter.applyStyle(cell, st)
            cell.ch = _items[index].text
          }
        }
      }
    }
    for (i in 0...lines.count) {
      if (nameStart + i < bounds.h - 2) Painter.text(sf, 1, nameStart + i, lines[i], style("default"), width)
    }
    var hint = style("classic.hint")
    Painter.fill(sf, Rect.new(1, bounds.h - 2, width, 1), " ", hint)
    var x = 1
    for (action in footerActions_) {
      Painter.text(sf, x, bounds.h - 2, action[0], hint, (bounds.w - 1 - x).max(0))
      x = x + action[0].count + 2
    }
  }

  handle(event) {
    if (event is KeyEvent) {
      var c = event.code
      if (c == Key.escape) {
        cancel_()
        return true
      }
      if (c == Key.enter) {
        accept_()
        return true
      }
      if (c == Key.f2) {
        variants_()
        return true
      }
      if (c == Key.left) {
        move_(-1)
        return true
      }
      if (c == Key.right) {
        move_(1)
        return true
      }
      if (c == Key.up) {
        move_(-_columns)
        return true
      }
      if (c == Key.down) {
        move_(_columns)
        return true
      }
      if (c == Key.pageUp) {
        move_(-_columns * _visibleRows)
        return true
      }
      if (c == Key.pageDown) {
        move_(_columns * _visibleRows)
        return true
      }
      if (c == Key.home) {
        move_(-_items.count)
        return true
      }
      if (c == Key.end) {
        move_(_items.count)
        return true
      }
      if (c == Key.backspace || c == Key.ctrlU) {
        var chars = []
        for (ch in _query) chars.add(ch)
        if (chars.count > 0) chars.removeAt(-1)
        _query = c == Key.ctrlU ? "" : chars.join()
        _message = null
        refresh_()
        return true
      }
      if (event.codepoint != null && event.codepoint >= 0x20 && event.codepoint != 0x7f) {
        _query = _query + event.text
        _message = null
        refresh_()
        return true
      }
      return super.handle(event)
    }
    if (event is MouseEvent && bounds != null) {
      if (event.event == Mouse.wheelUp) {
        move_(-_columns)
        return true
      }
      if (event.event == Mouse.wheelDown) {
        move_(_columns)
        return true
      }
      if (event.event == Mouse.button1Click && bounds.contains(event.startX, event.startY) &&
          event.startY == bounds.y + bounds.h - 2) {
        var column = event.startX - bounds.x - 1
        var x = 0
        for (action in footerActions_) {
          if (column >= x && column < x + action[0].count) {
            if (action[1] == 0) accept_()
            if (action[1] == 1) variants_()
            if (action[1] == 2) cancel_()
            return true
          }
          x = x + action[0].count + 2
        }
        return true
      }
      if (event.event != Mouse.move && event.event != Mouse.button1Click) return super.handle(event)
      var x = event.startX - bounds.x - 1
      var y = event.startY - bounds.y - 3
      var rowIndex = _top + y
      if (x >= 0 && y >= 0 && y < _visibleRows && rowIndex < _rows.count) {
        var row = _rows[rowIndex]
        var column = (x / 3).floor
        if (row is List && column < row.count) {
          if (_selected != row[column]) {
            _selected = row[column]
            _message = null
            markDirty()
          }
          if (event.event == Mouse.button1Click) accept_()
          return true
        }
      }
    }
    return super.handle(event)
  }
}
