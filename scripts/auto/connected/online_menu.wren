// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause

import "syncterm" for Conn, CTerm, Hook, Host, Input, Key, Screen, Emoji, Mod
import "ui_picker" for ListPicker
import "connected" for Connected
import "music_menu" for MusicMenu
import "capture_menu" for CaptureMenu
import "font_pick" for FontApp
import "scrollback_view" for ScrollbackView
import "wren_console" for WrenConsole
import "session_log" for SessionLogView
import "transfer_pick" for TransferFlow
import "emoji_pick" for EmojiFlow

class OnlineMenu {
  static logLevel_() {
    var picked = -1
    Screen.checked_(Screen.modalRun(Fn.new {
      picked = ListPicker.pickWithSelection("Log Level",
          "# Log Level\n\nSelect the transfer diagnostic threshold for this connection.",
          Host.logLevelNames, Host.logLevel)
    }))
    if (picked >= 0) Host.logLevel = picked
  }

  static outputRate_() {
    var rates = Host.outputRates
    var names = Host.outputRateNames
    var picked = -1
    Screen.checked_(Screen.modalRun(Fn.new {
      picked = ListPicker.pick("Output Rate",
          "# Output Rate\n\nLimit the remote display rate, or use Current for no limit.",
          names)
    }))
    if (picked >= 0) CTerm.throttleSpeed = rates[picked]
  }

  static entries_() {
    var entries = [
      ["Scrollback", Fn.new { ScrollbackView.run() }],
      ["Disconnect", Fn.new { Conn.endSession(false) }],
      ["Send Login", Fn.new { Connected.sendLogin() }],
      ["Upload", Fn.new { TransferFlow.upload() }],
      ["Download", Fn.new { TransferFlow.download() }],
      ["ANSI Music Control", Fn.new { MusicMenu.run() }],
      ["Capture Control", Fn.new { CaptureMenu.run() }],
      ["Font Setup", Fn.new { FontApp.run() }],
      ["Output Rate", Fn.new { outputRate_() }],
      ["Log Level", Fn.new { logLevel_() }],
      ["View Session Log (%(Host.altKeyShort)-V)", Fn.new { SessionLogView.run() }],
      ["Toggle Doorway Mode", Fn.new {
        CTerm.doorwayMode = !CTerm.doorwayMode
      }],
      ["Toggle Operation Overkill ][ Mode", Fn.new {
        CTerm.ooiiMode = (CTerm.ooiiMode + 1) % (Host.maxOOIIMode + 1)
      }],
      ["Toggle Remote Mouse", Fn.new {
        CTerm.mouseDisabled = !CTerm.mouseDisabled
      }],
      ["Wren Console", Fn.new { WrenConsole.run() }],
      ["Edit Dialing Directory", Fn.new { Host.editBBSList() }],
      ["Exit", Fn.new { Conn.endSession(true) }],
    ]
    if (Emoji.canSend) entries.insert(1, ["Insert Emoji...", Fn.new { EmojiFlow.run() }])
    return entries
  }

  static run() {
    Fiber.new {
      var entries = entries_()
      var picked = -1
      Screen.checked_(Screen.modalRun(Fn.new {
        picked = ListPicker.pick("SyncTERM Online Menu",
            "# Online Menu\n\nChoose a connected-session control.",
            entries.map {|entry| entry[0] }.toList)
      }))
      if (picked >= 0) entries[picked][1].call()
    }.call()
  }
}

Hook.onCommand_(122) {|key|
  OnlineMenu.run()
  return true
}
if (Screen.checked_(Host.textTerminal)) {
  Hook.onKey(115, Mod.control) {|key|
    OnlineMenu.run()
    return true
  }
}
