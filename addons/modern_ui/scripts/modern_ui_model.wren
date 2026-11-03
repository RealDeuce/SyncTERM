// Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import "syncterm" for Host, IOErr
import "syncterm_menu" for Menu, MenuError
import "menu_ui" for MenuUi
import "menu_bbs_editor" for BbsEditor

// Own only password text across model loads; entry views expire on a successful full load.
class ModernModel {
  static password=(value) { __password = value }
  static prepare() {
    var status = Menu.load(__password)
    while (status != null && (status.code == IOErr.passwordRequired || status.code == IOErr.decryptionFailed)) {
      var message = status.code == IOErr.passwordRequired ? "Directory password" : "Incorrect password; try again"
      var password = null
      if (Host.exitRequested_) {
        password = MenuUi.promptStandaloneAtExit("Unlock directory", message, "", 1023, true, null)
      } else {
        password = MenuUi.promptStandalone("Unlock directory", message, "", 1023, true)
      }
      if (password == null) return false
      status = Menu.load(password)
      if (status == null) __password = password
    }
    if (status != null) {
      if (Host.exitRequested_) {
        MenuUi.alertStandaloneAtExit("Directory error", status.message)
      } else {
        MenuUi.alertStandalone("Directory error", status.message)
      }
      return false
    }
    return true
  }
  static offerSave(source, name) {
    if (!MenuUi.confirmStandaloneAtExit("Save connection", "Save this connection to your directory?")) return false
    while (!Menu.nameAvailable(name)) {
      name = MenuUi.promptStandaloneAtExit("Save connection", "Entry name", name, 30, false, null)
      if (name == null || name.count == 0) return false
    }
    var entry = Menu.copy(source, name)
    if (entry is MenuError) {
      MenuUi.alertStandaloneAtExit("Save connection", entry.message)
      return false
    }
    return BbsEditor.editStandaloneAtExit(entry, false, true)
  }
}
