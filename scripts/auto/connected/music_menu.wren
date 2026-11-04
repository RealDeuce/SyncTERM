// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause

import "syncterm" for CTerm, Host, Input, Screen
import "ui_picker" for ListPicker

class MusicMenu {
  static run() {
    Fiber.new {
      Screen.checked_(Screen.modalRun(Fn.new {
        var picked = ListPicker.pickWithSelection("ANSI Music Setup",
            Host.musicHelp, Host.musicNames, CTerm.music)
        if (picked >= 0) CTerm.music = picked
      }))
    }.call()
  }
}
