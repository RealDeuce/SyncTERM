// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause

import "syncterm" for Hook, Key, Mouse, Host, CTerm, Input, Conn
import "disconnect_flow" for DisconnectFlow
import "scrollback_view" for ScrollbackView
import "music_menu" for MusicMenu
import "capture_menu" for CaptureMenu
import "font_pick" for FontApp
import "session_log" for SessionLogView
import "emoji_pick" for EmojiFlow

Hook.onKey(Key.altP) {|key| EmojiFlow.run() }

Hook.onKey(Key.altV) { |key|
  SessionLogView.run()
  return true
}

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

Hook.onMouse(Mouse.button1DragStart) {|event|
  var mode = CTerm.mouseMode
  if (CTerm.mouseDisabled || mode == 0 || mode == 9) {
    Input.mousedrag()
    return true
  }
  return false
}

var pasteOnClick = Fn.new {|event|
  if (CTerm.mouseDisabled || CTerm.mouseMode == 0) {
    Conn.paste()
    return true
  }
  return false
}
Hook.onMouse(Mouse.button2Click, pasteOnClick)
Hook.onMouse(Mouse.button3Click, pasteOnClick)

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
