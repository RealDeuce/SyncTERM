// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause

import "syncterm" for Cleanup_, Scrollback, Surface, Screen, Input, Key, Mouse, KeyEvent, MouseEvent, CTerm, Hook
import "ui_app" for App
import "ui_help" for Help

class ScrollbackView {
  static run() {
    Fiber.new {
      var savedMouse = Input.mouseEvents
      Cleanup_.run(Fn.new {
        Input.mouseEvents = 0
        Input.enableMouseEvent(Mouse.button1DragStart)
        Input.enableMouseEvent(Mouse.button1DragMove)
        Input.enableMouseEvent(Mouse.button1DragEnd)
        Input.enableMouseEvent(Mouse.wheelUpPress)
        Input.enableMouseEvent(Mouse.wheelDownPress)
        Screen.checked_(Screen.modalRun(Fn.new {
          var width = Scrollback.width
          var height = CTerm.height
          var x = CTerm.originX
          var y = CTerm.originY
          if (width <= 0 || height <= 0) return
          var live = Screen.checked_(Screen.readRect(x, y, x + width - 1, y + height - 1))
          if (live == null) return
          var viewport = Surface.new(width, height)
          var historyHeight = Scrollback.height
          blankSurroundings_()
          viewLoop_(viewport, live, historyHeight, x, y)
        }))
      }, Fn.new { Input.mouseEvents = savedMouse })
    }.call()
  }

  static blankSurroundings_() {
    Screen.checked_(Screen.setAttr(0x1e))
    var size = Screen.checked_(Screen.size)
    Screen.checked_(Screen.window.setBounds([1, 1, size[0], size[1]]))
    Screen.checked_(Screen.window.clear())
  }

  static viewLoop_(viewport, live, historyHeight, x, y) {
    var height = live.height
    var maxTop = historyHeight
    var top = maxTop
    while (true) {
      paint_(viewport, live, historyHeight, top, x, y)
      var event = Input.next()
      if (event is KeyEvent) {
        var key = event.shortcut_
        if (key == Key.escape || key == Key.quit || key == 0x71 || key == 0x51) return
        if (key == Key.up || key == 0x6B || key == 0x4B) {
          top = (top > 0) ? top - 1 : 0
        } else if (key == Key.down || key == 0x6A || key == 0x4A) {
          if (top == maxTop) return
          top = top + 1
        } else if (key == Key.pageUp || key == 0x68 || key == 0x48) {
          top = (top - height).max(0)
        } else if (key == Key.pageDown || key == 0x6C || key == 0x4C) {
          top = (top + height).min(maxTop)
        } else if (key == Key.home) {
          top = 0
        } else if (key == Key.end) {
          top = maxTop
        } else if (key == Key.f1) {
          showHelp_()
        }
      } else if (event is MouseEvent) {
        var mouse = event.event
        if (mouse == Mouse.wheelUpPress) {
          top = (top > 0) ? top - 1 : 0
        } else if (mouse == Mouse.wheelDownPress) {
          if (top == maxTop) return
          top = top + 1
        } else if (mouse == Mouse.button1DragStart) {
          Input.mousedrag()
        }
      }
    }
  }

  static paint_(viewport, live, historyHeight, top, x, y) {
    var copied = 0
    var height = live.height
    var width = viewport.width
    if (top < historyHeight) {
      copied = Screen.checked_(Scrollback.copyRowsTo(viewport, top,
          (historyHeight - top).min(height), 0))
    }
    if (copied < height) {
      var liveRow = top + copied - historyHeight
      viewport.putRect_(live, 0, liveRow, width, height - copied, 0, copied)
    }
    Screen.checked_(Screen.putRect(viewport, x, y))
    Screen.checked_(Screen.setAttr(0x9e))
    Screen.checked_(Screen.window.setPosition([x, y]))
    Screen.checked_(Screen.window.print("Scrollback"))
    if (width >= 10) {
      Screen.checked_(Screen.window.setPosition([x + width - 10, y]))
      Screen.checked_(Screen.window.print("Scrollback"))
    }
    Screen.checked_(Screen.window.setPosition([x, y]))
  }

  static showHelp_() {
    Screen.checked_(Screen.modalRun(Fn.new {
      var app = App.new()
      var help = Help.new("Scrollback Help",
        "Up / k\n:  Scrolls up one line\n" +
        "Down / j\n:  Scrolls down one line\n" +
        "PgUp / h\n:  Scrolls up one screen\n" +
        "PgDn / l\n:  Scrolls down one screen\n" +
        "Home\n:  Jumps to the oldest row\n" +
        "End\n:  Jumps to the newest row\n" +
        "Mouse wheel\n:  Scrolls one line\n" +
        "Esc / q\n:  Returns to the terminal\n")
      help.bounds = Help.centeredBounds_()
      help.onDismiss = Fn.new {|value| app.quit() }
      app.pushModal(help)
      app.runSync()
    }))
  }
}

Hook.onMouse(Mouse.wheelUpPress) {|event|
  var mode = CTerm.mouseMode
  if (mode == 0 || mode == 1 || mode == 9) {
    ScrollbackView.run()
    return true
  }
  return false
}
