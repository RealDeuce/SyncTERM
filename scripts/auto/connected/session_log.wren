// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause

import "syncterm" for Conn, Screen, Input, Key, KeyEvent
import "ui_app" for App
import "ui_help" for Help

class SessionLogView {
  static body_(text) {
    if (text == null || text == "") return "No session log messages have been recorded."
    // Each record is a separate wrapped line; log text is never markup.
    var lines = text.split("\n")
    if (lines[-1] == "") lines.removeAt(-1)
    return lines.map {|line|
      var escaped = line.replace("\\", "\\\\").replace("*", "\\*").replace("`", "\\`")
      return "- " + escaped
    }.join("\n")
  }

  static run() {
    Fiber.new {
      var text = Conn.sessionLog
      Screen.checked_(Screen.modalRun(Fn.new {
        var app = App.new()
        var pane = Help.new("Session Log", body_(text))
        pane.bounds = Help.centeredBounds_()
        pane.handle(KeyEvent.new(Key.end))
        pane.onDismiss = Fn.new {|value| app.quit() }
        app.onLayout = Fn.new {|w, h| pane.bounds = Help.centeredBounds_() }
        app.pushModal(pane)
        app.run()
      }))
    }.call()
  }
}
