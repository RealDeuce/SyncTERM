// The door edits a DOS byte stream. Keep each line's original terminator
// so opening an older message does not normalize unrelated text.
import "syncterm" for Codepage, Key, KeyEvent
import "ui_app" for App
import "ui_draw" for Painter
import "ui_widget" for Rect, Widget

class DdoTextEditor is Widget {
  construct new(app, name, contents, limit) {
    super()
    _app = app
    _title = "Editing %(name)"
    _limit = limit
    _length = contents.bytes.count
    _lines = [[]]
    _breaks = []
    _row = 0
    _col = 0
    _top = 0
    _left = 0
    _result = null

    var bytes = contents.bytes
    var i = 0
    while (i < _length) {
      var b = bytes[i]
      i = i + 1
      if (b == 13 || b == 10) {
        if (b == 13 && i < _length && bytes[i] == 10) {
          _breaks.add("\r\n")
          i = i + 1
        } else {
          _breaks.add(String.fromByte(b))
        }
        _lines.add([])
      } else {
        _lines[-1].add(b)
      }
    }
  }

  static edit(name, contents, limit) {
    var app = App.new()
    var editor = DdoTextEditor.new(app, name, contents, limit)
    app.onLayout = Fn.new {|w, h| editor.bounds = Rect.new(1, 1, w, h) }
    app.pushModal(editor)
    app.runSync()
    return editor.result
  }

  result { _result }
  cursorVisible { bounds.h > 2 }
  cursorPos { cursorVisible ? [bounds.x + _col - _left, bounds.y + 1 + _row - _top] : null }
  cursorShape { "normal" }

  onPaint_() {
    var width = bounds.w
    var height = (bounds.h - 2).max(0)
    _left = _left.min(_col).max(_col - width + 1).max(0)
    _top = _top.min(_row).max(_row - height + 1).max(0)
    var body = style("input")
    var bar = style("statusbar")
    Painter.fill(surface, Rect.new(0, 0, width, bounds.h), " ", body)
    Painter.text(surface, 0, 0, _title, bar, width)
    for (y in 0...height.min(_lines.count - _top)) {
      var line = _lines[_top + y]
      var chars = []
      var end = (_left + width).min(line.count)
      for (x in _left.min(end)...end) {
        var b = line[x]
        chars.add(b == 0 ? " " : Codepage.decodeByte(Codepage.cp437, b))
      }
      Painter.text(surface, 0, y + 1, chars.join(), body, width)
    }
    if (bounds.h > 1) {
      Painter.text(surface, 0, bounds.h - 1, "F2 Save and exit   Esc Discard and exit", bar, width)
    }
  }

  save_() {
    var bytes = []
    for (i in 0..._lines.count) {
      bytes.addAll(_lines[i])
      if (i < _breaks.count) bytes.addAll(_breaks[i].bytes)
    }
    // Wren joins by repeated concatenation. Bound the intermediate strings
    // even when a door supplies one long unwrapped line.
    var chunks = []
    var start = 0
    while (start < bytes.count) {
      var end = (start + 256).min(bytes.count)
      chunks.add(bytes[start...end].map {|b| String.fromByte(b) }.join())
      start = end
    }
    _result = chunks.join()
    _app.quit()
  }

  right_() {
    if (_col < _lines[_row].count) {
      _col = _col + 1
    } else if (_row + 1 < _lines.count) {
      _row = _row + 1
      _col = 0
    }
  }

  left_() {
    if (_col > 0) {
      _col = _col - 1
    } else if (_row > 0) {
      _row = _row - 1
      _col = _lines[_row].count
    }
  }

  delete_() {
    var line = _lines[_row]
    if (_col < line.count) {
      line.removeAt(_col)
      _length = _length - 1
    } else if (_row < _breaks.count) {
      _length = _length - _breaks.removeAt(_row).bytes.count
      line.addAll(_lines.removeAt(_row + 1))
    }
  }

  handle(event) {
    if (!(event is KeyEvent)) return false
    var k = event.shortcut_
    var line = _lines[_row]
    if (k == Key.f2) {
      save_()
    } else if (k == Key.escape) {
      _app.quit()
    } else if (k == Key.left) {
      left_()
    } else if (k == Key.right) {
      right_()
    } else if (k == Key.home) {
      _col = 0
    } else if (k == Key.end) {
      _col = line.count
    } else if (k == Key.up || k == Key.down || k == Key.pageUp || k == Key.pageDown) {
      var step = (k == Key.pageUp || k == Key.pageDown) ? (bounds.h - 2).max(1) : 1
      _row = (_row + ((k == Key.up || k == Key.pageUp) ? -step : step)).clamp(0, _lines.count - 1)
      _col = _col.min(_lines[_row].count)
    } else if (k == Key.delete || k == Key.delChar) {
      delete_()
    } else if (k == Key.backspace) {
      if (_row > 0 || _col > 0) {
        left_()
        delete_()
      }
    } else if (k == Key.enter) {
      if (_length + 2 <= _limit) {
        _lines[_row] = line[0..._col]
        _lines.insert(_row + 1, line[_col...line.count])
        _breaks.insert(_row, "\r\n")
        _row = _row + 1
        _col = 0
        _length = _length + 2
      }
    } else {
      var cp = event.codepoint
      if (cp == null || cp < 32 || cp == 127) return false
      var b = Codepage.encodeByte(Codepage.cp437, cp)
      if (b != null && _length < _limit) {
        line.insert(_col, b)
        _col = _col + 1
        _length = _length + 1
      }
    }
    markDirty()
    return true
  }
}
