// Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import "syncterm" for Emoji, CTerm, Screen, Input, Conn
import "ui_app" for App
import "ui_emoji_picker" for EmojiPicker

class EmojiFlow {
  static run() {
    if (!Emoji.canSend) return false
    var screen = Screen.save()
    var suspended = CTerm.suspended
    var mouse = Input.mouseEvents
    var picked = null
    var pane = null
    // The synchronous modal has no yielding callbacks. This boundary owns
    // cleanup even when a user override or a UI handler aborts.
    var worker = Fiber.new {
      CTerm.suspended = true
      for (i in 0..4) Screen.font[i] = 0
      var app = App.new()
      app.onError = Fn.new {|failed| Fiber.abort(failed.error) }
      pane = EmojiPicker.new(Fn.new { app.quit() })
      app.root.add(pane)
      app.root.focusedIndex = 0
      app.runSync()
      picked = pane.result
    }
    worker.try()
    if (pane != null) pane.finish()
    Input.mouseEvents = mouse
    CTerm.suspended = suspended
    Screen.restore(screen)
    if (worker.error != null) {
      System.print("Emoji picker failed: %(worker.error)")
      return true
    }
    if (picked != null && Emoji.canSend) {
      var error = Conn.sendText(picked.text).await()
      if (error == null) {
        var saved = Emoji.recordUse(picked)
        if (saved != null) System.print(saved)
      } else {
        System.print("Emoji could not be sent: %(error)")
      }
    }
    return true
  }
}
