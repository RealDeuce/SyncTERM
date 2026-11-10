import "syncterm" for Cleanup_, CustomCursor, Screen, Surface, Input, Key, KeyEvent,
    PhysicalKeyEvent, Mod
import "menu_scrollback" for OfflineScrollbackView
import "menu_ui" for MenuUi
import "ui_app" for App
import "ui_help" for Help
import "ui_pane" for Pane
import "ui_progress" for ProgressText
import "ui_draw" for Painter
import "ui_widget" for Rect
import "ui_style" for Theme

class MenuHostUI {
  static statusWidth_(screenWidth, title, lines) {
    var longest = title == null ? 0 : Screen.checked_(Screen.textWidth(title, 0))
    for (line in lines) {
      var width = Screen.checked_(Screen.textWidth(line, 0))
      if (width > longest) longest = width
    }
    return (longest + 4).min((screenWidth - 4).max(1))
  }

  static alert(title, message) {
    MenuUi.alertStandalone(title, message)
  }

  static alertWithHelp(title, message, helpText) {
    MenuUi.alertStandalone(title, message, helpText)
  }

  static help(title, body) {
    var app = App.new()
    var p = Help.new(title, body)
    p.bounds = Help.centeredBounds_()
    p.onDismiss = Fn.new { |v| app.quit() }
    app.pushModal(p)
    app.runSync()
  }

  static disconnected(title, details) {
    var notice = "Disconnected: Enter exits, F1 details"
    while (true) {
      var size = Screen.checked_(Screen.size)
      var width = (size[0] - 1).max(0).min(notice.count)
      var start = ((size[0] - width) / 2).floor
      var row = Surface.new(size[0], 1)
      var style = Theme.current.style("disconnected")
      Painter.fill(row, Rect.new(0, 0, size[0], 1), " ", style)
      Painter.text(row, start, 0, notice, style, width)
      Screen.checked_(Screen.putRect(row, 1, size[1]))
      Screen.checked_(Screen.window.setBounds([1, 1, size[0], size[1]]))
      Screen.checked_(Screen.window.setPosition([start + width + 1, size[1]]))
      Screen.checked_(Screen.checked_(CustomCursor.normal).apply())
      var event = Input.next(100)
      var key = -1
      if (event is KeyEvent) key = event.shortcut_
      if (event is PhysicalKeyEvent) key = Key.physicalShortcut_(event)
      if (key == Key.enter || key == Key.quit) return
      if (key == Key.f1) {
        var saved = Screen.checked_(Screen.save())
        Cleanup_.run(Fn.new { help(title, details) },
            Fn.new { Screen.checked_(Screen.restore(saved)) })
      }
      if (key == Key.chord_(98, Mod.command)) {
        OfflineScrollbackView.showStandalone()
      }
    }
  }

  static confirm(title, message) {
    return MenuUi.confirmStandalone(title, message)
  }

  static prompt(title, message, initial, maxLen, masked) {
    return MenuUi.promptStandalone(title, message, initial, maxLen, masked)
  }

  static choice(title, message, options, current) {
    return MenuUi.choiceStandalone(title, message, options, current)
  }

  static ripChoice(title, message, options, hotkeys, required) {
    return MenuUi.choiceStandalone(title, message, options, 0, hotkeys, required)
  }

  static status(title, lines) {
    if (__statusApp == null) {
      __statusCursor = Screen.checked_(CustomCursor.current)
      __statusApp = App.new()
      __statusPane = Pane.new()
      __statusPane.frameKind = "display"
      __statusPane.focusable = false
      __statusPane.focused = true
      __statusPane.helpable = false
      __statusPane.closeable = false
      __statusPane.shadow = true
      __statusBody = ProgressText.new()
      __statusPane.add(__statusBody)
      __statusApp.root.add(__statusPane)
    }
    __statusPane.title = title
    __statusBody.lines = lines
    var size = Screen.checked_(Screen.size)
    var w = statusWidth_(size[0], title, lines)
    var rows = __statusBody.rowCount((w - 2).max(1))
    var h = (rows + 4).max(5).min(size[1] - 4)
    __statusPane.bounds = Pane.modalBounds(w, h)
    __statusBody.bounds = __statusPane.innerBounds
    __statusApp.drawAll_()
  }

  static statusClear() {
    var cursor = __statusCursor
    __statusCursor = null
    __statusBody = null
    __statusPane = null
    __statusApp = null
    if (cursor != null) Screen.checked_(cursor.apply())
  }
}
