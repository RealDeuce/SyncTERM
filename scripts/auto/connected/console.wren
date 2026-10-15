// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause

import "syncterm" for Hook, Key, Host
import "wren_console" for WrenConsole

if (!Host.textTerminal) {
  Hook.onKey(Key.wrenConsole) {|key|
    WrenConsole.run()
    return true
  }
}
