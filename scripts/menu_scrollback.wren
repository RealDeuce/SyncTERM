import "syncterm" for Error
import "syncterm" for Cleanup_, Cell, Clipboard, Hyperlinks, Input, KeyEvent, MouseEvent,
    Key, Mouse, Screen, Surface
import "syncterm_menu" for Menu
import "ui_widget" for Widget, Rect
import "ui_draw" for Painter
import "ui_popup" for Alert
import "ui_style" for Style, Theme
import "ui_app" for App

class OfflineScrollbackCanvas is Widget {
  construct new(app, source, background, hasStatus, dismiss) {
    super()
    _app = app
    _source = source
    _background = background
    _hasStatus = hasStatus
    _dismiss = dismiss
    _top = 0
    _hoverId = 0
    _hoverUrl = null
    _cell = Cell.new()
  }

  terminalHeight_ {
    if (bounds == null) return 0
    return (bounds.h - (_hasStatus ? 1 : 0)).max(0)
  }

  maxTop_ { (_source.height - bounds.h).max(0) }

  bounds=(value) {
    super.bounds = value
    _top = maxTop_
  }

  sourceX_ { ((bounds.w - _source.width) / 2).floor }

  titleStyle_ {
    var base = style("title")
    return Style.new(base.font, base.legacyAttr + 0x80,
        base.fgRgb, base.bgRgb)
  }

  move_(delta) {
    _top = (_top + delta).max(0).min(maxTop_)
    if (_hoverId != 0 || _hoverUrl != null) {
      _hoverId = 0
      _hoverUrl = null
      Screen.optional_(Menu.setHyperlinkHover(0))
    }
    markDirty()
  }

  title_(x) {
    if (bounds.w <= 0 || x >= bounds.w) return
    Painter.text(surface, x.max(0), 0, "Scrollback", titleStyle_,
        (bounds.w - x.max(0)).min(10))
  }

  onPaint_() {
    if (_background != null) {
      surface.putRect(_background, 0, 0)
    } else {
      Painter.fill(surface, Rect.new(0, 0, bounds.w, bounds.h), " ",
          style("default"))
    }
    var sourceX = (-sourceX_).max(0)
    var width = (_source.width - sourceX).min(bounds.w)
    var rows = (_source.height - _top).min(bounds.h)
    if (width > 0 && rows > 0) {
      surface.putRect(_source, Rect.new(sourceX, _top, width, rows),
          sourceX_.max(0), 0)
    }
    title_(0)
    title_(_source.width - 10)
    if (_hoverUrl != null && bounds.h > 0) {
      var row = bounds.h - 1
      Painter.fill(surface, Rect.new(0, row, bounds.w, 1), " ",
          style("statusbar"))
      Painter.text(surface, 0, row, _hoverUrl, style("statusbar"), bounds.w)
    }
  }

  position_(event) {
    var x = event.startX - bounds.x - sourceX_
    var y = event.startY - bounds.y
    if (x < 0 || x >= _source.width || y < 0 ||
        y >= bounds.h) return null
    var row = _top + y
    if (row < 0 || row >= _source.height) return null
    return [x, row]
  }

  copied_(url) {
    Screen.checked_(Clipboard.setText(url))
    Alert.show(_app, "URL copied to clipboard", url)
  }

  activate_(event) {
    var position = position_(event)
    if (position == null) return
    if (!_source.cellAt(position[0], position[1], _cell)) return
    if (_cell.hyperlinkId != 0) {
      var result = Menu.openHyperlink(_cell.hyperlinkId)
      if (result is Error || !result) {
        var url = Hyperlinks[_cell.hyperlinkId]
        if (url != null) copied_(url)
      }
      return
    }
    var url = _source.urlAt(position[0], position[1])
    if (url != null) {
      var result = Menu.openUrl(url)
      if (result is Error || !result) copied_(url)
    }
  }

  hover_(event) {
    var id = 0
    var position = position_(event)
    if (position != null) {
      if (_source.cellAt(position[0], position[1], _cell)) {
        id = _cell.hyperlinkId
      }
    }
    var url = id == 0 ? null : Hyperlinks[id]
    if (id == _hoverId && url == _hoverUrl) return
    _hoverId = id
    _hoverUrl = url
    Screen.optional_(Menu.setHyperlinkHover(id))
    markDirty()
  }

  handle(event) {
    if (event is KeyEvent) {
      var key = event.shortcut_
      var cp = event.codepoint
      if (key == Key.escape) {
        _dismiss.call()
      } else if (key == Key.up || cp == 0x4A || cp == 0x6A) {
        move_(-1)
      } else if (key == Key.down || cp == 0x4B || cp == 0x6B) {
        move_(1)
      } else if (key == Key.pageUp) {
        move_(-bounds.h)
      } else if (key == Key.pageDown) {
        move_(bounds.h)
      } else if (cp == 0x48 || cp == 0x68) {
        move_(-terminalHeight_)
      } else if (cp == 0x4C || cp == 0x6C) {
        move_(terminalHeight_)
      } else if (key == Key.home) {
        move_(-_source.height)
      } else if (key == Key.end) {
        move_(_source.height)
      } else {
        return false
      }
      return true
    }
    if (event is MouseEvent) {
      if (event.event == Mouse.wheelUp) {
        move_(-event.wheelY.abs.max(1))
        return true
      }
      if (event.event == Mouse.wheelDown) {
        move_(event.wheelY.abs.max(1))
        return true
      }
      if (event.event == Mouse.button1Click) {
        activate_(event)
        return true
      }
      if (event.event == Mouse.move) {
        hover_(event)
        return true
      }
      if (event.event == Mouse.button1DragStart) {
        Input.mousedrag()
        return true
      }
    }
    return false
  }
}

class OfflineScrollbackView {
  static help_() {
    return "# Scrollback Buffer\n\n" +
        "J / Up Arrow\n:  Scroll up one line\n" +
        "K / Down Arrow\n:  Scroll down one line\n" +
        "H / Page Up\n:  Scroll up one screen\n" +
        "L / Page Down\n:  Scroll down one screen\n" +
        "Mouse Wheel\n:  Scroll up or down one line\n" +
        "Click\n:  Open a hyperlink or detected URL\n" +
        "Drag\n:  Copy selected text\n" +
        "Escape\n:  Close scrollback"
  }

  static show(app) {
    show_(app, false)
  }

  static showStandalone() {
    show_(App.new(), true)
  }

  static show_(app, standalone) {
    var source = Screen.checked_(Menu.offlineScrollback)
    if (source == null) return
    var savedScreen = Screen.checked_(Screen.save())
    var savedMouse = Input.mouseEvents
    Cleanup_.run(Fn.new {
      if (!Screen.checked_(Menu.prepareOfflineScrollback())) return
      Input.enableMouseEvent(Mouse.move)
      Input.enableMouseEvent(Mouse.button1Click)
      Input.enableMouseEvent(Mouse.button1DragStart)
      Input.enableMouseEvent(Mouse.button1DragMove)
      Input.enableMouseEvent(Mouse.button1DragEnd)
      Input.enableMouseEvent(Mouse.wheelUp)
      Input.enableMouseEvent(Mouse.wheelDown)
      Screen.optional_(Input.setMouseVisible(true))
      var size = Screen.checked_(Screen.size)
      var reserveRow = standalone && Menu.offlineScrollbackHasStatus
      if (reserveRow) {
        // Retain the status row as disconnected chrome without shrinking
        // sessions whose terminal originally used the entire screen.
        var row = Surface.new(size[0], 1)
        Painter.fill(row, Rect.new(0, 0, size[0], 1), " ",
            Theme.current.style("disconnected"))
        Screen.checked_(Screen.putRect(row, 1, size[1]))
      }
      var background = Screen.checked_(Screen.readRect(1, 1, size[0], size[1]))
      var dismiss = Fn.new {
        if (standalone) {
          app.quit()
        } else {
          app.popModal()
        }
      }
      var viewer = OfflineScrollbackCanvas.new(app, source, background,
          !standalone && Menu.offlineScrollbackHasStatus, dismiss)
      viewer.helpText = help_()
      viewer.keyHints = [["F1", "Help"], ["Arrows", "Scroll"],
          ["PgUp/PgDn", "Page"], ["Esc", "Close"]]
      viewer.focused = true
      viewer.bounds = Rect.new(1, 1, size[0], size[1] - (reserveRow ? 1 : 0))
      if (standalone) {
        app.pushModal(viewer)
        app.runSync()
      } else {
        app.modal(viewer)
      }
    }, Fn.new {
      Screen.cleanup_([
        Fn.new { Screen.optional_(Menu.setHyperlinkHover(0)) },
        Fn.new { Input.mouseEvents = savedMouse },
        Fn.new { Screen.checked_(Menu.finishOfflineScrollback()) },
        Fn.new { Screen.checked_(Screen.restore(savedScreen)) }
      ])
    })
  }
}
