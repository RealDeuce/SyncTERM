import "syncterm" for Screen
// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause

import "syncterm" for Hook, Key, Host, Mod
import "wren_console" for WrenConsole

if (!Screen.checked_(Host.textTerminal)) {
  Hook.onPhysicalShortcut_(96, Mod.control) {|key|
    WrenConsole.run()
    return true
  }
}
