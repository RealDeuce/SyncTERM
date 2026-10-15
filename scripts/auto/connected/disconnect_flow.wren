// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause

import "syncterm" for Conn, Input, Screen
import "ui_app" for App
import "ui_popup" for Confirm

class DisconnectFlow {
  static run(exitApp) {
    Fiber.new {
      Screen.modalRun(Fn.new {
        var app = App.new()
        var message = "Disconnect... Are you sure?\n\n" +
                      "Selecting Yes closes the connection."
        if (Confirm.runStandalone(app, message)) Conn.endSession(exitApp)
      })
      Input.setupMouseEvents()
    }.call()
  }
}
