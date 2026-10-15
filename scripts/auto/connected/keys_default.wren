// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause

import "syncterm" for Hook, Key, Host, CTerm, Input, Conn
import "disconnect_flow" for DisconnectFlow
import "scrollback_view" for ScrollbackView
import "music_menu" for MusicMenu
import "capture_menu" for CaptureMenu
import "font_pick" for FontApp

Hook.onKey(Key.altX) { |key|
  DisconnectFlow.run(true)
  return true
}

Hook.onKey(Key.altB) { |key|
  ScrollbackView.run()
  return true
}

Hook.onKey(Key.altM) { |key|
  MusicMenu.run()
  return true
}

Hook.onKey(Key.altC) { |key|
  CaptureMenu.run()
  return true
}

Hook.onKey(Key.altE) { |key|
  Fiber.new { Host.editBBSList() }.call()
  return true
}

Hook.onKey(Key.altF) { |key|
  FontApp.run()
  return true
}

Hook.onKey(Key.altO) { |key|
  CTerm.mouseDisabled = !CTerm.mouseDisabled
  Input.setupMouseEvents()
  return true
}

Hook.onKey(Key.altUp) { |key|
  CTerm.throttleSpeedUp()
  return true
}

Hook.onKey(Key.altDown) { |key|
  CTerm.throttleSpeedDown()
  return true
}

Hook.onKey(Key.shiftIns) { |key|
  Conn.paste()
  return true
}
Hook.onKey(Key.altH) { |key|
  DisconnectFlow.run(false)
  return true
}
if (Host.localControlKeys) {
  Hook.onKey(Key.ctrlQ) { |key|
    DisconnectFlow.run(false)
    return true
  }
}
